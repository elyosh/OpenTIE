#include "tie_runtime/runtime/goals_task.h"
#include "tie/flight_surface_tie98.h"
#include "tie/frontend_display_tie98.h"
#include "tie/goals.h"
#include "tie/user.h"
#include "tie_runtime/display/classic_display.h"
#include <landru/task.h>

typedef enum {
	GOALS_PHASE_RENDER = 0,
	GOALS_PHASE_POLL,
} GoalsPhase;

typedef struct GoalsTask {
	GoalsRoomState room;
	GoalsPhase phase;
} GoalsTask;

// ORIGINAL_FUNCTION: TIE95 0x2AD90
// ORIGINAL_FUNCTION: TIE98 0x42DCA0
// (task-split recovery)
static LandruTaskStepResult goals_task_step(void* self) {
	GoalsTask* t = (GoalsTask*)self;

	if (t->phase == GOALS_PHASE_RENDER) {
		const bool tie98_display = TieClassicDisplay_UsesDx5();
		if (tie98_display)
			FlightSurface_Lock();
		goals_render_page(t->room.scroll_y, &t->room.content_height);
		if (tie98_display) {
			FlightSurface_Unlock();
			FrontendDisplay_BlitOffscreenToRenderSurface();
			FrontendDisplay_PresentFrame();
		}
		t->phase = GOALS_PHASE_POLL;
		return LANDRU_TASK_STEP_CONTINUE;
	}

	int r = goals_poll_once(&t->room);
	if (r == 1) {
		/* TIE95 returns the 16-bit room result zero-extended in EAX. */
		user_submodal_result = (uint16_t)t->room.nav_code;
		return LANDRU_TASK_STEP_DONE;
	}
	if (r == 2)
		t->phase = GOALS_PHASE_RENDER;
	return LANDRU_TASK_STEP_CONTINUE;
}

static const LandruTaskVtable goals_task_vt = {
	.step = goals_task_step,
};

void TieGoals_Begin(void) {
	GoalsRoomState room;
	GoalsTask* t;

	goals_OpenRoom(&room);
	t = (GoalsTask*)landru_task_push(&goals_task_vt);
	if (!t)
		return;
	t->room = room;
	t->phase = GOALS_PHASE_RENDER;
}
