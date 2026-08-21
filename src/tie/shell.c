#include "tie/shell.h"
#include "tie/gamesnd.h"
#include "tie/shipext.h"
#include "tie/soundext.h"
#include "tie/tie.h"
#include "tie/wavestream_tie98.h"
#include "tie_runtime/audio/config.h"
#include "tie_runtime/audio/imuse_session.h"
#include "tie_runtime/audio/music_policy.h"
#include "tie_runtime/diagnostics/diagnostics.h"
#include "tie_runtime/display/classic_display.h"
#include "tie_runtime/display/classic_framebuffer.h"
#include "tie_runtime/flight_assets/model_types.h"
#include "tie_runtime/input/input.h"
#include "tie_runtime/runtime/exports.h"
#include "tie_runtime/runtime/profile.h"
#include "tie_runtime/snapshot/snapshot.h"
#include "tie_runtime/snapshot/snapshot_internal.h"
#include "tie_runtime/storage/storage.h"
#include <landru/task.h>

#include "tie/armship.h"
#include "tie/blueprnt.h"
#include "tie/brief.h"
#include "tie/combat.h"
#include "tie/credits.h"
#include "tie/debrief.h"
#include "tie/filmview.h"
#include "tie/frontend_display_tie98.h"
#include "tie/mainmenu.h"
#include "tie/map.h"
#include "tie/play1.h"
#include "tie/register.h"
#include "tie/talk.h"
#include "tie/tielogo.h"
#include "tie/title.h"
#include "tie/tourdesk.h"
#include "tie/train.h"
#include "tie_runtime/runtime/profile.h"

#include "landru/canvas.h"
#include "landru/error.h"
#include "landru/pal.h"
#include "landru/stream.h"
#include "landru/surface.h"
#include "landru/view.h"

#include <imuse/lolevel.h>

#include <stdlib.h>

/* --- Globals --- */

// GLOBAL: TIE98 0x5F3464
SceneHeadStruct* sHead_gbl;
int16_t digital_exists;

/* Stream buffer sizing (retail picks these dynamically from Total_Handle_Mem;
 * we keep a fixed pair since our platform uses malloc and has no tight memory
 * budget). Retail caps: buffer <= 6MB, prefetch <= 2MB at 75% of buffer. */
#define SHELL_STREAM_BUFFER_BYTES (2 * 1024 * 1024)
#define SHELL_STREAM_PREFETCH_BYTES ((SHELL_STREAM_BUFFER_BYTES * 75) / 100)

/* --- Scene classification --- */

/*
 * PLAY1 cutscenes that keep the Landru screen-diff optimisation active.
 * Retail dispatcher labels this as LABEL_171 (0x67a22).
 */
static int is_play1_scene(int16_t scene) {
	switch (scene) {
		case 5:
		case 6:
		case 7:   /* 0x05-0x07 */
		case 19:  /* 0x13 */
		case 25:  /* 0x19 */
		case 50:  /* 0x32 */
		case 60:  /* 0x3C */
		case 120: /* 0x78 */
		case 130: /* 0x82 */
		case 170: /* 0xAA */
		case 210:
		case 211:
		case 212:
		case 213: /* 0xD2-0xD5; retail explicitly lists 210 and 531-related */
		case 231: /* 0xE7 */
		case 240: /* 0xF0 */
		case 250:
		case 251:
		case 252:
		case 253:
		case 254:
		case 255:
		case 256:
		case 257:
		case 258:
		case 259:
		case 260:
		case 261:
		case 262:
		case 263: /* 0xFA-0x107 */
		case 270: /* 0x10E */
		case 280:
		case 281:
		case 282:
		case 283:
		case 284:
		case 285: /* 0x118-0x11D */
		case 390: /* 0x186 */
		case 400:
		case 401:
		case 402:
		case 403:
		case 404:
		case 405:
		case 406:
		case 407:
		case 408:
		case 409:
		case 410:
		case 411: /* 0x190-0x19B */
		case 420: /* 0x1A4 */
		case 500: /* 0x1F4 */
		case 510: /* 0x1FE */
		case 520: /* 0x208 */
		case 530:
		case 531: /* 0x212-0x213 */
		case 540: /* 0x21C */
		case 550: /* 0x226 */
		case 560: /* 0x230 */
		case 570:
		case 571:
		case 572:
		case 573: /* 0x23A-0x23D */
		case 580:
		case 581: /* 0x244-0x245 */
		case 590:
		case 591: /* 0x24E-0x24F */
		case 700: /* 0x2BC */
		case 710: /* 0x2C6 */
		case 720: /* 0x2D0 */
		case 730: /* 0x2DA */
		case 740: /* 0x2E4 - retail adds this vs demo */
		case 900: /* 0x384 */
			return 1;
		default:
			return 0;
	}
}

/*
 * PLAY1 cutscenes that need screen-diff disabled (full-screen FMV).
 * Retail dispatcher labels this as LABEL_172 (0x67a34).
 */
static int is_play1_nodiff_scene(int16_t scene) {
	switch (scene) {
		case 10: /* 0x0A */
		case 20: /* 0x14 */
		case 30:
		case 31:
		case 32: /* 0x1E-0x20 */
		case 40: /* 0x28 */
		case 61: /* 0x3D */
		case 70:
		case 71:
		case 72: /* 0x46-0x48 */
		case 600:
		case 601:
		case 602:
		case 603: /* 0x258-0x25B */
		case 610: /* 0x262 */
		case 620:
		case 621:
		case 622:
		case 623: /* 0x26C-0x26F */
			return 1;
		default:
			return 0;
	}
}

/* --- Flight helpers --- */

/*
 * Scene-4 (battle) post-flight pilot bookkeeping. If the mission failed
 * AND the player is still alive, take the "save progress" branch: read
 * the temp pilot from disk and commit it. Otherwise link (retain pilot
 * as-was at mission start).
 */
static void handle_battle_mission_exit(void) {
	if (shipext_Is_Mission_Success() || !shipext_Is_Player_OK()) {
		shipext_Link_Pilot();
	} else {
		if (shipext_Read_Temp_Pilot())
			shipext_Update_Pilot();
	}
}

typedef enum {
	TIE_FLIGHT_KIND_TRAIN_COMBAT = 0, /* SCENE_FLIGHT_TRAIN, SCENE_FLIGHT_COMBAT */
	TIE_FLIGHT_KIND_BATTLE,           /* SCENE_FLIGHT_BATTLE */
	TIE_FLIGHT_KIND_FILM_REPLAY,      /* SCENE_FILM_REPLAY */
} FlightSceneKind;

typedef enum {
	TIE_FLIGHT_PHASE_BEGIN = 0,
	TIE_FLIGHT_PHASE_AFTER_FLIGHT,
} FlightScenePhase;

typedef struct FlightSceneTask {
	int16_t cur_scene;
	FlightSceneKind kind;
	FlightScenePhase phase;
} FlightSceneTask;

// FUNCTION: TIE 0x672D5, TIE98 0x47EF60 (flight-scene task split)
static LandruTaskStepResult flight_scene_step(void* self) {
	FlightSceneTask* t = (FlightSceneTask*)self;

	switch (t->phase) {
		case TIE_FLIGHT_PHASE_BEGIN:
			/* PORT: configuration changes take effect at the next playable mission.
			 * Film Room playback continues to use the currently active profile. */
			if (t->kind != TIE_FLIGHT_KIND_FILM_REPLAY && !TieProfile_ApplyPendingFlight()) {
				TieDiagnostics_Fatal("Could not prepare the selected flight engine.");
				return LANDRU_TASK_STEP_DONE;
			}
			if (TieProfile_UsesDx5())
				g_frontendDisplayWndProcMode = 0;
			if (t->kind == TIE_FLIGHT_KIND_FILM_REPLAY) {
				gamesnd_game_Set_Flight_Sound();
				flightResolution = TieProfile_UsesTie98Logic() ? TIE_FLIGHT_RES_SVGA : TIE_FLIGHT_RES_VGA;
				lstream_Exit_Stream_Engine();
				tie_Push_Simulator_Task(1);
			} else {
				shipext_Mission_Enter(t->cur_scene);
				gamesnd_game_Set_Flight_Sound();
				lstream_Exit_Stream_Engine();
				/* Pull the user's "Transitions" pref into the live flag.
				 * Retail does `transitions_on = BYTE2(dword_F503E)` here
				 * (the low byte of options_gbl.transition_active). Without
				 * this refresh the flag keeps whatever value it had from
				 * the previous scene. */
				transitions_on = (uint8_t)options_gbl.transition_active;
				tie_Push_Simulator_Task(0);
			}
			t->phase = TIE_FLIGHT_PHASE_AFTER_FLIGHT;
			return LANDRU_TASK_STEP_CONTINUE;

		case TIE_FLIGHT_PHASE_AFTER_FLIGHT: {
			int16_t next;
			/* PORT: the simulator has restored the frontend framebuffer; make
			 * palette and presentation dispatch frontend-owned from here on. */
			TieSnapshotBuilder_SetSceneKind(TIE_SCENE_FRONTEND);
			if (TieProfile_UsesDx5())
				g_frontendDisplayWndProcMode = TieProfile_FrontendId() == TIE_FRONTEND_PROFILE_TIE98 ? 1 : -1;

			shipext_Reset_Battle_Results();
			lstream_Init_Stream_Engine(SHELL_STREAM_BUFFER_BYTES, SHELL_STREAM_PREFETCH_BYTES);
			lpal_Set_Screen_RGB(0, 255, 0, 0, 0);
			gamesnd_game_Set_Front_Sound();

			if (t->kind == TIE_FLIGHT_KIND_BATTLE) {
				(void)shipext_Mission_Exit(t->cur_scene, mission.player_status);
				handle_battle_mission_exit();
				shipext_Set_Mission_Cutscenes();
				next = shipext_Next_Battle_Cutscene();
			} else {
				next = shipext_Mission_Exit(t->cur_scene, mission.player_status);
			}
			next = shellext_Convert_Transition(next, 0);
			soundext_Prep_Sound_Scene(next);
			shellext_Set_Prefs_Sound();

			/* Hand the next scene back to ShellTask via the Landru exit
			 * latch (same channel the Layer-3 scene tasks use). */
			lerror_Set_Landru_Exit(next);
			return LANDRU_TASK_STEP_DONE;
		}
	}
	return LANDRU_TASK_STEP_DONE;
}

static const LandruTaskVtable flight_scene_task_vt = {
	.step = flight_scene_step,
};

static void flight_scene_push(int16_t scene, FlightSceneKind kind) {
	FlightSceneTask* t = (FlightSceneTask*)landru_task_push(&flight_scene_task_vt);
	if (!t)
		return;
	t->cur_scene = scene;
	t->kind = kind;
	t->phase = TIE_FLIGHT_PHASE_BEGIN;
}

/* --- Public API --- */

typedef enum {
	SHELL_PHASE_DISPATCH = 0,      /* pick scene; push converted task or run sync */
	SHELL_PHASE_AWAITING,          /* converted scene task on stack; we resume on its pop */
	SHELL_PHASE_AFTER_SUDDEN_FADE, /* sudden-end fade pushed during close; finalize after pop */
} ShellPhase;

typedef struct ShellTask {
	SceneHeadStruct the_head;
	int16_t cur_scene;
	int16_t next_scene; /* exit code captured at AWAITING for use after sudden-fade */
	int16_t exit_flag;
	bool diff_disabled; /* play1 nodiff scenes — re-enable on pop */
	bool owns_tie98_music;
	ShellPhase phase;
} ShellTask;

static const char* shell_tie98_music_path(int16_t scene) {
	switch (scene) {
		case SCENE_MAIN_MENU:
			return "music/concourse.wav";
		case SCENE_BLUEPRINT:
			return "music/tech.wav";
		case SCENE_TOUR_DESK:
		case SCENE_BRIEF_PRE:
		case SCENE_BRIEF:
		case SCENE_DEBRIEF:
			return "music/battle.wav";
		case SCENE_TALK_BRIEF_OFFICER:
			return "music/bridge.wav";
		case SCENE_TALK_BRIEF_PRIEST:
			return "music/secret.wav";
		case SCENE_TALK_DEBRIEF_OFFICER:
			return shipext_Is_Mission_Success() ? "music/phew.wav" : "music/bummer.wav";
		case SCENE_TALK_DEBRIEF_PRIEST:
			return mission.secondary_global == 1 ? "music/awe.wav" : "music/evilmonk.wav";
		default:
			return NULL;
	}
}

/* Pure scene → task dispatcher. Pushes the appropriate scene task on
 * the tie_core task stack and returns 1; ShellTask transitions to
 * AWAITING and resumes once the pushed task pops. Returns 0 if the
 * scene id is unknown — ShellTask treats that as a fatal mismatch and
 * exits the dispatcher loop (mirrors retail LABEL_205). May set
 * t->diff_disabled to flag post-pop screen-diff re-enable for play1
 * nodiff scenes. */
static int shell_dispatch_converted(int16_t cur_scene, ShellTask* t) {
	if (cur_scene == SCENE_CREDITS || cur_scene == SCENE_CREDITS_ALT) {
		credits_Push_Credits_Task(sHead_gbl);
		return 1;
	}
	if (cur_scene == SCENE_TIELOGO) {
		tielogo_Push_TieLogo_Task(sHead_gbl);
		return 1;
	}
	if (cur_scene == SCENE_MAIN_MENU) {
		mainmenu_Push_Main_Menu_Task(sHead_gbl);
		return 1;
	}
	if (cur_scene == SCENE_TITLE) {
		title_Push_Title_Task(sHead_gbl);
		return 1;
	}
	if (cur_scene == SCENE_ARM_SHIP) {
		armship_Push_ArmShip_Task(sHead_gbl);
		return 1;
	}
	if (cur_scene == SCENE_BRIEF_PRE || cur_scene == SCENE_BRIEF) {
		brief_Push_Brief_Task(sHead_gbl);
		return 1;
	}
	if (cur_scene == SCENE_DEBRIEF) {
		debrief_Push_Debrief_Task(sHead_gbl);
		return 1;
	}
	if (cur_scene == SCENE_BLUEPRINT) {
		blueprnt_Push_Blueprint_Task(sHead_gbl);
		return 1;
	}
	if (cur_scene == SCENE_TOUR_DESK) {
		tourdesk_Push_TourDesk_Task(sHead_gbl);
		return 1;
	}
	if (cur_scene == SCENE_FILM_VIEWER) {
		filmview_Push_FilmView_Task(sHead_gbl);
		return 1;
	}
	if (cur_scene == SCENE_TRAIN_A || cur_scene == SCENE_TRAIN_B) {
		train_Push_Train_Task(sHead_gbl);
		return 1;
	}
	if (cur_scene == SCENE_COMBAT_A || cur_scene == SCENE_COMBAT_B) {
		combat_Push_Combat_Task(sHead_gbl);
		return 1;
	}
	if (cur_scene == SCENE_REGISTER || cur_scene == SCENE_EXIT) {
		register_Push_Register_Task(sHead_gbl);
		return 1;
	}
	if (cur_scene == SCENE_TALK_BRIEF_OFFICER || cur_scene == SCENE_TALK_BRIEF_PRIEST ||
		cur_scene == SCENE_TALK_DEBRIEF_OFFICER || cur_scene == SCENE_TALK_DEBRIEF_PRIEST) {
		talk_Push_Talk_Task(sHead_gbl);
		return 1;
	}
	if (cur_scene == SCENE_TRAIN_MAP || cur_scene == SCENE_COMBAT_MAP_A || cur_scene == SCENE_COMBAT_MAP_B ||
		(cur_scene >= SCENE_COMBAT_MAP_C && cur_scene <= SCENE_COMBAT_MAP_E) ||
		cur_scene == SCENE_BRIEF_MAP) {
		map_Push_Map_Task(sHead_gbl);
		return 1;
	}
	if (cur_scene == SCENE_FLIGHT_TRAIN || cur_scene == SCENE_FLIGHT_COMBAT) {
		flight_scene_push(cur_scene, TIE_FLIGHT_KIND_TRAIN_COMBAT);
		return 1;
	}
	if (cur_scene == SCENE_FLIGHT_BATTLE) {
		flight_scene_push(cur_scene, TIE_FLIGHT_KIND_BATTLE);
		return 1;
	}
	if (cur_scene == SCENE_FILM_REPLAY) {
		flight_scene_push(cur_scene, TIE_FLIGHT_KIND_FILM_REPLAY);
		return 1;
	}
	if (is_play1_nodiff_scene(cur_scene)) {
		lcanvas_Disable_Screen_Diff();
		t->diff_disabled = true;
		play1_Push_Play1_Task(sHead_gbl);
		return 1;
	}
	if (is_play1_scene(cur_scene)) {
		play1_Push_Play1_Task(sHead_gbl);
		return 1;
	}
	return 0;
}

// FUNCTION: TIE 0x672D5, TIE98 0x47EF60 (dispatcher task split)
static LandruTaskStepResult shell_task_step(void* self) {
	ShellTask* t = (ShellTask*)self;

	if (t->phase == SHELL_PHASE_AWAITING) {
		/* Converted scene task popped; read the exit code, drain
		 * the text/sound scenes, then optionally push a sudden-end
		 * fade and yield. The Finalize_Close + Convert_Transition
		 * step happens in AFTER_SUDDEN_FADE — that phase runs even
		 * when no fade was pushed (single-step fall-through). */
		t->next_scene = lerror_Get_Landru_Exit();
		if (t->owns_tie98_music) {
			FrontendWaveStream_Shutdown();
			t->owns_tie98_music = false;
		}
		if (t->diff_disabled) {
			lcanvas_Enable_Screen_Diff();
			t->diff_disabled = false;
		}
		int16_t sudden_end = 0;
		shellext_Begin_Close_Landru_Scene(t->cur_scene, &sudden_end);
		if (sudden_end)
			shellext_Push_Sudden_Scene_Fade_Task();
		t->phase = SHELL_PHASE_AFTER_SUDDEN_FADE;
		return LANDRU_TASK_STEP_CONTINUE;
	}

	if (t->phase == SHELL_PHASE_AFTER_SUDDEN_FADE) {
		shellext_Finalize_Close_Landru_Scene();
		t->cur_scene = shellext_Convert_Transition(t->next_scene, 1);
		t->phase = SHELL_PHASE_DISPATCH;
		return LANDRU_TASK_STEP_CONTINUE;
	}

	/* DISPATCH phase. */
	if (t->exit_flag || lerror_Is_Landru_Error())
		return LANDRU_TASK_STEP_DONE;

	/* Reset HD-overlay scene tags to their defaults right before
	 * the new scene's Push runs. The dispatch function called below
	 * will Push the scene task, and bundled scenes (PLAY1 cutscenes,
	 * register, …) override these defaults from inside their Push.
	 * Doing this AFTER the previous scene's Begin_Close — not inside
	 * it — keeps the previous bundle tagged through XFADE's classic-
	 * FB transition fade, so the HD overlay covers the fading classic
	 * until the new scene takes over. Without this, a few frames of
	 * fading classic of the old scene leak through. */
	TieSnapshotBuilder_SetSceneKind(TIE_SCENE_FRONTEND);
	TieSnapshotBuilder_SetActiveFilm(NULL, NULL);
	TieSnapshotBuilder_SetRedrawModel(TIE_REDRAW_INCREMENTAL);
	if (TieProfile_FrontendId() == TIE_FRONTEND_PROFILE_TIE98 &&
		!lsurface_Select_Surface_Set(LANDRU_SURFACE_VGA)) {
		TieDiagnostics_Log(TIE_LOG_ERROR, "[SHELL] could not select VGA Landru surface for scene %d\n",
						   t->cur_scene);
		lerror_Set_Landru_Error(7);
		return LANDRU_TASK_STEP_DONE;
	}
	if (TieProfile_FrontendId() == TIE_FRONTEND_PROFILE_TIE98)
		lview_Init_View(lview_Get_Current_View());
	/* MODERN TASK ADAPTATION: original SHELL_Shell clears and presents
	 * immediately before entering these display-owning scene handlers. */
	if (TieProfile_FrontendId() == TIE_FRONTEND_PROFILE_TIE98 &&
		(t->cur_scene == SCENE_FLIGHT_COMBAT || t->cur_scene == SCENE_FLIGHT_BATTLE ||
		 t->cur_scene == SCENE_TRAIN_A || t->cur_scene == SCENE_TRAIN_MAP || t->cur_scene == SCENE_COMBAT_A ||
		 t->cur_scene == SCENE_COMBAT_MAP_A || t->cur_scene == SCENE_COMBAT_MAP_B ||
		 t->cur_scene == SCENE_FILM_VIEWER || t->cur_scene == SCENE_BRIEF_MAP ||
		 t->cur_scene == SCENE_FILM_REPLAY)) {
		FrontendDisplay_ClearPresentationSurfaces();
		FrontendDisplay_PresentFrame();
	}

	t->owns_tie98_music = false;
	if (TieMusicPolicy_UsesTie98()) {
		const char* music_path = shell_tie98_music_path(t->cur_scene);
		if (music_path) {
			FrontendWaveStream_PlayWaveFile(music_path, 1);
			t->owns_tie98_music = true;
		}
	}
	if (shell_dispatch_converted(t->cur_scene, t)) {
		/* Converted scene: open the Landru scene, push the scene
		 * task, transition to AWAITING. The runner steps the
		 * scene task on subsequent ticks; we resume in AWAITING
		 * once it pops. */
		TieDiagnostics_Log(TIE_LOG_INFO, "[SHELL] scene %d\n", t->cur_scene);
		shellext_Open_Landru_Scene(t->cur_scene);
		/* shell_dispatch_converted already pushed the scene task
		 * above us in the stack — its constructor was called
		 * before we get here. */
		t->phase = SHELL_PHASE_AWAITING;
		return LANDRU_TASK_STEP_CONTINUE;
	}

	/* Unknown scene: matches retail LABEL_205 — drop out of the
	 * dispatcher loop. ShellTask pops; xmain's host loop ends. */
	t->exit_flag = 1;
	return LANDRU_TASK_STEP_DONE;
}

static void shell_task_end(void* self) {
	(void)self;
	FrontendWaveStream_Shutdown();
	shipext_Validate_Tour_Battle();
	shipext_Delete_Temp_Pilot();
	lstream_Exit_Stream_Engine();
	shellext_Close_Landru(0);
	gamesnd_Close_Pre_iMuse();
	if (TieProfile_UsesDx5())
		g_frontendDisplayWndProcMode = -1;
}

static const LandruTaskVtable shell_task_vt = {
	.step = shell_task_step,
	.end = shell_task_end,
};

void shell_session_begin(int16_t scene, int16_t script) {
	ShellTask* t = (ShellTask*)landru_task_push(&shell_task_vt);
	if (!t)
		return;

	sHead_gbl = &t->the_head;
	t->the_head.last_scene = 1;
	t->the_head.cur_scene = scene;
	t->cur_scene = scene;
	t->exit_flag = 0;
	t->diff_disabled = false;
	t->owns_tie98_music = false;
	t->phase = SHELL_PHASE_DISPATCH;
	if (TieProfile_UsesDx5())
		g_frontendDisplayWndProcMode = TieProfile_FrontendId() == TIE_FRONTEND_PROFILE_TIE98 ? 1 : -1;

	frontResolution = (int16_t)TieProfile_Frontend()->vesa_mode;

	lerror_Clear_Landru_Error();
	gamesnd_Open_Pre_iMuse();
	gamesnd_game_Set_Front_Sound();

	shellext_Open_Landru(NULL, 0, script);

	imuse_pause(im);
	lstream_Set_Stream_Tick_Counts();
	imuse_resume(im);

	lstream_Init_Stream_Engine(SHELL_STREAM_BUFFER_BYTES, SHELL_STREAM_PREFETCH_BYTES);

	soundext_Prep_Sound_Scene(scene);
}

// FUNCTION: TIE 0x67EFA
void shell_programexit(const char* str) {
	shellext_Close_Landru(0);
	gamesnd_Close_Pre_iMuse();
	TieDiagnostics_Log(TIE_LOG_ERROR, "%s", str);
	TieDiagnostics_Fatal(str);
	exit(EXIT_FAILURE);
}
