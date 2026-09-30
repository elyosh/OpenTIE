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

static TieFlightScreen flight_screen_from_index(int32_t screen) {
	if (screen < 0 || screen > 6)
		return TIE_FLIGHT_SCREEN_NORMAL;
	return (TieFlightScreen)(screen + 1);
}

typedef enum {
	INFLIGHT_PHASE_BEGIN = 0,
	INFLIGHT_PHASE_DISPATCH,
	INFLIGHT_PHASE_AFTER_SUB,
	INFLIGHT_PHASE_FINISH,
} InflightPhase;

typedef struct InflightInfoTask {
	int32_t screen_id;
	uint16_t saved_master_vol;
	int16_t retreat_flag;
	int16_t exit_flag;
	int32_t screen;
	TieFlightScreen previous_screen;
	InflightPhase phase;
} InflightInfoTask;

static int32_t inflight_replay_deserialize(void) {
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

	/* Slot 2 — 10 bytes of pilot rank / kill state, 5 u16
	 * big-endian-of-pair values. */
	if (!TieReplayTiming_CurrentRecordAvailable())
		goto corrupt_payload;
	rp = (uint8_t*)replayptr;
	uint8_t* rec_dst = (uint8_t*)&pstate.player_total_kills;
	for (int i = 0; i < 10; i += 2) {
		int16_t w = br_i16le(rp + i);
		rec_dst[i + 2] = (uint8_t)(w >> 8);
		rec_dst[i + 3] = (uint8_t)(w & 0xFF);
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

	rtsvga2_invalidatepagecache();
	TieReplayTiming_Reset();
	return rep_return;

corrupt_payload:
	replay_stopreplay();
	return 0xFFFF;
}

static void inflight_finish(InflightInfoTask* t) {
	blank();

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
		bw_i16le(rp + 6, (int16_t)t->screen);
		memset(rp + 8, 0, REPLAYINPUTFRAME_DISK_SIZE - 8u);
		replayptr = rp + REPLAYINPUTFRAME_DISK_SIZE;
		TieReplayRecording_StoreRecord(false);

		/* Slot 2 — 10 bytes of pilot rank / kill state, 5 u16
		 * big-endian-of-pair values. */
		rp = (uint8_t*)replayptr;
		uint8_t* rec_src = (uint8_t*)&pstate.player_total_kills;
		for (int i = 0; i < 10; i += 2) {
			bw_u16le(rp + i, (uint16_t)(rec_src[i + 3] + (rec_src[i + 2] << 8)));
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

	festring_setfontsize(2);
	uint16_t pilotview_restore;
	if (camera.view_zoom_flag) {
		camera.pilotview = 0xFF;
		lastpilotpaneldraw = -1;
		pilotview_restore = camera.view_heading_offset ? 20u : 18u;
	} else {
		pilotview_restore = (camera.view_target_obj == pstate.object_idx) ? camera.pilotview : 18u;
		lastpilotpaneldraw = -1;
		camera.pilotview = 0xFF;
	}
	panelrts_setnewpilotview(pilotview_restore);
	msg_messageinit();
	msg_messagerestore();
	if (TieProfile_UsesTie98Logic())
		g_flightInitialTextureCacheFlushPending = 1;
	fullupdateflag = 1;
	imuse_set_master_vol(im, (int16_t)t->saved_master_vol);
	imuse_resume(im);
	/* Retail USER_inflightinfo @ 0x61a53: force the next
	 * rtsvga2_setcurrentpage to re-program the VESA bank so the
	 * cockpit panel and HUD regain their pages after the info room. */
	rtsvga2_invalidatepagecache();
}

// ORIGINAL_FUNCTION: TIE95 0x61544
// ORIGINAL_FUNCTION: TIE98 0x498430
// (task-split recovery)
static LandruTaskStepResult user_inflightinfo_task_step(void* self) {
	InflightInfoTask* t = (InflightInfoTask*)self;

	switch (t->phase) {
		case INFLIGHT_PHASE_BEGIN: {
			if (mission.train_craft_type && (uint16_t)t->screen_id <= 4u) {
				/* Retail USER_inflightinfo @ 0x6155e/0x61563: drop VESA
				 * page cache then return the cancel sentinel 0xFFFF. */
				rtsvga2_invalidatepagecache();
				user_submodal_result = 0xFFFF;
				return LANDRU_TASK_STEP_DONE;
			}
			if (replayviewmode) {
				user_submodal_result = inflight_replay_deserialize();
				return LANDRU_TASK_STEP_DONE;
			}

			t->saved_master_vol = (uint16_t)imuse_get_master_vol(im);
			t->retreat_flag = 0;
			t->exit_flag = 0;
			t->screen = t->screen_id;
			if (TieProfile_UsesTie98Logic()) {
				mapflag = 1;
				FSFX_UpdatePlayerEngineSound();
				mapflag = 0;
			}
			imuse_set_master_vol(im, 0);
			imuse_pause(im);
			t->phase = INFLIGHT_PHASE_DISPATCH;
			return LANDRU_TASK_STEP_CONTINUE;
		}

		case INFLIGHT_PHASE_DISPATCH: {
			if (t->screen == 6) {
				TieRuntime_RequestSettingsMenu();
				t->phase = INFLIGHT_PHASE_FINISH;
				return LANDRU_TASK_STEP_CONTINUE;
			}
			TieFlightScreen_SetActive(flight_screen_from_index(t->screen));
			const bool tie98_display = TieClassicDisplay_UsesDx5();
			if (tie98_display)
				FlightSurface_Lock();
			int panel_idx = (int)((uint16_t)(t->screen + 21));
			if (!panelviewptrs[panel_idx].handle) {
				temppanelptr = newbuf;
				panel_loadcontrolpanel(panelviewdefs[panel_idx].name, &panelviewptrs[panel_idx].image, 3u);
			}
			buildpalette((const uint8_t*)panelviewptrs[panel_idx].palette, 0, 64);
			drawshape(panelviewptrs[panel_idx].image, 0, 0, 253, 0);
			festring_showscreen();
			if (tie98_display)
				FlightSurface_Unlock();

			/* Pre-seed the result for sub-modals we skip in simulator
			 * missions — staying in DISPATCH would loop forever. */
			user_submodal_result = 0;
			int pushed = 0;
			switch ((int16_t)t->screen) {
				case 0:
					if (!mission.train_craft_type) {
						TieGoals_Begin();
						pushed = 1;
					}
					break;
				case 1:
					if (!mission.train_craft_type) {
						/* maproom mutates pstate.target_obj_idx; old/new is
						 * compared on resume to call user_setnewtarget. */
						TieMaproom_Begin();
						pushed = 1;
					}
					break;
				case 2:
					if (!mission.train_craft_type) {
						TieMsgRoom_Begin();
						pushed = 1;
					}
					break;
				case 3:
					if (!mission.train_craft_type) {
						TieDamage_Begin();
						pushed = 1;
					}
					break;
				case 4:
					if (!mission.train_craft_type) {
						TieWingman_Begin();
						pushed = 1;
					}
					break;
				case 5:
					TieHelp_Begin(t->retreat_flag);
					pushed = 1;
					break;
				default:
					break;
			}
			(void)pushed;
			t->phase = INFLIGHT_PHASE_AFTER_SUB;
			return LANDRU_TASK_STEP_CONTINUE;
		}

		case INFLIGHT_PHASE_AFTER_SUB: {
			int32_t sub = user_submodal_result;
			if (mission.end_flag) {
				festring_setfontsize(2);
				imuse_set_master_vol(im, (int16_t)t->saved_master_vol);
				imuse_resume(im);
				/* Retail USER_inflightinfo @ 0x616ec. */
				rtsvga2_invalidatepagecache();
				user_submodal_result = t->screen;
				return LANDRU_TASK_STEP_DONE;
			}

			if (sub == 0) {
				t->exit_flag = 1;
			} else if (sub == 0xFFFF) {
				t->screen--;
				if (t->screen & 0x8000)
					t->screen = 6;
				if (mission.train_craft_type && t->screen == 4)
					t->screen = 6;
				t->retreat_flag = 1;
			} else if (sub != 1) {
				t->screen = -1;
				t->exit_flag = 1;
			} else {
				if (++t->screen > 6)
					t->screen = mission.train_craft_type ? 5 : 0;
				t->retreat_flag = 0;
			}

			t->phase = t->exit_flag ? INFLIGHT_PHASE_FINISH : INFLIGHT_PHASE_DISPATCH;
			return LANDRU_TASK_STEP_CONTINUE;
		}

		case INFLIGHT_PHASE_FINISH:
			inflight_finish(t);
			user_submodal_result = t->screen;
			return LANDRU_TASK_STEP_DONE;
	}
	return LANDRU_TASK_STEP_DONE;
}

static void user_inflightinfo_task_end(void* self) {
	InflightInfoTask* t = (InflightInfoTask*)self;
	TieFlightScreen_SetActive(t->previous_screen);
}

static const LandruTaskVtable user_inflightinfo_task_vt = {
	.step = user_inflightinfo_task_step,
	.end = user_inflightinfo_task_end,
};

void TieInflightInfo_Begin(int32_t screen_id) {
	InflightInfoTask* t = (InflightInfoTask*)landru_task_push(&user_inflightinfo_task_vt);
	if (!t)
		return;
	t->previous_screen = TieFlightScreen_Active();
	t->screen_id = screen_id;
	t->saved_master_vol = 0;
	t->retreat_flag = 0;
	t->exit_flag = 0;
	t->screen = 0;
	t->phase = INFLIGHT_PHASE_BEGIN;
}
