#include "tie_runtime/runtime/replay_viewer_task.h"
#include "tie/replay.h"
#include "tie/tie.h"
#include "tie_runtime/runtime/flight_screen.h"
#include "tie_runtime/snapshot/snapshot_internal.h"
#include "tie_runtime/timing/replay_timing.h"
#include <landru/task.h>

typedef enum { REPLAY_DOSCREEN_PHASE_INIT, REPLAY_DOSCREEN_PHASE_POLL } ReplayDoScreenPhase;
typedef struct ReplayDoScreenTask {
	ReplayScreenState screen;
	TieFlightScreen previous_screen;
	ReplayDoScreenPhase phase;
} ReplayDoScreenTask;

// ORIGINAL_FUNCTION: TIE95 0x4646C
// ORIGINAL_FUNCTION: TIE98 0x473AA0
// (task-split recovery)
static LandruTaskStepResult replay_doreplayscreen_task_step(void* self) {
	ReplayDoScreenTask* t = (ReplayDoScreenTask*)self;

	if (t->phase == REPLAY_DOSCREEN_PHASE_INIT) {
		replay_InitScreen(&t->screen);
		t->phase = REPLAY_DOSCREEN_PHASE_POLL;
		return LANDRU_TASK_STEP_CONTINUE;
	}

	if (exitflag)
		return LANDRU_TASK_STEP_DONE;

	const bool pushed_subtask = replay_UpdateScreen(&t->screen);
	if (pushed_subtask)
		return LANDRU_TASK_STEP_CONTINUE;

	/* YIELD to keep replay paced at one body-call per TieRuntime_Tick
	 * (one body invocation reads + applies one replay packet). Under
	 * the multi-step run_frame driver, returning CONTINUE here would
	 * fast-forward through recorded packets within a single TieRuntime_Tick. */
	return exitflag ? LANDRU_TASK_STEP_DONE : LANDRU_TASK_STEP_YIELD;
}

static uint64_t TieReplay_NextWakeDelayUs(const void* self) {
	(void)self;
	return TieReplayTiming_NextWakeDelayUs();
}

static void replay_doreplayscreen_task_end(void* self) {
	ReplayDoScreenTask* t = (ReplayDoScreenTask*)self;
	TieFlightScreen_SetActive(t->previous_screen);
}

static const LandruTaskVtable replay_doreplayscreen_task_vt = {
	.step = replay_doreplayscreen_task_step,
	.end = replay_doreplayscreen_task_end,
	.next_wake_delay_us = TieReplay_NextWakeDelayUs,
};

void TieReplayViewer_Begin(void) {
	ReplayDoScreenTask* t = (ReplayDoScreenTask*)landru_task_push(&replay_doreplayscreen_task_vt);
	if (!t)
		return;
	/* PORT: Film Room playback bypasses the mission task that normally
	 * marks the snapshot as flight-owned. The replay screen's classic
	 * fallback remains selected through its TieFlightScreen value. */
	TieSnapshotBuilder_SetSceneKind(TIE_SCENE_FLIGHT);
	t->previous_screen = TieFlightScreen_SetActive(TIE_FLIGHT_SCREEN_REPLAY_VIEWER);
	t->screen.last_chase_status = 0xFFFF;
	t->screen.last_track_status = 0xFFFF;
	t->phase = REPLAY_DOSCREEN_PHASE_INIT;
}
