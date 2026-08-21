#ifndef TIE_REMASTER_FLIGHT_MESH_DRAW_H
#define TIE_REMASTER_FLIGHT_MESH_DRAW_H

/* Runtime inspection and tuning API for the scene-model PBR mesh path.
 * Accessors operate on an explicit renderer instance. */

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct TieFlightRenderer TieFlightRenderer;

bool TieFlightRenderer_UsesSceneModels(const TieFlightRenderer* g);

/* --- Per-species probe ----------------------------------------------
 *
 * Inspector view into the glTF cache. Indexed by species_idx (the
 * snapshot ship_idx, 0..160). Returns false when species_idx is out
 * of range; `ready` distinguishes "no glTF loaded yet" from "fully
 * populated".
 *
 * Carries both the symbolic ("X_WING") and display ("X-wing") names
 * so the inspector doesn't need to reach into host.h. */
typedef struct TieFlightSceneSpeciesInfo {
	uint16_t species_idx;
	bool ready;
	bool tried;
	uint32_t primitive_count;
	uint32_t material_count;
	uint32_t variant_count;
	/* Per-ship totals. */
	uint32_t total_vertex_count;
	uint32_t total_index_count;
	uint32_t hardpoint_count;
	float bound_min[3];
	float bound_max[3];
	/* Catalog-relative source path, or an empty string without a row. */
	char asset_path[256];
	char symbolic_name[64];
	char display_name[64];
} TieFlightSceneSpeciesInfo;

bool TieFlightRenderer_SceneSpeciesInfo(const TieFlightRenderer* g, uint16_t species_idx,
										TieFlightSceneSpeciesInfo* out);

/* Request updated flight-model assets. The request is acknowledged on the
 * render thread and takes effect at the next mission generation so simulation
 * and rendering keep the same immutable model within a mission. */
void TieFlightRenderer_SceneRequestReload(TieFlightRenderer* g);

/* Request one species; ignored when species_idx is out of range. */
void TieFlightRenderer_SceneRequestReloadOne(TieFlightRenderer* g, uint16_t species_idx);

#ifdef __cplusplus
}
#endif

#endif
