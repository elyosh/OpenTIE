#include <stdlib.h>
#include <string.h>

#include "tie/shellext.h"
#include "tie/soundext.h"
#include "tie/tielogo.h"
#include "tie_runtime/integration/landru_adapter.h"
#include "tie_runtime/snapshot/capture_views.h"
#include "tie_runtime/snapshot/snapshot.h"
#include "tie_runtime/snapshot/snapshot_internal.h"
#include <landru/task.h>

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

/* ---- Static data (initialized, from binary .data segment) ---- */

/* Fighter launch delay per slot (32 slots).
 * Higher = later launch, slower speed. */
static const int16_t fight_vel[32] = { 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 2, 2,
									   2, 2, 2, 2, 2, 2, 2, 2, 2, 1, 1, 1, 1, 1, 1, 1 };

/* SFX trigger times (relative to time 0, +24 offset applied at runtime).
 * Sentinel 999 terminates the list. */
static const int16_t fight_sfx[11] = { 42, 50, 60, 68, 73, 80, 87, 95, 103, 109, 999 };

/* Fighter X direction per slot: -1=left, 0=center, 1=right */
static const int16_t fight_xv[32] = { -1, -1, 0, 0, -1, 1, -1, -1, 0, 1, 1,  0, 1, 0, -1, 1,
									  1,  0,  1, 1, 1,  0, -1, 1,  0, 1, -1, 0, 1, 1, -1, 0 };

/* Fighter Y direction per slot: -1=up, 0=center, 1=down */
static const int16_t fight_yv[32] = { 0, -1, -1, 1, 0, 1, -1, 1,  1,  -1, 0, -1, 1, 1, 0, -1,
									  0, -1, -1, 1, 0, 1, -1, -1, -1, 0,  1, 1,  1, 0, 1, 1 };

/* ---- Static globals (BSS, from binary) ---- */

static int16_t fight_x[32];     /* current X position per slot */
static int16_t fight_y[32];     /* current Y position per slot */
static int16_t fight_state[32]; /* steps remaining (-1=inactive, 0=stamp) */
static int16_t fight_xadd[32];  /* X velocity per step */
static int16_t fight_yadd[32];  /* Y velocity per step */

/* Exported for film object access */
Actor* tie_actor;
Actor* fighter2_actor;
Actor* fighter_actor;

static BitmapStruct background;
static Actor* close_actor;
// GLOBAL: TIE 0xF5FEC
static Actor* backdrop;
// GLOBAL: TIE 0xF5FF0
static Film* tielogo_film;
static int32_t fight_count;

/* Sticky stamp list — every call to stamp_actor_to_background captures
 * the actor's pose into one entry. TieRecoveredLogo_CaptureSnapshot replays them
 * onto actors_2D each tick so the HD compositor sees the assembled-
 * logo accumulation that classic gets from draw_Backdrop's bitmap
 * blit. The list is reset in TIELOGO_PHASE_CLEANUP. Cap is generous
 * for a 32-fighter particle system + per-film stamps. */
#define TIELOGO_MAX_STAMPS 96
static LandruActorRenderState s_stamps[TIELOGO_MAX_STAMPS];
static int s_stamp_count;

/* ---- Internal helpers ---- */

/* Stamp an actor's current frame into the background bitmap.
 * Pushes the background as canvas, clips to the actor's frame,
 * calls the actor's draw function, then restores the canvas.
 *
 * If capture_sticky is true, also records the actor's current pose
 * into the HD sticky list before the canvas push — the cutscene
 * compositor doesn't see the off-screen bitmap, so the stamp is
 * replayed onto actors_2D each tick by TieRecoveredLogo_CaptureSnapshot.
 * Capture runs while the screen canvas is still bound (so frame /
 * scale / flags reflect the same pose classic is about to rasterise)
 * and before any user_*-callback hide that follows the stamp call.
 *
 * The var2==1 backdrop-drawer actor (e.g. "stars") passes false:
 * classic uses it as a draw-function host (replaced with
 * draw_Backdrop afterwards, which blits the accumulated bitmap each
 * frame) rather than as visible content of its own. The HD path
 * already covers it via lactor_emit_render_state's live entry at the
 * correct background zplane; capturing a sticky duplicate here would
 * land at the END of actors_2D (sticky entries are appended last) and
 * paint the full-screen backdrop on top of every other actor in
 * z order. */
static void stamp_actor_to_background(Actor* actor, bool capture_sticky) {
	Rect canvas_bounds, clip;

	if (capture_sticky && s_stamp_count < TIELOGO_MAX_STAMPS) {
		LandruActorRenderState* st = &s_stamps[s_stamp_count++];
		lactor_fill_render_state(actor, st);
		/* Force the captured pose to be drawn by the HD renderer
		 * even after classic hides the actor (lactor_Hide_Actor in
		 * user_Tie / fight_state[]=-1 in user_Fighter). */
		st->flags |= TIE_ACTOR2D_VISIBLE;
	}

	lcanvas_Get_Drawing_Canvas_Bounds(&canvas_bounds);
	lcanvas_Push_Canvas(&background);
	if (actor->draw) {
		lrect_Copy_Rect(&clip, &actor->frame);
		lcanvas_Set_Drawing_Canvas_Clip(&clip);
		actor->draw(actor, &canvas_bounds, &clip, actor->x, actor->y, 1);
		lcanvas_Max_Drawing_Canvas_Clip();
	}
	lcanvas_Pop_Canvas();
}

/* Actor draw callback that stamps the actor into the background bitmap.
 * Has the lactorDrawFunc signature so it can replace actor->draw.
 * The passed-in r/clip_r/x/y/refresh are ignored — the actor's own
 * frame and position are used instead. Returns the inner draw result.
 * No callers in the LecDemos build (0 xrefs), but present in the binary. */
static int film_Actor_To_Background(Actor* actor, Rect* r, Rect* clip_r, int16_t x, int16_t y,
									int16_t refresh) {
	(void)r;
	(void)clip_r;
	(void)x;
	(void)y;
	(void)refresh;
	Rect canvas_bounds, clip;
	int result = 0;

	lcanvas_Get_Drawing_Canvas_Bounds(&canvas_bounds);
	lcanvas_Push_Canvas(&background);
	if (actor->draw) {
		lrect_Copy_Rect(&clip, &actor->frame);
		lcanvas_Set_Drawing_Canvas_Clip(&clip);
		result = actor->draw(actor, &canvas_bounds, &clip, actor->x, actor->y, 1);
		lcanvas_Max_Drawing_Canvas_Clip();
	}
	lcanvas_Pop_Canvas();
	return result;
}

/* ---- Actor draw/user callbacks ---- */

/* Draw callback: copy the entire background bitmap to the canvas.
 * Replaces the normal draw for actors composited into the background. */
static int draw_Backdrop(Actor* actor, Rect* r, Rect* clip_r, int16_t x, int16_t y, int16_t refresh) {
	(void)actor;
	(void)r;
	(void)clip_r;
	(void)x;
	(void)y;
	if (!refresh)
		return 0;

	Rect ra;
	lrect_Set_Rect(&ra, 0, 0, 320, 200);
	lcanvas_Copy_Bitmap_Portion_To_Canvas(&background, &ra, 0, 0);
	return 1;
}

/* User callback for the close/cleanup actor. When the film finishes,
 * hides the backdrop and sets var1 to trigger canvas erase. */
static void user_Close(Actor* actor, int32_t time) {
	(void)time;
	if (tielogo_film->cur_cel == tielogo_film->cels) {
		lactor_Hide_Actor(backdrop);
		actor->var1 = 1;
	} else {
		actor->var1 = 0;
	}
}

/* Draw callback for the close actor. When var1 is set (film finished),
 * erases the canvas and maxes the dirty list for the scene transition. */
static int draw_Close(Actor* actor, Rect* r, Rect* clip_r, int16_t x, int16_t y, int16_t refresh) {
	(void)r;
	(void)clip_r;
	(void)x;
	(void)y;
	(void)refresh;
	if (actor->var1) {
		lcanvas_Erase_Canvas();
		ldirty_Max_Dirty_List();
		lcanvas_Invalid_Screen_Diff();
	}
	return 1;
}

/* User callback for the TIE text actor. Waits for the animation to
 * reach its penultimate frame, then stamps the actor into the background
 * and hides it on the next frame. */
static void user_Tie(Actor* actor, int32_t time) {
	(void)time;
	if (!lactor_Is_Actor_Visible(actor))
		return;

	if (tie_actor->id) {
		stamp_actor_to_background(actor, /*capture_sticky=*/true);
		lactor_Hide_Actor(actor);
	} else if (tie_actor->state == tie_actor->arraySize - 1) {
		tie_actor->id++;
	}
}

/* Draw callback for the fighter particle system. Draws all active
 * fighter slots at their current positions. */
static int draw_Fighter(Actor* actor, Rect* r, Rect* clip_r, int16_t x, int16_t y, int16_t refresh) {
	if (!refresh)
		return 1;

	int16_t i;
	for (i = 0; i < 32; i++) {
		if (fight_state[i] >= 0) {
			lactor_Set_Actor_State(actor, i, 0);
			lactanim_Draw_Anim_Actor(actor, r, clip_r, fight_x[i] + x, fight_y[i] + y, refresh);
		}
	}
	return 1;
}

/* User callback for the fighter fly-by. Manages the 32-slot particle
 * system: initialization, launch timing, movement, and stamp-to-background. */
static void user_Fighter(Actor* actor, int32_t time) {
	int16_t i;

	/* Frame 0: initialize all slots and compute total frame count */
	if (!time) {
		fight_count = 0;
		for (i = 0; i < 32; i++) {
			fight_state[i] = -1;
			fight_count += 140 / (20 - fight_vel[i]);
		}
		lactor_Hide_Actor(actor);
		return;
	}

	/* Play sfxKaWhoosh at predefined times */
	for (i = 0;; i++) {
		int32_t sfx_time = fight_sfx[i] + 24;
		if (sfx_time > time)
			break;
		if (sfx_time == time)
			soundext_Play_SFX(sfxKaWhoosh, 64);
	}

	/* Frame rate control */
	if (time == 52)
		ltimer_Set_Frame_Rate(4);
	if (time == 200)
		ltimer_Set_Frame_Rate(20);

	/* Outside active window: hide actor when visible */
	if (time < 52 || time >= fight_count + 52) {
		if (time == fight_count + 52)
			ltimer_Set_Frame_Rate(20);
		if (lactor_Is_Actor_Visible(actor))
			lactor_Hide_Actor(actor);
		return;
	}

	/* Inside active window */
	if (!lactor_Is_Actor_Visible(actor))
		lactor_Show_Actor(actor);

	int16_t slot_idx = 0;
	int32_t launch_frame = time - 52;
	int16_t accum_time = 0;

	for (slot_idx = 0; slot_idx < 32; slot_idx++) {
		if (accum_time == launch_frame) {
			/* Launch this slot */
			int16_t fighter_idx = slot_idx >> 1;
			if (slot_idx & 1)
				fighter_idx = 32 - (fighter_idx + 1);

			int16_t speed = 16 - fight_vel[fighter_idx];
			int16_t steps = 180 / speed;
			int16_t travel = speed * (steps + 1);

			fight_state[fighter_idx] = steps;
			fight_x[fighter_idx] = travel * fight_xv[fighter_idx];
			fight_y[fighter_idx] = travel * fight_yv[fighter_idx];
			fight_xadd[fighter_idx] = speed * -fight_xv[fighter_idx];
			fight_yadd[fighter_idx] = -fight_yv[fighter_idx] * speed;
		} else {
			int16_t steps_left = fight_state[slot_idx];
			if (steps_left > 0) {
				/* Move fighter toward center */
				fight_state[slot_idx] = steps_left - 1;
				fight_x[slot_idx] += fight_xadd[slot_idx];
				fight_y[slot_idx] += fight_yadd[slot_idx];
			} else if (steps_left == 0) {
				/* Fighter arrived: stamp into background */
				lactor_Set_Actor_State(fighter_actor, slot_idx, 0);
				stamp_actor_to_background(fighter_actor,
										  /*capture_sticky=*/true);
				fight_state[slot_idx] = -1;
			}
		}
		accum_time += fight_vel[slot_idx];
	}
}

/* Film per-frame callback. For actor objects (type_code 3): rewinds
 * the actor film, then if var1 == 20 captures the frame into the
 * background. If var2 == 1, replaces draw with draw_Backdrop. */
static int16_t film_Callback(Film* film, FilmObject* film_object) {
	int16_t should_stop = 0;

	if (film_object->id != 3)
		return 0;

	lfilm_Rewind_Actor_Film(film, film_object, (void*)((char*)film_object + sizeof(FilmObject)));
	Actor* actor = film_object->object;

	if (actor->var1 != 20)
		return 0;

	/* var2==1 actors are the backdrop drawer — captured into the
	 * classic background bitmap, then their draw is replaced with
	 * draw_Backdrop. Their HD coverage comes from lactor_emit_
	 * snapshot's live entry (the actor stays AF_VISIBLE in classic
	 * so the bitmap-blit path runs); skipping the sticky capture
	 * keeps the full-screen backdrop from being re-emitted at the
	 * end of actors_2D and stomping over every other actor in z. */
	bool capture_sticky = (actor->var2 != 1);
	stamp_actor_to_background(actor, capture_sticky);

	if (actor->var2 == 1) {
		lactor_Set_Actor_Draw_Function(actor, draw_Backdrop);
		backdrop = actor;
	}

	should_stop = (actor->var2 == 0) ? 1 : 0;
	return should_stop;
}

/* View update callback. On frame 0, maxes the dirty list.
 * Checks for scene exit (film finished = cur_cel == cels). */
static void end_View(int32_t frame_num) {
	if (!frame_num)
		ldirty_Max_Dirty_List();

	int16_t exit_id;
	int16_t film_done = (tielogo_film->cur_cel == tielogo_film->cels) ? 1 : 0;
	if (shellext_Check_Scene_Exit(&exit_id, 90, 100, film_done))
		lerror_Set_Landru_Exit(exit_id);
}

/* Add sticky background stamps and the fighter particle slots to the
 * full-frame actor snapshot. Both are drawn imperatively by classic code
 * and otherwise do not appear in the actor channel. Sticky entries are
 * appended after live actors so their accumulated image remains on top. */
int TieRecoveredLogo_ReadSnapshotActors(LandruActorRenderState* actors, int capacity) {
	if (!actors || capacity <= 0)
		return 0;
	int count = s_stamp_count < capacity ? s_stamp_count : capacity;
	memcpy(actors, s_stamps, (size_t)count * sizeof *actors);
	if (!fighter2_actor || count == capacity)
		return count;

	const int16_t saved_state = fighter2_actor->state;
	for (int index = 0; index < 32 && count < capacity; ++index) {
		if (fight_state[index] < 0)
			continue;
		LandruActorRenderState* output = &actors[count++];
		memset(output, 0, sizeof *output);
		fighter2_actor->state = (int16_t)index;
		lactor_fill_render_state(fighter2_actor, output);
		output->flags |= LANDRU_ACTOR_RENDER_VISIBLE;
		output->x = (int16_t)(output->x + fight_x[index]);
		output->y = (int16_t)(output->y + fight_y[index]);
	}
	fighter2_actor->state = saved_state;
	return count;
}

typedef enum {
	TIELOGO_PHASE_BEGIN = 0,
	TIELOGO_PHASE_CLEANUP = 1,
} TielogoPhase;

typedef struct TielogoTask {
	SceneHeadStruct* scene_head;
	ResFile* res_file;
	TielogoPhase phase;
} TielogoTask;

static LandruTaskStepResult tielogo_task_step(void* self) {
	TielogoTask* t = (TielogoTask*)self;

	if (t->phase == TIELOGO_PHASE_BEGIN) {
		Rect frame;

		t->res_file = shellext_Open_Empire_Resource("tielogo.lfd");

		lrect_Set_Rect(&frame, 0, 0, 320, 200);
		lbitmap_Init_Bitmap(&background);
		lbitmap_Alloc_Bitmap(&background, 320, 200);
		lbitmap_Erase_Bitmap(&background);

		lviewadd_Clear_View();
		lview_Disable_All_View_Erase();
		lcanvas_Invalid_Screen_Diff();

		/* Reset sticky stamps BEFORE loading the film. lfilm_Res_
		 * Callback_Film walks every film object and invokes the
		 * callback once during load — for any object whose initial
		 * var1 already equals 20 (the var2==1 backdrop "stars" actor
		 * here), film_Callback fires stamp_actor_to_background at
		 * load time, populating the sticky list before BEGIN finishes.
		 * Resetting AFTER the film load discarded those load-time
		 * stamps. */
		s_stamp_count = 0;

		/* Load logo film with per-frame callback */
		tielogo_film = lfilm_Res_Callback_Film("logo", &frame, 0, 0, 0, film_Callback);

		/* Find TIE text actor in the film and set its user callback */
		tie_actor = lactor_Find_Actor(FOURCC_ANIM, "tie3");
		lactor_Set_Actor_User_Function(tie_actor, user_Tie);
		tie_actor->id = 0;

		/* Load fighter animation actors.
		 *
		 * fighter_actor is a sprite container — user_Fighter sets its
		 * state/position imperatively at slot arrivals and stamps it
		 * into the offscreen background bitmap; nothing renders it
		 * via the standard actor-system path. fighter2_actor is the
		 * orchestrator: its draw_Fighter loops over fight_state[]
		 * slots and ignores the actor's own state. Both are flagged
		 * AF_NO_RENDER_CAPTURE so lactor_emit_render_state doesn't ship
		 * phantom poses to the HD compositor — the real HD coverage
		 * for the fly-in / stamping comes from TieRecoveredLogo_CaptureSnapshot
		 * (live fighter2 slot emits + sticky fighter stamp list). */
		fighter_actor = lactanim_Res_Anim_Actor("fighter", &frame, 0, 0, 0);
		lactor_Set_Actor_Time(fighter_actor, -1, -1);
		lactor_Set_Actor_Render_Capture_Hidden(fighter_actor, true);

		fighter2_actor = lactanim_Res_Anim_Actor("fighter2", &frame, 0, 0, 0);
		lactor_Set_Actor_User_Function(fighter2_actor, user_Fighter);
		lactor_Set_Actor_Draw_Function(fighter2_actor, draw_Fighter);
		lactor_Set_Actor_Render_Capture_Hidden(fighter2_actor, true);

		/* Create close/cleanup actor at z=-200 (drawn last). draw_Close
		 * erases the canvas at film end — no pixel content. CUST
		 * actors are auto-flagged AF_NO_RENDER_CAPTURE in
		 * lactcust_Alloc_Custom_Actor, so no explicit hide call here. */
		close_actor = lactcust_Alloc_Custom_Actor(NULL, &frame, 0, 0, -200);
		lactor_Set_Actor_User_Function(close_actor, user_Close);
		lactor_Set_Actor_Draw_Function(close_actor, draw_Close);

		/* Set the film's default palette from the scene head */
		lfilm_Set_Film_Def_Palette(tielogo_film, t->scene_head->def_palette);

		/* Install view update callback and push the modal view */
		lview_Set_View_Update_Function(end_View);

		if (lcursor_Is_Cursor_Visible())
			lcursor_Hide_Cursor();

		/* Tag the scene for the HD compositor: bundle key
		 * (TIELOGO, logo) resolves the remaster manifest at
		 * <root>/TIELOGO/films/logo/. FULL_FRAME matches PLAY1's
		 * choice for film cutscenes — the cutscene RT clears each
		 * frame so live film actors and sticky stamps together
		 * describe the full HD picture without trail accumulation.
		 * Cleared by shell.c at the next scene transition. */
		TieSnapshotBuilder_SetActiveFilm("TIELOGO", "logo");
		TieSnapshotBuilder_SetRedrawModel(TIE_REDRAW_FULL_FRAME);
		TieSnapshotBuilder_SetSceneKind(TIE_SCENE_CUTSCENE);

		lviewadd_Push_Handle_View_Task();

		t->phase = TIELOGO_PHASE_CLEANUP;
		return LANDRU_TASK_STEP_CONTINUE;
	}

	/* CLEANUP — view popped */
	Rect frame;
	lview_Clear_View_Update_Function();
	lview_Enable_All_View_Erase();

	lcanvas_Get_Drawing_Canvas_Bounds(&frame);
	lview_Set_View_Frame(0, &frame);
	lview_Set_View_Pos(0, frame.left, frame.top);

	lbitmap_Free_Bitmap(&background);
	lres_Close_Resource(t->res_file);

	/* Drop the sticky stamp list — TieRecoveredLogo_CaptureSnapshot will be a
	 * no-op for whatever scene runs next until another tielogo push
	 * repopulates it. The active_film tag is reset by shell.c. */
	s_stamp_count = 0;
	fighter2_actor = NULL;

	ltimer_Set_Frame_Rate(20);

	return LANDRU_TASK_STEP_DONE;
}

static const LandruTaskVtable tielogo_task_vt = {
	.step = tielogo_task_step,
};

void tielogo_Push_TieLogo_Task(SceneHeadStruct* scene_head) {
	TielogoTask* t = (TielogoTask*)landru_task_push(&tielogo_task_vt);
	if (!t)
		return;
	t->scene_head = scene_head;
	t->res_file = NULL;
	t->phase = TIELOGO_PHASE_BEGIN;
}
