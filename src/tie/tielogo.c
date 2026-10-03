#include "tie/tielogo.h"
#ifdef TIE_MODERN
#include "tie_runtime/runtime/profile.h"
#include "tie_runtime/runtime/tielogo_task.h"
#endif
#include "tie/shellext.h"
#include "tie/soundext.h"
#ifdef TIE_MODERN
#include "tie_runtime/snapshot/logo.h"
#endif

#include "landru/actanim.h"
#include "landru/actcust.h"
#include "landru/actor.h"
#include "landru/bitmap.h"
#include "landru/canvas.h"
#include "landru/cursor.h"
#include "landru/dirty.h"
#include "landru/error.h"
#include "landru/film.h"
#include "landru/fourcc.h"
#include "landru/rect.h"
#include "landru/res.h"
#include "landru/timer.h"
#include "landru/view.h"
#include "landru/viewadd.h"

#include <stdlib.h>
#include <string.h>

/* ---- Static data (initialized, from binary .data segment) ---- */

/* Fighter launch delay per slot (32 slots).
 * Higher = later launch, slower speed. */
// GLOBAL: TIE95 0xcf806
// GLOBAL: TIE98 0x4f2d40
static const int16_t fight_vel[32] = { 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 2, 2,
									   2, 2, 2, 2, 2, 2, 2, 2, 2, 1, 1, 1, 1, 1, 1, 1 };

/* SFX trigger times (relative to time 0, +24 offset applied at runtime).
 * Sentinel 999 terminates the list. */
// GLOBAL: TIE95 0xcf846
// GLOBAL: TIE98 0x4f2d80
static const int16_t fight_sfx[11] = { 42, 50, 60, 68, 73, 80, 87, 95, 103, 109, 999 };

/* Fighter X direction per slot: -1=left, 0=center, 1=right */
// GLOBAL: TIE95 0xcf85c
// GLOBAL: TIE98 0x4f2d98
static const int16_t fight_xv[32] = { -1, -1, 0, 0, -1, 1, -1, -1, 0, 1, 1,  0, 1, 0, -1, 1,
									  1,  0,  1, 1, 1,  0, -1, 1,  0, 1, -1, 0, 1, 1, -1, 0 };

/* Fighter Y direction per slot: -1=up, 0=center, 1=down */
// GLOBAL: TIE95 0xcf89c
// GLOBAL: TIE98 0x4f2dd8
static const int16_t fight_yv[32] = { 0, -1, -1, 1, 0, 1, -1, 1,  1,  -1, 0, -1, 1, 1, 0, -1,
									  0, -1, -1, 1, 0, 1, -1, -1, -1, 0,  1, 1,  1, 0, 1, 1 };

/* ---- Static globals (BSS, from binary) ---- */

// GLOBAL: TIE95 0xf5e88
// GLOBAL: TIE98 0x58a320
int16_t tielogo_fight_x[32]; /* current X position per slot */
// GLOBAL: TIE95 0xf5ec8
// GLOBAL: TIE98 0x58a360
int16_t tielogo_fight_y[32]; /* current Y position per slot */
// GLOBAL: TIE95 0xf5f08
// GLOBAL: TIE98 0x58a3a8
int16_t tielogo_fight_state[32]; /* steps remaining (-1=inactive, 0=stamp) */
// GLOBAL: TIE95 0xf5f48
// GLOBAL: TIE98 0x58a2e0
static int16_t tielogo_fight_xadd[32]; /* X velocity per step */
// GLOBAL: TIE95 0xf5f88
// GLOBAL: TIE98 0x58a298
static int16_t tielogo_fight_yadd[32]; /* Y velocity per step */

/* Exported for film object access */
// GLOBAL: TIE95 0xf5fc8
// GLOBAL: TIE98 0x58e08c
Actor* tie_actor;
// GLOBAL: TIE95 0xf5fcc
// GLOBAL: TIE98 0x58e090
Actor* fighter2_actor;
// GLOBAL: TIE95 0xf5fd0
// GLOBAL: TIE98 0x58e094
Actor* fighter_actor;

// GLOBAL: TIE95 0xf5fd4
// GLOBAL: TIE98 0x58a3e8
BitmapStruct tielogo_background;
// GLOBAL: TIE95 0xf5fe8
// GLOBAL: TIE98 0x58a3a4
static Actor* close_actor;
// GLOBAL: TIE95 0xf5fec
// GLOBAL: TIE98 0x58a2dc
static Actor* backdrop;
// GLOBAL: TIE95 0xf5ff0
// GLOBAL: TIE98 0x58a3a0
static Film* tielogo_film;
// GLOBAL: TIE95 0xf5ff4
// GLOBAL: TIE98 0x58a2d8
static int16_t fight_count;

static void tielogo_end_View(int32_t frame_num);
static int16_t tielogo_film_Callback(Film* film, FilmObject* film_object);
static int16_t tielogo_film_Actor_To_Background(Actor* actor);
static int16_t tielogo_draw_Backdrop(Actor* actor, Rect* r, Rect* clip_r, int16_t x, int16_t y,
									 int16_t refresh);
static void tielogo_user_Close(Actor* actor, int32_t time);
static int16_t tielogo_draw_Close(Actor* actor, Rect* r, Rect* clip_r, int16_t x, int16_t y, int16_t refresh);
static void tielogo_user_Tie(Actor* actor, int32_t time);
static void tielogo_user_Fighter(Actor* actor, int32_t time);
static int16_t tielogo_draw_Fighter(Actor* actor, Rect* r, Rect* clip_r, int16_t x, int16_t y,
									int16_t refresh);

// FUNCTION: TIE95 0x72780
// FUNCTION: TIE98 0x48F830
int tielogo_TieLogo(SceneHeadStruct* scene_head) {
	ResFile* resource;
	Rect frame;

	resource = shellext_Open_Empire_Resource("tielogo.lfd");
#ifdef TIE_MODERN
	if (!resource) {
		TieLogo_RunView(resource, false);
		return 0;
	}
#endif
	xrect_Set_Rect(&frame, 0, 0, 320, 200);
	xbm_Init_Bitmap(&tielogo_background);
#ifdef TIE_MODERN
	if (!xbm_Alloc_Bitmap(&tielogo_background, 320, 200)) {
		TieLogo_RunView(resource, false);
		return 0;
	}
#else
	xbm_Alloc_Bitmap(&tielogo_background, 320, 200);
#endif
	xbm_Erase_Bitmap(&tielogo_background);
	xviewadd_Clear_View();
	xview_Disable_All_View_Erase();
	xcanvas_Invalid_Screen_Diff();

	tielogo_film = xfilm_Res_Callback_Film("logo", &frame, 0, 0, 0, tielogo_film_Callback);
#ifdef TIE_MODERN
	if (!tielogo_film) {
		TieLogo_RunView(resource, false);
		return 0;
	}
#endif
	tie_actor = xactor_Find_Actor(FOURCC_ANIM, "tie3");
#ifdef TIE_MODERN
	if (!tie_actor) {
		TieLogo_RunView(resource, false);
		return 0;
	}
#endif
	xactor_Set_Actor_User_Function(tie_actor, tielogo_user_Tie);
	tie_actor->id = 0;

	fighter_actor = xactanim_Res_Anim_Actor("fighter", &frame, 0, 0, 0);
#ifdef TIE_MODERN
	if (!fighter_actor) {
		TieLogo_RunView(resource, false);
		return 0;
	}
#endif
	xactor_Set_Actor_Time(fighter_actor, -1, -1);
	fighter2_actor = xactanim_Res_Anim_Actor("fighter2", &frame, 0, 0, 0);
#ifdef TIE_MODERN
	if (!fighter2_actor) {
		TieLogo_RunView(resource, false);
		return 0;
	}
#endif
	xactor_Set_Actor_User_Function(fighter2_actor, tielogo_user_Fighter);
	xactor_Set_Actor_Draw_Function(fighter2_actor, tielogo_draw_Fighter);

	close_actor = xactcust_Alloc_Custom_Actor(LANDRU_NULL_HANDLE, &frame, 0, 0, -200);
#ifdef TIE_MODERN
	if (!close_actor) {
		TieLogo_RunView(resource, false);
		return 0;
	}
#endif
	xactor_Set_Actor_User_Function(close_actor, tielogo_user_Close);
	xactor_Set_Actor_Draw_Function(close_actor, tielogo_draw_Close);
	xfilm_Set_Film_Def_Palette(tielogo_film, scene_head->def_palette);
	xview_Set_View_Update_Function(tielogo_end_View);
	if (xcursor_Is_Cursor_Visible())
		xcursor_Hide_Cursor();
#ifdef TIE_MODERN
	TieLogo_RunView(resource, true);
	return 0;
#else
	shellext_Handle_TIE_View();
	xview_Clear_View_Update_Function();
	xview_Enable_All_View_Erase();
#ifdef TIE98
	xrect_Set_Rect(&frame, 0, 0, 640, 480);
#else
	xcanvas_Get_Drawing_Canvas_Bounds(&frame);
#endif
	xview_Set_View_Frame(0, &frame);
	xview_Set_View_Pos(0, frame.left, frame.top);
	xbm_Free_Bitmap(&tielogo_background);
	xres_Close_Resource(resource);
	xtimer_Set_Frame_Rate(20);
	return xerror_Get_Landru_Exit();
#endif
}

/* View update callback. On frame 0, maxes the dirty list.
 * Checks for scene exit (film finished = cur_cel == cels). */
// FUNCTION: TIE95 0x72938
// FUNCTION: TIE98 0x48fa70
static void tielogo_end_View(int32_t frame_num) {
	int16_t exit_id;

	if (!frame_num)
		xdirty_Max_Dirty_List();
	if (shellext_Check_Scene_Exit(&exit_id, 90, 100, tielogo_film->cur_cel == tielogo_film->cels))
		xerror_Set_Landru_Exit(exit_id);
}

/* Film per-frame callback. For actor objects (type_code 3): rewinds
 * the actor film, then if var1 == 20 captures the frame into the
 * background. If var2 == 1, replaces draw with tielogo_draw_Backdrop. */
// FUNCTION: TIE95 0x72988
// FUNCTION: TIE98 0x48fac0
static int16_t tielogo_film_Callback(Film* film, FilmObject* film_object) {
	int16_t should_stop = 0;
	Actor* actor;

	if (film_object->id == 3) {
		xfilm_Rewind_Actor_Film(film, film_object, (void*)((char*)film_object + sizeof(FilmObject)));
		actor = film_object->object;
		if (actor->var1 == 20) {
#ifdef TIE_MODERN
			/* The backdrop remains live; only hidden/stamped actors need sticky poses. */
			if (actor->var2 != 1)
				TieLogoSnapshot_Stamp(actor);
#endif
			tielogo_film_Actor_To_Background(actor);
			if (actor->var2 == 1) {
				xactor_Set_Actor_Draw_Function(actor, tielogo_draw_Backdrop);
				backdrop = actor;
			}
			should_stop = (int)(actor->var2 == 0) & 0xff;
		}
	}
	return should_stop;
}

/* Stamp an actor into the off-screen bitmap and restore the drawing canvas. */
// FUNCTION: TIE95 0x729e8
// FUNCTION: TIE98 0x48fb20
static int16_t tielogo_film_Actor_To_Background(Actor* actor) {
	Rect canvas_bounds, clip;
	int result = 0;

	xcanvas_Get_Drawing_Canvas_Bounds(&canvas_bounds);
	xcanvas_Push_Canvas(&tielogo_background);
	if (actor->draw) {
		xrect_Copy_Rect(&clip, &actor->frame);
		xcanvas_Set_Drawing_Canvas_Clip(&clip);
		result = actor->draw(actor, &canvas_bounds, &clip, actor->x, actor->y, 1);
		xcanvas_Max_Drawing_Canvas_Clip();
	}
	xcanvas_Pop_Canvas();
	return result;
}

/* ---- Actor draw/user callbacks ---- */

/* Draw callback: copy the entire background bitmap to the canvas.
 * Replaces the normal draw for actors composited into the background. */
// FUNCTION: TIE95 0x72a50
// FUNCTION: TIE98 0x48fba0
static int16_t tielogo_draw_Backdrop(Actor* actor, Rect* r, Rect* clip_r, int16_t x, int16_t y,
									 int16_t refresh) {
	Rect ra;
	(void)actor;
	(void)r;
	(void)clip_r;
	(void)x;
	(void)y;
	if (!refresh)
		return 0;

	xrect_Set_Rect(&ra, 0, 0, 320, 200);
	xcanvas_Copy_Bitmap_Portion_To_Canvas(&tielogo_background, &ra, 0, 0);
	return 1;
}

/* User callback for the close/cleanup actor. When the film finishes,
 * hides the backdrop and sets var1 to trigger canvas erase. */
// FUNCTION: TIE95 0x72a94
// FUNCTION: TIE98 0x48fbf0
static void tielogo_user_Close(Actor* actor, int32_t time) {
	(void)time;
	if (tielogo_film->cur_cel == tielogo_film->cels) {
		xactor_Hide_Actor(backdrop);
		actor->var1 = 1;
	} else {
		actor->var1 = 0;
	}
}

/* Draw callback for the close actor. When var1 is set (film finished),
 * erases the canvas and maxes the dirty list for the scene transition. */
// FUNCTION: TIE95 0x72ac0
// FUNCTION: TIE98 0x48fc30
static int16_t tielogo_draw_Close(Actor* actor, Rect* r, Rect* clip_r, int16_t x, int16_t y,
								  int16_t refresh) {
	(void)r;
	(void)clip_r;
	(void)x;
	(void)y;
	(void)refresh;
	if (actor->var1) {
		xcanvas_Erase_Canvas();
		xdirty_Max_Dirty_List();
		xcanvas_Invalid_Screen_Diff();
	}
	return 1;
}

/* User callback for the TIE text actor. Waits for the animation to
 * reach its penultimate frame, then stamps the actor into the background
 * and hides it on the next frame. */
// FUNCTION: TIE95 0x72ae0
// FUNCTION: TIE98 0x48fc50
static void tielogo_user_Tie(Actor* actor, int32_t time) {
	(void)time;
	if (!xactor_Is_Actor_Visible(actor))
		return;

	if (tie_actor->id) {
#ifdef TIE_MODERN
		TieLogoSnapshot_Stamp(actor);
#endif
		tielogo_film_Actor_To_Background(actor);
		xactor_Hide_Actor(actor);
	} else if (tie_actor->state == tie_actor->arraySize - 1) {
		tie_actor->id++;
	}
}

/* User callback for the fighter fly-by. Manages the 32-slot particle
 * system: initialization, launch timing, movement, and stamp-to-background. */
// FUNCTION: TIE95 0x72b20
// FUNCTION: TIE98 0x48fca0
static void tielogo_user_Fighter(Actor* actor, int32_t time) {
	int16_t i;
	int16_t slot_idx;
	int16_t speed;
	int16_t accum_time;

	/* Frame 0: initialize all slots and compute total frame count */
	if (!time) {
		fight_count = 0;
		for (i = 0; i < 32; i++) {
			tielogo_fight_state[i] = -1;
			speed = 20 - fight_vel[i];
			fight_count += 140 / speed;
		}
		xactor_Hide_Actor(actor);
		return;
	}

	/* Play sfxKaWhoosh at predefined times */
	for (i = 0; fight_sfx[i] + 24 <= time; i++) {
		if (fight_sfx[i] + 24 == time)
			soundext_Play_SFX(sfxKaWhoosh, 64);
	}

	/* Frame rate control */
	if (time == 52)
		xtimer_Set_Frame_Rate(4);
	if (time == 200)
		xtimer_Set_Frame_Rate(20);

	if (time >= 52 && time < fight_count + 52) {
		/* Inside active window */
		if (!xactor_Is_Actor_Visible(actor))
			xactor_Show_Actor(actor);

		for (slot_idx = 0, accum_time = 0; slot_idx < 32; slot_idx++) {
			if (accum_time == time - 52) {
				/* Launch this slot */
				int16_t fighter_idx;
				if (slot_idx & 1)
					fighter_idx = 32 - ((slot_idx >> 1) + 1);
				else
					fighter_idx = slot_idx >> 1;

				speed = 16 - fight_vel[fighter_idx];
				tielogo_fight_state[fighter_idx] = 180 / speed;
				tielogo_fight_x[fighter_idx] =
					speed * (tielogo_fight_state[fighter_idx] + 1) * fight_xv[fighter_idx];
				tielogo_fight_y[fighter_idx] =
					speed * (tielogo_fight_state[fighter_idx] + 1) * fight_yv[fighter_idx];
				tielogo_fight_xadd[fighter_idx] = -fight_xv[fighter_idx] * speed;
				tielogo_fight_yadd[fighter_idx] = -fight_yv[fighter_idx] * speed;
			} else if (tielogo_fight_state[slot_idx] > 0) {
				/* Move fighter toward center */
				tielogo_fight_state[slot_idx]--;
				tielogo_fight_x[slot_idx] += tielogo_fight_xadd[slot_idx];
				tielogo_fight_y[slot_idx] += tielogo_fight_yadd[slot_idx];
			} else if (tielogo_fight_state[slot_idx] == 0) {
				/* Fighter arrived: stamp into background */
				xactor_Set_Actor_State(fighter_actor, slot_idx, 0);
#ifdef TIE_MODERN
				TieLogoSnapshot_Stamp(fighter_actor);
#endif
				tielogo_film_Actor_To_Background(fighter_actor);
				tielogo_fight_state[slot_idx] = -1;
			}
			accum_time += fight_vel[slot_idx];
		}
	} else {
		/* Outside active window: hide actor when visible */
		if (time == fight_count + 52)
			xtimer_Set_Frame_Rate(20);
		if (xactor_Is_Actor_Visible(actor))
			xactor_Hide_Actor(actor);
	}
}

/* Draw callback for the fighter particle system. Draws all active
 * fighter slots at their current positions. */
// FUNCTION: TIE95 0x72d90
// FUNCTION: TIE98 0x48ff00
static int16_t tielogo_draw_Fighter(Actor* actor, Rect* r, Rect* clip_r, int16_t x, int16_t y,
									int16_t refresh) {
	int16_t i;
	if (!refresh)
		return 1;

	for (i = 0; i < 32; i++) {
		if (tielogo_fight_state[i] >= 0) {
			xactor_Set_Actor_State(actor, i, 0);
			xactanim_Draw_Anim_Actor(actor, r, clip_r, tielogo_fight_x[i] + x, tielogo_fight_y[i] + y,
									 refresh);
		}
	}
	return 1;
}
