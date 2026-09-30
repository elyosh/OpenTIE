#include "tie_runtime/runtime/debrief_task.h"
#include "tie/debrief.h"
#include "tie_runtime/runtime/profile.h"

#include <landru/task.h>
#include <landru/viewadd.h>

typedef struct DebriefTask {
	SceneHeadStruct* scene_head;
	ResFile* resource;
	bool svga;
	bool view_started;
} DebriefTask;

static LandruTaskStepResult debrief_step(void* self) {
	DebriefTask* task = self;
	if (!task->view_started) {
		task->resource = debrief_OpenScene(task->scene_head, task->svga);
		xviewadd_Push_Handle_View_Task();
		task->view_started = true;
		return LANDRU_TASK_STEP_CONTINUE;
	}
	debrief_CloseScene(task->resource, task->svga);
	return LANDRU_TASK_STEP_DONE;
}

static const LandruTaskVtable debrief_vtable = { debrief_step, NULL, NULL, NULL };

void TieDebrief_Begin(SceneHeadStruct* scene_head) {
	DebriefTask* task = landru_task_push(&debrief_vtable);
	if (!task)
		return;
	task->scene_head = scene_head;
	task->resource = NULL;
	task->svga = TieProfile_FrontendId() == TIE_FRONTEND_PROFILE_TIE98;
	task->view_started = false;
}
