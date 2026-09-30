#include "tie_runtime/runtime/msgroom_task.h"
#include "tie/msgroom.h"
#include "tie/user.h"
#include "tie_runtime/runtime/flight_requests.h"
#include <landru/task.h>

static LandruTaskStepResult msgroom_step(void* self) {
	MsgRoomRoomState* task = self;
	int32_t result = msgroom_messageroom();
	if (task->finished) {
		TieFlightRequest_SetSubmodalResult(result);
		return LANDRU_TASK_STEP_DONE;
	}
	return LANDRU_TASK_STEP_CONTINUE;
}
static const LandruTaskVtable msgroom_vtable = { .step = msgroom_step };
void TieMsgRoom_Begin(void) {
	if (!landru_task_push(&msgroom_vtable))
		return;
	msgroom_messageroom();
}
