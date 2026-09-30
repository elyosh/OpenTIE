#include "tie_runtime/runtime/replay_session_task.h"
#include "tie/replayio.h"
#include <landru/task.h>
static LandruTaskStepResult replayio_step(void* self) {
	ReplayioTask* task = self;
	replayio_replayscreen();
	return task->finished ? LANDRU_TASK_STEP_DONE : LANDRU_TASK_STEP_CONTINUE;
}
static const LandruTaskVtable replayio_vtable = { .step = replayio_step };
void TieReplaySession_Begin(void) {
	if (!landru_task_push(&replayio_vtable))
		return;
	replayio_replayscreen();
}
