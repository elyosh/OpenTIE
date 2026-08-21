#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "tie/feinput.h"
#include "tie/festring.h"
#include "tie/flight_surface_tie98.h"
#include "tie/frontend_display_tie98.h"
#include "tie/msg.h"
#include "tie/msgroom.h"
#include "tie/panelrts.h"
#include "tie/sys2.h"
#include "tie/tie.h"
#include "tie/user.h" /* user_submodal_result */
#include "tie_runtime/display/classic_display.h"
#include "tie_runtime/runtime/profile.h"
#include <landru/task.h>

/* --- Module globals (watdbg: msgroom.c ownership) --- */

int16_t lasthistorymsg = -1;
uint16_t numhistorymsgs;
int32_t msgsPerPage;

/* Static 300-slot message ring; messagehistory is always valid. */
static MsgHistoryEntry history_ring[MSG_HISTORY_SLOTS];
MsgHistoryEntry* messagehistory = history_ring;

/* Timestamp width-measuring pads. "00:00:00 " for HH:MM:SS lines, "00:00 "
 * for MM:SS lines. Used only for sys2_calclength() to compute the right-
 * aligned cursor-x position. */
static const char TS_PAD_HMS[] = "00:00:00 ";
static const char TS_PAD_MS[] = "00:00 ";

/* --- msgroom_scrollmsgs -- */

// FUNCTION: TIE 0x34A54
int16_t msgroom_scrollmsgs(int16_t cur_idx, int16_t delta) {
	if (numhistorymsgs == 0 || lasthistorymsg == -1)
		return cur_idx;

	const int16_t old_cur_idx = cur_idx;
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

typedef enum {
	MSGROOM_PHASE_RENDER = 0,
	MSGROOM_PHASE_POLL,
} MsgRoomPhase;

typedef struct MsgRoomTask {
	int16_t cur_top_idx;
	int16_t exit_dir;
	MsgRoomPhase phase;
} MsgRoomTask;

/* Render one page of the message ring. */
static void msgroom_render_page(int16_t cur_top_idx) {
	int16_t walk_idx = cur_top_idx;
	for (int16_t i = 0; i < (int16_t)msgsPerPage; i++) {
		if ((uint16_t)walk_idx == 0xFFFF) {
			if (numhistorymsgs < 300)
				break;
			walk_idx = (int16_t)(walk_idx + 300);
		}
		const int16_t idx = walk_idx;

		festring_setcursor(1, (int16_t)(((int)msgsPerPage - (i + 1)) * (fontheight + 3) + 2 * fontheight));

		char* body = messagehistory[idx].body;
		const uint8_t msg_type = (uint8_t)*body;

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
		char last_ch = 0;
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

		const int16_t ts_y = (int16_t)((fontheight + 3) * ((int)msgsPerPage - (i + 1)) + 2 * fontheight);

		uint16_t min_width;
		if (messagehistory[idx].hours) {
			const int16_t w = sys2_calclength((const uint8_t*)TS_PAD_HMS);
			festring_setcursor((int16_t)(screenXRes - w), ts_y);
			panelrts_outnum(messagehistory[idx].hours, 2, 1);
			if (outchar)
				outchar(':');
			min_width = 2;
		} else {
			const int16_t w = sys2_calclength((const uint8_t*)TS_PAD_MS);
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

/* Process one input-poll iteration. Returns 1 if an exit-class key
 * fired (exit_dir is set), 2 if any input changed cur_top_idx (the
 * page needs a redraw), 0 if nothing was consumed. The caller drives
 * the phase machine: any non-zero return means "a tick of work
 * happened"; a zero return means "no input ready, yield". */
static int msgroom_poll_once(MsgRoomTask* t) {
	feinput_getrawinput();
	feinput_checkinput();
	feinput_degitterinput();
	inputdeltay = (int16_t)(inputdeltay * 2);

	const uint16_t k = (uint16_t)inputkey;
	int handled = 0;
	int redraw = 0;
	int exited = 0;
	int16_t newtop = t->cur_top_idx;

	switch (k) {
		case 1: /* Up arrow: exit prev */
			t->exit_dir = -1;
			exited = 1;
			handled = 1;
			break;
		case 2: /* Down arrow: exit next */
			t->exit_dir = 1;
			exited = 1;
			handled = 1;
			break;
		case 3:    /* Right arrow */
		case 0x38: /* keypad '8' */
			newtop = msgroom_scrollmsgs(t->cur_top_idx, -1);
			handled = 1;
			redraw = 1;
			break;
		case 4:    /* Left arrow */
		case 0x32: /* keypad '2' */
			newtop = msgroom_scrollmsgs(t->cur_top_idx, 1);
			handled = 1;
			redraw = 1;
			break;
		case 0x33: /* keypad '3' (PgDn) */
			newtop = msgroom_scrollmsgs(t->cur_top_idx, (int16_t)msgsPerPage);
			handled = 1;
			redraw = 1;
			break;
		case 0x39: /* keypad '9' (PgUp) */
			newtop = msgroom_scrollmsgs(t->cur_top_idx, (int16_t)-msgsPerPage);
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
			t->exit_dir = 0;
			exited = 1;
			handled = 1;
			break;
		default:
			break;
	}

	/* Mouse fallback: stacks on top of keyboard scroll (faithful to
	 * the binary's fall-through). Left=-1, Right=+1. */
	const int btn = inputbuttons & 0xF;
	if (btn == 1 || btn == 2) {
		newtop = msgroom_scrollmsgs(newtop, (btn == 1) ? -1 : 1);
		handled = 1;
		redraw = 1;
	}

	t->cur_top_idx = newtop;

	if (exited)
		return 1;
	if (redraw)
		return 2;
	(void)handled;
	return 0;
}

// FUNCTION: TIE 0x34340, TIE98 0x4570C0 (task-split recovery)
static LandruTaskStepResult msgroom_task_step(void* self) {
	MsgRoomTask* t = (MsgRoomTask*)self;

	if (t->phase == MSGROOM_PHASE_RENDER) {
		const bool tie98_display = TieClassicDisplay_UsesDx5();
		if (tie98_display)
			FlightSurface_Lock();
		msgroom_render_page(t->cur_top_idx);
		if (tie98_display) {
			FlightSurface_Unlock();
			FrontendDisplay_BlitOffscreenToRenderSurface();
			FrontendDisplay_PresentFrame();
		}
		t->phase = MSGROOM_PHASE_POLL;
		return LANDRU_TASK_STEP_CONTINUE;
	}

	/* POLL */
	int r = msgroom_poll_once(t);
	if (r == 1) {
		user_submodal_result = (int32_t)t->exit_dir;
		return LANDRU_TASK_STEP_DONE;
	}
	if (r == 2)
		t->phase = MSGROOM_PHASE_RENDER;
	return LANDRU_TASK_STEP_CONTINUE;
}

static const LandruTaskVtable msgroom_task_vt = {
	.step = msgroom_task_step,
};

void msgroom_Push_MessageRoom_Task(void) {
	MsgRoomTask* t = (MsgRoomTask*)landru_task_push(&msgroom_task_vt);
	if (!t)
		return;

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

	t->cur_top_idx = lasthistorymsg;
	t->exit_dir = 0;
	t->phase = MSGROOM_PHASE_RENDER;
}
