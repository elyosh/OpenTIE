#include "tie_runtime/runtime/wingman_task.h"
#include "tie/flight_surface_tie98.h"
#include "tie/frontend_display_tie98.h"
#include "tie/user.h"
#include "tie/wingman.h"
#include "tie_runtime/display/classic_display.h"

#include <landru/task.h>

typedef enum {
	WINGMAN_PHASE_RENDER = 0,
	WINGMAN_PHASE_POLL,
} WingmanPhase;

typedef struct WingmanTask {
	WingmanRoomState room;
	WingmanPhase phase;
} WingmanTask;

// ORIGINAL_FUNCTION: TIE95 0x61F70
// ORIGINAL_FUNCTION: TIE98 0x499310
// (task-split recovery)
static LandruTaskStepResult wingman_task_step(void* self) {
	WingmanTask* t = (WingmanTask*)self;

	if (t->phase == WINGMAN_PHASE_RENDER) {
		const bool tie98_display = TieClassicDisplay_UsesDx5();
		if (tie98_display)
			FlightSurface_Lock();
		wingman_render_page(t->room.selected_idx);
		if (tie98_display) {
			FlightSurface_Unlock();
			FrontendDisplay_BlitOffscreenToRenderSurface();
			FrontendDisplay_PresentFrame();
		}
		t->phase = WINGMAN_PHASE_POLL;
		return LANDRU_TASK_STEP_CONTINUE;
	}

	int r = wingman_poll_once(&t->room);
	if (r == 1) {
		user_submodal_result = (int32_t)t->room.ret_delta;
		return LANDRU_TASK_STEP_DONE;
	}
	if (r == 2)
		t->phase = WINGMAN_PHASE_RENDER;
	return LANDRU_TASK_STEP_CONTINUE;
}

static const LandruTaskVtable wingman_task_vt = {
	.step = wingman_task_step,
};

void TieWingman_Begin(void) {
	WingmanRoomState room;
	WingmanTask* t;

	wingman_OpenRoom(&room);
	t = (WingmanTask*)landru_task_push(&wingman_task_vt);
	if (!t)
		return;
	t->room = room;
	t->phase = WINGMAN_PHASE_RENDER;
}
