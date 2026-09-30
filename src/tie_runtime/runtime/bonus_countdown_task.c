#include "tie_runtime/runtime/bonus_countdown_task.h"
#include "tie/flight_surface_tie98.h"
#include "tie/frontend_display_tie98.h"
#include "tie/fsfx.h"
#include "tie/gate.h"
#include "tie/msg.h"
#include "tie/msg_templates.h"
#include "tie/tie.h"
#include "tie/xtimer.h"
#include "tie_runtime/display/classic_display.h"
#include <landru/task.h>

/* The host advances the simulation clock between runtime ticks, so the
 * countdown must yield instead of waiting synchronously for its next tick. */

typedef struct BonusCountdownTask {
	uint16_t tickbudget; /* accumulated PIT ticks since last decrement */
} BonusCountdownTask;

static LandruTaskStepResult bonus_countdown_step(void* self) {
	BonusCountdownTask* t = (BonusCountdownTask*)self;

	if (!mtimer_min && !mtimer_sec) {
		argtable[0] = (uint16_t)mission.train_bonus;
		msg_messageprintf(MSG_BONUS_AWARDED);
		if (TieClassicDisplay_UsesDx5())
			FlightSurface_Lock();
		gate_settraininglevel(++mission.train_level);
		if (TieClassicDisplay_UsesDx5())
			FlightSurface_Unlock();
		bonus_countdown_active = 0;
		return LANDRU_TASK_STEP_DONE;
	}

	t->tickbudget = (uint16_t)(t->tickbudget + (uint16_t)xtimer_time_elapsed());
	if (t->tickbudget < 4)
		return LANDRU_TASK_STEP_YIELD; /* xtimer cursor advances between tie_ticks */
	t->tickbudget = 0;

	/* One iteration of the original loop body. */
	if (mtimer_sec) {
		--mtimer_sec;
	} else {
		mtimer_sec = 59;
		--mtimer_min;
	}

	mission.mission_score += 10;
	mission.train_bonus += 10;

	if ((mission.mission_score % 100) == 0)
		fsfx_triggersfx(0x21, 0xFFFF);

	if (TieClassicDisplay_UsesDx5()) {
		g_flightDrawToOffscreenSurface = 0;
		FlightSurface_Lock();
		gate_updatebonuspoints();
		FlightSurface_Unlock();
		g_flightDrawToOffscreenSurface = 1;
		FrontendDisplay_PresentFrame();
	} else {
		gate_updatebonuspoints();
	}
	return LANDRU_TASK_STEP_CONTINUE;
}

static const LandruTaskVtable bonus_countdown_task_vt = {
	.step = bonus_countdown_step,
};

void TieBonusCountdown_Begin(void) {
	BonusCountdownTask* t = (BonusCountdownTask*)landru_task_push(&bonus_countdown_task_vt);
	if (!t)
		return;
	t->tickbudget = 0;
	bonus_countdown_active = 1;
}
