#include "tie_runtime/runtime/credits_task.h"
#include "tie/credits.h"
#include "tie_runtime/runtime/profile.h"
#include "tie_runtime/snapshot/snapshot_internal.h"

#include <landru/error.h>
#include <landru/memhdl.h>
#include <landru/paragrp.h>
#include <landru/surface.h>
#include <landru/task.h>
#include <landru/timer.h>
#include <landru/view.h>
#include <landru/viewadd.h>

typedef struct CreditsTask {
	SceneHeadStruct* scene_head;
	ResFile* credit_res;
	ResFile* text_res;
	bool view_configured;
	LandruSurfaceSet previous_surface;
	bool started;
	bool restore_surface;
} CreditsTask;

static LandruTaskStepResult credits_step(void* self) {
	CreditsTask* task = self;
	if (task->started)
		return LANDRU_TASK_STEP_DONE;

	const bool tie98 = TieProfile_FrontendId() == TIE_FRONTEND_PROFILE_TIE98;
	task->previous_surface = xsurface_Get_Surface_Set();
	if (tie98) {
		if (!xsurface_Select_Surface_Set(LANDRU_SURFACE_VGA)) {
			xerror_Set_Landru_Error(6);
			return LANDRU_TASK_STEP_DONE;
		}
		task->restore_surface = true;
	}
	task->started = true;
	credits_Credits(task->scene_head);
	return LANDRU_TASK_STEP_CONTINUE;
}

static void credits_end(void* self) {
	CreditsTask* task = self;
	if (task->started) {
		if (task->view_configured) {
			xtimer_Set_Frame_Rate(20);
			xview_Enable_Global_View_Erase();
			xview_Clear_View_Update_Function();
			task->view_configured = 0;
		}
		xmemhdl_Free_Handle(credits_star_buffer);
		credits_star_buffer = LANDRU_NULL_HANDLE;
		xparagrp_Free_Paragraph(credits_text);
		credits_text = LANDRU_NULL_HANDLE;
		if (task->text_res)
			xres_Close_Resource(task->text_res);
		if (task->credit_res)
			xres_Close_Resource(task->credit_res);
		task->text_res = NULL;
		task->credit_res = NULL;
	}
	if (task->restore_surface)
		xsurface_Select_Surface_Set(task->previous_surface);
}

static const LandruTaskVtable credits_vtable = { credits_step, credits_end, NULL, NULL };

void TieCredits_RunView(ResFile* credit_res, ResFile* text_res, bool ready) {
	CreditsTask* task = landru_task_top();
	task->credit_res = credit_res;
	task->text_res = text_res;
	task->view_configured = ready;
	if (!ready) {
		xerror_Set_Landru_Error(6);
		return;
	}
	TieSnapshotBuilder_SetActiveFilm("CREDITS", "credits");
	xviewadd_Push_Handle_View_Task();
}

void TieCredits_Begin(SceneHeadStruct* scene_head) {
	CreditsTask* task = landru_task_push(&credits_vtable);
	if (!task)
		return;
	task->scene_head = scene_head;
}
