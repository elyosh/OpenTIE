#include <stdlib.h>
#include <string.h>

#include "landru/viewadd.h"
#include "tie/shellext.h"
#include "tie/tie.h"
#include "tie/title.h"
#include "tie_runtime/audio/config.h"
#include "tie_runtime/diagnostics/diagnostics.h"
#include "tie_runtime/display/classic_display.h"
#include "tie_runtime/display/classic_framebuffer.h"
#include "tie_runtime/flight_assets/model_types.h"
#include "tie_runtime/input/input.h"
#include "tie_runtime/runtime/exports.h"
#include "tie_runtime/runtime/profile.h"
#include "tie_runtime/snapshot/capture_views.h"
#include "tie_runtime/snapshot/snapshot.h"
#include "tie_runtime/snapshot/snapshot_internal.h"
#include "tie_runtime/storage/storage.h"
#include <landru/task.h>

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
#include "landru/stream.h"
#include "landru/timer.h"
#include "landru/view.h"

#include "tie/slant.h"

/* ---- Static data (from binary .data segment) ---- */

/* Perspective dither table — Y thresholds for scale-line rendering.
 * Each line is drawn only when its perspective Y exceeds the table entry. */
static const int16_t scale_table[20] = { 276, 116, 238, 38, 188, 132, 248, 70,  260, 100,
										 228, 54,  178, 22, 214, 148, 164, 202, 6,   86 };

/* Resource name strings */
static const char title_str[7][14] = { "title.lfd", "helv-20", "along",  "starwars",
									   "stars",     "title",   "todtxt1" };

/* ---- Static globals ---- */

#define MAX_LINES 18

static int16_t line_drawn[MAX_LINES];
static BitmapStruct background;
static Actor* starwars_actor;
static int16_t buff_y[MAX_LINES];
static Actor* back_actor;
static int16_t line_y[MAX_LINES];
static int16_t line_yf[MAX_LINES];
static int16_t line_yv[MAX_LINES];
// GLOBAL: TIE 0xF5968
static Actor* title_actor;
static int16_t line_used[MAX_LINES];
static Actor* along_actor;
static Actor* stars_actor;
static int16_t line_yvf[MAX_LINES];
/* Snapshot of each line's INITIAL y captured at scene start. The HD
 * application needs the constant per-line origin to drive its own time-
 * based scroll independent of tie_core's per-frame state; we save it
 * here in user_Back when the engine populates line_y[]. */
static int16_t line_y_initial_arr[MAX_LINES];
static Actor* smallsw_actor;
static int16_t scale_skipf[321];
static int16_t scale_skip[321];
static int16_t film_time;
static int16_t base_color;
static int16_t scale_amount_f;
static int16_t scale_amount;
static int16_t num_lines;
static void* title_text; /* paragraph data */
static int16_t title_font;

/* ================================================================
 * View update callback
 * ================================================================ */

static void end_View(int32_t time) {
	(void)time;

	/* Scene 8: check for exit at time 690 */
	if (shellext_Get_Cur_Scene() == SCENE_TITLE) {
		int16_t scene;
		if (shellext_Check_Scene_Exit(&scene, 10, 100, film_time == 690))
			lerror_Set_Landru_Exit(scene);
	}

	/* At time 100: reset view frame for the text crawl */
	if (film_time == 100) {
		Rect r;
		lrect_Set_Rect(&r, 0, 0, 320, 200);
		lview_Set_View_Frame(0, &r);
		lview_Set_View_Pos(0, r.left, r.top);
	}

	/* Fade out: increment base_color every other frame after time 620 */
	if (film_time >= 620 && (film_time & 1))
		base_color++;

	/* Time 689: clear all lines before loop */
	if (film_time == 689) {
		for (int16_t i = 0; i < num_lines; i++)
			line_used[i] = 0;
	}

	/* Advance time; skip ahead on slow systems */
	if (++film_time == 40) {
		if (lio_Is_System_Slower_Than(2))
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
		lactor_Hide_Actor(actor);
		return;
	}

	/* At time 84: capture palette and start fade to black */
	if (film_time == 84) {
		lpal_Screen_To_Src_Palette(0, 0, 255);
		lpal_Screen_To_Dest_Palette(0, 0, 255);
		lpal_Set_Dest_Pal_Color(81, 96, 0, 0, 0);
		lfade_Start_Full_Fade(FADE_WIPE_SNAP_ON, FADE_COLOR_PAL_TO_PAL, 1, 0, 0);
	}

	/* At time 39: show and set initial scale */
	if (film_time == 39) {
		lactor_Show_Actor(actor);
		lactor_Set_Actor_Scale(actor, 460, 460);
	}

	/* Decrease scale each frame */
	lactor_Set_Actor_Scale(actor, actor->xscale - scale_amount, actor->yscale - scale_amount);

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
		lactor_Hide_Actor(actor);
}

/* Slow system variant: static display, no per-frame scaling */
static void user_Slow_StarWars(Actor* actor, int32_t time) {
	(void)time;

	if (!film_time) {
		lactor_Hide_Actor(actor);
		return;
	}

	if (film_time == 39)
		lactor_Show_Actor(actor);

	if (film_time == 84) {
		lpal_Screen_To_Src_Palette(0, 0, 255);
		lpal_Screen_To_Dest_Palette(0, 0, 255);
		lpal_Set_Dest_Pal_Color(81, 96, 0, 0, 0);
		lfade_Start_Full_Fade(FADE_WIPE_SNAP_OFF, FADE_COLOR_PAL_TO_PAL, 1, 0, 1);
		lactor_Hide_Actor(actor);
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
		lactor_Hide_Actor(actor);
	} else {
		if (film_time == 38)
			lfade_Start_Full_Fade(FADE_WIPE_SNAP_ON, FADE_COLOR_TWO_PHASE, 1, 0, 1);
		if (film_time == 39)
			lactor_Show_Actor(actor);
	}
}

/* ================================================================
 * Text line animation
 * ================================================================ */

/* Per-frame: advance each active line's Y position with deceleration */
static void user_Title(Actor* actor, int32_t time) {
	(void)actor;
	(void)time;

	for (int16_t i = 0; i < num_lines; i++) {
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
static int draw_Title(Actor* actor, Rect* r, Rect* clip_r, int16_t off_x, int16_t off_y, int16_t refresh) {
	(void)actor;
	(void)r;
	(void)clip_r;
	(void)off_x;
	(void)off_y;

	if (!refresh)
		return 1;

	char* dataptr = (char*)lbitmap_Lock_Bitmap(&background);

	for (int16_t i = 0; i < num_lines; i++) {
		if (!line_used[i])
			continue;

		int16_t y = line_y[i];
		int16_t by = buff_y[i];
		int16_t yf = 2 * (y - 40);
		if (line_yf[i] >= 2048)
			yf--;

		for (int16_t j = 0; j < 20; j++) {
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

	lbitmap_Unlock_Bitmap(&background);
	return 1;
}

/* ================================================================
 * Background text preparer
 * ================================================================ */

/* At time 0: initialize 18 text lines with staggered positions */
static void user_Back(Actor* actor, int32_t time) {
	(void)actor;

	int16_t start = 100;
	if (shellext_Get_Cur_Scene() == SCENE_TITLE) {
		if (lio_Is_System_Slower_Than(2))
			start = 60;
	} else {
		start = 1;
	}

	if (time)
		return;

	num_lines = MAX_LINES;
	for (int16_t i = 0; i < num_lines; i++) {
		buff_y[i] = 20 * (i % 10);
		line_y[i] = start + 28 * i + 200;
		line_y_initial_arr[i] = line_y[i];
		line_yf[i] = 0;
		line_yv[i] = 1;
		line_yvf[i] = 0;
		line_used[i] = 1;
		line_drawn[i] = 0;
	}
}

/* Draw: render text lines into background bitmap as they come into view */
static int draw_Back(Actor* actor, Rect* r, Rect* clip_r, int16_t off_x, int16_t off_y, int16_t refresh) {
	(void)actor;
	(void)r;
	(void)clip_r;
	(void)off_x;
	(void)off_y;

	if (!refresh)
		return 1;

	lcanvas_Push_Canvas(&background);

	for (int16_t i = 0; i < num_lines; i++) {
		if (!line_drawn[i] && line_y[i] <= 200) {
			Rect tr;
			lrect_Set_Rect(&tr, 0, buff_y[i], 320, buff_y[i] + 20);
			lpaint_Paint_Clipped_Rect(&tr, 0);

			char string[64];
			lparagrp_Get_Paragraph_String(title_text, string, 0, i);
			lfont_Print_Centered_Text(string, &tr, 15, title_font);
			line_drawn[i] = 1;
		}
	}

	lcanvas_Pop_Canvas();
	return 1;
}

/* ================================================================
 * Snapshot emit — HD compositor channel
 * ================================================================
 *
 * Scene-start one-shot: the snapshot ships the static text + initial-y
 * for all 18 lines while the title task is alive. The application pre-renders
 * the whole stack to a texture once per scene and scrolls that texture
 * at a known rate using its own wall clock — no per-frame engine→application
 * y sync. The snapshot's only job is to deliver text + scene-start /
 * scene-end signaling (via scene_tag transitions handled in shell.c). */
int TieRecoveredTitle_SnapshotLineCount(void) { return title_text && num_lines > 0 ? num_lines : 0; }

bool TieRecoveredTitle_ReadSnapshotLine(int index, char* text, size_t capacity, float* initial_y) {
	if (!title_text || !text || !capacity || !initial_y || index < 0 || index >= num_lines)
		return false;
	text[0] = '\0';
	lparagrp_Get_Paragraph_String(title_text, text, 0, index);
	text[capacity - 1] = '\0';
	*initial_y = (float)line_y_initial_arr[index];
	return true;
}

typedef enum {
	TITLE_PHASE_BEGIN = 0,
	TITLE_PHASE_CLEANUP = 1,
} TitlePhase;

typedef struct TitleTask {
	SceneHeadStruct* scene_head;
	ResFile* file;
	TitlePhase phase;
} TitleTask;

static LandruTaskStepResult title_task_step(void* self) {
	TitleTask* t = (TitleTask*)self;

	if (t->phase == TITLE_PHASE_BEGIN) {
		Rect frame;

		/* Load resources */
		t->file = shellext_Open_Empire_Resource(title_str[0]); /* "title.lfd" */

		/* Resolve the paragraph / scene name. SCENE_TITLE uses the
		 * "title" paragraph (Star Wars opening crawl); other scenes
		 * use the per-battle "todtxtN" variant. We use the same string
		 * to load text and to tag the snapshot for the HD compositor's
		 * bundle lookup. */
		char film_name[16];
		if (shellext_Get_Cur_Scene() == SCENE_TITLE) {
			strcpy(film_name, title_str[5]); /* "title" */
		} else {
			strcpy(film_name, title_str[6]); /* "todtxt1" */
			film_name[6] = pilot_record.cur_battle + '1';
		}
		title_text = lparagrp_Res_Paragraph(t->file, film_name);

		/* Load font */
		/* TIE98 0x490067/0x4909E6: retain slot 2 for the
		 * SVGA frontend font and place the VGA title font in slot 4. */
		title_font = TieProfile_FrontendId() == TIE_FRONTEND_PROFILE_TIE98 ? 4 : 2;
		lfont_Res_Font(title_str[1], (uint16_t)title_font); /* "helv-20" */

		/* Initialize state */
		base_color = 0;
		scale_amount = 12;
		scale_amount_f = 0;
		film_time = (shellext_Get_Cur_Scene() == SCENE_TITLE) ? 0 : 99;

		/* Build scale lookup tables */
		for (int16_t i = 0; i <= 320; i++) {
			if (i) {
				scale_skip[i] = 320 / i - 1;
				scale_skipf[i] = (int16_t)(((320 % i) << 16) / i);
			} else {
				scale_skip[0] = 0;
				scale_skipf[0] = 0;
			}
		}

		/* Allocate background bitmap */
		lrect_Set_Rect(&frame, 0, 0, 320, 200);
		lbitmap_Init_Bitmap(&background);
		lbitmap_Alloc_Bitmap(&background, 320, 200);

		/* Create actors (scene 8 only: along + starwars) */
		if (shellext_Get_Cur_Scene() == SCENE_TITLE) {
			along_actor = lactdelt_Res_Delta_Actor(title_str[2], &frame, 0, 0, 20); /* "along" */
			lactor_Set_Actor_Time(along_actor, 0, 38);

			starwars_actor = lactdelt_Res_Delta_Actor(title_str[3], &frame, 30, 32, 20); /* "starwars" */
			if (lio_Is_System_Slower_Than(2))
				lactor_Set_Actor_User_Function(starwars_actor, user_Slow_StarWars);
			else
				lactor_Set_Actor_User_Function(starwars_actor, user_StarWars);
		}

		stars_actor = lactdelt_Res_Delta_Actor(title_str[4], &frame, 0, 0, 100); /* "stars" */
		lactor_Set_Actor_User_Function(stars_actor, user_Stars);

		back_actor = lactcust_Alloc_Custom_Actor(NULL, &frame, 0, 0, 10);
		lactor_Set_Actor_User_Function(back_actor, user_Back);
		lactor_Set_Actor_Draw_Function(back_actor, draw_Back);

		title_actor = lactcust_Alloc_Custom_Actor(NULL, &frame, 0, 0, 0);
		lactor_Set_Actor_User_Function(title_actor, user_Title);
		lactor_Set_Actor_Draw_Function(title_actor, draw_Title);

		/* Set palettes */
		Palette* pal = lpal_Res_Palette(title_str[5]); /* "title" */
		lpal_Set_Dest_Palette(pal);
		lpal_Set_Dest_Palette(t->scene_head->def_palette);

		/* Start fade and push the modal view task */
		lfade_Start_Full_Fade(FADE_WIPE_SNAP_ON, FADE_COLOR_TWO_PHASE, 1, 0, 1);
		lview_Set_View_Update_Function(end_View);
		lview_Disable_Global_View_Erase();

		const char* path = (TieStorage_IsDirectory(TIE_FILE_ROOT_FRONTEND_ASSET, "astream") ||
							TieStorage_IsDirectory(TIE_FILE_ROOT_FRONTEND_ASSET, "ASTREAM"))
							   ? "astream\\os1-v3.wrk"
							   : "stream\\os1-v3.wrk";
		lstream_Chain_Stream_File(path);

		/* Tag the scene for the HD compositor: bundle key (TITLE, <film>)
		 * resolves the remaster manifest at <root>/TITLE/films/<film>/.
		 * FULL_FRAME because the crawl + logo zoom move every tick;
		 * INCREMENTAL would leave trails on the persistent RT. Both are
		 * cleared by shell_task_step before the next scene's Push. */
		TieSnapshotBuilder_SetActiveFilm("TITLE", film_name);
		TieSnapshotBuilder_SetRedrawModel(TIE_REDRAW_FULL_FRAME);

		lviewadd_Push_Handle_View_Task();

		t->phase = TITLE_PHASE_CLEANUP;
		return LANDRU_TASK_STEP_CONTINUE;
	}

	/* CLEANUP */
	Rect frame;
	lview_Enable_Global_View_Erase();
	lview_Clear_View_Update_Function();
	lbitmap_Free_Bitmap(&background);
	lparagrp_Free_Paragraph(title_text);
	title_text = NULL; /* mute TieRecoveredTitle_CaptureSnapshot once we leave */
	num_lines = 0;
	lres_Close_Resource(t->file);

	lcanvas_Get_Drawing_Canvas_Bounds(&frame);
	lview_Set_View_Frame(0, &frame);
	lview_Set_View_Pos(0, 0, 0);

	return LANDRU_TASK_STEP_DONE;
}

static const LandruTaskVtable title_task_vt = {
	.step = title_task_step,
};

void title_Push_Title_Task(SceneHeadStruct* scene_head) {
	TitleTask* t = (TitleTask*)landru_task_push(&title_task_vt);
	if (!t)
		return;
	t->scene_head = scene_head;
	t->file = NULL;
	t->phase = TITLE_PHASE_BEGIN;
}
