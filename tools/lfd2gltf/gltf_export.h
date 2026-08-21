/*
 * Ship → glTF 2.0 export.
 *
 * Takes a parsed TieFlightShipModel from the ShipModelData converter.
 * and emits a glTF JSON + .bin pair that Blender / any glTF 2.0
 * importer can load. The component structure of the source ship —
 * one ShipModelMesh per logical part (MainHull, Wing, etc.) — is
 * preserved as a glTF node hierarchy so an artist can identify and
 * edit individual components.
 *
 * Per-primitive material assignment: triangles are grouped by
 * (mesh_index, material_id). One glTF material per material_id used,
 * coloured by HSL hue-rotation so the artist can visually
 * distinguish surface types in Blender. No textures, no PBR tuning
 * — just a debug colour identifying the engine material slot.
 *
 * Normal channel: writes TieFlightVertex.vertex_normal, rebuilt through
 * Aeron's runtime normal algorithm.
 */

#ifndef TIE_LFD2GLTF_EXPORT_H
#define TIE_LFD2GLTF_EXPORT_H

#include <stdbool.h>
#include <stddef.h>

#include "tie_runtime/flight_assets/ship_model_converter.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Export `pm` as `<out_dir>/<ship_name>.gltf` + `<ship_name>.bin`.
 * `ship_name` becomes the root node name and the buffer URI stem.
 * Returns true on success, false on any failure (fopen / cgltf
 * error). Diagnostic messages go to stderr. */
bool TieLfd2GltfExport_Ship(const TieFlightShipModel* pm, const char* out_dir, const char* ship_name);

#ifdef __cplusplus
}
#endif

#endif /* TIE_LFD2GLTF_EXPORT_H */
