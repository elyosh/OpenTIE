/*
 * TRAIN.C — Training room screen
 *
 * Lets the player select a training ship and difficulty level, view
 * mission descriptions and high scores on the monitor, watch a 3D
 * flyby of the training course, and enter training via the helmet
 * visor animation.
 *
 * Layout: 6 nav buttons (prev/next ship, prev/next level, start, exit),
 * a monitor screen with 3 display modes (mission text, high scores,
 * flyby course info), and actor-driven animations (flickering lights,
 * opening clam shell, helmet visor).
 *
 * Film actors by var1: 1=help text, 5=button[var2], 10=clam, 12=helmet,
 * 15=arrow, 16=light, 20=decorative.
 */

#include "tie/train.h"
#ifdef TIE_MODERN
#include "tie_runtime/runtime/train_task.h"
#endif
#include "landru/actcust.h"
#include "landru/actdelt.h"
#include "landru/actor.h"
#include "landru/cursor.h"
#include "landru/dirty.h"
#include "landru/error.h"
#include "landru/file.h"
#include "landru/film.h"
#include "landru/font.h"
#include "landru/inpattr.h"
#include "landru/inpcall.h"
#include "landru/input.h"
#include "landru/io.h"
#include "landru/paint.h"
#include "landru/rect.h"
#include "landru/res.h"
#include "landru/surface.h"
#include "landru/vesa.h"
#include "landru/view.h"
#include "landru/viewadd.h"
#include "tie/edition.h"
#include "tie/map.h"
#include "tie/shellext.h"
#include "tie/shipext.h"
#include "tie/soundext.h"
#include "tie/textext.h"
#include "tie_runtime/presentation/pilot_name.h"
#include "tie_runtime/runtime/profile.h"
#include "tie_runtime/storage/score_tables.h"

#include "tie/bpflight.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The original TU calls the library strcat routine rather than the inline form. */
#ifdef __WATCOMC__
#pragma function(strcat)
#endif

/* The original TU calls the library abs() rather than the inline form. */
#ifdef __WATCOMC__
#pragma function(abs)
#endif

/* Resource names: [0] = LFD file, [1] = first-visit film, [2] = return film. */
// GLOBAL: TIE95 0xCE58E
// GLOBAL: TIE98 0x4F3110
static const char train_str[3][20] = {
#ifdef TIE98
	"train640.lfd", "train640",
#else
	"train.lfd", "train",
#endif
	"trainbrf"
};

/* The DOS original keeps only the eight displayed entries with
 * ten-character names. */
enum {
#if defined(TIE_MODERN) || defined(TIE98)
	NUM_SCORE_ENTRIES = TRAIN_SCORE_ENTRY_COUNT,
	SCORE_NAME_LEN = TRAIN_SCORE_NAME_CAPACITY,
#else
	NUM_SCORE_ENTRIES = 8,
	SCORE_NAME_LEN = 10,
#endif
	VGA_SCORE_ENTRIES = 8,
};

/* The first eight defaults are shared by both originals; TIE98 adds two
 * empty slots and displays all ten entries. */
// GLOBAL: TIE95 0xCE5CA
// GLOBAL: TIE98 0x4F2F18
static char train_score_name[NUM_SCORE_ENTRIES][SCORE_NAME_LEN] = {
	"Luke", "Jon", "Larry", "Peter", "Bucky", "Jim", "Edward", "Wade",
};
// GLOBAL: TIE95 0xCE61C
// GLOBAL: TIE98 0x4F3068
static int32_t train_score_points[NUM_SCORE_ENTRIES] = {
	100, 100, 100, 100, 100, 100, 100, 100,
};
// GLOBAL: TIE95 0xCE63C
// GLOBAL: TIE98 0x4F3090
static int16_t train_score_level[NUM_SCORE_ENTRIES] = {
	1, 1, 1, 1, 1, 1, 1, 1,
};

/* Flyby course info: flat table of 5-word entries {start_time, end_time,
 * y, x, text_id}, terminated by start_time == -1. */
// GLOBAL: TIE95 0xCE64C
// GLOBAL: TIE98 0x4F30A8
static const int16_t train_course_info[52] = {
	20,  84,  60, 90, txtTrainCourse,   25,  84,  60, 100, txtTrainSegment,
	115, 150, 53, 70, txtTrainPyramid,  120, 150, 50, 80,  txtTrainBonus,
	175, 230, 30, 40, txtTrainObstacle, 180, 230, 15, 50,  txtTrainDestroy,
	235, 290, 10, 90, txtTrainSphere,   240, 290, 5,  100, txtTrainBonus,
	310, 375, 60, 40, txtTrainAdvance,  315, 375, 48, 50,  txtTrainAdvance2,
	-1,  0,
};

/* Module state */
// GLOBAL: TIE95 0xF5794
// GLOBAL: TIE98 0x58AAF4
static ResFile* train_file;
// GLOBAL: TIE95 0xF5798
// GLOBAL: TIE98 0x58AAF0
static Film* train_film;
// GLOBAL: TIE95 0xF579C
// GLOBAL: TIE98 0x58AB00
static Input* world_input;
// GLOBAL: TIE95 0xF5768
// GLOBAL: TIE98 0x58AB10
static Input* button_input[6];
// GLOBAL: TIE95 0xF57A4
// GLOBAL: TIE98 0x58AAE8
static Input* monitor_input;
// GLOBAL: TIE95 0xF5790
static Actor* arrow_actor;
// GLOBAL: TIE95 0xF5780
static Actor* button[6]; /* TIE98 stores one actor for each input. */
// GLOBAL: TIE95 0xF57A0
static Actor* helmet;
// GLOBAL: TIE95 0xF5788
// GLOBAL: TIE98 0x58AB04
static int32_t train_time;
// GLOBAL: TIE95 0xF578C
// GLOBAL: TIE98 0x58AB28
static int32_t train_mode;
// GLOBAL: TIE95 0xF57A8
// GLOBAL: TIE98 0x58AAF8
static int16_t train_help;
// GLOBAL: TIE98 0x58AAEC
static int32_t train_monitor_needs_clear;

/* train_pilot_medal_status: snapshot of train_max_level[ship] BEFORE the
 * training round so map.c (TRAIN_MAP scene) can detect a fresh max-level
 * achievement via "(old < 4) && (new >= 4)". The storage lives in map.c
 * (declared extern in map.h); both writers must reach the same global. */

/* Forward declarations (referenced by film callback before definition) */
static int16_t train_draw_Train_Help(Actor* the_actor, Rect* draw_rect, Rect* clip_rect, int16_t off_x,
									 int16_t off_y, int16_t refresh);
static void train_user_Train_Clam(Actor* the_actor, int32_t time);
static void train_user_Train_Light(Actor* the_actor, int32_t time);
static void train_user_Train_Helmet(Actor* the_actor, int32_t time);

static void train_end_Train_View(int32_t time);
static int16_t train_film_Train_Callback(Film* the_film, FilmObject* film_object);
static int16_t train_iupdate_Train(Input* input, Rect* draw_rect, Rect* clip_rect, int16_t active,
								   uint8_t mouseState, uint8_t prevMouseState, int16_t key, int16_t prevKey);
static void train_iuser_Train(Input* input, int32_t time);
static int16_t train_iupdate_Train_Screen(Input* input, Rect* draw_rect, Rect* clip_rect, int16_t active,
										  uint8_t mouseState, uint8_t prevMouseState, int16_t key,
										  int16_t prevKey);
static int16_t train_iuser_Train_Screen(Input* input, int32_t time);
static void train_idraw_Train_Screen(Input* input, Rect* draw_rect, Rect* clip_rect, int16_t refresh);
static int16_t train_Draw_Train_Screen_Mission(Rect* src);
static int16_t train_Draw_Train_Screen_Score(Rect* src);
static int16_t train_Draw_Train_Screen_Flyby(Rect* src);

/* ------------------------------------------------------------------ */

// FUNCTION: TIE95 0x6B4E8
// FUNCTION: TIE98 0x491B30
int16_t train_Train(SceneHeadStruct* the_head) {
	Rect frame;
	const char* film_name;
	TrainingScoreEntry loaded_scores[TRAIN_SCORE_ENTRY_COUNT];
	int16_t i;

	xio_Set_Mouse_Position(TIE_FRONTEND_EDITION(220, 235), TIE_FRONTEND_EDITION(190, 465));

#ifdef TIE_MODERN
	train_file = shellext_Open_Empire_Resource(TIE_FRONTEND_EDITION(train_str[0], "train640.lfd"));
#else
	train_file = shellext_Open_Empire_Resource(train_str[0]);
#endif
	xviewadd_Clear_View();
	xview_Disable_All_View_Erase();

	/* Select film based on scene: entry A = first visit, B = return */
	xrect_Set_Rect(&frame, 0, 0, TIE_FRONTEND_EDITION(320, 640), TIE_FRONTEND_EDITION(200, 480));
#ifdef TIE_MODERN
	film_name = (shellext_Get_Cur_Scene() == SCENE_TRAIN_A) ? TIE_FRONTEND_EDITION(train_str[1], "train640")
															: train_str[2];
#else
	film_name = (shellext_Get_Cur_Scene() == SCENE_TRAIN_A) ? train_str[1] : train_str[2];
#endif
	train_film = xfilm_Res_Callback_Film(film_name, &frame, 0, 0, 0, train_film_Train_Callback);
	xfilm_Set_Film_Def_Palette(train_film, the_head->def_palette);

	/* World input (full screen) */
	xrect_Set_Rect(&frame, 0, 0, TIE_FRONTEND_EDITION(320, 640), TIE_FRONTEND_EDITION(200, 480));
	world_input = xinput_Alloc_Input(NULL, &frame, 0, 0);

	/* Monitor screen input */
	xrect_Set_Rect(&frame, TIE_FRONTEND_EDITION(62, 144), TIE_FRONTEND_EDITION(6, 56),
				   TIE_FRONTEND_EDITION(254, 500), TIE_FRONTEND_EDITION(116, 300));
	monitor_input = xinput_Alloc_Input(world_input, &frame, 0, 0);
	xinpattr_Set_Input_Update_Function(monitor_input, train_iupdate_Train_Screen);
	xinpattr_Set_Input_User_Function(monitor_input, (InputUserFunc)train_iuser_Train_Screen);
	xinpattr_Set_Input_Draw_Function(monitor_input, train_idraw_Train_Screen);
	xinpattr_Refreshable_Input(monitor_input);
	monitor_input->id = 0;

	/* 6 navigation buttons */
	for (i = 0; i < 6; i++) {
		switch (i) {
			case 0:
				xrect_Set_Rect(&frame, TIE_FRONTEND_EDITION(188, 377), TIE_FRONTEND_EDITION(138, 336),
							   TIE_FRONTEND_EDITION(214, 398), TIE_FRONTEND_EDITION(154, 346));
				break;
			case 1:
				xrect_Set_Rect(&frame, TIE_FRONTEND_EDITION(214, 398), TIE_FRONTEND_EDITION(138, 336),
							   TIE_FRONTEND_EDITION(240, 422), TIE_FRONTEND_EDITION(154, 346));
				break;
			case 2:
				xrect_Set_Rect(&frame, TIE_FRONTEND_EDITION(188, 386), TIE_FRONTEND_EDITION(154, 454),
							   TIE_FRONTEND_EDITION(214, 416), TIE_FRONTEND_EDITION(170, 468));
				break;
			case 3:
				xrect_Set_Rect(&frame, TIE_FRONTEND_EDITION(214, 416), TIE_FRONTEND_EDITION(154, 454),
							   TIE_FRONTEND_EDITION(240, 446), TIE_FRONTEND_EDITION(170, 468));
				break;
			case 4:
				xrect_Set_Rect(&frame, TIE_FRONTEND_EDITION(206, 212), TIE_FRONTEND_EDITION(184, 455),
							   TIE_FRONTEND_EDITION(237, 252), TIE_FRONTEND_EDITION(200, 474));
				break;
			case 5:
				xrect_Set_Rect(&frame, TIE_FRONTEND_EDITION(49, 453), TIE_FRONTEND_EDITION(181, 325),
							   TIE_FRONTEND_EDITION(79, 522), TIE_FRONTEND_EDITION(200, 400));
				break;
		}
		button_input[i] = xinput_Alloc_Input(world_input, &frame, 0, 0);
		xinpattr_Set_Input_Update_Function(button_input[i], train_iupdate_Train);
		xinpattr_Set_Input_User_Function(button_input[i], train_iuser_Train);
		button_input[i]->mouseUsage = 4;
		button_input[i]->id = i + 1;
	}

	train_time = 0;
	train_help = 0;
#ifdef TIE_MODERN
	train_monitor_needs_clear = TIE_FRONTEND_TIE98;
#elif defined(TIE98)
	train_monitor_needs_clear = true;
#endif

	bpflight_Open_Flight_Engine(1);
	bpflight_Stop_Movie_Engine();

	/* Load either legacy TIE95 scores or the shared canonical format. */
	if (TieScoreTables_LoadTraining("train.hgh", loaded_scores)) {
		for (i = 0; i < NUM_SCORE_ENTRIES; i++) {
			snprintf(train_score_name[i], sizeof(train_score_name[i]), "%s", loaded_scores[i].name);
			train_score_points[i] = loaded_scores[i].score;
			train_score_level[i] = loaded_scores[i].level;
		}
	}
	xview_Set_View_Update_Function(train_end_Train_View);
#ifdef TIE_MODERN
	TieTrain_RunView(train_file, TIE_FRONTEND_TIE98);
	return 0;
#else
	shellext_Handle_TIE_View();
	xinpcall_Clear_Active_Input();
	xview_Clear_View_Update_Function();
	bpflight_Close_Flight_Engine();
	xview_Enable_All_View_Erase();

	if (xcursor_Is_Cursor_Visible())
		xcursor_Hide_Cursor();

	xres_Close_Resource(train_file);
	return xerror_Get_Landru_Exit();
#endif
}

/* ------------------------------------------------------------------ */

// FUNCTION: TIE95 0x6B7EC
// FUNCTION: TIE98 0x491EC0
static void train_end_Train_View(int32_t time) {
	if (time == 0 && !xcursor_Is_Cursor_Visible())
		xcursor_Show_Cursor();
}

/* ------------------------------------------------------------------ */

// FUNCTION: TIE95 0x6B800
// FUNCTION: TIE98 0x491EE0
static int16_t train_film_Train_Callback(Film* the_film, FilmObject* film_object) {
	Actor* the_actor;
	int16_t var1;
	if (TIE_FRONTEND_TIE98 && film_object->id == FTC_PALETTE) {
		xfilm_Rewind_Palette_Film(the_film, film_object, (void*)(film_object + 1));
		return 0;
	}

	if (film_object->id != 3)
		return 0;

	xfilm_Rewind_Actor_Film(the_film, film_object, (void*)(film_object + 1));
	the_actor = (Actor*)film_object->object;
	var1 = the_actor->var1;

	switch (var1) {
		case 1:
			xactor_Set_Actor_Draw_Function(the_actor, train_draw_Train_Help);
			break;
		case 5:
			button[the_actor->var2] = the_actor;
			break;
		case 10:
			xactor_Set_Actor_User_Function(the_actor, train_user_Train_Clam);
			return 0;
		case 12:
			xactor_Set_Actor_User_Function(the_actor, train_user_Train_Helmet);
			helmet = the_actor;
			return 0;
		case 15:
			arrow_actor = the_actor;
			return 0;
		case 16:
			xactor_Set_Actor_User_Function(the_actor, train_user_Train_Light);
			return 0;
		case 20:
			xactor_Non_Refreshable_Actor(the_actor);
			break;
	}

	return 0;
}

/* ------------------------------------------------------------------ */

// FUNCTION: TIE95 0x6B8DC
// FUNCTION: TIE98 0x491FF0
static int16_t train_iupdate_Train(Input* input, Rect* draw_rect, Rect* clip_rect, int16_t active,
								   uint8_t mouseState, uint8_t prevMouseState, int16_t key, int16_t prevKey) {
	int16_t id;
	(void)draw_rect;
	(void)clip_rect;
	(void)key;
	(void)prevKey;

	if (active)
		return 0;

	train_help = input->id;

	if (!mouseState && !prevMouseState)
		return 1;

	id = input->id;

#if defined(TIE_MODERN) || defined(TIE98)
#ifdef TIE_MODERN
	if (TIE_FRONTEND_TIE98) {
#else
	{
#endif
		Actor* input_actor;

		if (id == 5)
			input_actor = button[0];
		else if (id == 6)
			input_actor = button[1];
		else
			input_actor = button[id + 1];

		if (mouseState == 3 || prevMouseState == 3) {
			xactor_Set_Actor_State(input_actor, 0, 0);
			xinpattr_Selected_Input(input);
		} else {
			if (mouseState == 1 || prevMouseState == 1)
				soundext_Play_SFX(sfxButton, id <= 4 ? 95 : 80);
			xactor_Set_Actor_State(input_actor, 1, 0);
		}
		return 1;
	}
#endif

#ifndef TIE98
	if (id == 5) {
		/* Start training button */
		if (mouseState == 3 || prevMouseState == 3) {
			xactor_Set_Actor_State(button[0], 2, 0);
			xinpattr_Selected_Input(input);
		} else {
			if (mouseState == 1 || prevMouseState == 1)
				soundext_Play_SFX(sfxButton, 80);
			xactor_Set_Actor_State(button[0], 3, 0);
		}
	} else if (id == 6) {
		/* Exit door button */
		if (mouseState == 3 || prevMouseState == 3) {
			xactor_Set_Actor_State(button[1], 0, 0);
			xinpattr_Selected_Input(input);
		} else {
			if (mouseState == 1 || prevMouseState == 1)
				soundext_Play_SFX(sfxButton, 80);
			xactor_Set_Actor_State(button[1], 1, 0);
		}
	} else {
		/* Nav buttons 1-4 */
		if (mouseState == 3 || prevMouseState == 3) {
			xinpattr_Clear_Input_Flag1(input);
			xinpattr_Selected_Input(input);
			xactor_Hide_Actor(arrow_actor);
		}
		if (mouseState == 1 || prevMouseState == 1) {
			xinpattr_Set_Input_Flag1(input);
			xactor_Show_Actor(arrow_actor);
			xactor_Set_Actor_State(arrow_actor, 2 * (input->id - 1), 0);
			soundext_Play_SFX(sfxButton, 95);
		}
	}
	return 1;
#endif
}

/* ------------------------------------------------------------------ */

// FUNCTION: TIE95 0x6BA58
// FUNCTION: TIE98 0x492150
static void train_iuser_Train(Input* input, int32_t time) {
	(void)time;

	if (!xinpattr_Get_Input_Selected(input) || helmet->var2)
		return;

	switch (input->id) {
		case 1:
			shipext_Last_Train_Ship();
			train_mode = 0;
			train_time = 80;
			bpflight_Stop_Movie_Engine();
			break;
		case 2:
			shipext_Next_Train_Ship();
			train_mode = 0;
			train_time = 80;
			bpflight_Stop_Movie_Engine();
			break;
		case 3:
			shipext_Last_Train_Level();
			train_mode = 0;
			train_time = 80;
			bpflight_Stop_Movie_Engine();
			break;
		case 4:
			shipext_Next_Train_Level();
			train_mode = 0;
			train_time = 80;
			bpflight_Stop_Movie_Engine();
			break;
		case 5:
			helmet->var2 = 1;
			break;
		case 6:
			xerror_Set_Landru_Exit(SCENE_MAIN_MENU);
			break;
	}

	if (TIE_FRONTEND_TIE98 && input->id <= 4)
		train_monitor_needs_clear = 1;
}

/* ------------------------------------------------------------------ */

// FUNCTION: TIE95 0x6BB60
static void train_idraw_Train(Input* input, Rect* draw_rect, Rect* clip_rect, int16_t refresh) {
	int16_t color, id, i;
	char buf[48];

	if (!refresh)
		return;

	/* Fade color based on helmet state: dim when visor is down */
	if (helmet->state >= 5)
		color = 16;
	else
		color = 31 - 3 * helmet->state;

	if (color == 16)
		return;

	id = input->id;

	if (id == 6) {
		/* Ship name (truncated at parenthesis) */
		shipext_Get_Train_Ship_Name(buf);
		for (i = 0; buf[i]; i++) {
			if (buf[i] == '(') {
				if (i > 0 && buf[i - 1] == ' ')
					buf[i - 1] = '\0';
				else
					buf[i] = '\0';
				break;
			}
		}
		xfont_Print_Centered_Text(buf, draw_rect, 1, color);
	} else if (id == 7) {
		/* "Level N" */
		char fmt[32];
		textext_Copy_Text(fmt, txtTrainLevel);
		snprintf(buf, sizeof(buf), fmt, shipext_Get_Train_Level() + 1);
		xfont_Print_Centered_Text(buf, draw_rect, 1, color);
	}

	if (xinpattr_Is_Input_Dirty(input))
		xdirty_Dirty_Rect(clip_rect);
}

/* ------------------------------------------------------------------ */

/*
 * Help tooltip overlay. Draws the delta actor with centered text from
 * the TIEText table when a nav button is hovered and visor is up.
 */
// FUNCTION: TIE95 0x6BC68
// FUNCTION: TIE98 0x492200
static int16_t train_draw_Train_Help(Actor* the_actor, Rect* draw_rect, Rect* clip_rect, int16_t off_x,
									 int16_t off_y, int16_t refresh) {

	if (refresh) {
		if (train_help && !helmet->state) {
			Rect bounds;
			char text[32];
			xactdelt_Draw_Delta_Actor(the_actor, draw_rect, clip_rect, off_x, off_y, refresh);
			xactor_Get_Actor_Bounds(the_actor, &bounds);
			xfont_Enable_FontID_Shadow(0);
			/* train_help 1-6 maps to txtTrainLastShip(74)..txtTrainExit(79) */
			textext_Copy_Text(text, (int16_t)(train_help + 73));
			xfont_Print_Centered_Text(text, &bounds, TIE_FRONTEND_EDITION(0, 2), 15);
			xfont_Disable_FontID_Shadow(0);
		}
		train_help = 0;
	}
	return 1;
}

/* ------------------------------------------------------------------ */

/* Freeze clam actor on the final film frame */
// FUNCTION: TIE95 0x6BD00
// FUNCTION: TIE98 0x4922B0
static void train_user_Train_Clam(Actor* the_actor, int32_t time) {
	if (time == (int32_t)train_film->cels)
		xactor_Non_Refreshable_Actor(the_actor);
}

/* ------------------------------------------------------------------ */

/*
 * Flickering light animation. Uses var2 as a countdown timer.
 * Bit 14 of var2 distinguishes "on" vs "off" phase.
 * Each phase has random frame changes and a random-length hold.
 */
// FUNCTION: TIE95 0x6BD1C
// FUNCTION: TIE98 0x4922E0
static void train_user_Train_Light(Actor* the_actor, int32_t time) {

	if (time == 0) {
		xactor_Show_Actor(the_actor);
		the_actor->var2 = (rand() & 0xF) + 2;
	}

	if (the_actor->var2 & 0x4000) {
		int16_t countdown;
		/* "On" phase: random state every other frame */
		if (time & 1)
			the_actor->state = abs(rand()) % the_actor->arraySize;
		countdown = the_actor->var2 & 0x3FFF;
		if (countdown == 1) {
			the_actor->var2 = (rand() & 0xF) + 2;
			return;
		}
	} else {
		/* "Off" phase: random state, toggle phase when the countdown expires */
		the_actor->state = abs(rand()) % the_actor->arraySize;
		if (the_actor->var2 == 1) {
			the_actor->var2 = (rand() & 0xF) + 0x4002;
			return;
		}
	}
	the_actor->var2--;
}

/* ------------------------------------------------------------------ */

/*
 * Helmet visor animation. var2 == 1 triggers visor closing (entering
 * training). var2 == 0 with scene TRAIN_B auto-plays visor opening
 * (returning from training). Saves pilot and exits to flight scene.
 */
// FUNCTION: TIE95 0x6BDD0
// FUNCTION: TIE98 0x492390
static void train_user_Train_Helmet(Actor* the_actor, int32_t time) {

	if (the_actor->var2) {
		/* Entering training — close visor */
		if (!xactor_Is_Actor_Visible(the_actor)) {
			xactor_Show_Actor(the_actor);
			xactor_Set_Actor_State(the_actor, 0, 0);
			soundext_Play_SFX(sfxVisor, 80);
		} else {
			int16_t next_state = the_actor->state + 1;
			if (next_state == the_actor->arraySize) {
				uint8_t ship;
				/* Visor fully closed — enter training */
				shipext_Update_Pilot();
				xerror_Set_Landru_Exit(SCENE_FLIGHT_TRAIN);
				soundext_Stop_SFX(sfxVisor);
				soundext_Play_SFX(sfxVisorClick, 80);
				ship = shipext_Get_Train_Ship();
				train_pilot_medal_status = pilot_record.train_max_level[ship];
			} else {
				xactor_Set_Actor_State(the_actor, next_state, 0);
			}
		}
	} else {
		/* Idle / returning from training */
		int16_t cur_scene = shellext_Get_Cur_Scene();
		if (time < the_actor->arraySize && cur_scene == SCENE_TRAIN_B) {
			/* Auto-play visor opening (reverse) */
			if (!xactor_Is_Actor_Visible(the_actor)) {
				xactor_Show_Actor(the_actor);
				soundext_Play_SFX(sfxVisor, 80);
			}
			xactor_Set_Actor_State(the_actor, the_actor->arraySize - (time + 1), 0);
		} else {
			/* Idle: nothing to refresh if visor is already hidden. */
			if (!xactor_Is_Actor_Visible(the_actor))
				return;
			soundext_Stop_SFX(sfxVisor);
			soundext_Play_SFX(sfxVisorClick, 80);
			xactor_Hide_Actor(the_actor);
		}
		xview_Refresh_View();
	}
}

/* ------------------------------------------------------------------ */

// FUNCTION: TIE95 0x6BEF8
// FUNCTION: TIE98 0x4924C0
static int16_t train_iupdate_Train_Screen(Input* input, Rect* draw_rect, Rect* clip_rect, int16_t active,
										  uint8_t mouseState, uint8_t prevMouseState, int16_t key,
										  int16_t prevKey) {
	(void)draw_rect;
	(void)clip_rect;
	(void)key;
	(void)prevKey;

	if (active)
		return 0;
	if (mouseState == 3 || prevMouseState == 3)
		xinpattr_Selected_Input(input);
	return 1;
}

/* ------------------------------------------------------------------ */

/*
 * Monitor display mode state machine. Cycles through 3 modes on click:
 *   0-255: mission description (mode 0)
 *   256-383: high scores (mode 1)
 *   384-766: flyby with 3D movie (mode 2)
 *   767: reset to mode 0
 */
// FUNCTION: TIE95 0x6BF20
// FUNCTION: TIE98 0x492500
static int16_t train_iuser_Train_Screen(Input* input, int32_t time) {
	(void)time;

	if (xinpattr_Get_Input_Selected(input)) {
		if (train_time > 384)
			train_time = 767;
		else if (train_time > 256)
			train_time = 384;
		else if (train_time > 0)
			train_time = 256;
		else if (train_time > 0)
			train_time = 0;
	}

	switch (train_time) {
		case 0:
			train_mode = 0;
			train_time++;
			if (TIE_FRONTEND_TIE98)
				train_monitor_needs_clear = 1;
			break;
		case 256:
			train_time++;
			train_mode = 1;
			if (TIE_FRONTEND_TIE98)
				train_monitor_needs_clear = 1;
			break;
		case 384: {
			char movie_name[16];

			bpflight_Start_Movie_Engine();
			strcpy(movie_name, "trnfly1");
			bpflight_Open_New_Matrix(movie_name);
			train_mode = 2;
			train_time++;
			if (TIE_FRONTEND_TIE98)
				train_monitor_needs_clear = 1;
			break;
		}
		case 767:
			bpflight_Stop_Movie_Engine();
			train_time = 0;
			if (TIE_FRONTEND_TIE98)
				train_monitor_needs_clear = 1;
			break;
		default:
			train_time++;
			break;
	}
	return 1;
}

/* ------------------------------------------------------------------ */

// FUNCTION: TIE95 0x6C018
// FUNCTION: TIE98 0x492610
static void train_idraw_Train_Screen(Input* input, Rect* draw_rect, Rect* clip_rect, int16_t refresh) {
	if (!refresh)
		return;

#if defined(TIE_MODERN) || defined(TIE98)
#ifdef TIE_MODERN
	if (TIE_FRONTEND_TIE98 && train_monitor_needs_clear) {
#else
	if (train_monitor_needs_clear) {
#endif
		xpaint_Paint_Clipped_Rect(draw_rect, 0);
		train_monitor_needs_clear = 0;
	}
#endif

	if (helmet->state)
		return;

	switch (train_mode) {
		case 0:
			train_Draw_Train_Screen_Mission(draw_rect);
			break;
		case 1:
			train_Draw_Train_Screen_Score(draw_rect);
			break;
		case 2:
			train_Draw_Train_Screen_Flyby(draw_rect);
			break;
	}

	if (xinpattr_Is_Input_Dirty(input))
		xdirty_Dirty_Rect(clip_rect);
}

/* ------------------------------------------------------------------ */

/* Draw mission description on the training monitor */
// FUNCTION: TIE95 0x6C078
// FUNCTION: TIE98 0x4926A0
static int16_t train_Draw_Train_Screen_Mission(Rect* src) {
	int16_t num_lines, text_y, max_width, text_x, t, line_idx, fade;
	Rect dst;
	char string[48], buf[48], name[48];

	xrect_Copy_Rect(&dst, src);
	t = train_time;
	num_lines = shipext_Num_Train_Mission_Text_Lines();

	/* Center vertically */
	dst.top +=
		(dst.bottom - dst.top - (TIE_FRONTEND_EDITION(10, xfont_Get_FontID_Height(2)) * num_lines + 18)) >> 1;

	text_x = dst.left + TIE_FRONTEND_EDITION(2, 4);
	text_y = dst.top + 10;
	dst.bottom = dst.top + TIE_FRONTEND_EDITION(18, xfont_Get_FontID_Height(2) + 9);

	/* Find max line width to center horizontally */
	max_width = 0;
	for (line_idx = 0; line_idx < num_lines; line_idx++) {
		int16_t old_font, width;

		shipext_Get_Train_Mission_Text(string, line_idx);
		old_font = xfont_Get_Font();
		xfont_Set_Font(TIE_FRONTEND_EDITION(0, 2));
		width = xfont_Get_String_Width(string);
		xfont_Set_Font(old_font);
		if (width > max_width)
			max_width = width;
	}
	max_width = (dst.right - dst.left - max_width) >> 1;

	/* Animated horizontal bars */
	if (TIE_FRONTEND_TIE98) {
		if (t < 24) {
			xpaint_Horiz_Clipped_Line(dst.left + 6 * (33 - t), dst.top + 1, 12 * t + 3, 2);
			xpaint_Horiz_Clipped_Line(dst.left + 6 * (33 - t), dst.bottom - 1, 12 * t + 3, 2);
		} else {
			xpaint_Horiz_Clipped_Line(dst.left + 6, dst.top + 1, 390, 2);
			xpaint_Horiz_Clipped_Line(dst.left + 6, dst.bottom - 1, 390, 2);
		}
		if (t < xfont_Get_FontID_Height(2) * num_lines) {
			int16_t bar_t = t - (10 * num_lines - 10);
			if (bar_t >= 0)
				xpaint_Horiz_Clipped_Line(dst.left + 6 * (33 - bar_t),
										  dst.bottom + xfont_Get_FontID_Height(2) * num_lines + 2,
										  12 * bar_t + 3, 2);
		} else {
			xpaint_Horiz_Clipped_Line(dst.left + 6, dst.bottom + xfont_Get_FontID_Height(2) * num_lines + 2,
									  390, 2);
		}
	} else {
		if (t < 16) {
			xpaint_Horiz_Clipped_Line(dst.left + 99 - 6 * t, dst.top + 1, 12 * t + 3, 2);
			xpaint_Horiz_Clipped_Line(dst.left + 99 - 6 * t, dst.bottom - 1, 12 * t + 3, 2);
		} else {
			xpaint_Horiz_Clipped_Line(dst.left + 3, dst.top + 1, 195, 2);
			xpaint_Horiz_Clipped_Line(dst.left + 3, dst.bottom - 1, 195, 2);
		}
		if (t < 10 * num_lines) {
			int16_t bar_t = t - (10 * num_lines - 16);
			if (bar_t >= 0)
				xpaint_Horiz_Clipped_Line(dst.left + 99 - 6 * bar_t, dst.bottom + 10 * num_lines + 2,
										  12 * bar_t + 3, 2);
		} else {
			xpaint_Horiz_Clipped_Line(dst.left + 3, dst.bottom + 10 * num_lines + 2, 195, 2);
		}
	}

	/* Draw header line (ship name + level) then mission text lines */
	for (line_idx = 0; line_idx <= num_lines && t >= 0; line_idx++) {
		fade = (t > 7) ? 31 : 2 * t + 16;
		if (line_idx == 0) {
			/* Header: "ShipName Level N" */
			textext_Copy_Text(string, txtTrainLevel);
			shipext_Get_Train_Ship_Name(name);
#ifdef TIE_MODERN
			snprintf(buf, sizeof(buf), string, shipext_Get_Train_Level() + 1);
#else
			sprintf(buf, string, shipext_Get_Train_Level() + 1);
#endif
			strcat(name, " ");
			strcat(name, buf);
			xfont_Print_Centered_Text(name, &dst, TIE_FRONTEND_EDITION(0, 2), fade);
		} else {
			shipext_Get_Train_Mission_Text(string, line_idx - 1);
			xfont_Print_Clipped_Text(string, text_x + max_width, text_y, TIE_FRONTEND_EDITION(0, 2), fade);
		}

		t -= 8;
		text_y += TIE_FRONTEND_EDITION(10, xfont_Get_FontID_Height(2));
	}
	return 1;
}

/* ------------------------------------------------------------------ */

/* Draw high score table on the training monitor */
// FUNCTION: TIE95 0x6C45C
// FUNCTION: TIE98 0x492A40
static int16_t train_Draw_Train_Screen_Score(Rect* src) {
	int16_t name_x, score_x, level_x, y, i;
	char string[40], str[40];

	int16_t t = train_time - 256;
	int16_t border_offset;
	if (TIE_FRONTEND_TIE98)
		border_offset = (t < 32) ? 260 - 8 * t : 2;
	else
		border_offset = (t < 32) ? 130 - 4 * t : 2;

	/* Horizontal bars */
	xpaint_Horiz_Clipped_Line(border_offset + src->left, src->top + 6,
							  src->right - src->left - 2 * border_offset, 2);
	xpaint_Horiz_Clipped_Line(border_offset + src->left, src->bottom - 6,
							  src->right - src->left - 2 * border_offset, 2);

	name_x = src->left + TIE_FRONTEND_EDITION(4, 8);
	y = src->top + TIE_FRONTEND_EDITION(10, 18);
	score_x = name_x + TIE_FRONTEND_EDITION(60, 120);
	level_x = name_x + TIE_FRONTEND_EDITION(140, 280);

	for (i = 0; i < TIE_FRONTEND_EDITION(VGA_SCORE_ENTRIES, NUM_SCORE_ENTRIES) && t >= 0; i++) {
		int16_t fade;
#ifdef TIE_MODERN
		char display_name[SCORE_NAME_LEN];
#endif

		fade = t + 16;
		if (fade > 31)
			fade = 31;
#ifdef TIE_MODERN
		TiePilotName_CopyForDisplay(display_name, sizeof(display_name), train_score_name[i]);

		if (display_name[0]) {
			xfont_Print_Clipped_Text(display_name, name_x, y, TIE_FRONTEND_EDITION(0, 3), fade);
#else
		if (train_score_name[i][0]) {
			xfont_Print_Clipped_Text(train_score_name[i], name_x, y, TIE_FRONTEND_EDITION(0, 3), fade);
#endif
			textext_Copy_Text(string, txtTrainScore);
			sprintf(str, string, train_score_points[i]);
			xfont_Print_Clipped_Text(str, score_x, y, TIE_FRONTEND_EDITION(0, 3), fade);
			textext_Copy_Text(string, txtTrainLevel);
			sprintf(str, string, (uint16_t)train_score_level[i]);
			xfont_Print_Clipped_Text(str, level_x, y, TIE_FRONTEND_EDITION(0, 3), fade);
		}

		t -= 4;
		y += TIE_FRONTEND_EDITION(12, xfont_Get_FontID_Height(2) + 2);
	}
	return 1;
}

/* ------------------------------------------------------------------ */

/* Draw flyby course info text on the training monitor */
// FUNCTION: TIE95 0x6C644
// FUNCTION: TIE98 0x492BE0
static int16_t train_Draw_Train_Screen_Flyby(Rect* src) {
	int16_t i;
	int16_t t = train_time - 384;
	char text[48];

	xfont_Enable_FontID_Shadow(TIE_FRONTEND_EDITION(0, 2));

	for (i = 0; train_course_info[i] != -1; i += 5) {
		if (t >= train_course_info[i] && t < train_course_info[i + 1]) {
			int16_t px, py;
			uint16_t text_id;
			int16_t fade = t - train_course_info[i] + 16;
			if (TIE_FRONTEND_TIE98) {
				px = src->left + 2 * train_course_info[i + 2];
				py = src->top + 2 * train_course_info[i + 3];
			} else {
				px = train_course_info[i + 2] + src->left;
				py = train_course_info[i + 3] + src->top;
			}
			text_id = train_course_info[i + 4];
			if (fade > 31)
				fade = 31;
			textext_Copy_Text(text, text_id);
			xfont_Print_Clipped_Text(text, px, py, TIE_FRONTEND_EDITION(0, 2), fade);
		}
	}

	xfont_Disable_FontID_Shadow(TIE_FRONTEND_EDITION(0, 2));
	return 1;
}
