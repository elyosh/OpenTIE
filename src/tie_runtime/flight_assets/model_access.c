#include "tie_runtime/flight_assets/model_access.h"
#include "tie/shell.h"
#include "tie/tie.h"
#include "tie_runtime/flight_assets/native_opt.h"
#include "tie_runtime/flight_assets/service.h"
#include "tie_runtime/runtime/exports.h"

#include <string.h>

enum {
	MODEL_MESH_CRAFT_SLOTS = 40,
};

const Tie98OptimizedPolyObject* g_flightModelOverride;

static FlightObject s_static_render_objects[NUM_STATIC_OBJECTS];
static const Tie98OptimizedPolyObject* s_preview_models[2];
static uint16_t s_preview_model_types[2];

const Tie98OptimizedPolyObject* TieFlightAssets_PreviewModel(int slot) { return s_preview_models[slot]; }

int TieFlightAssets_LoadPreviewModel(int slot, const char* opt_name) {
	s_preview_models[slot] = TieNativeOpt_AcquireNamed(opt_name, &s_preview_model_types[slot]);
	return s_preview_models[slot] != NULL;
}

void TieFlightAssets_ClearPreviewModels(void) {
	s_preview_models[0] = NULL;
	s_preview_models[1] = NULL;
}

int TieFlightAssets_PreviewModelMaxExtent(int slot) {
	char extent_error[768];
	int extent;

	if (!TieFlightAssets_Tie98OriginalMaxExtent(s_preview_model_types[slot], &extent, extent_error,
												sizeof extent_error))
		shell_programexit(extent_error);
	return extent;
}

int tie98_preview_primary_model_max_extent(void) {
	if (!s_preview_models[0])
		return 0;
	return TieFlightAssets_PreviewModelMaxExtent(0);
}

static uint8_t s_explosion_type_overrides[TIE_SPECIES_COUNT][MODEL_MESH_CRAFT_SLOTS];

const TieFlightModelView* modelmesh_require_model(uint16_t model_type) {
	TieFlightModelApi models = TieFlightAssets_ModelApi();
	char error[768];
	const TieFlightModelView* model = models.acquire(models.context, model_type, error, sizeof error);
	if (!model)
		shell_programexit(error);
	return model;
}

void modelmesh_require_craft_capacity(uint16_t model_type) {
	if (modelmesh_require_model(model_type)->mesh_count > 39)
		shell_programexit("TIE98 OPT craft has more than 39 meshes");
}

uint8_t TieFlightAssets_MeshExplosionTypeOverride(uint16_t model_type, int mesh_index) {
	if (model_type < TIE_SPECIES_COUNT && mesh_index >= 0 && mesh_index < MODEL_MESH_CRAFT_SLOTS)
		return s_explosion_type_overrides[model_type][mesh_index];
	return 0;
}

void TieFlightAssets_EnableMeshExplosionType(uint16_t model_type, int mesh_index, uint8_t flag) {
	if (model_type >= TIE_SPECIES_COUNT || mesh_index < 0 || mesh_index >= MODEL_MESH_CRAFT_SLOTS)
		return;
	s_explosion_type_overrides[model_type][mesh_index] |= flag;
}

FlightObject* TieFlightAssets_StaticRenderObject(uint16_t slot_idx) {
	const StaticObject* source = &staticobjects[slot_idx];
	FlightObject* object = &s_static_render_objects[slot_idx];

	memset(object, 0, sizeof *object);
	object->genus = source->ship_class;
	object->ship_idx = source->species;
	object->world_x = (int32_t)source->world_x << 8;
	object->world_y = (int32_t)source->world_y << 8;
	object->world_z = (int32_t)source->world_z << 8;
	object->self_idx = (int16_t)(slot_idx + OBJ_REF_STATIC_BASE);
	return object;
}
