#include "tie_runtime/runtime/combat_task.h"
#include "tie/combat.h"

#include <landru/task.h>
#include <landru/viewadd.h>

typedef struct CombatTask {
	SceneHeadStruct* scene_head;
	bool view_started;
} CombatTask;

static LandruTaskStepResult combat_step(void* self) {
	CombatTask* task = self;
	if (!task->view_started) {
		combat_OpenScene(task->scene_head);
		xviewadd_Push_Handle_View_Task();
		task->view_started = true;
		return LANDRU_TASK_STEP_CONTINUE;
	}
	combat_CloseScene();
	return LANDRU_TASK_STEP_DONE;
}

static const LandruTaskVtable combat_vtable = { combat_step, NULL, NULL, NULL };

void TieCombat_Begin(SceneHeadStruct* scene_head) {
	CombatTask* task = landru_task_push(&combat_vtable);
	if (!task)
		return;
	task->scene_head = scene_head;
	task->view_started = false;
}
