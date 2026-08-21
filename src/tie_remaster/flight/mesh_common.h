#ifndef TIE_REMASTER_FLIGHT_MESH_COMMON_H
#define TIE_REMASTER_FLIGHT_MESH_COMMON_H

/*
 * Per-frame helpers shared between the OPT and glTF mesh draw passes.
 *
 * Both pipelines walk the same snapshot data and emit the same
 * articulation table; only the per-species cache they consult and the
 * texture/material binding topology differ. The functions here factor
 * out the parts that don't depend on which pipeline is rendering:
 *
 *   - craft_is_flight_drawable(): genus / visibility / camera-zoom /
 *     frustum-cull filter. Each pipeline supplies a tiny resolver
 *     callback that maps species_idx to (ready?, bound_radius_world).
 *
 *   - build_mesh_table(): AeronSceneMeshTable construction —
 *     visibility/emissive defaults plus direct model-component rotation.
 *
 */

#include <stdbool.h>
#include <stdint.h>

#include "aeron/asset/opt_model.h"

#include "tie_remaster/flight/renderer_internal.h"

struct TieSnapshot;
struct TieFlightObjectState;

#ifdef __cplusplus
extern "C" {
#endif

/* Build the retained-scene mesh-table payload for one craft. */
void TieFlightMesh_BuildmeshTable(const AeronMeshRot* mesh_rot, const struct TieFlightObjectState* fl,
								  const struct TieSnapshot* curr, AeronSceneMeshTable* out);

typedef enum TieFlightMeshTablePurpose {
	TIE_FLIGHT_MESH_TABLE_MAIN,
	TIE_FLIGHT_MESH_TABLE_PIP,
} TieFlightMeshTablePurpose;

void TieFlightMesh_BuildclassicMeshTable(const TieFlightSpeciesMesh* mesh,
										 const struct TieFlightObjectState* flight,
										 const struct TieSnapshot* snapshot,
										 TieFlightMeshTablePurpose purpose, AeronSceneMeshTable* out);

/* Build the per-craft TieFlightMeshVertexUniforms for the classic OPT / line paths
 * (view_proj copy, craft_to_world from quaternion plus local position at classic half scale ×
 * model_scale_shift, its inverse world_to_craft, and the directional /
 * gouraud / gate-frame flags). The cooked-glb path uses its own
 * GltfMeshVSUniforms derivation and does not share this. */
void TieFlightMesh_BuildmeshVs(TieFlightMeshVertexUniforms* out, const TieFlightSpeciesMesh* sm,
							   const struct TieFlightObjectState* fl, const struct TieSnapshot* curr,
							   const float position_local[3], const float view_proj[16]);

/* Projectiles glow at 2.0; other meshes use 1.0. mesh_type is ignored. */
float TieFlightMesh_EmissiveforMesh(uint8_t genus, uint8_t mesh_type);

/* Bolt eye-z extractor. The composed view_proj's row 3 (indices
 * 12..15 in row-major) IS the engine's transfm2_geteyez extractor in
 * float: `eye_z = vp[12]*x + vp[13]*y + vp[14]*z + vp[15]`. Same sign
 * convention as the line VS's clip.w. Positive for in-front bolts. */
static inline float TieFlightRenderer_EyeZ(const float view_proj[16], const float pos[3]) {
	return view_proj[12] * pos[0] + view_proj[13] * pos[1] + view_proj[14] * pos[2] + view_proj[15];
}

/* Pick a line-LOD segment by walking the per-mesh `line_lods` table
 * the way the engine's `draw_getdetailptr` walks ShipMeshLOD. Returns
 * NULL when the mesh has no LOD chain — caller draws the whole IBO. */
const TieFlightShipModelLineLod* TieFlightMeshCommon_PickLineLod(const TieFlightSpeciesMesh* sm, float eye_z);

/* Engine-pixel floor multiplier for the line VS / FS line-marking
 * branch. Default 0.5 = engine-SVGA-faithful (1 SVGA px of screen
 * coverage). Override at startup with `TIE_LINE_FLOOR_MUL=<float>`.
 * Cached on first call. Clamped to [0, 4]. */
float TieFlightMesh_LineFloorMultiplier(void);

#ifdef __cplusplus
}
#endif

#endif
