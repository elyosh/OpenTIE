#include "tie_runtime/runtime/inflight_info_task.h"
#include "tie/fediskio.h"
#include "tie/feinput.h"
#include "tie/festring.h"
#include "tie/flight_surface_tie98.h"
#include "tie/frontend_display_tie98.h"
#include "tie/fsfx.h"
#include "tie/msg.h"
#include "tie/panel.h"
#include "tie/panelrts.h"
#include "tie/replay.h"
#include "tie/rtsvga2.h"
#include "tie/tie.h"
#include "tie/tie_render_tie98.h"
#include "tie/user.h"
#include "tie_runtime/audio/imuse_session.h"
#include "tie_runtime/display/classic_display.h"
#include "tie_runtime/runtime/damage_task.h"
#include "tie_runtime/runtime/flight_requests.h"
#include "tie_runtime/runtime/flight_screen.h"
#include "tie_runtime/runtime/goals_task.h"
#include "tie_runtime/runtime/help_task.h"
#include "tie_runtime/runtime/inflight_state.h"
#include "tie_runtime/runtime/maproom_task.h"
#include "tie_runtime/runtime/msgroom_task.h"
#include "tie_runtime/runtime/profile.h"
#include "tie_runtime/runtime/runtime.h"
#include "tie_runtime/runtime/wingman_task.h"
#include "tie_runtime/timing/replay_recording.h"
#include "tie_runtime/timing/replay_timing.h"
#include "util/binio.h"
#include <imuse/lolevel.h>
#include <landru/task.h>
#include <string.h>

int32_t TieInflightInfo_ReadReplay(void) {
	/* --- Replay-playback deserialization path. ---
	 * Four REPLAYINPUTFRAME_DISK_SIZE side-payload records, each
	 * consumed in full
	 * by user_nextreplaycount (which advances the replay cursor
	 * + handles the per-chunk wrap). The trailing
	 * pad bytes inside each slot are produced by the writer
	 * below; we just step over them. */
	uint8_t* rp;
	int32_t rep_return;

	/* Slot 1 — radar / target / key / screen. */
	if (!TieReplayTiming_CurrentRecordAvailable())
		goto corrupt_payload;
	rp = (uint8_t*)replayptr;
	pstate.radar_target0 = br_i16le(rp + 0);
	pstate.target_obj_idx = br_u16le(rp + 2);
	inputkey = br_i16le(rp + 4);
	rep_return = br_i16le(rp + 6);
	replayptr = rp + REPLAYINPUTFRAME_DISK_SIZE;
	user_nextreplaycount();

	/* Slot 2 — Ten repair-priority bytes, five u16
	 * big-endian-of-pair values. */
	if (!TieReplayTiming_CurrentRecordAvailable())
		goto corrupt_payload;
	rp = (uint8_t*)replayptr;
	uint8_t* rec_dst = pstate.subsystem_repair_priority;
	for (int i = 0; i < 10; i += 2) {
		int16_t w = br_i16le(rp + i);
		rec_dst[i] = (uint8_t)(w >> 8);
		rec_dst[i + 1] = (uint8_t)(w & 0xFF);
	}
	replayptr = rp + REPLAYINPUTFRAME_DISK_SIZE;
	user_nextreplaycount();

	/* Slot 3 — detail / effect-flag bit-packing. */
	if (!TieReplayTiming_CurrentRecordAvailable())
		goto corrupt_payload;
	rp = (uint8_t*)replayptr;
	starshipexplodetail = br_u16le(rp);
	uint16_t pk1 = br_u16le(rp + 2);
	drawdebrisflag = (uint8_t)(pk1 & 0xF);
	pk1 >>= 4;
	drawbackdropflag = (uint8_t)(pk1 & 0xF);
	pk1 >>= 4;
	stardetaillevel = (uint16_t)(pk1 & 0xF);
	starshipdetail = (uint16_t)(pk1 >> 4);
	uint16_t pk2 = br_u16le(rp + 4);
	gouraudflag = (uint8_t)(pk2 & 0xFF);
	drawmarkingsflag = (uint8_t)((pk2 >> 8) & 0xF);
	shipdetailvalue = (int16_t)((pk2 >> 12) & 0xF);
	uint16_t pk3 = br_u16le(rp + 6);
	hyperspacedetail = (int16_t)(pk3 & 0xFF);
	shipdetailpolycnt = (uint16_t)((pk3 >> 8) & 0xFF);
	replayptr = rp + REPLAYINPUTFRAME_DISK_SIZE;
	user_nextreplaycount();

	/* Slot 4 — cheat flags + volume latches. */
	if (!TieReplayTiming_CurrentRecordAvailable())
		goto corrupt_payload;
	rp = (uint8_t*)replayptr;
	uint16_t cheats = br_u16le(rp);
	cheatingflag = (uint8_t)(cheats & 0xF);
	inflight_unlimited = (int8_t)((cheats >> 4) & 0xF);
	inflight_invulnerable = (int8_t)((cheats >> 8) & 0xF);
	inflight_collision = (int8_t)((cheats >> 12) & 0xF);
	uint16_t snd_pk = br_u16le(rp + 2);
	soundvolflag = (uint8_t)(snd_pk & 0xFF);
	inflight_sound_vol = (int8_t)(snd_pk >> 8);
	uint16_t mus_pk = br_u16le(rp + 4);
	musicvolflag = (uint8_t)(mus_pk & 0xFF);
	inflight_music_vol = (int8_t)(mus_pk >> 8);
	inflight_speech_vol = (int8_t)rp[6];
	replayptr = rp + REPLAYINPUTFRAME_DISK_SIZE;
	user_nextreplaycount();

	rtsvga2_InvalidatePageCache();
	TieReplayTiming_Reset();
	return rep_return;

corrupt_payload:
	replay_stopreplay();
	return 0xFFFF;
}

void TieInflightInfo_RecordRoom(int32_t screen) {
	if (recordingreplay) {
		/* Four REPLAYINPUTFRAME_DISK_SIZE side-payload records; data
		 * fills the leading 7-10 bytes (slot-specific), the trailing
		 * bytes are zeroed so the on-disk record is fully defined and
		 * the round-trip on the read path lands on the same bytes. */
		uint8_t* rp;

		/* Slot 1 — radar / target / key / screen. */
		rp = (uint8_t*)replayptr;
		bw_i16le(rp + 0, pstate.radar_target0);
		bw_i16le(rp + 2, (int16_t)pstate.target_obj_idx);
		bw_i16le(rp + 4, inputkey);
		bw_i16le(rp + 6, (int16_t)screen);
		memset(rp + 8, 0, REPLAYINPUTFRAME_DISK_SIZE - 8u);
		replayptr = rp + REPLAYINPUTFRAME_DISK_SIZE;
		TieReplayRecording_StoreRecord(false);

		/* Slot 2 — Ten repair-priority bytes, five u16
		 * big-endian-of-pair values. */
		rp = (uint8_t*)replayptr;
		const uint8_t* rec_src = pstate.subsystem_repair_priority;
		for (int i = 0; i < 10; i += 2) {
			bw_u16le(rp + i, (uint16_t)(rec_src[i + 1] + (rec_src[i] << 8)));
		}
		memset(rp + 10, 0, REPLAYINPUTFRAME_DISK_SIZE - 10u);
		replayptr = rp + REPLAYINPUTFRAME_DISK_SIZE;
		TieReplayRecording_StoreRecord(false);

		/* Slot 3 — detail / effect-flag bit-packing. */
		rp = (uint8_t*)replayptr;
		bw_u16le(rp, starshipexplodetail);
		uint16_t pack =
			(uint16_t)((drawdebrisflag & 0xF) +
					   (((drawbackdropflag & 0xF) + ((stardetaillevel & 0xF) + (starshipdetail << 4)) * 16) *
						16));
		bw_u16le(rp + 2, pack);
		bw_u16le(rp + 4, (uint16_t)(gouraudflag + ((drawmarkingsflag + (shipdetailvalue << 4)) << 8)));
		bw_u16le(rp + 6, (uint16_t)((uint8_t)hyperspacedetail + (shipdetailpolycnt << 8)));
		memset(rp + 8, 0, REPLAYINPUTFRAME_DISK_SIZE - 8u);
		replayptr = rp + REPLAYINPUTFRAME_DISK_SIZE;
		TieReplayRecording_StoreRecord(false);

		/* Slot 4 — cheat flags + volume latches. */
		rp = (uint8_t*)replayptr;
		bw_u16le(rp, (uint16_t)(cheatingflag + 16 * (inflight_unlimited + 16 * (inflight_invulnerable +
																				16 * inflight_collision))));
		bw_u16le(rp + 2, (uint16_t)(soundvolflag + (inflight_sound_vol << 8)));
		bw_u16le(rp + 4, (uint16_t)(musicvolflag + (inflight_music_vol << 8)));
		rp[6] = (uint8_t)inflight_speech_vol;
		memset(rp + 7, 0, REPLAYINPUTFRAME_DISK_SIZE - 7u);
		replayptr = rp + REPLAYINPUTFRAME_DISK_SIZE;
		TieReplayRecording_StoreRecord(false);
	}
}

static LandruTaskStepResult user_inflightinfo_task_step(void* self) {
	InflightInfoTask* task = self;
	int32_t result = user_inflightinfo(task->screen_id);
	if (task->finished) {
		TieFlightRequest_SetSubmodalResult(result);
		return LANDRU_TASK_STEP_DONE;
	}
	return LANDRU_TASK_STEP_CONTINUE;
}
static void user_inflightinfo_task_end(void* self) {
	InflightInfoTask* task = self;
	TieFlightScreen_SetActive(task->previous_screen);
}
static const LandruTaskVtable user_inflightinfo_task_vt = {
	.step = user_inflightinfo_task_step,
	.end = user_inflightinfo_task_end,
};
void TieInflightInfo_Begin(int32_t screen_id) {
	InflightInfoTask* task = landru_task_push(&user_inflightinfo_task_vt);
	if (!task)
		return;
	task->screen_id = screen_id;
	task->previous_screen = TieFlightScreen_Active();
}
