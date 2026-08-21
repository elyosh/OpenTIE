#ifndef TIE_SCENE2D_MAP_H
#define TIE_SCENE2D_MAP_H

/* Brief-map composition. Standard snapshot draws own widget content;
 * this module owns the backdrop and polygon source render target. */

#include "tie_remaster/scene2d/actor_layout.h" /* TieScene2dActorEntry, TieScene2dActorTexture */
#include "tie_remaster/scene2d/canvas.h"
#include "tie_runtime/snapshot/snapshot_types.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Pixel multiplier for the polygon source target. */
#define MAP_HD_MULT_DEFAULT 4

/* Resolves a full-texture backdrop. Failure selects the flat-color fallback. */
typedef bool (*TieScene2dMapResolveSpriteFn)(void* userdata, const char* res_name, int cur_cel,
											 TieScene2dActorTexture* out_tex);

typedef struct TieScene2dMapAssets {
	/* Manifest-driven backdrop sprite resolver. */
	TieScene2dMapResolveSpriteFn resolve_sprite;
	void* resolve_userdata;
} TieScene2dMapAssets;

/* Records the polygon path's backdrop into its source target. */
void TieScene2dMapComposition_RecordSource(TieScene2dCanvas* canvas, const TieSnapshot* snap,
										   const TieScene2dMapAssets* assets);

/* Records either the warped source target or a directly placed backdrop. */
void TieScene2dMapComposition_RecordOverlay(TieScene2dCanvas* canvas, const TieSnapshot* snap,
											const TieScene2dMapAssets* assets, AeronTexture* source_rt_tex,
											int source_rt_w, int source_rt_h);

#ifdef __cplusplus
}
#endif

#endif
