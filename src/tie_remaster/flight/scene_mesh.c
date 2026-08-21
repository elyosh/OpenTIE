/*
 * Per-species scene-mesh upload for cooked GLB and runtime-converted OPT.
 *
 * Uploads the render subobject owned by the source-independent flight-model
 * cache and builds the renderer-side per-species cache used by the draw pass.
 *
 * `tried` prevents duplicate work within one mission generation. A failed
 * required build also raises the selected provider's fatal error.
 */

#include "aeron/aeron.h"
#include "aeron/asset/flight_model.h"
#include "aeron/log.h"
#include "aeron/render.h"
#include "tie_remaster/flight/mesh_draw.h"
#include "tie_remaster/flight/renderer_internal.h"
#include "tie_runtime/runtime/exports.h"

#include <stdio.h>
#include <string.h>

/* Cache-size sanity: identical to the OPT cache so they share lookup
 * indices. */
_Static_assert(TIE_FLIGHT_MAX_SPECIES == TIE_SPECIES_COUNT,
			   "TIE_FLIGHT_MAX_SPECIES must equal TIE_SPECIES_COUNT");

/* ===== Release =================================================== */

void TieFlightRenderer_ReleaseSceneSpeciesShip(TieFlightRenderer* g, TieFlightSpeciesSceneShip* s) {
	if (!g || !s)
		return;
	if (s->mesh)
		AeronScene_MeshDestroy(s->mesh);
	memset(s, 0, sizeof *s);
}

/* ===== Per-species ensure ========================================== */

bool TieFlightRenderer_EnsureSceneSpeciesShip(TieFlightRenderer* g, AeronCommandBuffer* cmd,
											  uint16_t species_idx) {
	if (!g || !g->assets || species_idx >= TIE_FLIGHT_MAX_SPECIES)
		return false;
	if (TieFlightAssetSource_IsTie95(g->assets))
		return false;

	TieFlightSpeciesSceneShip* s = &g->scene_ships[species_idx];
	if (s->ready)
		return true;
	if (s->tried)
		return false;

	s->tried = true;

	char error[768];
	const AeronFlightModel* ship =
		TieFlightAssetSource_AcquireModel(g->assets, species_idx, error, sizeof error);
	if (!ship) {
		Aeron_RequestFatalError("Flight Asset Error", error);
		return false;
	}

	/* AeronSceneMesh copies the persistent articulation, glow and bounds data;
	 * upload-only CPU buffers can be released afterward. */
	char label[64];
	snprintf(label, sizeof label, "flight.model[%u]", (unsigned)species_idx);
	AeronSceneMeshCreateStatus status = AERON_SCENE_MESH_CREATE_SUCCESS;
	s->mesh = AeronScene_MeshCreate(cmd, ship, label, &status);
	if (!s->mesh) {
		snprintf(error, sizeof error,
				 "flight model source %s: scene-mesh creation failed for "
				 "species %u (%s)",
				 g->assets->name, species_idx,
				 status == AERON_SCENE_MESH_CREATE_RESOURCE_FAILURE ? "GPU resource failure"
																	: "invalid converted model");
		Aeron_RequestFatalError("Flight Asset Error", error);
		return false;
	}
	TieFlightAssetSource_ReleaseModelRenderData(g->assets, species_idx);

	s->ready = true;
	Aeron_LogInfo("tie.flight", "uploaded species %u: %u vertices, %u primitives, %u materials, %u variants",
				  (unsigned)species_idx, s->mesh->vertex_count, s->mesh->total_prim_count,
				  s->mesh->material_count, s->mesh->variant_count);
	return true;
}

/* ===== Inspector accessors ========================================== */

bool TieFlightRenderer_UsesSceneModels(const TieFlightRenderer* g) {
	return g ? g->scene_model_backend : false;
}

void TieFlightRenderer_SceneRequestReload(TieFlightRenderer* g) {
	if (g)
		g->scene_reload_all = true;
}

void TieFlightRenderer_SceneRequestReloadOne(TieFlightRenderer* g, uint16_t species_idx) {
	if (!g || species_idx >= TIE_FLIGHT_MAX_SPECIES)
		return;
	g->scene_reload_one_idx = species_idx;
}

bool TieFlightRenderer_SceneConsumeReload(TieFlightRenderer* g) {
	if (!g)
		return false;
	if (!g->scene_reload_all && g->scene_reload_one_idx >= TIE_FLIGHT_MAX_SPECIES)
		return false;
	Aeron_LogInfo("tie.flight", "model reload deferred until the next mission generation");
	g->scene_reload_all = false;
	g->scene_reload_one_idx = 0xFFFFu;
	return false;
}

bool TieFlightRenderer_SceneSpeciesInfo(const TieFlightRenderer* g, uint16_t species_idx,
										TieFlightSceneSpeciesInfo* out) {
	if (!g || !out || species_idx >= TIE_FLIGHT_MAX_SPECIES)
		return false;
	memset(out, 0, sizeof *out);
	out->species_idx = species_idx;

	const TieFlightSpeciesSceneShip* s = &g->scene_ships[species_idx];
	out->ready = s->ready;
	out->tried = s->tried;
	if (s->mesh) {
		out->material_count = s->mesh->material_count;
		out->variant_count = s->mesh->variant_count;
		out->total_vertex_count = s->mesh->vertex_count;
		out->total_index_count = s->mesh->index_count;
		out->primitive_count = s->mesh->total_prim_count;
		memcpy(out->bound_min, s->mesh->bound_min, sizeof out->bound_min);
		memcpy(out->bound_max, s->mesh->bound_max, sizeof out->bound_max);
	}

	const TieFlightAssetBundle* catalog = g->assets->catalog;
	if (catalog) {
		const TieFlightAssetEntry* e = TieFlightAssets_Find(catalog, species_idx);
		if (e)
			snprintf(out->asset_path, sizeof out->asset_path, "%s", e->path);
	}
	const char* sn = tie_species_symbolic_name(species_idx);
	const char* dn = tie_species_display_name(species_idx);
	if (sn)
		snprintf(out->symbolic_name, sizeof out->symbolic_name, "%s", sn);
	if (dn)
		snprintf(out->display_name, sizeof out->display_name, "%s", dn);
	return true;
}
