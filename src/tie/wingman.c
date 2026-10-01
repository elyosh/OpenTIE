/* Wingman command menu for USER_inflightinfo. Hotkeys are stored at byte six
 * of each STRINGS.DAT entry. Returns -1/+1 for adjacent screens and +2 for
 * cancellation; selection updates inputkey and raises dropflag. */

#include "tie/wingman.h"
#ifdef TIE_MODERN
#include "tie_runtime/display/classic_display.h"
#include "tie_runtime/runtime/wingman_task.h"
#endif
#include "tie/edition.h"
#include "tie/flight_surface_tie98.h"
#include "tie/frontend_display_tie98.h"

#include "tie/feinput.h"
#include "tie/festring.h"
#include "tie/tie.h"
#include <stdbool.h>
#ifdef TIE_MODERN
#include <landru/task.h>
#endif

#include <stdint.h>

enum {
	NUM_WINGMAN_CMDS = 10,
};

/* Row background colors (match DAMAGE/MSGROOM rooms in the same family). */
enum {
	COLOR_BG_NORMAL = 0x44,
	COLOR_BG_SELECTED = 0x46,
	COLOR_TEXT_DEFAULT = 0x52,
};

/* Keyboard codes after FEINPUT remaps DOS extended keys. */
enum {
	K_LEFT = 0x01,
	K_RIGHT = 0x02,
	K_UP = 0x03,
	K_DOWN = 0x04,
	K_ENTER = 0x0D,
	K_ESC = 0x1B,
	K_SPACE = 0x20,
	K_KP2 = 0x32, /* keypad '2' (down) */
	K_KP8 = 0x38, /* keypad '8' (up)   */
	K_Q_UPPER = 0x51,
	K_Z_UPPER = 0x5A,
	K_Q_LOWER = 0x71,
	K_W_LOWER = 0x77, /* this page's own hotkey (toggles wingman off)  */
	K_F1 = 0xBB,      /* F1 scancode + 0x80 (FEINPUT extended-remap)   */
};

/* Array of 10 string pointers. Populated in fediskio_loadstringdata. */
// GLOBAL: TIE95 0xEC20C
// GLOBAL: TIE98 0x58DFEC
const char** wingmanstrings;

// FUNCTION: TIE95 0x61F70
// FUNCTION: TIE98 0x499310
int32_t wingman_wingmanroom(void) {
	int16_t selected_idx = 0;
	int16_t full_redraw = 1;
	int16_t exit_room = 0;
	int ret_delta;
	uint16_t prev_buttons = 0;
	int16_t redraw;
	int16_t old_idx = 0;
#ifdef TIE_MODERN
	WingmanRoomState* continuation = landru_task_top();
	if (continuation->started) {
		/* Each resume redraws every row; the partial redraw is an optimization. */
		selected_idx = continuation->selected_idx;
		old_idx = selected_idx;
		prev_buttons = (uint16_t)continuation->prev_buttons;
		ret_delta = continuation->ret_delta;
	} else
#endif
	{
		dropflag = 1;
		festring_setlinewrap(0);
		festring_setautofill(1);
		festring_setfontsize(1);
		festring_setbound(0, 0, (int16_t)screenXRes, (int16_t)screenYRes);
		festring_setbackcolor(COLOR_BG_NORMAL);
		festring_settextcolor(COLOR_TEXT_DEFAULT);
#ifdef TIE_MODERN
		continuation->selected_idx = selected_idx;
		continuation->prev_buttons = (int16_t)prev_buttons;
		continuation->ret_delta = 0;
		continuation->started = true;
		continuation->render = true;
		return 0;
#endif
	}
	do {
#ifdef TIE_MODERN
		if (continuation->render)
#endif
		{
			uint16_t y;
			uint32_t row_spacing;
			int16_t i;

			if (TIE_DISPLAY_DX5)
				FlightSurface_Lock();
			/* 20-line visible grid in 320x200, 50-line in the 640x480 modes. */
			switch (flightResolution) {
			case TIE_FLIGHT_RES_SVGA:
#if defined(TIE98) || defined(TIE_MODERN)
			case TIE_FLIGHT_RES_SVGA_16:
			case TIE_FLIGHT_RES_SVGA_D3D:
#endif
				y = 51;
				break;
			case TIE_FLIGHT_RES_VGA:
			default:
				y = 21;
				break;
			}
			row_spacing = (screenYRes - y * 2) / NUM_WINGMAN_CMDS;
			for (i = 0; i < NUM_WINGMAN_CMDS; i++) {
				festring_setbackcolor(i == selected_idx ? COLOR_BG_SELECTED : COLOR_BG_NORMAL);
				if (i == selected_idx || i == old_idx || full_redraw) {
					festring_setcursor(1, y);
					festring_outstring((const uint8_t*)wingmanstrings[i]);
					outchar('\n');
				}
				y += row_spacing;
			}
			if (TIE_DISPLAY_DX5) {
				FlightSurface_Unlock();
				FrontendDisplay_BlitOffscreenToRenderSurface();
				FrontendDisplay_PresentFrame();
			}
			old_idx = selected_idx;
			full_redraw = 0;
#ifdef TIE_MODERN
			continuation->selected_idx = selected_idx;
			continuation->prev_buttons = (int16_t)prev_buttons;
			continuation->render = false;
			return 0;
#endif
		}
		redraw = 0;
		do {
			uint16_t buttons;

			feinput_getrawinput();
			feinput_checkinput();
			feinput_degitterinput();
			inputdeltay *= 2;

			switch ((uint16_t)inputkey) {
			case K_ESC:
			case K_Q_UPPER:
			case K_Q_LOWER:
			case K_Z_UPPER:
			case K_W_LOWER:
			case K_F1:
				exit_room = 1;
				ret_delta = 2;
				redraw = 1;
				break;
			case K_LEFT:
				ret_delta = -1;
				exit_room = 1;
				redraw = 1;
				break;
			case K_RIGHT:
				exit_room = 1;
				ret_delta = 1;
				redraw = 1;
				break;
			case 'A':
			case 'B':
			case 'C':
			case 'E':
			case 'G':
			case 'H':
			case 'I':
			case 'R':
			case 'S':
			case 'W':
				/* Direct-select: leave inputkey as the typed letter so the
				 * caller can dispatch on it (same contract as Enter below). */
				exit_room = 1;
				ret_delta = 0;
				redraw = 1;
				break;
			case K_UP:
			case K_KP8:
				/* Move up (wrap 0 <-> 9). */
				if (selected_idx == 0)
					selected_idx = NUM_WINGMAN_CMDS - 1;
				else
					selected_idx--;
				redraw = 1;
				break;
			case K_DOWN:
			case K_KP2:
				/* Move down (wrap). */
				if (++selected_idx == NUM_WINGMAN_CMDS)
					selected_idx = 0;
				redraw = 1;
				break;
			case K_ENTER:
			case K_SPACE:
				/* Select current row -- forward its hotkey letter. Keyboard
				 * select uses offset +7 (matches retail's separate keyboard
				 * hotkey), distinct from the mouse right-click path below
				 * which uses +6. */
				inputkey = (signed char)wingmanstrings[selected_idx][7];
				exit_room = 1;
				ret_delta = 0;
				redraw = 1;
				break;
			}

			/* Mouse edge-trigger on release of buttons 1 or 2. */
			buttons = inputbuttons & 0xF;
			if ((prev_buttons == 1 || prev_buttons == 2) && buttons == 0) {
				if (prev_buttons == 1) {
					if (++selected_idx == NUM_WINGMAN_CMDS)
						selected_idx = 0;
					redraw = 1;
				} else {
					inputkey = (signed char)wingmanstrings[selected_idx][6];
					exit_room = 1;
					ret_delta = 0;
					redraw = 1;
				}
			}
			prev_buttons = buttons;
#ifdef TIE_MODERN
			if (exit_room) {
				continuation->finished = true;
				return ret_delta;
			}
			continuation->selected_idx = selected_idx;
			continuation->prev_buttons = (int16_t)prev_buttons;
			continuation->render = redraw != 0;
			return 0;
#endif
		} while (!redraw);
	} while (!exit_room);
	return ret_delta;
}
