/* Submit BACKDRP2 tiles as SKY-stage scene billboards at a fixed camera-local
 * distance. Camera translation is excluded and reversed-Z far depth keeps
 * scene geometry in front. Direction decoding mirrors draw_wall in
 * src/tie/backdrp2.c. */

#include "tie_remaster/flight/backdrops.h"
#include <stdio.h>

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "aeron/aeron.h"
#include "aeron/log.h"
#include "aeron/render.h"
#include "aeron/scene/billboard.h"

#include "tie_remaster/flight/render_math.h"
#include "tie_remaster/flight/renderer_internal.h"
#include "tie_remaster/flight/sprite_cache.h"
#include "tie_remaster/gpu_debug.h"
#include "tie_remaster/scene2d/types.h"
#include "tie_runtime/snapshot/snapshot.h"

/* Planet runtime species slots. The remaster atlas stores their variants as
 * frames; original modes resolve the already-selected XACT request. */
#define BACKDROP_PLANET_SPECIES_LO 114
#define BACKDROP_PLANET_SPECIES_HI 116
/* Galaxy/cluster/star filler species map 1:1 to their selected atlas. */
#define BACKDROP_FILLER_SPECIES_LO 117
#define BACKDROP_FILLER_SPECIES_HI 126

/* Cube half-extent in the decode lattice (outward = worldeye>>2 = 8/32; the
 * in-plane offsets run 0..7). */
#define BACKDROP_CUBE_HALF 8.0f

/* SKY forces far depth; this distance only keeps clip.w positive and stable. */
#define BACKDROP_FAR_DIST 65536.0f

struct TieFlightBackdrop {
	TieFlightSpriteCache* sprites;
};

/* Per-face axis assignment (mirror of backdrp2.c draw_wall): axis 0=X,1=Y,2=Z.
 * normal_sign applies to the outward (±CUBE_HALF) component. */
static const struct {
	int normal_axis, normal_sign, prim_axis, sec_axis;
} BD_FACE[6] = {
	/* front  */ { 1, +1, 0, 2 },
	/* back   */ { 1, -1, 0, 2 },
	/* left   */ { 0, +1, 1, 2 },
	/* right  */ { 0, -1, 1, 2 },
	/* top    */ { 2, +1, 1, 0 },
	/* bottom */ { 2, -1, 1, 0 },
};

/* Decode a tile's world direction from its face and packed position byte.
 * NOTE: axis labels A/B/C -> X/Y/Z is the calibration knob — verify against
 * a known mission (B10M1GW: a planet dead-centre on the front face). */
static void TieFlightBackdrop_BdDecodeDir(int face, uint8_t pos, float out[3]) {
	const int prim_idx = pos & 0x07;
	const int prim_sign = (pos & 0x08) ? -1 : +1;
	const int sec_idx = (pos >> 4) & 0x07;
	const int sec_sign = (pos & 0x80) ? -1 : +1;

	out[0] = out[1] = out[2] = 0.0f;
	out[BD_FACE[face].normal_axis] += BD_FACE[face].normal_sign * BACKDROP_CUBE_HALF;
	out[BD_FACE[face].prim_axis] += (float)(prim_sign * prim_idx);
	out[BD_FACE[face].sec_axis] += (float)(sec_sign * sec_idx);
}

TieFlightBackdrop* TieFlightBackdrop_Create(TieFlightSpriteCache* sprites) {
	if (!sprites)
		return NULL;
	TieFlightBackdrop* b = (TieFlightBackdrop*)calloc(1, sizeof *b);
	if (!b)
		return NULL;
	b->sprites = sprites;
	return b;
}

void TieFlightBackdrop_Destroy(TieFlightBackdrop* b) {
	if (!b)
		return;
	free(b);
}

/* clip = view_proj · (dir, 0); fills clip[xyw] — front-of-camera gate. */
static inline void TieFlightBackdrop_ProjectDir(const float m[16], const float d[3], float out[3]) {
	out[0] = m[0] * d[0] + m[1] * d[1] + m[2] * d[2];
	out[1] = m[4] * d[0] + m[5] * d[1] + m[6] * d[2];
	out[2] = m[12] * d[0] + m[13] * d[1] + m[14] * d[2]; /* clip.w */
}

bool TieFlightBackdrop_Submit(TieFlightBackdrop* b, AeronScene3D* scene, AeronCommandBuffer* cmd,
							  const TieSnapshot* snap, const TieFlightCamera* fcam,
							  TieFlightLegacyRenderConvention convention) {
	if (!b || !scene || !cmd || !snap || !fcam)
		return false;
	const TieBackdropSet* bd = &snap->backdrops;
	if (!bd->draw_enabled)
		return false;

	/* Face slot ranges in [front, back, left, right, top, bottom] order. */
	const uint16_t cnt[6] = { bd->front_cnt, bd->back_cnt, bd->left_cnt,
							  bd->right_cnt, bd->top_cnt,  bd->bottom_cnt };
	/* Backdrop sprites are drawn at scale 1.0 (draw_drawbackdropimage passes
	 * 0x100 to rotscale) — native classic-pixel size, no perspective scale.
	 * Convert native px → eye-space tangent half-extent via the classic FoV;
	 * the scene projecting the world quad applies the HD FoV. */
	const float tan_h_classic = tanf(snap->camera.fov_h_half_rad);
	const float tan_v_classic = tanf(snap->camera.fov_v_half_rad);
	const float classic_frame_w = (snap->classic_w > 0) ? (float)snap->classic_w : 320.0f;
	const float classic_frame_h = (snap->classic_h > 0) ? (float)snap->classic_h : 200.0f;
	const float classic_viewport_w = snap->camera.viewport_frac_w * classic_frame_w;
	const float classic_viewport_h = snap->camera.viewport_frac_h * classic_frame_h;
	if (classic_viewport_w <= 0.0f || classic_viewport_h <= 0.0f)
		return false;
	const bool mirror_u = convention == TIE_FLIGHT_LEGACY_RENDER_TIE98_D3D;

	int submitted = 0;
	int slot = 0;

	for (int face = 0; face < 6; ++face) {
		for (int i = 0; i < cnt[face] && slot < TIE_MAX_BACKDROP_SLOTS; ++i, ++slot) {
			const uint8_t sp = bd->slot_species[slot];
			uint16_t frame_index = 0;
			if (sp >= BACKDROP_PLANET_SPECIES_LO && sp <= BACKDROP_PLANET_SPECIES_HI) {
				int ver = bd->slot_planet_version[slot];
				if (ver < 0)
					ver = 0;
				else if (ver > 7)
					ver = 7;
				frame_index = (uint16_t)ver;
			} else if (sp >= BACKDROP_FILLER_SPECIES_LO && sp <= BACKDROP_FILLER_SPECIES_HI) {
			} else {
				continue; /* not a backdrop sprite */
			}

			const TieFlightSpriteEntry* e = TieFlightSpriteCache_Resolve(b->sprites, sp);
			if (!e)
				return false;
			if (e->runtime_atlas)
				frame_index = 0;
			if (frame_index >= e->atlas->frame_count) {
				Aeron_RequestFatalError("Flight Asset Error",
										"backdrop variant exceeds its selected sprite atlas");
				return false;
			}

			float dir[3];
			TieFlightBackdrop_BdDecodeDir(face, bd->slot_pos[slot], dir);
			const float dl = sqrtf(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
			if (dl < 1e-6f)
				continue;
			const float n[3] = { dir[0] / dl, dir[1] / dl, dir[2] / dl };

			/* Per-face billboard frame. The engine aligns each tile's sprite
			 * image-right to a fixed world axis R (world X for front/back/top/
			 * bottom, world Y for left/right; backdrp2.c:138/153/170) and, via
			 * the rotscale reflection, image-up to n × R. Per-face (not per
			 * direction) so all tiles on a face share one orientation. */
			static const float FACE_RIGHT_AXIS[6][3] = {
				{ 1, 0, 0 }, { 1, 0, 0 }, /* front, back  → world X */
				{ 0, 1, 0 }, { 0, 1, 0 }, /* left,  right → world Y */
				{ 1, 0, 0 }, { 1, 0, 0 }, /* top,   bottom→ world X */
			};
			const float* R = FACE_RIGHT_AXIS[face];
			float up[3] = {
				n[1] * R[2] - n[2] * R[1],
				n[2] * R[0] - n[0] * R[2],
				n[0] * R[1] - n[1] * R[0],
			};
			const float ul = sqrtf(up[0] * up[0] + up[1] * up[1] + up[2] * up[2]);
			if (ul < 1e-6f)
				continue;
			up[0] /= ul;
			up[1] /= ul;
			up[2] /= ul;
			const float right[3] = {
				up[1] * n[2] - up[2] * n[1],
				up[2] * n[0] - up[0] * n[2],
				up[0] * n[1] - up[1] * n[0],
			};

			const TieScene2dRect* fr = &e->atlas->frames[frame_index];
			int page_w = 0, page_h = 0;
			if (!TieFlightSpriteEntry_PageSize(e, frame_index, &page_w, &page_h)) {
				Aeron_RequestFatalError("Flight Asset Error",
										"backdrop frame refers to an invalid atlas page");
				return false;
			}
			const float aw = (float)page_w;
			const float ah = (float)page_h;
			if (aw <= 0.0f || ah <= 0.0f || fr->h <= 0.0f)
				continue;
			const float u0 = fr->x / aw, v0 = fr->y / ah;
			const float u1 = (fr->x + fr->w) / aw, v1 = (fr->y + fr->h) / ah;

			/* Native classic sprite dims (undo any atlas upscale) → tangent
			 * half-extents in eye space. See the classic-FoV note above. */
			const float nw = fr->w / e->scale_factor;
			const float nh = fr->h / e->scale_factor;
			const float hx = nw * tan_h_classic / classic_viewport_w;
			const float hy = nh * tan_v_classic / classic_viewport_h;

			/* Centre must be in front of the camera. */
			float cc[3];
			TieFlightBackdrop_ProjectDir(fcam->view_proj, n, cc);
			if (cc[2] <= 1e-4f)
				continue;

			AeronTexture* tex = TieFlightSpriteEntry_Texture(e, frame_index);
			if (!tex) {
				Aeron_RequestFatalError("Flight Asset Error",
										"backdrop frame refers to an invalid atlas page");
				return false;
			}

			/* Camera-local directional corners (TL, BL, BR, TR loop). */
			AeronSceneBillboardDesc d;
			memset(&d, 0, sizeof d);
			d.texture = tex;
			d.blend = AERON_SCENE_BILLBOARD_BLEND_ALPHA;
			d.stage = AERON_SCENE_BILLBOARD_STAGE_SKY;
			static const float sgx[4] = { -1.0f, -1.0f, +1.0f, +1.0f };
			static const float sgy[4] = { +1.0f, -1.0f, -1.0f, +1.0f };
			static const float cuv[4][2] = {
				{ 0, 0 },
				{ 0, 1 },
				{ 1, 1 },
				{ 1, 0 },
			};
			for (int c = 0; c < 4; ++c) {
				for (int j = 0; j < 3; ++j) {
					const float dj = n[j] + right[j] * (sgx[c] * hx) + up[j] * (sgy[c] * hy);
					d.corners[c][j] = BACKDROP_FAR_DIST * dj;
				}
				const bool left = cuv[c][0] == 0.0f;
				d.uv[c][0] = (left != mirror_u) ? u0 : u1;
				d.uv[c][1] = (cuv[c][1] == 0.0f) ? v0 : v1;
				d.colors[c][0] = d.colors[c][1] = d.colors[c][2] = d.colors[c][3] = 1.0f;
			}
			AeronScene_AddBillboard(scene, &d);
			++submitted;
		}
	}
	return submitted > 0;
}
