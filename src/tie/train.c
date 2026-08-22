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

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
#include "tie/map.h"
#include "tie/rand.h"
#include "tie/shellext.h"
#include "tie/shipext.h"
#include "tie/soundext.h"
#include "tie/textext.h"
#include "tie/train.h"
#include "tie_runtime/presentation/pilot_name.h"
#include "tie_runtime/runtime/profile.h"
#include "tie_runtime/storage/score_tables.h"
#include <landru/task.h>

#include "tie/bpflight.h"

static const char train_film_b[] = "trainbrf";
static const char train_score_filename[] = "train.hgh";

#define NUM_SCORE_ENTRIES TRAIN_SCORE_ENTRY_COUNT
#define SCORE_NAME_LEN TRAIN_SCORE_NAME_CAPACITY
#define VGA_SCORE_ENTRIES 8

typedef struct TrainSpec {
	LandruSurfaceSet surface_set;
	const char* archive;
	const char* film_a;
	int16_t width, height;
	int16_t mouse_x, mouse_y;
	int16_t monitor_bounds[4];
	int16_t input_bounds[6][4];
	bool separate_button_actors;
	bool clear_monitor;
} TrainSpec;

/* DATA: TIE95 TRAIN_Train; TIE98 0x491B30. */
static const TrainSpec train_specs[] = {
    {
        .surface_set = LANDRU_SURFACE_VGA,
        .archive = "train.lfd", .film_a = "train",
        .width = 320, .height = 200,
        .mouse_x = 220, .mouse_y = 190,
        .monitor_bounds = {62, 6, 254, 116},
        .input_bounds = {
            {188, 138, 214, 154},
            {214, 138, 240, 154},
            {188, 154, 214, 170},
            {214, 154, 240, 170},
            {206, 184, 237, 200},
            {49, 181, 79, 200},
        },
        .separate_button_actors = false,
        .clear_monitor = false,
    },
    {
        .surface_set = LANDRU_SURFACE_SVGA,
        .archive = "train640.lfd", .film_a = "train640",
        .width = 640, .height = 480,
        .mouse_x = 235, .mouse_y = 465,
        .monitor_bounds = {144, 56, 500, 300},
        .input_bounds = {
            {377, 336, 398, 346},
            {398, 336, 422, 346},
            {386, 454, 416, 468},
            {416, 454, 446, 468},
            {212, 455, 252, 474},
            {453, 325, 522, 400},
        },
        .separate_button_actors = true,
        .clear_monitor = true,
    },
};

static const TrainSpec* active_spec;

/* The first eight defaults are shared by both originals; TIE98 adds two
 * empty slots and displays all ten entries. */
// GLOBAL: TIE95 0xCE5CA; TIE98 0x4F2F18
static char train_score_name[NUM_SCORE_ENTRIES][SCORE_NAME_LEN] = {
	"Luke", "Jon", "Larry", "Peter", "Bucky", "Jim", "Edward", "Wade",
};
// GLOBAL: TIE95 0xCE61C; TIE98 0x4F3068
static int32_t train_score_points[NUM_SCORE_ENTRIES] = {
	100, 100, 100, 100, 100, 100, 100, 100,
};
// GLOBAL: TIE95 0xCE63C; TIE98 0x4F3090
static int16_t train_score_level[NUM_SCORE_ENTRIES] = {
	1, 1, 1, 1, 1, 1, 1, 1,
};

/* Flyby course info: 5-word entries {start_time, end_time, y, x, text_id},
 * terminated by a sentinel with start_time == -1. */
typedef struct {
	int16_t start_time;
	int16_t end_time;
	int16_t y_offset;
	int16_t x_offset;
	int16_t text_id;
} CourseInfoEntry;

static const CourseInfoEntry train_course_info[] = {
	{ 20, 84, 60, 90, txtTrainCourse },
	{ 25, 84, 60, 100, txtTrainSegment },
	{ 115, 150, 53, 70, txtTrainPyramid },
	{ 120, 150, 50, 80, txtTrainBonus },
	{ 175, 230, 30, 40, txtTrainObstacle },
	{ 180, 230, 15, 50, txtTrainDestroy },
	{ 235, 290, 10, 90, txtTrainSphere },
	{ 240, 290, 5, 100, txtTrainBonus },
	{ 310, 375, 60, 40, txtTrainAdvance },
	{ 315, 375, 48, 50, txtTrainAdvance2 },
	{ -1, 0, 0, 0, 0 },
};

/* Module state */
static ResFile* train_file;
// GLOBAL: TIE 0xF5798
static Film* train_film;
static Input* world_input;
static Input* button_input[6];
static Input* monitor_input;
// GLOBAL: TIE 0xF5790
static Actor* arrow_actor;
// GLOBAL: TIE 0xF5780
static Actor* button[6]; /* TIE98 stores one actor for each input. */
// GLOBAL: TIE 0xF57A0
static Actor* helmet;
// GLOBAL: TIE 0xF5788
static int32_t train_time;
// GLOBAL: TIE 0xF578C
static int32_t train_mode;
// GLOBAL: TIE 0xF57A8
static int16_t train_help;
// GLOBAL: TIE98 0x50B328
static int32_t train_monitor_needs_clear;

/* train_pilot_medal_status: snapshot of train_max_level[ship] BEFORE the
 * training round so map.c (TRAIN_MAP scene) can detect a fresh max-level
 * achievement via "(old < 4) && (new >= 4)". The storage lives in map.c
 * (declared extern in map.h); both writers must reach the same global. */

/* Forward declarations (referenced by film callback before definition) */
static int train_draw_Train_Help(Actor* the_actor, Rect* draw_rect, Rect* clip_rect, int16_t off_x,
								 int16_t off_y, int16_t refresh);
static void train_user_Train_Clam(Actor* the_actor, int32_t time);
static void train_user_Train_Light(Actor* the_actor, int32_t time);
static void train_user_Train_Helmet(Actor* the_actor, int32_t time);

/* ------------------------------------------------------------------ */

// FUNCTION: TIE95 0x6B7EC; TIE98 0x491EC0
static void train_end_Train_View(int32_t time) {
	if (time == 0 && !lcursor_Is_Cursor_Visible())
		lcursor_Show_Cursor();
}

/* ------------------------------------------------------------------ */

// FUNCTION: TIE95 0x6B800; TIE98 0x491EE0
static int16_t train_film_Train_Callback(Film* the_film, FilmObject* film_object) {
	if (active_spec->surface_set == LANDRU_SURFACE_SVGA && film_object->id == FTC_PALETTE) {
		lfilm_Rewind_Palette_Film(the_film, film_object, (void*)(film_object + 1));
		return 0;
	}

	if (film_object->id != 3)
		return 0;

	lfilm_Rewind_Actor_Film(the_film, film_object, (void*)(film_object + 1));
	Actor* the_actor = (Actor*)film_object->object;
	int16_t var1 = the_actor->var1;

	switch (var1) {
		case 1:
			lactor_Set_Actor_Draw_Function(the_actor, train_draw_Train_Help);
			break;
		case 5:
			button[the_actor->var2] = the_actor;
			break;
		case 10:
			lactor_Set_Actor_User_Function(the_actor, train_user_Train_Clam);
			return 0;
		case 12:
			lactor_Set_Actor_User_Function(the_actor, train_user_Train_Helmet);
			helmet = the_actor;
			return 0;
		case 15:
			arrow_actor = the_actor;
			return 0;
		case 16:
			lactor_Set_Actor_User_Function(the_actor, train_user_Train_Light);
			return 0;
		case 20:
			lactor_Non_Refreshable_Actor(the_actor);
			break;
	}

	return 0;
}

/* ------------------------------------------------------------------ */

// FUNCTION: TIE95 0x6B8DC; TIE98 0x491FF0
static int16_t train_iupdate_Train(Input* input, Rect* draw_rect, Rect* clip_rect, int16_t active,
								   uint8_t mouseState, uint8_t prevMouseState, int16_t key, int16_t prevKey) {
	(void)draw_rect;
	(void)clip_rect;
	(void)key;
	(void)prevKey;

	if (active)
		return 0;

	train_help = input->id;

	if (!mouseState && !prevMouseState)
		return 1;

	int16_t id = input->id;

	if (active_spec->separate_button_actors) {
		Actor* input_actor;

		if (id == 5)
			input_actor = button[0];
		else if (id == 6)
			input_actor = button[1];
		else
			input_actor = button[id + 1];

		if (mouseState == 3 || prevMouseState == 3) {
			lactor_Set_Actor_State(input_actor, 0, 0);
			linpattr_Selected_Input(input);
		} else {
			if (mouseState == 1 || prevMouseState == 1)
				soundext_Play_SFX(sfxButton, id <= 4 ? 95 : 80);
			lactor_Set_Actor_State(input_actor, 1, 0);
		}
		return 1;
	}

	if (id == 5) {
		/* Start training button */
		if (mouseState == 3 || prevMouseState == 3) {
			lactor_Set_Actor_State(button[0], 2, 0);
			linpattr_Selected_Input(input);
		} else {
			if (mouseState == 1 || prevMouseState == 1)
				soundext_Play_SFX(sfxButton, 80);
			lactor_Set_Actor_State(button[0], 3, 0);
		}
	} else if (id == 6) {
		/* Exit door button */
		if (mouseState == 3 || prevMouseState == 3) {
			lactor_Set_Actor_State(button[1], 0, 0);
			linpattr_Selected_Input(input);
		} else {
			if (mouseState == 1 || prevMouseState == 1)
				soundext_Play_SFX(sfxButton, 80);
			lactor_Set_Actor_State(button[1], 1, 0);
		}
	} else {
		/* Nav buttons 1-4 */
		if (mouseState == 3 || prevMouseState == 3) {
			linpattr_Clear_Input_Flag1(input);
			linpattr_Selected_Input(input);
			lactor_Hide_Actor(arrow_actor);
		}
		if (mouseState == 1 || prevMouseState == 1) {
			linpattr_Set_Input_Flag1(input);
			lactor_Show_Actor(arrow_actor);
			lactor_Set_Actor_State(arrow_actor, 2 * (input->id - 1), 0);
			soundext_Play_SFX(sfxButton, 95);
		}
	}
	return 1;
}

/* ------------------------------------------------------------------ */

// FUNCTION: TIE95 0x6BA58; TIE98 0x492150
static void train_iuser_Train(Input* input, int32_t time) {
	(void)time;

	if (!linpattr_Get_Input_Selected(input) || helmet->var2)
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
			lerror_Set_Landru_Exit(SCENE_MAIN_MENU);
			break;
	}

	if (active_spec->clear_monitor && input->id <= 4)
		train_monitor_needs_clear = 1;
}

/* ------------------------------------------------------------------ */

// FUNCTION: TIE95 0x6BB60
static void train_idraw_Train(Input* input, Rect* draw_rect, Rect* clip_rect, int16_t refresh) {
	char buf[48];

	if (!refresh)
		return;

	/* Fade color based on helmet state: dim when visor is down */
	int16_t color;
	if (helmet->state >= 5)
		color = 16;
	else
		color = 31 - 3 * helmet->state;

	if (color == 16)
		return;

	int16_t id = input->id;

	if (id == 6) {
		/* Ship name (truncated at parenthesis) */
		shipext_Get_Train_Ship_Name(buf);
		for (int16_t i = 0; buf[i]; i++) {
			if (buf[i] == '(') {
				if (i > 0 && buf[i - 1] == ' ')
					buf[i - 1] = '\0';
				else
					buf[i] = '\0';
				break;
			}
		}
		lfont_Print_Centered_Text(buf, draw_rect, color, 1);
	} else if (id == 7) {
		/* "Level N" */
		char fmt[32];
		textext_Copy_Text(fmt, txtTrainLevel);
		snprintf(buf, sizeof(buf), fmt, shipext_Get_Train_Level() + 1);
		lfont_Print_Centered_Text(buf, draw_rect, color, 1);
	}

	if (linpattr_Is_Input_Dirty(input))
		ldirty_Dirty_Rect(clip_rect);
}

/* ------------------------------------------------------------------ */

/*
 * Help tooltip overlay. Draws the delta actor with centered text from
 * the TIEText table when a nav button is hovered and visor is up.
 */
// FUNCTION: TIE95 0x6BC68; TIE98 0x492200
static int train_draw_Train_Help(Actor* the_actor, Rect* draw_rect, Rect* clip_rect, int16_t off_x,
								 int16_t off_y, int16_t refresh) {
	if (refresh) {
		if (train_help && !helmet->state) {
			lactdelt_Draw_Delta_Actor(the_actor, draw_rect, clip_rect, off_x, off_y, refresh);
			Rect bounds;
			lactor_Get_Actor_Bounds(the_actor, &bounds);
			lfont_Enable_FontID_Shadow(0);
			char text[32];
			/* train_help 1-6 maps to txtTrainLastShip(74)..txtTrainExit(79) */
			textext_Copy_Text(text, (int16_t)(train_help + 73));
			lfont_Print_Centered_Text(text, &bounds, 15,
									  active_spec->surface_set == LANDRU_SURFACE_SVGA ? 2 : 0);
			lfont_Disable_FontID_Shadow(0);
		}
		train_help = 0;
	}
	return 1;
}

/* ------------------------------------------------------------------ */

/* Freeze clam actor on the final film frame */
// FUNCTION: TIE95 0x6BD00; TIE98 0x4922B0
static void train_user_Train_Clam(Actor* the_actor, int32_t time) {
	if (time == (int32_t)train_film->cels)
		lactor_Non_Refreshable_Actor(the_actor);
}

/* ------------------------------------------------------------------ */

/*
 * Flickering light animation. Uses var2 as a countdown timer.
 * Bit 14 of var2 distinguishes "on" vs "off" phase.
 * Each phase has random frame changes and a random-length hold.
 */
// FUNCTION: TIE95 0x6BD1C; TIE98 0x4922E0
static void train_user_Train_Light(Actor* the_actor, int32_t time) {
	if (time == 0) {
		lactor_Show_Actor(the_actor);
		the_actor->var2 = (rand_rand() & 0xF) + 2;
	}

	if (the_actor->var2 & 0x4000) {
		/* "On" phase: random state every other frame */
		if (time & 1)
			the_actor->state = rand_rand() % the_actor->arraySize;
		int16_t countdown = the_actor->var2 & 0x3FFF;
		if (countdown == 1) {
			the_actor->var2 = (rand_rand() & 0xF) + 2;
			return;
		}
	} else {
		/* "Off" phase: random state, check var1 for phase toggle */
		the_actor->state = rand_rand() % the_actor->arraySize;
		if (the_actor->var1 == 1) {
			the_actor->var2 = (rand_rand() & 0xF) + 0x4002;
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
// FUNCTION: TIE95 0x6BDD0; TIE98 0x492390
static void train_user_Train_Helmet(Actor* the_actor, int32_t time) {
	if (the_actor->var2) {
		/* Entering training — close visor */
		if (!lactor_Is_Actor_Visible(the_actor)) {
			lactor_Show_Actor(the_actor);
			lactor_Set_Actor_State(the_actor, 0, 0);
			soundext_Play_SFX(sfxVisor, 80);
		} else {
			int16_t next_state = the_actor->state + 1;
			if (next_state == the_actor->arraySize) {
				/* Visor fully closed — enter training */
				shipext_Update_Pilot();
				lerror_Set_Landru_Exit(SCENE_FLIGHT_TRAIN);
				soundext_Stop_SFX(sfxVisor);
				soundext_Play_SFX(sfxVisorClick, 80);
				uint8_t ship = shipext_Get_Train_Ship();
				train_pilot_medal_status = pilot_record.train_max_level[ship];
			} else {
				lactor_Set_Actor_State(the_actor, next_state, 0);
			}
		}
	} else {
		/* Idle / returning from training */
		int16_t cur_scene = shellext_Get_Cur_Scene();
		if (time < the_actor->arraySize && cur_scene == SCENE_TRAIN_B) {
			/* Auto-play visor opening (reverse) */
			if (!lactor_Is_Actor_Visible(the_actor)) {
				lactor_Show_Actor(the_actor);
				soundext_Play_SFX(sfxVisor, 80);
			}
			lactor_Set_Actor_State(the_actor, the_actor->arraySize - (time + 1), 0);
		} else {
			/* Idle: nothing to refresh if visor is already hidden. */
			if (!lactor_Is_Actor_Visible(the_actor))
				return;
			soundext_Stop_SFX(sfxVisor);
			soundext_Play_SFX(sfxVisorClick, 80);
			lactor_Hide_Actor(the_actor);
		}
		lview_Refresh_View();
	}
}

/* ------------------------------------------------------------------ */

// FUNCTION: TIE95 0x6BEF8; TIE98 0x4924C0
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
		linpattr_Selected_Input(input);
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
// FUNCTION: TIE95 0x6BF20; TIE98 0x492500
static void train_iuser_Train_Screen(Input* input, int32_t time) {
	(void)time;

	if (linpattr_Get_Input_Selected(input)) {
		if (train_time > 384)
			train_time = 767;
		else if (train_time > 256)
			train_time = 384;
		else if (train_time > 0)
			train_time = 256;
	}

	if (train_time == 0) {
		train_time = 1;
		train_mode = 0;
		if (active_spec->clear_monitor)
			train_monitor_needs_clear = 1;
		return;
	}

	if (train_time == 256) {
		train_time++;
		train_mode = 1;
		if (active_spec->clear_monitor)
			train_monitor_needs_clear = 1;
		return;
	}

	if (train_time == 384) {
		bpflight_Start_Movie_Engine();
		bpflight_Open_New_Matrix("trnfly1");
		train_mode = 2;
		train_time++;
		if (active_spec->clear_monitor)
			train_monitor_needs_clear = 1;
		return;
	}

	if (train_time == 767) {
		bpflight_Stop_Movie_Engine();
		train_time = 0;
		if (active_spec->clear_monitor)
			train_monitor_needs_clear = 1;
		return;
	}

	train_time++;
}

/* ------------------------------------------------------------------ */

/* Draw mission description on the training monitor */
// FUNCTION: TIE95 0x6C078; TIE98 0x4926A0
static void train_Draw_Train_Screen_Mission(Rect* src) {
	Rect dst;
	char string[48], buf[48], name[48];
	bool svga = active_spec->surface_set == LANDRU_SURFACE_SVGA;
	int16_t font_id = svga ? 2 : 0;
	int16_t font_height = svga ? lfont_Get_FontID_Height(2) : 10;

	lrect_Copy_Rect(&dst, src);
	int16_t t = train_time;
	int16_t num_lines = shipext_Num_Train_Mission_Text_Lines();

	/* Center vertically */
	dst.top += (dst.bottom - dst.top - (font_height * num_lines + 18)) >> 1;

	int16_t text_x = dst.left + (svga ? 4 : 2);
	int16_t text_y = dst.top + 10;
	dst.bottom = dst.top + (svga ? font_height + 9 : 18);

	/* Find max line width to center horizontally */
	int16_t max_width = 0;
	for (int16_t i = 0; i < num_lines; i++) {
		shipext_Get_Train_Mission_Text(string, i);
		int16_t old_font = lfont_Get_Font();
		lfont_Set_Font(font_id);
		int16_t w = lfont_Get_String_Width(string);
		lfont_Set_Font(old_font);
		if (w > max_width)
			max_width = w;
	}
	int16_t text_left = ((dst.right - dst.left - max_width) >> 1) + text_x;

	/* Animated horizontal bars */
	if (svga) {
		if (t >= 24) {
			lpaint_Horiz_Clipped_Line(dst.left + 6, dst.top + 1, 390, 2);
			lpaint_Horiz_Clipped_Line(dst.left + 6, dst.bottom - 1, 390, 2);
		} else {
			lpaint_Horiz_Clipped_Line(dst.left + 6 * (33 - t), dst.top + 1, 12 * t + 3, 2);
			lpaint_Horiz_Clipped_Line(dst.left + 6 * (33 - t), dst.bottom - 1, 12 * t + 3, 2);
		}
	} else {
		if (t >= 16) {
			lpaint_Horiz_Clipped_Line(dst.left + 3, dst.top + 1, 195, 2);
			lpaint_Horiz_Clipped_Line(dst.left + 3, dst.bottom - 1, 195, 2);
		} else {
			lpaint_Horiz_Clipped_Line(dst.left + 99 - 6 * t, dst.top + 1, 12 * t + 3, 2);
			lpaint_Horiz_Clipped_Line(dst.left + 99 - 6 * t, dst.bottom - 1, 12 * t + 3, 2);
		}
	}

	int16_t bar_y = dst.bottom + font_height * num_lines + 2;
	if (svga) {
		if (t >= font_height * num_lines) {
			lpaint_Horiz_Clipped_Line(dst.left + 6, bar_y, 390, 2);
		} else {
			int16_t bt = t - (10 * num_lines - 10);
			if (bt >= 0) {
				lpaint_Horiz_Clipped_Line(dst.left + 6 * (33 - bt), bar_y, 12 * bt + 3, 2);
			}
		}
	} else {
		if (t >= 10 * num_lines) {
			lpaint_Horiz_Clipped_Line(dst.left + 3, bar_y, 195, 2);
		} else {
			int16_t bt = t - (10 * num_lines - 16);
			if (bt >= 0) {
				lpaint_Horiz_Clipped_Line(dst.left + 99 - 6 * bt, bar_y, 12 * bt + 3, 2);
			}
		}
	}

	/* Draw header line (ship name + level) then mission text lines */
	int16_t line_idx = 0;
	int16_t text_line = -1;
	while (t >= 0) {
		int16_t fade = (t <= 7) ? 2 * t + 16 : 31;

		if (line_idx == 0) {
			/* Header: "ShipName Level N" */
			textext_Copy_Text(string, txtTrainLevel);
			shipext_Get_Train_Ship_Name(name);
			snprintf(buf, sizeof(buf), string, shipext_Get_Train_Level() + 1);
			strcat(name, " ");
			strcat(name, buf);
			lfont_Print_Centered_Text(name, &dst, fade, font_id);
		} else {
			shipext_Get_Train_Mission_Text(string, text_line);
			lfont_Print_Clipped_Text(string, text_left, text_y, font_id, fade);
		}

		t -= 8;
		line_idx++;
		text_y += font_height;
		text_line++;

		if (line_idx > num_lines)
			break;
	}
}

/* ------------------------------------------------------------------ */

/* Draw high score table on the training monitor */
// FUNCTION: TIE95 0x6C45C; TIE98 0x492A40
static void train_Draw_Train_Screen_Score(Rect* src) {
	char string[40], str[40];
	bool svga = active_spec->surface_set == LANDRU_SURFACE_SVGA;
	int16_t font_id = svga ? 3 : 0;

	int16_t t = train_time - 256;
	int16_t border_offset;
	if (svga)
		border_offset = (t >= 32) ? 2 : 260 - 8 * t;
	else
		border_offset = (t >= 32) ? 2 : 130 - 4 * t;
	int16_t total_width = 2 * border_offset;

	/* Horizontal bars */
	lpaint_Horiz_Clipped_Line(border_offset + src->left, src->top + 6, src->right - src->left - total_width,
							  2);
	lpaint_Horiz_Clipped_Line(border_offset + src->left, src->bottom - 6,
							  src->right - src->left - total_width, 2);

	int16_t name_x = src->left + (svga ? 8 : 4);
	int16_t score_x = name_x + (svga ? 120 : 60);
	int16_t level_x = name_x + (svga ? 280 : 140);
	int16_t y = src->top + (svga ? 18 : 10);

	const int16_t displayed_scores = svga ? NUM_SCORE_ENTRIES : VGA_SCORE_ENTRIES;
	for (int16_t i = 0; i < displayed_scores && t >= 0; i++) {
		int16_t fade = (t + 16 > 31) ? 31 : t + 16;
		char display_name[SCORE_NAME_LEN];
		TiePilotName_CopyForDisplay(display_name, sizeof(display_name), train_score_name[i]);

		if (display_name[0]) {
			lfont_Print_Clipped_Text(display_name, name_x, y, font_id, fade);
			textext_Copy_Text(string, txtTrainScore);
			snprintf(str, sizeof(str), string, train_score_points[i]);
			lfont_Print_Clipped_Text(str, score_x, y, font_id, fade);
			textext_Copy_Text(string, txtTrainLevel);
			snprintf(str, sizeof(str), string, (uint16_t)train_score_level[i]);
			lfont_Print_Clipped_Text(str, level_x, y, font_id, fade);
		}

		t -= 4;
		y += svga ? lfont_Get_FontID_Height(2) + 2 : 12;
	}
}

/* ------------------------------------------------------------------ */

/* Draw flyby course info text on the training monitor */
// FUNCTION: TIE95 0x6C644; TIE98 0x492BE0
static void train_Draw_Train_Screen_Flyby(Rect* src) {
	int16_t t = train_time - 384;
	char text[48];
	bool svga = active_spec->surface_set == LANDRU_SURFACE_SVGA;
	int16_t font_id = svga ? 2 : 0;

	lfont_Enable_FontID_Shadow(font_id);

	for (int i = 0; train_course_info[i].start_time != -1; i++) {
		const CourseInfoEntry* e = &train_course_info[i];
		if (t >= e->start_time && t < e->end_time) {
			int16_t fade = t - e->start_time + 16;
			if (fade > 31)
				fade = 31;
			int16_t px;
			int16_t py;
			if (svga) {
				px = src->left + 2 * e->y_offset;
				py = src->top + 2 * e->x_offset;
			} else {
				px = src->left + e->y_offset;
				py = src->top + e->x_offset;
			}
			textext_Copy_Text(text, e->text_id);
			lfont_Print_Clipped_Text(text, px, py, font_id, fade);
		}
	}

	lfont_Disable_FontID_Shadow(font_id);
}

/* ------------------------------------------------------------------ */

// FUNCTION: TIE95 0x6C018; TIE98 0x492610
static void train_idraw_Train_Screen(Input* input, Rect* draw_rect, Rect* clip_rect, int16_t refresh) {
	if (!refresh)
		return;

	if (active_spec->clear_monitor && train_monitor_needs_clear) {
		lpaint_Paint_Clipped_Rect(draw_rect, 0);
		train_monitor_needs_clear = 0;
	}

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

	if (linpattr_Is_Input_Dirty(input))
		ldirty_Dirty_Rect(clip_rect);
}

/* ------------------------------------------------------------------ */

typedef enum {
	TRAIN_PHASE_BEGIN = 0,
	TRAIN_PHASE_CLEANUP = 1,
} TrainPhase;

typedef struct TrainTask {
	SceneHeadStruct* the_head;
	TrainPhase phase;
	const TrainSpec* spec;
} TrainTask;

/* PORT: asynchronous adaptation of TIE95 TRAIN_Train and
 * TIE98 TRAIN_Train (0x491B30). */
static LandruTaskStepResult train_task_step(void* self) {
	TrainTask* t = (TrainTask*)self;

	if (t->phase == TRAIN_PHASE_BEGIN) {
		Rect frame;
		const int16_t* bounds;

		active_spec = t->spec;
		if (active_spec->surface_set == LANDRU_SURFACE_SVGA) {
			(void)lsurface_Select_Surface_Set(active_spec->surface_set);
			lview_Init_View(lview_Get_Current_View());
			lvesa_Erase_Video(16);
		}

		lio_Set_Mouse_Position(active_spec->mouse_x, active_spec->mouse_y);

		train_file = shellext_Open_Empire_Resource(active_spec->archive);
		lviewadd_Clear_View();
		lview_Disable_All_View_Erase();

		/* Select film based on scene: entry A = first visit, B = return */
		lrect_Set_Rect(&frame, 0, 0, active_spec->width, active_spec->height);
		const char* film_name =
			(shellext_Get_Cur_Scene() == SCENE_TRAIN_A) ? active_spec->film_a : train_film_b;
		train_film = lfilm_Res_Callback_Film(film_name, &frame, 0, 0, 0, train_film_Train_Callback);
		lfilm_Set_Film_Def_Palette(train_film, t->the_head->def_palette);

		/* World input (full screen) */
		lrect_Set_Rect(&frame, 0, 0, active_spec->width, active_spec->height);
		world_input = linput_Alloc_Input(NULL, &frame, 0, 0);

		/* Monitor screen input */
		bounds = active_spec->monitor_bounds;
		lrect_Set_Rect(&frame, bounds[0], bounds[1], bounds[2], bounds[3]);
		monitor_input = linput_Alloc_Input(world_input, &frame, 0, 0);
		linpattr_Set_Input_Update_Function(monitor_input, train_iupdate_Train_Screen);
		linpattr_Set_Input_User_Function(monitor_input, train_iuser_Train_Screen);
		linpattr_Set_Input_Draw_Function(monitor_input, train_idraw_Train_Screen);
		linpattr_Refreshable_Input(monitor_input);
		monitor_input->id = 0;

		/* 6 navigation buttons */
		for (int16_t i = 0; i < 6; i++) {
			bounds = active_spec->input_bounds[i];
			lrect_Set_Rect(&frame, bounds[0], bounds[1], bounds[2], bounds[3]);
			button_input[i] = linput_Alloc_Input(world_input, &frame, 0, 0);
			linpattr_Set_Input_Update_Function(button_input[i], train_iupdate_Train);
			linpattr_Set_Input_User_Function(button_input[i], train_iuser_Train);
			button_input[i]->mouseUsage = 4;
			button_input[i]->id = i + 1;
		}

		train_time = 0;
		train_help = 0;
		train_monitor_needs_clear = active_spec->clear_monitor;

		bpflight_Open_Flight_Engine(1);
		bpflight_Stop_Movie_Engine();

		/* Load either legacy TIE95 scores or the shared canonical format. */
		TrainingScoreEntry loaded_scores[TRAIN_SCORE_ENTRY_COUNT];
		if (TieScoreTables_LoadTraining(train_score_filename, loaded_scores)) {
			for (int16_t i = 0; i < NUM_SCORE_ENTRIES; i++) {
				snprintf(train_score_name[i], sizeof(train_score_name[i]), "%s", loaded_scores[i].name);
				train_score_points[i] = loaded_scores[i].score;
				train_score_level[i] = loaded_scores[i].level;
			}
		}

		/* Push the modal view task */
		lview_Set_View_Update_Function(train_end_Train_View);
		lviewadd_Push_Handle_View_Task();

		t->phase = TRAIN_PHASE_CLEANUP;
		return LANDRU_TASK_STEP_CONTINUE;
	}

	/* CLEANUP */
	linpcall_Clear_Active_Input();
	lview_Clear_View_Update_Function();
	bpflight_Close_Flight_Engine();
	lview_Enable_All_View_Erase();

	if (lcursor_Is_Cursor_Visible())
		lcursor_Hide_Cursor();

	lres_Close_Resource(train_file);
	if (t->spec->surface_set == LANDRU_SURFACE_SVGA) {
		lvesa_Erase_Video(16);
		lviewadd_Clear_View();
		(void)lsurface_Select_Surface_Set(LANDRU_SURFACE_VGA);
	}
	return LANDRU_TASK_STEP_DONE;
}

static const LandruTaskVtable train_task_vt = {
	.step = train_task_step,
};

void train_Push_Train_Task(SceneHeadStruct* the_head) {
	TrainTask* t = (TrainTask*)landru_task_push(&train_task_vt);
	if (!t)
		return;
	t->the_head = the_head;
	t->phase = TRAIN_PHASE_BEGIN;
	t->spec = &train_specs[TieProfile_FrontendId() == TIE_FRONTEND_PROFILE_TIE98 ? 1 : 0];
}
