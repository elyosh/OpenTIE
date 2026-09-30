#include "tie_runtime/runtime/goals_task.h"
#include "tie/goals.h"
#include "tie/user.h"
#include <landru/task.h>

static LandruTaskStepResult goals_step(void* self) {
	GoalsRoomState* task = self;
	int32_t result = goals_missiongoalsroom();
	if (task->finished) {
		user_submodal_result = result;
		return LANDRU_TASK_STEP_DONE;
	}
	return LANDRU_TASK_STEP_CONTINUE;
}
static const LandruTaskVtable goals_vtable = { .step = goals_step };
void TieGoals_Begin(void) {
	if (!landru_task_push(&goals_vtable))
		return;
	goals_missiongoalsroom();
}
