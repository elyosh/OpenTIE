/*
 * COMBAT.C — Combat simulation room screen
 *
 * Lets the player select a combat sim ship and mission, view mission
 * descriptions, high scores, ship info, and a 3D flyby on the monitor,
 * and enter combat via the helmet visor animation. Structurally very
 * similar to TRAIN.C.
 *
 * Layout: 8 inputs (6 nav buttons + 2 refreshable text labels), a
 * monitor screen with 4 display modes (mission text, scores, ship info,
 * flyby), and actor-driven animations (flickering lights, helmet visor).
 *
 * Film actors by var1: 1=help text, 5=button[var2], 10=light,
 * 12=helmet, 15=arrow, 20=decorative.
 */

#include "tie/combat.h"
#ifdef TIE_MODERN
#include "tie_runtime/runtime/combat_task.h"
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
#include "landru/memptr.h"
#include "landru/paint.h"
#include "landru/rect.h"
#include "landru/res.h"
#include "landru/surface.h"
#include "landru/vesa.h"
#include "landru/view.h"
#include "landru/viewadd.h"
#include "tie/edition.h"
#include "tie/shellext.h"
#include "tie/shipext.h"
#include "tie/soundext.h"
#include "tie/textext.h"
#include "tie_runtime/presentation/pilot_name.h"
#ifdef TIE_MODERN
#include "tie_runtime/runtime/profile.h"
#endif
#include "tie_runtime/storage/score_tables.h"

#include "tie/bpflight.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The original TU calls the library abs() rather than the inline form. */
#ifdef __WATCOMC__
#pragma function(abs)
#endif

/* Resource names: [0] = combat LFD, [1] = train LFD, [2] = film, [3] = unused. */
// GLOBAL: TIE95 0xCE6B6
// GLOBAL: TIE98 0x4DF310
static const char combat_str[4][20] = { "combat.lfd", "train.lfd", "combat", "combutns" };

enum {
	COMBAT_MAX_MISSIONS = 8,
};

/* Module state */
// GLOBAL: TIE95 0xF5908
// GLOBAL: TIE98 0x50AA84
static ResFile* combat_file;
#ifndef TIE98
/* TIE98 no longer opens TRAIN.LFD from the combat room. */
// GLOBAL: TIE95 0xF58E8
static ResFile* train_file;
#endif
// GLOBAL: TIE95 0xF5904
// GLOBAL: TIE98 0x50AA80
static Film* combat_film;
// GLOBAL: TIE95 0xF58F4
// GLOBAL: TIE98 0x50AA70
static Input* world_input;
// GLOBAL: TIE95 0xF58B8
// GLOBAL: TIE98 0x50AA88
static Input* button_input[8];
// GLOBAL: TIE95 0xF5900
// GLOBAL: TIE98 0x50AA54
static Input* monitor_input;
// GLOBAL: TIE95 0xF58E0
static Actor* arrow_actor;
// GLOBAL: TIE95 0xF58D8
static Actor* button[7];
// GLOBAL: TIE95 0xF58FC
// GLOBAL: TIE98 0x50AA6C
static Actor* helmet;
// GLOBAL: TIE95 0xF58E4
// GLOBAL: TIE98 0x50AAA8
static int32_t combat_time;
// GLOBAL: TIE98 0x50AA58
static int32_t combat_mode;
// GLOBAL: TIE95 0xF58F0
// GLOBAL: TIE98 0x50AA60
static int32_t combat_round;
// GLOBAL: TIE95 0xF5910
static int16_t combat_help;
// GLOBAL: TIE95 0xF590C
// GLOBAL: TIE98 0x50AAAC
static int16_t combat_num_scores;
// GLOBAL: TIE95 0xF5912
// GLOBAL: TIE98 0x50AA74
static int16_t combat_score_id;
// GLOBAL: TIE98 0x50AA68
static int32_t combat_monitor_needs_clear;

/* Forward declarations (referenced by film callback) */
static int16_t combat_draw_Combat_Help(Actor* the_actor, Rect* draw_rect, Rect* clip_rect, int16_t off_x,
									   int16_t off_y, int16_t refresh);
static int16_t combat_draw_Combat_Back(Actor* the_actor, Rect* draw_rect, Rect* clip_rect, int16_t off_x,
									   int16_t off_y, int16_t refresh);
static void combat_user_Combat_Light(Actor* the_actor, int32_t time);
static void combat_user_Combat_Helmet(Actor* the_actor, int32_t time);

/* ------------------------------------------------------------------ */

/*
 * Load combat high scores for the current ship/battle.
 * Generates filename: ship01-12.hgh or battle01+.hgh.
 * Caches by combat_score_id to avoid redundant loads.
 */
// FUNCTION: TIE95 0x6DF54
static void combat_Load_Combat_High_Scores(void) {
	char filename[16];

	int16_t ship = shipext_Get_Combat_Ship();
	if (ship == combat_score_id)
		return;

	if (ship < 12) {
		strcpy(filename, "shipxx.hgh");
		filename[4] = (char)(ship + 1) / 10 + '0';
		filename[5] = (char)(ship + 1) % 10 + '0';
	} else {
		strcpy(filename, "battlexx.hgh");
		filename[6] = (char)(ship - 11) / 10 + '0';
		filename[7] = (char)(ship - 11) % 10 + '0';
	}

	if (combat_score_data) {
		free(combat_score_data);
		combat_score_data = NULL;
	}
	combat_score_data = calloc(COMBAT_MAX_MISSIONS, sizeof(GameScoreHead));
	combat_num_scores = 0;
	if (combat_score_data)
		TieScoreTables_LoadGame(filename, combat_score_data, COMBAT_MAX_MISSIONS, &combat_num_scores);

	combat_score_id = ship;
}

/* ------------------------------------------------------------------ */

// FUNCTION: TIE95 0x6CE98
static void combat_end_Combat_View(int32_t time) {
	if (time == 0 && !xcursor_Is_Cursor_Visible())
		xcursor_Show_Cursor();
}

/* ------------------------------------------------------------------ */

// FUNCTION: TIE95 0x6CEAC
// FUNCTION: TIE98 0x40A710
static int16_t combat_film_Combat_Callback(Film* the_film, FilmObject* film_object) {
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
			xactor_Set_Actor_Draw_Function(the_actor, combat_draw_Combat_Help);
			break;
		case 5:
			button[the_actor->var2] = the_actor;
			break;
		case 10:
			xactor_Set_Actor_User_Function(the_actor, combat_user_Combat_Light);
			return 0;
		case 12:
			xactor_Set_Actor_User_Function(the_actor, combat_user_Combat_Helmet);
			helmet = the_actor;
			return 0;
		case 15:
			arrow_actor = the_actor;
			return 0;
		case 20:
			xactor_Non_Refreshable_Actor(the_actor);
			break;
	}

	return 0;
}

/* ------------------------------------------------------------------ */

// FUNCTION: TIE98 0x40B870
static int16_t combat_draw_Combat_Back(Actor* the_actor, Rect* draw_rect, Rect* clip_rect, int16_t off_x,
									   int16_t off_y, int16_t refresh) {
	(void)the_actor;
	(void)clip_rect;
	(void)off_x;
	(void)off_y;
	(void)refresh;
	if (combat_mode == 2)
		xpaint_Paint_Clipped_Rect(draw_rect, 0);
	return 1;
}

/* ------------------------------------------------------------------ */

// FUNCTION: TIE95 0x6CF70
// FUNCTION: TIE98 0x40A800
static int16_t combat_iupdate_Combat(Input* input, Rect* draw_rect, Rect* clip_rect, int16_t active,
									 uint8_t mouseState, uint8_t prevMouseState, int16_t key,
									 int16_t prevKey) {
	int16_t id, tie98_button_index;

	(void)draw_rect;
	(void)clip_rect;
	(void)key;
	(void)prevKey;

	if (active)
		return 0;

	combat_help = input->id;

	if (!mouseState && !prevMouseState)
		return 1;

	id = input->id;
	/* TIE98 film actor indices are zero-based; combat input IDs are one-based. */
	tie98_button_index = id - 1;

	if (id == 5) {
		/* Start button */
		if (mouseState == 3 || prevMouseState == 3) {
			xactor_Set_Actor_State(button[TIE_FRONTEND_EDITION(1, tie98_button_index)],
								   TIE_FRONTEND_EDITION(2, 0), 0);
			xinpattr_Selected_Input(input);
		} else {
			if (mouseState == 1 || prevMouseState == 1)
				soundext_Play_SFX(sfxButton, 80);
			xactor_Set_Actor_State(button[TIE_FRONTEND_EDITION(1, tie98_button_index)],
								   TIE_FRONTEND_EDITION(3, 1), 0);
		}
	} else if (id == 6) {
		/* Exit door */
		if (mouseState == 3 || prevMouseState == 3) {
			xactor_Set_Actor_State(button[TIE_FRONTEND_EDITION(0, tie98_button_index)], 0, 0);
			xinpattr_Selected_Input(input);
		} else {
			if (mouseState == 1 || prevMouseState == 1)
				soundext_Play_SFX(sfxButton, 80);
			xactor_Set_Actor_State(button[TIE_FRONTEND_EDITION(0, tie98_button_index)], 1, 0);
		}
	} else {
		/* Nav buttons 1-4 */
		if (mouseState == 3 || prevMouseState == 3) {
			xinpattr_Clear_Input_Flag1(input);
			xinpattr_Selected_Input(input);
			if (TIE_FRONTEND_TIE98)
				xactor_Set_Actor_State(button[tie98_button_index], 0, 0);
			else
				xactor_Hide_Actor(arrow_actor);
		}
		if (mouseState == 1 || prevMouseState == 1) {
			soundext_Play_SFX(sfxButton, 80);
			xinpattr_Set_Input_Flag1(input);
			if (TIE_FRONTEND_TIE98) {
				xactor_Set_Actor_State(button[tie98_button_index], 1, 0);
			} else {
				xactor_Show_Actor(arrow_actor);
				xactor_Set_Actor_State(arrow_actor, 2 * (input->id - 1) + 1, 0);
			}
		}
	}
	return 1;
}

/* ------------------------------------------------------------------ */

// FUNCTION: TIE95 0x6D0E8
// FUNCTION: TIE98 0x40A970
static void combat_iuser_Combat(Input* input, int32_t time) {
	/* Pressure door SFX on button 5 at entry scene A with time == 4 */
	if (input->id == 5 && time == 4 && shellext_Get_Cur_Scene() == SCENE_COMBAT_A)
		soundext_Play_SFX(sfxPressureDoor, 64);

	if (!xinpattr_Get_Input_Selected(input) || helmet->var2)
		return;

	switch (input->id) {
		case 1:
			shipext_Last_Combat_Ship();
			combat_time = (combat_time >= 384) ? 0 : 128;
			bpflight_Stop_Movie_Engine();
			combat_monitor_needs_clear = TIE_FRONTEND_EDITION(false, true);
			break;
		case 2:
			shipext_Next_Combat_Ship();
			combat_time = (combat_time >= 256) ? 0 : 128;
			bpflight_Stop_Movie_Engine();
			combat_monitor_needs_clear = TIE_FRONTEND_EDITION(false, true);
			break;
		case 3:
			shipext_Last_Combat_Mission();
			combat_time = (combat_time >= 256) ? 0 : 128;
			bpflight_Stop_Movie_Engine();
			combat_monitor_needs_clear = TIE_FRONTEND_EDITION(false, true);
			break;
		case 4:
			shipext_Next_Combat_Mission();
			combat_time = (combat_time >= 256) ? 0 : 128;
			bpflight_Stop_Movie_Engine();
			combat_monitor_needs_clear = TIE_FRONTEND_EDITION(false, true);
			break;
		case 5:
			if (!helmet->var2)
				helmet->var2 = 1;
			break;
		case 6:
			xerror_Set_Landru_Exit(SCENE_MAIN_MENU);
			break;
	}
}

/* ------------------------------------------------------------------ */

// FUNCTION: TIE95 0x6D260
// FUNCTION: TIE98 0x40AAD0
static void combat_idraw_Combat(Input* input, Rect* draw_rect, Rect* clip_rect, int16_t refresh) {
	int16_t color, id, i;

	char buf[48];

	if (!refresh)
		return;

	if (helmet->state >= 5)
		color = 16;
	else
		color = 31 - 3 * helmet->state;

	if (color == 16)
		return;

	id = input->id;

	if (id == 7) {
		/* Ship name (truncated at parenthesis) */
		shipext_Get_Combat_Ship_Name(buf);
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
	} else if (id == 8) {
		/* "Mission N" */
		const char* label = textext_Get_Text(txtCombatMission);
		char fmt[32];
		strcpy(fmt, label);
		snprintf(buf, sizeof(buf), "%s %d", fmt, shipext_Get_Combat_Mission() + 1);
		xfont_Print_Centered_Text(buf, draw_rect, 1, color);
	}

	if (xinpattr_Is_Input_Dirty(input))
		xdirty_Dirty_Rect(clip_rect);
}

/* ------------------------------------------------------------------ */

// FUNCTION: TIE95 0x6D37C
// FUNCTION: TIE98 0x40AC00
static int16_t combat_iupdate_Combat_Screen(Input* input, Rect* draw_rect, Rect* clip_rect, int16_t active,
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
 * Monitor display mode state machine. Cycles 3 modes on click:
 *   0:        idle init (selected -> setup mode 0)
 *   1..255:   mission description (mode 0)
 *   256..383: score table (mode 1)
 *   384..638: 3D flyby movie + text overlay (mode 2)
 *   639:      stop movie + reset; advance combat_round (mod 4)
 */
// FUNCTION: TIE95 0x6D3A4
// FUNCTION: TIE98 0x40AC40
static void combat_iuser_Combat_Screen(Input* input, int32_t time) {
	(void)time;

	if (xinpattr_Get_Input_Selected(input)) {
		if (combat_time > 384)
			combat_time = 639;
		else if (combat_time > 256)
			combat_time = 384;
		else if (combat_time > 0)
			combat_time = 256;
	}

	if (combat_time == 0) {
		combat_time = 1;
		combat_mode = 0;
		combat_monitor_needs_clear = TIE_FRONTEND_EDITION(false, true);
		return;
	}

	if (combat_time == 256) {
		combat_time++;
		combat_mode = 1;
		combat_monitor_needs_clear = TIE_FRONTEND_EDITION(false, true);
		return;
	}

	if (combat_time == 384) {
		char matrix_name[16];
		bpflight_Start_Movie_Engine();
		snprintf(matrix_name, sizeof(matrix_name), "cmbtfly%d", combat_round + 1);
		bpflight_Open_New_Matrix(matrix_name);
		combat_mode = 2;
		combat_time++;
		return;
	}

	if (combat_time == 639) {
		bpflight_Stop_Movie_Engine();
		combat_time = 0;
		combat_round = (combat_round + 1) & 3;
		return;
	}

	combat_time++;
}

/* ------------------------------------------------------------------ */

/* Draw mission description on the combat monitor */
// FUNCTION: TIE95 0x6D52C
// FUNCTION: TIE98 0x40ADE0
static void combat_Draw_Combat_Screen_Mission(Rect* src) {
	int16_t t, num_lines, line_height, text_x, text_y, max_width, text_left;
	int16_t full_bar, half_bar, bar_step, header_x, header_w, bar_y;
	int16_t line_idx, text_line, i;
	uint16_t font_id;
	const char* mission_label;

	Rect dst;
	char string[48], buf[48], name[48];

	xrect_Copy_Rect(&dst, src);
	t = combat_time;
	num_lines = shipext_Num_Combat_Mission_Text_Lines();

	font_id = TIE_FRONTEND_EDITION(0, 2);
	line_height = TIE_FRONTEND_EDITION(10, (int16_t)xfont_Get_FontID_Height(font_id));
	dst.top += (dst.bottom - dst.top - (line_height * num_lines + 18)) >> 1;

	text_x = dst.left + TIE_FRONTEND_EDITION(2, 4);
	text_y = dst.top + 10;
	dst.bottom = dst.top + (TIE_FRONTEND_EDITION(18, 9 + line_height));

	max_width = 0;
	for (i = 0; i < num_lines; i++) {
		int16_t old_font, w;
		shipext_Get_Combat_Mission_Text(string, i);
		old_font = xfont_Get_Font();
		xfont_Set_Font(font_id);
		w = xfont_Get_String_Width(string);
		xfont_Set_Font(old_font);
		if (w > max_width)
			max_width = w;
	}
	text_left = ((dst.right - dst.left - max_width) >> 1) + text_x;
	full_bar = TIE_FRONTEND_EDITION(195, 390);
	half_bar = TIE_FRONTEND_EDITION(99, 198);
	bar_step = TIE_FRONTEND_EDITION(6, 12);

	/* Animated horizontal bars: header strip is framed by two parallel bars
	 * (top of header at dst.top+1, bottom of header at dst.bottom-1). Both
	 * grow outward from the centre as `t` ramps 0..16, then snap full at 195. */
	if (t >= 16) {
		header_x = dst.left + TIE_FRONTEND_EDITION(3, 6);
		header_w = full_bar;
	} else {
		header_x = dst.left + half_bar - bar_step * t;
		header_w = 2 * bar_step * t + 3;
	}
	xpaint_Horiz_Clipped_Line(header_x, dst.top + 1, header_w, 2);
	xpaint_Horiz_Clipped_Line(header_x, dst.bottom - 1, header_w, 2);

	bar_y = dst.bottom + line_height * num_lines + 2;
	if (t >= line_height * num_lines) {
		xpaint_Horiz_Clipped_Line(dst.left + TIE_FRONTEND_EDITION(3, 6), bar_y, full_bar, 2);
	} else {
		int16_t bt = t - (line_height * num_lines - 16);
		if (bt >= 0)
			xpaint_Horiz_Clipped_Line(dst.left + half_bar - bar_step * bt, bar_y, 2 * bar_step * bt + 3, 2);
	}

	/* Header + mission text lines */
	line_idx = 0;
	text_line = -1;
	mission_label = textext_Get_Text(txtCombatMission);
	while (t >= 0) {
		int16_t fade = (t <= 7) ? 2 * t + 16 : 31;

		if (line_idx == 0) {
			shipext_Get_Combat_Ship_Name(name);
			/* Header format: ship name, mission label, and one-based mission number. */
			snprintf(buf, sizeof(buf), "%s %s %d", name, mission_label, shipext_Get_Combat_Mission() + 1);
			xfont_Print_Centered_Text(buf, &dst, font_id, fade);
		} else {
			shipext_Get_Combat_Mission_Text(string, text_line);
			xfont_Print_Clipped_Text(string, text_left, text_y, font_id, fade);
		}

		t -= 8;
		line_idx++;
		text_y += line_height;
		text_line++;

		if (line_idx > num_lines)
			break;
	}
}

/* ------------------------------------------------------------------ */

/* Draw high score table on the combat monitor */
// FUNCTION: TIE95 0x6D888
// FUNCTION: TIE98 0x40B140
static void combat_Draw_Combat_Screen_Score(Rect* src) {
	const char* mission_name;
	int16_t mi, t, border, width, name_x, score_x, kills_x, y, displayed_scores, i;
	uint16_t font_id;
	GameScoreHead* rec;

	char fmt[40], string[40], name_buf[16];

	combat_Load_Combat_High_Scores();
	mission_name = shipext_Get_Mission_Name();
	strcpy(name_buf, mission_name);

	/* Find the mission record matching the current mission name */
	mi = 0;
	while (mi < combat_num_scores && combat_score_data[mi].name[0] &&
		   strcmp(combat_score_data[mi].name, name_buf))
		mi++;

	if (mi >= combat_num_scores || !combat_score_data[mi].name[0]) {
		textext_Copy_Text(string, txtCombatHighScore);
		xfont_Print_Clipped_Text(string, src->left + TIE_FRONTEND_EDITION(64, 128),
								 src->top + TIE_FRONTEND_EDITION(10, 24), TIE_FRONTEND_EDITION(0, 2), 31);
		return;
	}

	t = combat_time - 256;
	border = ((t >= 32) ? TIE_FRONTEND_EDITION(2, 4)
						: TIE_FRONTEND_EDITION(130, 260) - TIE_FRONTEND_EDITION(4, 8) * t);
	width = (src->right - src->left) - 2 * border;

	/* Horizontal bars */
	xpaint_Horiz_Clipped_Line(border + src->left, src->top + 6, width, 2);
	xpaint_Horiz_Clipped_Line(border + src->left, src->bottom - 6, width, 2);

	name_x = src->left + TIE_FRONTEND_EDITION(4, 8);
	score_x = src->left + TIE_FRONTEND_EDITION(64, 150);
	kills_x = src->left + TIE_FRONTEND_EDITION(144, 280);
	y = src->top + TIE_FRONTEND_EDITION(10, 24);
	font_id = TIE_FRONTEND_EDITION(0, 3);

	rec = &combat_score_data[mi];

	displayed_scores = TIE_FRONTEND_EDITION(8, GAME_SCORE_ENTRY_COUNT);
	for (i = 0; i < displayed_scores && t >= 0; i++) {
		int16_t fade = (t + 16 > 31) ? 31 : t + 16;
		char display_name[GAME_SCORE_NAME_CAPACITY];
		TiePilotName_CopyForDisplay(display_name, sizeof(display_name), rec->scores[i].name);

		if (display_name[0]) {
			xfont_Print_Clipped_Text(display_name, name_x, y, font_id, fade);
			textext_Copy_Text(fmt, txtCombatScore);
			snprintf(string, sizeof(string), fmt, rec->scores[i].score);
			xfont_Print_Clipped_Text(string, score_x, y, font_id, fade);
			textext_Copy_Text(fmt, txtCombatKills);
			snprintf(string, sizeof(string), fmt, (uint16_t)rec->scores[i].status);
			xfont_Print_Clipped_Text(string, kills_x, y, font_id, fade);
		}

		if (TIE_FRONTEND_TIE98)
			y += xfont_Get_FontID_Height(2) + 2;
		else
			y += 12;
		t -= 4;
	}
}

/* ------------------------------------------------------------------ */

/*
 * Draw ship info text overlaid on the 3D movie. Active during
 * combat_time 384..638 (movie phase). Two-step reveal:
 *   - Mission ship name (top of strip), fades in 16..47, hold,
 *     fades out at 576..607.
 *   - One blueprint description line (below name) per 32-tick window,
 *     window index = (t-64)>>5, only while 64<=t<192. Sub-fade based
 *     on (t & 0x1F): in 0..7, hold 8..23, out 24..31.
 *
 * Strip occupies the bottom 10 px of `src` (bottom-24 .. bottom-14).
 * Skips drawing entirely while fade==16 (edges of name window).
 */
// FUNCTION: TIE95 0x6DBB0
// FUNCTION: TIE98 0x40B480
static void combat_Draw_Combat_Screen_Flyby(Rect* src) {
	uint16_t font_id;
	int16_t fade, ship, old_bp;

	Rect dst;
	char str[48];
	int16_t t = combat_time - 384;

	xrect_Copy_Rect(&dst, src);
	dst.top = dst.bottom - TIE_FRONTEND_EDITION(24, 58);
	dst.bottom = dst.top + TIE_FRONTEND_EDITION(10, 24);
	font_id = TIE_FRONTEND_EDITION(0, 2);

	/* Name fade: hidden outside [16..207]; ramps in 16..47, hold 48..191,
	 * ramps out 192..207. */
	if (t < 32 || t >= 208)
		fade = 16;
	else if (t < 48)
		fade = combat_time - 400;
	else if (t >= 192)
		fade = (207 - t) + 16;
	else
		fade = 31;

	if (fade == 16)
		return;

	ship = shipext_Get_Mission_Ship();
	shipext_Get_Ship_Name(str, ship, 0, 0);
	xfont_Print_Centered_Text(str, &dst, font_id, fade);

	old_bp = shipext_Get_Blueprint_Ship();
	shipext_Set_Blueprint_Ship(ship);
	xrect_Offset_Rect(&dst, 0, xfont_Get_FontID_Height(font_id));

	if (t >= 64 && t < 192) {
		int16_t line_idx = (t - 64) >> 5;
		if (line_idx < shipext_Get_Num_Blueprint_Ship_Lines()) {
			int16_t sub_t = t & 0x1F;
			int16_t sub_fade;
			shipext_Get_Blueprint_Ship_Line(str, line_idx);
			if (sub_t < 8)
				sub_fade = 2 * sub_t + 16;
			else if (sub_t < 24)
				sub_fade = 31;
			else
				sub_fade = 2 * (31 - sub_t) + 16;
			xfont_Print_Centered_Text(str, &dst, font_id, sub_fade);
		}
	}

	shipext_Set_Blueprint_Ship(old_bp);
}

/* ------------------------------------------------------------------ */

// FUNCTION: TIE95 0x6D4CC
// FUNCTION: TIE98 0x40AD50
static void combat_idraw_Combat_Screen(Input* input, Rect* draw_rect, Rect* clip_rect, int16_t refresh) {
	if (!refresh || helmet->state)
		return;
	if (combat_monitor_needs_clear) {
		xpaint_Paint_Clipped_Rect(draw_rect, 0);
		combat_monitor_needs_clear = false;
	}

	switch (combat_mode) {
		case 0:
			combat_Draw_Combat_Screen_Mission(draw_rect);
			break;
		case 1:
			combat_Draw_Combat_Screen_Score(draw_rect);
			break;
		case 2:
			combat_Draw_Combat_Screen_Flyby(draw_rect);
			break;
	}

	if (xinpattr_Is_Input_Dirty(input))
		xdirty_Dirty_Rect(clip_rect);
}

/* ------------------------------------------------------------------ */

/*
 * Help tooltip overlay. Draws delta actor with text from
 * TIEText[combat_help + 84] when a button is hovered and visor is up.
 */
// FUNCTION: TIE95 0x6DCF4
// FUNCTION: TIE98 0x40B5E0
static int16_t combat_draw_Combat_Help(Actor* the_actor, Rect* draw_rect, Rect* clip_rect, int16_t off_x,
									   int16_t off_y, int16_t refresh) {

	if (refresh) {
		if (combat_help && !helmet->state) {
			Rect bounds;
			char text[32];
			xactdelt_Draw_Delta_Actor(the_actor, draw_rect, clip_rect, off_x, off_y, refresh);
			xactor_Get_Actor_Bounds(the_actor, &bounds);
			xfont_Enable_FontID_Shadow(0);
			textext_Copy_Text(text, (int16_t)(combat_help + 84));
			xfont_Print_Centered_Text(text, &bounds, TIE_FRONTEND_EDITION(0, 2), 15);
			xfont_Disable_FontID_Shadow(0);
		}
		combat_help = 0;
	}
	return 1;
}

/* ------------------------------------------------------------------ */

/* Flickering light — identical algorithm to TRAIN */
// FUNCTION: TIE95 0x6DD8C
// FUNCTION: TIE98 0x40B690
static void combat_user_Combat_Light(Actor* the_actor, int32_t time) {

	if (time == 0) {
		xactor_Show_Actor(the_actor);
		the_actor->var2 = (rand() & 0xF) + 2;
	}

	if (the_actor->var2 & 0x4000) {
		int16_t countdown;
		if (time & 1)
			the_actor->state = abs(rand()) % the_actor->arraySize;
		countdown = the_actor->var2 & 0x3FFF;
		if (countdown == 1) {
			the_actor->var2 = (rand() & 0xF) + 2;
			return;
		}
	} else {
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
 * Helmet visor animation.
 * var2==1: close visor → exit to scene 133 (COMBAT_MAP_A),
 *   calls Find_Mission_Ship, SFX.
 * var2==0 with scene COMBAT_B: auto-play visor opening.
 * Otherwise hide visor if visible.
 */
// FUNCTION: TIE95 0x6DE40
// FUNCTION: TIE98 0x40B740
static void combat_user_Combat_Helmet(Actor* the_actor, int32_t time) {
	if (the_actor->var2) {
		/* Entering combat — close visor */
		if (!xactor_Is_Actor_Visible(the_actor)) {
			xactor_Show_Actor(the_actor);
			xactor_Set_Actor_State(the_actor, 0, 0);
			soundext_Play_SFX(sfxVisor, 80);
		} else {
			int16_t next_state = the_actor->state + 1;
			if (next_state == the_actor->arraySize) {
				xerror_Set_Landru_Exit(SCENE_COMBAT_MAP_A);
				shipext_Find_Mission_Ship();
				soundext_Stop_SFX(sfxVisor);
				soundext_Play_SFX(sfxVisorClick, 80);
			} else {
				xactor_Set_Actor_State(the_actor, next_state, 0);
			}
		}
	} else {
		int16_t cur_scene = shellext_Get_Cur_Scene();
		if (time < the_actor->arraySize && cur_scene == SCENE_COMBAT_B) {
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

// FUNCTION: TIE95 0x6CB50
// FUNCTION: TIE98 0x40A320
int16_t combat_Combat(SceneHeadStruct* the_head) {
	Rect frame;
	char mission_name[64];
	int16_t i;

	xio_Set_Mouse_Position(TIE_FRONTEND_EDITION(268, 536), TIE_FRONTEND_EDITION(152, 354));

	combat_score_id = -1;
	combat_Load_Combat_High_Scores();

#ifdef TIE_MODERN
	train_file = TieProfile_UsesTie98Frontend() ? NULL : shellext_Open_Empire_Resource(combat_str[1]);
#elif !defined(TIE98)
	train_file = shellext_Open_Empire_Resource(combat_str[1]);
#endif
	combat_file = shellext_Open_Empire_Resource(combat_str[0]);
	xviewadd_Clear_View();
	xview_Disable_All_View_Erase();

	xrect_Set_Rect(&frame, 0, 0, TIE_FRONTEND_EDITION(320, 640), TIE_FRONTEND_EDITION(200, 480));
	combat_film = xfilm_Res_Callback_Film(combat_str[2], &frame, 0, 0, 0, combat_film_Combat_Callback);
	xfilm_Set_Film_Def_Palette(combat_film, the_head->def_palette);

#if defined(TIE98) && !defined(TIE_MODERN)
	{
		Actor* monitor_back;
		xrect_Set_Rect(&frame, 124, 7, 516, 272);
		monitor_back = xactcust_Alloc_Custom_Actor(LANDRU_NULL_HANDLE, &frame, 0, 0, 30);
		xactor_Set_Actor_Draw_Function(monitor_back, combat_draw_Combat_Back);
	}
#endif
	/* World input */
	xrect_Set_Rect(&frame, 0, 0, TIE_FRONTEND_EDITION(320, 640), TIE_FRONTEND_EDITION(200, 480));
	world_input = xinput_Alloc_Input(NULL, &frame, 0, 0);

	/* Monitor screen input */
	xrect_Set_Rect(&frame, TIE_FRONTEND_EDITION(59, 124), TIE_FRONTEND_EDITION(2, 7),
				   TIE_FRONTEND_EDITION(262, 516), TIE_FRONTEND_EDITION(115, 272));
	monitor_input = xinput_Alloc_Input(world_input, &frame, 0, 0);
	xinpattr_Set_Input_Update_Function(monitor_input, combat_iupdate_Combat_Screen);
	xinpattr_Set_Input_User_Function(monitor_input, combat_iuser_Combat_Screen);
	xinpattr_Set_Input_Draw_Function(monitor_input, combat_idraw_Combat_Screen);
	xinpattr_Refreshable_Input(monitor_input);
	monitor_input->id = 0;
#ifdef TIE_MODERN
	if (TieProfile_UsesTie98Frontend()) {
		Actor* monitor_back = xactcust_Alloc_Custom_Actor(LANDRU_NULL_HANDLE, &frame, 0, 0, 30);
		xactor_Set_Actor_Draw_Function(monitor_back, combat_draw_Combat_Back);
	}
#endif

	for (i = 0; i < 8; i++) {
		switch (i) {
			case 0:
				xrect_Set_Rect(&frame, TIE_FRONTEND_EDITION(32, 88), TIE_FRONTEND_EDITION(142, 336),
							   TIE_FRONTEND_EDITION(54, 136), TIE_FRONTEND_EDITION(156, 373));
				break;
			case 1:
				xrect_Set_Rect(&frame, TIE_FRONTEND_EDITION(124, 211), TIE_FRONTEND_EDITION(142, 336),
							   TIE_FRONTEND_EDITION(146, 255), TIE_FRONTEND_EDITION(156, 373));
				break;
			case 2:
				xrect_Set_Rect(&frame, TIE_FRONTEND_EDITION(22, 70), TIE_FRONTEND_EDITION(160, 370),
							   TIE_FRONTEND_EDITION(44, 120), TIE_FRONTEND_EDITION(174, 409));
				break;
			case 3:
				xrect_Set_Rect(&frame, TIE_FRONTEND_EDITION(124, 206), TIE_FRONTEND_EDITION(160, 370),
							   TIE_FRONTEND_EDITION(146, 250), TIE_FRONTEND_EDITION(174, 409));
				break;
			case 4:
				xrect_Set_Rect(&frame, TIE_FRONTEND_EDITION(254, 514), TIE_FRONTEND_EDITION(142, 337),
							   TIE_FRONTEND_EDITION(280, 577), TIE_FRONTEND_EDITION(158, 389));
				break;
			case 5:
				xrect_Set_Rect(&frame, TIE_FRONTEND_EDITION(10, 0), TIE_FRONTEND_EDITION(176, 406),
							   TIE_FRONTEND_EDITION(38, 72), TIE_FRONTEND_EDITION(200, 466));
				break;
			case 6:
				xrect_Set_Rect(&frame, TIE_FRONTEND_EDITION(55, 134), TIE_FRONTEND_EDITION(146, 344),
							   TIE_FRONTEND_EDITION(124, 209), TIE_FRONTEND_EDITION(156, 363));
				break;
			case 7:
				xrect_Set_Rect(&frame, TIE_FRONTEND_EDITION(47, 128), TIE_FRONTEND_EDITION(164, 378),
							   TIE_FRONTEND_EDITION(122, 201), TIE_FRONTEND_EDITION(176, 400));
				break;
		}
		button_input[i] = xinput_Alloc_Input(world_input, &frame, 0, 0);
		if (i <= 5) {
			button_input[i]->mouseUsage = 4;
			xinpattr_Set_Input_Update_Function(button_input[i], combat_iupdate_Combat);
			xinpattr_Set_Input_User_Function(button_input[i], combat_iuser_Combat);
		} else {
			xinpattr_Set_Input_Draw_Function(button_input[i], combat_idraw_Combat);
			xinpattr_Refreshable_Input(button_input[i]);
		}
		button_input[i]->id = i + 1;
	}

	combat_time = 0;
	combat_round = rand() & 3;
	combat_monitor_needs_clear = TIE_FRONTEND_EDITION(false, true);

	shipext_Get_Combat_Mission_Name(mission_name);
	shipext_Set_Mission_Name(mission_name);
	shipext_Find_Mission_Ship();

	bpflight_Open_Flight_Engine(2);
	bpflight_Stop_Movie_Engine();
	shipext_Show_Combat_Ship_Name();
	xview_Set_View_Update_Function(combat_end_Combat_View);
#ifdef TIE_MODERN
	TieCombat_RunView(combat_file, train_file, TieProfile_UsesTie98Frontend());
	return 0;
#else
	shellext_Handle_TIE_View();
	xinpcall_Clear_Active_Input();
	xview_Clear_View_Update_Function();
	bpflight_Close_Flight_Engine();
	xview_Enable_All_View_Erase();

	if (xcursor_Is_Cursor_Visible())
		xcursor_Hide_Cursor();

	xres_Close_Resource(combat_file);
#ifndef TIE98
	xres_Close_Resource(train_file);
#endif
	if (combat_score_data) {
		free(combat_score_data);
		combat_score_data = NULL;
	}
	return xerror_Get_Landru_Exit();
#endif
}
