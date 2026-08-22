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
#include "landru/memptr.h"
#include "landru/paint.h"
#include "landru/rect.h"
#include "landru/res.h"
#include "landru/surface.h"
#include "landru/vesa.h"
#include "landru/view.h"
#include "landru/viewadd.h"
#include "tie/combat.h"
#include "tie/rand.h"
#include "tie/shellext.h"
#include "tie/shipext.h"
#include "tie/soundext.h"
#include "tie/textext.h"
#include "tie_runtime/presentation/pilot_name.h"
#include "tie_runtime/runtime/profile.h"
#include "tie_runtime/storage/score_tables.h"
#include <landru/task.h>

#include "tie/bpflight.h"

static const char combat_resource_str[] = "combat.lfd";
static const char train_resource_str[] = "train.lfd";
static const char combat_film_name[] = "combat";

#define COMBAT_MAX_MISSIONS 8

/* Module state */
static ResFile* combat_file;
static ResFile* train_file;
static Film* combat_film;
static Input* world_input;
static Input* button_input[8];
static Input* monitor_input;
// GLOBAL: TIE 0xF5790
static Actor* arrow_actor;
// GLOBAL: TIE 0xF5780
static Actor* button[7];
// GLOBAL: TIE 0xF57A0; TIE98 0x50AA6C
static Actor* helmet;
// GLOBAL: TIE98 0x50AAA8
static int32_t combat_time;
// GLOBAL: TIE98 0x50AA58
static int32_t combat_mode;
static int32_t combat_round;
// GLOBAL: TIE 0xF5910
static int16_t combat_help;
static int16_t combat_num_scores;
static GameScoreHead* combat_score_data;
static int16_t combat_score_id;
static bool combat_svga;
// GLOBAL: TIE98 0x50AA68
static int32_t combat_monitor_needs_clear;

/* Forward declarations (referenced by film callback) */
static int combat_draw_Combat_Help(Actor* the_actor, Rect* draw_rect, Rect* clip_rect, int16_t off_x,
								   int16_t off_y, int16_t refresh);
static int combat_draw_Combat_Back(Actor* the_actor, Rect* draw_rect, Rect* clip_rect, int16_t off_x,
								   int16_t off_y, int16_t refresh);
static void combat_user_Combat_Light(Actor* the_actor, int32_t time);
static void combat_user_Combat_Helmet(Actor* the_actor, int32_t time);

/* ------------------------------------------------------------------ */

/*
 * Load combat high scores for the current ship/battle.
 * Generates filename: ship01-12.hgh or battle01+.hgh.
 * Caches by combat_score_id to avoid redundant loads.
 */
// FUNCTION: TIE 0x6DF54
static void combat_Load_Combat_High_Scores(void) {
	int16_t ship = shipext_Get_Combat_Ship();
	if (ship == combat_score_id)
		return;

	char filename[16];
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

// FUNCTION: TIE 0x6CE98
static void combat_end_Combat_View(int32_t time) {
	if (time == 0 && !lcursor_Is_Cursor_Visible())
		lcursor_Show_Cursor();
}

/* ------------------------------------------------------------------ */

// FUNCTION: TIE 0x6CEAC; TIE98 0x40A710
static int16_t combat_film_Combat_Callback(Film* the_film, FilmObject* film_object) {
	if (combat_svga && film_object->id == FTC_PALETTE) {
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
			lactor_Set_Actor_Draw_Function(the_actor, (lactorDrawFunc)combat_draw_Combat_Help);
			break;
		case 5:
			button[the_actor->var2] = the_actor;
			break;
		case 10:
			lactor_Set_Actor_User_Function(the_actor, combat_user_Combat_Light);
			return 0;
		case 12:
			lactor_Set_Actor_User_Function(the_actor, combat_user_Combat_Helmet);
			helmet = the_actor;
			return 0;
		case 15:
			arrow_actor = the_actor;
			return 0;
		case 20:
			lactor_Non_Refreshable_Actor(the_actor);
			break;
	}

	return 0;
}

/* ------------------------------------------------------------------ */

// FUNCTION: TIE98 0x40B870
static int combat_draw_Combat_Back(Actor* the_actor, Rect* draw_rect, Rect* clip_rect, int16_t off_x,
								   int16_t off_y, int16_t refresh) {
	(void)the_actor;
	(void)clip_rect;
	(void)off_x;
	(void)off_y;
	(void)refresh;
	if (combat_mode == 2)
		lpaint_Paint_Clipped_Rect(draw_rect, 0);
	return 1;
}

/* ------------------------------------------------------------------ */

// FUNCTION: TIE 0x6CF70; TIE98 0x40A800
static int16_t combat_iupdate_Combat(Input* input, Rect* draw_rect, Rect* clip_rect, int16_t active,
									 uint8_t mouseState, uint8_t prevMouseState, int16_t key,
									 int16_t prevKey) {
	(void)draw_rect;
	(void)clip_rect;
	(void)key;
	(void)prevKey;

	if (active)
		return 0;

	combat_help = input->id;

	if (!mouseState && !prevMouseState)
		return 1;

	int16_t id = input->id;
	/* TIE98 film actor indices are zero-based; combat input IDs are one-based. */
	const int16_t tie98_button_index = id - 1;

	if (id == 5) {
		/* Start button */
		if (mouseState == 3 || prevMouseState == 3) {
			lactor_Set_Actor_State(button[combat_svga ? tie98_button_index : 1], combat_svga ? 0 : 2, 0);
			linpattr_Selected_Input(input);
		} else {
			if (mouseState == 1 || prevMouseState == 1)
				soundext_Play_SFX(sfxButton, 80);
			lactor_Set_Actor_State(button[combat_svga ? tie98_button_index : 1], combat_svga ? 1 : 3, 0);
		}
	} else if (id == 6) {
		/* Exit door */
		if (mouseState == 3 || prevMouseState == 3) {
			lactor_Set_Actor_State(button[combat_svga ? tie98_button_index : 0], 0, 0);
			linpattr_Selected_Input(input);
		} else {
			if (mouseState == 1 || prevMouseState == 1)
				soundext_Play_SFX(sfxButton, 80);
			lactor_Set_Actor_State(button[combat_svga ? tie98_button_index : 0], 1, 0);
		}
	} else {
		/* Nav buttons 1-4 */
		if (mouseState == 3 || prevMouseState == 3) {
			linpattr_Clear_Input_Flag1(input);
			linpattr_Selected_Input(input);
			if (combat_svga)
				lactor_Set_Actor_State(button[tie98_button_index], 0, 0);
			else
				lactor_Hide_Actor(arrow_actor);
		}
		if (mouseState == 1 || prevMouseState == 1) {
			soundext_Play_SFX(sfxButton, 80);
			linpattr_Set_Input_Flag1(input);
			if (combat_svga)
				lactor_Set_Actor_State(button[tie98_button_index], 1, 0);
			else {
				lactor_Show_Actor(arrow_actor);
				lactor_Set_Actor_State(arrow_actor, 2 * (input->id - 1) + 1, 0);
			}
		}
	}
	return 1;
}

/* ------------------------------------------------------------------ */

// FUNCTION: TIE 0x6D0E8; TIE98 0x40A970
static void combat_iuser_Combat(Input* input, int32_t time) {
	/* Pressure door SFX on button 5 at entry scene A with time == 4 */
	if (input->id == 5 && time == 4 && shellext_Get_Cur_Scene() == SCENE_COMBAT_A)
		soundext_Play_SFX(sfxPressureDoor, 64);

	if (!linpattr_Get_Input_Selected(input) || helmet->var2)
		return;

	switch (input->id) {
		case 1:
			shipext_Last_Combat_Ship();
			combat_time = (combat_time >= 384) ? 0 : 128;
			bpflight_Stop_Movie_Engine();
			combat_monitor_needs_clear = combat_svga;
			break;
		case 2:
			shipext_Next_Combat_Ship();
			combat_time = (combat_time >= 256) ? 0 : 128;
			bpflight_Stop_Movie_Engine();
			combat_monitor_needs_clear = combat_svga;
			break;
		case 3:
			shipext_Last_Combat_Mission();
			combat_time = (combat_time >= 256) ? 0 : 128;
			bpflight_Stop_Movie_Engine();
			combat_monitor_needs_clear = combat_svga;
			break;
		case 4:
			shipext_Next_Combat_Mission();
			combat_time = (combat_time >= 256) ? 0 : 128;
			bpflight_Stop_Movie_Engine();
			combat_monitor_needs_clear = combat_svga;
			break;
		case 5:
			if (!helmet->var2)
				helmet->var2 = 1;
			break;
		case 6:
			lerror_Set_Landru_Exit(SCENE_MAIN_MENU);
			break;
	}
}

/* ------------------------------------------------------------------ */

// FUNCTION: TIE 0x6D260; TIE98 0x40AAD0
static void combat_idraw_Combat(Input* input, Rect* draw_rect, Rect* clip_rect, int16_t refresh) {
	char buf[48];

	if (!refresh)
		return;

	int16_t color;
	if (helmet->state >= 5)
		color = 16;
	else
		color = 31 - 3 * helmet->state;

	if (color == 16)
		return;

	int16_t id = input->id;

	if (id == 7) {
		/* Ship name (truncated at parenthesis) */
		shipext_Get_Combat_Ship_Name(buf);
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
	} else if (id == 8) {
		/* "Mission N" */
		const char* label = textext_Get_Text(txtCombatMission);
		char fmt[32];
		strcpy(fmt, label);
		snprintf(buf, sizeof(buf), "%s %d", fmt, shipext_Get_Combat_Mission() + 1);
		lfont_Print_Centered_Text(buf, draw_rect, color, 1);
	}

	if (linpattr_Is_Input_Dirty(input))
		ldirty_Dirty_Rect(clip_rect);
}

/* ------------------------------------------------------------------ */

// FUNCTION: TIE 0x6D37C; TIE98 0x40AC00
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
		linpattr_Selected_Input(input);
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
// FUNCTION: TIE 0x6D3A4; TIE98 0x40AC40
static void combat_iuser_Combat_Screen(Input* input, int32_t time) {
	(void)time;

	if (linpattr_Get_Input_Selected(input)) {
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
		combat_monitor_needs_clear = combat_svga;
		return;
	}

	if (combat_time == 256) {
		combat_time++;
		combat_mode = 1;
		combat_monitor_needs_clear = combat_svga;
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
// FUNCTION: TIE 0x6D52C; TIE98 0x40ADE0
static void combat_Draw_Combat_Screen_Mission(Rect* src) {
	Rect dst;
	char string[48], buf[48], name[48];

	lrect_Copy_Rect(&dst, src);
	int16_t t = combat_time;
	int16_t num_lines = shipext_Num_Combat_Mission_Text_Lines();

	const uint16_t font_id = combat_svga ? 2 : 0;
	const int16_t line_height = lfont_Get_FontID_Height(font_id);
	dst.top += (dst.bottom - dst.top - (line_height * num_lines + 18)) >> 1;

	int16_t text_x = dst.left + (combat_svga ? 4 : 2);
	int16_t text_y = dst.top + line_height;
	dst.bottom = dst.top + (combat_svga ? 9 + line_height : 18);

	int16_t max_width = 0;
	for (int16_t i = 0; i < num_lines; i++) {
		shipext_Get_Combat_Mission_Text(string, i);
		int16_t old_font = lfont_Get_Font();
		lfont_Set_Font(font_id);
		int16_t w = lfont_Get_String_Width(string);
		lfont_Set_Font(old_font);
		if (w > max_width)
			max_width = w;
	}
	int16_t text_left = ((dst.right - dst.left - max_width) >> 1) + text_x;
	const int16_t full_bar = combat_svga ? 390 : 195;
	const int16_t half_bar = combat_svga ? 198 : 99;
	const int16_t bar_step = combat_svga ? 12 : 6;

	/* Animated horizontal bars: header strip is framed by two parallel bars
	 * (top of header at dst.top+1, bottom of header at dst.bottom-1). Both
	 * grow outward from the centre as `t` ramps 0..16, then snap full at 195. */
	int16_t header_x, header_w;
	if (t >= 16) {
		header_x = dst.left + (combat_svga ? 6 : 3);
		header_w = full_bar;
	} else {
		header_x = dst.left + half_bar - bar_step * t;
		header_w = 2 * bar_step * t + 3;
	}
	lpaint_Horiz_Clipped_Line(header_x, dst.top + 1, header_w, 2);
	lpaint_Horiz_Clipped_Line(header_x, dst.bottom - 1, header_w, 2);

	int16_t bar_y = dst.bottom + line_height * num_lines + 2;
	if (t >= line_height * num_lines) {
		lpaint_Horiz_Clipped_Line(dst.left + (combat_svga ? 6 : 3), bar_y, full_bar, 2);
	} else {
		int16_t bt = t - (line_height * num_lines - 16);
		if (bt >= 0)
			lpaint_Horiz_Clipped_Line(dst.left + half_bar - bar_step * bt, bar_y, 2 * bar_step * bt + 3, 2);
	}

	/* Header + mission text lines */
	int16_t line_idx = 0;
	int16_t text_line = -1;
	const char* mission_label = textext_Get_Text(txtCombatMission);
	while (t >= 0) {
		int16_t fade = (t <= 7) ? 2 * t + 16 : 31;

		if (line_idx == 0) {
			shipext_Get_Combat_Ship_Name(name);
			/* Header format: ship name, mission label, and one-based mission number. */
			snprintf(buf, sizeof(buf), "%s %s %d", name, mission_label, shipext_Get_Combat_Mission() + 1);
			lfont_Print_Centered_Text(buf, &dst, fade, font_id);
		} else {
			shipext_Get_Combat_Mission_Text(string, text_line);
			lfont_Print_Clipped_Text(string, text_left, text_y, font_id, fade);
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
// FUNCTION: TIE 0x6D888; TIE98 0x40B140
static void combat_Draw_Combat_Screen_Score(Rect* src) {
	char fmt[40], string[40], name_buf[16];

	combat_Load_Combat_High_Scores();
	const char* mission_name = shipext_Get_Mission_Name();
	strcpy(name_buf, mission_name);

	/* Find the mission record matching the current mission name */
	int16_t mi = 0;
	while (mi < combat_num_scores && combat_score_data[mi].name[0] &&
		   strcmp(combat_score_data[mi].name, name_buf))
		mi++;

	if (mi >= combat_num_scores || !combat_score_data[mi].name[0]) {
		textext_Copy_Text(string, txtCombatHighScore);
		lfont_Print_Clipped_Text(string, src->left + (combat_svga ? 128 : 64),
								 src->top + (combat_svga ? 24 : 10), combat_svga ? 2 : 0, 31);
		return;
	}

	int16_t t = combat_time - 256;
	int16_t border = combat_svga ? ((t >= 32) ? 4 : 260 - 8 * t) : ((t >= 32) ? 2 : 130 - 4 * t);
	int16_t width = (src->right - src->left) - 2 * border;

	/* Horizontal bars */
	lpaint_Horiz_Clipped_Line(border + src->left, src->top + 6, width, 2);
	lpaint_Horiz_Clipped_Line(border + src->left, src->bottom - 6, width, 2);

	int16_t name_x = src->left + (combat_svga ? 8 : 4);
	int16_t score_x = src->left + (combat_svga ? 150 : 64);
	int16_t kills_x = src->left + (combat_svga ? 280 : 144);
	int16_t y = src->top + (combat_svga ? 24 : 10);
	const uint16_t font_id = combat_svga ? 3 : 0;

	GameScoreHead* rec = &combat_score_data[mi];

	const int16_t displayed_scores = combat_svga ? GAME_SCORE_ENTRY_COUNT : 8;
	for (int16_t i = 0; i < displayed_scores && t >= 0; i++) {
		int16_t fade = (t + 16 > 31) ? 31 : t + 16;
		char display_name[GAME_SCORE_NAME_CAPACITY];
		TiePilotName_CopyForDisplay(display_name, sizeof(display_name), rec->scores[i].name);

		if (display_name[0]) {
			lfont_Print_Clipped_Text(display_name, name_x, y, font_id, fade);
			textext_Copy_Text(fmt, txtCombatScore);
			snprintf(string, sizeof(string), fmt, rec->scores[i].score);
			lfont_Print_Clipped_Text(string, score_x, y, font_id, fade);
			textext_Copy_Text(fmt, txtCombatKills);
			snprintf(string, sizeof(string), fmt, (uint16_t)rec->scores[i].status);
			lfont_Print_Clipped_Text(string, kills_x, y, font_id, fade);
		}

		y += combat_svga ? lfont_Get_FontID_Height(2) + 2 : 12;
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
// FUNCTION: TIE 0x6DBB0; TIE98 0x40B480
static void combat_Draw_Combat_Screen_Flyby(Rect* src) {
	Rect dst;
	char str[48];
	int16_t t = combat_time - 384;

	lrect_Copy_Rect(&dst, src);
	dst.top = dst.bottom - (combat_svga ? 58 : 24);
	dst.bottom = dst.top + (combat_svga ? 24 : 10);
	const uint16_t font_id = combat_svga ? 2 : 0;

	/* Name fade: hidden outside [16..207]; ramps in 16..47, hold 48..191,
	 * ramps out 192..207. */
	int16_t fade;
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

	int16_t ship = shipext_Get_Mission_Ship();
	shipext_Get_Ship_Name(str, ship, 0, 0);
	lfont_Print_Centered_Text(str, &dst, fade, font_id);

	int16_t old_bp = shipext_Get_Blueprint_Ship();
	shipext_Set_Blueprint_Ship(ship);
	lrect_Offset_Rect(&dst, 0, lfont_Get_FontID_Height(font_id));

	if (t >= 64 && t < 192) {
		int16_t line_idx = (t - 64) >> 5;
		if (line_idx < shipext_Get_Num_Blueprint_Ship_Lines()) {
			int16_t sub_t = t & 0x1F;
			shipext_Get_Blueprint_Ship_Line(str, line_idx);
			int16_t sub_fade;
			if (sub_t < 8)
				sub_fade = 2 * sub_t + 16;
			else if (sub_t < 24)
				sub_fade = 31;
			else
				sub_fade = 2 * (31 - sub_t) + 16;
			lfont_Print_Centered_Text(str, &dst, sub_fade, font_id);
		}
	}

	shipext_Set_Blueprint_Ship(old_bp);
}

/* ------------------------------------------------------------------ */

// FUNCTION: TIE 0x6D4CC; TIE98 0x40AD50
static void combat_idraw_Combat_Screen(Input* input, Rect* draw_rect, Rect* clip_rect, int16_t refresh) {
	if (!refresh || helmet->state)
		return;
	if (combat_monitor_needs_clear) {
		lpaint_Paint_Clipped_Rect(draw_rect, 0);
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

	if (linpattr_Is_Input_Dirty(input))
		ldirty_Dirty_Rect(clip_rect);
}

/* ------------------------------------------------------------------ */

/*
 * Help tooltip overlay. Draws delta actor with text from
 * TIEText[combat_help + 84] when a button is hovered and visor is up.
 */
// FUNCTION: TIE 0x6DCF4; TIE98 0x40B5E0
static int combat_draw_Combat_Help(Actor* the_actor, Rect* draw_rect, Rect* clip_rect, int16_t off_x,
								   int16_t off_y, int16_t refresh) {
	if (refresh) {
		if (combat_help && !helmet->state) {
			lactdelt_Draw_Delta_Actor(the_actor, draw_rect, clip_rect, off_x, off_y, refresh);
			Rect bounds;
			lactor_Get_Actor_Bounds(the_actor, &bounds);
			lfont_Enable_FontID_Shadow(0);
			char text[32];
			textext_Copy_Text(text, (int16_t)(combat_help + 84));
			lfont_Print_Centered_Text(text, &bounds, 15, combat_svga ? 2 : 0);
			lfont_Disable_FontID_Shadow(0);
		}
		combat_help = 0;
	}
	return 1;
}

/* ------------------------------------------------------------------ */

/* Flickering light — identical algorithm to TRAIN */
// FUNCTION: TIE 0x6DD8C; TIE98 0x40B690
static void combat_user_Combat_Light(Actor* the_actor, int32_t time) {
	if (time == 0) {
		lactor_Show_Actor(the_actor);
		the_actor->var2 = (rand_rand() & 0xF) + 2;
	}

	if (the_actor->var2 & 0x4000) {
		if (time & 1)
			the_actor->state = rand_rand() % the_actor->arraySize;
		int16_t countdown = the_actor->var2 & 0x3FFF;
		if (countdown == 1) {
			the_actor->var2 = (rand_rand() & 0xF) + 2;
			return;
		}
	} else {
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
 * Helmet visor animation.
 * var2==1: close visor → exit to scene 133 (COMBAT_MAP_A),
 *   calls Find_Mission_Ship, SFX.
 * var2==0 with scene COMBAT_B: auto-play visor opening.
 * Otherwise hide visor if visible.
 */
// FUNCTION: TIE 0x6DE40; TIE98 0x40B740
static void combat_user_Combat_Helmet(Actor* the_actor, int32_t time) {
	if (the_actor->var2) {
		/* Entering combat — close visor */
		if (!lactor_Is_Actor_Visible(the_actor)) {
			lactor_Show_Actor(the_actor);
			lactor_Set_Actor_State(the_actor, 0, 0);
			soundext_Play_SFX(sfxVisor, 80);
		} else {
			int16_t next_state = the_actor->state + 1;
			if (next_state == the_actor->arraySize) {
				lerror_Set_Landru_Exit(SCENE_COMBAT_MAP_A);
				shipext_Find_Mission_Ship();
				soundext_Stop_SFX(sfxVisor);
				soundext_Play_SFX(sfxVisorClick, 80);
			} else {
				lactor_Set_Actor_State(the_actor, next_state, 0);
			}
		}
	} else {
		int16_t cur_scene = shellext_Get_Cur_Scene();
		if (time < the_actor->arraySize && cur_scene == SCENE_COMBAT_B) {
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

typedef enum {
	COMBAT_PHASE_BEGIN = 0,
	COMBAT_PHASE_CLEANUP = 1,
} CombatPhase;

typedef struct CombatTask {
	SceneHeadStruct* the_head;
	CombatPhase phase;
} CombatTask;

static LandruTaskStepResult combat_task_step(void* self) {
	CombatTask* t = (CombatTask*)self;

	if (t->phase == COMBAT_PHASE_BEGIN) {
		Rect frame;
		char mission_name[64];
		combat_svga = TieProfile_FrontendId() == TIE_FRONTEND_PROFILE_TIE98;
		const int16_t width = combat_svga ? 640 : 320;
		const int16_t height = combat_svga ? 480 : 200;
		if (combat_svga) {
			(void)lsurface_Select_Surface_Set(LANDRU_SURFACE_SVGA);
			lview_Init_View(lview_Get_Current_View());
			lvesa_Erase_Video(16);
		}

		lio_Set_Mouse_Position(combat_svga ? 536 : 268, combat_svga ? 354 : 152);

		combat_score_id = -1;
		combat_Load_Combat_High_Scores();

		train_file = combat_svga ? NULL : shellext_Open_Empire_Resource(train_resource_str);
		combat_file = shellext_Open_Empire_Resource(combat_resource_str);
		lviewadd_Clear_View();
		lview_Disable_All_View_Erase();

		lrect_Set_Rect(&frame, 0, 0, width, height);
		combat_film = lfilm_Res_Callback_Film(combat_film_name, &frame, 0, 0, 0, combat_film_Combat_Callback);
		lfilm_Set_Film_Def_Palette(combat_film, t->the_head->def_palette);

		/* World input */
		lrect_Set_Rect(&frame, 0, 0, width, height);
		world_input = linput_Alloc_Input(NULL, &frame, 0, 0);

		/* Monitor screen input */
		if (combat_svga)
			lrect_Set_Rect(&frame, 124, 7, 516, 272);
		else
			lrect_Set_Rect(&frame, 59, 2, 262, 115);
		monitor_input = linput_Alloc_Input(world_input, &frame, 0, 0);
		linpattr_Set_Input_Update_Function(monitor_input, combat_iupdate_Combat_Screen);
		linpattr_Set_Input_User_Function(monitor_input, combat_iuser_Combat_Screen);
		linpattr_Set_Input_Draw_Function(monitor_input, combat_idraw_Combat_Screen);
		linpattr_Refreshable_Input(monitor_input);
		monitor_input->id = 0;
		if (combat_svga) {
			Actor* monitor_back = lactcust_Alloc_Custom_Actor(NULL, &frame, 0, 0, 30);
			lactor_Set_Actor_Draw_Function(monitor_back, combat_draw_Combat_Back);
		}

		/* Six navigation buttons followed by the ship and mission labels. */
		Rect btn_rects[8];
		if (combat_svga) {
			lrect_Set_Rect(&btn_rects[0], 88, 336, 136, 373);
			lrect_Set_Rect(&btn_rects[1], 211, 336, 255, 373);
			lrect_Set_Rect(&btn_rects[2], 70, 370, 120, 409);
			lrect_Set_Rect(&btn_rects[3], 206, 370, 250, 409);
			lrect_Set_Rect(&btn_rects[4], 514, 337, 577, 389);
			lrect_Set_Rect(&btn_rects[5], 0, 406, 72, 466);
			lrect_Set_Rect(&btn_rects[6], 134, 344, 209, 363);
			lrect_Set_Rect(&btn_rects[7], 128, 378, 201, 400);
		} else {
			lrect_Set_Rect(&btn_rects[0], 32, 142, 54, 156);
			lrect_Set_Rect(&btn_rects[1], 124, 142, 146, 156);
			lrect_Set_Rect(&btn_rects[2], 22, 160, 44, 174);
			lrect_Set_Rect(&btn_rects[3], 124, 160, 146, 174);
			lrect_Set_Rect(&btn_rects[4], 254, 142, 280, 158);
			lrect_Set_Rect(&btn_rects[5], 10, 176, 38, 200);
			lrect_Set_Rect(&btn_rects[6], 55, 146, 124, 156);
			lrect_Set_Rect(&btn_rects[7], 47, 164, 122, 176);
		}
		for (int16_t i = 0; i < 8; i++) {
			button_input[i] = linput_Alloc_Input(world_input, &btn_rects[i], 0, 0);
			if (i <= 5) {
				button_input[i]->mouseUsage = 4;
				linpattr_Set_Input_Update_Function(button_input[i], combat_iupdate_Combat);
				linpattr_Set_Input_User_Function(button_input[i], combat_iuser_Combat);
			} else {
				linpattr_Set_Input_Draw_Function(button_input[i], combat_idraw_Combat);
				linpattr_Refreshable_Input(button_input[i]);
			}
			button_input[i]->id = i + 1;
		}

		combat_time = 0;
		combat_round = rand_rand() & 3;
		combat_monitor_needs_clear = combat_svga;

		shipext_Get_Combat_Mission_Name(mission_name);
		shipext_Set_Mission_Name(mission_name);
		shipext_Find_Mission_Ship();

		bpflight_Open_Flight_Engine(2);
		bpflight_Stop_Movie_Engine();
		shipext_Show_Combat_Ship_Name();

		/* Push the modal view task */
		lview_Set_View_Update_Function(combat_end_Combat_View);
		lviewadd_Push_Handle_View_Task();

		t->phase = COMBAT_PHASE_CLEANUP;
		return LANDRU_TASK_STEP_CONTINUE;
	}

	/* CLEANUP */
	linpcall_Clear_Active_Input();
	lview_Clear_View_Update_Function();
	bpflight_Close_Flight_Engine();
	lview_Enable_All_View_Erase();

	if (lcursor_Is_Cursor_Visible())
		lcursor_Hide_Cursor();

	lres_Close_Resource(combat_file);
	if (train_file)
		lres_Close_Resource(train_file);
	if (combat_score_data) {
		free(combat_score_data);
		combat_score_data = NULL;
	}
	if (combat_svga) {
		lvesa_Erase_Video(16);
		lviewadd_Clear_View();
		(void)lsurface_Select_Surface_Set(LANDRU_SURFACE_VGA);
	}

	return LANDRU_TASK_STEP_DONE;
}

static const LandruTaskVtable combat_task_vt = {
	.step = combat_task_step,
};

void combat_Push_Combat_Task(SceneHeadStruct* the_head) {
	CombatTask* t = (CombatTask*)landru_task_push(&combat_task_vt);
	if (!t)
		return;
	t->the_head = the_head;
	t->phase = COMBAT_PHASE_BEGIN;
}
