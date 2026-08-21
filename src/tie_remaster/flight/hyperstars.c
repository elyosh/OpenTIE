/* HD hyperspace-streak rendering. Geometry follows tie_updatescreen_tie95,
 * draw_drawhyperstar, and drawpol.c's line-object path. Colors come from
 * palette[252..255], matching `(slot & 3) - 4` with uint8 wrap. */

#include "tie_remaster/flight/hyperstars.h"
#include <stdio.h>

#include "tie_remaster/flight/render_math.h"
#include "tie_remaster/flight/renderer_internal.h"
#include "tie_remaster/gpu_debug.h"
#include "tie_remaster/scene2d/srgb_math.h"

#include "tie_runtime/snapshot/snapshot.h"

#include "aeron/log.h"
#include "aeron/render.h"
#include "aeron/scene/world.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Engine-side dispatch caps. NUM_STATIC_OBJECTS == 64 (tie.h) and
 * the cinematic emits at most that many; up to 4 mirrors per slot
 * (the binary's two unconditional + two gated on hyperspacedetail/2)
 * and 4 verts per mirror line yields the worst-case vertex count. */
#define HS_MAX_STATIC_SLOTS 64
#define HS_MAX_MIRRORS_PER_SLOT 4
#define HS_VERTS_PER_LINE 4
#define HS_INDICES_PER_LINE 6
#define HS_MAX_LINES (HS_MAX_STATIC_SLOTS * HS_MAX_MIRRORS_PER_SLOT)
#define HS_MAX_VERTS (HS_MAX_LINES * HS_VERTS_PER_LINE)
#define HS_MAX_INDICES (HS_MAX_LINES * HS_INDICES_PER_LINE)

/* Polymesh int16 vertex coord → native float world units. Same scale
 * every other HD pass that reads classic engine vertex data uses.
 *
 * The engine's transfm2_geteyecoords (transfm2.c:117/146) applies
 *   eye += (rotworldeye_Q15 × vertex_int16) >> 16
 *        = (basis_unit × vertex_int16) / 2
 * i.e. one raw int16 unit on a polymesh contributes 0.5 native world
 * units to the eye output.
 *
 * NOTE: this is not the same as the ×256 scale the static
 * object pos[] uses — those values were stored pre-divided by 256
 * by the engine (tie_checkstaticobjecteyexyz multiplies them by 256
 * to produce q1616). Polymesh int16 coords feed the rotated
 * accumulator above instead, which the engine effectively halves. */

/* Per-vertex layout. Each line emits 4 corners sharing (pos_a, pos_b,
 * color); endpoint (0|1) picks the clip-space center, side (-1|+1)
 * picks the perpendicular offset for thickness expansion. */
typedef struct TieHyperstarsVertex {
	float pos_a[3]; /* line endpoint A in world space */
	float pos_b[3]; /* line endpoint B in world space */
	float endpoint; /* 0.0 = vertex sits at A, 1.0 = at B */
	float side;     /* -1.0 / +1.0, perpendicular direction */
	float color[4]; /* pre-resolved RGBA from palette[252+(slot&3)] */
} TieHyperstarsVertex;

typedef struct TieHyperstarsVertexUniforms {
	float view_proj[16];
	float pixel_to_clip_xy[2]; /* (2/vp_w, 2/vp_h) */
	float thickness_mul;       /* HD render-resolution thickness scaler */
	float _pad;
} TieHyperstarsVertexUniforms;

struct TieFlightHyperstars {
	AeronShader* vs;
	AeronShader* ps;
	AeronGraphicsPipeline* pipeline;
	AeronTextureFormat rt_format;
	AeronSampleCount pipeline_samples;

	AeronBuffer* vb;
	uint32_t vb_cap;
	AeronBuffer* ib;
	uint32_t ib_cap;
	/* Number of indices in the current frame's batch (cleared by
	 * prepare; consumed by draw_in_pass). */
	uint32_t pending_indices;
};

static AeronGraphicsPipeline* TieFlightHyperstars_CreatePipeline(AeronShader* vs, AeronShader* ps,
																 AeronTextureFormat rt_fmt,
																 AeronSampleCount sample_count) {
	AeronVertexAttributeDesc attrs[5] = {
		{ .location = 0,
		  .buffer_slot = 0,
		  .format = AERON_VERTEX_FORMAT_FLOAT3,
		  .offset = (uint32_t)offsetof(TieHyperstarsVertex, pos_a) },
		{ .location = 1,
		  .buffer_slot = 0,
		  .format = AERON_VERTEX_FORMAT_FLOAT3,
		  .offset = (uint32_t)offsetof(TieHyperstarsVertex, pos_b) },
		{ .location = 2,
		  .buffer_slot = 0,
		  .format = AERON_VERTEX_FORMAT_FLOAT,
		  .offset = (uint32_t)offsetof(TieHyperstarsVertex, endpoint) },
		{ .location = 3,
		  .buffer_slot = 0,
		  .format = AERON_VERTEX_FORMAT_FLOAT,
		  .offset = (uint32_t)offsetof(TieHyperstarsVertex, side) },
		{ .location = 4,
		  .buffer_slot = 0,
		  .format = AERON_VERTEX_FORMAT_FLOAT4,
		  .offset = (uint32_t)offsetof(TieHyperstarsVertex, color) },
	};
	AeronVertexBufferLayoutDesc vbd = {
		.slot = 0,
		.stride = (uint32_t)sizeof(TieHyperstarsVertex),
	};
	/* Additive blend (ONE + ONE) — streaks brighten whatever's behind
	 * them. Depth test on so a foreground ship occludes streaks; depth
	 * write off so overlapping streaks don't fight. */
	AeronColorTargetStateDesc color_target = {
		.format = rt_fmt,
		.blend = { .enabled = 1,
				   .src_color = AERON_BLEND_ONE,
				   .dst_color = AERON_BLEND_ONE,
				   .color_op = AERON_BLEND_OP_ADD,
				   .src_alpha = AERON_BLEND_ONE,
				   .dst_alpha = AERON_BLEND_ONE,
				   .alpha_op = AERON_BLEND_OP_ADD },
	};
	return Aeron_CreateGraphicsPipeline(&(AeronGraphicsPipelineDesc) {
		.vertex_shader = vs,
		.fragment_shader = ps,
		.primitive_type = AERON_PRIMITIVE_TRIANGLES,
		.cull_mode = AERON_CULL_NONE,
		.vertex_buffers = &vbd,
		.vertex_buffer_count = 1,
		.attributes = attrs,
		.attribute_count = 5,
		.sample_count = sample_count,
		.depth_format = AERON_TEXTURE_FORMAT_D32_FLOAT,
		.depth = { .depth_test = 1, .depth_write = 0, .compare = AERON_COMPARE_GREATER_EQUAL },
		.color_target_count = 1,
		.color_targets = &color_target,
	});
}

TieFlightHyperstars* TieFlightHyperstars_Create(AeronTextureFormat rt_fmt) {
	TieFlightHyperstars* h = (TieFlightHyperstars*)calloc(1, sizeof *h);
	if (!h)
		return NULL;
	h->rt_format = rt_fmt;

	h->vs = TieFlightRenderer_CompileShader("flight_hyperstars.vert", AERON_SHADER_STAGE_VERTEX,
											/*num_samplers=*/0,
											/*num_uniform_buffers=*/1,
											/*num_storage_textures=*/0);
	h->ps = TieFlightRenderer_CompileShader("flight_hyperstars.frag", AERON_SHADER_STAGE_FRAGMENT,
											/*num_samplers=*/0,
											/*num_uniform_buffers=*/0,
											/*num_storage_textures=*/0);
	if (!h->vs || !h->ps) {
		Aeron_LogError("tie.flight", "hyperstar shader load failed");
		TieFlightHyperstars_Destroy(h);
		return NULL;
	}
	h->pipeline = TieFlightHyperstars_CreatePipeline(h->vs, h->ps, rt_fmt, AERON_SAMPLE_COUNT_1);
	h->pipeline_samples = h->pipeline ? AERON_SAMPLE_COUNT_1 : 0;
	if (!h->pipeline) {
		Aeron_LogError("tie.flight", "hyperstar pipeline creation failed");
		TieFlightHyperstars_Destroy(h);
		return NULL;
	}
	return h;
}

void TieFlightHyperstars_Destroy(TieFlightHyperstars* h) {
	if (!h)
		return;
	if (h->pipeline)
		Aeron_DestroyGraphicsPipeline(h->pipeline);
	if (h->vs)
		Aeron_DestroyShader(h->vs);
	if (h->ps)
		Aeron_DestroyShader(h->ps);
	if (h->vb)
		Aeron_DestroyBuffer(h->vb);
	if (h->ib)
		Aeron_DestroyBuffer(h->ib);
	free(h);
}

/* Emit one quad (4 vertices + 6 indices) for the line A→B with the
 * given color. Indices are relative to the verts array base index
 * `first_v` (caller-supplied). */
static void TieFlightHyperstars_EmitLine(TieHyperstarsVertex* verts, uint32_t first_v, uint16_t* indices,
										 uint32_t first_i, const float a[3], const float b[3],
										 const float color[4]) {
	/* TL, TR, BR, BL — winding chosen so the perpendicular expansion
	 * in the VS produces a consistent front-facing band. */
	const float corners[4][2] = {
		{ 0.0f, -1.0f }, /* endpoint A, side -1 */
		{ 1.0f, -1.0f }, /* endpoint B, side -1 */
		{ 1.0f, +1.0f }, /* endpoint B, side +1 */
		{ 0.0f, +1.0f }, /* endpoint A, side +1 */
	};
	for (int c = 0; c < 4; ++c) {
		TieHyperstarsVertex* v = &verts[first_v + c];
		memcpy(v->pos_a, a, sizeof v->pos_a);
		memcpy(v->pos_b, b, sizeof v->pos_b);
		v->endpoint = corners[c][0];
		v->side = corners[c][1];
		memcpy(v->color, color, sizeof v->color);
	}
	/* Two triangles, CCW. */
	indices[first_i + 0] = (uint16_t)(first_v + 0);
	indices[first_i + 1] = (uint16_t)(first_v + 1);
	indices[first_i + 2] = (uint16_t)(first_v + 2);
	indices[first_i + 3] = (uint16_t)(first_v + 0);
	indices[first_i + 4] = (uint16_t)(first_v + 2);
	indices[first_i + 5] = (uint16_t)(first_v + 3);
}

bool TieFlightHyperstars_Prepare(TieFlightHyperstars* h, AeronCommandBuffer* cmd, const TieSnapshot* snap,
								 const TieFlightCamera* fcam) {
	if (!h)
		return false;
	h->pending_indices = 0;
	if (!cmd || !snap || !fcam)
		return false;
	if (!h->pipeline)
		return false;
	if (snap->hyperstar_count == 0)
		return false;
	if (snap->hyperspace.phase != 3 && snap->hyperspace.phase != 5)
		return false;

	const uint16_t detail = snap->hyperspace.hyperspacedetail;
	if (detail == 0)
		return false;
	const uint16_t half_detail = (uint16_t)(detail / 2);

	/* Resolve the four shade colors from the live palette. The engine
	 * writes (slot & 3) - 4 = 252..255 into the edge color byte; we
	 * read those four entries straight from snap->palette so engine
	 * palette cycling carries through transparently. */
	float shade_rgba[4][4];
	for (int s = 0; s < 4; ++s) {
		const uint32_t p = snap->palette[252 + s];
		TieScene2dSrgb_PalToLinearRgb(p, &shade_rgba[s][0], &shade_rgba[s][1], &shade_rgba[s][2]);
		shade_rgba[s][3] = 1.0f;
	}

	/* Streak endpoint Y offsets — shared by every slot. Polymesh int16
	 * vertices use the classic half-unit transform. */
	const float p0_y = (float)snap->hyperspace.hyperstar_p0_x * TIE_CLASSIC_VERTEX_TO_WORLD_UNITS;
	const float p1_y = (float)snap->hyperspace.hyperstar_p1_x * TIE_CLASSIC_VERTEX_TO_WORLD_UNITS;

	static TieHyperstarsVertex s_verts[HS_MAX_VERTS];
	static uint16_t s_indices[HS_MAX_INDICES];
	uint32_t vert_count = 0;
	uint32_t index_count = 0;

	/* Emit ONE line per (slot, mirror) pair. Mirror pattern from
	 * tie.c:1920-1945: two unconditional + two gated on
	 * `hyperspacedetail/2 > i`. The mirror transforms apply to the
	 * world XZ; the Y-axis line offset (p0_y → p1_y) is identical
	 * across every line. */
	for (uint16_t k = 0; k < snap->hyperstar_count; ++k) {
		const TieHyperstar* hs = &snap->hyperstars[k];
		const uint8_t slot = hs->slot;
		if (slot >= detail)
			continue; /* defensive — emit already
					   * caps at detail */

		const int32_t wx = hs->world_pos[0];
		const int32_t wy = hs->world_pos[1];
		const int32_t wz = hs->world_pos[2];

		/* Mirror positions follow the engine's half/quarter construction.
		 * Integer division can differ from an arithmetic right shift by at
		 * most one native unit for negative odd values. */
		int32_t mirrors_world[HS_MAX_MIRRORS_PER_SLOT][3];
		int n_mirrors = 2;
		mirrors_world[0][0] = wx;
		mirrors_world[0][1] = wy;
		mirrors_world[0][2] = wz;
		mirrors_world[1][0] = wx;
		mirrors_world[1][1] = wy;
		mirrors_world[1][2] = -wz;
		if (slot < half_detail) {
			mirrors_world[2][0] = -wx / 2;
			mirrors_world[2][1] = wy;
			mirrors_world[2][2] = -wz / 2;
			mirrors_world[3][0] = -wx / 4;
			mirrors_world[3][1] = wy;
			mirrors_world[3][2] = wz / 4;
			n_mirrors = 4;
		}

		const float* col = shade_rgba[slot & 3];
		for (int m = 0; m < n_mirrors; ++m) {
			if (vert_count + HS_VERTS_PER_LINE > HS_MAX_VERTS)
				goto cap_hit;
			float local[3];
			AeronWorld_LocalI32(fcam->origin_world, mirrors_world[m], local);
			const float a[3] = { local[0], local[1] + p0_y, local[2] };
			const float b[3] = { local[0], local[1] + p1_y, local[2] };
			TieFlightHyperstars_EmitLine(s_verts, vert_count, s_indices, index_count, a, b, col);
			vert_count += HS_VERTS_PER_LINE;
			index_count += HS_INDICES_PER_LINE;
		}
	}
cap_hit:

	if (index_count == 0)
		return false;

	/* Upload — must run before the caller opens the flight render pass. */
	uint32_t vb_need = vert_count * (uint32_t)sizeof(TieHyperstarsVertex);
	if (!TieFlightRenderer_GrowBuffer(&h->vb, &h->vb_cap, vb_need, AERON_BUFFER_USAGE_VERTEX,
									  "flight.hyperstars.vb")) {
		Aeron_RequestFatalRendererError("hyperstar vertex-buffer creation");
		return false;
	}
	if (!TieFlightRenderer_UploadToBuffer(cmd, h->vb, s_verts, vb_need)) {
		Aeron_RequestFatalRendererError("hyperstar vertex upload");
		return false;
	}

	uint32_t ib_need = index_count * (uint32_t)sizeof(uint16_t);
	if (!TieFlightRenderer_GrowBuffer(&h->ib, &h->ib_cap, ib_need, AERON_BUFFER_USAGE_INDEX,
									  "flight.hyperstars.ib")) {
		Aeron_RequestFatalRendererError("hyperstar index-buffer creation");
		return false;
	}
	if (!TieFlightRenderer_UploadToBuffer(cmd, h->ib, s_indices, ib_need)) {
		Aeron_RequestFatalRendererError("hyperstar index upload");
		return false;
	}

	h->pending_indices = index_count;
	return true;
}

void TieFlightHyperstars_DrawInPass(TieFlightHyperstars* h, AeronCommandBuffer* cmd, AeronRenderPass* pass,
									const AeronRectI* vp, const TieFlightCamera* fcam) {
	if (!h || !cmd || !pass || !fcam)
		return;
	const AeronSampleCount samples = Aeron_RenderPassGetSampleCount(pass);
	if (!h->pipeline || h->pipeline_samples != samples) {
		if (h->pipeline)
			Aeron_DestroyGraphicsPipeline(h->pipeline);
		h->pipeline = TieFlightHyperstars_CreatePipeline(h->vs, h->ps, h->rt_format, samples);
		h->pipeline_samples = h->pipeline ? samples : 0;
		if (!h->pipeline) {
			Aeron_CommandBufferSetFailure(cmd, "Hyperstar pipeline preparation failed");
			return;
		}
	}
	if (h->pending_indices == 0)
		return;

	TIE_GPU_PUSH(cmd, "Flight hyperstars");
	if (vp)
		Aeron_SetViewport(pass, vp);
	Aeron_BindGraphicsPipeline(pass, h->pipeline);

	/* Thickness: classic uses thickness_base=0x40=64 for hyperstars
	 * (hyperstardata edge thickness lo byte). The HD math mirrors
	 * flight_line: thick_px = (thickness_mul × base) / (avg_w / 256).
	 * thickness_mul scales with HD resolution (mesh pass uses rt_h /
	 * classic_h ≈ 1080/200 ≈ 5.4 at 1080p). */
	TieHyperstarsVertexUniforms u;
	memcpy(u.view_proj, fcam->view_proj, sizeof u.view_proj);
	u.pixel_to_clip_xy[0] = 2.0f / (float)((fcam->rt_w > 0) ? fcam->rt_w : 1);
	u.pixel_to_clip_xy[1] = 2.0f / (float)((fcam->rt_h > 0) ? fcam->rt_h : 1);
	/* Streak thickness mul: use 1.0 baseline; the classic engine's
	 * VGA-equivalent is 1, SVGA is 2, and HD scales up linearly with
	 * vertical resolution / 200. */
	const float classic_h = 200.0f;
	u.thickness_mul = (fcam->rt_h > 0) ? ((float)fcam->rt_h / classic_h) : 1.0f;
	u._pad = 0.0f;
	Aeron_BindUniformData(pass, AERON_SHADER_STAGE_VERTEX, /*slot=*/0, &u, sizeof u);

	Aeron_BindVertexBuffer(pass, 0, h->vb, 0);
	Aeron_BindIndexBuffer(pass, h->ib, AERON_INDEX_FORMAT_UINT16, 0);

	Aeron_DrawIndexed(pass, h->pending_indices, 0, 0);
	TIE_GPU_POP(cmd); /* "Flight hyperstars" */
}
