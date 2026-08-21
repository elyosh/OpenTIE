#include "tie_runtime/timing/replay_recording.h"

#include "tie/msg.h"
#include "tie/replay.h"
#include "tie/replayio.h"
#include "tie/tie.h"
#include "tie/user.h"
#include "tie_runtime/timing/flight_timing.h"

static uint64_t s_recorded_duration_us;

void TieReplayRecording_ResetDuration(void) { s_recorded_duration_us = 0; }

void TieReplayRecording_StoreRecord(bool advances_time) {
	if (advances_time)
		s_recorded_duration_us += (uint64_t)frameticks * 4000u;
	if ((++replaybuffercnt) >= REPLAY_INPUT_CHUNK_FRAMES) {
		if (replayspoolflag) {
			if (!replayio_spoolreplayinput()) {
				recordingreplay = 0;
				replaytotalcnt -= replaybuffercnt;
			}
			replaybuffercnt = 0;
			replayptr = replaybufferstart;
			calcframerate = 0;
		} else {
			recordingreplay = 0;
			msg_messageprintf(MSG_CAMERA_FILM_USED);
		}
	}
	if (++replaytotalcnt >= replaymaxcnt ||
		s_recorded_duration_us >= TieFlightTiming_RecordDurationLimitUs()) {
		recordingreplay = 0;
		msg_messageprintf(MSG_CAMERA_FILM_USED);
	}
}

bool TieReplayRecording_KeyStartsInfoPayload(uint16_t key) {
	switch (key) {
		case KEY_ESCAPE:
		case KEY_Z:
		case KEY_d:
		case KEY_g:
		case KEY_k:
		case KEY_l:
		case KEY_m:
			return true;
		default:
			return false;
	}
}

bool TieReplayRecording_PrepareSlots(uint16_t required_slots, bool timed_frame_starts_payload) {
	if ((uint32_t)replaytotalcnt + required_slots > (uint32_t)replaymaxcnt ||
		(timed_frame_starts_payload &&
		 s_recorded_duration_us + (uint64_t)frameticks * 4000u >= TieFlightTiming_RecordDurationLimitUs())) {
		recordingreplay = 0;
		msg_messageprintf(MSG_CAMERA_FILM_USED);
		return false;
	}
	if ((uint32_t)replaybuffercnt + required_slots <= REPLAY_INPUT_CHUNK_FRAMES)
		return true;
	if (!replayspoolflag) {
		recordingreplay = 0;
		msg_messageprintf(MSG_CAMERA_FILM_USED);
		return false;
	}
	if (!replayio_spoolreplayinput()) {
		replaytotalcnt -= replaybuffercnt;
		recordingreplay = 0;
		msg_messageprintf(MSG_CAMERA_FAIL);
	}
	replaybuffercnt = 0;
	replayptr = replaybufferstart;
	calcframerate = 0;
	return recordingreplay != 0;
}
