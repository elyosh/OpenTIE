#include <stddef.h>
#include <stdint.h>

#include "tie/collide.h"
#include "tie/damage.h"
#include "tie/feinput.h"
#include "tie/festring.h"
#include "tie/flight_surface_tie98.h"
#include "tie/frontend_display_tie98.h"
#include "tie/tie.h"
#include "tie/user.h" /* user_submodal_result */
#include "tie_runtime/display/classic_display.h"
#include "tie_runtime/runtime/profile.h"
#include <landru/task.h>

/* --- Module-owned global (watdbg: damage.c) ---------------------------- */

/* Set by fediskio_loadstringdata to point into the relocated string table;
 * carries 10 C-string pointers (the SystemStringId labels). */
char** systemstrings;

/* --- Local helpers ---------------------------------------------------- */

#define NUM_SYSTEMS 10

/* Color indices (front-end palette). */
#define COLOR_BG_NORMAL 0x44    /* unselected row background          */
#define COLOR_BG_SELECTED 0x46  /* highlighted row background         */
#define COLOR_TEXT_NAME 0x43    /* default name-column text color     */
#define COLOR_TEXT_NA 0x41      /* "N/A" (subsystem not installed)    */
#define COLOR_TEXT_TIME 0x4A    /* "MM:SS" repair countdown          */
#define COLOR_TEXT_PARTIAL 0x4E /* partial system health              */
#define COLOR_TEXT_HEALTHY 0x52 /* "100%" (fully operational)        */

/* Key codes observed in the binary for this room. */
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
#define K_D_LOWER 100 /* 'd' -- this page's hotkey (damage) */
#define K_F1 0xBB     /* F1 scancode + 0x80 offset */

/* Output formatters: write an ASCII-decoded field to `dst` and terminate. */
static void format_pct(char* dst, uint16_t health_percent) {
	/* "NN%\0": two digits, percent sign, nul. */
	dst[0] = (char)('0' + health_percent / 10);
	dst[1] = (char)('0' + health_percent % 10);
	dst[2] = '%';
	dst[3] = '\0';
}

static void format_time(char* dst, uint16_t ticks) {
	/* "MM:SS\0" from a tick count reinterpreted as minutes:seconds. */
	const uint8_t minutes = (uint8_t)(ticks / 60);
	const uint8_t seconds = (uint8_t)(ticks % 60);
	dst[0] = (char)('0' + minutes / 10);
	dst[1] = (char)('0' + minutes % 10);
	dst[2] = ':';
	dst[3] = (char)('0' + seconds / 10);
	dst[4] = (char)('0' + seconds % 10);
	dst[5] = '\0';
}

static void build_priority_to_system(uint8_t* priority_to_system) {
	for (int i = 0; i < NUM_SYSTEMS; i++)
		priority_to_system[pstate.subsystem_repair_priority[i]] = (uint8_t)i;
}

/* Promote `sel_sys` to priority 0: bump every higher-priority system
 * down one slot, then set the selection to 0. Matches the binary's
 * Enter/Space/RMB code. */
static void promote_to_top(uint16_t sel_sys) {
	const uint8_t selected_priority = pstate.subsystem_repair_priority[sel_sys];
	for (int i = 0; i < NUM_SYSTEMS; i++) {
		const uint8_t priority = pstate.subsystem_repair_priority[i];
		if (selected_priority > priority)
			pstate.subsystem_repair_priority[i] = (uint8_t)(priority + 1);
	}
	pstate.subsystem_repair_priority[sel_sys] = 0;
}

/* --- damage_outputsystem --- */

// FUNCTION: TIE 0x1AD94
void damage_outputsystem(SystemStringId system_id, int16_t y) {
	const uint16_t idx = (uint16_t)system_id;
	char buf[8];

	if ((systemmask[idx] & pstate.player_craft->subsystem_active) == 0) {
		/* Subsystem not installed on this craft. */
		festring_settextcolor(COLOR_TEXT_NA);
		buf[0] = 'N';
		buf[1] = '/';
		buf[2] = 'A';
		buf[3] = '\0';
	} else {
		const int16_t health_percent = (int16_t)pstate.subsystem_health_percent[idx];
		if (health_percent == 0) {
			festring_settextcolor(COLOR_TEXT_TIME);
			format_time(buf, pstate.subsystem_repair_seconds[idx]);
		} else if (health_percent == 100) {
			festring_settextcolor(COLOR_TEXT_HEALTHY);
			buf[0] = '1';
			buf[1] = '0';
			buf[2] = '0';
			buf[3] = '%';
			buf[4] = '\0';
		} else {
			festring_settextcolor(COLOR_TEXT_PARTIAL);
			format_pct(buf, pstate.subsystem_health_percent[idx]);
		}
	}

	festring_outstring((const uint8_t*)systemstrings[idx]);
	if (outchar)
		outchar('\n');
	festring_setcursor(1, y);
	festring_outstringright((const uint8_t*)buf);
}

/* --- damage_nextsystem --- */

/* Helper: does system `s` satisfy the "same group as cur_sys" predicate?
 * Group predicates: under repair (health==0) vs operational (health!=0). Kept inline
 * for clarity; the binary doesn't factor this out but the structure is the
 * same.
 *
 * The algorithm walks the priority order twice:
 *   pass 1: systems under repair (health == 0)
 *   pass 2: operational systems  (health != 0)
 * cur_sys lives in exactly one of those passes. Once the matching pass
 * finds cur_sys, direction determines:
 *   +1: return the next system in the pass, falling through to the other
 *       pass if at the end, falling back to the first pass once more, and
 *       finally returning cur_sys itself if everything else failed.
 *   -1: return the previously-seen system in the same pass. If none was
 *       seen (cur_sys was first), scan the other
 *       pass backward from priority 9; if still nothing, return the final system.
 */

// FUNCTION: TIE 0x1ABB4
uint8_t damage_nextsystem(uint16_t cur_sys, int16_t direction) {
	uint8_t priority_to_system[NUM_SYSTEMS];
	build_priority_to_system(priority_to_system);

	int16_t last_repair = -1;

	/* -- Pass 1: systems under repair (health == 0). ----------------- */
	for (int j = 0; j < NUM_SYSTEMS; j++) {
		const uint8_t sys = priority_to_system[j];
		if (pstate.subsystem_health_percent[sys])
			continue;

		if (sys != cur_sys) {
			last_repair = (int16_t)sys;
			continue;
		}

		/* Matched cur_sys inside pass 1. */
		if ((uint16_t)direction == 0xFFFF) {
			/* Backward. */
			if (last_repair != -1)
				return (uint8_t)last_repair;
			/* No previous repair -- wrap into the operational group from
			 * priority 9 down. */
			for (int k = NUM_SYSTEMS - 1; k >= 0; k--) {
				if (pstate.subsystem_health_percent[priority_to_system[k]])
					return priority_to_system[k];
			}
			return priority_to_system[NUM_SYSTEMS - 1];
		}

		/* Forward. */
		for (int k = j + 1; k < NUM_SYSTEMS; k++) {
			if (!pstate.subsystem_health_percent[priority_to_system[k]])
				return priority_to_system[k];
		}
		for (int k = 0; k < NUM_SYSTEMS; k++) {
			if (pstate.subsystem_health_percent[priority_to_system[k]])
				return priority_to_system[k];
		}
		for (int k = 0; k < NUM_SYSTEMS; k++) {
			if (!pstate.subsystem_health_percent[priority_to_system[k]])
				return priority_to_system[k];
		}
		return (uint8_t)cur_sys;
	}

	/* -- Pass 2: operational systems (health != 0). ----------------- */
	/* Most recent operational system seen before cur_sys. */
	int16_t last_operational = -1;
	for (int j = 0; j < NUM_SYSTEMS; j++) {
		const uint8_t sys = priority_to_system[j];
		if (!pstate.subsystem_health_percent[sys])
			continue;

		if (sys != cur_sys) {
			last_operational = (int16_t)sys;
			continue;
		}

		/* Matched cur_sys inside pass 2. */
		if ((uint16_t)direction == 0xFFFF) {
			/* Backward. */
			if (last_operational != -1)
				return (uint8_t)last_operational;
			return priority_to_system[NUM_SYSTEMS - 1];
		}

		/* Forward. */
		for (int k = j + 1; k < NUM_SYSTEMS; k++) {
			if (pstate.subsystem_health_percent[priority_to_system[k]])
				return priority_to_system[k];
		}
		for (int k = 0; k < NUM_SYSTEMS; k++) {
			if (!pstate.subsystem_health_percent[priority_to_system[k]])
				return priority_to_system[k];
		}
		for (int k = 0; k < NUM_SYSTEMS; k++) {
			if (pstate.subsystem_health_percent[priority_to_system[k]])
				return priority_to_system[k];
		}
		return (uint8_t)cur_sys;
	}

	/* cur_sys matched neither pass -- should be unreachable since every
	 * system has health in {0, !=0}. Binary falls through to a zero return. */
	return 0;
}

typedef enum {
	DAMAGE_PHASE_RENDER = 0,
	DAMAGE_PHASE_POLL,
} DamagePhase;

typedef struct DamageTask {
	int16_t sel_sys;    /* -1 until first present row picks it up */
	int16_t mouse_prev; /* edge-detect on LMB/RMB release */
	int16_t ret_dir;
	DamagePhase phase;
} DamageTask;

static void damage_render_page(int16_t* sel_sys) {
	uint8_t priority_to_system[NUM_SYSTEMS];
	build_priority_to_system(priority_to_system);

	/* 20-line layout in 320x200, 50-line in 640x480; line spacing
	 * derived from remaining vertical space. */
	int16_t y = tie_is_high_resolution_flight() ? 51 : 21;
	const uint32_t line_step = (uint32_t)(screenYRes - 2 * y) / NUM_SYSTEMS;

	/* -- Draw group A: present and under repair (health == 0). ---- */
	for (int i = 0; i < NUM_SYSTEMS; i++) {
		const uint8_t sys = priority_to_system[i];
		if (pstate.subsystem_health_percent[sys])
			continue;
		if ((systemmask[sys] & pstate.player_craft->subsystem_active) == 0)
			continue;

		festring_setcursor(1, y);
		if (*sel_sys == -1)
			*sel_sys = (int16_t)sys;
		festring_setbackcolor((uint16_t)(*sel_sys == (int16_t)sys ? COLOR_BG_SELECTED : COLOR_BG_NORMAL));
		damage_outputsystem((SystemStringId)sys, y);
		y = (int16_t)(y + line_step);
	}

	/* -- Draw group B: present and operational (health != 0). ---- */
	for (int i = 0; i < NUM_SYSTEMS; i++) {
		const uint8_t sys = priority_to_system[i];
		if (!pstate.subsystem_health_percent[sys])
			continue;
		if ((systemmask[sys] & pstate.player_craft->subsystem_active) == 0)
			continue;

		festring_setcursor(1, y);
		if (*sel_sys == -1)
			*sel_sys = (int16_t)sys;
		festring_setbackcolor((uint16_t)(*sel_sys == (int16_t)sys ? COLOR_BG_SELECTED : COLOR_BG_NORMAL));
		damage_outputsystem((SystemStringId)sys, y);
		y = (int16_t)(y + line_step);
	}

	/* -- Draw group C: subsystem not installed. ----------------- */
	for (int i = 0; i < NUM_SYSTEMS; i++) {
		const uint8_t sys = priority_to_system[i];
		if ((systemmask[sys] & pstate.player_craft->subsystem_active) != 0)
			continue;

		festring_setcursor(1, y);
		if (*sel_sys == -1)
			*sel_sys = (int16_t)sys;
		festring_setbackcolor((uint16_t)(*sel_sys == (int16_t)sys ? COLOR_BG_SELECTED : COLOR_BG_NORMAL));
		damage_outputsystem((SystemStringId)sys, y);
		y = (int16_t)(y + line_step);
	}
}

/* Single input-poll iteration. Returns 1 if exit-class fires (caller
 * latches user_submodal_result), 2 if selection moved (page needs
 * redraw), 0 if nothing was consumed. */
static int damage_poll_once(DamageTask* t) {
	feinput_getrawinput();
	feinput_checkinput();
	feinput_degitterinput();
	inputdeltay = (int16_t)(inputdeltay * 2);

	const uint16_t key = (uint16_t)inputkey;
	enum { ACT_NONE, ACT_NEXT, ACT_PREV, ACT_TOP, ACT_EXIT } action = ACT_NONE;
	int redraw = 0;

	switch (key) {
		case K_UP:
			t->ret_dir = -1;
			return 1;
		case K_DOWN:
			t->ret_dir = 1;
			return 1;
		case K_RIGHT:
		case K_KP8:
			action = ACT_PREV;
			break;
		case K_LEFT:
		case K_KP2:
			action = ACT_NEXT;
			break;
		case K_ENTER:
		case K_SPACE:
			action = ACT_TOP;
			break;
		case K_ESC:
		case K_Q_UPPER:
		case K_Q_LOWER:
		case K_D_LOWER:
		case K_F1:
			action = ACT_EXIT;
			break;
		default:
			break;
	}

	switch (action) {
		case ACT_PREV:
			do {
				t->sel_sys = damage_nextsystem((uint16_t)t->sel_sys, (int16_t)0xFFFF);
			} while ((systemmask[t->sel_sys] & pstate.player_craft->subsystem_active) == 0);
			redraw = 1;
			break;
		case ACT_NEXT:
			do {
				t->sel_sys = damage_nextsystem((uint16_t)t->sel_sys, 1);
			} while ((systemmask[t->sel_sys] & pstate.player_craft->subsystem_active) == 0);
			redraw = 1;
			break;
		case ACT_TOP:
			promote_to_top((uint16_t)t->sel_sys);
			inputbuttons = 0; /* clear so the mouse-release edge below doesn't retrigger */
			redraw = 1;
			break;
		case ACT_EXIT:
			t->ret_dir = 0;
			return 1;
		case ACT_NONE:
			break;
	}

	/* Mouse fallback: edge-triggered on release of btn 1 or 2 that
	 * was held last frame. Binary semantics:
	 *   LMB released -> next system (like '2'/Left)
	 *   RMB released -> promote to top (like Enter/Space) */
	const int mouse_btn = inputbuttons & 0xF;
	if ((t->mouse_prev == 1 || t->mouse_prev == 2) && mouse_btn == 0) {
		if (t->mouse_prev == 1) {
			do {
				t->sel_sys = damage_nextsystem((uint16_t)t->sel_sys, 1);
			} while ((systemmask[t->sel_sys] & pstate.player_craft->subsystem_active) == 0);
		} else {
			promote_to_top((uint16_t)t->sel_sys);
		}
		redraw = 1;
	}
	t->mouse_prev = (int16_t)mouse_btn;

	return redraw ? 2 : 0;
}

// FUNCTION: TIE 0x1A600, TIE98 0x414CC0 (task-split recovery)
static LandruTaskStepResult damage_task_step(void* self) {
	DamageTask* t = (DamageTask*)self;

	if (t->phase == DAMAGE_PHASE_RENDER) {
		const bool tie98_display = TieClassicDisplay_UsesDx5();
		if (tie98_display)
			FlightSurface_Lock();
		damage_render_page(&t->sel_sys);
		if (tie98_display) {
			FlightSurface_Unlock();
			FrontendDisplay_BlitOffscreenToRenderSurface();
			FrontendDisplay_PresentFrame();
		}
		t->phase = DAMAGE_PHASE_POLL;
		return LANDRU_TASK_STEP_CONTINUE;
	}

	int r = damage_poll_once(t);
	if (r == 1) {
		user_submodal_result = (int32_t)t->ret_dir;
		return LANDRU_TASK_STEP_DONE;
	}
	if (r == 2)
		t->phase = DAMAGE_PHASE_RENDER;
	return LANDRU_TASK_STEP_CONTINUE;
}

static const LandruTaskVtable damage_task_vt = {
	.step = damage_task_step,
};

void damage_Push_DamageRoom_Task(void) {
	dropflag = 1;
	festring_setlinewrap(0);
	festring_setautofill(1);
	festring_setfontsize(1);
	festring_setbound(0, 0, (int16_t)screenXRes, (int16_t)screenYRes);
	festring_setbackcolor(COLOR_BG_NORMAL);
	festring_settextcolor(COLOR_TEXT_NAME);

	DamageTask* t = (DamageTask*)landru_task_push(&damage_task_vt);
	if (!t)
		return;
	t->sel_sys = -1; /* no selection yet; set on first present row */
	t->mouse_prev = 0;
	t->ret_dir = 0;
	t->phase = DAMAGE_PHASE_RENDER;
}
