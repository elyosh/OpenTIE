#include "tie_runtime/runtime/replay_viewer_task.h"
#include "tie/replay.h"
#include "tie/tie.h"
#include "tie_runtime/runtime/flight_screen.h"
#include "tie_runtime/snapshot/snapshot_internal.h"
#include "tie_runtime/timing/replay_timing.h"
#include <landru/task.h>

static LandruTaskStepResult replay_doreplayscreen_task_step(void* self) {
	ReplayScreenState* t = self;
	bool started = t->started;
	replay_doreplayscreen();
	if (!started || t->pushed_subtask)
		return LANDRU_TASK_STEP_CONTINUE;
	return exitflag ? LANDRU_TASK_STEP_DONE : LANDRU_TASK_STEP_YIELD;
}

static uint64_t TieReplay_NextWakeDelayUs(const void* self) {
	(void)self;
	return TieReplayTiming_NextWakeDelayUs();
}

static void replay_doreplayscreen_task_end(void* self) {
	ReplayScreenState* t = (ReplayScreenState*)self;
	TieFlightScreen_SetActive(t->previous_screen);
}

static const LandruTaskVtable replay_doreplayscreen_task_vt = {
	.step = replay_doreplayscreen_task_step,
	.end = replay_doreplayscreen_task_end,
	.next_wake_delay_us = TieReplay_NextWakeDelayUs,
};

void TieReplayViewer_Begin(void) {
	ReplayScreenState* t = (ReplayScreenState*)landru_task_push(&replay_doreplayscreen_task_vt);
	if (!t)
		return;
	/* PORT: Film Room playback bypasses the mission task that normally
	 * marks the snapshot as flight-owned. The replay screen's classic
	 * fallback remains selected through its TieFlightScreen value. */
	TieSnapshotBuilder_SetSceneKind(TIE_SCENE_FLIGHT);
	t->previous_screen = TieFlightScreen_SetActive(TIE_FLIGHT_SCREEN_REPLAY_VIEWER);
	t->last_chase_status = 0xFFFF;
	t->last_track_status = 0xFFFF;
}
