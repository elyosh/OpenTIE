#include "tie/title.h"
#include "landru/viewadd.h"
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
static const int16_t scale_table[20] = { 276, 116, 238, 38, 188, 132, 248, 70,  260, 100,
										 228, 54,  178, 22, 214, 148, 164, 202, 6,   86 };

/* ---- Static globals ---- */

enum {
	MAX_LINES = 18,
};

static int16_t line_drawn[MAX_LINES];
static BitmapStruct background;
static Actor* starwars_actor;
static int16_t buff_y[MAX_LINES];
static Actor* back_actor;
static int16_t line_y[MAX_LINES];
static int16_t line_yf[MAX_LINES];
static int16_t line_yv[MAX_LINES];
// GLOBAL: TIE95 0xF5104
static Actor* title_actor;
static int16_t line_used[MAX_LINES];
static Actor* along_actor;
static Actor* stars_actor;
static int16_t line_yvf[MAX_LINES];
static Actor* smallsw_actor;
static int16_t scale_skipf[321];
static int16_t scale_skip[321];
static int16_t film_time;
static int16_t base_color;
static int16_t scale_amount_f;
static int16_t scale_amount;
int16_t title_num_lines;
LandruHandle title_text; /* paragraph data */
static int16_t title_font;

/* ================================================================
 * View update callback
 * ================================================================ */

static void end_View(int32_t time) {
	int16_t i;
	(void)time;

	/* Scene 8: check for exit at time 690 */
	if (shellext_Get_Cur_Scene() == SCENE_TITLE) {
		int16_t scene;
		if (shellext_Check_Scene_Exit(&scene, 10, 100, film_time == 690))
			xerror_Set_Landru_Exit(scene);
	}

	/* At time 100: reset view frame for the text crawl */
	if (film_time == 100) {
		Rect r;
		xrect_Set_Rect(&r, 0, 0, 320, 200);
		xview_Set_View_Frame(0, &r);
		xview_Set_View_Pos(0, r.left, r.top);
	}

	/* Fade out: increment base_color every other frame after time 620 */
	if (film_time >= 620 && (film_time & 1))
		base_color++;

	/* Time 689: clear all lines before loop */
	if (film_time == 689) {
		for (i = 0; i < title_num_lines; i++)
			line_used[i] = 0;
	}

	/* Advance time; skip ahead on slow systems */
	if (++film_time == 40) {
		if (xio_Is_System_Slower_Than(2))
			film_time = 64;
	}
}

/* ================================================================
 * Star Wars logo zoom callbacks
 * ================================================================ */

/* Normal speed: per-frame scale decrease with deceleration */
static void user_StarWars(Actor* actor, int32_t time) {
	(void)time;

	if (!film_time) {
		xactor_Hide_Actor(actor);
		return;
	}

	/* At time 84: capture palette and start fade to black */
	if (film_time == 84) {
		xpal_Screen_To_Src_Palette(0, 0, 255);
		xpal_Screen_To_Dest_Palette(0, 0, 255);
		xpal_Set_Dest_Pal_Color(81, 96, 0, 0, 0);
		xfade_Start_Full_Fade(FADE_WIPE_SNAP_ON, FADE_COLOR_PAL_TO_PAL, 1, 0, 0);
	}

	/* At time 39: show and set initial scale */
	if (film_time == 39) {
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
	if (film_time > 39 && film_time < 100) {
		scale_amount_f += 48;
		if (scale_amount_f >= 256) {
			scale_amount_f -= 256;
			scale_amount--;
		}
	}

	/* At time 100: hide */
	if (film_time == 100)
		xactor_Hide_Actor(actor);
}

/* Slow system variant: static display, no per-frame scaling */
static void user_Slow_StarWars(Actor* actor, int32_t time) {
	(void)time;

	if (!film_time) {
		xactor_Hide_Actor(actor);
		return;
	}

	if (film_time == 39)
		xactor_Show_Actor(actor);

	if (film_time == 84) {
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

static void user_Stars(Actor* actor, int32_t time) {
	(void)time;
	if (shellext_Get_Cur_Scene() != SCENE_TITLE)
		return;

	if (!film_time) {
		xactor_Hide_Actor(actor);
	} else {
		if (film_time == 38)
			xfade_Start_Full_Fade(FADE_WIPE_SNAP_ON, FADE_COLOR_TWO_PHASE, 1, 0, 1);
		if (film_time == 39)
			xactor_Show_Actor(actor);
	}
}

/* ================================================================
 * Text line animation
 * ================================================================ */

/* Per-frame: advance each active line's Y position with deceleration */
static void user_Title(Actor* actor, int32_t time) {
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
static int16_t draw_Title(Actor* actor, Rect* r, Rect* clip_r, int16_t off_x, int16_t off_y,
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

	dataptr = (char*)xbitmap_Lock_Bitmap(&background);

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

	xbitmap_Unlock_Bitmap(&background);
	return 1;
}

/* ================================================================
 * Background text preparer
 * ================================================================ */

/* At time 0: initialize 18 text lines with staggered positions */
static void user_Back(Actor* actor, int32_t time) {
	int16_t start = 100;
	int16_t i;
	(void)actor;
	if (shellext_Get_Cur_Scene() == SCENE_TITLE) {
		if (xio_Is_System_Slower_Than(2))
			start = 60;
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
static int16_t draw_Back(Actor* actor, Rect* r, Rect* clip_r, int16_t off_x, int16_t off_y, int16_t refresh) {
	int16_t i;
	(void)actor;
	(void)r;
	(void)clip_r;
	(void)off_x;
	(void)off_y;

	if (!refresh)
		return 1;

	xcanvas_Push_Canvas(&background);

	for (i = 0; i < title_num_lines; i++) {
		if (!line_drawn[i] && line_y[i] <= 200) {
			Rect tr;
			char string[64];
			xrect_Set_Rect(&tr, 0, buff_y[i], 320, buff_y[i] + 20);
			xpaint_Paint_Clipped_Rect(&tr, 0);

			xparagrp_Get_Paragraph_String(title_text, string, 0, i);
			xfont_Print_Centered_Text(string, &tr, 15, title_font);
			line_drawn[i] = 1;
		}
	}

	xcanvas_Pop_Canvas();
	return 1;
}

/* The native task owns the view wait, stream selection, and snapshot tags. */
int16_t title_OpenScene(SceneHeadStruct* scene_head, TitleSceneResources* resources, int16_t font_slot) {
	Rect frame;
	Palette* pal;
	int16_t i;

	/* Load resources */
	resources->file = shellext_Open_Empire_Resource("title.lfd");
	if (!resources->file)
		return 0;

	if (shellext_Get_Cur_Scene() == SCENE_TITLE) {
		strcpy(resources->film_name, "title");
	} else {
		strcpy(resources->film_name, "todtxt1");
		resources->film_name[6] = pilot_record.cur_battle + '1';
	}
	title_text = xparagrp_Res_Paragraph(resources->file, resources->film_name);
	if (!title_text)
		return 0;

	/* Load font */
	/* TIE98 0x490067/0x4909E6: retain slot 2 for the
	 * SVGA frontend font and place the VGA title font in slot 4. */
	title_font = font_slot;
	xfont_Res_Font("helv-20", (uint16_t)title_font);

	/* Initialize state */
	base_color = 0;
	scale_amount = 12;
	scale_amount_f = 0;
	film_time = (shellext_Get_Cur_Scene() == SCENE_TITLE) ? 0 : 99;

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
	xbitmap_Init_Bitmap(&background);
	if (!xbitmap_Alloc_Bitmap(&background, 320, 200))
		return 0;

	/* Create actors (scene 8 only: along + starwars) */
	if (shellext_Get_Cur_Scene() == SCENE_TITLE) {
		along_actor = xactdelt_Res_Delta_Actor("along", &frame, 0, 0, 20);
		if (!along_actor)
			return 0;
		xactor_Set_Actor_Time(along_actor, 0, 38);

		starwars_actor = xactdelt_Res_Delta_Actor("starwars", &frame, 30, 32, 20);
		if (!starwars_actor)
			return 0;
		if (xio_Is_System_Slower_Than(2))
			xactor_Set_Actor_User_Function(starwars_actor, user_Slow_StarWars);
		else
			xactor_Set_Actor_User_Function(starwars_actor, user_StarWars);
	}

	stars_actor = xactdelt_Res_Delta_Actor("stars", &frame, 0, 0, 100);
	if (!stars_actor)
		return 0;
	xactor_Set_Actor_User_Function(stars_actor, user_Stars);

	back_actor = xactcust_Alloc_Custom_Actor(LANDRU_NULL_HANDLE, &frame, 0, 0, 10);
	if (!back_actor)
		return 0;
	xactor_Set_Actor_User_Function(back_actor, user_Back);
	xactor_Set_Actor_Draw_Function(back_actor, draw_Back);

	title_actor = xactcust_Alloc_Custom_Actor(LANDRU_NULL_HANDLE, &frame, 0, 0, 0);
	if (!title_actor)
		return 0;
	xactor_Set_Actor_User_Function(title_actor, user_Title);
	xactor_Set_Actor_Draw_Function(title_actor, draw_Title);

	/* Set palettes */
	pal = xpal_Res_Palette("title");
	if (!pal)
		return 0;
	xpal_Set_Dest_Palette(pal);
	xpal_Set_Dest_Palette(scene_head->def_palette);

	/* Start fade and push the modal view task */
	xfade_Start_Full_Fade(FADE_WIPE_SNAP_ON, FADE_COLOR_TWO_PHASE, 1, 0, 1);
	xview_Set_View_Update_Function(end_View);
	xview_Disable_Global_View_Erase();
	return 1;
}

void title_CloseScene(TitleSceneResources* resources) {
	Rect frame;
	xview_Enable_Global_View_Erase();
	xview_Clear_View_Update_Function();
	xbitmap_Free_Bitmap(&background);
	xparagrp_Free_Paragraph(title_text);
	title_text = LANDRU_NULL_HANDLE;
	title_num_lines = 0;
	if (resources->file)
		xres_Close_Resource(resources->file);
	resources->file = NULL;
	xcanvas_Get_Drawing_Canvas_Bounds(&frame);
	xview_Set_View_Frame(0, &frame);
	xview_Set_View_Pos(0, 0, 0);
}
