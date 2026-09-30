#include "tie_runtime/runtime/train_task.h"
#include "tie/train.h"
#include "tie_runtime/runtime/profile.h"

#include <landru/task.h>
#include <landru/viewadd.h>

typedef struct TrainTask {
	SceneHeadStruct* scene_head;
	bool view_started;
	bool svga;
} TrainTask;

static LandruTaskStepResult train_step(void* self) {
	TrainTask* task = self;
	if (!task->view_started) {
		train_OpenScene(task->scene_head, task->svga);
		xviewadd_Push_Handle_View_Task();
		task->view_started = true;
		return LANDRU_TASK_STEP_CONTINUE;
	}
	train_CloseScene(task->svga);
	return LANDRU_TASK_STEP_DONE;
}

static const LandruTaskVtable train_vtable = { train_step, NULL, NULL, NULL };

void TieTrain_Begin(SceneHeadStruct* scene_head) {
	TrainTask* task = landru_task_push(&train_vtable);
	if (!task)
		return;
	task->scene_head = scene_head;
	task->view_started = false;
	task->svga = TieProfile_FrontendId() == TIE_FRONTEND_PROFILE_TIE98;
}
