#ifndef TIE_RUNTIME_FLIGHT_ASSETS_MODEL_ACCESS_H
#define TIE_RUNTIME_FLIGHT_ASSETS_MODEL_ACCESS_H

#include "tie_runtime/flight_assets/model_types.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Host model view replacing the original TIE98 OPT registry handle lock.
 * Exits through shell_programexit when the model cannot be acquired. */
const TieFlightModelView* modelmesh_require_model(uint16_t model_type);
/* The retained TIE95 CraftData has 39 component slots plus lightning. */
void modelmesh_require_craft_capacity(uint16_t model_type);

/* Replaces TIE98's temporary rewrite of a locked OPT header. The native OPT
 * cache owns parsed host structures rather than writable file images, so
 * DRAW_drawhyperstar and the briefing previews select their model here;
 * FlightModel_Draw_Object draws it instead of the ship's loaded model. */
extern const Tie98OptimizedPolyObject* g_flightModelOverride;

/* TIE98 BPFLIGHT preview model slots. The original reloads a movable OPT
 * handle in g_loaded_model_handles[slot]; the host native-OPT cache owns an
 * immutable equivalent for the same IVFILES name. */
const Tie98OptimizedPolyObject* TieFlightAssets_PreviewModel(int slot);
/* Returns nonzero when the named model was acquired into `slot`. */
int TieFlightAssets_LoadPreviewModel(int slot, const char* opt_name);
void TieFlightAssets_ClearPreviewModels(void);
/* Original ModelBounds_GetMaxExtent of the model in `slot`; exits through
 * shell_programexit when it cannot be computed. */
int TieFlightAssets_PreviewModelMaxExtent(int slot);
/* Max extent of the primary preview model, 0 while none is loaded. */
int tie98_preview_primary_model_max_extent(void);

/* The original ModelMesh explosion-type setters mutate the loaded OPT mesh
 * descriptor. Host model views are immutable, so these process-lifetime
 * descriptor bits are retained beside them. */
uint8_t TieFlightAssets_MeshExplosionTypeOverride(uint16_t model_type, int mesh_index);
void TieFlightAssets_EnableMeshExplosionType(uint16_t model_type, int mesh_index, uint8_t flag);

/* TIE98 passes &staticobjects[slot] to the OPT object draw entry points,
 * whose static branch reads the packed StaticObject position. The host keeps
 * the TIE95 FlightObject layout for both editions, so each static slot has a
 * stable full-width view carrying the widened position and model index. */
struct FlightObject* TieFlightAssets_StaticRenderObject(uint16_t slot_idx);

#ifdef __cplusplus
}
#endif

#endif
