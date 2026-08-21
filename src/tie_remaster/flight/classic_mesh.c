/*
 * Per-species DOS ShipModelData conversion, upload, and release. Mission
 * synchronization warms every required model before publishing the generation.
 */

#include "tie_remaster/flight/renderer_internal.h"
#include "tie_remaster/gpu_debug.h"

#include "aeron/render.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tie_runtime/audio/config.h"
#include "tie_runtime/diagnostics/diagnostics.h"
#include "tie_runtime/display/classic_display.h"
#include "tie_runtime/display/classic_framebuffer.h"
#include "tie_runtime/flight_assets/model_types.h"
#include "tie_runtime/input/input.h"
#include "tie_runtime/runtime/exports.h"
#include "tie_runtime/runtime/profile.h"
#include "tie_runtime/storage/storage.h"

void TieFlightRenderer_ReleaseSpeciesMesh(TieFlightRenderer* g, TieFlightSpeciesMesh* m) {
	if (m->owns_resources) {
		if (m->vbo)
			Aeron_DestroyBuffer(m->vbo);
		if (m->ibo)
			Aeron_DestroyBuffer(m->ibo);
		if (m->line_vbo)
			Aeron_DestroyBuffer(m->line_vbo);
		if (m->line_ibo)
			Aeron_DestroyBuffer(m->line_ibo);
		if (m->decal_records_sb)
			Aeron_DestroyBuffer(m->decal_records_sb);
		if (m->decal_verts_sb)
			Aeron_DestroyBuffer(m->decal_verts_sb);
		free(m->mesh_rot);
		free(m->line_lods);
	}
	memset(m, 0, sizeof *m);
}

bool TieFlightRenderer_EnsureSpeciesMesh(TieFlightRenderer* g, AeronCommandBuffer* cmd, uint16_t species_idx,
										 const void* blob, size_t blob_size, bool is_laser) {
	if (species_idx >= TIE_FLIGHT_MAX_SPECIES)
		return false;
	TieFlightSpeciesMesh* m = &g->meshes[species_idx];
	if (m->ready)
		return true;
	if (m->tried)
		return false;
	m->tried = true;
	m->owns_resources = true;
	if (!blob || blob_size == 0)
		return false;

	TieFlightShipModel pm = { 0 };
	bool built = is_laser
					 ? TieShipModelConverter_BuildLaser(blob, blob_size, &pm)
					 : TieShipModelConverter_Build(blob, blob_size, g->assets->smooth_angle_degrees, &pm);
	if (!built)
		return false;

	/* Lasers produce line-only geometry (vertex_count == 0). Ships
	 * require triangles. Reject only the "expected geometry missing"
	 * case for the matching path. */
	if (is_laser) {
		if (pm.line_vertex_count == 0 || pm.line_index_count == 0) {
			TieShipModelConverter_Free(&pm);
			return false;
		}
	} else {
		if (pm.vertex_count == 0 || pm.index_count == 0) {
			TieShipModelConverter_Free(&pm);
			return false;
		}
	}

	/* Group every per-species buffer upload under a single debug-label
	 * scope so a frame capture attributes the 4–6 copy passes
	 * (vbo/ibo/line_vbo/line_ibo/decal_records_sb/decal_verts_sb) to
	 * the loading species rather than to a flat sibling list. */
	char gnbuf[48];
	snprintf(gnbuf, sizeof gnbuf, "Flight species[%u] load", (unsigned)species_idx);
	TIE_GPU_PUSH(cmd, gnbuf);

	if (pm.vertex_count > 0 && pm.index_count > 0) {
		uint32_t vbo_size = (uint32_t)(pm.vertex_count * sizeof(TieFlightVertex));
		uint32_t ibo_size = (uint32_t)(pm.index_count * sizeof(uint16_t));
		m->vbo = TieFlightRenderer_CreateBuffer(AERON_BUFFER_USAGE_VERTEX, vbo_size);
		m->ibo = TieFlightRenderer_CreateBuffer(AERON_BUFFER_USAGE_INDEX, ibo_size);
		char nbuf[40];
		snprintf(nbuf, sizeof nbuf, "flight.species[%u].vbo", (unsigned)species_idx);
		TIE_GPU_NAME_BUFFER(g->device, m->vbo, nbuf);
		snprintf(nbuf, sizeof nbuf, "flight.species[%u].ibo", (unsigned)species_idx);
		TIE_GPU_NAME_BUFFER(g->device, m->ibo, nbuf);
		if (!m->vbo || !m->ibo) {
			Aeron_RequestFatalRendererError("classic flight mesh buffer creation");
			TIE_GPU_POP(cmd); /* "Flight species[N] load" */
			TieShipModelConverter_Free(&pm);
			return false;
		}
		if (!TieFlightRenderer_UploadToBuffer(cmd, m->vbo, pm.vertices, vbo_size) ||
			!TieFlightRenderer_UploadToBuffer(cmd, m->ibo, pm.indices, ibo_size)) {
			Aeron_RequestFatalRendererError("classic flight mesh upload");
			TIE_GPU_POP(cmd); /* "Flight species[N] load" */
			TieShipModelConverter_Free(&pm);
			return false;
		}
	}
	/* Optional line geometry: ships without antennas land here with
	 * line_vertex_count == 0 and we leave line_vbo/_ibo NULL — the
	 * per-craft draw skips the line pass when the count is zero. */
	if (pm.line_vertex_count > 0 && pm.line_index_count > 0) {
		uint32_t lvb_size = (uint32_t)(pm.line_vertex_count * sizeof(TieFlightLineVertex));
		uint32_t lib_size = (uint32_t)(pm.line_index_count * sizeof(uint16_t));
		m->line_vbo = TieFlightRenderer_CreateBuffer(AERON_BUFFER_USAGE_VERTEX, lvb_size);
		m->line_ibo = TieFlightRenderer_CreateBuffer(AERON_BUFFER_USAGE_INDEX, lib_size);
		char lnbuf[44];
		snprintf(lnbuf, sizeof lnbuf, "flight.species[%u].line_vbo", (unsigned)species_idx);
		TIE_GPU_NAME_BUFFER(g->device, m->line_vbo, lnbuf);
		snprintf(lnbuf, sizeof lnbuf, "flight.species[%u].line_ibo", (unsigned)species_idx);
		TIE_GPU_NAME_BUFFER(g->device, m->line_ibo, lnbuf);
		if (!m->line_vbo || !m->line_ibo ||
			!TieFlightRenderer_UploadToBuffer(cmd, m->line_vbo, pm.line_vertices, lvb_size) ||
			!TieFlightRenderer_UploadToBuffer(cmd, m->line_ibo, pm.line_indices, lib_size)) {
			if (m->line_vbo)
				Aeron_DestroyBuffer(m->line_vbo);
			if (m->line_ibo)
				Aeron_DestroyBuffer(m->line_ibo);
			m->line_vbo = NULL;
			m->line_ibo = NULL;
			Aeron_RequestFatalRendererError("classic flight line upload");
			TIE_GPU_POP(cmd);
			TieShipModelConverter_Free(&pm);
			return false;
		} else {
			m->line_index_count = pm.line_index_count;
			/* Copy the LOD table so the per-bolt draw can pick a
			 * segment by eye-z. Non-laser meshes leave this NULL/0 and
			 * the renderer draws the whole IBO. */
			if (pm.line_lod_count > 0 && pm.line_lods) {
				size_t sz = (size_t)pm.line_lod_count * sizeof(*pm.line_lods);
				m->line_lods = (TieFlightShipModelLineLod*)malloc(sz);
				if (m->line_lods) {
					memcpy(m->line_lods, pm.line_lods, sz);
					m->line_lod_count = pm.line_lod_count;
				} else {
					Aeron_RequestFatalRendererError("classic flight line metadata allocation");
					TIE_GPU_POP(cmd);
					TieShipModelConverter_Free(&pm);
					return false;
				}
			}
		}
	}
	/* Decal upload — per-face point-in-polygon overlay consumed by the
	 * mesh fragment shader. Two GRAPHICS_STORAGE_READ buffers: a
	 * TieFlightDecal record table + a TieFlightDecalVertex pool indexed via
	 * each record's vert_offset/vert_count window. NULL on failure or
	 * empty payload. Allocation or upload failure rejects the required model. */
	if (pm.decal_count > 0 && pm.decal_vert_count > 0) {
		uint32_t drs_size = (uint32_t)(pm.decal_count * sizeof(TieFlightDecal));
		uint32_t dvs_size = (uint32_t)(pm.decal_vert_count * sizeof(TieFlightDecalVertex));
		m->decal_records_sb = TieFlightRenderer_CreateBuffer(AERON_BUFFER_USAGE_STORAGE, drs_size);
		m->decal_verts_sb = TieFlightRenderer_CreateBuffer(AERON_BUFFER_USAGE_STORAGE, dvs_size);
		char dnbuf[52];
		snprintf(dnbuf, sizeof dnbuf, "flight.species[%u].decal_records_sb", (unsigned)species_idx);
		TIE_GPU_NAME_BUFFER(g->device, m->decal_records_sb, dnbuf);
		snprintf(dnbuf, sizeof dnbuf, "flight.species[%u].decal_verts_sb", (unsigned)species_idx);
		TIE_GPU_NAME_BUFFER(g->device, m->decal_verts_sb, dnbuf);
		if (!m->decal_records_sb || !m->decal_verts_sb ||
			!TieFlightRenderer_UploadToBuffer(cmd, m->decal_records_sb, pm.decals, drs_size) ||
			!TieFlightRenderer_UploadToBuffer(cmd, m->decal_verts_sb, pm.decal_verts, dvs_size)) {
			if (m->decal_records_sb)
				Aeron_DestroyBuffer(m->decal_records_sb);
			if (m->decal_verts_sb)
				Aeron_DestroyBuffer(m->decal_verts_sb);
			m->decal_records_sb = NULL;
			m->decal_verts_sb = NULL;
			Aeron_RequestFatalRendererError("classic flight decal upload");
			TIE_GPU_POP(cmd);
			TieShipModelConverter_Free(&pm);
			return false;
		} else {
			m->decal_count = pm.decal_count;
			m->decal_vert_count = pm.decal_vert_count;
		}
	}

	m->vertex_count = pm.vertex_count;
	m->index_count = pm.index_count;
	m->model_scale_shift = pm.model_scale_shift;
	/* Pre-compute the conservative bounding-sphere radius in world
	 * units. Used by per-frame frustum culling in the main flight pass.
	 * Farthest AABB corner from craft origin = magnitude of the
	 * componentwise-max-absolute extent vector. */
	{
		const float ex =
			fabsf(pm.bound_min[0]) > fabsf(pm.bound_max[0]) ? fabsf(pm.bound_min[0]) : fabsf(pm.bound_max[0]);
		const float ey =
			fabsf(pm.bound_min[1]) > fabsf(pm.bound_max[1]) ? fabsf(pm.bound_min[1]) : fabsf(pm.bound_max[1]);
		const float ez =
			fabsf(pm.bound_min[2]) > fabsf(pm.bound_max[2]) ? fabsf(pm.bound_min[2]) : fabsf(pm.bound_max[2]);
		const float ms_factor = (pm.model_scale_shift == 2) ? 4.0f : 1.0f;
		m->bound_radius_world =
			sqrtf(ex * ex + ey * ey + ez * ez) * TIE_CLASSIC_VERTEX_TO_WORLD_UNITS * ms_factor;
	}
	/* Copy the per-mesh rotation table for use during per-craft draws.
	 * converter's owned copy gets freed below. has_any_rotation gets
	 * set if ANY mesh in this species has rotation_offset (= turret
	 * or similar animated part) — the per-craft draw uses this to
	 * skip the uniform build/push for fully static ships. */
	if (pm.mesh_count > 0 && pm.mesh_rot) {
		uint32_t mc = pm.mesh_count;
		if (mc > AERON_MAX_MESH_SLOTS)
			mc = AERON_MAX_MESH_SLOTS;
		m->mesh_rot = (AeronMeshRot*)calloc(mc, sizeof *m->mesh_rot);
		if (m->mesh_rot) {
			memcpy(m->mesh_rot, pm.mesh_rot, mc * sizeof *m->mesh_rot);
			m->mesh_count = mc;
			for (uint32_t k = 0; k < mc; ++k) {
				if (m->mesh_rot[k].has_rotation) {
					m->has_any_rotation = true;
					break;
				}
			}
		} else {
			Aeron_RequestFatalRendererError("classic flight rotation metadata allocation");
			TIE_GPU_POP(cmd);
			TieShipModelConverter_Free(&pm);
			return false;
		}
	}
	m->ready = true;
	TIE_GPU_POP(cmd); /* "Flight species[N] load" */
	TieShipModelConverter_Free(&pm);
	return true;
}
