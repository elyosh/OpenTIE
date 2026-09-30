#include "tie_runtime/runtime/wingman_task.h"
#include "tie/user.h"
#include "tie/wingman.h"
#include "tie_runtime/runtime/flight_requests.h"
#include <landru/task.h>

static LandruTaskStepResult wingman_step(void* self) {
	WingmanRoomState* task = self;
	int32_t result = wingman_wingmanroom();
	if (task->finished) {
		TieFlightRequest_SetSubmodalResult(result);
		return LANDRU_TASK_STEP_DONE;
	}
	return LANDRU_TASK_STEP_CONTINUE;
}
static const LandruTaskVtable wingman_vtable = { .step = wingman_step };
void TieWingman_Begin(void) {
	if (!landru_task_push(&wingman_vtable))
		return;
	wingman_wingmanroom();
}
