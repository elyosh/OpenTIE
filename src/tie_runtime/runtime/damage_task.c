#include "tie_runtime/runtime/damage_task.h"
#include "tie/damage.h"
#include "tie/user.h"
#include "tie_runtime/runtime/flight_requests.h"
#include <landru/task.h>

static LandruTaskStepResult damage_step(void* self) {
	DamageRoomState* task = self;
	int32_t result = damage_damageroom();
	if (task->finished) {
		TieFlightRequest_SetSubmodalResult(result);
		return LANDRU_TASK_STEP_DONE;
	}
	return LANDRU_TASK_STEP_CONTINUE;
}
static const LandruTaskVtable damage_vtable = { .step = damage_step };
void TieDamage_Begin(void) {
	if (!landru_task_push(&damage_vtable))
		return;
	damage_damageroom();
}
