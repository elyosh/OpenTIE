#include "tie_runtime/runtime/credits_task.h"
#include "tie/credits.h"
#include "tie_runtime/runtime/profile.h"
#include "tie_runtime/snapshot/snapshot_internal.h"

#include <landru/error.h>
#include <landru/surface.h>
#include <landru/task.h>
#include <landru/viewadd.h>

typedef struct CreditsTask {
	SceneHeadStruct* scene_head;
	CreditsSceneResources resources;
	LandruSurfaceSet previous_surface;
	bool started;
	bool restore_surface;
} CreditsTask;

// ORIGINAL_FUNCTION: TIE95 0x71090
// ORIGINAL_FUNCTION: TIE98 0x414620
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
	if (!credits_OpenScene(task->scene_head, &task->resources, tie98)) {
		xerror_Set_Landru_Error(6);
		return LANDRU_TASK_STEP_DONE;
	}
	TieSnapshotBuilder_SetActiveFilm("CREDITS", "credits");
	xviewadd_Push_Handle_View_Task();
	return LANDRU_TASK_STEP_CONTINUE;
}

static void credits_end(void* self) {
	CreditsTask* task = self;
	if (task->started)
		credits_CloseScene(&task->resources);
	if (task->restore_surface)
		xsurface_Select_Surface_Set(task->previous_surface);
}

static const LandruTaskVtable credits_vtable = { credits_step, credits_end, NULL, NULL };

void TieCredits_Begin(SceneHeadStruct* scene_head) {
	CreditsTask* task = landru_task_push(&credits_vtable);
	if (!task)
		return;
	task->scene_head = scene_head;
}
