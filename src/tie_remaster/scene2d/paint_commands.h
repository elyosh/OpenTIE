#ifndef TIE_SCENE2D_PAINT_H
#define TIE_SCENE2D_PAINT_H

/*
 * Paint-command composer — walks TiePaintCmd[] from the snapshot
 * and records Aeron fill primitives for each command's decomposition.
 */

#include "tie_remaster/scene2d/canvas.h"
#include "tie_remaster/scene2d/types.h"
#include "tie_runtime/snapshot/snapshot_types.h" /* TiePaintCmd */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Append one batch of solid-color quads (the paint-command channel)
 * to `frame`. `palette` is the snapshot's 256-entry XRGB8888 array.
 * Caller is expected to have set frame's viewport_w/h
 * before calling. No-op when count == 0. */
void TieScene2dPaintCommands_Record(TieScene2dCanvas* canvas, const TiePaintCmd* cmds, int count,
									const uint32_t* palette);

#ifdef __cplusplus
}
#endif

#endif
