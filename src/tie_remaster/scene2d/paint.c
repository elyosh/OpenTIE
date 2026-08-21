/*
 * Paint-command renderer. Solid primitives use the Aeron draw list's
 * shared white texture; paint_commands.c records each TiePaintCmd.
 */

#include "tie_remaster/scene2d/paint.h"
#include "tie_remaster/scene2d/paint_commands.h"
void TieScene2dPaint_RecordInSource(AeronDrawList2D* list, int viewport_w, int viewport_h, int source_w,
									int source_h, uint8_t source_pixel_aspect, const TiePaintCmd* cmds,
									int count, const uint32_t* palette) {
	if (!list || !cmds || count <= 0 || !palette)
		return;
	if (viewport_w <= 0 || viewport_h <= 0)
		return;

	TieScene2dCanvas canvas;
	TieScene2dCanvas_Begin(&canvas, list, viewport_w, viewport_h);
	TieScene2dCanvas_SetSourceAspect(&canvas, source_w, source_h, source_pixel_aspect);
	TieScene2dPaintCommands_Record(&canvas, cmds, count, palette);
}
