#include "tie_runtime/runtime/msgroom_task.h"
#include "tie/flight_surface_tie98.h"
#include "tie/frontend_display_tie98.h"
#include "tie/msgroom.h"
#include "tie/user.h"
#include "tie_runtime/display/classic_display.h"
#include <landru/task.h>

typedef enum {
	MSGROOM_PHASE_RENDER = 0,
	MSGROOM_PHASE_POLL,
} MsgRoomPhase;

typedef struct MsgRoomTask {
	MsgRoomRoomState room;
	MsgRoomPhase phase;
} MsgRoomTask;

// ORIGINAL_FUNCTION: TIE95 0x34340
// ORIGINAL_FUNCTION: TIE98 0x4570C0
// (task-split recovery)
static LandruTaskStepResult msgroom_task_step(void* self) {
	MsgRoomTask* t = (MsgRoomTask*)self;

	if (t->phase == MSGROOM_PHASE_RENDER) {
		const bool tie98_display = TieClassicDisplay_UsesDx5();
		if (tie98_display)
			FlightSurface_Lock();
		msgroom_render_page(t->room.cur_top_idx);
		if (tie98_display) {
			FlightSurface_Unlock();
			FrontendDisplay_BlitOffscreenToRenderSurface();
			FrontendDisplay_PresentFrame();
		}
		t->phase = MSGROOM_PHASE_POLL;
		return LANDRU_TASK_STEP_CONTINUE;
	}

	/* POLL */
	int r = msgroom_poll_once(&t->room);
	if (r == 1) {
		user_submodal_result = (int32_t)t->room.exit_dir;
		return LANDRU_TASK_STEP_DONE;
	}
	if (r == 2)
		t->phase = MSGROOM_PHASE_RENDER;
	return LANDRU_TASK_STEP_CONTINUE;
}

static const LandruTaskVtable msgroom_task_vt = {
	.step = msgroom_task_step,
};

void TieMsgRoom_Begin(void) {
	MsgRoomTask* t = (MsgRoomTask*)landru_task_push(&msgroom_task_vt);
	if (!t)
		return;
	msgroom_OpenRoom(&t->room);
	t->phase = MSGROOM_PHASE_RENDER;
}
