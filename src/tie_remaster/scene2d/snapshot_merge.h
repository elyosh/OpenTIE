#ifndef TIE_REMASTER_SNAPSHOT_MERGE_DISPATCH_H
#define TIE_REMASTER_SNAPSHOT_MERGE_DISPATCH_H

/* Merge actor, text, and paint snapshot streams by z_order. Records are
 * filtered by target and contiguous records from one channel are batched.
 * A caller may insert the brief-map quad at a configured z position. */

#include "tie_remaster/scene2d/manifest.h" /* TieScene2dActorView */
#include "tie_runtime/snapshot/snapshot_types.h"

#include "aeron/scene/draw_list2d.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct TieScene2dCutscene TieScene2dCutscene;
typedef struct TieScene2dTextRenderer TieScene2dTextRenderer;

/* Caller-supplied callback that emits the brief-map quad at the
 * configured z slot. Invoked with the same render pass / cmd / viewport
 * dims passed to TieScene2dSnapshotDispatch_Run. The application binds this to
 * TieScene2dMap_RecordOverlay (cutscene RT pass) — the source RT pass leaves
 * map_quad_emit NULL since the source RT IS the brief-map source. */
typedef void (*TieScene2dSnapshotMergeMapQuadFn)(void* userdata, AeronDrawList2D* list, int viewport_w,
												 int viewport_h);

typedef struct TieScene2dSnapshotMergeDispatch {
	/* Record streams — caller provides full arrays; the dispatcher
	 * filters by `target` at iteration time. NULL streams are
	 * permitted (treated as zero-length). */
	const TieScene2dActorView* views;
	int view_count;
	const TieUIText* ui_texts;
	int ui_text_count;
	const TiePaintCmd* paint_cmds;
	int paint_cmd_count;
	const uint32_t* palette;

	/* Landru coordinate surface represented by the record streams. */
	int source_w;
	int source_h;
	uint8_t source_pixel_aspect;

	/* Records whose target tag does NOT equal accept_target are
	 * filtered out. Use TIE_EMIT_TARGET_CUTSCENE for the main cutscene
	 * RT pass and TIE_EMIT_TARGET_BRIEF_SOURCE for the brief-map
	 * source RT pass. */
	uint8_t accept_target;

	/* Renderer instances — borrowed for the call. NULL skips the
	 * channel: e.g. omitting cutscene silently drops actor draws. */
	TieScene2dCutscene* cutscene;
	TieScene2dTextRenderer* text_renderer;

	/* Cutscene-actors lookup tag (passed to TieScene2dCutscene_RecordActorsInSource).
	 * lfd/film NULL → actor channel is skipped. */
	const char* lfd;
	const char* film;
	int cur_cel;

	/* Optional brief-map quad. When emit_map_quad is non-NULL, the
	 * dispatcher slots it at z = map_quad_z so it merges with the
	 * other channels. Source RT pass leaves emit_map_quad NULL. */
	int map_quad_z;
	TieScene2dSnapshotMergeMapQuadFn emit_map_quad;
	void* map_quad_userdata;
} TieScene2dSnapshotMergeDispatch;

void TieScene2dSnapshotDispatch_Run(AeronDrawList2D* list, int viewport_w, int viewport_h,
									const TieScene2dSnapshotMergeDispatch* cfg);

#ifdef __cplusplus
}
#endif

#endif
