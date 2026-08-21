
#include <landru/actor.h>
#include <landru/canvas.h>
#include <landru/dialog.h>
#include <landru/dirty.h>
#include <landru/error.h>
#include <landru/film.h>
#include <landru/input.h>
#include <landru/io.h>
#include <landru/pal.h>
#include <landru/rect.h>
#include <landru/sound.h>
#include <landru/task.h>
#include <landru/timer.h>
#include <landru/view.h>
#include <landru/viewadd.h>

#include "host_internal.h"

bool lviewadd_Clip_Object_To_View(int16_t viewId, int16_t zPlane, Rect* actFrame, Rect* outFull,
								  Rect* outClipped) {
	if (zPlane < view_gbl->zstart[viewId] || zPlane > view_gbl->zstop[viewId])
		return false;

	int16_t xOffset = view_gbl->frame[viewId].left - view_gbl->rel_x[viewId];
	int16_t yOffset = view_gbl->frame[viewId].top - view_gbl->rel_y[viewId];
	lrect_Copy_Rect(outFull, actFrame);
	lrect_Offset_Rect(outFull, xOffset, yOffset);
	lrect_Copy_Rect(outClipped, outFull);

	Rect clipFrame;
	lview_Get_View_Clip_Frame(viewId, &clipFrame);
	lrect_Clip_Rect(outClipped, &clipFrame);
	return !lrect_Empty_Rect(outClipped);
}

/* ------------------------------------------------------------------
 * Front-end view loop — fully task-driven.
 *
 * Pushed via lviewadd_Push_Handle_View_Task. The body decomposes
 * cleanly into Begin/Step/End because the iteration is stateless
 * apart from view_gbl: the loop just re-runs the update / erase /
 * draw / wait pipeline until the Landru "running" flag goes false.
 *
 * Sub-dialog opens from inside the view-update callback or input
 * callbacks go through ldialog_Schedule_Sub_Dialog; the channel is
 * drained at end of DRAW + AFTER_FRAME_FADE, the sub-dialog is
 * pushed on top with a handler invoked when it pops. */

typedef enum {
	VIEW_PHASE_DRAW = 0,
	VIEW_PHASE_WAIT,
	VIEW_PHASE_AFTER_FRAME_FADE,
	VIEW_PHASE_AFTER_FRAME_PRESENT,
	VIEW_PHASE_AFTER_SUB, /* sub-dialog popped; invoke handler */
} ViewAddPhase;

typedef struct ViewAddTask {
	ViewAddPhase phase;
	DialogSubResultHandler sub_handler;
	void* sub_ctx;
} ViewAddTask;

static LandruTaskStepResult viewadd_task_step(void* self) {
	ViewAddTask* t = (ViewAddTask*)self;

	if (!lerror_Is_Landru_Running())
		return LANDRU_TASK_STEP_DONE;

	if (t->phase == VIEW_PHASE_WAIT) {
		/* Yield until the per-scene frame budget elapses. YIELD (not
		 * CONTINUE) so landru_task_run_frame returns to the application
		 * and its clock can advance before the next frame. */
		if (ltimer_Frame_Budget_Pending())
			return LANDRU_TASK_STEP_YIELD;
		ltimer_Commit_Frame();
		lcanvas_Push_Fade_Screen_To_Video_Task(0);
		t->phase = VIEW_PHASE_AFTER_FRAME_FADE;
		return LANDRU_TASK_STEP_CONTINUE;
	}

	if (t->phase == VIEW_PHASE_AFTER_FRAME_FADE) {
		/* WAIT pushed a fade; FadeTask has popped (else we wouldn't
		 * be running). Run the post-fade per-frame work, drain any
		 * sub-dialog request the view-update callback staged, and
		 * return to DRAW (or AFTER_SUB if a sub-dialog was pushed).
		 * The view-update callback is the only callback in this
		 * pipeline that may legitimately schedule a sub-dialog
		 * (e.g. filmview's end_View at the penultimate cel). */
		if (!view_gbl->step) {
			lpal_Cycle_Screen();
			if (view_gbl->update)
				view_gbl->update(view_gbl->time);
			view_gbl->time++;
		}
		landru_host_video_copy_to_present_surface();
		landru_host_video_present();
		t->phase = VIEW_PHASE_AFTER_FRAME_PRESENT;
		if (landru_host_has_platform_video())
			return LANDRU_TASK_STEP_FRAME_COMPLETE;
	}

	if (t->phase == VIEW_PHASE_AFTER_FRAME_PRESENT) {
		if (ldialog_Try_Push_Pending_Sub_Dialog(&t->sub_handler, &t->sub_ctx)) {
			t->phase = VIEW_PHASE_AFTER_SUB;
			return LANDRU_TASK_STEP_FRAME_COMPLETE;
		}
		t->phase = VIEW_PHASE_DRAW;
		return LANDRU_TASK_STEP_FRAME_COMPLETE;
	}

	if (t->phase == VIEW_PHASE_AFTER_SUB) {
		/* Sub-dialog popped — invoke handler, return to DRAW. */
		ldialog_Invoke_Sub_Handler(t->sub_handler, t->sub_ctx);
		t->sub_handler = NULL;
		t->sub_ctx = NULL;
		t->phase = VIEW_PHASE_DRAW;
		return LANDRU_TASK_STEP_CONTINUE;
	}

	/* DRAW phase — runs once per frame; transitions to WAIT at the end. */
	lio_Poll_Input();

	if (!view_gbl->step) {
		int32_t time = view_gbl->time;
		lview_Move_View();
		ltimer_Often();
		linput_Update_System_Inputs();
		ltimer_Often();
		lfilm_Update_Films(time);
		ltimer_Often();
		lactor_Update_Actors(time);
		ltimer_Often();

		int32_t time2 = view_gbl->time;
		linput_User_System_Inputs(time2);
		ltimer_Often();
		lfilm_User_Films(time2);
		lfilm_Check_Film_ZPlanes();
		ltimer_Often();
		lactor_User_Actors(time2);
		lactor_Check_Actor_ZPlanes();
		ltimer_Often();
		lsound_User_Sounds(time2);
		landru_host_frontend_audio_pump();
		lsound_Free_User_Sounds(lsound_Ask_Sound_List());
		ltimer_Often();
	}

	/* Drain any sub-dialog request a system-input callback staged
	 * during this DRAW pass before continuing to erase/draw. */
	if (ldialog_Try_Push_Pending_Sub_Dialog(&t->sub_handler, &t->sub_ctx)) {
		t->phase = VIEW_PHASE_AFTER_SUB;
		return LANDRU_TASK_STEP_CONTINUE;
	}

	/* Erase phase */
	if (view_gbl->clear) {
		lcanvas_Erase_Canvas_Rect(&view_gbl->clip_frame);
	} else {
		for (int i = 0; i < 4; i++) {
			if (lview_Is_View_Erase(i)) {
				Rect rect;
				lview_Get_View_Clip_Frame(i, &rect);
				lcanvas_Erase_Canvas_Rect(&rect);
				ltimer_Often();
			}
		}
	}

	/* Draw phase */
	int16_t refresh = view_gbl->refresh_world;
	if (refresh)
		ldirty_Max_Dirty_List();
	lfilm_Draw_Films(refresh);
	ltimer_Often();
	lactor_Draw_Actors(refresh);
	ltimer_Often();
	linput_Draw_System_Inputs(refresh);
	ltimer_Often();
	ldialog_Draw_System_Dialogs(refresh);
	ltimer_Often();
	view_gbl->refresh_world = 0;

	lviewadd_Draw_View_Debug(view_gbl->time);

	t->phase = VIEW_PHASE_WAIT;
	return LANDRU_TASK_STEP_CONTINUE;
}

static uint64_t viewadd_task_next_wake_delay_us(const void* self) {
	const ViewAddTask* t = (const ViewAddTask*)self;
	if (t->phase == VIEW_PHASE_DRAW || t->phase == VIEW_PHASE_WAIT ||
		t->phase == VIEW_PHASE_AFTER_FRAME_PRESENT)
		return ltimer_Next_Frame_Delay_Us();
	return 0;
}

static void viewadd_task_end(void* self) {
	(void)self;
	lview_Free_All_From_View(view_gbl);
	lview_Init_View(view_gbl);
	lpal_Stop_All_Cycles();
}

static const LandruTaskVtable viewadd_task_vt = {
	.step = viewadd_task_step,
	.end = viewadd_task_end,
	.next_wake_delay_us = viewadd_task_next_wake_delay_us,
};

void lviewadd_Push_Handle_View_Task(void) {
	ViewAddTask* t = (ViewAddTask*)landru_task_push(&viewadd_task_vt);
	if (!t)
		return;
	t->phase = VIEW_PHASE_DRAW;
	t->sub_handler = NULL;
	t->sub_ctx = NULL;

	view_gbl->time = 0;
	view_gbl->step = 0;
	view_gbl->stepCount = 0;
	lio_Flush_Input();
}

void lviewadd_Clear_View(void) {
	if (view_gbl->clear) {
		lcanvas_Erase_Canvas_Rect(&view_gbl->clip_frame);
	} else {
		for (int i = 0; i < 4; i++) {
			if (lview_Is_View_Erase(i)) {
				Rect rect;
				lview_Get_View_Clip_Frame(i, &rect);
				lcanvas_Erase_Canvas_Rect(&rect);
				ltimer_Often();
			}
		}
	}
}

void lviewadd_Draw_View(int16_t refresh) {
	if (refresh)
		ldirty_Max_Dirty_List();
	lfilm_Draw_Films(refresh);
	ltimer_Often();
	lactor_Draw_Actors(refresh);
	ltimer_Often();
	linput_Draw_System_Inputs(refresh);
	ltimer_Often();
	ldialog_Draw_System_Dialogs(refresh);
	ltimer_Often();
}

void lviewadd_Draw_View_Under_Dialog(void) {
	lfilm_Draw_Films(1);
	ltimer_Often();
	lactor_Draw_Actors(1);
	ltimer_Often();
	linput_Draw_System_Inputs(1);
	ltimer_Often();
}

void lviewadd_Draw_View_Debug(int32_t time) {
	/* Reset the canvas clip to the full bitmap before the per-frame
	 * Fade_Screen_To_Video. The actor draw phase leaves bm->clip set to
	 * the last visible actor's view clip; without this reset, both
	 * Cursor_To_Back's canvas draw and the dirty-rect push would be
	 * constrained to that small rect. The binary does this here as the
	 * canonical end-of-draw-phase clip restore. */
	lcanvas_Max_Drawing_Canvas_Clip();
	(void)time;
}

void lviewadd_Update_View(int32_t time) {
	lview_Move_View();
	ltimer_Often();
	linput_Update_System_Inputs();
	ltimer_Often();
	lfilm_Update_Films(time);
	ltimer_Often();
	lactor_Update_Actors(time);
	ltimer_Often();
}

void lviewadd_User_View(int32_t time) {
	linput_User_System_Inputs(time);
	ltimer_Often();
	lfilm_User_Films(time);
	lfilm_Check_Film_ZPlanes();
	ltimer_Often();
	lactor_User_Actors(time);
	lactor_Check_Actor_ZPlanes();
	ltimer_Often();
	lsound_User_Sounds(time);
	landru_host_frontend_audio_pump();
	lsound_Free_User_Sounds(lsound_Ask_Sound_List());
	ltimer_Often();
}

void lviewadd_Update_End_View(void) {
	if (view_gbl->update)
		view_gbl->update(view_gbl->time);
}
