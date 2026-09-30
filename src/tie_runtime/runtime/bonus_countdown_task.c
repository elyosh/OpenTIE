#include "tie_runtime/runtime/bonus_countdown_task.h"
#include "tie/gate.h"
#include <landru/task.h>
static LandruTaskStepResult bonus_countdown_step(void* self) {
	BonusCountdownTask* task = self;
	gate_updategateanimations();
	if (!bonus_countdown_active)
		return LANDRU_TASK_STEP_DONE;
	return task->waiting ? LANDRU_TASK_STEP_YIELD : LANDRU_TASK_STEP_CONTINUE;
}
static const LandruTaskVtable bonus_countdown_vtable = { .step = bonus_countdown_step };
void TieBonusCountdown_Begin(void) {
	if (!landru_task_push(&bonus_countdown_vtable))
		return;
	bonus_countdown_active = 1;
}
