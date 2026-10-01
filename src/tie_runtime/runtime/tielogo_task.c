#include "tie_runtime/runtime/tielogo_task.h"
#include "tie/tielogo.h"
#include "tie_runtime/snapshot/logo.h"
#include "tie_runtime/snapshot/snapshot_internal.h"

#include <landru/bitmap.h>
#include <landru/canvas.h>
#include <landru/error.h>
#include <landru/paragrp.h>
#include <landru/task.h>
#include <landru/timer.h>
#include <landru/view.h>
#include <landru/viewadd.h>

typedef struct TielogoTask {
	SceneHeadStruct* scene_head;
	ResFile* resource;
	bool started;
} TielogoTask;

static LandruTaskStepResult tielogo_step(void* self) {
	TielogoTask* task = self;
	if (task->started)
		return LANDRU_TASK_STEP_DONE;
	task->started = true;
	/* Film loading invokes callbacks that can capture the first stamps. */
	TieLogoSnapshot_Reset();
	tielogo_TieLogo(task->scene_head);
	return LANDRU_TASK_STEP_CONTINUE;
}

static void tielogo_end(void* self) {
	TielogoTask* task = self;
	Rect frame;
	if (!task->started)
		return;
	xview_Clear_View_Update_Function();
	xview_Enable_All_View_Erase();
	xcanvas_Get_Drawing_Canvas_Bounds(&frame);
	xview_Set_View_Frame(0, &frame);
	xview_Set_View_Pos(0, frame.left, frame.top);
	xbm_Free_Bitmap(&tielogo_background);
	if (task->resource)
		xres_Close_Resource(task->resource);
	fighter2_actor = NULL;
	xtimer_Set_Frame_Rate(20);
	TieLogoSnapshot_Reset();
}

static const LandruTaskVtable tielogo_vtable = { tielogo_step, tielogo_end, NULL, NULL };

void TieLogo_RunView(ResFile* resource, bool ready) {
	TielogoTask* task = landru_task_top();
	task->resource = resource;
	if (!ready) {
		xerror_Set_Landru_Error(6);
		return;
	}
	xactor_Set_Actor_Render_Capture_Hidden(fighter_actor, true);
	xactor_Set_Actor_Render_Capture_Hidden(fighter2_actor, true);
	TieSnapshotBuilder_SetActiveFilm("TIELOGO", "logo");
	TieSnapshotBuilder_SetRedrawModel(TIE_REDRAW_FULL_FRAME);
	TieSnapshotBuilder_SetSceneKind(TIE_SCENE_CUTSCENE);
	xviewadd_Push_Handle_View_Task();
}

void TieLogo_Begin(SceneHeadStruct* scene_head) {
	TielogoTask* task = landru_task_push(&tielogo_vtable);
	if (!task)
		return;
	task->scene_head = scene_head;
}
