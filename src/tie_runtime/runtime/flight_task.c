#include "tie_runtime/runtime/flight_task.h"
#include "tie_runtime/runtime/flight_requests.h"
#include "tie_runtime/runtime/inflight_info_task.h"
#include "tie_runtime/runtime/replay_session_task.h"

#include "tie/anim.h"
#include "tie/backdrp2.h"
#include "tie/cdaudio_tie98.h"
#include "tie/collide.h"
#include "tie/create.h"
#include "tie/draw.h"
#include "tie/drawpol.h"
#include "tie/dynamix.h"
#include "tie/fediskio.h"
#include "tie/feinput.h"
#include "tie/festring.h"
#include "tie/flight_composite_tie98.h"
#include "tie/flight_surface_tie98.h"
#include "tie/fmusic.h"
#include "tie/frontend_display_tie98.h"
#include "tie/frontend_sound_tie98.h"
#include "tie/fscript.h"
#include "tie/fsfx.h"
#include "tie/fview.h"
#include "tie/gamesnd.h"
#include "tie/gate.h"
#include "tie/laser.h"
#include "tie/logbuf2.h"
#include "tie/math2.h"
#include "tie/mission.h"
#include "tie/modelbounds.h"
#include "tie/modelmesh.h"
#include "tie/move.h"
#include "tie/msg.h"
#include "tie/msgroom.h"
#include "tie/pai.h"
#include "tie/panel.h"
#include "tie/render_scene_tie98.h"
#include "tie/render_texture_tie98.h"
#include "tie/replay.h"
#include "tie/replayio.h"
#include "tie/rotscale.h"
#include "tie/rtsvga2.h"
#include "tie/score.h"
#include "tie/spec.h"
#include "tie/species.h" /* hyperstardata — for hyperspace emit */
#include "tie/starship.h"
#include "tie/static.h"
#include "tie/tie.h"
#include "tie/tie_render_tie98.h"
#include "tie/transfm2.h"
#include "tie/trig2.h"
#include "tie/user.h"
#include "tie/xtimer.h"
#include "tie/xtrans2.h"
#include "tie_runtime/audio/config.h"
#include "tie_runtime/audio/imuse_session.h"
#include "tie_runtime/audio/music_policy.h"
#include "tie_runtime/diagnostics/diagnostics.h"
#include "tie_runtime/diagnostics/flight_trace.h"
#include "tie_runtime/display/classic_display.h"
#include "tie_runtime/display/classic_framebuffer.h"
#include "tie_runtime/display/tie98_renderer.h"
#include "tie_runtime/flight_assets/model_types.h"
#include "tie_runtime/input/input.h"
#include "tie_runtime/runtime/exports.h"
#include "tie_runtime/runtime/flight_screen.h"
#include "tie_runtime/runtime/inflight_state.h"
#include "tie_runtime/runtime/profile.h"
#include "tie_runtime/runtime/runtime.h"
#include "tie_runtime/snapshot/snapshot.h"
#include "tie_runtime/snapshot/snapshot_billboards.h" /* SNAPSHOT-ONLY billboard capture drain */
#include "tie_runtime/snapshot/snapshot_flight.h"
#include "tie_runtime/snapshot/snapshot_internal.h"
#include "tie_runtime/storage/storage.h"
#include "tie_runtime/timing/ai_lead.h"
#include "tie_runtime/timing/chase_camera.h"
#include "tie_runtime/timing/flight_clock.h"
#include "tie_runtime/timing/flight_timing.h"
#include "tie_runtime/timing/flight_timing_state.h"
#include "tie_runtime/timing/replay_timing.h"
#include "tie_runtime/timing/sim_clock.h" /* TieSimClock_NowUs — loading-screen minimum hold */
#include "util/binio.h"
#include <imuse/filelist.h>
#include <imuse/hilevel.h>
#include <imuse/lolevel.h>
#include <landru/error.h>
#include <landru/task.h>
#include <landru/vesa.h>

typedef struct FlightTask {
	/* 1 = the previous step pushed TieReplaySession_Begin; on
	 * this step the viewer has popped and we owe the player a RESUMED
	 * banner (matches the original synchronous code which posted
	 * MSG_RESUMED right after replayio_replayscreen returned). */
	uint8_t resumed_banner_pending;
	uint8_t rebase_pending;
} FlightTask;

/* tie_doframe returns false when its PIT-tick budget hasn't landed yet —
 * yield so the next TieRuntime_Tick advances xtimer.
 * PORT: an accelerated-time skip frame preloads tickcounter with its own
 * frameticks so the original loop ran the next frame immediately, without
 * presenting. Keep stepping inside this host tick until a frame renders. */
static LandruTaskStepResult flight_step_frame(void) {
	if (!tie_doframe())
		return LANDRU_TASK_STEP_YIELD;
	return tickcounter >= TieFlightTiming_StepTicks() ? LANDRU_TASK_STEP_CONTINUE
													  : LANDRU_TASK_STEP_FRAME_COMPLETE;
}

static LandruTaskStepResult flight_hyper_step(void* self) {
	(void)self;
	if (!hyperspaceflag)
		return LANDRU_TASK_STEP_DONE;
	return flight_step_frame();
}

static LandruTaskStepResult flight_mission_step(void* self) {
	FlightTask* t = (FlightTask*)self;
	if (mission.end_flag != 0)
		return LANDRU_TASK_STEP_DONE;
	if (t->rebase_pending) {
		t->rebase_pending = 0;
		TieFlightClock_Rebase();
		tickcounter = 0;
	}

	/* If the previous step pushed the replay viewer, the viewer task
	 * has now popped (otherwise we wouldn't be running). Post the
	 * RESUMED banner the original synchronous flow emitted right
	 * after replayio_replayscreen returned. */
	if (t->resumed_banner_pending) {
		t->resumed_banner_pending = 0;
		msg_messageprintf(MSG_RESUMED);
	}

	/* Pending info-room request from a user_userinterface keybind?
	 * Push the in-flight info task on top so the host loop steps it
	 * next; this step yields, and the next tick advances the
	 * sub-task. When the info-room pops, control returns here and
	 * tie_doframe resumes its frame-by-frame cadence. */
	int32_t pending = TieFlightRequest_ConsumeInfoRoom();
	if (pending >= 0) {
		TieInflightInfo_Begin(pending);
		t->rebase_pending = 1;
		return LANDRU_TASK_STEP_CONTINUE;
	}

	/* Pending replay-viewer request from the 'v' keybind? Push the
	 * viewer on top and arm the post-pop RESUMED banner. */
	if (TieFlightRequest_ConsumeReplayViewer()) {
		TieReplaySession_Begin();
		t->resumed_banner_pending = 1;
		t->rebase_pending = 1;
		return LANDRU_TASK_STEP_CONTINUE;
	}

	return flight_step_frame();
}

static uint64_t flight_task_next_wake_delay_us(const void* self) {
	(void)self;
	if (replayviewmode)
		return UINT64_MAX;
	return TieFlightClock_DelayUntilTicksUs(tickcounter, TieFlightTiming_StepTicks());
}

static const LandruTaskVtable flight_hyper_task_vt = {
	.step = flight_hyper_step,
	.next_wake_delay_us = flight_task_next_wake_delay_us,
};
static const LandruTaskVtable flight_mission_task_vt = {
	.step = flight_mission_step,
	.next_wake_delay_us = flight_task_next_wake_delay_us,
};

/* Public push helper: replayio's "re-enter sim from viewer" path needs to
 * push the flight loop from outside this module. FlightTask is zero-initialized
 * by the task runner. */
void TieFlightTask_BeginMission(void) {
	/* Tag the snapshot scene-kind for any modern renderer reading
	 * TieSnapshot_Current during this scene. Sticks across the
	 * flight task's lifetime; reset by whichever modal pushes next
	 * (debrief, replay viewer, etc.). */
	TieSnapshotBuilder_SetSceneKind(TIE_SCENE_FLIGHT);
	{
		/* A key left by a room in an earlier session is stale. */
		int16_t stale_key;
		(void)TieFlightRequest_ConsumeRoomKey(&stale_key);
	}
	(void)landru_task_push(&flight_mission_task_vt);
}

void TieFlightRuntime_ReleaseRecoveredResources(void) {
	RenderTexture_ReleaseMissionCaches();
	TieRuntime_RequestFlightResourceRelease();
}

void TieFlightRuntime_ResetTiming(void) {
	TieInput_ResetThrottle();
	inputthrottle = UINT32_MAX;
	TieFlightTiming_BeginSession(TieProfile_Flight());
	TieFlightTimingState_Reset();
	TieAiLead_Reset();
	TieChaseCamera_Reset();
}

bool TieFlightRuntime_PrepareSimulator(int replay_mode) {
	TieFlightRuntime_ResetTiming();

	TieSnapshotBuilder_SetSceneKind(TIE_SCENE_FLIGHT_LOADING);
	const bool display_active = replay_mode ? TieClassicDisplay_ActivateFlightMode((uint16_t)flightResolution)
											: TieClassicDisplay_ActivateFlight();
	if (!display_active) {
		xerror_Set_Landru_Error(12);
		TieFlightTiming_EndSession();
		return false;
	}

	return true;
}

void TieFlightRuntime_BeginHyperspace(void) {
	TieSnapshotBuilder_SetSceneKind(TIE_SCENE_FLIGHT);
	(void)landru_task_push(&flight_hyper_task_vt);
}
static LandruTaskStepResult tie_simulator_task_step(void* self) {
	TieSimulatorTask* task = self;
	task->next_step = LANDRU_TASK_STEP_CONTINUE;
	tie_simulator(task->replay_mode);
	return task->next_step;
}
static void tie_simulator_task_end(void* self) {
	TieSimulatorTask* task = self;
	if (task->phase == TIE_SIM_PHASE_PROMPT_RENDER || task->phase == TIE_SIM_PHASE_PROMPT_POLL ||
		task->phase == TIE_SIM_PHASE_PROMPT_AFTER_VIEWER)
		TieFlightScreen_SetActive(task->previous_screen);
}
static const LandruTaskVtable tie_simulator_task_vt = {
	.step = tie_simulator_task_step,
	.end = tie_simulator_task_end,
};
void TieFlightTask_Begin(int replay_mode) {
	TieSimulatorTask* task = landru_task_push(&tie_simulator_task_vt);
	if (task)
		task->replay_mode = replay_mode;
}
