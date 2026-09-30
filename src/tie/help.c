#include "tie/help.h"
#include "tie/feinput.h"
#include "tie/festring.h"
#include "tie/tie.h"
#include "tie/user.h" /* normalized input key codes */

#include <stddef.h>
#include <stdint.h>

/* --- Module-owned globals (watdbg: help.c) ----------------------------- */

/*
 * Per-entry background colour for the 48 help rows. Three palette bands:
 *   0x50 (80) - flight / view / target controls
 *   0x48 (72) - weapons
 *   0x44 (68) - communications / system
 * Accessed as an unsigned byte array by both the strip painter and the row
 * render loop.
 */
// GLOBAL: TIE95 0xC53FC
// GLOBAL: TIE98 0x4E3CE0
const uint8_t commandcolor[48] = {
	0x50, 0x50, 0x50, 0x50, 0x50, 0x50, 0x50, 0x50, 0x50, 0x44, 0x44, 0x48, 0x48, 0x48, 0x48, 0x48,
	0x48, 0x50, 0x50, 0x50, 0x50, 0x50, 0x50, 0x50, 0x48, 0x48, 0x48, 0x48, 0x48, 0x48, 0x48, 0x48,
	0x48, 0x44, 0x44, 0x44, 0x50, 0x50, 0x50, 0x50, 0x50, 0x48, 0x48, 0x48, 0x48, 0x48, 0x48, 0x48,
};

/* Unused DOS scan-code table retained in the TIE95 binary. */
// GLOBAL: TIE95 0xC53CC
const uint8_t commandkey[48] = {
	0x2B, 0x2D, 0x5B, 0x5D, 0x08, 0x5C, 0x68, 0x92, 0x71, 0x63, 0x76, 0x5F, 0x2E, 0xBB, 0xBC, 0xBD,
	0xBE, 0x7A, 0xAE, 0x99, 0xAF, 0xA0, 0xB2, 0x9F, 0x74, 0x75, 0x72, 0x65, 0x61, 0x2C, 0x5F, 0x5F,
	0x77, 0x78, 0x62, 0x6D, 0x6C, 0x67, 0x64, 0x5A, 0x3F, 0x1B, 0xC2, 0xC3, 0x3B, 0xC4, 0xDD, 0x73,
};

// GLOBAL: TIE95 0xD4BD0
// GLOBAL: TIE98 0x5FE818
int32_t helpTop;
// GLOBAL: TIE95 0xD4BD8
// GLOBAL: TIE98 0x5FE814
int32_t helpBottom;

/*
 * 48-element char* tables. Filled by fediskio_loadstringdata to point into
 * the relocated stringdata_buf.
 */
// GLOBAL: TIE95 0xD4BCC
// GLOBAL: TIE98 0x5FE80C
char** helpkeystrings;
// GLOBAL: TIE95 0xD4BD4
// GLOBAL: TIE98 0x5FE810
char** helpscreenstrings;

/* --- Local constants --------------------------------------------------- */

enum {
	HELP_ROWS_PER_COL = 24,
	HELP_TOTAL_ENTRIES = 48,
	HELP_BG_DEFAULT = 0x44,    /* margin colour outside the grid */
	HELP_TEXT_COLOUR = 0x43,   /* default foreground */
	HELP_CURSOR_COLOUR = 0x46, /* background for the selected row */
};

/*
 * inputkey dispatch targets. The values below are post-FEINPUT remap:
 * FEINPUT_getrawinput folds extended DOS scan codes such that the four
 * arrow keys become 1..4 while other ASCII keys pass through unchanged.
 */
enum HelpAction {
	HA_NONE = 0,
	HA_PREVIOUS_COLUMN, /* cursor -= 24, or exit -1 from left column  */
	HA_NEXT_COLUMN,     /* cursor += 24, or exit +1 from right column */
	HA_PREVIOUS_ITEM,   /* --cursor, wraps 0 -> 47                    */
	HA_NEXT_ITEM,       /* ++cursor, wraps 47 -> 0                    */
	HA_EXIT,            /* return to flight */
};

static int help_classify_key(uint16_t key) {
	switch (key) {
		case KEY_LEFT_ARROW:
		case 0x34: /* '4' */
			return HA_PREVIOUS_COLUMN;
		case KEY_RIGHT_ARROW:
		case 0x36: /* '6' */
			return HA_NEXT_COLUMN;
		case KEY_UP_ARROW:
		case 0x38: /* '8' */
			return HA_PREVIOUS_ITEM;
		case KEY_DOWN_ARROW:
		case 0x32: /* '2' */
			return HA_NEXT_ITEM;
		case 0x1B: /* ESC  */
		case 0x51: /* 'Q'  */
		case 0x71: /* 'q'  */
		case 0x6B: /* 'k'  */
		case 0xBB: /* F1 extended scan (0x3B + 0x80) */
			return HA_EXIT;
		default:
			return HA_NONE;
	}
}

/* Paint the colour groups, including the extra spacing in SVGA modes. */
static void help_paint_strip(int16_t left, int16_t top, int16_t right, int16_t bottom, uint16_t colour) {
	festring_setbound(left, top, right, bottom);
	festring_setbackcolor(colour);
	clearwindow();
}

static void help_paint_background(const HelpRoomState* t) {
	const int16_t col_width = (int16_t)((screenXRes >> 1) - 2);
	int16_t col_left = 1;
	int16_t strip_y = (int16_t)(helpTop + t->group_gap);
	int16_t last_y = strip_y;
	uint16_t strip_color = commandcolor[0];
	int i;

	for (i = 0; i < HELP_TOTAL_ENTRIES; i++) {
		const uint16_t this_color = commandcolor[i];
		if (this_color != strip_color) {
			const int16_t top = (int16_t)(last_y - t->group_gap / 4 - 1);
			const int16_t bottom = strip_y == helpBottom ? (int16_t)(strip_y - 1) : strip_y;
			help_paint_strip(col_left, top, (int16_t)(col_left + col_width), bottom, strip_color);
			if (strip_y != helpBottom) {
				strip_y = (int16_t)(strip_y + t->group_gap);
				last_y = strip_y;
			}
			strip_color = this_color;
		}
		if (strip_y == helpBottom) {
			strip_y = (int16_t)(helpTop + t->group_gap);
			last_y = strip_y;
			col_left = (int16_t)((screenXRes >> 1) + 1);
		}
		strip_y = (int16_t)(strip_y + t->row_step);
	}
	help_paint_strip(col_left, (int16_t)(last_y - t->group_gap / 4 - 1), (int16_t)(col_left + col_width),
					 (int16_t)(helpBottom - 1), strip_color);
}

void help_render_rows(HelpRoomState* t) {
	int16_t cur_x = 1;
	int16_t cur_y = (int16_t)(helpTop + t->group_gap);
	uint16_t previous_color = commandcolor[0];
	int16_t row;

	festring_setbound(0, 0, (int16_t)((screenXRes >> 1) - 1), (int16_t)(helpBottom - 1));
	for (row = 0; row < HELP_TOTAL_ENTRIES; row++) {
		const uint16_t color = commandcolor[row];
		festring_setbackcolor(row == t->cursor_idx ? HELP_CURSOR_COLOUR : color);
		if (color != previous_color) {
			previous_color = color;
			cur_y = (int16_t)(cur_y + t->group_gap);
		}
		festring_setcursor(cur_x, cur_y);
		if (t->redraw_all || row == t->cursor_idx || row == t->previous_cursor) {
			festring_outstring((const uint8_t*)helpkeystrings[row]);
			outchar('\n');
			festring_setcursor(cur_x, cur_y);
			festring_outstringright((const uint8_t*)helpscreenstrings[row]);
		}
		cur_y = (int16_t)(cur_y + t->row_step);
		if (cur_y == helpBottom) {
			cur_y = (int16_t)helpTop;
			cur_x = (int16_t)((screenXRes >> 1) + 1);
			festring_setbound((int16_t)(screenXRes >> 1), 0, (int16_t)(screenXRes - 1),
							  (int16_t)(helpBottom - 1));
		}
	}
	t->previous_cursor = t->cursor_idx;
	t->redraw_all = 0;
}

/* Single input-poll iteration. 1 = exit fired, 2 = cursor moved
 * (page redraw needed), 0 = no input. */
int help_poll_once(HelpRoomState* t) {
	int action;
	int redraw = 0;
	int exit_room = 0;
	uint16_t cur_buttons;

	feinput_getrawinput();
	feinput_checkinput();
	feinput_degitterinput();
	inputdeltay *= 2; /* leftover scaler; value not consumed here */

	action = help_classify_key((uint16_t)inputkey);

	switch (action) {
		case HA_PREVIOUS_COLUMN:
			if (t->cursor_idx >= HELP_ROWS_PER_COL) {
				t->cursor_idx -= HELP_ROWS_PER_COL;
				redraw = 1;
			} else {
				t->page_delta = -1;
				exit_room = 1;
			}
			break;
		case HA_NEXT_COLUMN:
			if (t->cursor_idx < HELP_ROWS_PER_COL) {
				t->cursor_idx = (int16_t)(t->cursor_idx + HELP_ROWS_PER_COL);
				if (t->cursor_idx == HELP_TOTAL_ENTRIES)
					t->cursor_idx = HELP_TOTAL_ENTRIES - 1;
				redraw = 1;
			} else {
				t->page_delta = 1;
				exit_room = 1;
			}
			break;
		case HA_PREVIOUS_ITEM:
			t->cursor_idx = t->cursor_idx ? (int16_t)(t->cursor_idx - 1) : (int16_t)(HELP_TOTAL_ENTRIES - 1);
			redraw = 1;
			break;
		case HA_NEXT_ITEM:
			t->cursor_idx = (int16_t)(t->cursor_idx + 1);
			if (t->cursor_idx == HELP_TOTAL_ENTRIES)
				t->cursor_idx = 0;
			redraw = 1;
			break;
		case HA_EXIT:
			t->page_delta = 0;
			exit_room = 1;
			break;
		default:
			break;
	}

	/* Mouse: edge-detect a button release (prev was 1 or 2, current
	 * is 0). Button bit 0 (LMB) steps right, bit 1 (RMB) steps left.
	 * inputbuttons is masked to the low nibble -- the Thrustmaster
	 * top-hat bits live higher up. */
	cur_buttons = (uint16_t)(inputbuttons & 0x0F);
	if ((t->prev_buttons == 1 || t->prev_buttons == 2) && cur_buttons == 0) {
		if (t->prev_buttons == 1) {
			t->cursor_idx = (int16_t)(t->cursor_idx + 1);
			if (t->cursor_idx == HELP_TOTAL_ENTRIES)
				t->cursor_idx = 0;
		} else {
			t->cursor_idx = t->cursor_idx ? (int16_t)(t->cursor_idx - 1) : (int16_t)(HELP_TOTAL_ENTRIES - 1);
		}
		redraw = 1;
	}
	t->prev_buttons = cur_buttons;

	return exit_room ? 1 : (redraw ? 2 : 0);
}

void help_OpenRoom(HelpRoomState* t, int32_t start_right_col) {
	festring_setfontsize(2);
	if (tie_is_high_resolution_flight()) {
		helpTop = 44;
		t->row_step = (int16_t)(fontheight + 5);
		t->group_gap = 12;
		dropflag = 1;
	} else {
		helpTop = 18;
		t->row_step = (int16_t)(fontheight + 2);
		t->group_gap = 0;
		dropflag = 0;
	}
	helpBottom = helpTop + HELP_ROWS_PER_COL * t->row_step + 4 * t->group_gap;
	festring_setlinewrap(0);
	festring_setautofill(1);
	festring_setbound(0, 0, (int16_t)screenXRes, (int16_t)screenYRes);
	festring_setbackcolor(HELP_BG_DEFAULT);
	festring_settextcolor(HELP_TEXT_COLOUR);

	help_paint_background(t);
	festring_setbound(0, 0, (int16_t)screenXRes, (int16_t)(helpBottom - 1));
	festring_setbackcolor(HELP_BG_DEFAULT);

	t->cursor_idx = start_right_col ? HELP_ROWS_PER_COL : 0;
	t->previous_cursor = t->cursor_idx;
	t->redraw_all = 1;
	t->page_delta = 0;
	t->prev_buttons = 0;
}
