#include "tie_runtime/runtime/talk_task.h"
#include "tie/talk.h"
#include <landru/task.h>
#include <landru/viewadd.h>

typedef struct TalkTask {
	SceneHeadStruct* head;
	ResFile* resource;
	bool opened;
} TalkTask;

static LandruTaskStepResult talk_step(void* self) {
	TalkTask* task = self;
	if (!task->opened) {
		task->resource = talk_OpenScene(task->head);
		xviewadd_Push_Handle_View_Task();
		task->opened = true;
		return LANDRU_TASK_STEP_CONTINUE;
	}
	talk_CloseScene(task->resource);
	return LANDRU_TASK_STEP_DONE;
}

static const LandruTaskVtable talk_vtable = { .step = talk_step };

void TieTalk_Begin(SceneHeadStruct* head) {
	TalkTask* task = landru_task_push(&talk_vtable);
	if (!task)
		return;
	task->head = head;
	task->resource = NULL;
	task->opened = false;
}
