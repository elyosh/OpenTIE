/* Wingman command menu for USER_inflightinfo. Hotkeys are stored at byte six
 * of each STRINGS.DAT entry. Returns -1/+1 for adjacent screens and +2 for
 * cancellation; selection updates inputkey and raises dropflag. */

#include <stdint.h>

#include "tie/feinput.h"
#include "tie/festring.h"
#include "tie/flight_surface_tie98.h"
#include "tie/frontend_display_tie98.h"
#include "tie/tie.h"
#include "tie/user.h" /* user_submodal_result */
#include "tie/wingman.h"
#include "tie_runtime/display/classic_display.h"
#include "tie_runtime/runtime/profile.h"
#include <landru/task.h>

#define NUM_WINGMAN_CMDS 10

/* Row background colors (match DAMAGE/MSGROOM rooms in the same family). */
#define COLOR_BG_NORMAL 0x44
#define COLOR_BG_SELECTED 0x46
#define COLOR_TEXT_DEFAULT 0x52

/* Keyboard codes (post-remap). Arrows 1..4 ordering is UP/DOWN/RIGHT/LEFT. */
#define K_UP 0x01
#define K_DOWN 0x02
#define K_RIGHT 0x03
#define K_LEFT 0x04
#define K_ENTER 0x0D
#define K_ESC 0x1B
#define K_SPACE 0x20
#define K_KP2 0x32 /* keypad '2' (down) */
#define K_KP8 0x38 /* keypad '8' (up)   */
#define K_Q_UPPER 0x51
#define K_Q_LOWER 0x71
#define K_W_LOWER 0x77 /* this page's own hotkey (toggles wingman off)  */
#define K_F1 0xBB      /* F1 scancode + 0x80 (FEINPUT extended-remap)   */

/* Array of 10 string pointers. Populated in fediskio_loadstringdata. */
const char** wingmanstrings;

/* Is this inputkey one of the 10 direct-select command hotkeys?
 *
 * The original binary decides this with nested range compares rather than a
 * table lookup; the enumerated set matches exactly: {A, B, C, E, G, H, I,
 * R, S, W}. These are the uppercase hotkeys baked into STRINGS.DAT. Pressing
 * one selects the current row but leaves inputkey alone so USER_inflightinfo
 * can route it to the same action the letter normally triggers. */
static int is_command_hotkey(uint16_t key) {
	if (key >= 'A' && key <= 'C')
		return 1; /* 0x41..0x43 */
	if (key == 'E')
		return 1; /* 0x45       */
	if (key >= 'G' && key <= 'I')
		return 1; /* 0x47..0x49 */
	if (key == 'R' || key == 'S')
		return 1; /* 0x52/0x53  */
	if (key == 'W')
		return 1; /* 0x57       */
	return 0;
}

typedef enum {
	WINGMAN_PHASE_RENDER = 0,
	WINGMAN_PHASE_POLL,
} WingmanPhase;

typedef struct WingmanTask {
	int16_t selected_idx;
	int16_t prev_buttons;
	int16_t ret_delta;
	WingmanPhase phase;
} WingmanTask;

static void wingman_render_page(int16_t selected_idx) {
	/* 20-line visible grid in 320x200, 50-line in the 640x480 modes. */
	const int16_t margin = tie_is_high_resolution_flight() ? 51 : 21;
	const uint32_t row_spacing = (uint32_t)(screenYRes - 2 * margin) / NUM_WINGMAN_CMDS;

	int16_t y = margin;
	for (int i = 0; i < NUM_WINGMAN_CMDS; i++) {
		festring_setbackcolor((uint16_t)(i == selected_idx ? COLOR_BG_SELECTED : COLOR_BG_NORMAL));
		festring_setcursor(1, y);
		festring_outstring((const uint8_t*)wingmanstrings[i]);
		outchar('\n');
		y = (int16_t)(y + row_spacing);
	}
}

/* Single input-poll iteration. 1 = exit fired, 2 = selection moved
 * (page redraw needed), 0 = no input. */
static int wingman_poll_once(WingmanTask* t) {
	feinput_getrawinput();
	feinput_checkinput();
	feinput_degitterinput();
	inputdeltay = (int16_t)(inputdeltay * 2);

	const uint16_t key = (uint16_t)inputkey;
	int redraw = 0;

	if (key == K_UP) {
		t->ret_delta = -1;
		return 1;
	} else if (key == K_DOWN) {
		t->ret_delta = 1;
		return 1;
	} else if (key == K_RIGHT || key == K_KP8) {
		/* Move up (wrap 0 <-> 9). */
		t->selected_idx = (int16_t)(t->selected_idx ? t->selected_idx - 1 : NUM_WINGMAN_CMDS - 1);
		redraw = 1;
	} else if (key == K_LEFT || key == K_KP2) {
		/* Move down (wrap). */
		t->selected_idx = (int16_t)((t->selected_idx + 1) % NUM_WINGMAN_CMDS);
		redraw = 1;
	} else if (key == K_ENTER || key == K_SPACE) {
		/* Select current row -- forward its hotkey letter. Keyboard
		 * select uses offset +7 (matches retail's separate keyboard
		 * hotkey), distinct from the mouse right-click path below
		 * which uses +6. */
		inputkey = (int16_t)(int8_t)wingmanstrings[t->selected_idx][7];
		t->ret_delta = 0;
		return 1;
	} else if (key == K_ESC || key == K_Q_UPPER || key == K_Q_LOWER || key == K_W_LOWER || key == K_F1) {
		t->ret_delta = 2;
		return 1;
	} else if (is_command_hotkey(key)) {
		/* Direct-select: leave inputkey as the typed letter so the
		 * caller can dispatch on it (same contract as Enter above). */
		t->ret_delta = 0;
		return 1;
	}

	/* Mouse edge-trigger on release of buttons 1 or 2. */
	const int mouse_btn = inputbuttons & 0xF;
	if ((t->prev_buttons == 1 || t->prev_buttons == 2) && mouse_btn == 0) {
		if (t->prev_buttons == 1) {
			t->selected_idx = (int16_t)((t->selected_idx + 1) % NUM_WINGMAN_CMDS);
			redraw = 1;
		} else {
			inputkey = (int16_t)(int8_t)wingmanstrings[t->selected_idx][6];
			t->ret_delta = 0;
			t->prev_buttons = (int16_t)mouse_btn;
			return 1;
		}
	}
	t->prev_buttons = (int16_t)mouse_btn;

	return redraw ? 2 : 0;
}

// FUNCTION: TIE 0x61F70, TIE98 0x499310 (task-split recovery)
static LandruTaskStepResult wingman_task_step(void* self) {
	WingmanTask* t = (WingmanTask*)self;

	if (t->phase == WINGMAN_PHASE_RENDER) {
		const bool tie98_display = TieClassicDisplay_UsesDx5();
		if (tie98_display)
			FlightSurface_Lock();
		wingman_render_page(t->selected_idx);
		if (tie98_display) {
			FlightSurface_Unlock();
			FrontendDisplay_BlitOffscreenToRenderSurface();
			FrontendDisplay_PresentFrame();
		}
		t->phase = WINGMAN_PHASE_POLL;
		return LANDRU_TASK_STEP_CONTINUE;
	}

	int r = wingman_poll_once(t);
	if (r == 1) {
		user_submodal_result = (int32_t)t->ret_delta;
		return LANDRU_TASK_STEP_DONE;
	}
	if (r == 2)
		t->phase = WINGMAN_PHASE_RENDER;
	return LANDRU_TASK_STEP_CONTINUE;
}

static const LandruTaskVtable wingman_task_vt = {
	.step = wingman_task_step,
};

void wingman_Push_WingmanRoom_Task(void) {
	dropflag = 1;
	festring_setlinewrap(0);
	festring_setautofill(1);
	festring_setfontsize(1);
	festring_setbound(0, 0, (int16_t)screenXRes, (int16_t)screenYRes);
	festring_setbackcolor(COLOR_BG_NORMAL);
	festring_settextcolor(COLOR_TEXT_DEFAULT);

	WingmanTask* t = (WingmanTask*)landru_task_push(&wingman_task_vt);
	if (!t)
		return;
	t->selected_idx = 0;
	t->prev_buttons = 0;
	t->ret_delta = 0;
	t->phase = WINGMAN_PHASE_RENDER;
}
