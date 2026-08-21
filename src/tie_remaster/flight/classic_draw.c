/* Draws classic palette meshes and lines into a caller-owned render pass. */

#include "tie_remaster/flight/classic_draw.h"
#include <stdio.h>

#include "tie_remaster/flight/classic_lighting.h"
#include "tie_remaster/flight/mesh_common.h"
#include "tie_remaster/flight/render_math.h"
#include "tie_remaster/flight/renderer_internal.h"
#include "tie_remaster/gpu_debug.h"

#include "tie_runtime/snapshot/snapshot.h"

#include "aeron/render.h"
#include "aeron/scene/world.h"

#include <stdlib.h>
#include <string.h>

/* ============================================================== */
/* Main-pass classic mesh+line draw                                */
/* ============================================================== */

void TieFlightRenderer_ClassicDrawPass(TieFlightRenderer* g, AeronCommandBuffer* cmd, AeronRenderPass* pass,
									   const TieSnapshot* curr, const float view_proj[16]) {
	if (!g || !cmd || !pass || !curr)
		return;
	if (!g->mesh_pipeline)
		return;

	TIE_GPU_MARKER(cmd, "Mesh + lines + decals");

	AeronGraphicsPipeline* initial_mesh_pp = g->mesh_pipeline;
	Aeron_BindGraphicsPipeline(pass, initial_mesh_pp);
	Aeron_BindStorageBuffer(pass, AERON_SHADER_STAGE_VERTEX, 0, g->classic_mesh_tables.buffer);
	/* Mesh FS samplers: materialcolors LUT + palette. */
	Aeron_BindTextureSampler(pass, AERON_SHADER_STAGE_FRAGMENT, 0, g->materialcolors_tex, g->sampler_linear);
	Aeron_BindTextureSampler(pass, AERON_SHADER_STAGE_FRAGMENT, 1, g->palette_tex, g->sampler_linear);

	/* Per-pass FS uniforms. marking_state_offset is rewritten
	 * per-craft below from fl->decal_color. */
	TieFlightMeshPixelUniforms ps_u = { 0 };
	ps_u.line_thickness_mul = (float)g->rt_h / 240.0f;
	ps_u.line_floor_mul = TieFlightMesh_LineFloorMultiplier();

	/* Local-light candidate pool. Built once per pass from the
	 * snapshot's flight objects; the per-craft loop below culls
	 * this down to TIE_FLIGHT_CLASSIC_LIGHTS_PER_CRAFT entries that
	 * actually reach each craft's bounding sphere and pushes the
	 * per-craft cbuffer to VS slot 1. */
	TieFlightClassicLightCandidate light_pool[TIE_FLIGHT_CLASSIC_LIGHT_POOL_CAP];
	const uint32_t light_pool_count = TieFlightClassicLights_BuildPool(
		curr, curr->camera.world_pos, light_pool, TIE_FLIGHT_CLASSIC_LIGHT_POOL_CAP);

	ps_u.marking_state_offset = 0.0f;
	Aeron_BindUniformData(pass, AERON_SHADER_STAGE_FRAGMENT, /*slot=*/0, &ps_u, sizeof ps_u);
	float last_marking_offset = 0.0f;
	uint16_t last_ship_idx = 0xFFFFu;
	uint16_t tri_vbo_species = 0xFFFFu;

	/* ===== Frustum cull + species sort ===== */
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
		switch (fl->genus) {
			case TIE_GENUS_FIGHTER:
			case TIE_GENUS_TRANSPORT:
			case TIE_GENUS_UTILITY:
			case TIE_GENUS_FREIGHTER:
			case TIE_GENUS_STARSHIP:
			case TIE_GENUS_PLATFORM:
			case TIE_GENUS_GATE:
				break;
			case TIE_GENUS_DEBRIS:
				if (fl->parent_ship_idx == TIE_SPECIES_NONE)
					continue;
				break;
			default:
				/* Projectiles are line-only; the lines pass owns them. */
				continue;
		}
		if (fl->flags & TIE_FOBJ_INVISIBLE)
			continue;
		const TieFlightSpeciesMesh* sm = &g->meshes[species_idx];
		if (!sm->ready)
			continue;
		if (fl->slot == curr->camera.target_obj_slot && curr->camera.zoom_active == 0 &&
			curr->replay_mode != 2)
			continue;
		float position_local[3];
		AeronWorld_LocalI32(curr->camera.world_pos, fl->world_pos, position_local);
		if (TieFlightRenderer_SphereOutsideFrustum(&fr, position_local, sm->bound_radius_world))
			continue;
		draw_keys[draw_count++] = ((uint32_t)species_idx << 16) | (uint32_t)i;
	}
	qsort(draw_keys, draw_count, sizeof draw_keys[0], TieFlightRenderer_CmpDrawKey);

	for (uint32_t k = 0; k < draw_count; ++k) {
		const uint16_t i = (uint16_t)(draw_keys[k] & 0xFFFFu);
		const TieFlightObjectState* fl = &curr->flights[i];
		const uint16_t species_idx =
			(fl->genus == TIE_GENUS_DEBRIS) ? (uint16_t)fl->parent_ship_idx : (uint16_t)fl->ship_idx;
		if (species_idx >= TIE_FLIGHT_MAX_SPECIES)
			continue;
		TieFlightSpeciesMesh* sm = &g->meshes[species_idx];
		if (!sm->ready)
			continue;

		/* Per-craft marking-state offset. Engine calls
		 * drawpol_setmarkingcolors(objects[i].decal_color) before
		 * each craft's draw (draw.c:705) and resets to 0 after
		 * (draw.c:793). decal_color is from FG.camoflage at
		 * creation: 0=OFF, 1=NORMAL, 2=ALT. */
		const float craft_marking_offset = (fl->decal_color == 1)   ? -1.0f
										   : (fl->decal_color == 2) ? +1.0f
																	: 0.0f;
		if (craft_marking_offset != last_marking_offset) {
			ps_u.marking_state_offset = craft_marking_offset;
			Aeron_BindUniformData(pass, AERON_SHADER_STAGE_FRAGMENT, /*slot=*/0, &ps_u, sizeof ps_u);
			last_marking_offset = craft_marking_offset;
		}

		float position_local[3];
		AeronWorld_LocalI32(curr->camera.world_pos, fl->world_pos, position_local);
		TieFlightMeshVertexUniforms vs_u;
		TieFlightMesh_BuildmeshVs(&vs_u, sm, fl, curr, position_local, view_proj);
		vs_u.mesh_table_index = i;
		Aeron_BindUniformData(pass, AERON_SHADER_STAGE_VERTEX, /*slot=*/0, &vs_u, sizeof vs_u);
		/* Same scale the VS build derived; reused below as the local-
		 * light cull's world-units-per-vertex-unit factor. */
		const float craft_scale =
			TIE_CLASSIC_VERTEX_TO_WORLD_UNITS * ((sm->model_scale_shift == 2) ? 4.0f : 1.0f);

		/* Per-craft local-light buffer. Cull the global pool down
		 * to lights that actually reach this craft's bounding
		 * sphere. */
		{
			TieFlightLightBufferGpu lb_craft = { 0 };
			TieFlightClassicLights_CullForCraft(light_pool, light_pool_count, position_local,
												sm->bound_radius_world, craft_scale, &lb_craft);
			Aeron_BindUniformData(pass, AERON_SHADER_STAGE_VERTEX, /*slot=*/1, &lb_craft, sizeof lb_craft);
		}

		/* Same-species bind-skip. The sorted iteration above
		 * groups craft by species, so consecutive iterations
		 * usually share VBO/IBO/SSBO bindings — only rebind on a
		 * species change. */
		const bool species_changed = (species_idx != last_ship_idx);

		/* The fragment shader always declares both decal buffers. For
		 * decal-free geometry, decal_count is zero and the persistent
		 * mesh-table storage buffer is a valid unread placeholder. */
		if (species_changed) {
			AeronBuffer* dsb_records =
				sm->decal_records_sb ? sm->decal_records_sb : g->classic_mesh_tables.buffer;
			AeronBuffer* dsb_verts = sm->decal_verts_sb ? sm->decal_verts_sb : g->classic_mesh_tables.buffer;
			Aeron_BindStorageBuffer(pass, AERON_SHADER_STAGE_FRAGMENT, 0, dsb_records);
			Aeron_BindStorageBuffer(pass, AERON_SHADER_STAGE_FRAGMENT, 1, dsb_verts);
		}

		/* Triangle pass — only when this species has triangle
		 * geometry. Lasers / missiles are line-only meshes and skip
		 * this block; the lines/bolts pass owns their draws. */
		if (sm->vbo && sm->ibo && sm->index_count > 0) {
			if (tri_vbo_species != species_idx) {
				Aeron_BindVertexBuffer(pass, 0, sm->vbo, 0);
				Aeron_BindIndexBuffer(pass, sm->ibo, AERON_INDEX_FORMAT_UINT16, 0);
				tri_vbo_species = species_idx;
			}
			Aeron_DrawIndexed(pass, sm->index_count, 0, 0);
		}

		last_ship_idx = species_idx;
	}
}

/* ============================================================== */
/* PIP single-craft classic draw                                   */
/* ============================================================== */

void TieFlightRenderer_ClassicDrawSingle(TieFlightRenderer* g, AeronCommandBuffer* cmd, AeronRenderPass* pass,
										 const TieSnapshot* snap, const TieFlightObjectState* fl,
										 const float view_proj[16], int pip_w, int pip_h) {
	if (!g || !cmd || !pass || !snap || !fl)
		return;
	if (!g->classic_pip_mesh_pipeline)
		return;
	if (fl->ship_idx >= TIE_FLIGHT_MAX_SPECIES)
		return;
	TieFlightSpeciesMesh* sm = &g->meshes[fl->ship_idx];
	if (!sm->ready)
		return;

	TIE_GPU_MARKER(cmd, "PIP classic mesh");
	Aeron_BindGraphicsPipeline(pass, g->classic_pip_mesh_pipeline);
	Aeron_BindStorageBuffer(pass, AERON_SHADER_STAGE_VERTEX, 0, g->classic_mesh_tables.buffer);

	Aeron_BindTextureSampler(pass, AERON_SHADER_STAGE_FRAGMENT, 0, g->materialcolors_tex, g->sampler_linear);
	Aeron_BindTextureSampler(pass, AERON_SHADER_STAGE_FRAGMENT, 1, g->palette_tex, g->sampler_linear);

	TieFlightMeshPixelUniforms ps_u = { 0 };
	{
		const uint8_t dc = fl->decal_color;
		ps_u.marking_state_offset = (dc == 1) ? -1.0f : (dc == 2) ? +1.0f : 0.0f;
	}
	{
		uint16_t engine_crt_h = snap->cockpit.pip_h;
		if (engine_crt_h == 0)
			engine_crt_h = 240;
		ps_u.line_thickness_mul = 2.0f * (float)pip_h / (float)engine_crt_h;
	}
	ps_u.line_floor_mul = TieFlightMesh_LineFloorMultiplier();
	Aeron_BindUniformData(pass, AERON_SHADER_STAGE_FRAGMENT, 0, &ps_u, sizeof ps_u);
	(void)pip_w;

	/* VS-side light buffer for the per-vertex local-light accumulator. */
	TieFlightClassicLightCandidate light_pool[TIE_FLIGHT_CLASSIC_LIGHT_POOL_CAP];
	const uint32_t light_pool_count =
		TieFlightClassicLights_BuildPool(snap, fl->world_pos, light_pool, TIE_FLIGHT_CLASSIC_LIGHT_POOL_CAP);
	TieFlightLightBufferGpu lb = { 0 };
	{
		const float pip_craft_scale_for_cull =
			TIE_CLASSIC_VERTEX_TO_WORLD_UNITS * ((sm->model_scale_shift == 2) ? 4.0f : 1.0f);
		const float position_local[3] = { 0.0f, 0.0f, 0.0f };
		TieFlightClassicLights_CullForCraft(light_pool, light_pool_count, position_local,
											sm->bound_radius_world, pip_craft_scale_for_cull, &lb);
	}

	TieFlightMeshVertexUniforms vs_u;
	const float pip_position_local[3] = { 0.0f, 0.0f, 0.0f };
	TieFlightMesh_BuildmeshVs(&vs_u, sm, fl, snap, pip_position_local, view_proj);
	vs_u.mesh_table_index = g->classic_pip_table_index;
	Aeron_BindUniformData(pass, AERON_SHADER_STAGE_VERTEX, 0, &vs_u, sizeof vs_u);

	Aeron_BindUniformData(pass, AERON_SHADER_STAGE_VERTEX, 1, &lb, sizeof lb);

	/* Bind valid storage placeholders when the target has no decals. */
	{
		AeronBuffer* dsb_records =
			sm->decal_records_sb ? sm->decal_records_sb : g->classic_mesh_tables.buffer;
		AeronBuffer* dsb_verts = sm->decal_verts_sb ? sm->decal_verts_sb : g->classic_mesh_tables.buffer;
		Aeron_BindStorageBuffer(pass, AERON_SHADER_STAGE_FRAGMENT, 0, dsb_records);
		Aeron_BindStorageBuffer(pass, AERON_SHADER_STAGE_FRAGMENT, 1, dsb_verts);
	}

	/* Triangle pass. Lines + bolts are handled by
	 * TieFlightRenderer_LinesDrawSingle. */
	if (sm->vbo && sm->ibo && sm->index_count > 0) {
		Aeron_BindVertexBuffer(pass, 0, sm->vbo, 0);
		Aeron_BindIndexBuffer(pass, sm->ibo, AERON_INDEX_FORMAT_UINT16, 0);
		Aeron_DrawIndexed(pass, sm->index_count, 0, 0);
	}
}
