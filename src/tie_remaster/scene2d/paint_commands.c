/*
 * Paint-command layout and recording.
 *
 * Each TiePaintCmd records up to nine primitives. Tint is
 * derived from the snapshot palette (PMA-premultiplied since the
 * pipeline is PMA-blend).
 */

#include "tie_remaster/scene2d/paint_commands.h"
#include "tie_remaster/scene2d/viewport.h" /* TieScene2dViewportTransform + TieScene2dViewport_ComputeXform */

#include <string.h>

static inline TieScene2dRect TieScene2dPaint_ClassicRect(const TieScene2dViewportTransform* vx, int cx,
														 int cy, int cw, int ch) {
	int x0, y0, x1, y1;

	if (!TieScene2dViewport_MapEdge(vx->region_x, vx->region_w, vx->source_w, cx, &x0) ||
		!TieScene2dViewport_MapEdge(vx->region_y, vx->region_h, vx->source_h, cy, &y0) ||
		!TieScene2dViewport_MapEdge(vx->region_x, vx->region_w, vx->source_w, cx + cw, &x1) ||
		!TieScene2dViewport_MapEdge(vx->region_y, vx->region_h, vx->source_h, cy + ch, &y1))
		return (TieScene2dRect) { 0 };

	return (TieScene2dRect) {
		.x = (float)x0,
		.y = (float)y0,
		.w = (float)(x1 - x0),
		.h = (float)(y1 - y0),
	};
}

/* Palette index → PMA-premultiplied tint. The PMA pipeline expects
 * sample.rgb already weighted by alpha; with sample = (1,1,1,1) (the
 * white texture) and an alpha-1 opaque solid, this collapses to
 * (R*A, G*A, B*A, A) with A=1. */
static inline TieScene2dRgba TieScene2dPaint_PaletteTint(const uint32_t* palette, uint8_t idx) {
	uint32_t argb = palette[idx];
	float r = (float)((argb >> 16) & 0xFFu) / 255.0f;
	float g = (float)((argb >> 8) & 0xFFu) / 255.0f;
	float b = (float)(argb & 0xFFu) / 255.0f;
	return (TieScene2dRgba) { r * 1.0f, g * 1.0f, b * 1.0f, 1.0f };
}

/* Emit one solid-color quad. Skipped when the rect collapses (some
 * BEVEL inner regions are 0 px on tiny widgets). */
static void TieScene2dPaint_RecordSolid(TieScene2dCanvas* canvas, TieScene2dRect dst, TieScene2dRgba tint) {
	if (dst.w <= 0.0f || dst.h <= 0.0f)
		return;
	const float rgba[4] = { tint.r, tint.g, tint.b, tint.a };
	TieScene2dCanvas_AddFill(canvas, dst.x, dst.y, dst.w, dst.h, rgba, AERON_BLIT2D_BLEND_PMA);
}

/* Translate a classic-px clip rect to a viewport-pixel scissor. Empty
 * clip → zero scissor (sdl_remaster_submit treats w<=0 || h<=0 as
 * "no scissor"); negative widths fall through the same way. */
static int TieScene2dPaint_ScissorFromClip(const TieScene2dViewportTransform* vx, int16_t cl, int16_t ct,
										   int16_t cr, int16_t cb, AeronRectI* out) {
	if (cr <= cl || cb <= ct) {
		*out = (AeronRectI) { 0 };
		return 1;
	}
	int x0, y0, x1, y1;
	if (!TieScene2dViewport_MapEdge(vx->region_x, vx->region_w, vx->source_w, cl, &x0) ||
		!TieScene2dViewport_MapEdge(vx->region_y, vx->region_h, vx->source_h, ct, &y0) ||
		!TieScene2dViewport_MapEdge(vx->region_x, vx->region_w, vx->source_w, cr, &x1) ||
		!TieScene2dViewport_MapEdge(vx->region_y, vx->region_h, vx->source_h, cb, &y1))
		return 0;
	*out = (AeronRectI) { x0, y0, x1 - x0, y1 - y0 };
	return 1;
}

void TieScene2dPaintCommands_Record(TieScene2dCanvas* canvas, const TiePaintCmd* cmds, int count,
									const uint32_t* palette) {
	if (!canvas || !cmds || count <= 0 || !palette)
		return;

	TieScene2dViewportTransform vx;
	if (!TieScene2dViewport_ComputeXformForSourceAspect(canvas->viewport_w, canvas->viewport_h,
														canvas->source_w, canvas->source_h,
														canvas->source_pixel_aspect, &vx))
		return;

	/* One batch per cmd so each can carry its captured canvas-clip
	 * as a scissor (mirrors classic's per-op clip via dl_rect /
	 * dl_horiz_line). Cmds with empty / unset clip get a zero
	 * scissor — sdl_remaster_submit treats that as "no scissor". */
	for (int i = 0; i < count; i++) {
		const TiePaintCmd* p = &cmds[i];

		AeronRectI sc;
		if (!TieScene2dPaint_ScissorFromClip(&vx, p->clip_left, p->clip_top, p->clip_right, p->clip_bottom,
											 &sc))
			return;
		TieScene2dCanvas_SetScissor(canvas, sc);

		switch (p->op) {
			case TIE_PAINT_FILL_RECT: {
				TieScene2dPaint_RecordSolid(canvas, TieScene2dPaint_ClassicRect(&vx, p->x, p->y, p->w, p->h),
											TieScene2dPaint_PaletteTint(palette, p->colors[0]));
				break;
			}

			case TIE_PAINT_FRAME_RECT: {
				TieScene2dRgba c = TieScene2dPaint_PaletteTint(palette, p->colors[0]);
				TieScene2dPaint_RecordSolid(canvas, TieScene2dPaint_ClassicRect(&vx, p->x, p->y, p->w, 1),
											c); /* top */
				TieScene2dPaint_RecordSolid(
					canvas, TieScene2dPaint_ClassicRect(&vx, p->x, p->y + p->h - 1, p->w, 1), c); /* bot */
				TieScene2dPaint_RecordSolid(canvas, TieScene2dPaint_ClassicRect(&vx, p->x, p->y, 1, p->h),
											c); /* left */
				TieScene2dPaint_RecordSolid(
					canvas, TieScene2dPaint_ClassicRect(&vx, p->x + p->w - 1, p->y, 1, p->h), c); /* right */
				break;
			}

			case TIE_PAINT_HLINE: {
				TieScene2dPaint_RecordSolid(canvas, TieScene2dPaint_ClassicRect(&vx, p->x, p->y, p->w, 1),
											TieScene2dPaint_PaletteTint(palette, p->colors[0]));
				break;
			}
			case TIE_PAINT_VLINE: {
				TieScene2dPaint_RecordSolid(canvas, TieScene2dPaint_ClassicRect(&vx, p->x, p->y, 1, p->h),
											TieScene2dPaint_PaletteTint(palette, p->colors[0]));
				break;
			}
			case TIE_PAINT_PIXEL: {
				TieScene2dPaint_RecordSolid(canvas, TieScene2dPaint_ClassicRect(&vx, p->x, p->y, 1, 1),
											TieScene2dPaint_PaletteTint(palette, p->colors[0]));
				break;
			}

			case TIE_PAINT_BEVEL:
			case TIE_PAINT_FRAME_BEVEL: {
				/* 1-classic-px frame: shadow = right+bottom; highlight =
				 * top+left. `pressed` swaps shadow/highlight colors.
				 * Matches landru's xpaint_Paint_Clipped_Bevel. */
				uint8_t hi_idx = p->pressed ? p->colors[0] : p->colors[1];
				uint8_t sh_idx = p->pressed ? p->colors[1] : p->colors[0];
				TieScene2dRgba hi = TieScene2dPaint_PaletteTint(palette, hi_idx);
				TieScene2dRgba sh = TieScene2dPaint_PaletteTint(palette, sh_idx);
				if (p->op == TIE_PAINT_BEVEL) {
					TieScene2dPaint_RecordSolid(
						canvas, TieScene2dPaint_ClassicRect(&vx, p->x + 1, p->y + 1, p->w - 2, p->h - 2),
						TieScene2dPaint_PaletteTint(palette, p->colors[2]));
				}
				TieScene2dPaint_RecordSolid(canvas, TieScene2dPaint_ClassicRect(&vx, p->x, p->y, p->w, 1),
											hi); /* top */
				TieScene2dPaint_RecordSolid(canvas, TieScene2dPaint_ClassicRect(&vx, p->x, p->y, 1, p->h),
											hi); /* left */
				TieScene2dPaint_RecordSolid(
					canvas, TieScene2dPaint_ClassicRect(&vx, p->x, p->y + p->h - 1, p->w, 1), sh); /* bot */
				TieScene2dPaint_RecordSolid(
					canvas, TieScene2dPaint_ClassicRect(&vx, p->x + p->w - 1, p->y, 1, p->h), sh); /* right */
				break;
			}

			case TIE_PAINT_DBEVEL:
			case TIE_PAINT_FRAME_DBEVEL: {
				/* Outer 1-px frame + inner 1-px frame; pressed swaps each
				 * pair. Inner frame inset by one classic px on every side. */
				uint8_t o_hi = p->pressed ? p->colors[0] : p->colors[2];
				uint8_t o_sh = p->pressed ? p->colors[2] : p->colors[0];
				uint8_t i_hi = p->pressed ? p->colors[1] : p->colors[3];
				uint8_t i_sh = p->pressed ? p->colors[3] : p->colors[1];
				TieScene2dRgba ohi = TieScene2dPaint_PaletteTint(palette, o_hi);
				TieScene2dRgba osh = TieScene2dPaint_PaletteTint(palette, o_sh);
				TieScene2dRgba ihi = TieScene2dPaint_PaletteTint(palette, i_hi);
				TieScene2dRgba ish = TieScene2dPaint_PaletteTint(palette, i_sh);
				if (p->op == TIE_PAINT_DBEVEL) {
					TieScene2dPaint_RecordSolid(
						canvas, TieScene2dPaint_ClassicRect(&vx, p->x + 2, p->y + 2, p->w - 4, p->h - 4),
						TieScene2dPaint_PaletteTint(palette, p->colors[4]));
				}
				/* outer */
				TieScene2dPaint_RecordSolid(canvas, TieScene2dPaint_ClassicRect(&vx, p->x, p->y, p->w, 1),
											ohi);
				TieScene2dPaint_RecordSolid(canvas, TieScene2dPaint_ClassicRect(&vx, p->x, p->y, 1, p->h),
											ohi);
				TieScene2dPaint_RecordSolid(
					canvas, TieScene2dPaint_ClassicRect(&vx, p->x, p->y + p->h - 1, p->w, 1), osh);
				TieScene2dPaint_RecordSolid(
					canvas, TieScene2dPaint_ClassicRect(&vx, p->x + p->w - 1, p->y, 1, p->h), osh);
				/* inner */
				TieScene2dPaint_RecordSolid(
					canvas, TieScene2dPaint_ClassicRect(&vx, p->x + 1, p->y + 1, p->w - 2, 1), ihi);
				TieScene2dPaint_RecordSolid(
					canvas, TieScene2dPaint_ClassicRect(&vx, p->x + 1, p->y + 1, 1, p->h - 2), ihi);
				TieScene2dPaint_RecordSolid(
					canvas, TieScene2dPaint_ClassicRect(&vx, p->x + 1, p->y + p->h - 2, p->w - 2, 1), ish);
				TieScene2dPaint_RecordSolid(
					canvas, TieScene2dPaint_ClassicRect(&vx, p->x + p->w - 2, p->y + 1, 1, p->h - 2), ish);
				break;
			}

			case TIE_PAINT_XOR_RECT:
				/* XOR rectangles are unsupported by the PMA pipeline. */
				break;

			case TIE_PAINT_SHADE_RECT: {
				/* Darken-toward-color rect. Models shade_Shadow_Line_List
				 * (src/tie/shade.c) which dims classic-FB pixels through a
				 * palette LUT. The PMA alpha-over pipeline gives the same
				 * per-channel result as the engine's RGB shift toward
				 * target by intensity:
				 *   dst_new = dst*(1-a) + src
				 * with src = (target_R*a, target_G*a, target_B*a, a) and
				 * a = intensity / 256. For target=(0,0,0) this is a pure
				 * darken (dst * (1-a)); for target=(R,0,0) it tints toward
				 * red. colors[0..2] carry target RGB in 0..255, colors[3]
				 * carries the engine intensity byte. */
				float a = (float)p->colors[3] / 256.0f;
				float tr = (float)p->colors[0] / 255.0f;
				float tg = (float)p->colors[1] / 255.0f;
				float tb = (float)p->colors[2] / 255.0f;
				TieScene2dRgba tint = { tr * a, tg * a, tb * a, a };
				TieScene2dPaint_RecordSolid(canvas, TieScene2dPaint_ClassicRect(&vx, p->x, p->y, p->w, p->h),
											tint);
				break;
			}

			default:
				break;
		}
	}
}
