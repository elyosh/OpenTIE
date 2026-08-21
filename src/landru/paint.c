#include <string.h>

#include <landru/canvas.h>
#include <landru/paint.h>
#include <landru/rect.h>
#include <landru/render.h>

#include "render_internal.h"

/*
 * XPAINT uses a mini display-list renderer. The DL_* functions clip and
 * append opcodes to a word array. Paint_List rasterizes the accumulated
 * opcodes into draw_buff_gbl in a single pass.
 *
 * Opcodes: 0=end, 1=filled rect, 2=horiz line, 3=vert line, 4=XOR rect.
 */

#define DL_MAX_WORDS 64

static uint16_t draw_list[DL_MAX_WORDS];

/* Separate from the canvas-target gate. Callers use this when another render
 * channel represents the same primitives and publishing both would duplicate
 * them. Text publication is unaffected. */
static bool s_emit_suppressed;
static bool s_thickness_duplicate;

void lpaint_Set_Emit_Suppressed(bool on) { s_emit_suppressed = on; }

void lpaint_Set_Thickness_Duplicate(bool on) { s_thickness_duplicate = on; }

/* --- Draw list builders --- */

static void dl_horiz_line(Rect* clip, uint16_t* list, int16_t* count, int16_t x, int16_t y, int16_t length,
						  int16_t color) {
	if (y < clip->top || y >= clip->bottom)
		return;
	int16_t x_end = x + length;
	if (x < clip->left)
		x = clip->left;
	if (x_end > clip->right)
		x_end = clip->right;
	length = x_end - x;
	if (length <= 0)
		return;
	int16_t idx = *count;
	list[idx++] = 2;
	list[idx++] = x;
	list[idx++] = y;
	list[idx++] = length;
	list[idx++] = color;
	*count = idx;
}

static void dl_vert_line(Rect* clip, uint16_t* list, int16_t* count, int16_t x, int16_t y, int16_t length,
						 int16_t color) {
	if (x < clip->left || x >= clip->right)
		return;
	int16_t y_end = y + length;
	if (y < clip->top)
		y = clip->top;
	if (y_end > clip->bottom)
		y_end = clip->bottom;
	length = y_end - y;
	if (length <= 0)
		return;
	int16_t idx = *count;
	list[idx++] = 3;
	list[idx++] = x;
	list[idx++] = y;
	list[idx++] = length;
	list[idx++] = color;
	*count = idx;
}

static void dl_rect(Rect* clip, uint16_t* list, int16_t* count, Rect* r, int16_t color) {
	Rect clipped = *r;
	if (!lrect_Clip_Rect(&clipped, clip))
		return;
	int16_t idx = *count;
	list[idx++] = 1;
	list[idx++] = clipped.left;
	list[idx++] = clipped.top;
	list[idx++] = clipped.right - clipped.left;
	list[idx++] = clipped.bottom - clipped.top;
	list[idx++] = color;
	*count = idx;
}

static void dl_xor_rect(Rect* clip, uint16_t* list, int16_t* count, Rect* r, int16_t color) {
	Rect clipped = *r;
	if (!lrect_Clip_Rect(&clipped, clip))
		return;
	int16_t idx = *count;
	list[idx++] = 4;
	list[idx++] = clipped.left;
	list[idx++] = clipped.top;
	list[idx++] = clipped.right - clipped.left;
	list[idx++] = clipped.bottom - clipped.top;
	list[idx++] = color;
	*count = idx;
}

static void dl_end(uint16_t* list, int16_t* count) {
	list[*count] = 0;
	(*count)++;
}

/* --- Display list executor --- */

static void paint_list(const uint16_t* list) {
	uint8_t* buf = (uint8_t*)draw_buff_gbl;
	int16_t stride = draw_w_gbl;
	int i = 0;

	while (1) {
		uint16_t op = list[i++];
		if (op == 0)
			break;

		if (op == 1) {
			/* Filled rect: x, y, width, height, color */
			int16_t x = list[i++];
			int16_t y = list[i++];
			int16_t w = list[i++];
			int16_t h = list[i++];
			uint8_t color = (uint8_t)list[i++];
			uint8_t* dst = buf + y * stride + x;
			for (int16_t row = 0; row < h; row++) {
				memset(dst, color, w);
				dst += stride;
			}
		} else if (op == 2) {
			/* Horiz line: x, y, length, color */
			int16_t x = list[i++];
			int16_t y = list[i++];
			int16_t len = list[i++];
			uint8_t color = (uint8_t)list[i++];
			memset(buf + y * stride + x, color, len);
		} else if (op == 3) {
			/* Vert line: x, y, length, color */
			int16_t x = list[i++];
			int16_t y = list[i++];
			int16_t len = list[i++];
			uint8_t color = (uint8_t)list[i++];
			uint8_t* dst = buf + y * stride + x;
			for (int16_t row = 0; row < len; row++) {
				*dst = color;
				dst += stride;
			}
		} else if (op == 4) {
			/* XOR rect: x, y, width, height, color */
			int16_t x = list[i++];
			int16_t y = list[i++];
			int16_t w = list[i++];
			int16_t h = list[i++];
			uint8_t color = (uint8_t)list[i++];
			uint8_t* dst = buf + y * stride + x;
			for (int16_t row = 0; row < h; row++) {
				for (int16_t col = 0; col < w; col++)
					dst[col] ^= color;
				dst += stride;
			}
		}
	}
}

/* --- Build bevel (single) --- */

static int16_t build_clipped_bevel(Rect* src, int16_t shadow, int16_t highlight, int16_t fill_color,
								   int16_t pressed, int16_t fill_interior) {
	if (lrect_Empty_Rect(src))
		return 0;

	int16_t top_color = pressed ? shadow : highlight;
	int16_t bottom_color = pressed ? highlight : shadow;
	int16_t w = src->right - src->left;
	int16_t h = src->bottom - src->top;

	int16_t count = 0;
	Rect clip;
	lcanvas_Get_Drawing_Canvas_Clip(&clip);

	/* Vertical sides: always shadow color */
	if (h > 2) {
		dl_vert_line(&clip, draw_list, &count, src->left, src->top + 1, h - 2, shadow);
		if (w > 1)
			dl_vert_line(&clip, draw_list, &count, src->right - 1, src->top + 1, h - 2, shadow);
	}
	/* Bottom */
	if (h > 1)
		dl_horiz_line(&clip, draw_list, &count, src->left, src->bottom - 1, w, bottom_color);
	/* Top */
	dl_horiz_line(&clip, draw_list, &count, src->left, src->top, w, top_color);

	/* Interior fill */
	if (fill_interior) {
		Rect inner = *src;
		lrect_Inset_Rect(&inner, 1, 1);
		if (!lrect_Empty_Rect(&inner))
			dl_rect(&clip, draw_list, &count, &inner, fill_color);
	}

	if (!count)
		return 0;
	dl_end(draw_list, &count);
	paint_list(draw_list);
	return 1;
}

/* --- Build double bevel --- */

static int16_t build_clipped_dbevel(Rect* src, int16_t outer_shadow, int16_t inner_shadow,
									int16_t outer_highlight, int16_t inner_highlight, int16_t fill_color,
									int16_t pressed, int16_t fill_interior) {
	if (lrect_Empty_Rect(src))
		return 0;

	int16_t outer_top = pressed ? outer_shadow : outer_highlight;
	int16_t outer_bot = pressed ? outer_highlight : outer_shadow;
	int16_t inner_top = pressed ? inner_shadow : inner_highlight;
	int16_t inner_bot = pressed ? inner_highlight : inner_shadow;
	int16_t w = src->right - src->left;
	int16_t h = src->bottom - src->top;

	int16_t count = 0;
	Rect clip;
	lcanvas_Get_Drawing_Canvas_Clip(&clip);

	/* Outer frame */
	if (h > 2) {
		dl_vert_line(&clip, draw_list, &count, src->left, src->top + 1, h - 2, outer_shadow);
		if (w > 1)
			dl_vert_line(&clip, draw_list, &count, src->right - 1, src->top + 1, h - 2, outer_shadow);
	}
	if (h > 1)
		dl_horiz_line(&clip, draw_list, &count, src->left, src->bottom - 1, w, outer_bot);
	dl_horiz_line(&clip, draw_list, &count, src->left, src->top, w, outer_top);

	/* Inner frame */
	if (w > 2) {
		if (h > 4) {
			dl_vert_line(&clip, draw_list, &count, src->left + 1, src->top + 2, h - 4, inner_shadow);
			if (w > 3)
				dl_vert_line(&clip, draw_list, &count, src->right - 2, src->top + 2, h - 4, inner_shadow);
		}
		if (h > 3)
			dl_horiz_line(&clip, draw_list, &count, src->left + 1, src->bottom - 2, w - 2, inner_bot);
		/* The inner top remains visible on thin bevels. */
		dl_horiz_line(&clip, draw_list, &count, src->left + 1, src->top + 1, w - 2, inner_top);
	}

	/* Interior fill */
	if (fill_interior) {
		Rect inner = *src;
		lrect_Inset_Rect(&inner, 2, 2);
		if (!lrect_Empty_Rect(&inner))
			dl_rect(&clip, draw_list, &count, &inner, fill_color);
	}

	if (!count)
		return 0;
	dl_end(draw_list, &count);
	paint_list(draw_list);
	return 1;
}

/* --- Public API --- */

/* Publish at the primitive API boundary so every caller is represented. */
static void emit_paint(LandruPaintOp op, uint8_t pressed, int16_t x, int16_t y, int16_t w, int16_t h,
					   int16_t c0, int16_t c1, int16_t c2, int16_t c3, int16_t c4) {
	/* Scratch-canvas coordinates are local and cannot be treated as screen
	 * coordinates unless the caller explicitly enables publication. */
	if (!lcanvas_Render_Emit_Allowed())
		return;
	if (s_emit_suppressed)
		return;
	if (s_thickness_duplicate)
		return;

	LandruPaintCommand command = { 0 };
	LandruPaintCommand* out = &command;
	out->op = op;
	out->pressed = pressed;
	out->colors[0] = (uint8_t)c0;
	out->colors[1] = (uint8_t)c1;
	out->colors[2] = (uint8_t)c2;
	out->colors[3] = (uint8_t)c3;
	out->colors[4] = (uint8_t)c4;
	out->target = lcanvas_Render_Emit_Target();
	out->x = x;
	out->y = y;
	out->w = w;
	out->h = h;
	/* Preserve the active canvas clip for equivalent consumer-side
	 * rasterization. */
	Rect cc;
	lcanvas_Get_Drawing_Canvas_Clip(&cc);
	out->clip_left = cc.left;
	out->clip_top = cc.top;
	out->clip_right = cc.right;
	out->clip_bottom = cc.bottom;
	landru_render_paint(out);
}

int16_t lpaint_Plot_Clipped_Pixel(int16_t x, int16_t y, int16_t color) {
	emit_paint(LANDRU_PAINT_PIXEL, 0, x, y, 0, 0, color, 0, 0, 0, 0);
	int16_t count = 0;
	Rect clip;
	lcanvas_Get_Drawing_Canvas_Clip(&clip);
	dl_horiz_line(&clip, draw_list, &count, x, y, 1, color);
	if (!count)
		return 0;
	dl_end(draw_list, &count);
	paint_list(draw_list);
	return 1;
}

int16_t lpaint_Horiz_Clipped_Line(int16_t x, int16_t y, int16_t length, int16_t color) {
	emit_paint(LANDRU_PAINT_HLINE, 0, x, y, length, 0, color, 0, 0, 0, 0);
	int16_t count = 0;
	Rect clip;
	lcanvas_Get_Drawing_Canvas_Clip(&clip);
	dl_horiz_line(&clip, draw_list, &count, x, y, length, color);
	if (!count)
		return 0;
	dl_end(draw_list, &count);
	paint_list(draw_list);
	return 1;
}

int16_t lpaint_Vert_Clipped_Line(int16_t x, int16_t y, int16_t length, int16_t color) {
	emit_paint(LANDRU_PAINT_VLINE, 0, x, y, 0, length, color, 0, 0, 0, 0);
	int16_t count = 0;
	Rect clip;
	lcanvas_Get_Drawing_Canvas_Clip(&clip);
	dl_vert_line(&clip, draw_list, &count, x, y, length, color);
	if (!count)
		return 0;
	dl_end(draw_list, &count);
	paint_list(draw_list);
	return 1;
}

int16_t lpaint_Paint_Clipped_Rect(Rect* r, int16_t color) {
	emit_paint(LANDRU_PAINT_FILL_RECT, 0, r->left, r->top, r->right - r->left, r->bottom - r->top, color, 0,
			   0, 0, 0);
	int16_t count = 0;
	Rect clip;
	lcanvas_Get_Drawing_Canvas_Clip(&clip);
	dl_rect(&clip, draw_list, &count, r, color);
	if (!count)
		return 0;
	dl_end(draw_list, &count);
	paint_list(draw_list);
	return 1;
}

int16_t lpaint_Frame_Clipped_Rect(Rect* r, int16_t color) {
	if (lrect_Empty_Rect(r))
		return 0;
	int16_t w = r->right - r->left;
	int16_t h = r->bottom - r->top;
	emit_paint(LANDRU_PAINT_FRAME_RECT, 0, r->left, r->top, w, h, color, 0, 0, 0, 0);
	int16_t count = 0;
	Rect clip;
	lcanvas_Get_Drawing_Canvas_Clip(&clip);

	if (h > 2) {
		dl_vert_line(&clip, draw_list, &count, r->left, r->top + 1, h - 2, color);
		if (w > 1)
			dl_vert_line(&clip, draw_list, &count, r->right - 1, r->top + 1, h - 2, color);
	}
	if (h > 1)
		dl_horiz_line(&clip, draw_list, &count, r->left, r->bottom - 1, w, color);
	dl_horiz_line(&clip, draw_list, &count, r->left, r->top, w, color);

	if (!count)
		return 0;
	dl_end(draw_list, &count);
	paint_list(draw_list);
	return 1;
}

int16_t lpaint_Paint_Clipped_Bevel(Rect* r, int16_t shadow, int16_t highlight, int16_t fill,
								   int16_t pressed) {
	emit_paint(LANDRU_PAINT_BEVEL, (uint8_t)pressed, r->left, r->top, r->right - r->left, r->bottom - r->top,
			   shadow, highlight, fill, 0, 0);
	return build_clipped_bevel(r, shadow, highlight, fill, pressed, 1);
}

int16_t lpaint_Frame_Clipped_Bevel(Rect* r, int16_t shadow, int16_t highlight, int16_t fill,
								   int16_t pressed) {
	emit_paint(LANDRU_PAINT_FRAME_BEVEL, (uint8_t)pressed, r->left, r->top, r->right - r->left,
			   r->bottom - r->top, shadow, highlight, fill, 0, 0);
	return build_clipped_bevel(r, shadow, highlight, fill, pressed, 0);
}

int16_t lpaint_Paint_Clipped_DBevel(Rect* r, int16_t outer_shadow, int16_t inner_shadow,
									int16_t outer_highlight, int16_t inner_highlight, int16_t fill,
									int16_t pressed) {
	emit_paint(LANDRU_PAINT_DBEVEL, (uint8_t)pressed, r->left, r->top, r->right - r->left, r->bottom - r->top,
			   outer_shadow, inner_shadow, outer_highlight, inner_highlight, fill);
	return build_clipped_dbevel(r, outer_shadow, inner_shadow, outer_highlight, inner_highlight, fill,
								pressed, 1);
}

int16_t lpaint_Frame_Clipped_DBevel(Rect* r, int16_t outer_shadow, int16_t inner_shadow,
									int16_t outer_highlight, int16_t inner_highlight, int16_t fill,
									int16_t pressed) {
	emit_paint(LANDRU_PAINT_FRAME_DBEVEL, (uint8_t)pressed, r->left, r->top, r->right - r->left,
			   r->bottom - r->top, outer_shadow, inner_shadow, outer_highlight, inner_highlight, fill);
	return build_clipped_dbevel(r, outer_shadow, inner_shadow, outer_highlight, inner_highlight, fill,
								pressed, 0);
}

int16_t lpaint_XOR_Clipped_Rect(Rect* r, int16_t color) {
	emit_paint(LANDRU_PAINT_XOR_RECT, 0, r->left, r->top, r->right - r->left, r->bottom - r->top, color, 0, 0,
			   0, 0);
	int16_t count = 0;
	Rect clip;
	lcanvas_Get_Drawing_Canvas_Clip(&clip);
	dl_xor_rect(&clip, draw_list, &count, r, color);
	if (!count)
		return 0;
	dl_end(draw_list, &count);
	paint_list(draw_list);
	return 1;
}
