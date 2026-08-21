/*
 * Cursor layout and recording. One quad per call. Pen
 * positioning + pixel-snap are mechanically extracted from the old
 * cursor blit helper; Aeron handles pixel-to-clip conversion.
 */

#include "tie_remaster/scene2d/cursor_layout.h"

void TieScene2dCursor_Record(TieScene2dCanvas* canvas, AeronTexture* cursor_texture, int tex_dim,
							 const TieCursorState* cursor, float hotspot_vp_x, float hotspot_vp_y,
							 float scale_x, float scale_y) {
	if (!canvas || !cursor_texture || !cursor || tex_dim <= 0)
		return;
	if (!cursor->visible || cursor->w <= 0 || cursor->h <= 0)
		return;

	/* Pen = sprite top-left, derived by stepping back from the hotspot
	 * by the hot-anchor offset times the 4:3 scale. Snap to integer
	 * pixels — non-integer Y on scale_y=10.8 causes UV-bleed at the
	 * texture edge under linear filtering. */
	float pen_x = hotspot_vp_x - (float)cursor->hot_x * scale_x;
	float pen_y = hotspot_vp_y - (float)cursor->hot_y * scale_y;
	pen_x = (float)((int)(pen_x + 0.5f));
	pen_y = (float)((int)(pen_y + 0.5f));

	AeronDrawList2DSprite sprite = {
		.texture = cursor_texture,
		.src_u1 = (float)cursor->w / (float)tex_dim,
		.src_v1 = (float)cursor->h / (float)tex_dim,
		.dst_x = pen_x,
		.dst_y = pen_y,
		.dst_w = (float)cursor->w * scale_x,
		.dst_h = (float)cursor->h * scale_y,
		.tint = { 1.0f, 1.0f, 1.0f, 1.0f },
		.filter = AERON_BLIT2D_FILTER_NEAREST,
		.blend = AERON_BLIT2D_BLEND_PMA,
	};
	TieScene2dCanvas_SetScissor(canvas, (AeronRectI) { 0 });
	TieScene2dCanvas_AddSprite(canvas, &sprite);
}
