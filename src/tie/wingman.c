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
	int16_t selected_idx;
	int16_t prev_buttons;
	int16_t ret_delta;
	int render_again = 1;
#ifdef TIE_MODERN
	WingmanRoomState* continuation = landru_task_top();
	selected_idx = continuation->selected_idx;
	prev_buttons = continuation->prev_buttons;
	ret_delta = continuation->ret_delta;
	render_again = continuation->render;
	if (!continuation->started)
#endif
	{

		dropflag = 1;
		festring_setlinewrap(0);
		festring_setautofill(1);
		festring_setfontsize(1);
		festring_setbound(0, 0, (int16_t)screenXRes, (int16_t)screenYRes);
		festring_setbackcolor(COLOR_BG_NORMAL);
		festring_settextcolor(COLOR_TEXT_DEFAULT);

		selected_idx = 0;
		prev_buttons = 0;
		ret_delta = 0;

#ifdef TIE_MODERN
		continuation->selected_idx = selected_idx;
		continuation->prev_buttons = prev_buttons;
		continuation->ret_delta = ret_delta;
		continuation->started = true;
		continuation->render = true;
		return 0;
#endif
	}
	for (;;) {
		if (render_again) {
			if (TIE_DISPLAY_DX5)
				FlightSurface_Lock();
			{
				/* 20-line visible grid in 320x200, 50-line in the 640x480 modes. */
				const int16_t margin =
					(flightResolution == TIE_FLIGHT_RES_SVGA || flightResolution == TIE_FLIGHT_RES_SVGA_16 ||
					 flightResolution == TIE_FLIGHT_RES_SVGA_D3D)
						? 51
						: 21;
				const uint32_t row_spacing = (uint32_t)(screenYRes - 2 * margin) / NUM_WINGMAN_CMDS;

				int16_t y = margin;
				int i;
				for (i = 0; i < NUM_WINGMAN_CMDS; i++) {
					festring_setbackcolor(
						(uint16_t)(i == selected_idx ? COLOR_BG_SELECTED : COLOR_BG_NORMAL));
					festring_setcursor(1, y);
					festring_outstring((const uint8_t*)wingmanstrings[i]);
					outchar('\n');
					y = (int16_t)(y + row_spacing);
				}
			}
			if (TIE_DISPLAY_DX5) {
				FlightSurface_Unlock();
				FrontendDisplay_BlitOffscreenToRenderSurface();
				FrontendDisplay_PresentFrame();
			}
#ifdef TIE_MODERN
			continuation->selected_idx = selected_idx;
			continuation->prev_buttons = prev_buttons;
			continuation->ret_delta = ret_delta;
			continuation->render = false;
			return 0;
#endif
		}
		{
			uint16_t key;
			int mouse_btn;
			int redraw = 0;
			int exit_room = 0;

			feinput_getrawinput();
			feinput_checkinput();
			feinput_degitterinput();
			inputdeltay = (int16_t)(inputdeltay * 2);

			key = (uint16_t)inputkey;

			if (key == K_LEFT) {
				ret_delta = -1;
				exit_room = 1;
			} else if (key == K_RIGHT) {
				ret_delta = 1;
				exit_room = 1;
			} else if (key == K_UP || key == K_KP8) {
				/* Move up (wrap 0 <-> 9). */
				selected_idx = (int16_t)(selected_idx ? selected_idx - 1 : NUM_WINGMAN_CMDS - 1);
				redraw = 1;
			} else if (key == K_DOWN || key == K_KP2) {
				/* Move down (wrap). */
				selected_idx = (int16_t)((selected_idx + 1) % NUM_WINGMAN_CMDS);
				redraw = 1;
			} else if (key == K_ENTER || key == K_SPACE) {
				/* Select current row -- forward its hotkey letter. Keyboard
				 * select uses offset +7 (matches retail's separate keyboard
				 * hotkey), distinct from the mouse right-click path below
				 * which uses +6. */
				inputkey = (int16_t)(int8_t)wingmanstrings[selected_idx][7];
				ret_delta = 0;
				exit_room = 1;
			} else if (key == K_ESC || key == K_Q_UPPER || key == K_Q_LOWER || key == K_Z_UPPER ||
					   key == K_W_LOWER || key == K_F1) {
				ret_delta = 2;
				exit_room = 1;
			} else if (((key >= 'A' && key <= 'C') || key == 'E' || (key >= 'G' && key <= 'I') ||
						key == 'R' || key == 'S' || key == 'W')) {
				/* Direct-select: leave inputkey as the typed letter so the
				 * caller can dispatch on it (same contract as Enter above). */
				ret_delta = 0;
				exit_room = 1;
			}

			/* Mouse edge-trigger on release of buttons 1 or 2. */
			mouse_btn = inputbuttons & 0xF;
			if ((prev_buttons == 1 || prev_buttons == 2) && mouse_btn == 0) {
				if (prev_buttons == 1) {
					selected_idx = (int16_t)((selected_idx + 1) % NUM_WINGMAN_CMDS);
					redraw = 1;
				} else {
					inputkey = (int16_t)(int8_t)wingmanstrings[selected_idx][6];
					ret_delta = 0;
					exit_room = 1;
				}
			}
			prev_buttons = (int16_t)mouse_btn;

			if (exit_room) {
#ifdef TIE_MODERN
				continuation->finished = true;
#endif
				return ret_delta;
			}
			render_again = redraw;
		}
#ifdef TIE_MODERN
		continuation->selected_idx = selected_idx;
		continuation->prev_buttons = prev_buttons;
		continuation->ret_delta = ret_delta;
		continuation->render = render_again != 0;
		return 0;
#endif
	}
}
