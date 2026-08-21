#ifndef TIE_REMASTER_SCENE2D_MAP_H
#define TIE_REMASTER_SCENE2D_MAP_H

#include "tie_remaster/scene2d/manifest.h" /* TieScene2dActorView */
#include "tie_runtime/snapshot/snapshot_types.h"

#include "aeron/render.h"
#include "aeron/scene/draw_list2d.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct TieScene2dMapRenderer TieScene2dMapRenderer;
typedef struct TieScene2dCutscene TieScene2dCutscene;
typedef struct TieScene2dTextRenderer TieScene2dTextRenderer;

/* Init / shutdown. target_format is the cutscene RT format that the
 * overlay-pass quad is rendered against. `cutscene` is borrowed — the
 * brief renderer pulls the shared KTX2 texture cache and the
 * remaster_dir asset-root from it; pass NULL to disable HD asset
 * loading (renderer falls back to procedural primitives).
 * `text_renderer` is borrowed so source-RT prep can dispatch the brief
 * widget's text onto the same source RT as its actors and paint commands. */
TieScene2dMapRenderer* TieScene2dMap_Init(AeronCommandBuffer* cmd, AeronTextureFormat target_format,
										  TieScene2dCutscene* cutscene,
										  TieScene2dTextRenderer* text_renderer);
void TieScene2dMap_Shutdown(TieScene2dMapRenderer* g);

/* Source pass — render the brief-map content into the module's HD
 * source RT. Opens its own render pass against the source RT (so the
 * caller must NOT have an active render pass when this is invoked).
 *
 * For the polygon path the engine emits stamped target ==
 * TIE_EMIT_TARGET_BRIEF_SOURCE — actor draws_2d, paint_cmds, ui_texts
 * the engine produced inside the brief_buffer scratch canvas — are
 * dispatched onto the source RT alongside the HD backdrop sprite. The
 * cutscene-RT merge dispatch filters out the same target tag, so each
 * record renders exactly once.
 *
 * Caller passes the live views[] / lfd / film / cur_cel triple already
 * built for the cutscene RT pass; all are used unchanged.
 *
 * No-op when snap->map.active == 0 OR snap->map.has_polygon == 0. */
bool TieScene2dMap_Prep(TieScene2dMapRenderer* g, AeronCommandBuffer* cmd, const TieSnapshot* snap,
						const TieScene2dActorView* views, int nviews, const char* lfd, const char* film,
						int cur_cel);

/* Overlay pass — emit the brief-map quad (rect or quad4) on the
 * supplied render pass. Caller has already opened the cutscene RT
 * pass. No-op when snap->map.active == 0 or the source pass
 * never ran this tick. */
void TieScene2dMap_RecordOverlay(TieScene2dMapRenderer* g, AeronDrawList2D* list, int viewport_w,
								 int viewport_h, const TieSnapshot* snap);

#ifdef __cplusplus
}
#endif

#endif
