#include "tie_runtime/runtime/flight_requests.h"
#include "tie/feinput.h"
#include "tie/frontend_display_tie98.h"
#include "tie/msg.h"
#include "tie/msg_templates.h"
#include "tie/tie.h"
#include "tie/user.h"
#include "tie_runtime/audio/imuse_session.h"
#include "tie_runtime/display/classic_display.h"
#include "tie_runtime/input/input.h"

#include <imuse/hilevel.h>
#include <imuse/lolevel.h>

static int32_t s_submodal_result;
static int32_t s_info_room_pending = -1;
static bool s_replay_viewer_pending;
static bool s_pause_active;
static int16_t s_pause_saved_vol;

void TieFlightRequest_SetSubmodalResult(int32_t result) { s_submodal_result = result; }

int32_t TieFlightRequest_SubmodalResult(void) { return s_submodal_result; }

void TieFlightRequest_InfoRoom(int32_t screen) { s_info_room_pending = screen; }

int32_t TieFlightRequest_ConsumeInfoRoom(void) {
	int32_t r = s_info_room_pending;
	s_info_room_pending = -1;
	return r;
}

void TieFlightRequest_ReplayViewer(void) { s_replay_viewer_pending = true; }

bool TieFlightRequest_ConsumeReplayViewer(void) {
	bool r = s_replay_viewer_pending;
	s_replay_viewer_pending = false;
	return r;
}

void TieFlightPause_Enter(void) {
	TieInput_ClearKeys();
	TieInput_ResetThrottle();
	s_pause_saved_vol = imuse_get_master_vol(im);
	imuse_set_master_vol(im, 0);
	imuse_pause(im);
	msg_messageprintf(MSG_PAUSED);
	if (TieClassicDisplay_UsesDx5()) {
		FrontendDisplay_BlitOffscreenToRenderSurface();
		FrontendDisplay_PresentFrame();
	}
	s_pause_active = true;
}

/* Drain commands received while paused; only the mapped Pause command
 * resumes. */
void TieFlightPause_Service(void) {
	while (TieInput_KeyPending()) {
		if (feinput_getrawinput() != KEY_p)
			continue;
		TieInput_ClearKeys();
		TieInput_ResetThrottle();
		if (TieClassicDisplay_UsesDx5())
			FrontendDisplay_PresentFrame();
		msg_messageprintf(MSG_RESUMED);
		calcframerate = 0;
		keypress = 0;
		imuse_set_master_vol(im, s_pause_saved_vol);
		imuse_resume(im);
		s_pause_active = false;
		break;
	}
}

bool TieFlightPause_IsActive(void) { return s_pause_active; }
