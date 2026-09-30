#include "tie/option.h"
#include "tie/feinput.h"
#include "tie/festring.h"
#include "tie/rtsvga2.h"
#include "tie/tie.h"
#include "tie/user.h"

#include <string.h>

/* In-flight audio and gameplay settings persisted across missions. */
// GLOBAL: TIE95 0xc1550
// GLOBAL: TIE98 0x4f4118
int8_t inflight_music_vol;
// GLOBAL: TIE95 0xc1551
// GLOBAL: TIE98 0x4f411c
int8_t inflight_sound_vol;
// GLOBAL: TIE95 0xc1552
// GLOBAL: TIE98 0x4f4120
int8_t inflight_speech_vol;
// GLOBAL: TIE95 0xc1553
// GLOBAL: TIE98 0x58ac48
int8_t inflight_unlimited;
// GLOBAL: TIE95 0xc1554
// GLOBAL: TIE98 0x58ac4c
int8_t inflight_invulnerable;
// GLOBAL: TIE95 0xc1555
// GLOBAL: TIE98 0x4f4124
int8_t inflight_collision;

/* --- Module-owned globals (watdbg: option.c) --------------------------- */

/*
 * Per-row background-band colour. Two palette bands:
 *   0x50 (80) - graphics/detail section
 *   0x48 (72) - gameplay + audio section
 * The row-colour scan drawn before the main loop paints a rectangle per
 * run of equal colour so the banded visual pattern survives even when
 * the highlight moves across row boundaries.
 */
// GLOBAL: TIE95 0xc588c
static const uint8_t option_color[14] = {
	0x50, 0x50, 0x50, 0x50, 0x48, 0x48, 0x48, 0x48, 0x50, 0x50, 0x50, 0x48, 0x48, 0x48,
};

/*
 * optionTop / optionBottom are module-global in watdbg. Source-level they
 * drive the row-colour-band render and the top-of-text cursor; no one
 * outside OPTION reads them, so keep them file-static.
 *
 * Layout per flightResolution at entry:
 *   0x13    (320x200 VGA)  -> top=21,  bottom=182
 *   0x101   (640x480 VESA) -> top=51,  bottom=436
 *   fallback               -> top=21,  bottom=182
 */
// GLOBAL: TIE95 0xd5048
// GLOBAL: TIE98 0x5fce48
static int32_t option_top;
// GLOBAL: TIE95 0xd5054
// GLOBAL: TIE98 0x5fce40
static int32_t option_bottom;

/*
 * Filled by fediskio_loadstringdata — point into the relocated
 * stringdata_buf. optionstrings has 14 entries (one label per row);
 * settingstrings has 16 slots (15 used). See kind_offsets[] below for
 * which range each row indexes.
 */
// GLOBAL: TIE95 0xD504C
// GLOBAL: TIE98 0x5FCE44
char** optionstrings;
// GLOBAL: TIE95 0xD5050
// GLOBAL: TIE98 0x5FCE4C
char** settingstrings;

/* --- Local constants --------------------------------------------------- */

enum {
	BG_DEFAULT = 0x44,
	TEXT_COLOUR = 0x43,
	CURSOR_COLOUR = 0x46,
	VOLUME_KIND = 15,  /* kind_offsets[] marker for volume rows */
	VOLUME_CELLS = 16, /* width of a volume bar, in half-font cells */
};

/* --- Static: volume bar ------------------------------------------------ */

/*
 * Render a 16-cell volume bar for one option row. The track is a clear
 * background (0x40) spanning 16 half-font-height cells right-aligned to
 * (screenXRes - fontheight/2). The first `vol` cells are then over-filled
 * with the highlight colour (0x53). Restores the default text bound
 * before returning so callers don't need to.
 */
// FUNCTION: TIE95 0x35504
// FUNCTION: TIE98 0x458BE0
static void out_volume_bar(uint16_t vol, int16_t y) {
	const int half_fh = (int)(int8_t)fontheight >> 1;
	const int16_t left = (int16_t)((int)screenXRes - 17 * half_fh);
	uint16_t cell;

	festring_setbound((int16_t)(left - 1), (int16_t)(y + 1), (int16_t)((int)screenXRes - half_fh),
					  (int16_t)(y + fontheight - 1));
	festring_setbackcolor(0x40);
	clearwindow();

	for (cell = 0; cell < vol; cell++) {
		festring_setbound((int16_t)(left + cell * half_fh), (int16_t)(y + 2),
						  (int16_t)(left + (cell + 1) * half_fh - 1), (int16_t)(y + fontheight - 2));
		festring_setbackcolor(0x53);
		clearwindow();
	}

	festring_setbound(2, 0, (int16_t)((int)screenXRes - 2), (int16_t)screenYRes);
}

/* --- Static helpers for the main routine ------------------------------ */

/*
 * Pick the layout-specific top/bottom pixels. Mirrors the three-branch
 * block at the head of OPTION_optionsroom.
 */
static void pick_layout(void) {
	if (flightResolution == TIE_FLIGHT_RES_VGA) {
		option_top = 21;
		option_bottom = 182;
	} else if (tie_is_high_resolution_flight()) {
		option_top = 51;
		option_bottom = 436;
	} else {
		option_top = 21;
		option_bottom = 182;
	}
}

/*
 * Initial row-colour-band render. Walks option_color[] and flushes a
 * filled rectangle for each run of equal colour. The final rectangle
 * extends from the last colour change down to option_bottom.
 */
static void paint_row_bands(void) {
	const int row_step = fontheight + 2;
	const int16_t right = (int16_t)((int)screenXRes - 2);

	int16_t run_top = (int16_t)option_top;
	int16_t y = run_top;
	uint16_t color = option_color[0];
	int i;

	for (i = 0; i < OPTION_ROW_COUNT; i++) {
		if (option_color[i] != color) {
			festring_setbound(2, (int16_t)(run_top - 1), right, (int16_t)(y - 1));
			festring_setbackcolor(color);
			clearwindow();
			y = (int16_t)(y + 2);
			run_top = y;
			color = option_color[i];
		}
		y = (int16_t)(y + row_step);
	}

	festring_setbound(2, (int16_t)(run_top - 1), right, (int16_t)option_bottom);
	festring_setbackcolor(color);
	clearwindow();
}

/* Draw only changed rows after the initial full render. */
void option_RenderRows(OptionRoomState* t) {
	int16_t cursor_y = (int16_t)option_top;
	uint8_t prev_color = option_color[0];
	int16_t row;

	festring_setbound(2, 0, (int16_t)(screenXRes - 2), (int16_t)screenYRes);
	for (row = 0; row < OPTION_ROW_COUNT; row++) {
		const uint16_t bg = row == t->selection ? CURSOR_COLOUR : option_color[row];
		if (option_color[row] != prev_color) {
			prev_color = option_color[row];
			cursor_y = (int16_t)(cursor_y + 2);
		}
		festring_setbackcolor(bg);
		if (t->redraw_all || row == t->selection || row == t->previous_selection) {
			const int kind = t->kind_offsets[row];
			festring_setcursor(2, cursor_y);
			festring_outstring((const uint8_t*)optionstrings[row]);
			outchar('\n');
			festring_setcursor(0, cursor_y);
			if (kind == VOLUME_KIND)
				out_volume_bar(t->values[row], cursor_y);
			else
				festring_outstringright((const uint8_t*)settingstrings[kind + t->values[row]]);
		}
		cursor_y = (int16_t)(cursor_y + fontheight + 2);
	}
	t->previous_selection = t->selection;
	t->redraw_all = 0;
}

/*
 * Cycle the current row's value forward with wrap.
 *   v == max -> 0
 *   else        -> v + 1
 */
static void cycle_forward(uint8_t* values, const uint8_t* max_values, int16_t selection) {
	const uint8_t cur = values[selection];
	values[selection] = (cur == max_values[selection]) ? 0 : (uint8_t)(cur + 1);
}

/*
 * Cycle the current row's value backward with wrap.
 *   v == 0 -> max
 *   else   -> v - 1
 */
static void cycle_backward(uint8_t* values, const uint8_t* max_values, int16_t selection) {
	const uint8_t cur = values[selection];
	values[selection] = cur ? (uint8_t)(cur - 1) : max_values[selection];
}

static void apply_visual_to_globals(const uint8_t* values) {
	gouraudflag = (uint8_t)(values[0] << 6);
	shipdetailvalue = (int16_t)(1 - values[1]);
	shipdetailpolycnt = (uint16_t)(4 * values[1] + 8);
	starshipdetail = (uint16_t)(values[2] + 1);
	starshipexplodetail = (uint16_t)(((uint32_t)4096 << values[1]) - 1);
	drawmarkingsflag = values[3];
	drawbackdropflag = values[4];
	drawdebrisflag = values[5];
	palette_cycle_user = values[6];
	stardetaillevel = (uint16_t)(2 - values[7]);
	hyperspacedetail = (int16_t)(75 - 25 * (2 - values[7]));
}

void option_ApplyFlightValues(const uint8_t* values) {
	inflight_collision = (int8_t)values[8];
	inflight_invulnerable = (int8_t)values[9];
	inflight_unlimited = (int8_t)values[10];
	inflight_sound_vol = (int8_t)values[11];
	inflight_music_vol = (int8_t)values[12];
	inflight_speech_vol = (int8_t)values[13];

	soundvolflag = (uint8_t)(inflight_speech_vol + inflight_sound_vol);
	musicvolflag = (uint8_t)inflight_music_vol;
	cheatingflag = (uint8_t)(cheatingflag | (uint8_t)inflight_invulnerable | (uint8_t)inflight_unlimited);
}

/* Apply the game-state portion of the fourteen DOS options. */
void option_ApplyValues(const uint8_t* values) {
	apply_visual_to_globals(values);
	option_ApplyFlightValues(values);
}

/* Single input-poll iteration. 1 = exit_after_write fires (caller
 * persists + applies + DONE), 2 = redraw needed, 0 = no input. */
int option_PollOnce(OptionRoomState* t) {
	uint16_t key;
	uint16_t cur_buttons;
	int redraw = 0;
	int exit_room = 0;

	feinput_getrawinput();
	feinput_checkinput();
	feinput_degitterinput();
	inputdeltay = (int16_t)(inputdeltay * 2);

	key = (uint16_t)inputkey;

	if (key == 1) {
		/* LEFT */
		if (t->kind_offsets[t->selection] == VOLUME_KIND) {
			if (t->values[t->selection])
				t->values[t->selection]--;
			redraw = 1;
		} else {
			t->exit_code = -1;
			exit_room = 1;
		}
	} else if (key == 2) {
		/* RIGHT */
		if (t->kind_offsets[t->selection] == VOLUME_KIND) {
			if (t->values[t->selection] < t->max_values[t->selection])
				t->values[t->selection]++;
			redraw = 1;
		} else {
			t->exit_code = 1;
			exit_room = 1;
		}
	} else if (key == 3 || key == 0x38 /* '8' */) {
		/* UP */
		t->selection = t->selection ? (int16_t)(t->selection - 1) : (int16_t)(OPTION_ROW_COUNT - 1);
		redraw = 1;
	} else if (key == 4 || key == 0x32 /* '2' */) {
		/* DOWN */
		t->selection = (int16_t)(t->selection + 1);
		if (t->selection == OPTION_ROW_COUNT)
			t->selection = 0;
		redraw = 1;
	} else if (key == 0x0D    /* CR / '\r' */
			   || key == 0x20 /* ' ' */
			   || key == 0x2B /* '+' */
			   || key == 0x3D /* '=' */) {
		cycle_forward(t->values, t->max_values, t->selection);
		redraw = 1;
	} else if (key == 0x2D /* '-' */) {
		cycle_backward(t->values, t->max_values, t->selection);
		redraw = 1;
	} else if (key == 0x1B    /* ESC */
			   || key == 0x51 /* 'Q' */
			   || key == 0x71 /* 'q' */) {
		t->exit_code = 2;
		exit_room = 1;
	}

	/* Mouse: edge-detect a button-1 or button-2 release.
	 * Button 1 release -> selection++ (wrap).
	 * Button 2 release -> cycle current value forward. */
	cur_buttons = (uint16_t)(inputbuttons & 0x0F);
	if ((t->prev_buttons == 1 || t->prev_buttons == 2) && cur_buttons == 0) {
		if (t->prev_buttons == 1) {
			t->selection = (int16_t)(t->selection + 1);
			if (t->selection == OPTION_ROW_COUNT)
				t->selection = 0;
		} else {
			cycle_forward(t->values, t->max_values, t->selection);
		}
		redraw = 1;
	}
	t->prev_buttons = cur_buttons;

	return exit_room ? 1 : (redraw ? 2 : 0);
}

void option_OpenRoom(OptionRoomState* t) {
	static const uint8_t max_values[OPTION_ROW_COUNT] = {
		1, 2, 3, 1, 1, 1, 1, 1, 1, 1, 1, 16, 16, 16,
	};
	static const uint8_t kind_offsets[OPTION_ROW_COUNT] = {
		0, 4, 7, 0, 0, 0, 0, 2, 0, 11, 13, VOLUME_KIND, VOLUME_KIND, VOLUME_KIND,
	};

	pick_layout();

	dropflag = 1;
	festring_setlinewrap(0);
	festring_setautofill(1);
	festring_setfontsize(1);
	festring_setbound(0, 0, (int16_t)screenXRes, (int16_t)screenYRes);
	festring_setbackcolor(BG_DEFAULT);
	festring_settextcolor(TEXT_COLOUR);

	paint_row_bands();
	festring_setbound(0, 0, (int16_t)screenXRes, (int16_t)screenYRes);
	festring_setbackcolor(BG_DEFAULT);

	/* Pack owning globals into values[0..13]. values[1]/values[7]
	 * invert the stored range (higher index = more detail on-screen). */
	t->values[0] = (uint8_t)(gouraudflag != 0);
	t->values[1] = (uint8_t)(1 - shipdetailvalue);
	t->values[2] = (uint8_t)(starshipdetail - 1);
	t->values[3] = drawmarkingsflag;
	t->values[4] = drawbackdropflag;
	t->values[5] = drawdebrisflag;
	t->values[6] = palette_cycle_user;
	t->values[7] = (uint8_t)(2 - stardetaillevel);
	t->values[8] = (uint8_t)inflight_collision;
	t->values[9] = (uint8_t)inflight_invulnerable;
	t->values[10] = (uint8_t)inflight_unlimited;
	t->values[11] = (uint8_t)inflight_sound_vol;
	t->values[12] = (uint8_t)inflight_music_vol;
	t->values[13] = (uint8_t)inflight_speech_vol;

	memcpy(t->max_values, max_values, sizeof max_values);
	memcpy(t->kind_offsets, kind_offsets, sizeof kind_offsets);

	t->prev_buttons = 0;
	t->selection = 0;
	t->exit_code = 0;
	t->previous_selection = 0;
	t->redraw_all = 1;
}
