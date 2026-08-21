#include "tie_runtime/timing/replay_timing.h"

#include <limits.h>

#include "tie/tie.h"

enum { REPLAY_MAX_FRAME_DELTA_US = 64000 };

/* Host-time accumulator for applying recorded frames at their original cadence. */
static uint64_t s_playback_wait_us;
static bool s_playback_frame_due;
static bool s_playback_was_active;

bool TieReplayTiming_CurrentRecordAvailable(void) {
	return replayptr && replaytotalcnt > 0 && replaytotalcntdown < (uint32_t)replaytotalcnt &&
		   replaybuffercnt < REPLAY_INPUT_CHUNK_FRAMES;
}

bool TieReplayTiming_DecodeCurrentInputFrame(ReplayInputFrame* destination) {
	if (!destination || !TieReplayTiming_CurrentRecordAvailable())
		return false;
	ReplayInputFrame_decode(destination, (const uint8_t*)replayptr);
	return destination->frameticks > 0 && destination->frameticks <= REPLAY_MAX_FRAME_DELTA_US / 4000u &&
		   destination->delta_us <= REPLAY_MAX_FRAME_DELTA_US;
}

uint32_t TieReplayTiming_InputFrameDeltaUs(const ReplayInputFrame* frame) {
	if (!frame || !frame->frameticks)
		return 0;
	const uint32_t tick_delta_us = (uint32_t)frame->frameticks * 4000u;
	return frame->delta_us < tick_delta_us ? tick_delta_us : frame->delta_us;
}

void TieReplayTiming_Reset(void) {
	s_playback_wait_us = 0;
	s_playback_frame_due = false;
	s_playback_was_active = false;
}

int32_t TieReplayTiming_SelectEngineDeltaUs(int32_t host_delta_us) {
	const bool active = replayviewmode && updateactionflag && replaytotalcnt > 0 &&
						replaytotalcntdown < (uint32_t)replaytotalcnt;
	if (!active) {
		TieReplayTiming_Reset();
		return host_delta_us;
	}

	ReplayInputFrame frame;
	if (!TieReplayTiming_DecodeCurrentInputFrame(&frame)) {
		s_playback_frame_due = true;
		return 0;
	}
	const uint32_t frame_delta_us = TieReplayTiming_InputFrameDeltaUs(&frame);
	if (!s_playback_was_active) {
		s_playback_wait_us = 0;
		s_playback_was_active = true;
	}
	if (fastforwardflag) {
		s_playback_frame_due = true;
		return (int32_t)frame_delta_us;
	}
	if (host_delta_us > 0)
		s_playback_wait_us += (uint32_t)host_delta_us;
	if (s_playback_wait_us < frame_delta_us) {
		s_playback_frame_due = false;
		return 0;
	}
	s_playback_wait_us -= frame_delta_us;
	s_playback_frame_due = true;
	return (int32_t)frame_delta_us;
}

bool TieReplayTiming_IsFrameDue(void) { return s_playback_frame_due; }

void TieReplayTiming_ConsumeFrame(void) { s_playback_frame_due = false; }

uint64_t TieReplayTiming_NextWakeDelayUs(void) {
	if (!replayviewmode || !updateactionflag)
		return UINT64_MAX;
	if (fastforwardflag || s_playback_frame_due)
		return 0;
	ReplayInputFrame frame;
	if (!TieReplayTiming_DecodeCurrentInputFrame(&frame))
		return 0;
	const uint64_t frame_delta_us = TieReplayTiming_InputFrameDeltaUs(&frame);
	return s_playback_wait_us >= frame_delta_us ? 0 : frame_delta_us - s_playback_wait_us;
}
