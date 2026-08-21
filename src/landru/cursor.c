#include <stddef.h>

#include <landru/bitmap.h>
#include <landru/canvas.h>
#include <landru/cursor.h>
#include <landru/fade.h>
#include <landru/io.h>
#include <landru/mouse.h>
#include <landru/rect.h>
#include <landru/render.h>
#include <landru/vesa.h>

#include "render_internal.h"

static BitmapStruct cursor_bm_gbl;
static BitmapStruct cursor_back_gbl;
static int16_t cursor_enable_gbl;
// GLOBAL: TIE 0xFBC84
static int16_t cursor_show_gbl;
static bool fade_cursor_gbl;
static bool cur_redraw_gbl;
// GLOBAL: TIE 0xFBC82
static uint8_t cursor_state_gbl;
static bool cursor_module_gbl;
static int16_t curs_hot_x, curs_hot_y;
static int16_t last_curs_x, last_curs_y;
static uint8_t cursor_id_gbl; /* current cursor kind for render capture */
/* PORT: frontends present the cursor outside the Landru surface. */
static bool cursor_external_presentation_gbl;
// GLOBAL: TIE98 0x58ABCC
int g_softwareCursorEnabled;
// GLOBAL: TIE98 0x58B320
static int g_cursorDisplayCount;
// GLOBAL: TIE98 0x58B324
static uint8_t g_softwareCursorDarkColorIndex;
// GLOBAL: TIE98 0x4F47A8
static uint8_t g_softwareCursorBrightColorIndex;

// GLOBAL: TIE98 0x4F46A8
static const uint8_t g_softwareCursorBitmap[16][16] = {
	{ 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }, { 1, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
	{ 1, 2, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }, { 1, 2, 2, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
	{ 1, 2, 2, 2, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }, { 1, 2, 2, 2, 2, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
	{ 1, 2, 2, 2, 2, 2, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0 }, { 1, 2, 2, 2, 2, 2, 2, 2, 1, 0, 0, 0, 0, 0, 0, 0 },
	{ 1, 2, 2, 2, 2, 2, 2, 2, 2, 1, 0, 0, 0, 0, 0, 0 }, { 1, 2, 2, 2, 2, 2, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0 },
	{ 1, 2, 2, 1, 2, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0 }, { 1, 2, 1, 0, 1, 2, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0 },
	{ 1, 1, 0, 0, 1, 2, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0 }, { 1, 0, 0, 0, 0, 1, 2, 2, 1, 0, 0, 0, 0, 0, 0, 0 },
	{ 0, 0, 0, 0, 0, 1, 2, 2, 1, 0, 0, 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0 },
};

static uint8_t pointer_cursor[16][16] = {
	{ 0x02, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
	{ 0x02, 0x0f, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
	{ 0x02, 0x0f, 0x0f, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
	{ 0x02, 0x0f, 0x0f, 0x0f, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
	{ 0x02, 0x0f, 0x0f, 0x0f, 0x0f, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
	{ 0x02, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
	{ 0x02, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
	{ 0x02, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
	{ 0x02, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
	{ 0x02, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x02, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
	{ 0x02, 0x0f, 0x0f, 0x0f, 0x0f, 0x02, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
	{ 0x02, 0x0f, 0x0f, 0x02, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
	{ 0x02, 0x02, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
	{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
	{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
	{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
};

static uint8_t wait_cursor[16][16] = {
	{ 0x00, 0x00, 0x02, 0x02, 0x02, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
	{ 0x00, 0x00, 0x02, 0x0f, 0x0f, 0x0f, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
	{ 0x00, 0x00, 0x02, 0x0f, 0x0f, 0x0f, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
	{ 0x00, 0x00, 0x02, 0x0f, 0x01, 0x0f, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
	{ 0x00, 0x00, 0x02, 0x0f, 0x01, 0x0f, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
	{ 0x00, 0x02, 0x0f, 0x0f, 0x01, 0x0f, 0x0f, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
	{ 0x02, 0x0f, 0x0f, 0x0f, 0x01, 0x0f, 0x0f, 0x0f, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
	{ 0x02, 0x0f, 0x01, 0x01, 0x01, 0x0f, 0x0f, 0x0f, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
	{ 0x02, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
	{ 0x00, 0x02, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
	{ 0x00, 0x00, 0x02, 0x0f, 0x0f, 0x0f, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
	{ 0x00, 0x00, 0x02, 0x0f, 0x0f, 0x0f, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
	{ 0x00, 0x00, 0x02, 0x0f, 0x0f, 0x0f, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
	{ 0x00, 0x00, 0x02, 0x0f, 0x0f, 0x0f, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
	{ 0x00, 0x00, 0x02, 0x02, 0x02, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
	{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
};

void lcursor_Create_Cursor_Module(void) {
	lbitmap_Alloc_Trans_System_Bitmap(&cursor_bm_gbl, 32, 32);
	lbitmap_Alloc_System_Bitmap(&cursor_back_gbl, 32, 32);
	lbitmap_Erase_Bitmap(&cursor_bm_gbl);
	lbitmap_Erase_Bitmap(&cursor_back_gbl);
	cursor_enable_gbl = 1;
	cursor_show_gbl = 0;
	fade_cursor_gbl = false;
	cur_redraw_gbl = true;
	cursor_state_gbl = 0;
	curs_hot_x = 0;
	curs_hot_y = 0;
	last_curs_x = lio_Mouse_X();
	last_curs_y = lio_Mouse_Y();
	Rect rect;
	lrect_Set_Rect(&rect, 0, 0, 16, 16);
	lbitmap_Set_Bitmap_Clipping(&cursor_bm_gbl, &rect);
	lbitmap_Set_Bitmap_Clipping(&cursor_back_gbl, &rect);
	cursor_module_gbl = true;
	lcursor_Set_Cursor(2);
}

void lcursor_Destroy_Cursor_Module(void) {
	if (cursor_module_gbl) {
		lbitmap_Free_Bitmap(&cursor_bm_gbl);
		lbitmap_Free_Bitmap(&cursor_back_gbl);
		cursor_module_gbl = false;
	}
}

void lcursor_Set_Cursor_HotSpot(int16_t hot_x, int16_t hot_y) {
	if (curs_hot_x != hot_x || curs_hot_y != hot_y) {
		curs_hot_x = hot_x;
		curs_hot_y = hot_y;
		cur_redraw_gbl = true;
	}
}

void lcursor_Get_Cursor_HotSpot(int16_t* out_x, int16_t* out_y) {
	*out_x = curs_hot_x;
	*out_y = curs_hot_y;
}

void lcursor_Set_Cursor(uint8_t cursorId) {
	cursor_id_gbl = cursorId;
	uint8_t* cursor = NULL;
	if (cursorId == 0)
		cursor = (uint8_t*)pointer_cursor;
	else if (cursorId == 1)
		cursor = (uint8_t*)wait_cursor;

	uint8_t* dst = lbitmap_Lock_Bitmap(&cursor_bm_gbl);
	for (int y = 0; y < 32; y++) {
		for (int x = 0; x < 32; x++) {
			if (y < 16 && x < 16)
				dst[x + y * 32] = cursor ? cursor[x + y * 16] : 0;
		}
	}
	lbitmap_Unlock_Bitmap(&cursor_bm_gbl);
}

void lcursor_Push_Cursor_Canvas(void) { lcanvas_Push_Canvas(&cursor_bm_gbl); }

void lcursor_Pop_Cursor_Canvas(void) { lcanvas_Pop_Canvas(); }

void lcursor_Set_Cursor_Canvas(void) { lcanvas_Set_Drawing_Canvas_Bitmap(&cursor_bm_gbl); }

void lcursor_Cursor_To_Screen(int16_t x, int16_t y) {
	if (cursor_external_presentation_gbl) {
		last_curs_x = x;
		last_curs_y = y;
		cur_redraw_gbl = false;
		return;
	}
	if (last_curs_x != x || last_curs_y != y || cur_redraw_gbl) {
		lcursor_Erase_Cursor(0);
		lcursor_Draw_Cursor(x, y, 0);
		cur_redraw_gbl = false;
	}
}

void lcursor_Draw_Cursor(int16_t x, int16_t y, int16_t offscreen) {
	if (lrect_Empty_Rect(&cursor_bm_gbl.clip) || cursor_show_gbl)
		return;
	if (cursor_external_presentation_gbl) {
		last_curs_x = x;
		last_curs_y = y;
		return;
	}

	if (offscreen)
		cursor_state_gbl |= 2;
	else
		cursor_state_gbl |= 1;

	last_curs_x = x;
	last_curs_y = y;
	int16_t left = x - curs_hot_x;
	int16_t top = y - curs_hot_y;
	Rect rect;
	lrect_Set_Rect(&rect, left, top, cursor_bm_gbl.w + left, cursor_bm_gbl.h + top);
	if (offscreen) {
		lcanvas_Copy_Canvas_Portion_To_Bitmap(&cursor_back_gbl, &rect, 0, 0);
		lcanvas_Copy_Bitmap_Clip_To_Canvas(&cursor_bm_gbl, left, top);
	} else {
		lvesa_Copy_Video_Portion_To_Bitmap(&cursor_back_gbl, &rect, 0, 0);
		lvesa_Copy_Bitmap_Clip_To_Video(&cursor_bm_gbl, left, top);
	}
}

void lcursor_Erase_Cursor(int16_t offscreen) {
	if (cursor_external_presentation_gbl)
		return;
	if (offscreen) {
		if (!(cursor_state_gbl & 2))
			return;
		cursor_state_gbl ^= 2;
	} else {
		if (!(cursor_state_gbl & 1))
			return;
		cursor_state_gbl ^= 1;
	}

	int16_t x = last_curs_x - curs_hot_x;
	int16_t y = last_curs_y - curs_hot_y;

	if (offscreen) {
		lcanvas_Copy_Bitmap_Clip_To_Canvas(&cursor_back_gbl, x, y);
		if (lcanvas_Is_Screen_Diff_Used() && !lrect_Empty_Rect(&cursor_bm_gbl.clip)) {
			Rect diff;
			lrect_Copy_Rect(&diff, &cursor_bm_gbl.clip);
			lrect_Offset_Rect(&diff, x - diff.left, y - diff.top);
			lcanvas_Copy_Screen_Portion_To_Diff(&diff);
		}
	} else {
		lvesa_Copy_Bitmap_Clip_To_Video(&cursor_back_gbl, x, y);
	}
}

void lcursor_Cursor_To_Back(void) {
	if (lfade_Trans_Fade_Active()) {
		if (!fade_cursor_gbl) {
			fade_cursor_gbl = true;
			lcursor_Hide_Cursor();
		}
	} else {
		lcursor_Draw_Cursor(last_curs_x, last_curs_y, 1);
	}
}

void lcursor_Cursor_To_Front(void) {
	if (!lfade_Trans_Fade_Active()) {
		if (fade_cursor_gbl) {
			fade_cursor_gbl = false;
			lcursor_Show_Cursor();
		} else {
			lcursor_Erase_Cursor(1);
		}
	}
}

void lcursor_Cursor_From_Fade(void) {
	if (!lfade_Trans_Fade_Active() && fade_cursor_gbl) {
		fade_cursor_gbl = false;
		lcursor_Show_Cursor();
	}
}

bool lcursor_Is_Cursor(void) { return cursor_enable_gbl == 1; }

bool lcursor_Is_Cursor_Visible(void) { return cursor_show_gbl == 0; }

void lcursor_Enable_Cursor(void) {
	cursor_enable_gbl = 1;
	lcursor_Show_Cursor();
}

void lcursor_Disable_Cursor(void) {
	lcursor_Hide_Cursor();
	cursor_enable_gbl = -1;
}

void lcursor_Refresh_Cursor(void) { cur_redraw_gbl = true; }

void lcursor_Show_Cursor(void) {
	if (lcursor_Is_Cursor()) {
		cursor_show_gbl--;
		if (cursor_show_gbl == 0) {
			cur_redraw_gbl = true;
			/* Notify the host when the engine resumes cursor rendering. */
			lmouse_MS_Show_Mouse();
		}
		++g_cursorDisplayCount;
		lcursor_Cursor_To_Screen(last_curs_x, last_curs_y);
	}
}

void lcursor_Hide_Cursor(void) {
	if (lcursor_Is_Cursor()) {
		if (cursor_show_gbl == 0) {
			cur_redraw_gbl = true;
			/* Notify the host when the engine stops cursor rendering. */
			lmouse_MS_Hide_Mouse();
		}
		cursor_show_gbl++;
		--g_cursorDisplayCount;
		lcursor_Cursor_To_Screen(last_curs_x, last_curs_y);
	}
}

void lcursor_port_Set_External_Presentation(bool external) {
	if (cursor_external_presentation_gbl == external)
		return;
	if (external && cursor_module_gbl) {
		lcursor_Erase_Cursor(0);
		lcursor_Erase_Cursor(1);
	}
	cursor_external_presentation_gbl = external;
	cur_redraw_gbl = true;
}

// FUNCTION: TIE98 0x4A54C0
int XCURSOR_Get_Display_Count(void) { return g_cursorDisplayCount; }

// FUNCTION: TIE98 0x4A5570
void XCURSOR_Draw_Software_Cursor_To_Surface(uint8_t* surface, int pitch, int height) {
	int mouse_x = lio_Mouse_X();
	int mouse_y = lio_Mouse_Y();
	if (mouse_x > pitch - 1)
		mouse_x = pitch - 1;
	if (mouse_y > height - 1)
		mouse_y = height - 1;
	int width = pitch - mouse_x;
	if (width > 16)
		width = 16;
	if (width < 0)
		width = 0;
	int rows = height - mouse_y;
	if (rows > 16)
		rows = 16;
	if (rows < 0)
		rows = 0;

	for (int y = 0; y < rows; ++y) {
		uint8_t* output = surface + pitch * (mouse_y + y) + mouse_x;
		for (int x = 0; x < width; ++x) {
			const uint8_t cursor_pixel = g_softwareCursorBitmap[y][x];
			if (cursor_pixel == 1)
				output[x] = g_softwareCursorDarkColorIndex;
			else if (cursor_pixel == 2)
				output[x] = g_softwareCursorBrightColorIndex;
		}
	}
}

// FUNCTION: TIE98 0x4A5630
void XCURSOR_Select_Contrast_Colors(const uint8_t* rgb6_palette) {
	int darkest_sum = 189;
	int brightest_sum = 0;
	uint8_t darkest = 0;
	uint8_t brightest = 0;
	for (int index = 0; index < 256; ++index) {
		const int sum = rgb6_palette[index * 3] + rgb6_palette[index * 3 + 1] + rgb6_palette[index * 3 + 2];
		if (sum > brightest_sum) {
			brightest_sum = sum;
			brightest = (uint8_t)index;
		}
		if (sum < darkest_sum) {
			darkest_sum = sum;
			darkest = (uint8_t)index;
		}
		if (brightest_sum == 189 && darkest_sum == 0)
			break;
	}
	g_softwareCursorDarkColorIndex = darkest;
	g_softwareCursorBrightColorIndex = brightest;
}

void lcursor_emit_render_state(void) {
	LandruCursorRenderState state = { 0 };
	LandruCursorRenderState* out = &state;
	/* Use the live cursor pose (mouse_cursor_x_gbl), not the
	 * rate-limited rendered pose (last_curs_x). last_curs_x is set
	 * inside lcursor_Draw_Cursor, which only fires when the
	 * cursor_rate_gbl=8 PIT-tick gate (~31 Hz) elapses — between
	 * fires it is stale by up to 32 ms. Consumers need the live pose to
	 * observe engine-initiated cursor warps immediately. */
	out->x = mouse_cursor_x_gbl;
	out->y = mouse_cursor_y_gbl;
	out->hot_x = curs_hot_x;
	out->hot_y = curs_hot_y;
	out->w = cursor_bm_gbl.w;
	out->h = cursor_bm_gbl.h;
	/* An externally presented cursor is intentionally absent from the video
	 * bitmap, so its visibility follows Landru's module and nesting state. */
	out->visible = cursor_external_presentation_gbl
					   ? (cursor_module_gbl && cursor_enable_gbl == 1 && cursor_show_gbl == 0)
					   : (cursor_module_gbl && (cursor_state_gbl & 1));
	out->kind = (LandruCursorKind)cursor_id_gbl;
	landru_render_cursor(out);
}

const uint8_t* lcursor_get_bitmap(int16_t* out_w, int16_t* out_h) {
	if (out_w)
		*out_w = cursor_bm_gbl.w;
	if (out_h)
		*out_h = cursor_bm_gbl.h;
	return cursor_bm_gbl.data;
}
