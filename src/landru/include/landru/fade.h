#ifndef LANDRU_FADE_H
#define LANDRU_FADE_H

#include <stdbool.h>
#include <stdint.h>

#include <landru/rect.h>

typedef enum {
	FADE_WIPE_NONE = 0,
	FADE_WIPE_INSTANT = 1,
	FADE_WIPE_SNAP_ON = 2,
	FADE_WIPE_SNAP_OFF = 3,
	FADE_WIPE_RIGHT = 4,
	FADE_WIPE_LEFT = 5,
	FADE_WIPE_DOWN = 6,
	FADE_WIPE_UP = 7,
	FADE_WIPE_DIAG_TL_BR = 8,
	FADE_WIPE_DIAG_TR_BL = 9,
	FADE_WIPE_DIAG_BL_TR = 10,
	FADE_WIPE_DIAG_BR_TL = 11,
	FADE_WIPE_EXPAND_RIGHT = 12,
	FADE_WIPE_EXPAND_LEFT = 13,
	FADE_WIPE_EXPAND_UP = 14,
	FADE_WIPE_EXPAND_DOWN = 15,
	FADE_WIPE_CORNER_TL = 16,
	FADE_WIPE_CORNER_TR = 17,
	FADE_WIPE_CORNER_BL = 18,
	FADE_WIPE_CORNER_BR = 19,
	FADE_WIPE_IRIS_H = 20,
	FADE_WIPE_IRIS_V = 21,
	FADE_WIPE_IRIS_BOX = 22,
	FADE_WIPE_SPECIAL_1 = 23,
	FADE_WIPE_SPECIAL_2 = 24,
	FADE_WIPE_SPECIAL_3 = 25,
} FadeWipeMode;

typedef enum {
	FADE_COLOR_NONE = 0,
	FADE_COLOR_CROSSFADE = 1,  /* instant src→dst palette swap at half */
	FADE_COLOR_TWO_PHASE = 2,  /* src→target (phase 1), target→dst (phase 2) */
	FADE_COLOR_TO_COLOR = 3,   /* src palette → mono target color (Palette_To_Mono) */
	FADE_COLOR_FROM_COLOR = 4, /* mono target color → dst palette (Mono_To_Palette) */
	/* Smooth src→dst palette interpolation (Palette_To_Palette).
	 * Originally named FADE_COLOR_TO_BLACK because the typical caller
	 * pre-stages dst_pal as all-black to get a fade-out, but the mode
	 * itself is a generic palette lerp — the (0,0,0) target color it
	 * passes to Build_Fade_Palette is unused. Used for both fades to
	 * black and mid-film palette swaps (e.g. logo_f cel 18 swaps from
	 * logoluke to logoman with this mode). */
	FADE_COLOR_PAL_TO_PAL = 5,
} FadeColorMode;

typedef struct {
	uint8_t chan_mode[4];
	int16_t chan_delay[4];
	int16_t chan_sustain[4];
	int16_t chan_step[4];
	int16_t chan_total[4];
	uint8_t full_mode;
	int16_t full_delay;
	int16_t full_sustain;
	int16_t full_step;
	int16_t full_total;
	uint8_t color_mode;
	int16_t color_delay;
	int16_t color_sustain;
	int16_t color_step;
	int16_t color_total;
	uint8_t fade_red;
	uint8_t fade_green;
	uint8_t fade_blue;
} Fade;

void lfade_Create_Fade_Module(void);
void lfade_Destroy_Fade_Module(void);
Fade* lfade_Alloc_Fade(void);
void lfade_Free_Fade(Fade* fade);
void lfade_Start_Fade(int16_t channel, FadeWipeMode mode, FadeColorMode color_mode, int16_t capture_dest,
					  int16_t delay, int16_t sustain);
void lfade_Start_Full_Fade(FadeWipeMode mode, FadeColorMode color_mode, int16_t capture_dest, int16_t delay,
						   int16_t sustain);
void lfade_Start_Color_Fade(FadeColorMode color_mode, int16_t capture_dest, int16_t total_steps,
							int16_t delay, int16_t sustain);
void lfade_Set_Fade_Color(uint8_t red, uint8_t green, uint8_t blue);
int16_t lfade_Calc_Fade_Length(FadeWipeMode mode);
void lfade_Fade_To_Video_Palette(int32_t current_time, int32_t end_time);
/* Post-fade cursor restoration policy. The pre-fade Cursor_To_Back
 * (when needed) is the caller's responsibility — only the trailing
 * action is captured here so the FadeTask's end callback can run it
 * synchronously when the task pops. */
typedef enum {
	FADE_END_CURSOR_NONE = 0,
	FADE_END_CURSOR_TO_FRONT = 1,  /* cursor was visible — Cursor_To_Front */
	FADE_END_CURSOR_FROM_FADE = 2, /* cursor was hidden — Cursor_From_Fade */
} FadeEndCursorAction;

/* Push a FadeTask onto the tie_core task stack. Caller-task yields
 * (CONTINUE); when the task pops, its end callback runs all the
 * post-fade housekeeping (cursor restoration, Refresh_View for
 * sustained fades). Returns 1 on success, 0 on task-stack overflow.
 * The clip rect is copied into task storage by value so caller
 * stack locals are safe. */
int lfade_Push_Fade_To_Video_Screen_Task(const Rect* clip, int16_t is_dialog, FadeEndCursorAction end_cursor,
										 bool force_refresh_view);
void lfade_Calc_Fade_Lock_Time(int32_t* lock_end, int32_t* lock_start);
void lfade_Fade_Copy_To_Video(Rect* bounds, FadeWipeMode mode, int16_t step, int16_t total,
							  int16_t is_not_last);
bool lfade_Is_Fade_Step(FadeWipeMode mode, int16_t sustain, int16_t delay, int32_t cur, int32_t end);
bool lfade_Is_Fade_Delay_Step(FadeWipeMode mode, int16_t sustain, int32_t cur, int32_t end);
bool lfade_Fade_Active(void);
bool lfade_Trans_Fade_Active(void);

/* Publish the current full-frame color fade to the optional render sink.
 * Per-channel and wipe modes remain classic-framebuffer operations. */
void lfade_emit_render_state(void);

#endif
