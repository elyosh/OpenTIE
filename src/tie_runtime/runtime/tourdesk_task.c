#include "tie_runtime/runtime/tourdesk_task.h"
#include "tie/tourdesk.h"
#include "tie_runtime/runtime/profile.h"

#include <landru/task.h>
#include <landru/viewadd.h>

typedef struct TourDeskTask {
	SceneHeadStruct* scene_head;
	bool view_started;
	bool svga;
} TourDeskTask;

static LandruTaskStepResult tourdesk_step(void* self) {
	TourDeskTask* task = self;
	if (!task->view_started) {
		tourdesk_OpenScene(task->scene_head, task->svga);
		xviewadd_Push_Handle_View_Task();
		task->view_started = true;
		return LANDRU_TASK_STEP_CONTINUE;
	}
	tourdesk_CloseScene(task->svga);
	return LANDRU_TASK_STEP_DONE;
}

static const LandruTaskVtable tourdesk_vtable = { tourdesk_step, NULL, NULL, NULL };

void TieTourDesk_Begin(SceneHeadStruct* scene_head) {
	TourDeskTask* task = landru_task_push(&tourdesk_vtable);
	if (!task)
		return;
	task->scene_head = scene_head;
	task->view_started = false;
	task->svga = TieProfile_FrontendId() == TIE_FRONTEND_PROFILE_TIE98;
}
