#include <stdlib.h>

#include <landru/bitmap.h>
#include <landru/canvas.h>
#include <landru/cursor.h>
#include <landru/dirty.h>
#include <landru/fade.h>
#include <landru/paint.h>
#include <landru/rect.h>
#include <landru/remap.h>
#include <landru/surface.h>
#include <landru/vesa.h>

bool canvas_module_gbl;
// GLOBAL: TIE 0xFBBF4
bool diff_exists_gbl;
// GLOBAL: TIE 0xFBBFA
bool diff_used_gbl;
bool diff_valid_gbl;
BitmapStruct screen_bm_gbl;
BitmapStruct diff_bm_gbl;
BitmapStruct buff_bm_gbl;
BitmapStruct* canvas_bm_gbl;
// GLOBAL: TIE 0xFBBC8
BitmapStruct* draw_bm_gbl;

// GLOBAL: TIE 0xFBB9C
void* buff_gbl;
// GLOBAL: TIE 0xFBBE4
void* draw_buff_gbl;
// GLOBAL: TIE 0xFBBE8
int16_t buff_size_gbl;
int16_t canvas_stack_size_gbl;
// GLOBAL: TIE 0xFBBF6
int16_t draw_w_gbl;
int16_t draw_h_gbl;
// GLOBAL: TIE 0xFBBF2
int16_t clip_left_gbl;
// GLOBAL: TIE 0xFBBEA
int16_t clip_top_gbl;
// GLOBAL: TIE 0xFBBEC
int16_t clip_right_gbl;
// GLOBAL: TIE 0xFBBEE
int16_t clip_bottom_gbl;
BitmapStruct* canvas_stack_bm_gbl[4];

void lcanvas_Create_Canvas_Module(int16_t buffSize, int16_t width, int16_t height, bool diff) {
	lbitmap_Alloc_System_Bitmap(&screen_bm_gbl, width, height);

	if (diff) {
		lbitmap_Alloc_System_Bitmap(&diff_bm_gbl, width, height);
		diff_exists_gbl = true;
		diff_used_gbl = true;
	} else {
		diff_exists_gbl = false;
		diff_used_gbl = false;
	}

	if (buffSize) {
		buff_gbl = malloc(buffSize);
		buff_size_gbl = buffSize;
	} else {
		buff_gbl = NULL;
		buff_size_gbl = 0;
	}

	lbitmap_Init_Bitmap(&buff_bm_gbl);
	buff_bm_gbl.data = buff_gbl;
	diff_valid_gbl = false;
	canvas_bm_gbl = &screen_bm_gbl;
	lcanvas_Set_Drawing_Canvas();
	canvas_stack_size_gbl = 0;
	canvas_module_gbl = true;
}

void lcanvas_Destroy_Canvas_Module(void) {
	if (!canvas_module_gbl)
		return;

	free(buff_gbl);
	lbitmap_Free_Bitmap(&screen_bm_gbl);
	if (diff_exists_gbl) {
		lbitmap_Free_Bitmap(&diff_bm_gbl);
	}
	canvas_module_gbl = false;
}

BitmapStruct* lcanvas_Get_Current_Canvas_Bitmap(void) { return canvas_bm_gbl; }

void* lcanvas_Ask_Buffer(int16_t* outBufSize) {
	*outBufSize = buff_size_gbl;
	return buff_gbl;
}

bool lcanvas_Push_Canvas(BitmapStruct* bitmap) {
	if (canvas_stack_size_gbl >= 4)
		return false;
	canvas_stack_bm_gbl[canvas_stack_size_gbl] = canvas_bm_gbl;
	canvas_bm_gbl = bitmap;
	canvas_stack_size_gbl++;
	lcanvas_Set_Drawing_Canvas();
	return true;
}

bool lcanvas_Push_Buffer_Canvas(int16_t width, int16_t height) {
	if (!buff_bm_gbl.data)
		return false;
	if (width * height > buff_size_gbl)
		return false;
	buff_bm_gbl.w = width;
	buff_bm_gbl.h = height;
	return lcanvas_Push_Canvas(&buff_bm_gbl);
}

void lcanvas_Pop_Canvas(void) {
	if (canvas_stack_size_gbl) {
		canvas_stack_size_gbl--;
		canvas_bm_gbl = canvas_stack_bm_gbl[canvas_stack_size_gbl];
		lcanvas_Set_Drawing_Canvas();
	}
}

bool lcanvas_Activate_Screen_Bitmap(BitmapStruct* old_bitmap, BitmapStruct* new_bitmap) {
	if (!old_bitmap || !new_bitmap || canvas_stack_size_gbl != 0 || canvas_bm_gbl != old_bitmap)
		return false;
	canvas_bm_gbl = new_bitmap;
	lcanvas_Set_Drawing_Canvas();
	return true;
}

bool lcanvas_Is_Screen_Drawing_Target(void) { return draw_bm_gbl == lsurface_Get_Active_Render_Bitmap(); }

/* Non-screen coordinates are normally excluded from render publication.
 * A caller may opt in when the consumer understands the auxiliary target. */
static bool s_render_allow_non_screen;

void lcanvas_Set_Render_Allow_Non_Screen(bool on) { s_render_allow_non_screen = on; }

bool lcanvas_Render_Emit_Allowed(void) {
	return lcanvas_Is_Screen_Drawing_Target() || s_render_allow_non_screen;
}

/* Route explicitly captured scratch-canvas work to the auxiliary target. */
LandruRenderTarget lcanvas_Render_Emit_Target(void) {
	return s_render_allow_non_screen ? LANDRU_RENDER_TARGET_AUXILIARY : LANDRU_RENDER_TARGET_SCREEN;
}

/* Suppress flag for the lfont render-capture path — see canvas.h.
 * textext's draw callback owns this. The actor system's dirty-rect
 * machinery fires the callback on a non-deterministic cadence, so we
 * can't rely on it for per-tick render coverage; textext re-emits
 * its own subtitle records every tick and uses this flag to keep the
 * lfont hook from double-emitting on the redraw frames. */
static bool s_suppress_text_render;

void lcanvas_Set_Suppress_Text_Render(bool on) { s_suppress_text_render = on; }

bool lcanvas_Render_Text_Emit_Allowed(void) {
	return !s_suppress_text_render && lcanvas_Render_Emit_Allowed();
}

void lcanvas_Set_Drawing_Canvas(void) { lcanvas_Set_Drawing_Canvas_Bitmap(canvas_bm_gbl); }

void lcanvas_Set_Drawing_Canvas_Bitmap(BitmapStruct* bitmap) {
	int16_t width = bitmap->w;
	int16_t height = bitmap->h;
	void* ptr = lbitmap_Lock_Bitmap(bitmap);
	if (ptr) {
		draw_buff_gbl = ptr;
		draw_w_gbl = width;
		draw_h_gbl = height;
	}
	lbitmap_Unlock_Bitmap(bitmap);
	draw_bm_gbl = bitmap;
	clip_left_gbl = bitmap->clip.left;
	clip_top_gbl = bitmap->clip.top;
	clip_right_gbl = bitmap->clip.right;
	clip_bottom_gbl = bitmap->clip.bottom;
}

void lcanvas_Get_Drawing_Canvas_Bounds(Rect* rect) {
	if (lcanvas_Is_Screen_Drawing_Target())
		lsurface_Get_Logical_Bounds(rect);
	else
		lrect_Set_Rect(rect, 0, 0, draw_bm_gbl->w, draw_bm_gbl->h);
}

void lcanvas_Set_Drawing_Canvas_Clip(Rect* rect) {
	draw_bm_gbl->clip = *rect;
	clip_left_gbl = rect->left;
	clip_top_gbl = rect->top;
	clip_right_gbl = rect->right;
	clip_bottom_gbl = rect->bottom;
}

void lcanvas_Get_Drawing_Canvas_Clip(Rect* rect) { *rect = draw_bm_gbl->clip; }

void lcanvas_Max_Drawing_Canvas_Clip(void) {
	lbitmap_Max_Bitmap_Clipping(draw_bm_gbl);
	clip_left_gbl = draw_bm_gbl->clip.left;
	clip_top_gbl = draw_bm_gbl->clip.top;
	clip_right_gbl = draw_bm_gbl->clip.right;
	clip_bottom_gbl = draw_bm_gbl->clip.bottom;
}

bool lcanvas_Clip_Rect_To_Canvas(Rect* rect) { return lbitmap_Clip_Rect_To_Bitmap(draw_bm_gbl, rect); }

void lcanvas_Enable_Screen_Diff(void) {
	diff_used_gbl = diff_exists_gbl;
	diff_valid_gbl = false;
}

void lcanvas_Disable_Screen_Diff(void) { diff_used_gbl = false; }

void lcanvas_Invalid_Screen_Diff(void) { diff_valid_gbl = false; }

void lcanvas_Valid_Screen_Diff(void) { diff_valid_gbl = diff_used_gbl; }

bool lcanvas_Is_Screen_Diff(void) { return diff_exists_gbl; }

bool lcanvas_Is_Screen_Diff_Used(void) { return diff_used_gbl && diff_exists_gbl; }

bool lcanvas_Is_Screen_Diff_Valid(void) { return diff_valid_gbl && diff_used_gbl && diff_exists_gbl; }

void lcanvas_Copy_Screen_To_Diff(void) {
	BitmapStruct* screen = lsurface_Get_Active_Render_Bitmap();
	BitmapStruct* diff = lsurface_Get_Active_Diff_Bitmap();
	if (screen && diff)
		lbitmap_Copy_Bitmap(diff, screen, 0, 0);
}

void lcanvas_Copy_Screen_Portion_To_Diff(Rect* rect) {
	BitmapStruct* screen = lsurface_Get_Active_Render_Bitmap();
	BitmapStruct* diff = lsurface_Get_Active_Diff_Bitmap();
	if (screen && diff)
		lbitmap_Copy_Bitmap_Portion(diff, screen, rect, rect->left, rect->top);
}

void lcanvas_Copy_Diff_To_Screen(void) {
	BitmapStruct* screen = lsurface_Get_Active_Render_Bitmap();
	BitmapStruct* diff = lsurface_Get_Active_Diff_Bitmap();
	if (screen && diff)
		lbitmap_Copy_Bitmap(screen, diff, 0, 0);
}

void lcanvas_Copy_Diff_Portion_To_Screen(Rect* rect) {
	BitmapStruct* screen = lsurface_Get_Active_Render_Bitmap();
	BitmapStruct* diff = lsurface_Get_Active_Diff_Bitmap();
	if (screen && diff)
		lbitmap_Copy_Bitmap_Portion(screen, diff, rect, rect->left, rect->top);
}

/* Push a full-screen fade task with appropriate cursor management and
 * end-of-task work. Caller-task yields after this call; on the next
 * step the FadeTask has popped and the screen is in its post-fade
 * state with the cursor restored. */
void lcanvas_Push_Fade_Screen_To_Video_Task(int16_t fade_style) {
	Rect bounds;
	lsurface_Get_Logical_Bounds(&bounds);

	bool cursor_was_visible = lcursor_Is_Cursor_Visible();
	if (cursor_was_visible)
		lcursor_Cursor_To_Back();

	(void)lfade_Push_Fade_To_Video_Screen_Task(
		&bounds, fade_style, cursor_was_visible ? FADE_END_CURSOR_TO_FRONT : FADE_END_CURSOR_FROM_FADE,
		/*force_refresh_view=*/false);
}

void lcanvas_Copy_Screen_To_Video(Rect* rect) {
	BitmapStruct* screen = lsurface_Get_Active_Render_Bitmap();
	BitmapStruct* diff = lsurface_Get_Active_Diff_Bitmap();
	Rect clipped;
	if (!screen)
		return;
	lbitmap_Max_Bitmap_Clipping(screen);
	lrect_Copy_Rect(&clipped, rect);
	lbitmap_Clip_Rect_To_Bitmap(screen, &clipped);

	if (diff_valid_gbl && diff_used_gbl && diff) {
		lvesa_Copy_Diff_Bitmap_Portion_To_Video(screen, diff, &clipped, clipped.left, clipped.top);
	} else {
		lvesa_Copy_Bitmap_Portion_To_Video(screen, &clipped, clipped.left, clipped.top);
		if (diff_used_gbl && diff) {
			lbitmap_Copy_Bitmap_Portion(diff, screen, &clipped, clipped.left, clipped.top);
		}
	}
	/* Leave the dirty flag set for the host's next presentation. */
}

void lcanvas_Copy_Screen_Portion_To_Video(Rect* rect, int16_t x, int16_t y) {
	BitmapStruct* screen = lsurface_Get_Active_Render_Bitmap();
	BitmapStruct* diff = lsurface_Get_Active_Diff_Bitmap();
	if (!screen)
		return;
	lvesa_Copy_Bitmap_Portion_To_Video(screen, rect, x, y);
	if (diff)
		lbitmap_Copy_Bitmap_Portion(diff, screen, rect, rect->left, rect->top);
}

void lcanvas_Copy_Dirty_Screen_To_Video(Rect* clip) {
	BitmapStruct* screen = lsurface_Get_Active_Render_Bitmap();
	BitmapStruct* diff = lsurface_Get_Active_Diff_Bitmap();
	Rect rect;
	if (!screen)
		return;

	if (lfade_Trans_Fade_Active()) {
		diff_valid_gbl = false;
		ldirty_Swap_Dirty_List(1);
		lvesa_Copy_Bitmap_Portion_To_Video(screen, clip, clip->left, clip->top);
	} else if (diff_valid_gbl && diff_used_gbl && diff) {
		ldirty_Prepare_Dirty_List();
		while (ldirty_Next_Dirty_Rect(&rect)) {
			if (lrect_Clip_Rect(&rect, clip)) {
				lvesa_Copy_Diff_Bitmap_Portion_To_Video(screen, diff, &rect, rect.left, rect.top);
			}
		}
	} else {
		ldirty_Prepare_Dirty_List();
		while (ldirty_Next_Dirty_Rect(&rect)) {
			if (lrect_Clip_Rect(&rect, clip)) {
				lvesa_Copy_Bitmap_Portion_To_Video(screen, &rect, rect.left, rect.top);
				if (diff_used_gbl && diff) {
					lbitmap_Copy_Bitmap_Portion(diff, screen, &rect, rect.left, rect.top);
				}
			}
		}
		if (diff_used_gbl && diff)
			diff_valid_gbl = diff_used_gbl;
	}
}

void lcanvas_Erase_Canvas(void) { lbitmap_Erase_Bitmap(draw_bm_gbl); }

void lcanvas_Erase_Canvas_Rect(Rect* rect) {
	Rect saved_clip = draw_bm_gbl->clip;
	lbitmap_Max_Bitmap_Clipping(draw_bm_gbl);
	lcanvas_Set_Drawing_Canvas_Clip(&draw_bm_gbl->clip);
	int16_t color = lremap_Get_Remap(REMAP_GRAY_0);
	lpaint_Paint_Clipped_Rect(rect, color);
	draw_bm_gbl->clip = saved_clip;
	clip_left_gbl = saved_clip.left;
	clip_top_gbl = saved_clip.top;
	clip_right_gbl = saved_clip.right;
	clip_bottom_gbl = saved_clip.bottom;
}

void lcanvas_Copy_Bitmap_To_Canvas(BitmapStruct* bitmap, int16_t x, int16_t y) {
	lbitmap_Copy_Bitmap(draw_bm_gbl, bitmap, x, y);
}

void lcanvas_Copy_Bitmap_Clip_To_Canvas(BitmapStruct* bitmap, int16_t x, int16_t y) {
	lbitmap_Copy_Bitmap_Clip(draw_bm_gbl, bitmap, x, y);
}

void lcanvas_Copy_Bitmap_Portion_To_Canvas(BitmapStruct* bitmap, Rect* rect, int16_t x, int16_t y) {
	lbitmap_Copy_Bitmap_Portion(draw_bm_gbl, bitmap, rect, x, y);
}

void lcanvas_Copy_Canvas_Portion_To_Bitmap(BitmapStruct* bitmap, Rect* rect, int16_t x, int16_t y) {
	lbitmap_Copy_Bitmap_Portion(bitmap, draw_bm_gbl, rect, x, y);
}
