/*
 * Paint-command composition — replays the engine's solid-color drawing
 * primitives (lpaint_*) on the HD overlay RT so they appear on top of
 * the HD sprites (which would otherwise occlude them).
 *
 * Each TiePaintCmd in the snapshot becomes one or more colored quads.
 * No external assets — palette indices are looked up against the
 * snapshot palette and recorded through Aeron's shared solid-color path.
 */
#ifndef TIE_REMASTER_SCENE2D_PAINT_H
#define TIE_REMASTER_SCENE2D_PAINT_H

#include "tie_runtime/snapshot/snapshot_types.h" /* TiePaintCmd */

#include "aeron/render.h"
#include "aeron/scene/draw_list2d.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void TieScene2dPaint_RecordInSource(AeronDrawList2D* list, int viewport_w, int viewport_h, int source_w,
									int source_h, uint8_t source_pixel_aspect, const TiePaintCmd* cmds,
									int count, const uint32_t* palette);

#ifdef __cplusplus
}
#endif

#endif
