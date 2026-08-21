#ifndef LANDRU_CANVAS_H
#define LANDRU_CANVAS_H

#include <stdbool.h>
#include <stdint.h>

#include <landru/bitmap.h>
#include <landru/render.h>

extern bool canvas_module_gbl;
extern bool diff_exists_gbl;
extern bool diff_used_gbl;
extern bool diff_valid_gbl;
extern BitmapStruct screen_bm_gbl;
extern BitmapStruct diff_bm_gbl;
extern BitmapStruct buff_bm_gbl;
extern BitmapStruct* canvas_bm_gbl;
extern BitmapStruct* draw_bm_gbl;

extern void* buff_gbl;
extern void* draw_buff_gbl;
extern int16_t buff_size_gbl;
extern int16_t canvas_stack_size_gbl;
extern int16_t draw_w_gbl;
extern int16_t draw_h_gbl;
extern int16_t clip_left_gbl;
extern int16_t clip_top_gbl;
extern int16_t clip_right_gbl;
extern int16_t clip_bottom_gbl;
extern BitmapStruct* canvas_stack_bm_gbl[4];

void lcanvas_Create_Canvas_Module(int16_t buffSize, int16_t width, int16_t height, bool diff);
void lcanvas_Destroy_Canvas_Module(void);
BitmapStruct* lcanvas_Get_Current_Canvas_Bitmap(void);
void* lcanvas_Ask_Buffer(int16_t* outBufSize);
bool lcanvas_Push_Canvas(BitmapStruct* bitmap);
bool lcanvas_Push_Buffer_Canvas(int16_t width, int16_t height);
void lcanvas_Pop_Canvas(void);

/* Changes the session screen canvas at a scene boundary. */
bool lcanvas_Activate_Screen_Bitmap(BitmapStruct* old_bitmap, BitmapStruct* new_bitmap);

/* True iff the active drawing target is the canonical screen bitmap.
 * Used by render capture to suppress paint/draw/text records
 * generated while a non-screen scratch canvas is active. */
bool lcanvas_Is_Screen_Drawing_Target(void);

/* Render-capture gate. Same as Is_Screen_Drawing_Target except it also
 * returns true when a caller has explicitly opted in via
 * lcanvas_Set_Render_Allow_Non_Screen. emit_paint / lactor_emit_draw /
 * lfont_Draw_Clipped_Text gate on this. */
bool lcanvas_Render_Emit_Allowed(void);

/* Routing tag for a render record. The auxiliary target is active while
 * non-screen canvas capture has been explicitly enabled. */
LandruRenderTarget lcanvas_Render_Emit_Target(void);

/* Override the canvas-leak gate while a non-screen canvas is bound.
 * Caller MUST set false before returning to general engine code,
 * otherwise scratch-canvas primitives leak into the render sink. */
void lcanvas_Set_Render_Allow_Non_Screen(bool on);

/* Suppress lfont_Draw_Clipped_Text render capture while set. Callers that
 * publish persistent text separately use this to prevent duplicate records.
 * Nested set/clear is not supported. */
void lcanvas_Set_Suppress_Text_Render(bool on);

/* lfont_Draw_Clipped_Text gates render capture on this combined
 * accessor. It is true iff the canvas gate allows it and no caller has set
 * the suppress flag. */
bool lcanvas_Render_Text_Emit_Allowed(void);
void lcanvas_Set_Drawing_Canvas(void);
void lcanvas_Set_Drawing_Canvas_Bitmap(BitmapStruct* bitmap);
void lcanvas_Get_Drawing_Canvas_Bounds(Rect* rect);
void lcanvas_Set_Drawing_Canvas_Clip(Rect* rect);
void lcanvas_Get_Drawing_Canvas_Clip(Rect* rect);
void lcanvas_Max_Drawing_Canvas_Clip(void);
bool lcanvas_Clip_Rect_To_Canvas(Rect* rect);
void lcanvas_Enable_Screen_Diff(void);
void lcanvas_Disable_Screen_Diff(void);
void lcanvas_Invalid_Screen_Diff(void);
void lcanvas_Valid_Screen_Diff(void);
bool lcanvas_Is_Screen_Diff(void);
bool lcanvas_Is_Screen_Diff_Used(void);
bool lcanvas_Is_Screen_Diff_Valid(void);
void lcanvas_Copy_Screen_To_Diff(void);
void lcanvas_Copy_Screen_Portion_To_Diff(Rect* rect);
void lcanvas_Copy_Diff_To_Screen(void);
void lcanvas_Copy_Diff_Portion_To_Screen(Rect* rect);
/* Push a fade-to-video task with cursor + present + refresh handling
 * as the task's end work. Caller-task yields (CONTINUE) after this
 * call; resumes on its next step with the fade complete. */
void lcanvas_Push_Fade_Screen_To_Video_Task(int16_t fade_style);
void lcanvas_Copy_Screen_To_Video(Rect* rect);
void lcanvas_Copy_Screen_Portion_To_Video(Rect* rect, int16_t x, int16_t y);
void lcanvas_Copy_Dirty_Screen_To_Video(Rect* rect);
void lcanvas_Erase_Canvas(void);
void lcanvas_Erase_Canvas_Rect(Rect* rect);
void lcanvas_Copy_Bitmap_To_Canvas(BitmapStruct* bitmap, int16_t x, int16_t y);
void lcanvas_Copy_Bitmap_Clip_To_Canvas(BitmapStruct* bitmap, int16_t x, int16_t y);
void lcanvas_Copy_Bitmap_Portion_To_Canvas(BitmapStruct* bitmap, Rect* rect, int16_t x, int16_t y);
void lcanvas_Copy_Canvas_Portion_To_Bitmap(BitmapStruct* bitmap, Rect* rect, int16_t x, int16_t y);

#endif
