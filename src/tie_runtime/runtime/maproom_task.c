#include "tie_runtime/runtime/maproom_task.h"
#include "tie/maproom.h"
#include "tie/user.h"
#include "tie_runtime/runtime/flight_requests.h"
#include <landru/task.h>

static LandruTaskStepResult maproom_step(void* self) {
	MaproomState* task = self;
	int32_t result = maproom_maproom();
	if (task->finished) {
		TieFlightRequest_SetSubmodalResult(result);
		return LANDRU_TASK_STEP_DONE;
	}
	return task->waiting ? LANDRU_TASK_STEP_YIELD : LANDRU_TASK_STEP_CONTINUE;
}
static const LandruTaskVtable maproom_vtable = { .step = maproom_step };
void TieMaproom_Begin(void) {
	if (!landru_task_push(&maproom_vtable))
		return;
	maproom_maproom();
}
