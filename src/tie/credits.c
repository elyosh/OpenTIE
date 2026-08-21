#include <stdlib.h>
#include <string.h>

#include "landru/viewadd.h"
#include "tie/credits.h"
#include "tie/shellext.h"
#include "tie/shipext.h"
#include "tie_runtime/snapshot/snapshot.h"
#include "tie_runtime/snapshot/snapshot_internal.h"
#include <landru/task.h>

#include "landru/actcust.h"
#include "landru/actdelt.h"
#include "landru/actor.h"
#include "landru/canvas.h"
#include "landru/dirty.h"
#include "landru/error.h"
#include "landru/fade.h"
#include "landru/font.h"
#include "landru/paint.h"
#include "landru/pal.h"
#include "landru/paragrp.h"
#include "landru/rect.h"
#include "landru/res.h"
#include "landru/timer.h"
#include "landru/view.h"

#include "tie/stub.h"

/* ---- Static globals ---- */

static Rect credit_dirty_rect;
static Actor* red_bar;
static Actor* blue_bar;
static Actor* credit_actor; /* custom draw actor for the credits */
static Actor* stars_actor;
static int16_t next_credit_scene;
// GLOBAL: TIE 0xF5E02
static int16_t credit_text_len; /* hold duration per credit (130) */
static int16_t film_time;       /* current frame counter */
// GLOBAL: TIE 0xF5E06
static void* star_buffer; /* 320x100 star pixel cache */
// GLOBAL: TIE 0xF5E08
static int16_t num_credit_lines; /* paragraph count in credit text */
// GLOBAL: TIE 0xF5E0A
static int16_t credit_film_len; /* total animation length */
// GLOBAL: TIE 0xF5E0C
static void* credit_text; /* paragraph data from tietext0.lfd */

/* ================================================================
 * Helpers
 * ================================================================ */

/* Copy 320x100 star buffer to canvas twice (y=0 and y=100) to tile
 * a 320x200 star background.
 *
 * Also emits two stars draws_2D records for the HD snapshot. The
 * stars actor's own draw is NOT invoked — classic FB already has the
 * starfield from the buffer copies above. The emits exist only so
 * the cutscene compositor can render the HD `stars` sprite at the
 * same two positions; the buffer-copy → black+sparse-delta wipe
 * that erases last frame's bar/text trails on classic is mirrored
 * in HD by the opaque HD stars sprite drawn before bars and text in
 * z order. */
static void Credit_Stars_To_Back(void) {
	Rect r;
	lrect_Set_Rect(&r, 0, 0, 320, 100);
	stub_Copy_From_Clipped_Buffer(star_buffer, &r, 0, 0, 320, 100);
	stub_Copy_From_Clipped_Buffer(star_buffer, &r, 0, 100, 320, 100);

	lactor_emit_draw(stars_actor, 0, 0);
	lactor_emit_draw(stars_actor, 0, 100);
}

/* Render a star actor into the 320x100 star buffer. Fills with black,
 * calls the actor's draw, copies canvas to buffer. */
static void Credit_Actor_To_Buffer(Actor* actor, void* buffer) {
	Rect r;
	lrect_Set_Rect(&r, 0, 0, 320, 100);
	if (actor->draw) {
		lpaint_Paint_Clipped_Rect(&r, 0);
		actor->draw(actor, &r, &r, actor->x, actor->y, 1);
	}
	stub_Copy_To_Clipped_Buffer(buffer, &r, 0, 0, 320, 100);
}

/* Initialize credit display state: dirty rect, paragraph count,
 * text hold duration, total film length. */
static void Init_Credit_Info(void) {
	lrect_Set_Rect(&credit_dirty_rect, 40, 40, 280, 160);
	num_credit_lines = lparagrp_Count_Paragraphs(credit_text);
	credit_text_len = 130;
	film_time = 0;
	credit_film_len = 90 * (num_credit_lines - 1) + 138;
}

/* ================================================================
 * View update callback
 * ================================================================ */

static void end_View(int32_t frame_num) {
	(void)frame_num;
	int16_t exit_id;
	int16_t done = (film_time == credit_film_len) ? 1 : 0;
	if (shellext_Check_Scene_Exit(&exit_id, next_credit_scene, next_credit_scene, done))
		lerror_Set_Landru_Exit(exit_id);
	film_time++;
}

/* ================================================================
 * Credit draw callback
 * ================================================================ */

/* Each credit paragraph occupies a 130-frame window within the timeline.
 * Paragraphs are spaced 90 frames apart, so they overlap.
 *
 * time_offset 0..59:   fade in  — color ramps from base_y toward target
 * time_offset 60..99:  hold     — red/blue bars animate in
 * time_offset 100..129: fade out — text_y rises, color fades
 *
 * base_y = film_time + 96, decremented by 90 per paragraph.
 * color = palette index for the text (ramps 167..239 range).
 * text_y = vertical position for text block. */
static int draw_Credit(Actor* actor, Rect* bounds, Rect* clip, int16_t xoff, int16_t yoff, int16_t refresh) {
	(void)actor;
	(void)xoff;
	(void)yoff;

	if (!refresh)
		return 1;

	/* Film ended: black screen */
	if (credit_film_len <= film_time) {
		Rect r;
		lrect_Set_Rect(&r, 0, 0, 320, 200);
		lpaint_Paint_Clipped_Rect(&r, 0);
		lcanvas_Invalid_Screen_Diff();
		ldirty_Max_Dirty_List();
		return 1;
	}

	/* Draw tiled star background */
	Credit_Stars_To_Back();

	int16_t time_offset = film_time;
	int16_t credit_idx = 0;
	int16_t base_y = film_time + 96;

	/* Walk through each credit paragraph */
	while (credit_idx < num_credit_lines) {
		if (time_offset < 0)
			break;

		if (time_offset < credit_text_len) {
			/* This paragraph is visible */
			int16_t color;
			int16_t text_y;

			if (time_offset < 60) {
				/* Fade-in phase */
				color = base_y;
				text_y = 140 - time_offset;
			} else {
				int16_t hold_time = time_offset - 60;
				if (hold_time < 40) {
					/* Hold phase */
					text_y = 80;
					if (hold_time >= 31)
						color = time_offset - 60 + 167;
					else
						color = base_y;
				} else {
					/* Fade-out phase */
					color = time_offset - 100 + 207;
					text_y = 80 - (time_offset - 100);
					if (color > 239)
						color = 239;
				}
			}

			int16_t num_strings = lparagrp_Count_Paragraph_Strings(credit_text, credit_idx);

			/* Draw red/blue bar decorations during hold phase */
			if (time_offset >= 45) {
				int16_t bar_width;

				if (time_offset - 45 >= 35) {
					if (time_offset - 80 < 0) {
						bar_width = 280;
					} else {
						bar_width = 8 * (time_offset - 80 + 35);
					}
				} else {
					bar_width = 8 * (time_offset - 45);
				}

				if (bar_width != -1) {
					lactdelt_Draw_Delta_Actor(red_bar, bounds, clip, bar_width - 240, 79, refresh);

					Rect saved_clip;
					lcanvas_Get_Drawing_Canvas_Clip(&saved_clip);

					Rect bar_clip;
					lrect_Set_Rect(&bar_clip, 0, 0, 320, 10 * (num_strings - 1) + 93);
					lrect_Clip_Rect(&bar_clip, clip);
					lcanvas_Set_Drawing_Canvas_Clip(&bar_clip);

					lactdelt_Draw_Delta_Actor(blue_bar, &bar_clip, &bar_clip, 320 - bar_width, 91, refresh);

					lcanvas_Set_Drawing_Canvas_Clip(&saved_clip);
				}
			}

			/* Draw text lines */
			Rect text_rect;
			lrect_Set_Rect(&text_rect, 0, text_y, 320, text_y + 10);

			int16_t i;
			for (i = 0; i < num_strings; i++) {
				char line_buf[80];
				lparagrp_Get_Paragraph_String(credit_text, line_buf, credit_idx, i);
				lfont_Print_Centered_Text(line_buf, &text_rect, color, 0);

				int16_t spacing = i ? 10 : 12;
				lrect_Offset_Rect(&text_rect, 0, spacing);
			}
		}

		/* Advance to next paragraph */
		base_y -= 90;
		time_offset -= 90;
		credit_idx++;
	}

	ldirty_Dirty_Rect(&credit_dirty_rect);
	return 1;
}

/* ================================================================
 * Entry point
 * ================================================================ */

typedef enum {
	CREDITS_PHASE_BEGIN = 0,
	CREDITS_PHASE_CLEANUP = 1,
} CreditsPhase;

typedef struct CreditsTask {
	SceneHeadStruct* scene_head;
	ResFile* credit_res;
	ResFile* text_res;
	CreditsPhase phase;
} CreditsTask;

static LandruTaskStepResult credits_task_step(void* self) {
	CreditsTask* t = (CreditsTask*)self;

	if (t->phase == CREDITS_PHASE_BEGIN) {
		Rect r;

		/* Determine next scene after credits */
		if (shellext_Get_Cur_Scene() == SCENE_CREDITS)
			next_credit_scene = SCENE_REGISTER;
		else
			next_credit_scene = shipext_Next_Battle_Cutscene();

		/* Load resources */
		t->credit_res = shellext_Open_Empire_Resource("credits.lfd");
		t->text_res = shellext_Open_Empire_Resource("tietext0.lfd");
		credit_text = lparagrp_Res_Paragraph(t->text_res, "credits");

		/* Allocate star buffer and render star background */
		lrect_Set_Rect(&r, 0, 0, 320, 200);
		star_buffer = calloc(1, 32000);

		stars_actor = lactdelt_Res_Delta_Actor("stars", &r, 0, 0, 100);
		lactor_Set_Actor_Time(stars_actor, 0, 0);

		red_bar = lactdelt_Res_Delta_Actor("redbar", &r, 0, 0, 100);
		lactor_Set_Actor_Time(red_bar, 0, 0);

		blue_bar = lactdelt_Res_Delta_Actor("bluebar", &r, 0, 0, 100);
		lactor_Set_Actor_Time(blue_bar, 0, 0);

		Credit_Actor_To_Buffer(stars_actor, star_buffer);

		credit_actor = lactcust_Alloc_Custom_Actor(NULL, &r, 0, 0, 0);
		lactor_Set_Actor_Draw_Function(credit_actor, (lactorDrawFunc)draw_Credit);

		Palette* pal = lpal_Res_Palette("colors");
		lpal_Set_Dest_Palette(pal);
		lpal_Set_Dest_Palette(t->scene_head->def_palette);

		Init_Credit_Info();

		lfade_Start_Full_Fade(FADE_WIPE_SNAP_ON, FADE_COLOR_TWO_PHASE, 1, 0, 1);

		lview_Set_View_Update_Function(end_View);
		lview_Disable_Global_View_Erase();
		ltimer_Set_Frame_Rate(12);

		/* Tag the scene for the HD compositor: bundle key
		 * (CREDITS, credits) resolves the remaster manifest at
		 * <root>/CREDITS/films/credits/. INCREMENTAL is the right
		 * cadence — draw_Credit's per-frame redraw inside
		 * credit_dirty_rect emits stars/bars/text records that
		 * persist on the RT (LOAD load_op); the stars emits in
		 * Credit_Stars_To_Back cover the previous frame's bars
		 * and text. SCENE_CREDITS_ALT shares the same bundle. */
		TieSnapshotBuilder_SetActiveFilm("CREDITS", "credits");

		/* Push the view modal task. The runner steps IT on
		 * subsequent ticks; we do not run again until it pops. */
		lviewadd_Push_Handle_View_Task();

		t->phase = CREDITS_PHASE_CLEANUP;
		return LANDRU_TASK_STEP_CONTINUE;
	}

	/* CLEANUP — view task popped; do teardown. */
	ltimer_Set_Frame_Rate(20);
	lview_Enable_Global_View_Erase();
	lview_Clear_View_Update_Function();

	free(star_buffer);
	star_buffer = NULL;
	lparagrp_Free_Paragraph(credit_text);
	lres_Close_Resource(t->text_res);
	lres_Close_Resource(t->credit_res);

	return LANDRU_TASK_STEP_DONE;
}

static const LandruTaskVtable credits_task_vt = {
	.step = credits_task_step,
};

void credits_Push_Credits_Task(SceneHeadStruct* scene_head) {
	CreditsTask* t = (CreditsTask*)landru_task_push(&credits_task_vt);
	if (!t)
		return;
	t->scene_head = scene_head;
	t->credit_res = NULL;
	t->text_res = NULL;
	t->phase = CREDITS_PHASE_BEGIN;
}
