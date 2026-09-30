#include "tie_runtime/runtime/blueprint_task.h"
#include "tie/blueprnt.h"

#include <landru/task.h>
#include <landru/viewadd.h>

typedef struct BlueprintTask {
	SceneHeadStruct* scene_head;
	bool view_started;
} BlueprintTask;

static LandruTaskStepResult blueprnt_step(void* self) {
	BlueprintTask* task = self;
	if (!task->view_started) {
		blueprnt_OpenScene(task->scene_head);
		xviewadd_Push_Handle_View_Task();
		task->view_started = true;
		return LANDRU_TASK_STEP_CONTINUE;
	}
	blueprnt_CloseScene();
	return LANDRU_TASK_STEP_DONE;
}

static const LandruTaskVtable blueprnt_vtable = { blueprnt_step, NULL, NULL, NULL };

void TieBlueprint_Begin(SceneHeadStruct* scene_head) {
	BlueprintTask* task = landru_task_push(&blueprnt_vtable);
	if (!task)
		return;
	task->scene_head = scene_head;
	task->view_started = false;
}
