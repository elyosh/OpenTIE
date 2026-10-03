#include "tie/credits.h"
#ifdef TIE_MODERN
#include "tie_runtime/runtime/credits_task.h"
#endif
#include "tie/edition.h"
#include "tie/shellext.h"
#include "tie/shipext.h"

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

// GLOBAL: TIE95 0xf5de8
// GLOBAL: TIE98 0x50f7a8
Rect credits_dirty_rect;
// GLOBAL: TIE95 0xf5df0
// GLOBAL: TIE98 0x50f7b4
Actor* credits_red_bar;
// GLOBAL: TIE95 0xf5df4
// GLOBAL: TIE98 0x50f7d4
Actor* credits_blue_bar;
// GLOBAL: TIE95 0xf5df8
// GLOBAL: TIE98 0x50f7d8
static Actor* credits_actor; /* custom draw actor for the credits */
// GLOBAL: TIE95 0xf5dfc
// GLOBAL: TIE98 0x50f7b8
static Actor* credits_stars_actor;
// GLOBAL: TIE95 0xf5e00
// GLOBAL: TIE98 0x50f7a0
static int16_t credits_next_scene;
// GLOBAL: TIE95 0xf5e02
// GLOBAL: TIE98 0x50f7b0
int16_t credits_text_len; /* hold duration per credit (130) */
// GLOBAL: TIE95 0xf5e04
// GLOBAL: TIE98 0x50f7bc
int16_t credits_film_time; /* current frame counter */
// GLOBAL: TIE95 0xf5e06
// GLOBAL: TIE98 0x50f7c8
LandruHandle credits_star_buffer; /* 320x100 star pixel cache */
// GLOBAL: TIE95 0xf5e08
// GLOBAL: TIE98 0x50f7c0
int16_t credits_num_credit_lines; /* paragraph count in credit text */
// GLOBAL: TIE95 0xf5e0a
// GLOBAL: TIE98 0x50f7cc
int16_t credits_film_len; /* total animation length */
// GLOBAL: TIE95 0xf5e0c
// GLOBAL: TIE98 0x50f7d0
LandruHandle credits_text; /* paragraph data from tietext0.lfd */

static void credits_end_View(int32_t frame_num);
static void credits_Init_Credit_Info(void);
void credits_Credit_Stars_To_Back(void);
static int16_t credits_draw_Credit(Actor* actor, Rect* bounds, Rect* clip, int16_t xoff, int16_t yoff,
								   int16_t refresh);
static void credits_Credit_Actor_To_Buffer(Actor* actor, LandruHandle buffer);

/* ================================================================
 * Entry point
 * ================================================================ */

/* The original blocking scene is split at its modal-view call. */
// FUNCTION: TIE95 0x71090
// FUNCTION: TIE98 0x414620
int credits_Credits(SceneHeadStruct* scene_head) {
	ResFile* credit_res;
#ifdef TIE_MODERN
	ResFile* text_res = NULL;
#else
	ResFile* text_res;
#endif
	Rect r;
	Palette* pal;

	if (shellext_Get_Cur_Scene() == SCENE_CREDITS)
		credits_next_scene = SCENE_REGISTER;
	else
		credits_next_scene = shipext_Next_Battle_Cutscene();

	credit_res = shellext_Open_Empire_Resource("credits.lfd");
#ifdef TIE_MODERN
	if (!credit_res) {
		TieCredits_RunView(credit_res, text_res, false);
		return 0;
	}
#endif
	text_res = shellext_Open_Empire_Resource("tietext0.lfd");
#ifdef TIE_MODERN
	if (!text_res) {
		TieCredits_RunView(credit_res, text_res, false);
		return 0;
	}
#endif
	credits_text = xparagrp_Res_Paragraph(text_res, "credits");
#ifdef TIE_MODERN
	if (!credits_text) {
		TieCredits_RunView(credit_res, text_res, false);
		return 0;
	}
#endif

	xrect_Set_Rect(&r, 0, 0, 320, 200);
	credits_star_buffer = xmemhdl_Alloc_Clear_Handle(32000, LANDRU_MEMORY_RESOURCE);
#ifdef TIE_MODERN
	if (!credits_star_buffer) {
		TieCredits_RunView(credit_res, text_res, false);
		return 0;
	}
#endif
	credits_stars_actor = xactdelt_Res_Delta_Actor("stars", &r, 0, 0, 100);
#ifdef TIE_MODERN
	if (!credits_stars_actor) {
		TieCredits_RunView(credit_res, text_res, false);
		return 0;
	}
#endif
	xactor_Set_Actor_Time(credits_stars_actor, 0, 0);
	credits_red_bar = xactdelt_Res_Delta_Actor("redbar", &r, 0, 0, 100);
#ifdef TIE_MODERN
	if (!credits_red_bar) {
		TieCredits_RunView(credit_res, text_res, false);
		return 0;
	}
#endif
	xactor_Set_Actor_Time(credits_red_bar, 0, 0);
	credits_blue_bar = xactdelt_Res_Delta_Actor("bluebar", &r, 0, 0, 100);
#ifdef TIE_MODERN
	if (!credits_blue_bar) {
		TieCredits_RunView(credit_res, text_res, false);
		return 0;
	}
#endif
	xactor_Set_Actor_Time(credits_blue_bar, 0, 0);
	credits_Credit_Actor_To_Buffer(credits_stars_actor, credits_star_buffer);

	credits_actor = xactcust_Alloc_Custom_Actor(LANDRU_NULL_HANDLE, &r, 0, 0, 0);
#ifdef TIE_MODERN
	if (!credits_actor) {
		TieCredits_RunView(credit_res, text_res, false);
		return 0;
	}
#endif
	xactor_Set_Actor_Draw_Function(credits_actor, credits_draw_Credit);
	pal = xpal_Res_Palette("colors");
#ifdef TIE_MODERN
	if (!pal) {
		TieCredits_RunView(credit_res, text_res, false);
		return 0;
	}
#endif
	xpal_Set_Dest_Palette(pal);
	xpal_Set_Dest_Palette(scene_head->def_palette);

	credits_Init_Credit_Info();
	xfade_Start_Full_Fade(FADE_WIPE_SNAP_ON, FADE_COLOR_TWO_PHASE, 1, 0, 1);
	xview_Set_View_Update_Function(credits_end_View);
	xview_Disable_Global_View_Erase();
	xtimer_Set_Frame_Rate(12);

#ifdef TIE_MODERN
	TieCredits_RunView(credit_res, text_res, true);
	return 0;
#else
	shellext_Handle_TIE_View();
	xtimer_Set_Frame_Rate(20);
	xview_Enable_Global_View_Erase();
	xview_Clear_View_Update_Function();
	xmemhdl_Free_Handle(credits_star_buffer);
	xparagrp_Free_Paragraph(credits_text);
	xres_Close_Resource(text_res);
	xres_Close_Resource(credit_res);
	return xerror_Get_Landru_Exit();
#endif
}

/* ================================================================
 * View update callback
 * ================================================================ */

// FUNCTION: TIE95 0x71244
// FUNCTION: TIE98 0x414850
static void credits_end_View(int32_t frame_num) {
	int16_t exit_id;
	int16_t done = credits_film_time == credits_film_len;
	(void)frame_num;
	if (shellext_Check_Scene_Exit(&exit_id, credits_next_scene, credits_next_scene, done))
		xerror_Set_Landru_Exit(exit_id);
	credits_film_time++;
}

/* Initialize credit display state: dirty rect, paragraph count,
 * text hold duration, total film length. */
// FUNCTION: TIE95 0x71290
// FUNCTION: TIE98 0x4148A0
static void credits_Init_Credit_Info(void) {
	xrect_Set_Rect(&credits_dirty_rect, TIE_FRONTEND_EDITION(40, 20), 40, TIE_FRONTEND_EDITION(280, 300),
				   160);
	credits_num_credit_lines = xparagrp_Count_Paragraphs(credits_text);
	credits_text_len = 130;
	credits_film_time = 0;
	credits_film_len = 90 * (credits_num_credit_lines - 1) + 138;
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
 * base_y = credits_film_time + 96, decremented by 90 per paragraph.
 * color = palette index for the text (ramps 167..239 range).
 * text_y = vertical position for text block. */
// FUNCTION: TIE95 0x712EC
// FUNCTION: TIE98 0x414900
static int16_t credits_draw_Credit(Actor* actor, Rect* bounds, Rect* clip, int16_t xoff, int16_t yoff,
								   int16_t refresh) {
	int16_t time_offset;
	int16_t credit_idx;
	Rect rect;
	Rect saved_clip;

	(void)actor;
	(void)xoff;
	(void)yoff;

	if (!refresh)
		return 1;

	/* Film ended: black screen */
	if (credits_film_len <= credits_film_time) {
		xrect_Set_Rect(&rect, 0, 0, 320, 200);
		xpaint_Paint_Clipped_Rect(&rect, 0);
		xcanvas_Invalid_Screen_Diff();
		xdirty_Max_Dirty_List();
		return 1;
	}

	/* Draw tiled star background */
	credits_Credit_Stars_To_Back();

	time_offset = credits_film_time;

	/* Walk through each credit paragraph */
	for (credit_idx = 0; credit_idx < credits_num_credit_lines; credit_idx++) {
		if (time_offset < credits_text_len) {
			/* This paragraph is visible */
			int16_t color;
			int16_t text_y;
			int16_t num_strings;
			int16_t bar_time;
			int16_t i;

			if (time_offset < 0)
				break;

			if (time_offset < 60) {
				/* Fade-in phase */
				text_y = 140 - time_offset;
				color = time_offset + 96;
			} else {
				int16_t hold_time = time_offset - 60;
				if (hold_time < 40) {
					/* Hold phase */
					text_y = 80;
					if (hold_time < 31)
						color = time_offset + 96;
					else if (hold_time < 24)
						color = 191;
					else
						color = hold_time + 167;
				} else {
					/* Fade-out phase */
					hold_time -= 40;
					text_y = 80 - hold_time;
					color = hold_time + 207;
					if (color > 239)
						color = 239;
				}
			}

			num_strings = xparagrp_Count_Paragraph_Strings(credits_text, credit_idx);

			/* Draw red/blue bar decorations during hold phase */
			bar_time = time_offset - 45;
			if (bar_time >= 0) {
				int16_t bar_width;

				if (bar_time < 35) {
					bar_width = bar_time * 8;
				} else {
					bar_time -= 35;
					if (bar_time < 0)
						bar_width = 280;
					else
						bar_width = (bar_time + 35) * 8;
				}

				if (bar_width != -1) {
					xactdelt_Draw_Delta_Actor(credits_red_bar, bounds, clip, bar_width - 240, 79, refresh);

					xcanvas_Get_Drawing_Canvas_Clip(&saved_clip);

					xrect_Set_Rect(&rect, 0, 0, 320, 10 * (num_strings - 1) + 93);
					xrect_Clip_Rect(&rect, clip);
					xcanvas_Set_Drawing_Canvas_Clip(&rect);

					xactdelt_Draw_Delta_Actor(credits_blue_bar, &rect, &rect, 320 - bar_width, 91, refresh);

					xcanvas_Set_Drawing_Canvas_Clip(&saved_clip);
				}
			}

			/* Draw text lines */
			xrect_Set_Rect(&rect, 0, text_y, 320, text_y + 10);

			for (i = 0; i < num_strings; i++) {
				char line_buf[80];
				xparagrp_Get_Paragraph_String(credits_text, line_buf, credit_idx, i);
				xfont_Print_Centered_Text(line_buf, &rect, 0, color);

				if (i == 0)
					xrect_Offset_Rect(&rect, 0, 12);
				else
					xrect_Offset_Rect(&rect, 0, 10);
			}
		}

		/* Advance to next paragraph */
		time_offset -= 90;
	}

	if (TIE_FRONTEND_TIE98)
		xdirty_Max_Dirty_List();
	else
		xdirty_Dirty_Rect(&credits_dirty_rect);
	return 1;
}

/* ================================================================
 * Helpers
 * ================================================================ */

/* Tile the cached star image over the 320x200 credits background. */
// FUNCTION: TIE95 0x715C0
// FUNCTION: TIE98 0x414BE0
void credits_Credit_Stars_To_Back(void) {
	Rect r;
	xrect_Set_Rect(&r, 0, 0, 320, 100);
	stub_Copy_From_Clipped_Buffer(credits_star_buffer, &r, 0, 0, 320, 100);
	stub_Copy_From_Clipped_Buffer(credits_star_buffer, &r, 0, 100, 320, 100);
#ifdef TIE_MODERN
	/* Snapshot capture mirrors the two buffer copies without drawing again. */
	xactor_emit_draw(credits_stars_actor, 0, 0);
	xactor_emit_draw(credits_stars_actor, 0, 100);
#endif
}

/* Render a star actor into the 320x100 star buffer. Fills with black,
 * calls the actor's draw, copies canvas to buffer. */
// FUNCTION: TIE95 0x7161C
// FUNCTION: TIE98 0x414C40
static void credits_Credit_Actor_To_Buffer(Actor* actor, LandruHandle buffer) {
	Rect r;
	xrect_Set_Rect(&r, 0, 0, 320, 100);
	if (actor->draw) {
		xpaint_Paint_Clipped_Rect(&r, 0);
		actor->draw(actor, &r, &r, actor->x, actor->y, 1);
	}
	stub_Copy_To_Clipped_Buffer(buffer, &r, 0, 0, 320, 100);
}
