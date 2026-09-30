#include "tie_runtime/runtime/flight_task.h"
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
#include "tie/rand.h"
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

/* Helper: shield-balance / shield-max as 0..3 quartile (used by
 * tie_simulator end-of-mission post_mission_shield_q4 latch). The binary
 * does this same `MATH2_percentage(...) >> 14` in-line; broken out here
 * for readability. */
static int shield_quartile(uint16_t balance, uint16_t maxv) {
	return (int)math2_percentage(balance, maxv) >> 14;
}

typedef enum {
	VIEW_REPLAY_PROMPT_PHASE_RENDER = 0,
	VIEW_REPLAY_PROMPT_PHASE_POLL,
	VIEW_REPLAY_PROMPT_PHASE_AFTER_VIEWER,
} ViewReplayPromptPhase;

typedef struct ViewReplayPromptTask {
	uint16_t saved_master_vol;
	TieFlightScreen previous_screen;
	ViewReplayPromptPhase phase;
} ViewReplayPromptTask;

static LandruTaskStepResult view_replay_prompt_task_step(void* self) {
	ViewReplayPromptTask* t = (ViewReplayPromptTask*)self;

	switch (t->phase) {

		case VIEW_REPLAY_PROMPT_PHASE_RENDER: {
			int32_t margin = screenXRes / 10;
			int32_t right = screenXRes - margin;

			imuse_set_master_vol(im, 0);
			imuse_pause(im);
			if (TieClassicDisplay_UsesDx5())
				FlightSurface_Lock();
			festring_setfontsize(1);

			int32_t top = (screenYRes >> 1) - (int32_t)fontheight - (screenYRes >> 3);
			int32_t bottom = top + 2 * (int32_t)fontheight;

			festring_setbound(0, 0, (int16_t)screenXRes, (int16_t)screenYRes);
			festring_setbackcolor(0x40u);
			clearwindow();
			festring_setbound((int16_t)(margin - 1), (int16_t)(top - 1), (int16_t)(right + 1),
							  (int16_t)(bottom + 1));
			festring_setbackcolor(0x4Au);
			clearwindow();
			festring_setbound((int16_t)margin, (int16_t)top, (int16_t)right, (int16_t)bottom);
			festring_setbackcolor(0x40u);
			clearwindow();
			festring_settextcolor(0x43u);
			festring_setdropcolor(0x41u);
			festring_setcursor((int16_t)(margin + 1), (int16_t)(top + (int32_t)fontheight / 2));
			festring_outstringcenter((const uint8_t*)viewfilmstr);
			unblank();
			if (TieClassicDisplay_UsesDx5()) {
				FlightSurface_Unlock();
				FrontendDisplay_BlitOffscreenToRenderSurface();
				FrontendDisplay_PresentFrame();
			}
			t->phase = VIEW_REPLAY_PROMPT_PHASE_POLL;
			return LANDRU_TASK_STEP_CONTINUE;
		}

		case VIEW_REPLAY_PROMPT_PHASE_POLL: {
			/* Wait for input. YIELD so the next TieRuntime_Tick gets a chance
			 * to refresh the keyboard queue via the host's input pump
			 * (libretro pumps before TieRuntime_Tick; SDL3 same). Spinning
			 * within one TieRuntime_Tick would never see new keystrokes. */
			if (!TieInput_KeyPending())
				return LANDRU_TASK_STEP_YIELD;

			int ch = TieInput_ReadKey();
			if (ch == 'y' || ch == 'Y') {
				blank();
				imuse_set_master_vol(im, (int)t->saved_master_vol);
				imuse_resume(im);
				TieReplaySession_Begin();
				t->phase = VIEW_REPLAY_PROMPT_PHASE_AFTER_VIEWER;
				return LANDRU_TASK_STEP_CONTINUE;
			}
			if (ch == 'n' || ch == 'N') {
				imuse_set_master_vol(im, (int)t->saved_master_vol);
				imuse_resume(im);
				imuse_stop_all_sounds(im);
				return LANDRU_TASK_STEP_DONE;
			}
			/* Any other key: keep polling. */
			return LANDRU_TASK_STEP_CONTINUE;
		}

		case VIEW_REPLAY_PROMPT_PHASE_AFTER_VIEWER:
			/* Replay viewer popped — match the original synchronous flow's
			 * trailing blank() after replayio_replayscreen returned. */
			blank();
			return LANDRU_TASK_STEP_DONE;
	}

	return LANDRU_TASK_STEP_DONE;
}

static void view_replay_prompt_task_end(void* self) {
	ViewReplayPromptTask* t = (ViewReplayPromptTask*)self;
	TieFlightScreen_SetActive(t->previous_screen);
}

static const LandruTaskVtable view_replay_prompt_task_vt = {
	.step = view_replay_prompt_task_step,
	.end = view_replay_prompt_task_end,
};

static void view_replay_prompt_Push_Task(uint16_t saved_master_vol) {
	ViewReplayPromptTask* t = (ViewReplayPromptTask*)landru_task_push(&view_replay_prompt_task_vt);
	if (!t)
		return;
	t->previous_screen = TieFlightScreen_SetActive(TIE_FLIGHT_SCREEN_REPLAY_PROMPT);
	t->saved_master_vol = saved_master_vol;
	t->phase = VIEW_REPLAY_PROMPT_PHASE_RENDER;
}

typedef struct FlightTask {
	/* 1 = the previous step pushed TieReplaySession_Begin; on
	 * this step the viewer has popped and we owe the player a RESUMED
	 * banner (matches the original synchronous code which posted
	 * MSG_RESUMED right after replayio_replayscreen returned). */
	uint8_t resumed_banner_pending;
	uint8_t rebase_pending;
} FlightTask;

static LandruTaskStepResult flight_hyper_step(void* self) {
	(void)self;
	if (!hyperspaceflag)
		return LANDRU_TASK_STEP_DONE;
	/* tie_doframe returns false when its PIT-tick budget hasn't
	 * landed yet — yield so the next TieRuntime_Tick advances xtimer. */
	return tie_doframe() ? LANDRU_TASK_STEP_FRAME_COMPLETE : LANDRU_TASK_STEP_YIELD;
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
	int32_t pending = user_consume_info_room_request();
	if (pending >= 0) {
		TieInflightInfo_Begin(pending);
		t->rebase_pending = 1;
		return LANDRU_TASK_STEP_CONTINUE;
	}

	/* Pending replay-viewer request from the 'v' keybind? Push the
	 * viewer on top and arm the post-pop RESUMED banner. */
	if (user_consume_replay_viewer_request()) {
		TieReplaySession_Begin();
		t->resumed_banner_pending = 1;
		t->rebase_pending = 1;
		return LANDRU_TASK_STEP_CONTINUE;
	}

	/* See flight_hyper_step — yield when tie_doframe couldn't
	 * consume its PIT-tick budget yet this TieRuntime_Tick. */
	return tie_doframe() ? LANDRU_TASK_STEP_FRAME_COMPLETE : LANDRU_TASK_STEP_YIELD;
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
	(void)landru_task_push(&flight_mission_task_vt);
}

typedef enum {
	TIE_SIM_PHASE_INIT = 0,
	TIE_SIM_PHASE_LOADSCREEN_HOLD,
	TIE_SIM_PHASE_AFTER_HYPER,
	TIE_SIM_PHASE_AFTER_REPLAY_VIEWER,
	TIE_SIM_PHASE_AFTER_MISSION,
	TIE_SIM_PHASE_AFTER_REPLAY_PROMPT,
	TIE_SIM_PHASE_AFTER_INFOROOM,
	TIE_SIM_PHASE_TEARDOWN,
} TieSimPhase;

typedef struct TieSimulatorTask {
	int replay_mode;
	TieSimPhase phase;
	/* Saved across the hyperspace cinematic so AFTER_HYPER can restore
	 * the player's draw-backdrop / draw-debris preferences after the
	 * cinematic stomps them. Only valid between INIT and AFTER_HYPER. */
	uint8_t saved_drawbackdrop;
	uint8_t saved_drawdebris;
	/* Synthetic-clock stamp of the tick that first presented the
	 * "Initializing Combat Sequence..." banner. LOADSCREEN_HOLD yields
	 * until TIE_SIM_LOADSCREEN_MIN_US have elapsed past it. */
	uint64_t loadscreen_shown_us;
} TieSimulatorTask;

void TieFlightRuntime_ReleaseRecoveredResources(void) {
	RenderTexture_ReleaseMissionCaches();
	TieRuntime_RequestFlightResourceRelease();
}

/* Minimum on-screen time for the loading banner. Retail never needed
 * one — visibility came from multi-second CD/floppy I/O covering the
 * mission + species load. Modern hosts load in milliseconds, so the
 * banner frame would be overdrawn by the first flight frame within the
 * same tick without an explicit hold. */
#define TIE_SIM_LOADSCREEN_MIN_US (2u * 1000u * 1000u)

/* RECOVERY HELPER: removes the identical TIE98 replay/live star-surface
 * initialization sequences in TIE_simulator. */
static void tie_prepare_star_surface_tie98(void) {
	Tie98StarColors_Invalidate();
	FlightSurface_Lock();
	worldeyeA1 = 0;
	worldeyeA2 = 0;
	worldeyeA3 = 0;
	worldeyeB1 = 0;
	worldeyeB2 = 0;
	worldeyeB3 = 0;
	worldeyeC1 = 0;
	worldeyeC2 = 0;
	worldeyeC3 = 0;
	pixelswide = 0;
	pixelsdeep = 0;
	rtsvga2_drawstars_tie98();
	FlightSurface_Unlock();
}

/* Common live-path mission setup, shared between the hyperspace and
 * no-hyperspace INIT branches. Builds the mission, seeds the gates /
 * train-level UI, prints the HYPER_COMPLETED / BONUS_PRIOR_LEVELS
 * messages, waits for the first XTIMER tick, and pushes the main
 * flight task. */
static void tie_reset_flight_timing_session_state(void) {
	TieInput_ResetThrottle();
	inputthrottle = UINT32_MAX;
	TieFlightTiming_BeginSession(TieProfile_Flight());
	TieFlightTimingState_Reset();
	TieAiLead_Reset();
	TieChaseCamera_Reset();
}

static void tie_simulator_setup_mission_and_push(void) {
	const bool tie98_display = TieClassicDisplay_UsesDx5();
	if (tie98_display)
		FlightSurface_Lock();
	TIE_FLIGHT_TRACE_BEGIN_MISSION(missionfilename);
	create_createmission();
	tie_reset_flight_timing_session_state();
	if (tie98_display) {
		FlightSurface_Unlock();
		FrontendDisplay_BlitOffscreenToRenderSurface();
		FrontendDisplay_PresentFrame();
		FrontendDisplay_BlitOffscreenToRenderSurface();
	}
	/* Retail clears colorcycleuserflag unconditionally after the
	 * live mission is built, not just inside the hyperspace branch.
	 * The hyperspace path already cleared it; do it again here so a
	 * mission that skips the hyper-in still starts with cycling
	 * disabled. */
	colorcycleuserflag = 0;
	if (mission.train_craft_type) {
		if (tie98_display)
			FlightSurface_Lock();
		gate_createtraininggates();
		gate_settraininglevel(mission.train_level);
		if (tie98_display)
			FlightSurface_Unlock();
	}
	TIE_FLIGHT_TRACE_MISSION_CREATED();

	if (pstate.player_fg_idx < 48 && !fg_array[pstate.player_fg_idx].start_fg_used &&
		spec_data[pstate.player_spec_num].has_hyperdrive)
		msg_messageprintf(MSG_HYPER_COMPLETED);

	if (mission.train_craft_type && mission.train_level > 1u) {
		argtable[0] = (uint16_t)(mission.train_level - 1);
		msg_messageprintf(MSG_BONUS_PRIOR_LEVELS);
		/* RETAIL: seed mission.mission_score with the prior-levels
		 * bonus (10000 per level). Demo only printed the message. */
		mission.mission_score = 10000 * ((int)mission.train_level - 1);
	}

	/* Main flight loop is a task. The outer `while (mission.end_flag
	 * == 0)` is gone; the task runs tie_doframe per step and pops
	 * when end_flag is set. tie_doframe handles the active admission floor
	 * (the host's TieRuntime_Tick advances sim_clock, which xtimer reads
	 * via TieSimClockCursor on the first call to seed the cursor) and
	 * pause sub-state. Route through the wrapper so the snapshot's
	 * scene_kind gets tagged TIE_SCENE_FLIGHT for HD renderers — the
	 * direct landru_task_push call here used to skip that tag. */
	if (TieMusicPolicy_UsesTie98())
		tie_start_tie98_mission_music();
	TieFlightTask_BeginMission();
}

static LandruTaskStepResult simulator_initialize(TieSimulatorTask* t) {
	tie_reset_flight_timing_session_state();
	/* PORT: loading and replay setup both own the selected flight
	 * version's framebuffer without starting the HD flight view.
	 * Stand-alone replay mode was selected explicitly by SHELL_Shell. */
	TieSnapshotBuilder_SetSceneKind(TIE_SCENE_FLIGHT_LOADING);
	const bool display_active = t->replay_mode
									? TieClassicDisplay_ActivateFlightMode((uint16_t)flightResolution)
									: TieClassicDisplay_ActivateFlight();
	if (!display_active) {
		xerror_Set_Landru_Error(12);
		TieFlightTiming_EndSession();
		return LANDRU_TASK_STEP_DONE;
	}
	const bool tie98_display = TieClassicDisplay_UsesDx5();
	/* --- Common engine init ------------------------------------ */
	deadflag_EB76C = 0; /* RETAIL-only init; no live reader */
	deadflag_EB774 = 1; /* RETAIL-only init (LOBYTE write) */
	maingameflag = 0;   /* moved up vs demo to match retail order */
	replaymaxcnt = (int32_t)TieFlightTiming_RecordFrameLimit();
	panels_in_ems = 0; /* PANEL_tryEMSforpanels later sets to 1 */
	blastcount = 0;
	g_engineSoundPreviousPlayerSpecies = -1;
	rotscale_invalidate_linedata(); /* drop any stale ROTSCALE line cache */
	replayspoolflag = 1;
	/* RETAIL: math2_setrandomseed() NOT called here (binary calls nullsub_3,
	 * a one-byte ret). The RNG seed survives from whatever called us. */
	/* math2_setrandomseed();   // demo-only */

	tie_initflightresolution();
	rtsvga2_blankVGA(); /* RETAIL-only */
	if (tie98_display)
		FlightSurface_Lock();
	rtsvga2_initgraphVGA();
	if (tie98_display) {
		FlightSurface_Unlock();
	}
	feinput_setupgraphics(3u); /* RETAIL: was 2 in demo */

	graphicsinit = 1;
	colorcycleflag = 1;
	palette_cycle_user = 1; /* RETAIL user "Color Cycling" option ON */
	colorcycleuserflag = 1;
	/* NB: musicflag/debugnum/soundinit/outputflag/blankcondition are NOT
	 * touched here. The demo writes them (=0/=1) as part of the init;
	 * retail does not — they keep whatever value they had at engine boot. */

	if (tie98_display) {
		maingameflag = 1;
		FlightSurface_Lock();
	}
	fediskio_Init_Buffers_and_Fonts();
	if (tie98_display) {
		FlightSurface_Unlock();
		maingameflag = 0;
		FrontendDisplay_BlitOffscreenToRenderSurface();
		FrontendDisplay_PresentFrame();
	}
	mapiconsloaded = 0; /* retail byte_CD1C5 = 0 between buffers
						   init and palette load; binary parity. */
	if (!TieProfile_UsesTie98Logic()) {
		fediskio_readfiletofarmemory(TIE_FILE_ROOT_FLIGHT_ASSET, "VGA.PAC", xtransdataptr);
		buildpalette(xtransdataptr, 64, 192);
	}
	if (tie98_display)
		FlightSurface_Lock();
	feinput_setupinputdevices();
	if (tie98_display)
		FlightSurface_Unlock();

	/* RETAIL-only: training-filename auto-detect. */
	if (special_features_flag) {
		if (missionfilename[0] == 't')
			mission.train_craft_type_src = 5;
		else
			mission.train_craft_type_src = 0;
	}

	/* Init 256 (idx, palette-delta) star pairs. The loop runs i = 0, 2, 4, ...
	 * because each pair is 2 bytes. Re-pick 'pos' until it lands in the
	 * 0..0x7C range (so the 5x5x5 grid lookup never overflows). */
	for (int16_t i = 0; i < 511; i += 2) {
		/* Retail TIE_simulator uses RAND_rand (standard LCG), NOT
		 * MATH2_getrandom (mission-RNG LFSR). The starfield positions
		 * must be non-deterministic w.r.t. mission replay state — stars
		 * are purely cosmetic and don't feed into sim/replay streams. */
		do {
			stars[i] = (uint8_t)(rand_rand() & 0x7F);
		} while (stars[i] > 0x7C);
		stars[i + 1] = (uint8_t)(rand_rand() & 3);
	}

	starcol1 = 0;
	lightX = 18000;
	lightY = -18000;
	/* TIE95 retail (0x55A60) uses lightZ = -18000; TIE98
	 * TIE_simulator (0x48D0B9) flips it to +18000. */
	lightZ = TieProfile_UsesTie98Logic() ? 18000 : -18000;
	colorcycleflag = 1;
	TieInflightOptions_Apply();
	shipdetailvalue = -1;
	shipdetailpolycnt = 16;

	if (t->replay_mode) {
		/* --- Replay-only path -------------------------------- */
		gamesnd_game_Open_iMuse(); /* RETAIL: was End_Transition in demo */
		hyperspaceflag = 0;
		entercombatflag = 0;
		colorcycleuserflag = 0; /* RETAIL-only extra reset */
		if (tie98_display)
			tie_prepare_star_surface_tie98();
		TieReplaySession_Begin();
		t->phase = TIE_SIM_PHASE_AFTER_REPLAY_VIEWER;
		return LANDRU_TASK_STEP_CONTINUE;
	}

	/* --- Live mission path ----------------------------------- */
	maingameflag = 1;
	fediskio_createpilotrecord();
	/* Reset the message-room ring before the mission starts. Retail
	 * writes word_C5888 = -1 (lasthistorymsg) and word_C588A = 0
	 * (numhistorymsgs) here so the msg-room starts empty for each
	 * flight. Without these clears, stale messages from the previous
	 * flight persist. */
	lasthistorymsg = -1;
	numhistorymsgs = 0;
	panelflag = 0;
	replayavailable = 0;

	if (tie98_display)
		FlightSurface_Lock();
	create_loadmission(missionfilename);
	if (tie98_display) {
		FlightSurface_Unlock();
		FrontendDisplay_BlitOffscreenToRenderSurface();
		FrontendDisplay_PresentFrame();
	}
	fediskio_loadspecies();
	gamesnd_game_Open_iMuse(); /* RETAIL: was End_Transition in demo */
	if (tie98_display)
		tie_prepare_star_surface_tie98();

	/* Loading is done, but the "Initializing Combat Sequence..."
	 * banner painted by fediskio_Init_Buffers_and_Fonts has not
	 * reached the screen yet — the application presents after this tick's
	 * step chain settles. YIELD (don't block) into LOADSCREEN_HOLD
	 * so the banner frame is presented, then hold it for a minimum
	 * display time while ticks keep driving the 0xFA palette pulse.
	 * Retail got this pause implicitly from slow CD I/O. */
	t->loadscreen_shown_us = TieSimClock_NowUs();
	t->phase = TIE_SIM_PHASE_LOADSCREEN_HOLD;
	return LANDRU_TASK_STEP_YIELD;
}

// ORIGINAL_FUNCTION: TIE95 0x55A60
// ORIGINAL_FUNCTION: TIE98 0x48CF10
// (task-split recovery)
static LandruTaskStepResult tie_simulator_task_step(void* self) {
	TieSimulatorTask* t = (TieSimulatorTask*)self;

	switch (t->phase) {
		case TIE_SIM_PHASE_INIT:
			return simulator_initialize(t);

		case TIE_SIM_PHASE_LOADSCREEN_HOLD: {
			if (TieSimClock_NowUs() - t->loadscreen_shown_us < TIE_SIM_LOADSCREEN_MIN_US)
				return LANDRU_TASK_STEP_YIELD;

			/* Hyperspace-in cinematic gate: run the warp-in sequence when the
			 * player's FG hasn't already started AND transitions are on AND
			 * the player's craft has a hyperdrive. */
			if (pstate.player_fg_idx < 48 && !fg_array[pstate.player_fg_idx].start_fg_used &&
				transitions_on && spec_data[pstate.player_spec_num].has_hyperdrive) {
				t->saved_drawbackdrop = drawbackdropflag;
				t->saved_drawdebris = drawdebrisflag;

				if (TieClassicDisplay_UsesDx5())
					FlightSurface_Lock();
				create_createhyperin();
				if (TieClassicDisplay_UsesDx5())
					FlightSurface_Unlock();
				hyperspaceflag = 2;
				colorcycleuserflag = 0;
				drawbackdropflag = 0;
				drawdebrisflag = 0;
				anim_dohyperspace();
				TieSnapshotBuilder_SetSceneKind(TIE_SCENE_FLIGHT);
				(void)landru_task_push(&flight_hyper_task_vt);
				t->phase = TIE_SIM_PHASE_AFTER_HYPER;
				return LANDRU_TASK_STEP_CONTINUE;
			}

			/* No hyperspace cinematic: jump straight to mission setup. */
			tie_simulator_setup_mission_and_push();
			t->phase = TIE_SIM_PHASE_AFTER_MISSION;
			return LANDRU_TASK_STEP_CONTINUE;
		}

		case TIE_SIM_PHASE_AFTER_REPLAY_VIEWER:
			/* Replay viewer task popped. Replay branch skips the live
			 * end-of-mission UI block; jump straight to TEARDOWN. */
			t->phase = TIE_SIM_PHASE_TEARDOWN;
			return LANDRU_TASK_STEP_CONTINUE;

		case TIE_SIM_PHASE_AFTER_HYPER:
			/* Hyperspace cinematic finished; restore the player's draw
			 * preferences, clear the hyperspace-tunnel particle 'used'
			 * flags, reload the mission cleanly, then run mission setup
			 * and push the main flight task. */
			drawbackdropflag = t->saved_drawbackdrop;
			drawdebrisflag = t->saved_drawdebris;

			/* Clear the 0x10 'used' flag on the hyperspace-tunnel particle
			 * species (114/115/116) so the next mission starts clean. */
			species_table[114].load_flags &= ~0x10u;
			species_table[115].load_flags &= ~0x10u;
			species_table[116].load_flags &= ~0x10u;
			hyperspaceflag = 0;

			/* Reload the mission cleanly. RETAIL passes 4 args; the
			 * extras are init-overrides which the demo path ignores. */
			if (TieClassicDisplay_UsesDx5())
				FlightSurface_Lock();
			create_loadmission(missionfilename);
			if (TieClassicDisplay_UsesDx5())
				FlightSurface_Unlock();

			tie_simulator_setup_mission_and_push();
			t->phase = TIE_SIM_PHASE_AFTER_MISSION;
			return LANDRU_TASK_STEP_CONTINUE;

		case TIE_SIM_PHASE_AFTER_MISSION:
			/* End-of-mission state. */
			TIE_FLIGHT_TRACE_END_MISSION();
			if (TieMusicPolicy_UsesTie98())
				CDAUDIO_Close_Device();
			if (mission.player_status < 10u || mission.end_flag == 2) {
				/* Binary dereferences player_craft unconditionally here. */
				pstate.post_mission_shield_q4 =
					(uint8_t)shield_quartile(pstate.player_craft->hull_damage, pstate.player_craft->hull_max);
				if (mission.end_flag == 2)
					mission.player_status = 3;
				blank();

				if (replayavailable) {
					/* Replay prompt is now a sub-task: push it and yield;
					 * AFTER_REPLAY_PROMPT runs the post-prompt info-room +
					 * pilot-record update once the prompt (and any chained
					 * replay viewer it pushed) pops. */
					uint16_t saved_vol = (uint16_t)imuse_get_master_vol(im);
					view_replay_prompt_Push_Task(saved_vol);
					t->phase = TIE_SIM_PHASE_AFTER_REPLAY_PROMPT;
					return LANDRU_TASK_STEP_CONTINUE;
				}

				if (mission.train_craft_type == 0) {
					/* Push the in-flight info task and yield; the
					 * AFTER_INFOROOM phase resumes once it pops to run
					 * the post-info blank + pilot-record update. */
					TieInflightInfo_Begin(0);
					t->phase = TIE_SIM_PHASE_AFTER_INFOROOM;
					return LANDRU_TASK_STEP_CONTINUE;
				}
				if (mission.player_status == 3)
					fediskio_updatepilotrecord(0, 0);
			} else {
				blank();
			}
			t->phase = TIE_SIM_PHASE_TEARDOWN;
			return LANDRU_TASK_STEP_CONTINUE;

		case TIE_SIM_PHASE_AFTER_REPLAY_PROMPT:
			/* Replay prompt task popped (and any replay viewer it chained
			 * has popped too — both are nested below us on the task
			 * stack). Run the post-prompt block from AFTER_MISSION's
			 * synchronous tail: optional info-room push, optional pilot-
			 * record update. */
			if (mission.train_craft_type == 0) {
				TieInflightInfo_Begin(0);
				t->phase = TIE_SIM_PHASE_AFTER_INFOROOM;
				return LANDRU_TASK_STEP_CONTINUE;
			}
			if (mission.player_status == 3)
				fediskio_updatepilotrecord(0, 0);
			t->phase = TIE_SIM_PHASE_TEARDOWN;
			return LANDRU_TASK_STEP_CONTINUE;

		case TIE_SIM_PHASE_AFTER_INFOROOM:
			blank();
			if (mission.player_status == 3)
				fediskio_updatepilotrecord(0, 0);
			t->phase = TIE_SIM_PHASE_TEARDOWN;
			return LANDRU_TASK_STEP_CONTINUE;

		case TIE_SIM_PHASE_TEARDOWN:
			/* --- Tear-down ----------------------------------------- */
			TieFlightTiming_EndSession();
			imuse_stop_all_sounds(im);
			imuse_filelist_unload_all(im);
			if (TieProfile_UsesTie98Logic()) {
				colorcycleflag = 0;
				fediskio_FreeFlightHandles();
				TieFlightRuntime_ReleaseRecoveredResources();
				maingameflag = 0;
				gamesnd_Transition_Sound();
				if (!TieClassicDisplay_ActivateFrontend()) {
					xerror_Set_Landru_Error(12);
					return LANDRU_TASK_STEP_DONE;
				}
				rtsvga2_clearflightdisplay();
			} else {
				gamesnd_Transition_Sound();
				colorcycleflag = 0;
				fediskio_FreeFlightHandles();
				TieFlightRuntime_ReleaseRecoveredResources();
				/* PORT: TIE95 has no display object selected by maingameflag.
				 * Clear it before restoring a TIE98 frontend's Landru surface. */
				if (TieProfile_FrontendId() == TIE_FRONTEND_PROFILE_TIE98)
					maingameflag = 0;
				if (!TieClassicDisplay_ActivateFrontend()) {
					xerror_Set_Landru_Error(12);
					return LANDRU_TASK_STEP_DONE;
				}
			}
			return LANDRU_TASK_STEP_DONE;
	}

	/* Unreachable; keeps -Wreturn-type happy. */
	return LANDRU_TASK_STEP_DONE;
}

static const LandruTaskVtable tie_simulator_task_vt = {
	.step = tie_simulator_task_step,
};

void TieFlightTask_Begin(int replay_mode) {
	TieSimulatorTask* t = (TieSimulatorTask*)landru_task_push(&tie_simulator_task_vt);
	if (!t)
		return;
	t->replay_mode = replay_mode;
	t->phase = TIE_SIM_PHASE_INIT;
}
