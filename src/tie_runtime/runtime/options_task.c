#include "tie_runtime/runtime/options_task.h"
#include "tie/option.h"
#include "tie/user.h"
#include "tie_runtime/runtime/flight_requests.h"
#include <landru/task.h>
static LandruTaskStepResult options_step(void* self) {
	OptionRoomState* task = self;
	int32_t result = option_optionsroom(0);
	if (task->finished) {
		TieFlightRequest_SetSubmodalResult(result);
		return LANDRU_TASK_STEP_DONE;
	}
	return LANDRU_TASK_STEP_CONTINUE;
}
static const LandruTaskVtable options_vtable = { .step = options_step };
void TieOptions_Begin(void) {
	if (!landru_task_push(&options_vtable))
		return;
	option_optionsroom(0);
}
