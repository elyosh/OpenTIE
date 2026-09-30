#include "tie_runtime/runtime/replay_save_task.h"
#include "tie/feinput.h"
#include "tie/festring.h"
#include "tie/flight_surface_tie98.h"
#include "tie/frontend_display_tie98.h"
#include "tie/msg_templates.h"
#include "tie/replay.h"
#include "tie/tie.h"
#include "tie_runtime/audio/imuse_session.h"
#include "tie_runtime/display/classic_display.h"
#include "tie_runtime/input/input.h"
#include "tie_runtime/storage/storage.h"
#include <imuse/lolevel.h>
#include <landru/task.h>
#include <string.h>

static LandruTaskStepResult replay_save_task_step(void* self) {
	ReplaySaveTask* t = self;
	const bool tie98_display = TieClassicDisplay_UsesDx5();
	if (t->phase == REPLAY_SAVE_PHASE_BEGIN) {
		if (replaymusic == 1) {
			replaymusic = 0;
			replayvolume = (int16_t)imuse_get_master_vol(im);
			imuse_set_master_vol(im, 0);
			imuse_pause(im);
		}
		if (tie98_display) {
			FrontendDisplay_PresentFrame();
			FrontendDisplay_PresentFrontSurface();
			g_flightDrawToOffscreenSurface = 0;
			t->front_surface_route = 1;
			FlightSurface_Lock();
		}
		replay_drawreplaybutton(7);
	}
	if (t->phase != REPLAY_SAVE_PHASE_FINISH) {
		uint16_t result = replay_savereplay();
		if (result == TIE_REPLAY_SAVE_PENDING)
			return t->waiting ? LANDRU_TASK_STEP_YIELD : LANDRU_TASK_STEP_CONTINUE;
		t->result = result;
		t->phase = REPLAY_SAVE_PHASE_FINISH;
		return LANDRU_TASK_STEP_CONTINUE;
	}
	if (t->front_surface_route) {
		g_flightDrawToOffscreenSurface = 1;
		t->front_surface_route = 0;
		FlightSurface_Lock();
	}
	replay_replaymessage(t->result);
	if (!tie98_display)
		replay_drawreplaybutton(6);
	replay_outputclipname();
	if (tie98_display)
		FlightSurface_Unlock();
	return LANDRU_TASK_STEP_DONE;
}

static void replay_save_task_end(void* self) {
	ReplaySaveTask* t = (ReplaySaveTask*)self;
	/* PORT: forced task-stack teardown must not leave the recovered renderer
	 * routed to the TIE98 front surface or retain modal text state. */
	if (t->front_surface_route)
		g_flightDrawToOffscreenSurface = 1;
	if (t->editor_active) {
		festring_setautofill(0);
		festring_setfontsize(2);
	}
}

static const LandruTaskVtable replay_save_task_vt = {
	.step = replay_save_task_step,
	.end = replay_save_task_end,
};

bool TieReplaySave_Begin(void) {
	ReplaySaveTask* t = (ReplaySaveTask*)landru_task_push(&replay_save_task_vt);
	if (!t)
		return false;
	memset(t, 0, sizeof *t);
	t->phase = REPLAY_SAVE_PHASE_BEGIN;
	return true;
}
