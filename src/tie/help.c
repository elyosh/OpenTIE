#include "tie/help.h"
#ifdef TIE_MODERN
#include "tie_runtime/display/classic_display.h"
#include "tie_runtime/runtime/help_task.h"
#endif
#include "tie/edition.h"
#include "tie/flight_surface_tie98.h"
#include "tie/frontend_display_tie98.h"

#include "tie/feinput.h"
#include "tie/festring.h"
#include "tie/tie.h"
#include "tie/user.h" /* normalized input key codes */
#include <stdbool.h>
#ifdef TIE_MODERN
#include <landru/task.h>
#endif

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

// FUNCTION: TIE95 0x2C7F0
// FUNCTION: TIE98 0x42F390
int32_t help_helproom(int16_t start_right_col) {
	int16_t cursor_idx;
	int16_t previous_cursor;
	int redraw_all;
	int32_t page_delta;
	uint16_t prev_buttons;
	int32_t row_step;
	int32_t group_gap;
	int16_t exit_room;
	int16_t redraw;
#ifdef TIE_MODERN
	HelpRoomState* continuation = landru_task_top();
	cursor_idx = continuation->cursor_idx;
	previous_cursor = continuation->previous_cursor;
	redraw_all = continuation->redraw_all;
	page_delta = continuation->page_delta;
	prev_buttons = continuation->prev_buttons;
	row_step = continuation->row_step;
	group_gap = continuation->group_gap;
	if (!continuation->started)
#endif
	{
		if (TIE_DISPLAY_DX5)
			FlightSurface_Lock();

		festring_setfontsize(2);
		switch (flightResolution) {
			case TIE_FLIGHT_RES_SVGA:
#if defined(TIE98) || defined(TIE_MODERN)
			case TIE_FLIGHT_RES_SVGA_16:
			case TIE_FLIGHT_RES_SVGA_D3D:
#endif
				helpTop = 44;
				row_step = fontheight + 5;
				group_gap = 12;
				dropflag = 1;
				break;
			case TIE_FLIGHT_RES_VGA:
				helpTop = 18;
				row_step = fontheight + 2;
				group_gap = 0;
				dropflag = 0;
				break;
			default:
				helpTop = 18;
				row_step = fontheight + 2;
				group_gap = 0;
				dropflag = 0;
				break;
		}
		helpBottom = helpTop + HELP_ROWS_PER_COL * row_step + 4 * group_gap;
		festring_setlinewrap(0);
		festring_setautofill(1);
		prev_buttons = 0;
		festring_setbound(0, 0, (uint16_t)screenXRes, (uint16_t)screenYRes);
		festring_setbackcolor(HELP_BG_DEFAULT);
		festring_settextcolor(HELP_TEXT_COLOUR);

		{
			uint16_t col_left;
			int16_t strip_y;
			uint16_t strip_color;
			uint16_t last_y;
			int16_t i;

			strip_color = commandcolor[0];
			last_y = (uint16_t)(helpTop + group_gap);
			strip_y = (int16_t)last_y;
			col_left = 1;

			for (i = 0; i < HELP_TOTAL_ENTRIES; i++) {
				const uint16_t this_color = commandcolor[i];
				if (this_color != strip_color) {
					if (strip_y == helpBottom) {
						festring_setbound(col_left, (uint16_t)(last_y - (group_gap / 4 + 1)),
										  (uint16_t)(col_left + ((screenXRes >> 1) - 2)),
										  (uint16_t)(strip_y - 1));
					} else {
						festring_setbound(col_left, (uint16_t)(last_y - (group_gap / 4 + 1)),
										  (uint16_t)(col_left + ((screenXRes >> 1) - 2)), (uint16_t)strip_y);
						strip_y += group_gap;
						last_y = strip_y;
					}
					festring_setbackcolor(strip_color);
					clearwindow();
					strip_color = commandcolor[i];
				}
				if (strip_y == helpBottom) {
					last_y = (uint16_t)(helpTop + group_gap);
					strip_y = (int16_t)last_y;
					col_left = (uint16_t)((screenXRes >> 1) + 1);
				}
				strip_y += row_step;
			}
			festring_setbound(col_left, (uint16_t)(last_y - (group_gap / 4 + 1)),
							  (uint16_t)(col_left + ((screenXRes >> 1) - 2)), (uint16_t)(helpBottom - 1));
			festring_setbackcolor(strip_color);
			clearwindow();
		}
		festring_setbound(0, 0, (uint16_t)screenXRes, (uint16_t)(helpBottom - 1));
		festring_setbackcolor(HELP_BG_DEFAULT);

		cursor_idx = start_right_col ? HELP_ROWS_PER_COL : 0;
		redraw_all = 1;
		previous_cursor = cursor_idx;

		if (TIE_DISPLAY_DX5)
			FlightSurface_Unlock();
#ifdef TIE_MODERN
		continuation->cursor_idx = cursor_idx;
		continuation->previous_cursor = previous_cursor;
		continuation->redraw_all = (int16_t)redraw_all;
		continuation->page_delta = (int16_t)page_delta;
		continuation->prev_buttons = prev_buttons;
		continuation->row_step = (int16_t)row_step;
		continuation->group_gap = (int16_t)group_gap;
		continuation->started = true;
		continuation->render = true;
		return 0;
#endif
	}
	exit_room = 0;
	do {
#ifdef TIE_MODERN
		if (continuation->render)
#endif
		{
			uint16_t cur_y;
			uint16_t cur_x;
			uint16_t previous_color;
			int16_t row;

			if (TIE_DISPLAY_DX5)
				FlightSurface_Lock();
			cur_y = (uint16_t)(helpTop + group_gap);
			cur_x = 1;
			festring_setbound(0, 0, (uint16_t)((screenXRes >> 1) - 1), (uint16_t)(helpBottom - 1));
			previous_color = commandcolor[0];
			for (row = 0; row < HELP_TOTAL_ENTRIES; row++) {
				uint16_t color;

				festring_setbackcolor(row == cursor_idx ? HELP_CURSOR_COLOUR : commandcolor[row]);
				color = commandcolor[row];
				if (color != previous_color) {
					cur_y += group_gap;
					previous_color = color;
				}
				festring_setcursor(cur_x, cur_y);
				if (redraw_all || row == cursor_idx || row == previous_cursor) {
					festring_outstring((const uint8_t*)helpkeystrings[row]);
					outchar('\n');
					festring_setcursor(cur_x, cur_y);
					festring_outstringright((const uint8_t*)helpscreenstrings[row]);
				}
				cur_y += row_step;
				if (cur_y == helpBottom) {
					cur_y = (uint16_t)helpTop;
					cur_x = (uint16_t)((screenXRes >> 1) + 1);
					festring_setbound((uint16_t)(screenXRes >> 1), 0, (uint16_t)(screenXRes - 1),
									  (uint16_t)(helpBottom - 1));
				}
			}
			previous_cursor = cursor_idx;
			redraw_all = 0;
			if (TIE_DISPLAY_DX5) {
				FlightSurface_Unlock();
				FrontendDisplay_BlitOffscreenToRenderSurface();
				FrontendDisplay_PresentFrame();
			}
#ifdef TIE_MODERN
			continuation->cursor_idx = cursor_idx;
			continuation->previous_cursor = previous_cursor;
			continuation->redraw_all = (int16_t)redraw_all;
			continuation->render = false;
			return 0;
#endif
		}
		redraw = 0;
		do {
			uint16_t cur_buttons;

			feinput_getrawinput();
			feinput_checkinput();
			feinput_degitterinput();
			inputdeltay *= 2; /* leftover scaler; value not consumed here */

			switch ((uint16_t)inputkey) {
				case 0x1B: /* ESC  */
				case 0x51: /* 'Q'  */
				case 0x71: /* 'q'  */
				case 0x6B: /* 'k'  */
				case 0xBB: /* F1 extended scan (0x3B + 0x80) */
					exit_room = 1;
					page_delta = 0;
					redraw = 1;
					break;
				case KEY_LEFT_ARROW:
				case 0x34: /* '4' */
					if (cursor_idx < HELP_ROWS_PER_COL) {
						exit_room = 1;
						page_delta = -1;
					} else {
						cursor_idx -= HELP_ROWS_PER_COL;
					}
					redraw = 1;
					break;
				case KEY_RIGHT_ARROW:
				case 0x36: /* '6' */
					if (cursor_idx >= HELP_ROWS_PER_COL) {
						exit_room = 1;
						page_delta = 1;
					} else {
						cursor_idx += HELP_ROWS_PER_COL;
						if (cursor_idx == HELP_TOTAL_ENTRIES)
							cursor_idx = HELP_TOTAL_ENTRIES - 1;
					}
					redraw = 1;
					break;
				case KEY_UP_ARROW:
				case 0x38: /* '8' */
					if (cursor_idx == 0)
						cursor_idx = HELP_TOTAL_ENTRIES - 1;
					else
						cursor_idx--;
					redraw = 1;
					break;
				case KEY_DOWN_ARROW:
				case 0x32: /* '2' */
					cursor_idx++;
					if (cursor_idx == HELP_TOTAL_ENTRIES)
						cursor_idx = 0;
					redraw = 1;
					break;
			}

			/* Mouse: edge-detect a button release (prev was 1 or 2, current
			 * is 0). Button bit 0 (LMB) steps right, bit 1 (RMB) steps left.
			 * inputbuttons is masked to the low nibble -- the Thrustmaster
			 * top-hat bits live higher up. */
			cur_buttons = (uint16_t)(inputbuttons & 0x0F);
			if ((prev_buttons == 1 || prev_buttons == 2) && cur_buttons == 0) {
				if (prev_buttons == 1) {
					cursor_idx++;
					if (cursor_idx == HELP_TOTAL_ENTRIES)
						cursor_idx = 0;
				} else {
					if (cursor_idx == 0)
						cursor_idx = HELP_TOTAL_ENTRIES - 1;
					else
						cursor_idx--;
				}
				redraw = 1;
			}
			prev_buttons = cur_buttons;
#ifdef TIE_MODERN
			if (!exit_room) {
				continuation->cursor_idx = cursor_idx;
				continuation->page_delta = (int16_t)page_delta;
				continuation->prev_buttons = prev_buttons;
				continuation->render = redraw != 0;
				return 0;
			}
#endif
		} while (!redraw);
	} while (!exit_room);
#ifdef TIE_MODERN
	continuation->finished = true;
#endif
	return page_delta;
}
