/*
 * Classic-mode line/bolt draw — implementation.
 *
 * Owns the line and bolt-line pipelines, which draw projectile bolts
 * and ship antennas / struts as screen-space line geometry with the
 * engine's distance-driven thickness law. In HD mode the cooked-glb
 * pass owns both projectiles and antennas, so the orchestrator never
 * calls into this file there.
 *
 * Runs as a post-pass after the classic mesh sub-pass on the same
 * render encoder. Re-binds every pipeline / sampler / cbuffer slot
 * it depends on at entry; the caller doesn't owe any state.
 *
 * Per-craft transform uniforms are pushed here independently of the mesh
 * sub-pass; shared mesh-table data comes from TieFlightRenderer's storage buffer.
 */

#include "tie_remaster/flight/line_draw.h"
#include <stdio.h>

#include "tie_remaster/flight/mesh_common.h"
#include "tie_remaster/flight/render_math.h"
#include "tie_remaster/flight/renderer_internal.h"
#include "tie_remaster/gpu_debug.h"

#include "tie_runtime/snapshot/snapshot.h"

#include "aeron/render.h"
#include "aeron/scene/world.h"

#include <stdlib.h>
#include <string.h>

/* True if this craft has line geometry that this pass should draw.
 *  - Ships and projectiles draw when the species cache carries line
 *    accents (antennas / struts on ships, the full bolt mesh on
 *    projectiles).
 *  - Debris never draws lines: the line VBO has no per-mesh tagging,
 *    so drawing it would emit every parent-species line at the
 *    chunk's pose. */
static bool TieFlightRenderer_CraftNeedsLines(const TieFlightObjectState* fl,
											  const TieFlightSpeciesMesh* sm) {
	if (!sm || !sm->line_vbo || !sm->line_ibo || sm->line_index_count == 0)
		return false;
	if (fl->genus == TIE_GENUS_DEBRIS)
		return false;
	return true;
}

/* ============================================================== */
/* Main-pass line/bolt draw                                        */
/* ============================================================== */

void TieFlightRenderer_LinesDrawPass(TieFlightRenderer* g, AeronCommandBuffer* cmd, AeronRenderPass* pass,
									 const TieSnapshot* curr, const float view_proj[16]) {
	if (!g || !cmd || !pass || !curr)
		return;
	if (!g->line_pipeline && !g->bolt_line_pipeline)
		return;
	Aeron_BindStorageBuffer(pass, AERON_SHADER_STAGE_VERTEX, 0, g->classic_mesh_tables.buffer);

	TIE_GPU_MARKER(cmd, "Lines + bolts");

	/* Classic FS samplers — both line + bolt pipelines share
	 * mesh_classic_lut_ps, which reads materialcolors LUT + palette. */
	Aeron_BindTextureSampler(pass, AERON_SHADER_STAGE_FRAGMENT, 0, g->materialcolors_tex, g->sampler_linear);
	Aeron_BindTextureSampler(pass, AERON_SHADER_STAGE_FRAGMENT, 1, g->palette_tex, g->sampler_linear);

	/* TieFlightMeshPixelUniforms (FS slot 0) — same fields the mesh FS reads. */
	TieFlightMeshPixelUniforms ps_u = { 0 };
	ps_u.marking_state_offset = 0.0f;
	ps_u.line_thickness_mul = (float)g->rt_h / 240.0f;
	ps_u.line_floor_mul = TieFlightMesh_LineFloorMultiplier();
	Aeron_BindUniformData(pass, AERON_SHADER_STAGE_FRAGMENT, /*slot=*/0, &ps_u, sizeof ps_u);
	float last_marking_offset = 0.0f;

	/* TieFlightLineVertexUniforms (VS slot 1) — pixel→clip scale + thickness. */
	TieFlightLineVertexUniforms lvs_u = { 0 };
	lvs_u.pixel_to_clip_xy[0] = 2.0f / (float)g->rt_w;
	lvs_u.pixel_to_clip_xy[1] = 2.0f / (float)g->rt_h;
	lvs_u.thickness_mul = (float)g->rt_h / 240.0f;
	lvs_u.line_floor_mul = TieFlightMesh_LineFloorMultiplier();
	Aeron_BindUniformData(pass, AERON_SHADER_STAGE_VERTEX, /*slot=*/1, &lvs_u, sizeof lvs_u);

	/* Frustum cull + species sort — mirror the mesh sub-pass. */
	TieFlightFrustumPlanes fr;
	TieFlightRenderer_BuildFrustumPlanes(&fr, view_proj);

	uint32_t draw_keys[TIE_MAX_FLIGHT_OBJECTS];
	uint32_t draw_count = 0;
	for (uint16_t i = 0; i < curr->flight_count; ++i) {
		const TieFlightObjectState* fl = &curr->flights[i];
		const uint16_t species_idx =
			(fl->genus == TIE_GENUS_DEBRIS) ? (uint16_t)fl->parent_ship_idx : (uint16_t)fl->ship_idx;
		if (species_idx >= TIE_FLIGHT_MAX_SPECIES)
			continue;
		if (fl->flags & TIE_FOBJ_INVISIBLE)
			continue;
		if (fl->slot == curr->camera.target_obj_slot && curr->camera.zoom_active == 0 &&
			curr->replay_mode != 2)
			continue;
		const TieFlightSpeciesMesh* sm = &g->meshes[species_idx];
		if (!sm->ready)
			continue;
		if (!TieFlightRenderer_CraftNeedsLines(fl, sm))
			continue;
		float position_local[3];
		AeronWorld_LocalI32(curr->camera.world_pos, fl->world_pos, position_local);
		if (TieFlightRenderer_SphereOutsideFrustum(&fr, position_local, sm->bound_radius_world))
			continue;
		draw_keys[draw_count++] = ((uint32_t)species_idx << 16) | (uint32_t)i;
	}
	qsort(draw_keys, draw_count, sizeof draw_keys[0], TieFlightRenderer_CmpDrawKey);

	AeronGraphicsPipeline* current_pp = NULL;
	uint16_t last_species_idx = 0xFFFFu;

	for (uint32_t k = 0; k < draw_count; ++k) {
		const uint16_t i = (uint16_t)(draw_keys[k] & 0xFFFFu);
		const TieFlightObjectState* fl = &curr->flights[i];
		const uint16_t species_idx = (uint16_t)(draw_keys[k] >> 16);
		TieFlightSpeciesMesh* sm = &g->meshes[species_idx];

		/* The shared fragment shader declares both decal buffers even
		 * though line vertices always skip decal reads. */
		if (species_idx != last_species_idx) {
			AeronBuffer* dsb_records =
				sm->decal_records_sb ? sm->decal_records_sb : g->classic_mesh_tables.buffer;
			AeronBuffer* dsb_verts = sm->decal_verts_sb ? sm->decal_verts_sb : g->classic_mesh_tables.buffer;
			Aeron_BindStorageBuffer(pass, AERON_SHADER_STAGE_FRAGMENT, 0, dsb_records);
			Aeron_BindStorageBuffer(pass, AERON_SHADER_STAGE_FRAGMENT, 1, dsb_verts);
			last_species_idx = species_idx;
		}

		/* Per-craft marking-state offset (FS) — only re-push on change. */
		const float craft_marking_offset = (fl->decal_color == 1)   ? -1.0f
										   : (fl->decal_color == 2) ? +1.0f
																	: 0.0f;
		if (craft_marking_offset != last_marking_offset) {
			ps_u.marking_state_offset = craft_marking_offset;
			Aeron_BindUniformData(pass, AERON_SHADER_STAGE_FRAGMENT, /*slot=*/0, &ps_u, sizeof ps_u);
			last_marking_offset = craft_marking_offset;
		}

		/* Per-craft VS uniforms — recomputed (cheap) so this pass
		 * doesn't depend on what the mesh sub-pass pushed. */
		float position_local[3];
		AeronWorld_LocalI32(curr->camera.world_pos, fl->world_pos, position_local);
		TieFlightMeshVertexUniforms vs_u;
		TieFlightMesh_BuildmeshVs(&vs_u, sm, fl, curr, position_local, view_proj);
		vs_u.mesh_table_index = i;
		Aeron_BindUniformData(pass, AERON_SHADER_STAGE_VERTEX, /*slot=*/0, &vs_u, sizeof vs_u);

		/* Pipeline pick: bolt-line for projectiles (depth-write off),
		 * regular line for hull antennas (depth-write on). */
		const bool is_bolt =
			(fl->genus == TIE_GENUS_PROJECTILE_NPC || fl->genus == TIE_GENUS_PROJECTILE_PLAYER);
		AeronGraphicsPipeline* line_pp = is_bolt ? g->bolt_line_pipeline : g->line_pipeline;
		if (!line_pp)
			continue;
		if (current_pp != line_pp) {
			Aeron_BindGraphicsPipeline(pass, line_pp);
			current_pp = line_pp;
		}

		Aeron_BindVertexBuffer(pass, 0, sm->line_vbo, 0);
		Aeron_BindIndexBuffer(pass, sm->line_ibo, AERON_INDEX_FORMAT_UINT16, 0);

		const float eye_z = TieFlightRenderer_EyeZ(view_proj, position_local);
		const TieFlightShipModelLineLod* seg = TieFlightMeshCommon_PickLineLod(sm, eye_z);
		if (seg) {
			Aeron_DrawIndexed(pass, seg->index_count, seg->index_offset, 0);
		} else {
			Aeron_DrawIndexed(pass, sm->line_index_count, 0, 0);
		}
	}
}

/* ============================================================== */
/* PIP single-craft line/bolt draw                                 */
/* ============================================================== */

void TieFlightRenderer_LinesDrawSingle(TieFlightRenderer* g, AeronCommandBuffer* cmd, AeronRenderPass* pass,
									   const TieSnapshot* snap, const TieFlightObjectState* fl,
									   const float view_proj[16], int pip_w, int pip_h) {
	if (!g || !cmd || !pass || !snap || !fl)
		return;
	if (fl->ship_idx >= TIE_FLIGHT_MAX_SPECIES)
		return;
	TieFlightSpeciesMesh* sm = &g->meshes[fl->ship_idx];
	if (!sm)
		return;
	if (!sm->line_vbo || !sm->line_ibo || sm->line_index_count == 0)
		return;

	const bool is_bolt = (fl->genus == TIE_GENUS_PROJECTILE_NPC || fl->genus == TIE_GENUS_PROJECTILE_PLAYER);
	AeronGraphicsPipeline* line_pp =
		is_bolt ? g->classic_pip_bolt_line_pipeline : g->classic_pip_line_pipeline;
	if (!line_pp)
		return;

	/* PIP line VS uniforms — engine doesn't alter thicknessMultiple for
	 * the PIP, so the resolution-scaled multiplier rebases to pip_h. */
	TieFlightLineVertexUniforms lvs_u = { 0 };
	lvs_u.pixel_to_clip_xy[0] = 2.0f / (float)pip_w;
	lvs_u.pixel_to_clip_xy[1] = 2.0f / (float)pip_h;
	{
		uint16_t engine_crt_h = snap->cockpit.pip_h;
		if (engine_crt_h == 0)
			engine_crt_h = 240;
		lvs_u.thickness_mul = 2.0f * (float)pip_h / (float)engine_crt_h;
	}
	lvs_u.line_floor_mul = TieFlightMesh_LineFloorMultiplier();
	Aeron_BindUniformData(pass, AERON_SHADER_STAGE_VERTEX, /*slot=*/1, &lvs_u, sizeof lvs_u);

	Aeron_BindGraphicsPipeline(pass, line_pp);
	Aeron_BindVertexBuffer(pass, 0, sm->line_vbo, 0);
	Aeron_BindIndexBuffer(pass, sm->line_ibo, AERON_INDEX_FORMAT_UINT16, 0);

	const float pip_position_local[3] = { 0.0f, 0.0f, 0.0f };
	const float eye_z = TieFlightRenderer_EyeZ(view_proj, pip_position_local);
	const TieFlightShipModelLineLod* seg = TieFlightMeshCommon_PickLineLod(sm, eye_z);
	if (seg) {
		Aeron_DrawIndexed(pass, seg->index_count, seg->index_offset, 0);
	} else {
		Aeron_DrawIndexed(pass, sm->line_index_count, 0, 0);
	}
}
