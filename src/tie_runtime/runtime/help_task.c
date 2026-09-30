#include "tie_runtime/runtime/help_task.h"
#include "tie/help.h"
#include "tie/user.h"
#include "tie_runtime/runtime/flight_requests.h"
#include <landru/task.h>

static LandruTaskStepResult help_step(void* self) {
	HelpRoomState* task = self;
	int32_t result = help_helproom(0);
	if (task->finished) {
		TieFlightRequest_SetSubmodalResult(result);
		return LANDRU_TASK_STEP_DONE;
	}
	return LANDRU_TASK_STEP_CONTINUE;
}
static const LandruTaskVtable help_vtable = { .step = help_step };
void TieHelp_Begin(int32_t start_right_col) {
	if (!landru_task_push(&help_vtable))
		return;
	help_helproom(start_right_col);
}
