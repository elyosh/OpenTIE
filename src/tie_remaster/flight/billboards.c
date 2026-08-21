/*
 * flight_billboards — snapshot billboard sprites (explosions, debris,
 * lightning) as batched OVERLAY scene billboards.
 *
 * This module keeps the game-shaped CPU work — species→atlas
 * resolution, the classic size/rotation/anchor math (dual-FoV pixel
 * sizing, the rotscale reflection with its anchor wobble), the
 * flat-object depth bias, and the parent-displacement velocity — and
 * submits world-space quads via AeronScene_AddBillboard. The GPU
 * plumbing (batching, pipelines, the NDC-offset projection trick it
 * replaces, velocity stamping) lives in aeron_scene_billboards3d; the
 * NDC-offset construction is reproduced exactly by building corners
 * on the camera right/up axes at the sprite's eye depth.
 */

#include "tie_remaster/flight/billboards.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "aeron/aeron.h"
#include "aeron/log.h"
#include "aeron/render.h"
#include "aeron/scene/billboard.h"
#include "aeron/scene/world.h"

#include "tie_remaster/flight/render_math.h"
#include "tie_remaster/flight/renderer_internal.h"
#include "tie_remaster/flight/sprite_cache.h"
#include "tie_remaster/scene2d/types.h"
#include "tie_runtime/snapshot/snapshot.h"

struct TieFlightBillboards {
	/* Borrowed from TieFlightRenderer and shared with the backdrop pass. */
	TieFlightSpriteCache* sprites;
};

/* Per-quad HDR emissive multiplier for billboards. Lightning is
 * discriminated via parent_kind (it carries genus = 0 from the
 * craft-anchored capture path); explosions via the parent object's
 * genus; debris and everything else stays at 1.0. Mirrors
 * TieFlightMesh_EmissiveforMesh. */
static inline float TieFlightBillboards_EmissiveForBillboard(uint8_t parent_kind, uint8_t genus) {
	if (parent_kind == TIE_BILLBOARD_LIGHTNING)
		return 2.0f;
	if (genus == TIE_GENUS_EXPLOSION)
		return 2.0f;
	return 1.0f;
}

TieFlightBillboards* TieFlightBillboards_Create(TieFlightSpriteCache* sprites) {
	if (!sprites)
		return NULL;
	TieFlightBillboards* fb = (TieFlightBillboards*)calloc(1, sizeof *fb);
	if (!fb)
		return NULL;
	fb->sprites = sprites;
	return fb;
}

void TieFlightBillboards_Destroy(TieFlightBillboards* fb) {
	if (!fb)
		return;
	free(fb);
}

bool TieFlightBillboards_Submit(TieFlightBillboards* fb, AeronScene3D* scene, AeronCommandBuffer* cmd,
								const TieSnapshot* snap, const TieFlightCamera* fcam,
								const TieFlightMotionBlurPrevious* mb,
								TieFlightLegacyRenderConvention convention) {
	if (!fb || !scene || !cmd || !snap || !fcam)
		return false;
	if (!snap->billboard_count)
		return false;

	/* Convert engine-pixel sizes from the captured classic aperture to
	 * the full-output projection used by the mesh pass. */
	const float tan_h_classic = tanf(snap->camera.fov_h_half_rad);
	const float tan_v_classic = tanf(snap->camera.fov_v_half_rad);
	const float tan_h_hd = tanf(fcam->h_half_rad);
	const float tan_v_hd = tanf(fcam->v_half_rad);
	const float classic_frame_w = (snap->classic_w > 0) ? (float)snap->classic_w : 320.0f;
	const float classic_frame_h = (snap->classic_h > 0) ? (float)snap->classic_h : 200.0f;
	const float classic_viewport_w = snap->camera.viewport_frac_w * classic_frame_w;
	const float classic_viewport_h = snap->camera.viewport_frac_h * classic_frame_h;

	if (tan_h_hd <= 0.0f || tan_v_hd <= 0.0f || tan_h_classic <= 0.0f || tan_v_classic <= 0.0f ||
		classic_viewport_w <= 0.0f || classic_viewport_h <= 0.0f)
		return false;
	const bool d3d_sprite_convention = convention == TIE_FLIGHT_LEGACY_RENDER_TIE98_D3D;

	/* Construct world corners from the camera's right/down basis at a
	 * constant eye depth. */
	float cam_rows[9]; /* world→eye rows: 0 = right, 1 = down, 2 = fwd */
	TieRenderMath_QuaternionToMat3(snap->camera.ori, cam_rows);
	const float* campos = fcam->camera.pos;

	/* Counting-sort the billboard indices by species_idx so same-atlas
	 * quads submit consecutively — the scene batches consecutive
	 * (texture, blend) runs into one draw. */
	uint8_t sorted_idx[TIE_MAX_BILLBOARDS];
	{
		uint16_t bucket_count[256] = { 0 };
		for (uint16_t i = 0; i < snap->billboard_count; ++i)
			++bucket_count[snap->billboards[i].species_idx];
		uint16_t bucket_pos[256];
		uint16_t running = 0;
		for (int sp = 0; sp < 256; ++sp) {
			bucket_pos[sp] = running;
			running = (uint16_t)(running + bucket_count[sp]);
		}
		for (uint16_t i = 0; i < snap->billboard_count; ++i) {
			uint8_t sp = snap->billboards[i].species_idx;
			sorted_idx[bucket_pos[sp]++] = (uint8_t)i;
		}
	}

	/* Motion-blur velocity context: each quad's previous-frame corners
	 * shift by its PARENT flight object's frame displacement (the
	 * sprite rides its parent rigidly). Unmatched parents keep prev =
	 * current — the velocity prepass then stamps pure camera-induced
	 * motion, same as before. The camera_blur cur/prev projection
	 * choice moved into the scene's velocity VS (post.mb_camera_blur). */
	const bool vel_on = mb && mb->enabled && mb->prev && mb->prev_index;
	static int slot_to_flight[TIE_MAX_FLIGHT_OBJECTS];
	if (vel_on) {
		for (int sl = 0; sl < TIE_MAX_FLIGHT_OBJECTS; ++sl)
			slot_to_flight[sl] = -1;
		for (uint16_t i = 0; i < snap->flight_count; ++i) {
			uint16_t sl = snap->flights[i].slot;
			if (sl < TIE_MAX_FLIGHT_OBJECTS)
				slot_to_flight[sl] = (int)i;
		}
	}

	int submitted = 0;

	for (uint16_t k = 0; k < snap->billboard_count; ++k) {
		const TieBillboardState* b = &snap->billboards[sorted_idx[k]];
		if (b->species_idx == 0)
			continue;

		const TieFlightSpriteEntry* cache = TieFlightSpriteCache_Resolve(fb->sprites, b->species_idx);
		if (!cache)
			return false;

		if (b->bitmap_idx >= cache->atlas->frame_count) {
			Aeron_RequestFatalError("Flight Asset Error",
									"billboard bitmap index exceeds its selected sprite atlas");
			return false;
		}
		const TieScene2dRect* frame = &cache->atlas->frames[b->bitmap_idx];

		int page_w = 0, page_h = 0;
		if (!TieFlightSpriteEntry_PageSize(cache, b->bitmap_idx, &page_w, &page_h)) {
			Aeron_RequestFatalError("Flight Asset Error", "billboard frame refers to an invalid atlas page");
			return false;
		}
		const float atlas_w = (float)page_w;
		const float atlas_h = (float)page_h;
		const float u0 = frame->x / atlas_w;
		const float v0 = frame->y / atlas_h;
		const float u1 = (frame->x + frame->w) / atlas_w;
		const float v1 = (frame->y + frame->h) / atlas_h;

		/* Recover classic sprite_w/sprite_h from the (possibly
		 * upscaled) atlas frame dims via the per-species scale factor
		 * cached at load time. */
		const float sprite_native_w = (float)frame->w / cache->scale_factor;
		const float sprite_native_h = (float)frame->h / cache->scale_factor;

		/* Engine BAM is a 16-bit binary angle (full circle = 2^16). */
		const float angle = (float)b->rotation_bam * (2.0f * 3.14159265358979f / 65536.0f);
		const float ca = cosf(angle);
		const float sa = sinf(angle);

		/* Software ROTSCALE rotates around the authored XACT anchor.
		 * TIE98 D3D ignores XACT and centers its generated quad. */
		const float cox =
			d3d_sprite_convention ? -0.5f * sprite_native_w : (float)cache->atlas->origin_x[b->bitmap_idx];
		const float coy0 =
			d3d_sprite_convention ? 0.5f * sprite_native_h : (float)cache->atlas->origin_y[b->bitmap_idx];

		/* Image-space corners around the anchor (classic pixels, math
		 * +Y up): TL, TR, BR, BL — a perimeter loop for the fan. */
		const float img_x[4] = { cox, cox + sprite_native_w, cox + sprite_native_w, cox };
		const float img_y[4] = { coy0, coy0, coy0 - sprite_native_h, coy0 - sprite_native_h };
		const float left_u = d3d_sprite_convention ? u1 : u0;
		const float right_u = d3d_sprite_convention ? u0 : u1;
		const float cuv[4][2] = { { left_u, v0 }, { right_u, v0 }, { right_u, v1 }, { left_u, v1 } };

		/* Per-classic-pixel → HD-NDC factor (dual-FoV) at the engine's
		 * calcscale-derived sprite size (pixel_scale_q8 / 256). */
		const float pixel_scale = (float)b->pixel_scale_q8;
		const float pix2ndc_x = pixel_scale * 2.0f * tan_h_classic / (256.0f * classic_viewport_w * tan_h_hd);
		const float pix2ndc_y = pixel_scale * 2.0f * tan_v_classic / (256.0f * classic_viewport_h * tan_v_hd);

		float pw[3];
		AeronWorld_LocalI32(fcam->origin_world, b->world_pos, pw);

		/* Sprite eye depth (camera-forward row); NDC offsets convert
		 * to world offsets at this depth. Behind-camera sprites were
		 * clipped by the old path; skip them. */
		const float dp[3] = { pw[0] - campos[0], pw[1] - campos[1], pw[2] - campos[2] };
		const float z_eye = cam_rows[6] * dp[0] + cam_rows[7] * dp[1] + cam_rows[8] * dp[2];
		if (z_eye <= 1e-4f)
			continue;

		/* Flat-object depth bias — forward by ~the parent's bounding-
		 * sphere radius, replicating classic's flat-inside-mesh-bbox
		 * rule (xtrans2.c:488). bound_hwidth is engine raw units;
		 * the classic half scale converts it to native view units. */
		const float bias_world = (float)b->bound_hwidth * TIE_CLASSIC_VERTEX_TO_WORLD_UNITS;

		/* Previous-frame displacement from the parent object. */
		float prev_delta[3] = { 0.0f, 0.0f, 0.0f };
		if (vel_on) {
			const int ci = (b->parent_slot < TIE_MAX_FLIGHT_OBJECTS) ? slot_to_flight[b->parent_slot] : -1;
			if (ci >= 0) {
				const int pj = mb->prev_index[ci];
				if (pj >= 0) {
					AeronWorld_DeltaI32(mb->prev->flights[pj].world_pos, snap->flights[ci].world_pos,
										prev_delta);
				}
			}
		}

		const float e = TieFlightBillboards_EmissiveForBillboard(b->parent_kind, b->genus);

		AeronSceneBillboardDesc d;
		memset(&d, 0, sizeof d);
		d.texture = TieFlightSpriteEntry_Texture(cache, b->bitmap_idx);
		d.blend = AERON_SCENE_BILLBOARD_BLEND_ALPHA;
		d.stage = AERON_SCENE_BILLBOARD_STAGE_OVERLAY;
		d.depth_bias_view = bias_world;
		if (!d.texture) {
			Aeron_RequestFatalError("Flight Asset Error", "billboard frame refers to an invalid atlas page");
			return false;
		}

		float prev_corners[4][3];
		for (int c = 0; c < 4; ++c) {
			/* Reflection matrix, then classic px → NDC, then NDC → eye
			 * offsets on the camera right/down rows at z_eye (eye
			 * y-down: ndc_y = −eye_y / (tan_v·z)). */
			const float rx = ca * img_x[c] + sa * img_y[c];
			const float ry = sa * img_x[c] - ca * img_y[c];
			const float ndc_x = rx * pix2ndc_x;
			const float ndc_y = ry * pix2ndc_y;
			const float ex = ndc_x * tan_h_hd * z_eye;
			const float ey = -ndc_y * tan_v_hd * z_eye;
			for (int j = 0; j < 3; ++j) {
				d.corners[c][j] = pw[j] + cam_rows[0 * 3 + j] * ex + cam_rows[1 * 3 + j] * ey;
				prev_corners[c][j] = d.corners[c][j] + prev_delta[j];
			}
			d.uv[c][0] = cuv[c][0];
			d.uv[c][1] = cuv[c][1];
			d.colors[c][0] = d.colors[c][1] = d.colors[c][2] = e;
			d.colors[c][3] = 1.0f;
		}
		d.prev_corners = prev_corners;
		AeronScene_AddBillboard(scene, &d);
		++submitted;
	}

	return submitted > 0;
}
