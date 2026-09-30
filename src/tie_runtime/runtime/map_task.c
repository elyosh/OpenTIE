#include "tie_runtime/runtime/map_task.h"
#include "tie/map.h"
#include <landru/task.h>
#include <landru/viewadd.h>

typedef struct MapTask {
	SceneHeadStruct* head;
	bool opened;
} MapTask;
static LandruTaskStepResult map_step(void* self) {
	MapTask* task = self;
	if (!task->opened) {
		map_OpenScene(task->head);
		xviewadd_Push_Handle_View_Task();
		task->opened = true;
		return LANDRU_TASK_STEP_CONTINUE;
	}
	map_CloseScene();
	return LANDRU_TASK_STEP_DONE;
}
static const LandruTaskVtable map_vtable = { .step = map_step };
void TieMap_Begin(SceneHeadStruct* head) {
	MapTask* task = landru_task_push(&map_vtable);
	if (!task)
		return;
	task->head = head;
	task->opened = false;
}
