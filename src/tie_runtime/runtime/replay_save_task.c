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

static const char kClipSuffix[] = ".clp";

/* --------------------------------------------------------------------------
 * Modal text entry
 * -------------------------------------------------------------------------- */

typedef enum ReplaySavePhase {
	REPLAY_SAVE_PHASE_BEGIN = 0,
	REPLAY_SAVE_PHASE_EDIT_NAME,
	REPLAY_SAVE_PHASE_CHECK_FILE,
	REPLAY_SAVE_PHASE_CONFIRM_REPLACE,
	REPLAY_SAVE_PHASE_WRITE,
	REPLAY_SAVE_PHASE_FINISH,
} ReplaySavePhase;

typedef struct ReplaySaveTask {
	uint8_t name_input[40];
	char filename[16];
	int16_t prompt_cx;
	int16_t prompt_cy;
	uint16_t result;
	uint8_t position;
	uint8_t editor_active;
	uint8_t front_surface_route;
	ReplaySavePhase phase;
} ReplaySaveTask;

static void replay_save_present_front(void) {
	if (!TieClassicDisplay_UsesDx5())
		return;
	FrontendDisplay_PresentFrontSurface();
	FrontendDisplay_PresentFrame();
}

static void replay_save_draw_editor(const ReplaySaveTask* t) {
	festring_setcursor(t->prompt_cx, t->prompt_cy);
	festring_outstring(t->name_input);
	festring_setbackcolor(0x4A);
	outchar(' ');
	festring_setbackcolor(0x2C);
	outchar('\n');
}

// ORIGINAL_FUNCTION: TIE95 0x476CC
// ORIGINAL_FUNCTION: TIE98 0x474CA0
/* task-split recovery
 * PORT: the original polls inside REPLAY_editstring until Enter. Host input is
 * queued once per application frame, so this state machine consumes one key
 * per step and yields whenever the queue is empty. */
static bool replay_save_edit_name_step(ReplaySaveTask* t) {
	feinput_getinput();

	if (keypress == 1)
		keypress = 8;
	if (keypress == 8 && t->position > 0)
		--t->position;

	if (keypress >= 48) {
		if ((keypress < 65 && keypress >= 58) || (keypress < 97 && keypress >= 91) || (keypress >= 123))
			keypress = 1;
	} else if (keypress != 13 && keypress != 45 && keypress != 0) {
		keypress = 1;
	}

	if (keypress != 1 && keypress != 13 && keypress != 0 && t->position < 8) {
		uint8_t ch = (uint8_t)keypress;
		if (ch >= 'a' && ch <= 'z')
			ch -= 32;
		t->name_input[t->position++] = ch;
	}
	t->name_input[t->position] = 0;

	if (keypress) {
		if (TieClassicDisplay_UsesDx5())
			FlightSurface_Lock();
		replay_save_draw_editor(t);
		if (TieClassicDisplay_UsesDx5())
			FlightSurface_Unlock();
		replay_save_present_front();
	}
	return keypress == 13;
}

static void replay_save_finish_editor(ReplaySaveTask* t) {
	if (TieClassicDisplay_UsesDx5())
		FlightSurface_Lock();
	festring_setcursor(t->prompt_cx, t->prompt_cy);
	festring_outstring(t->name_input);
	outchar('\n');
	if (TieClassicDisplay_UsesDx5())
		FlightSurface_Unlock();
	festring_setautofill(0);
	festring_setfontsize(2);
	t->editor_active = 0;
	replay_save_present_front();
}

static void replay_save_build_filename(ReplaySaveTask* t) {
	size_t n = 0;
	while (n < sizeof(t->filename) - 1 && t->name_input[n]) {
		t->filename[n] = (char)t->name_input[n];
		++n;
	}
	size_t s = 0;
	while (n < sizeof(t->filename) - 1 && kClipSuffix[s])
		t->filename[n++] = kClipSuffix[s++];
	t->filename[n] = '\0';
}

static LandruTaskStepResult replay_save_task_step(void* self) {
	ReplaySaveTask* t = (ReplaySaveTask*)self;
	const bool tie98_display = TieClassicDisplay_UsesDx5();

	switch (t->phase) {
		case REPLAY_SAVE_PHASE_BEGIN:
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
			replay_replaymessage(MSG_ENTER_FILENAME);
			festring_setfontsize(1);
			festring_setautofill(1);
			t->editor_active = 1;
			if (tie_is_high_resolution_flight()) {
				t->prompt_cx = 126;
				t->prompt_cy = 456;
			} else {
				t->prompt_cx = 74;
				t->prompt_cy = 190;
			}
			replay_save_draw_editor(t);
			if (tie98_display)
				FlightSurface_Unlock();
			replay_save_present_front();
			t->phase = REPLAY_SAVE_PHASE_EDIT_NAME;
			return LANDRU_TASK_STEP_CONTINUE;

		case REPLAY_SAVE_PHASE_EDIT_NAME:
			if (!TieInput_KeyPending())
				return LANDRU_TASK_STEP_YIELD;
			if (!replay_save_edit_name_step(t))
				return LANDRU_TASK_STEP_CONTINUE;
			replay_save_finish_editor(t);
			if (!t->name_input[0]) {
				t->result = MSG_REPLAY_NOT_SAVED;
				t->phase = REPLAY_SAVE_PHASE_FINISH;
				return LANDRU_TASK_STEP_CONTINUE;
			}
			replay_save_build_filename(t);
			t->phase = REPLAY_SAVE_PHASE_CHECK_FILE;
			return LANDRU_TASK_STEP_CONTINUE;

		case REPLAY_SAVE_PHASE_CHECK_FILE: {
			TieFile* existing = TieStorage_Open(TIE_FILE_ROOT_USER, t->filename, "rb");
			if (!existing) {
				t->phase = REPLAY_SAVE_PHASE_WRITE;
				return LANDRU_TASK_STEP_CONTINUE;
			}
			TieStorage_Close(existing);
			if (tie98_display)
				FlightSurface_Lock();
			replay_replaymessage(MSG_FILE_REPLACE);
			if (tie98_display)
				FlightSurface_Unlock();
			replay_save_present_front();
			t->phase = REPLAY_SAVE_PHASE_CONFIRM_REPLACE;
			return LANDRU_TASK_STEP_CONTINUE;
		}

		case REPLAY_SAVE_PHASE_CONFIRM_REPLACE: {
			if (!TieInput_KeyPending())
				return LANDRU_TASK_STEP_YIELD;
			const int key = TieInput_ReadKey();
			if (key != 'y' && key != 'Y') {
				t->result = MSG_REPLAY_NOT_SAVED;
				t->phase = REPLAY_SAVE_PHASE_FINISH;
			} else {
				t->phase = REPLAY_SAVE_PHASE_WRITE;
			}
			return LANDRU_TASK_STEP_CONTINUE;
		}

		case REPLAY_SAVE_PHASE_WRITE:
			t->result = replay_savereplay_file(t->name_input, t->filename);
			t->phase = REPLAY_SAVE_PHASE_FINISH;
			return LANDRU_TASK_STEP_CONTINUE;

		case REPLAY_SAVE_PHASE_FINISH:
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
