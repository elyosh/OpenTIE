#include "tie/title.h"
#ifdef TIE_MODERN
#include "tie_runtime/runtime/profile.h"
#include "tie_runtime/runtime/title_task.h"
#endif
#include "landru/stream.h"
#include "landru/viewadd.h"
#include "tie/edition.h"
#include "tie/shell.h"
#include "tie/shellext.h"
#include "tie/tie.h"
#ifdef TIE_MODERN
#include "tie_runtime/snapshot/title.h"
#endif

#include "landru/actcust.h"
#include "landru/actdelt.h"
#include "landru/actor.h"
#include "landru/bitmap.h"
#include "landru/canvas.h"
#include "landru/cursor.h"
#include "landru/error.h"
#include "landru/fade.h"
#include "landru/font.h"
#include "landru/io.h"
#include "landru/paint.h"
#include "landru/pal.h"
#include "landru/paragrp.h"
#include "landru/rect.h"
#include "landru/res.h"
#include "landru/timer.h"
#include "landru/view.h"

#include "tie/slant.h"

#include <stdlib.h>
#include <string.h>

/* ---- Static data (from binary .data segment) ---- */

/* Perspective dither table — Y thresholds for scale-line rendering.
 * Each line is drawn only when its perspective Y exceeds the table entry. */
// GLOBAL: TIE95 0xCE3E4
// GLOBAL: TIE98 0x4F2E18
static const int16_t scale_table[20] = { 276, 116, 238, 38, 188, 132, 248, 70,  260, 100,
										 228, 54,  178, 22, 214, 148, 164, 202, 6,   86 };

/* ---- Static globals ---- */

enum {
	MAX_LINES = 18,
};

// GLOBAL: TIE95 0xF5108
// GLOBAL: TIE98 0x58A9C0
static int16_t line_drawn[MAX_LINES];
// GLOBAL: TIE95 0xF5058
// GLOBAL: TIE98 0x58AA48
BitmapStruct title_background;
// GLOBAL: TIE95 0xF506C
// GLOBAL: TIE98 0x58AA40
static Actor* starwars_actor;
// GLOBAL: TIE95 0xF5070
// GLOBAL: TIE98 0x58AA18
static int16_t buff_y[MAX_LINES];
// GLOBAL: TIE95 0xF5094
// GLOBAL: TIE98 0x58A994
static Actor* back_actor;
// GLOBAL: TIE95 0xF5098
// GLOBAL: TIE98 0x58A9F0
static int16_t line_y[MAX_LINES];
// GLOBAL: TIE95 0xF50BC
// GLOBAL: TIE98 0x58A970
static int16_t line_yf[MAX_LINES];
// GLOBAL: TIE95 0xF50E0
// GLOBAL: TIE98 0x58A998
static int16_t line_yv[MAX_LINES];
// GLOBAL: TIE95 0xF5104
// GLOBAL: TIE98 0x58A6AC
static Actor* title_actor;
// GLOBAL: TIE95 0xF512C
// GLOBAL: TIE98 0x58A6C0
static int16_t line_used[MAX_LINES];
// GLOBAL: TIE95 0xF5150
// GLOBAL: TIE98 0x58A6B4
static Actor* along_actor;
// GLOBAL: TIE95 0xF5154
// GLOBAL: TIE98 0x58A6E4
static Actor* stars_actor;
// GLOBAL: TIE95 0xF5158
// GLOBAL: TIE98 0x58A400
static int16_t line_yvf[MAX_LINES];
// GLOBAL: TIE95 0xF5180
// GLOBAL: TIE98 0x58A6E8
static int16_t scale_skipf[321];
// GLOBAL: TIE95 0xF5402
// GLOBAL: TIE98 0x58A428
static int16_t scale_skip[321];
// GLOBAL: TIE95 0xF5688
// GLOBAL: TIE98 0x58A96C
static int16_t film_time;
// GLOBAL: TIE95 0xF568A
// GLOBAL: TIE98 0x58A9BC
static int16_t base_color;
// GLOBAL: TIE95 0xF568C
// GLOBAL: TIE98 0x58A9E8
static int16_t scale_amount_f;
// GLOBAL: TIE95 0xF568E
// GLOBAL: TIE98 0x58A6B8
static int16_t scale_amount;
// GLOBAL: TIE95 0xF5690
// GLOBAL: TIE98 0x58AA3C
int16_t title_num_lines;
// GLOBAL: TIE95 0xF5684
// GLOBAL: TIE98 0x58AA44
LandruHandle title_text; /* paragraph data */
// GLOBAL: TIE95 0xF5686
// GLOBAL: TIE98 0x58A6B0
static int16_t title_font;

/* ================================================================
 * View update callback
 * ================================================================ */

// FUNCTION: TIE95 0x66B9C
// FUNCTION: TIE98 0x490340
static void title_end_View(int32_t time) {
	int16_t i;
	int16_t scene;
	Rect r;
	(void)time;

	/* Scene 8: check for exit at time 690 */
	if (shellext_Get_Cur_Scene() == SCENE_TITLE) {
		if (shellext_Check_Scene_Exit(&scene, 10, 100, film_time == TIE_FRONTEND_EDITION(690, 694)))
			xerror_Set_Landru_Exit(scene);
	}

	/* At time 100: reset view frame for the text crawl */
	if (film_time == TIE_FRONTEND_EDITION(100, 104)) {
		xrect_Set_Rect(&r, 0, 0, 320, 200);
		xview_Set_View_Frame(0, &r);
		xview_Set_View_Pos(0, r.left, r.top);
	}

	/* Fade out: increment base_color every other frame after time 620 */
	if (film_time >= TIE_FRONTEND_EDITION(620, 624) && (film_time & 1))
		base_color++;

	/* Time 689: clear all lines before loop */
	if (film_time == TIE_FRONTEND_EDITION(689, 693)) {
		for (i = 0; i < title_num_lines; i++)
			line_used[i] = 0;
	}

	/* Advance time; skip ahead on slow systems */
	if (++film_time == TIE_FRONTEND_EDITION(40, 44)) {
		if (xio_Is_System_Slower_Than(2))
			film_time = TIE_FRONTEND_EDITION(64, 68);
	}
}

/* ================================================================
 * Star Wars logo zoom callbacks
 * ================================================================ */

/* Normal speed: per-frame scale decrease with deceleration */
// FUNCTION: TIE95 0x66CB8
// FUNCTION: TIE98 0x490440
static void title_user_StarWars(Actor* actor, int32_t time) {
	(void)time;

	if (!film_time) {
		xactor_Hide_Actor(actor);
		return;
	}

	/* At time 84: capture palette and start fade to black */
	if (film_time == TIE_FRONTEND_EDITION(84, 88)) {
		xpal_Screen_To_Src_Palette(0, 0, 255);
		xpal_Screen_To_Dest_Palette(0, 0, 255);
		xpal_Set_Dest_Pal_Color(81, 96, 0, 0, 0);
		xfade_Start_Full_Fade(FADE_WIPE_SNAP_ON, FADE_COLOR_PAL_TO_PAL, 1, 0, 0);
	}

	/* At time 39: show and set initial scale */
	if (film_time == TIE_FRONTEND_EDITION(39, 43)) {
		xactor_Show_Actor(actor);
		xactor_Set_Actor_Scale(actor, 460, 460);
	}

	/* Decrease scale each frame */
	xactor_Set_Actor_Scale(actor, actor->xscale - scale_amount, actor->yscale - scale_amount);

	/* Clamp to minimum 1 */
	if (actor->xscale < 1)
		actor->xscale = 1;
	if (actor->yscale < 1)
		actor->yscale = 1;

	/* Decelerate scale speed between times 39-100 */
	if (film_time > TIE_FRONTEND_EDITION(39, 43) && film_time < TIE_FRONTEND_EDITION(100, 104)) {
		scale_amount_f += 48;
		if (scale_amount_f >= 256) {
			scale_amount_f -= 256;
			scale_amount--;
		}
	}

	/* At time 100: hide */
	if (film_time == TIE_FRONTEND_EDITION(100, 104))
		xactor_Hide_Actor(actor);
}

/* Slow system variant: static display, no per-frame scaling */
// FUNCTION: TIE95 0x66DFC
// FUNCTION: TIE98 0x490560
static void title_user_Slow_StarWars(Actor* actor, int32_t time) {
	(void)time;

	if (!film_time) {
		xactor_Hide_Actor(actor);
		return;
	}

	if (film_time == TIE_FRONTEND_EDITION(39, 43))
		xactor_Show_Actor(actor);

	if (film_time == TIE_FRONTEND_EDITION(84, 88)) {
		xpal_Screen_To_Src_Palette(0, 0, 255);
		xpal_Screen_To_Dest_Palette(0, 0, 255);
		xpal_Set_Dest_Pal_Color(81, 96, 0, 0, 0);
		xfade_Start_Full_Fade(FADE_WIPE_SNAP_OFF, FADE_COLOR_PAL_TO_PAL, 1, 0, 1);
		xactor_Hide_Actor(actor);
	}
}

/* ================================================================
 * Stars background callback
 * ================================================================ */

// FUNCTION: TIE95 0x66E80
// FUNCTION: TIE98 0x4905F0
static void title_user_Stars(Actor* actor, int32_t time) {
	(void)time;
	if (shellext_Get_Cur_Scene() != SCENE_TITLE)
		return;

	if (!film_time) {
		xactor_Hide_Actor(actor);
	} else {
		if (film_time == TIE_FRONTEND_EDITION(38, 42))
			xfade_Start_Full_Fade(FADE_WIPE_SNAP_ON, FADE_COLOR_TWO_PHASE, 1, 0, 1);
		if (film_time == TIE_FRONTEND_EDITION(39, 43))
			xactor_Show_Actor(actor);
	}
}

/* ================================================================
 * Text line animation
 * ================================================================ */

/* Per-frame: advance each active line's Y position with deceleration */
// FUNCTION: TIE95 0x66EE0
// FUNCTION: TIE98 0x490650
static void title_user_Title(Actor* actor, int32_t time) {
	int16_t i;
	(void)actor;
	(void)time;

	for (i = 0; i < title_num_lines; i++) {
		if (!line_used[i])
			continue;

		/* Accumulate fractional velocity */
		line_yf[i] += line_yvf[i];
		if (line_yf[i] >= 4096) {
			line_yf[i] -= 4096;
			line_y[i]--;
		}

		/* Move line upward */
		line_y[i] -= line_yv[i];

		/* Decelerate when line is on screen */
		if (line_y[i] < 200) {
			line_yvf[i] -= 16;
			if (line_yvf[i] < 0) {
				line_yvf[i] += 4096;
				if (line_yv[i] <= 0)
					line_used[i] = 0;
				else
					line_yv[i]--;
			}
		}
	}
}

/* Draw: render each active line with perspective horizontal scaling */
// FUNCTION: TIE95 0x66FD4
// FUNCTION: TIE98 0x490740
static int16_t title_draw_Title(Actor* actor, Rect* r, Rect* clip_r, int16_t off_x, int16_t off_y,
								int16_t refresh) {
	char* dataptr;
	int16_t i;
	(void)actor;
	(void)r;
	(void)clip_r;
	(void)off_x;
	(void)off_y;

	if (!refresh)
		return 1;

	dataptr = (char*)xbm_Lock_Bitmap(&title_background);

	for (i = 0; i < title_num_lines; i++) {
		int16_t y, by, yf, j;
		if (!line_used[i])
			continue;

		y = line_y[i];
		by = buff_y[i];
		yf = 2 * (y - 40);
		if (line_yf[i] >= 2048)
			yf--;

		for (j = 0; j < 20; j++) {
			if (scale_table[j] <= yf) {
				int16_t w = 320 - 2 * (200 - y);
				int16_t color = ((y - 40) >> 1) + 96 - base_color;
				if (color < 96)
					color = 96;

				if (y < 200 && w > 0) {
					int16_t x = 160 - (w >> 1);
					slant_Scale_Line(dataptr, 0, by, scale_skip[w], scale_skipf[w], x, y, w, (uint8_t)color);
				}
				y++;
			}
			by++;
		}
	}

	xbm_Unlock_Bitmap(&title_background);
	return 1;
}

/* ================================================================
 * Background text preparer
 * ================================================================ */

/* At time 0: initialize 18 text lines with staggered positions */
// FUNCTION: TIE95 0x67148
// FUNCTION: TIE98 0x490870
static void title_user_Back(Actor* actor, int32_t time) {
	int16_t start = TIE_FRONTEND_EDITION(100, 104);
	int16_t i;
	(void)actor;
	if (shellext_Get_Cur_Scene() == SCENE_TITLE) {
		if (xio_Is_System_Slower_Than(2))
			start = TIE_FRONTEND_EDITION(60, 64);
	} else {
		start = 1;
	}

	if (time)
		return;

	title_num_lines = MAX_LINES;
	for (i = 0; i < title_num_lines; i++) {
		buff_y[i] = 20 * (i % 10);
		line_y[i] = start + 28 * i + 200;
#ifdef TIE_MODERN
		TieTitleSnapshot_SetOrigin(i, line_y[i]);
#endif
		line_yf[i] = 0;
		line_yv[i] = 1;
		line_yvf[i] = 0;
		line_used[i] = 1;
		line_drawn[i] = 0;
	}
}

/* Draw: render text lines into background bitmap as they come into view */
// FUNCTION: TIE95 0x671FC
// FUNCTION: TIE98 0x490940
static int16_t title_draw_Back(Actor* actor, Rect* r, Rect* clip_r, int16_t off_x, int16_t off_y,
							   int16_t refresh) {
	int16_t i;
	(void)actor;
	(void)r;
	(void)clip_r;
	(void)off_x;
	(void)off_y;

	if (!refresh)
		return 1;

	xcanvas_Push_Canvas(&title_background);

	for (i = 0; i < title_num_lines; i++) {
		if (!line_drawn[i] && line_y[i] <= 200) {
			Rect tr;
			char string[64];
			xrect_Set_Rect(&tr, 0, buff_y[i], 320, buff_y[i] + 20);
			xpaint_Paint_Clipped_Rect(&tr, 0);

			xparagrp_Get_Paragraph_String(title_text, string, 0, i);
			xfont_Print_Centered_Text(string, &tr, title_font, 15);
			line_drawn[i] = 1;
		}
	}

	xcanvas_Pop_Canvas();
	return 1;
}

// FUNCTION: TIE95 0x668A0
// FUNCTION: TIE98 0x48FF80
int16_t title_Title(SceneHeadStruct* scene_head) {
	ResFile* resource;
	char film_name[16];
	Rect frame;
	Palette* pal;
	int16_t i;

	/* Load resources */
	resource = shellext_Open_Empire_Resource("title.lfd");
#ifdef TIE_MODERN
	if (!resource) {
		TieTitle_RunView(resource, NULL, false);
		return 0;
	}
#endif

	if (shellext_Get_Cur_Scene() == SCENE_TITLE) {
		strcpy(film_name, "title");
	} else {
		strcpy(film_name, "todtxt1");
		film_name[6] = pilot_record.cur_battle + '1';
	}
	title_text = xparagrp_Res_Paragraph(resource, film_name);
#ifdef TIE_MODERN
	if (!title_text) {
		TieTitle_RunView(resource, NULL, false);
		return 0;
	}
#endif

	/* Load font */
	/* TIE98 0x490067/0x4909E6: retain slot 2 for the
	 * SVGA frontend font and place the VGA title font in slot 4. */
#ifdef TIE_MODERN
	title_font = TieProfile_UsesTie98Frontend() ? 4 : 2;
#elif defined(TIE98)
	title_font = 2;
#else
	title_font = 2;
#endif
#if defined(TIE98) && !defined(TIE_MODERN)
	xfont_Res_Font("helv-20", 4);
#else
	xfont_Res_Font("helv-20", (uint16_t)title_font);
#endif

	/* Initialize state */
	base_color = 0;
	scale_amount = 12;
	scale_amount_f = 0;
	film_time = (shellext_Get_Cur_Scene() == SCENE_TITLE) ? 0 : TIE_FRONTEND_EDITION(99, 103);

	/* Build scale lookup tables */
	for (i = 0; i <= 320; i++) {
		if (i) {
			scale_skip[i] = 320 / i - 1;
			scale_skipf[i] = (int16_t)(((320 % i) << 16) / i);
		} else {
			scale_skip[0] = 0;
			scale_skipf[0] = 0;
		}
	}

	/* Allocate background bitmap */
	xrect_Set_Rect(&frame, 0, 0, 320, 200);
	xbm_Init_Bitmap(&title_background);
#ifdef TIE_MODERN
	if (!xbm_Alloc_Bitmap(&title_background, 320, 200)) {
		TieTitle_RunView(resource, NULL, false);
		return 0;
	}
#else
	xbm_Alloc_Bitmap(&title_background, 320, 200);
#endif

	/* Create actors (scene 8 only: along + starwars) */
	if (shellext_Get_Cur_Scene() == SCENE_TITLE) {
		along_actor = xactdelt_Res_Delta_Actor("along", &frame, 0, 0, 20);
#ifdef TIE_MODERN
		if (!along_actor) {
			TieTitle_RunView(resource, NULL, false);
			return 0;
		}
#endif
#if defined(TIE98) && !defined(TIE_MODERN)
		xactor_Set_Actor_Time(along_actor, 0, 42);
#else
		xactor_Set_Actor_Time(along_actor, 0, 38);
#endif

		starwars_actor = xactdelt_Res_Delta_Actor("starwars", &frame, 30, 32, 20);
#ifdef TIE_MODERN
		if (!starwars_actor) {
			TieTitle_RunView(resource, NULL, false);
			return 0;
		}
#endif
		if (xio_Is_System_Slower_Than(2))
			xactor_Set_Actor_User_Function(starwars_actor, title_user_Slow_StarWars);
		else
			xactor_Set_Actor_User_Function(starwars_actor, title_user_StarWars);
	}

	stars_actor = xactdelt_Res_Delta_Actor("stars", &frame, 0, 0, 100);
#ifdef TIE_MODERN
	if (!stars_actor) {
		TieTitle_RunView(resource, NULL, false);
		return 0;
	}
#endif
	xactor_Set_Actor_User_Function(stars_actor, title_user_Stars);

	back_actor = xactcust_Alloc_Custom_Actor(LANDRU_NULL_HANDLE, &frame, 0, 0, 10);
#ifdef TIE_MODERN
	if (!back_actor) {
		TieTitle_RunView(resource, NULL, false);
		return 0;
	}
#endif
	xactor_Set_Actor_User_Function(back_actor, title_user_Back);
	xactor_Set_Actor_Draw_Function(back_actor, title_draw_Back);

	title_actor = xactcust_Alloc_Custom_Actor(LANDRU_NULL_HANDLE, &frame, 0, 0, 0);
#ifdef TIE_MODERN
	if (!title_actor) {
		TieTitle_RunView(resource, NULL, false);
		return 0;
	}
#endif
	xactor_Set_Actor_User_Function(title_actor, title_user_Title);
	xactor_Set_Actor_Draw_Function(title_actor, title_draw_Title);

	/* Set palettes */
	pal = xpal_Res_Palette("title");
#ifdef TIE_MODERN
	if (!pal) {
		TieTitle_RunView(resource, NULL, false);
		return 0;
	}
#endif
	xpal_Set_Dest_Palette(pal);
	xpal_Set_Dest_Palette(scene_head->def_palette);

	/* Start fade and push the modal view task */
	xfade_Start_Full_Fade(FADE_WIPE_SNAP_ON, FADE_COLOR_TWO_PHASE, 1, 0, 1);
	xview_Set_View_Update_Function(title_end_View);
	xview_Disable_Global_View_Erase();
#ifdef TIE_MODERN
	TieTitle_RunView(resource, film_name, true);
	return 0;
#else
#ifdef TIE95
	if (install_cfg_mode <= 1)
		xstream_Chain_Stream_File(0, "\\astream\\os1-v3.wrk");
	else if (install_cfg_mode == 2)
		xstream_Chain_Stream_File(0, "astream\\os1-v3.wrk");
#endif
	shellext_Handle_TIE_View();
	xview_Enable_Global_View_Erase();
	xview_Clear_View_Update_Function();
	xbm_Free_Bitmap(&title_background);
	xparagrp_Free_Paragraph(title_text);
	xres_Close_Resource(resource);
	xcanvas_Get_Drawing_Canvas_Bounds(&frame);
	xview_Set_View_Frame(0, &frame);
	xview_Set_View_Pos(0, 0, 0);
	return xerror_Get_Landru_Exit();
#endif
}
