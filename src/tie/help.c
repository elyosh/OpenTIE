#include <stddef.h>
#include <stdint.h>

#include "tie/feinput.h"
#include "tie/festring.h"
#include "tie/flight_surface_tie98.h"
#include "tie/frontend_display_tie98.h"
#include "tie/help.h"
#include "tie/tie.h"
#include "tie/user.h" /* user_submodal_result */
#include "tie_runtime/display/classic_display.h"
#include "tie_runtime/runtime/profile.h"
#include <landru/task.h>

/* --- Module-owned globals (watdbg: help.c) ----------------------------- */

/*
 * Per-entry background colour for the 48 help rows. Three palette bands:
 *   0x50 (80) - flight / view / target controls
 *   0x48 (72) - weapons
 *   0x44 (68) - communications / system
 * Accessed as an unsigned byte array by both the strip painter and the row
 * render loop.
 */
const uint8_t commandcolor[48] = {
	0x50, 0x50, 0x50, 0x50, 0x50, 0x50, 0x50, 0x50, 0x50, 0x44, 0x44, 0x48, 0x48, 0x48, 0x48, 0x48,
	0x48, 0x50, 0x50, 0x50, 0x50, 0x50, 0x50, 0x50, 0x48, 0x48, 0x48, 0x48, 0x48, 0x48, 0x48, 0x48,
	0x48, 0x44, 0x44, 0x44, 0x50, 0x50, 0x50, 0x50, 0x50, 0x48, 0x48, 0x48, 0x48, 0x48, 0x48, 0x48,
};

/*
 * Dead data: 48 would-be scan codes (zero xrefs in the binary). Emitted for
 * parity with the Watcom build; remove if the final link strips unused
 * objects.
 */
const uint8_t commandkey[48] = {
	0x2B, 0x2D, 0x5B, 0x5D, 0x08, 0x5C, 0x68, 0x92, 0x71, 0x63, 0x76, 0x5F, 0x2E, 0xBB, 0xBC, 0xBD,
	0xBE, 0x7A, 0xAE, 0x99, 0xAF, 0xA0, 0xB2, 0x9F, 0x74, 0x75, 0x72, 0x65, 0x61, 0x2C, 0x5F, 0x5F,
	0x77, 0x78, 0x62, 0x6D, 0x6C, 0x67, 0x64, 0x5A, 0x3F, 0x1B, 0xC2, 0xC3, 0x3B, 0xC4, 0xDD, 0x73,
};

int32_t helpTop;
int32_t helpBottom;

/*
 * 48-element char* tables. Filled by fediskio_loadstringdata to point into
 * the relocated stringdata_buf.
 */
char** helpkeystrings;
char** helpscreenstrings;

/* --- Local constants --------------------------------------------------- */

#define HELP_ROWS_PER_COL 24
#define HELP_TOTAL_ENTRIES 48
#define HELP_BG_DEFAULT 0x44    /* margin colour outside the grid */
#define HELP_TEXT_COLOUR 0x43   /* default foreground */
#define HELP_CURSOR_COLOUR 0x46 /* background for the selected row */

/*
 * inputkey dispatch targets. The values below are post-FEINPUT remap:
 * FEINPUT_getrawinput folds extended DOS scan codes such that the four
 * arrow keys become 1..4 while other ASCII keys pass through unchanged.
 */
enum HelpAction {
	HA_NONE = 0,
	HA_UP,    /* cursor -= 24, or exit -1 from left column  */
	HA_DOWN,  /* cursor += 24, or exit +1 from right column */
	HA_LEFT,  /* --cursor, wraps 0 -> 47                    */
	HA_RIGHT, /* ++cursor, wraps 47 -> 0                    */
	HA_PICK,  /* exit with 0 (ESC / Enter / F1 / 'q' ...)   */
};

static int help_classify_key(uint16_t key) {
	switch (key) {
		case 1:
		case 0x34: /* '4' */
			return HA_UP;
		case 2:
		case 0x36: /* '6' */
			return HA_DOWN;
		case 3:
		case 0x38: /* '8' */
			return HA_LEFT;
		case 4:
		case 0x32: /* '2' */
			return HA_RIGHT;
		case 0x1B: /* ESC  */
		case 0x51: /* 'Q'  */
		case 0x71: /* 'q'  */
		case 0x6B: /* 'k'  */
		case 0xBB: /* F1 extended scan (0x3B + 0x80) */
			return HA_PICK;
		default:
			return HA_NONE;
	}
}

/* --- Phase 1 helper: paint colour-banded background strips ------------- */

/*
 * Emit a strip rectangle and clear it to `colour`. The strip covers rows
 * top..bottom of the column whose left/width are left/width. festring_setbound
 * expects inclusive top/bottom; clearwindow() uses the currently-installed
 * backcolor (the compiler's `mov eax, new_strip_color; call clearwindow`
 * leftover in EAX is unused by the real entry point).
 */
static void help_paint_strip(int16_t left, int16_t top_m1, int16_t right, int16_t bottom, uint16_t colour) {
	festring_setbound(left, top_m1, right, bottom);
	festring_setbackcolor(colour);
	clearwindow();
}

static void help_paint_background(void) {
	const int row_step = fontheight + 2;
	const int16_t col_width = (int16_t)((screenXRes >> 1) - 2);
	int16_t col_left = 1;                   /* left column x origin */
	int16_t strip_y = (int16_t)helpTop;     /* current row's top Y  */
	int16_t last_y = strip_y;               /* top of the current run */
	uint16_t strip_color = commandcolor[0]; /* colour of the run    */

	for (int i = 0; i < HELP_TOTAL_ENTRIES; i++) {
		const uint16_t this_color = commandcolor[i];

		/* Flush the previous run on a colour change. */
		if (this_color != strip_color) {
			/* When strip_y has just been wrapped to the top of the right
			 * column (see the end-of-iteration wrap below), the emitted
			 * rect needs to stop one pixel above helpBottom, not at it. */
			const int16_t top_m1 = (int16_t)(last_y - 1);
			const int16_t bot = (strip_y == (int16_t)helpBottom) ? (int16_t)(strip_y - 1) : strip_y;
			help_paint_strip(col_left, top_m1, col_left + col_width, bot, strip_color);
			strip_color = this_color;
			last_y = strip_y;
		}

		/* Column wrap: left bottom -> right top. */
		if (strip_y == (int16_t)helpBottom) {
			strip_y = (int16_t)helpTop;
			last_y = strip_y;
			col_left = (int16_t)((screenXRes >> 1) + 1);
		}

		strip_y = (int16_t)(strip_y + row_step);
	}

	/* Trailing strip from the last colour change down to the bottom. */
	help_paint_strip(col_left, (int16_t)(last_y - 1), (int16_t)(col_left + col_width),
					 (int16_t)(helpBottom - 1), strip_color);
}

/* --- Phase 2 helper: draw the 48 text rows ----------------------------- */

static void help_render_rows(int16_t cursor_idx) {
	int16_t cur_x = 1;
	int16_t cur_y = (int16_t)helpTop;

	/* Start with the left column's clip box. */
	festring_setbound(0, 0, (int16_t)((screenXRes >> 1) - 1), (int16_t)(helpBottom - 1));

	for (int16_t row = 0; row < HELP_TOTAL_ENTRIES; row++) {
		const uint16_t bg = (row == cursor_idx) ? HELP_CURSOR_COLOUR : commandcolor[row];
		festring_setbackcolor(bg);
		festring_setcursor(cur_x, cur_y);
		festring_outstring((const uint8_t*)helpkeystrings[row]);

		/*
		 * Five-way newline injection around the cursor. The binary emits one
		 * outchar('\n') for each of these predicates, and a cell can match
		 * more than one (e.g. row == cursor AND cursor == 47 wraps). That
		 * stacking is intentional -- it creates the visible gap that the
		 * highlight rectangle needs against the colour-banded background.
		 */
		const int16_t delta = (int16_t)(row - cursor_idx);
		if (delta > -2 && delta < 2)
			outchar('\n');
		if (row == 0 && cursor_idx >= HELP_TOTAL_ENTRIES - 1)
			outchar('\n');
		if (row + HELP_ROWS_PER_COL == cursor_idx)
			outchar('\n');
		if (row - HELP_ROWS_PER_COL == cursor_idx)
			outchar('\n');
		if (row == HELP_TOTAL_ENTRIES - 1 && cursor_idx <= 0)
			outchar('\n');

		festring_setcursor(cur_x, cur_y);
		festring_outstringright((const uint8_t*)helpscreenstrings[row]);

		cur_y = (int16_t)(cur_y + fontheight + 2);
		if (cur_y == (int16_t)helpBottom) {
			/* Wrap from the left column to the right column. */
			cur_y = (int16_t)helpTop;
			cur_x = (int16_t)((screenXRes >> 1) + 1);
			festring_setbound((int16_t)(screenXRes >> 1), 0, (int16_t)(screenXRes - 1),
							  (int16_t)(helpBottom - 1));
		}
	}
}

/* --- Public entry point ------------------------------------------------ */

typedef enum {
	HELP_PHASE_RENDER = 0,
	HELP_PHASE_POLL,
} HelpPhase;

typedef struct HelpTask {
	int16_t cursor_idx;
	int16_t page_delta;
	uint16_t prev_buttons;
	HelpPhase phase;
} HelpTask;

/* Single input-poll iteration. 1 = exit fired, 2 = cursor moved
 * (page redraw needed), 0 = no input. */
static int help_poll_once(HelpTask* t) {
	feinput_getrawinput();
	feinput_checkinput();
	feinput_degitterinput();
	inputdeltay *= 2; /* leftover scaler; value not consumed here */

	const int action = help_classify_key((uint16_t)inputkey);
	int redraw = 0;

	switch (action) {
		case HA_UP:
			if (t->cursor_idx >= HELP_ROWS_PER_COL) {
				t->cursor_idx -= HELP_ROWS_PER_COL;
				redraw = 1;
			} else {
				t->page_delta = -1;
				return 1;
			}
			break;
		case HA_DOWN:
			if (t->cursor_idx < HELP_ROWS_PER_COL) {
				t->cursor_idx = (int16_t)(t->cursor_idx + HELP_ROWS_PER_COL);
				if (t->cursor_idx == HELP_TOTAL_ENTRIES)
					t->cursor_idx = HELP_TOTAL_ENTRIES - 1;
				redraw = 1;
			} else {
				t->page_delta = 1;
				return 1;
			}
			break;
		case HA_LEFT:
			t->cursor_idx = t->cursor_idx ? (int16_t)(t->cursor_idx - 1) : (int16_t)(HELP_TOTAL_ENTRIES - 1);
			redraw = 1;
			break;
		case HA_RIGHT:
			t->cursor_idx = (int16_t)(t->cursor_idx + 1);
			if (t->cursor_idx == HELP_TOTAL_ENTRIES)
				t->cursor_idx = 0;
			redraw = 1;
			break;
		case HA_PICK:
			t->page_delta = 0;
			return 1;
		default:
			break;
	}

	/* Mouse: edge-detect a button release (prev was 1 or 2, current
	 * is 0). Button bit 0 (LMB) steps right, bit 1 (RMB) steps left.
	 * inputbuttons is masked to the low nibble -- the Thrustmaster
	 * top-hat bits live higher up. */
	const uint16_t cur_buttons = (uint16_t)(inputbuttons & 0x0F);
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

	return redraw ? 2 : 0;
}

// FUNCTION: TIE 0x2C7F0, TIE98 0x42F390 (task-split recovery)
static LandruTaskStepResult help_task_step(void* self) {
	HelpTask* t = (HelpTask*)self;

	if (t->phase == HELP_PHASE_RENDER) {
		const bool tie98_display = TieClassicDisplay_UsesDx5();
		if (tie98_display)
			FlightSurface_Lock();
		help_render_rows(t->cursor_idx);
		if (tie98_display) {
			FlightSurface_Unlock();
			FrontendDisplay_BlitOffscreenToRenderSurface();
			FrontendDisplay_PresentFrame();
		}
		t->phase = HELP_PHASE_POLL;
		return LANDRU_TASK_STEP_CONTINUE;
	}

	int r = help_poll_once(t);
	if (r == 1) {
		user_submodal_result = (int32_t)t->page_delta;
		return LANDRU_TASK_STEP_DONE;
	}
	if (r == 2)
		t->phase = HELP_PHASE_RENDER;
	return LANDRU_TASK_STEP_CONTINUE;
}

static const LandruTaskVtable help_task_vt = {
	.step = help_task_step,
};

void help_Push_HelpRoom_Task(int32_t start_right_col) {
	const bool tie98_display = TieClassicDisplay_UsesDx5();
	if (tie98_display)
		FlightSurface_Lock();
	/* 1. Layout. The 640x480 flight modes use a bigger top margin. */
	helpTop = tie_is_high_resolution_flight() ? 44 : 18;
	festring_setfontsize(2);
	helpBottom = helpTop + HELP_ROWS_PER_COL * (fontheight + 2);
	dropflag = 0;

	festring_setlinewrap(0);
	festring_setautofill(1);
	festring_setbound(0, 0, (int16_t)screenXRes, (int16_t)screenYRes);
	festring_setbackcolor(HELP_BG_DEFAULT);
	festring_settextcolor(HELP_TEXT_COLOUR);

	/* 2. Paint background + clamp the text region for text rendering. */
	help_paint_background();
	festring_setbound(0, 0, (int16_t)screenXRes, (int16_t)(helpBottom - 1));
	festring_setbackcolor(HELP_BG_DEFAULT);
	if (tie98_display)
		FlightSurface_Unlock();

	HelpTask* t = (HelpTask*)landru_task_push(&help_task_vt);
	if (!t)
		return;
	t->cursor_idx = start_right_col ? HELP_ROWS_PER_COL : 0;
	t->page_delta = 0;
	t->prev_buttons = 0;
	t->phase = HELP_PHASE_RENDER;
}
