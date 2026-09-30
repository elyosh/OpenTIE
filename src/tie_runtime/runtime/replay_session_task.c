#include "tie_runtime/runtime/replay_session_task.h"
#include "tie/fediskio.h"
#include "tie/feinput.h"
#include "tie/festring.h"
#include "tie/flight_surface_tie98.h"
#include "tie/frontend_display_tie98.h"
#include "tie/fsfx.h"
#include "tie/msg.h"
#include "tie/msg_templates.h"
#include "tie/msgroom.h"
#include "tie/panel.h"
#include "tie/panelrts.h"
#include "tie/replay.h"
#include "tie/replayio.h"
#include "tie/rtsvga2.h"
#include "tie/tie.h"
#include "tie_runtime/audio/imuse_session.h"
#include "tie_runtime/display/classic_display.h"
#include "tie_runtime/runtime/flight_task.h"
#include "tie_runtime/runtime/profile.h"
#include "tie_runtime/runtime/replay_viewer_task.h"
#include <imuse/lolevel.h>
#include <landru/error.h>
#include <landru/task.h>

typedef enum {
	REPLAYIO_PHASE_ENTER = 0,
	REPLAYIO_PHASE_AFTER_VIEWER,
	REPLAYIO_PHASE_AFTER_REENTERSIM,
	REPLAYIO_PHASE_DONE,
} ReplayioPhase;

typedef struct ReplayioTask {
	int16_t saved_res;
	ReplayioPhase phase;
} ReplayioTask;

static void replayio_pause_imuse_save_volume(void) {
	replayvolume = (int16_t)imuse_get_master_vol(im);
	imuse_set_master_vol(im, 0);
	imuse_pause(im);
	replaymusic = 0;
}

static void replayio_resume_imuse_if_paused(void) {
	if (!replaymusic) {
		imuse_set_master_vol(im, (uint16_t)replayvolume);
		imuse_resume(im);
		replaymusic = 1;
	}
}

// ORIGINAL_FUNCTION: TIE95 0x47CF4
// ORIGINAL_FUNCTION: TIE98 0x475350
// (task-split recovery)
static LandruTaskStepResult replayio_task_step(void* self) {
	ReplayioTask* t = (ReplayioTask*)self;

	switch (t->phase) {

		case REPLAYIO_PHASE_ENTER:
			/* Bail-out check: in-flight ('maingameflag') invocations
			 * checkpoint the live mission to disk first. If the disk
			 * save fails, the entire viewer is skipped — iMUSE is
			 * already silenced, so leave it that way and pop. */
			if (maingameflag && !replayio_copytosave(replaysavegamefile))
				return LANDRU_TASK_STEP_DONE;

			recordingreplay = 0;
			replayviewmode = 1;
			if (TieClassicDisplay_UsesDx5())
				FlightSurface_Lock();
			replayio_LoadInitialPanel();
			replay_rewindreplay();
			if (TieClassicDisplay_UsesDx5())
				FlightSurface_Unlock();

			/* Fall through to the loop-top: clear reentersim, repaint
			 * once, push the modal viewer task. */
			reentersimflag = 0;
			festring_showscreen();
			if (TieClassicDisplay_UsesDx5()) {
				FrontendDisplay_BlitOffscreenToRenderSurface();
				FrontendDisplay_PresentFrame();
				FrontendDisplay_BlitOffscreenToRenderSurface();
			}
			TieReplayViewer_Begin();
			t->phase = REPLAYIO_PHASE_AFTER_VIEWER;
			return LANDRU_TASK_STEP_CONTINUE;

		case REPLAYIO_PHASE_AFTER_VIEWER:
			/* DoReplayScreen task popped. Decide which branch fires. */
			if (replaymusic) {
				replayvolume = (int16_t)imuse_get_master_vol(im);
				imuse_set_master_vol(im, 0);
				imuse_pause(im);
				replaymusic = 0;
			}
			replayviewmode = 0;

			if (maingameflag) {
				/* Cockpit: return to in-flight. Retail restores the flight
				 * resolution (if the user changed it inside the viewer)
				 * before copyfromsave. */
				if (!replayio_RestoreGraphics(t->saved_res)) {
					xerror_Set_Landru_Error(12);
					return LANDRU_TASK_STEP_DONE;
				}
				replayio_copyfromsave(replaysavegamefile);
				replayio_setreturnview();
				replayio_resume_imuse_if_paused();
				return LANDRU_TASK_STEP_DONE;
			}

			if (!reentersimflag) {
				/* Stand-alone viewer: normal exit. */
				if (TieClassicDisplay_UsesDx5())
					FlightSurface_Lock();
				if (tie_is_high_resolution_flight())
					festring_setbound(28, 16, 611, 315);
				else
					festring_setbound(14, 8, 306, 131);
				festring_setbackcolor(0x40);
				clearwindow();
				if (TieClassicDisplay_UsesDx5())
					FlightSurface_Unlock();
				replayio_resume_imuse_if_paused();
				return LANDRU_TASK_STEP_DONE;
			}

			/* Stand-alone viewer + user pressed 's': re-enter the sim
			 * for a second sortie. Retail adds msg_clearmessagequeue +
			 * blastcount reset. */
			blank();
			recordingreplay = 0;
			numhistorymsgs = 0;
			camera.view_zoom_flag = 0;
			camera.up_angle = 0;
			mission.end_flag = 0;
			camera.view_target_obj = pstate.object_idx;
			camera.pilotview = 0;
			camera.side_angle = 0;
			camera.view_dir_dirty = 0;
			blastcount = 0;
			msg_clearmessagequeue();
			replayio_setreturnview();
			replayio_resume_imuse_if_paused();

			TieFlightTask_BeginMission();
			t->phase = REPLAYIO_PHASE_AFTER_REENTERSIM;
			return LANDRU_TASK_STEP_CONTINUE;

		case REPLAYIO_PHASE_AFTER_REENTERSIM:
			/* Flight task popped (mission.end_flag was set). Pause iMUSE,
			 * reload the replay-start checkpoint, repaint the FILM panel,
			 * and push DoReplayScreen again. The fidelity check
			 * `if (!reentersimflag) return;` from the binary is preserved:
			 * inside the inner sim nothing clears reentersimflag, so the
			 * always-true branch loops back to AFTER_VIEWER which is what
			 * actually decides exit on the next viewer dismissal. */
			replayio_pause_imuse_save_volume();
			blank();
			recordingreplay = 0;
			replayviewmode = 1;
			if (TieClassicDisplay_UsesDx5())
				FlightSurface_Lock();
			replay_loadreplay();
			replayio_copyfromsave(replaystartfile);
			msg_messageinit();
			replayio_LoadStandalonePanel();
			replay_rewindreplay();
			if (TieClassicDisplay_UsesDx5())
				FlightSurface_Unlock();

			if (!reentersimflag)
				return LANDRU_TASK_STEP_DONE;

			reentersimflag = 0;
			festring_showscreen();
			if (TieClassicDisplay_UsesDx5()) {
				FrontendDisplay_BlitOffscreenToRenderSurface();
				FrontendDisplay_PresentFrame();
				FrontendDisplay_BlitOffscreenToRenderSurface();
			}
			TieReplayViewer_Begin();
			t->phase = REPLAYIO_PHASE_AFTER_VIEWER;
			return LANDRU_TASK_STEP_CONTINUE;

		case REPLAYIO_PHASE_DONE:
			/* Unreachable — DONE is returned directly from the producing
			 * phase, never set as a stored value. Kept exhaustive for the
			 * compiler's switch-coverage check. */
			return LANDRU_TASK_STEP_DONE;
	}

	return LANDRU_TASK_STEP_DONE;
}

static const LandruTaskVtable replayio_task_vt = {
	.step = replayio_task_step,
};

void TieReplaySession_Begin(void) {
	ReplayioTask* t = (ReplayioTask*)landru_task_push(&replayio_task_vt);
	if (!t)
		return;
	t->saved_res = (int16_t)flightResolution;
	t->phase = REPLAYIO_PHASE_ENTER;
	if (TieProfile_UsesTie98Logic()) {
		uint8_t saved_mapflag = mapflag;
		mapflag = 1;
		FSFX_UpdatePlayerEngineSound();
		mapflag = saved_mapflag;
	}

	/* Pause iMUSE up-front: retail does this before the disk-save
	 * decision so that even the early-fail path leaves the mixer in
	 * the silenced state the cockpit caller expects. */
	replayio_pause_imuse_save_volume();
}
