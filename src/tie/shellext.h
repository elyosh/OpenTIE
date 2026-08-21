#ifndef __SHELLEXT_H__
#define __SHELLEXT_H__

#include <stdint.h>

#include "tie_runtime/audio/config.h"
#include "tie_runtime/diagnostics/diagnostics.h"
#include "tie_runtime/display/classic_display.h"
#include "tie_runtime/display/classic_framebuffer.h"
#include "tie_runtime/flight_assets/model_types.h"
#include "tie_runtime/input/input.h"
#include "tie_runtime/runtime/exports.h"
#include "tie_runtime/runtime/profile.h"
#include "tie_runtime/storage/storage.h"

/* Scene IDs — used by Shell dispatch, Set_Landru_Exit, and scene transitions.
 * Numbering follows LucasArts structure: single-digit = core, 10-70 = intro
 * cutscenes, 80+ = front-end screens, 100+ = menu/training/combat,
 * 120+ with transitions, 170+ = battle flow, 200+ = battle cutscenes. */
typedef enum {
	/* Flight missions */
	SCENE_FLIGHT_TRAIN = 2,  /* training flight mission */
	SCENE_FLIGHT_COMBAT = 3, /* combat sim flight mission */
	SCENE_FLIGHT_BATTLE = 4, /* battle flight mission (cutscene chain follows) */

	/* Intro / opening cutscenes (PLAY1, no screen diff) */
	SCENE_CUT_INTRO_1 = 10,
	SCENE_CUT_INTRO_2 = 20,
	SCENE_CUT_INTRO_3A = 30,
	SCENE_CUT_INTRO_3B = 31,
	SCENE_CUT_INTRO_3C = 32,
	SCENE_CUT_INTRO_4 = 40,

	/* Cutscenes (PLAY1, various) */
	SCENE_CUT_PROLOGUE_1 = 6,
	SCENE_CUT_PROLOGUE_2 = 7,
	SCENE_CUT_25 = 25,
	SCENE_CUT_50 = 50,
	SCENE_CUT_60 = 60,
	SCENE_CUT_61 = 61, /* no screen diff */
	SCENE_CUT_70 = 70, /* no screen diff */
	SCENE_CUT_71 = 71, /* no screen diff */
	SCENE_CUT_72 = 72, /* no screen diff */

	/* Logo / Credits */
	SCENE_TIELOGO = 80,     /* TIE Fighter logo animation */
	SCENE_CREDITS = 90,     /* credits screen */
	SCENE_CREDITS_ALT = 91, /* credits (alternate entry) */

	/* Title / Register / Exit */
	SCENE_TITLE = 8,      /* title screen */
	SCENE_REGISTER = 100, /* pilot registration */
	SCENE_EXIT = 101,     /* exit game (via register or main menu) */

	/* Main Menu */
	SCENE_MAIN_MENU = 110, /* main menu */

	/* Training */
	SCENE_TRAIN_TRANSITION = 120, /* training cutscene transition (PLAY1) */
	SCENE_TRAIN_A = 121,          /* training screen (entry A) */
	SCENE_TRAIN_B = 122,          /* training screen (entry B) */
	SCENE_TRAIN_MAP = 123,        /* training MAP display */

	/* Combat Simulation */
	SCENE_COMBAT_TRANSITION = 130, /* combat sim cutscene transition (PLAY1) */
	SCENE_COMBAT_A = 131,          /* combat sim screen (entry A) */
	SCENE_COMBAT_B = 132,          /* combat sim screen (entry B) */
	SCENE_COMBAT_MAP_A = 133,      /* combat sim MAP (entry A) */
	SCENE_COMBAT_MAP_B = 134,      /* combat sim MAP (entry B) */
	SCENE_COMBAT_MAP_C = 135,      /* combat sim MAP (entry C) */
	SCENE_COMBAT_MAP_D = 136,      /* combat sim MAP (entry D) */
	SCENE_COMBAT_MAP_E = 137,      /* combat sim MAP (entry E) */

	/* Front-end screens */
	SCENE_FILM_VIEWER = 140,   /* film room viewer */
	SCENE_BLUEPRINT = 150,     /* ship blueprint viewer */
	SCENE_TOUR_DESK = 160,     /* tour desk / galaxy map / battle selection */
	SCENE_TOUR_CUTSCENE = 170, /* tour selection cutscene (PLAY1) */

	/* Briefing flow */
	SCENE_BRIEF_PRE = 179,          /* pre-briefing (door entry) */
	SCENE_BRIEF = 180,              /* briefing hub */
	SCENE_BRIEF_MAP = 181,          /* briefing MAP display */
	SCENE_TALK_BRIEF_OFFICER = 182, /* talk to officer (briefing) */
	SCENE_TALK_BRIEF_PRIEST = 183,  /* talk to priest (briefing) */

	/* Debrief flow */
	SCENE_DEBRIEF = 190,              /* debrief hub */
	SCENE_TALK_DEBRIEF_OFFICER = 191, /* talk to officer (debrief) */
	SCENE_TALK_DEBRIEF_PRIEST = 192,  /* talk to priest (debrief) */

	/* Battle cutscenes (PLAY1) */
	SCENE_CUT_BATTLE_210 = 210,
	SCENE_CUT_BATTLE_231 = 231,
	SCENE_CUT_BATTLE_240 = 240,
	SCENE_CUT_BATTLE_250 = 250, /* 250-263: per-battle cinematics */
	SCENE_CUT_BATTLE_270 = 270, /* battle start transition (also PLAY1) */

	/* Post-battle */
	SCENE_ARM_SHIP = 275, /* weapon loadout screen */
	SCENE_CUT_280 = 280,  /* 280-285: post-battle cinematics */

	/* Film Room playback */
	SCENE_FILM_REPLAY = 290, /* selected .CLP replay */

	/* Expansion cutscenes (PLAY1) */
	SCENE_CUT_390 = 390,
	SCENE_CUT_400 = 400, /* 400-411: expansion pack cutscenes */
	SCENE_CUT_420 = 420,
	SCENE_CUT_500 = 500, /* 500-591: expansion pack cutscenes */

	/* FMV cutscenes (PLAY1, no screen diff) */
	SCENE_FMV_600 = 600, /* 600-622: FMV sequences */

	/* Late-game cutscenes (PLAY1) */
	SCENE_CUT_700 = 700,
	SCENE_CUT_710 = 710,
	SCENE_CUT_720 = 720,
	SCENE_CUT_730 = 730,
	SCENE_CUT_900 = 900,
} TIEScene;
#include <stdbool.h>

#include "landru/actor.h"
#include "landru/file.h"
#include "landru/pal.h"
#include "landru/res.h"

typedef struct {
	int16_t music_active;
	int16_t sound_active;
	int16_t speech_active;
	int16_t music_volume;
	int16_t sound_volume;
	int16_t speech_volume;
	int16_t text_active;
	int16_t transition_active;
	int16_t game_level;
	int16_t auto_backup;
	int16_t auto_restore;
} FrontOptionsStruct;

typedef struct {
	ResFile* def_file;
	Palette* def_palette;
	Actor* def_icons;
	Actor* def_cursors;
	int16_t def_font6;
	int16_t def_font8;
	int16_t cur_scene;
	int16_t last_scene;
	int16_t sudden_end;
} SceneHeadStruct;

extern FrontOptionsStruct options_gbl;
extern int32_t f_res;

void shellext_Open_Landru(void* extern_mem, int16_t use_timer, int16_t use_script);
void shellext_Close_Landru(int16_t use_timer);
void shellext_Open_Landru_Scene(int16_t scene);
/* Two-step scene close: shell task calls Begin to drain text/sound
 * scenes and read out the sudden_end flag, then optionally pushes
 * shellext_Push_Sudden_Scene_Fade_Task and yields, and finally calls
 * Finalize on its post-fade phase to land the screen-diff copy. */
void shellext_Begin_Close_Landru_Scene(int16_t scene, int16_t* out_sudden_end);
void shellext_Finalize_Close_Landru_Scene(void);
ResFile* shellext_Open_Empire_Resource(const char* filename);
LandruFile* shellext_Open_Empire_File(const char* filename, const char* mode);
int16_t shellext_Check_Cur_Scene(int16_t current_scene);
int16_t shellext_Get_Cur_Scene(void);
int16_t shellext_Check_Last_Scene(int16_t last_scene);
int16_t shellext_Get_Last_Scene(void);
int16_t shellext_Is_Scene_Exit(int16_t scene_flag);
int16_t shellext_Check_Scene_Exit(int16_t* exit_id, int16_t next_scene, int16_t next_section,
								  int16_t scene_flag);
int16_t shellext_Sudden_Scene_End(void);
int16_t shellext_Is_Sudden_Scene_End(void);

/* Push fade tasks; the caller yields until the task's end callback completes
 * cursor and view-refresh housekeeping. */
void shellext_Push_Sudden_Scene_Fade_Task(void);
void shellext_Push_Back_Stage_To_VGA_Task(int16_t dialog);
int16_t shellext_escape_TIE(void);
void shellext_Load_Preferences(void);
int16_t shellext_Set_Prefs_Sound(void);
int16_t shellext_Convert_Transition(int16_t scene, int16_t sudden);

#endif
