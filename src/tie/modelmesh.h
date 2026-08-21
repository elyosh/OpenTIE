#ifndef TIE_MODELMESH_H
#define TIE_MODELMESH_H

#include <stdbool.h>
#include <stdint.h>

#include "tie_runtime/audio/config.h"
#include "tie_runtime/diagnostics/diagnostics.h"
#include "tie_runtime/display/classic_display.h"
#include "tie_runtime/display/classic_framebuffer.h"
#include "tie_runtime/flight_assets/model_types.h"
#include "tie_runtime/input/input.h"
#include "tie_runtime/runtime/exports.h"
#include "tie_runtime/runtime/profile.h"
#include "tie_runtime/storage/storage.h"

enum TieModelMeshType {
	TIE_MESH_DEFAULT = 0,
	TIE_MESH_MAIN_HULL = 1,
	TIE_MESH_WING = 2,
	TIE_MESH_FUSELAGE = 3,
	TIE_MESH_GUN_TURRET = 4,
	TIE_MESH_SMALL_GUN = 5,
	TIE_MESH_ENGINE = 6,
	TIE_MESH_BRIDGE = 7,
	TIE_MESH_SHIELD_GENERATOR = 8,
	TIE_MESH_ENERGY_GENERATOR = 9,
	TIE_MESH_LAUNCHER = 10,
	TIE_MESH_COMMUNICATIONS = 11,
	TIE_MESH_BEAM_SYSTEM = 12,
	TIE_MESH_COMMAND_BEAM = 13,
	TIE_MESH_DOCKING_PLATFORM = 14,
	TIE_MESH_LANDING_PLATFORM = 15,
	TIE_MESH_HANGAR = 16,
	TIE_MESH_CARGO_POD = 17,
	TIE_MESH_MISC_HULL = 18,
	TIE_MESH_ANTENNA = 19,
	TIE_MESH_ROTARY_WING = 20,
	TIE_MESH_ROTARY_GUN_TURRET = 21,
	TIE_MESH_ROTARY_LAUNCHER = 22,
	TIE_MESH_ROTARY_COMMUNICATIONS = 23,
	TIE_MESH_ROTARY_BEAM_SYSTEM = 24,
	TIE_MESH_ROTARY_COMMAND_BEAM = 25,
};

const TieFlightModelView* modelmesh_require_model(uint16_t model_type);
void modelmesh_require_craft_capacity(uint16_t model_type);

int modelmesh_getcount(uint16_t model_type);
int modelmesh_gettype(uint16_t model_type, int mesh_index);
int modelmesh_getobjecttypemeshtype(uint16_t model_type, int mesh_index);
int modelmesh_getvertexcount(uint16_t model_type, int mesh_index);
int modelmesh_getvertexx(uint16_t model_type, int mesh_index, int vertex_index);
int modelmesh_getvertexy(uint16_t model_type, int mesh_index, int vertex_index);
int modelmesh_getvertexz(uint16_t model_type, int mesh_index, int vertex_index);
int modelmesh_getcenterx(uint16_t model_type, int mesh_index);
int modelmesh_getcentery(uint16_t model_type, int mesh_index);
int modelmesh_getcenterz(uint16_t model_type, int mesh_index);
int modelmesh_getboundsminx(uint16_t model_type, int mesh_index);
int modelmesh_getboundsminy(uint16_t model_type, int mesh_index);
int modelmesh_getboundsminz(uint16_t model_type, int mesh_index);
int modelmesh_getboundsmaxx(uint16_t model_type, int mesh_index);
int modelmesh_getboundsmaxy(uint16_t model_type, int mesh_index);
int modelmesh_getboundsmaxz(uint16_t model_type, int mesh_index);
int modelmesh_gettargetid(uint16_t model_type, int mesh_index);
int modelmesh_getcomponentfocusx(uint16_t model_type, int mesh_index);
int modelmesh_getcomponentfocusy(uint16_t model_type, int mesh_index);
int modelmesh_getcomponentfocusz(uint16_t model_type, int mesh_index);
int modelmesh_getcomponentmaxextent(uint16_t model_type, int mesh_index);
int modelmesh_isobjecttypemeshdamageable(uint16_t model_type, int mesh_index);
int modelmesh_hasexplosiontype1(uint16_t model_type, int mesh_index);
void modelmesh_enableexplosiontype1(uint16_t model_type, int mesh_index);
void modelmesh_enableexplosiontype2(uint16_t model_type, int mesh_index);
const TieModelRotationScale* modelmesh_getrotscaledata(uint16_t model_type, int mesh_index);
int modelmesh_counthardpoints(uint16_t model_type, int mesh_index);
int modelmesh_getalternatehardpointindex(uint16_t model_type, int mesh_index, int hardpoint_index);
/* Original TIE98 getter order: (OPT X, -OPT Y, OPT Z), not (side, up, forward). */
void modelmesh_gethardpoint(uint16_t model_type, int mesh_index, int hardpoint_index, int* type, int* x,
							int* y, int* z);
int modelmesh_findbridgeindex(uint16_t model_type);

/* Point coordinates use the ModelMesh_GetHardpoint order above. */
void modelmesh_applyanimatedmeshrotationtopoint(int angle, uint16_t model_type, int mesh_index, int x, int y,
												int z, int* out_x, int* out_y, int* out_z);

#endif
