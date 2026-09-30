#include "tie_runtime/runtime/help_task.h"
#include "tie/flight_surface_tie98.h"
#include "tie/frontend_display_tie98.h"
#include "tie/help.h"
#include "tie/user.h"
#include "tie_runtime/display/classic_display.h"

#include <landru/task.h>

typedef enum {
	HELP_PHASE_RENDER = 0,
	HELP_PHASE_POLL,
} HelpPhase;

typedef struct HelpTask {
	HelpRoomState room;
	HelpPhase phase;
} HelpTask;

// ORIGINAL_FUNCTION: TIE95 0x2C7F0
// ORIGINAL_FUNCTION: TIE98 0x42F390
// (task-split recovery)
static LandruTaskStepResult help_task_step(void* self) {
	HelpTask* t = (HelpTask*)self;

	if (t->phase == HELP_PHASE_RENDER) {
		const bool tie98_display = TieClassicDisplay_UsesDx5();
		if (tie98_display)
			FlightSurface_Lock();
		help_render_rows(&t->room);
		if (tie98_display) {
			FlightSurface_Unlock();
			FrontendDisplay_BlitOffscreenToRenderSurface();
			FrontendDisplay_PresentFrame();
		}
		t->phase = HELP_PHASE_POLL;
		return LANDRU_TASK_STEP_CONTINUE;
	}

	int r = help_poll_once(&t->room);
	if (r == 1) {
		user_submodal_result = (int32_t)t->room.page_delta;
		return LANDRU_TASK_STEP_DONE;
	}
	if (r == 2)
		t->phase = HELP_PHASE_RENDER;
	return LANDRU_TASK_STEP_CONTINUE;
}

static const LandruTaskVtable help_task_vt = {
	.step = help_task_step,
};

void TieHelp_Begin(int32_t start_right_col) {
	HelpRoomState room;
	HelpTask* t;

	const bool tie98_display = TieClassicDisplay_UsesDx5();
	if (tie98_display)
		FlightSurface_Lock();
	help_OpenRoom(&room, start_right_col);
	if (tie98_display)
		FlightSurface_Unlock();
	t = (HelpTask*)landru_task_push(&help_task_vt);
	if (!t)
		return;
	t->room = room;
	t->phase = HELP_PHASE_RENDER;
}
