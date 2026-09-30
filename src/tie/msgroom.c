#include "tie/msgroom.h"
#ifdef TIE_MODERN
#include "tie_runtime/display/classic_display.h"
#include "tie_runtime/runtime/msgroom_task.h"
#endif
#include "tie/edition.h"
#include "tie/flight_surface_tie98.h"
#include "tie/frontend_display_tie98.h"

#include "tie/feinput.h"
#include "tie/festring.h"
#include "tie/msg.h"
#include "tie/panelrts.h"
#include "tie/sys2.h"
#include "tie/tie.h"
#include "tie/user.h" /* user_submodal_result */
#include "tie_runtime/display/classic_display.h"
#include "tie_runtime/runtime/profile.h"
#include <stdbool.h>
#ifdef TIE_MODERN
#include <landru/task.h>
#endif

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* --- Module globals (watdbg: msgroom.c ownership) --- */

// GLOBAL: TIE95 0xC5888
// GLOBAL: TIE98 0x4E632C
int16_t lasthistorymsg = -1;
// GLOBAL: TIE95 0xC588A
// GLOBAL: TIE98 0x584D28
uint16_t numhistorymsgs;
// GLOBAL: TIE95 0xD5034
// GLOBAL: TIE98 0x5FCE50
int32_t msgsPerPage;

// GLOBAL: TIE95 0xD5038
// GLOBAL: TIE98 0x5FCE54
MsgHistoryEntry* messagehistory;

/* --- msgroom_scrollmsgs -- */

// FUNCTION: TIE95 0x34A54
int16_t msgroom_scrollmsgs(int16_t cur_idx, int16_t delta) {
	int16_t old_cur_idx;

	if (numhistorymsgs == 0 || lasthistorymsg == -1)
		return cur_idx;

	old_cur_idx = cur_idx;
	cur_idx = (int16_t)(cur_idx + delta);

	if (numhistorymsgs < 300) {
		/* Linear regime: clamp to [msgsPerPage-1, lasthistorymsg]. */
		if (cur_idx < (int16_t)msgsPerPage)
			cur_idx = (int16_t)(msgsPerPage - 1);
		if (cur_idx >= lasthistorymsg)
			return lasthistorymsg;
		return cur_idx;
	}

	/* Wrapped ring: two clamps (backward-at-seam, forward-at-newest)
	 * then modular wrap into [0, 300). */
	if (delta < 0) {
		int16_t past_end = (int16_t)(msgsPerPage + lasthistorymsg);
		if (past_end >= 300)
			past_end = (int16_t)(past_end - 300);
		if (old_cur_idx >= past_end && cur_idx < past_end)
			cur_idx = past_end;
	}
	if (delta > 0 && old_cur_idx <= lasthistorymsg && cur_idx > lasthistorymsg)
		cur_idx = lasthistorymsg;

	if (cur_idx < 0)
		cur_idx = (int16_t)(cur_idx + 300);
	else if (cur_idx >= 300)
		cur_idx = (int16_t)(cur_idx - 300);
	return cur_idx;
}

// FUNCTION: TIE95 0x34340
// FUNCTION: TIE98 0x4570C0
int32_t msgroom_messageroom(void) {
	int16_t cur_top_idx;
	int16_t exit_dir;
	int render_again = 1;
#ifdef TIE_MODERN
	MsgRoomRoomState* continuation = landru_task_top();
	cur_top_idx = continuation->cur_top_idx;
	exit_dir = continuation->exit_dir;
	render_again = continuation->render;
	if (!continuation->started)
#endif
	{

		/* Static setup runs once at push time; this matches the legacy
		 * synchronous prelude and reaches the first render with the
		 * usual font/colour state. */
		msgsPerPage = (flightResolution == TIE_FLIGHT_RES_VGA) ? 14 : 16;
		dropflag = 1;
		festring_setlinewrap(0);
		festring_setautofill(1);
		festring_setfontsize(1);
		festring_setbound(0, 0, (int16_t)screenXRes, (int16_t)screenYRes);
		festring_setbackcolor(0x44);
		festring_settextcolor(0x43);

		messagehistory = (MsgHistoryEntry*)xmemhdl_Lock_Handle(messageloghandle);
		xmemhdl_Unlock_Handle(messageloghandle);
		cur_top_idx = lasthistorymsg;
		exit_dir = 0;

#ifdef TIE_MODERN
		continuation->cur_top_idx = cur_top_idx;
		continuation->exit_dir = exit_dir;
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
				int16_t walk_idx = cur_top_idx;
				int16_t i;

				for (i = 0; i < (int16_t)msgsPerPage; i++) {
					int16_t idx;
					char* body;
					uint8_t msg_type;
					char last_ch;
					int16_t ts_y;
					uint16_t min_width;

					if ((uint16_t)walk_idx == 0xFFFF) {
						if (numhistorymsgs < 300)
							break;
						walk_idx = (int16_t)(walk_idx + 300);
					}
					idx = walk_idx;

					festring_setcursor(
						1, (int16_t)(((int)msgsPerPage - (i + 1)) * (fontheight + 3) + 2 * fontheight));

					body = messagehistory[idx].body;
					msg_type = (uint8_t)*body;

					if (msg_type >= 8) {
						/* Unknown / out-of-table: fallback color. */
						festring_settextcolor(0x42);
					} else {
						body = &messagehistory[idx].body[1];
						festring_settextcolor(fontcolorconvert[msg_type]);

						if (msg_type == 1) {
							/* Optional '0'..'3' sub-side selector at body[1]. */
							const uint8_t sub = (uint8_t)*body;
							if (sub >= '0' && sub <= '3') {
								festring_settextcolor(radiosidecolors[sub - '0']);
								body++;
							}
						} else if (msg_type == 2) {
							festring_settextcolor(eventsidecolors[messagehistory[idx].side]);
						}
					}

					/* Body emitter with '[' / ']' color nudges, tracking last_ch. */
					last_ch = 0;
					while (*body) {
						const uint8_t c = (uint8_t)*body;
						if (c == '[') {
							textcolor = (uint8_t)((textcolor == 0xD4) ? (textcolor - 1) : (textcolor + 1));
							body++;
						} else if (c == ']') {
							textcolor = (uint8_t)((textcolor == 0xD3) ? (textcolor + 1) : (textcolor - 1));
							body++;
						} else {
							if (outchar)
								outchar(c);
							body++;
							last_ch = (char)c;
						}
					}

					/* Auto-punctuation: append '.' if last emitted wasn't in "?!: ". */
					if (last_ch != '?' && last_ch != '!' && last_ch != ':' && last_ch != ' ')
						if (outchar)
							outchar('.');

					/* Right-aligned timestamp column. */
					festring_setbackcolor(0x44);
					if (outchar)
						outchar('\n');
					festring_settextcolor(0x42);
					festring_setbackcolor(0x44);

					ts_y = (int16_t)((fontheight + 3) * ((int)msgsPerPage - (i + 1)) + 2 * fontheight);

					if (messagehistory[idx].hours) {
						const int16_t w = sys2_calclength((const uint8_t*)"00:00:00 ");
						festring_setcursor((int16_t)(screenXRes - w), ts_y);
						panelrts_outnum(messagehistory[idx].hours, 2, 1);
						if (outchar)
							outchar(':');
						min_width = 2;
					} else {
						const int16_t w = sys2_calclength((const uint8_t*)"00:00 ");
						festring_setcursor((int16_t)(screenXRes - w), ts_y);
						min_width = 1;
					}
					panelrts_outnum(messagehistory[idx].minutes, 2, min_width);
					if (outchar)
						outchar(':');
					panelrts_outnum(messagehistory[idx].seconds, 2, 2);
					if (outchar)
						outchar(' ');

					walk_idx = (int16_t)(walk_idx - 1);
				}
			}
			if (TIE_DISPLAY_DX5) {
				FlightSurface_Unlock();
				FrontendDisplay_BlitOffscreenToRenderSurface();
				FrontendDisplay_PresentFrame();
			}
#ifdef TIE_MODERN
			continuation->cur_top_idx = cur_top_idx;
			continuation->exit_dir = exit_dir;
			continuation->render = false;
			return 0;
#endif
		}
		{
			uint16_t k;
			int handled;
			int redraw;
			int exited;
			int16_t newtop;
			int btn;

			feinput_getrawinput();
			feinput_checkinput();
			feinput_degitterinput();
			inputdeltay = (int16_t)(inputdeltay * 2);

			k = (uint16_t)inputkey;
			handled = 0;
			redraw = 0;
			exited = 0;
			newtop = cur_top_idx;

			switch (k) {
				case KEY_LEFT_ARROW: /* Previous information room */
					exit_dir = -1;
					exited = 1;
					handled = 1;
					break;
				case KEY_RIGHT_ARROW: /* Next information room */
					exit_dir = 1;
					exited = 1;
					handled = 1;
					break;
				case KEY_UP_ARROW:
				case 0x38: /* keypad '8' */
					newtop = msgroom_scrollmsgs(cur_top_idx, -1);
					handled = 1;
					redraw = 1;
					break;
				case KEY_DOWN_ARROW:
				case 0x32: /* keypad '2' */
					newtop = msgroom_scrollmsgs(cur_top_idx, 1);
					handled = 1;
					redraw = 1;
					break;
				case 0x33: /* keypad '3' (PgDn) */
					newtop = msgroom_scrollmsgs(cur_top_idx, (int16_t)msgsPerPage);
					handled = 1;
					redraw = 1;
					break;
				case 0x39: /* keypad '9' (PgUp) */
					newtop = msgroom_scrollmsgs(cur_top_idx, (int16_t)-msgsPerPage);
					handled = 1;
					redraw = 1;
					break;
				case 0x37: /* keypad '7' (Home): oldest still in ring */
					if (numhistorymsgs && lasthistorymsg != -1) {
						if (numhistorymsgs >= 300) {
							int16_t anchor = (int16_t)(msgsPerPage + lasthistorymsg);
							if (anchor >= 300)
								anchor = (int16_t)(anchor - 300);
							newtop = anchor;
						} else {
							int16_t a = (int16_t)(msgsPerPage - 1);
							if (a > lasthistorymsg)
								a = lasthistorymsg;
							newtop = a;
						}
					}
					handled = 1;
					redraw = 1;
					break;
				case 0x31: /* keypad '1' (End): newest */
					if (numhistorymsgs && lasthistorymsg != -1)
						newtop = lasthistorymsg;
					handled = 1;
					redraw = 1;
					break;
				case 0x1B: /* ESC */
				case 0x51: /* 'Q' */
				case 0x6C: /* 'l' */
				case 0x71: /* 'q' */
				case 0xBB: /* F1 (scancode 0x3B + 0x80 offset) */
					exit_dir = 0;
					exited = 1;
					handled = 1;
					break;
				default:
					break;
			}

			/* Mouse fallback: stacks on top of keyboard scroll (faithful to
			 * the binary's fall-through). Left=-1, Right=+1. */
			btn = inputbuttons & 0xF;
			if (btn == 1 || btn == 2) {
				newtop = msgroom_scrollmsgs(newtop, (btn == 1) ? -1 : 1);
				handled = 1;
				redraw = 1;
			}

			cur_top_idx = newtop;

			if (exited) {
#ifdef TIE_MODERN
				continuation->finished = true;
#endif
				return exit_dir;
			}
			render_again = redraw;
			(void)handled;
		}
#ifdef TIE_MODERN
		continuation->cur_top_idx = cur_top_idx;
		continuation->exit_dir = exit_dir;
		continuation->render = render_again != 0;
		return 0;
#endif
	}
}
