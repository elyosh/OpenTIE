#include "tie/damage.h"
#ifdef TIE_MODERN
#include "tie_runtime/display/classic_display.h"
#include "tie_runtime/runtime/damage_task.h"
#endif
#include "tie/edition.h"
#include "tie/flight_surface_tie98.h"
#include "tie/frontend_display_tie98.h"

#include "tie/collide.h"
#include "tie/feinput.h"
#include "tie/festring.h"
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

/* --- Module-owned global (watdbg: damage.c) ---------------------------- */

/* Set by fediskio_loadstringdata to point into the relocated string table;
 * carries 10 C-string pointers (the SystemStringId labels). */
// GLOBAL: TIE95 0xD358C
// GLOBAL: TIE98 0x62696C
char** systemstrings;

/* --- Local helpers ---------------------------------------------------- */

enum {
	NUM_SYSTEMS = 10,
};

/* Color indices (front-end palette). */
enum {
	COLOR_BG_NORMAL = 0x44,    /* unselected row background          */
	COLOR_BG_SELECTED = 0x46,  /* highlighted row background         */
	COLOR_TEXT_NAME = 0x43,    /* default name-column text color     */
	COLOR_TEXT_NA = 0x41,      /* "N/A" (subsystem not installed)    */
	COLOR_TEXT_TIME = 0x4A,    /* "MM:SS" repair countdown          */
	COLOR_TEXT_PARTIAL = 0x4E, /* partial system health              */
	COLOR_TEXT_HEALTHY = 0x52, /* "100%" (fully operational)        */
};

/* Post-FEINPUT arrow codes: left/right/up/down are 1/2/3/4. */
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
	K_Q_LOWER = 0x71,
	K_D_LOWER = 100, /* 'd' -- this page's hotkey (damage) */
	K_F1 = 0xBB,     /* F1 scancode + 0x80 offset */
};

/* --- damage_outputsystem --- */

// FUNCTION: TIE95 0x1AD94
void damage_outputsystem(uint16_t system_id, int16_t y) {
	char buf[6];

	if ((pstate.player_craft->subsystem_active & (uint16_t)systemmask[system_id]) == 0) {
		/* Subsystem not installed on this craft. */
		festring_settextcolor(COLOR_TEXT_NA);
		buf[0] = 'N';
		buf[1] = '/';
		buf[2] = 'A';
		buf[3] = '\0';
	} else if (pstate.subsystem_health_percent[system_id] == 0) {
		/* "MM:SS" from the repair time in seconds. */
		uint8_t minutes;
		uint8_t seconds;

		festring_settextcolor(COLOR_TEXT_TIME);
		minutes = (uint8_t)(pstate.subsystem_repair_seconds[system_id] / 60);
		seconds = (uint8_t)(pstate.subsystem_repair_seconds[system_id] - minutes * 60);
		buf[0] = (char)('0' + minutes / 10);
		buf[1] = (char)(minutes - minutes / 10 * 10 + '0');
		buf[2] = ':';
		buf[3] = (char)('0' + seconds / 10);
		buf[4] = (char)(seconds - seconds / 10 * 10 + '0');
		buf[5] = '\0';
	} else if (pstate.subsystem_health_percent[system_id] == 100) {
		festring_settextcolor(COLOR_TEXT_HEALTHY);
		buf[0] = '1';
		buf[1] = '0';
		buf[2] = '0';
		buf[3] = '%';
		buf[4] = '\0';
	} else {
		/* "NN%" */
		uint8_t tens;

		festring_settextcolor(COLOR_TEXT_PARTIAL);
		tens = (uint8_t)(pstate.subsystem_health_percent[system_id] / 10);
		buf[0] = (char)('0' + tens);
		buf[1] = (char)('0' + (uint8_t)(pstate.subsystem_health_percent[system_id] - tens * 10));
		buf[2] = '%';
		buf[3] = '\0';
	}

	festring_outstring((const uint8_t*)systemstrings[system_id]);
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

// FUNCTION: TIE95 0x1ABB4
uint8_t damage_nextsystem(uint16_t cur_sys, int16_t direction) {
	int k;
	uint8_t priority_to_system[NUM_SYSTEMS];
	int16_t last_repair;
	int j;
	int16_t last_operational;

	{
		int i;

		for (i = 0; i < NUM_SYSTEMS; i++)
			priority_to_system[pstate.subsystem_repair_priority[i]] = (uint8_t)i;
	}

	last_repair = -1;

	/* -- Pass 1: systems under repair (health == 0). ----------------- */
	for (j = 0; j < NUM_SYSTEMS; j++) {
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
			int k;

			if (last_repair != -1)
				return (uint8_t)last_repair;
			/* No previous repair -- wrap into the operational group from
			 * priority 9 down. */
			for (k = NUM_SYSTEMS - 1; k >= 0; k--) {
				if (pstate.subsystem_health_percent[priority_to_system[k]])
					return priority_to_system[k];
			}
			return priority_to_system[NUM_SYSTEMS - 1];
		}

		/* Forward. */
		for (k = j + 1; k < NUM_SYSTEMS; k++) {
			if (!pstate.subsystem_health_percent[priority_to_system[k]])
				return priority_to_system[k];
		}
		for (k = 0; k < NUM_SYSTEMS; k++) {
			if (pstate.subsystem_health_percent[priority_to_system[k]])
				return priority_to_system[k];
		}
		for (k = 0; k < NUM_SYSTEMS; k++) {
			if (!pstate.subsystem_health_percent[priority_to_system[k]])
				return priority_to_system[k];
		}
		return (uint8_t)cur_sys;
	}

	/* -- Pass 2: operational systems (health != 0). ----------------- */
	/* Most recent operational system seen before cur_sys. */
	last_operational = -1;
	for (j = 0; j < NUM_SYSTEMS; j++) {
		const uint8_t sys = priority_to_system[j];
		int k;

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
		for (k = j + 1; k < NUM_SYSTEMS; k++) {
			if (pstate.subsystem_health_percent[priority_to_system[k]])
				return priority_to_system[k];
		}
		for (k = 0; k < NUM_SYSTEMS; k++) {
			if (!pstate.subsystem_health_percent[priority_to_system[k]])
				return priority_to_system[k];
		}
		for (k = 0; k < NUM_SYSTEMS; k++) {
			if (pstate.subsystem_health_percent[priority_to_system[k]])
				return priority_to_system[k];
		}
		return (uint8_t)cur_sys;
	}

	/* cur_sys matched neither pass -- should be unreachable since every
	 * system has health in {0, !=0}. Binary falls through to a zero return. */
	return 0;
}

// FUNCTION: TIE95 0x1A600
// FUNCTION: TIE98 0x414CC0
int32_t damage_damageroom(void) {
	int16_t sel_sys;
	int16_t mouse_prev;
	int16_t ret_dir;
	int render_again = 1;
#ifdef TIE_MODERN
	DamageRoomState* continuation = landru_task_top();
	sel_sys = continuation->sel_sys;
	mouse_prev = continuation->mouse_prev;
	ret_dir = continuation->ret_dir;
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
		festring_settextcolor(COLOR_TEXT_NAME);

		sel_sys = -1; /* no selection yet; set on first present row */
		mouse_prev = 0;
		ret_dir = 0;

#ifdef TIE_MODERN
		continuation->sel_sys = sel_sys;
		continuation->mouse_prev = mouse_prev;
		continuation->ret_dir = ret_dir;
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
				uint8_t priority_to_system[NUM_SYSTEMS];
				int16_t y;
				uint32_t line_step;
				int i;

				{
					int i;

					for (i = 0; i < NUM_SYSTEMS; i++)
						priority_to_system[pstate.subsystem_repair_priority[i]] = (uint8_t)i;
				}

				/* 20-line layout in 320x200, 50-line in 640x480; line spacing
				 * derived from remaining vertical space. */
				y = (flightResolution == TIE_FLIGHT_RES_SVGA || flightResolution == TIE_FLIGHT_RES_SVGA_16 ||
					 flightResolution == TIE_FLIGHT_RES_SVGA_D3D)
						? 51
						: 21;
				line_step = (screenYRes - 2 * y) / NUM_SYSTEMS;

				/* -- Draw group A: present and under repair (health == 0). ---- */
				for (i = 0; i < NUM_SYSTEMS; i++) {
					const uint8_t sys = priority_to_system[i];
					if (pstate.subsystem_health_percent[sys])
						continue;
					if ((systemmask[sys] & pstate.player_craft->subsystem_active) == 0)
						continue;

					festring_setcursor(1, y);
					if (sel_sys == -1)
						sel_sys = (int16_t)sys;
					festring_setbackcolor(
						(uint16_t)(sel_sys == (int16_t)sys ? COLOR_BG_SELECTED : COLOR_BG_NORMAL));
					damage_outputsystem(sys, y);
					y = (int16_t)(y + line_step);
				}

				/* -- Draw group B: present and operational (health != 0). ---- */
				for (i = 0; i < NUM_SYSTEMS; i++) {
					const uint8_t sys = priority_to_system[i];
					if (!pstate.subsystem_health_percent[sys])
						continue;
					if ((systemmask[sys] & pstate.player_craft->subsystem_active) == 0)
						continue;

					festring_setcursor(1, y);
					if (sel_sys == -1)
						sel_sys = (int16_t)sys;
					festring_setbackcolor(
						(uint16_t)(sel_sys == (int16_t)sys ? COLOR_BG_SELECTED : COLOR_BG_NORMAL));
					damage_outputsystem(sys, y);
					y = (int16_t)(y + line_step);
				}

				/* -- Draw group C: subsystem not installed. ----------------- */
				for (i = 0; i < NUM_SYSTEMS; i++) {
					const uint8_t sys = priority_to_system[i];
					if ((systemmask[sys] & pstate.player_craft->subsystem_active) != 0)
						continue;

					festring_setcursor(1, y);
					if (sel_sys == -1)
						sel_sys = (int16_t)sys;
					festring_setbackcolor(
						(uint16_t)(sel_sys == (int16_t)sys ? COLOR_BG_SELECTED : COLOR_BG_NORMAL));
					damage_outputsystem(sys, y);
					y = (int16_t)(y + line_step);
				}
			}
			if (TIE_DISPLAY_DX5) {
				FlightSurface_Unlock();
				FrontendDisplay_BlitOffscreenToRenderSurface();
				FrontendDisplay_PresentFrame();
			}
#ifdef TIE_MODERN
			continuation->sel_sys = sel_sys;
			continuation->mouse_prev = mouse_prev;
			continuation->ret_dir = ret_dir;
			continuation->render = false;
			return 0;
#endif
		}
		{
			enum { ACT_NONE, ACT_NEXT, ACT_PREV, ACT_TOP, ACT_EXIT } action;
			uint16_t key;
			int redraw;
			int mouse_btn;

			feinput_getrawinput();
			feinput_checkinput();
			feinput_degitterinput();
			inputdeltay = (int16_t)(inputdeltay * 2);

			key = (uint16_t)inputkey;
			action = ACT_NONE;
			redraw = 0;

			switch (key) {
				case K_LEFT:
					ret_dir = -1;
					{
#ifdef TIE_MODERN
						continuation->finished = true;
#endif
						return ret_dir;
					}
				case K_RIGHT:
					ret_dir = 1;
					{
#ifdef TIE_MODERN
						continuation->finished = true;
#endif
						return ret_dir;
					}
				case K_UP:
				case K_KP8:
					action = ACT_PREV;
					break;
				case K_DOWN:
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
						sel_sys = damage_nextsystem((uint16_t)sel_sys, (int16_t)0xFFFF);
					} while ((systemmask[sel_sys] & pstate.player_craft->subsystem_active) == 0);
					redraw = 1;
					break;
				case ACT_NEXT:
					do {
						sel_sys = damage_nextsystem((uint16_t)sel_sys, 1);
					} while ((systemmask[sel_sys] & pstate.player_craft->subsystem_active) == 0);
					redraw = 1;
					break;
				case ACT_TOP: {
					const uint8_t selected_priority = pstate.subsystem_repair_priority[sel_sys];
					int i;

					for (i = 0; i < NUM_SYSTEMS; i++) {
						const uint8_t priority = pstate.subsystem_repair_priority[i];
						if (selected_priority > priority)
							pstate.subsystem_repair_priority[i] = (uint8_t)(priority + 1);
					}
					pstate.subsystem_repair_priority[sel_sys] = 0;
				}
					inputbuttons = 0; /* clear so the mouse-release edge below doesn't retrigger */
					redraw = 1;
					break;
				case ACT_EXIT:
					ret_dir = 0;
					{
#ifdef TIE_MODERN
						continuation->finished = true;
#endif
						return ret_dir;
					}
				case ACT_NONE:
					break;
			}

			/* Mouse fallback: edge-triggered on release of btn 1 or 2 that
			 * was held last frame. Binary semantics:
			 *   LMB released -> next system (like '2'/Left)
			 *   RMB released -> promote to top (like Enter/Space) */
			mouse_btn = inputbuttons & 0xF;
			if ((mouse_prev == 1 || mouse_prev == 2) && mouse_btn == 0) {
				if (mouse_prev == 1) {
					do {
						sel_sys = damage_nextsystem((uint16_t)sel_sys, 1);
					} while ((systemmask[sel_sys] & pstate.player_craft->subsystem_active) == 0);
				} else {
					{
						const uint8_t selected_priority = pstate.subsystem_repair_priority[sel_sys];
						int i;

						for (i = 0; i < NUM_SYSTEMS; i++) {
							const uint8_t priority = pstate.subsystem_repair_priority[i];
							if (selected_priority > priority)
								pstate.subsystem_repair_priority[i] = (uint8_t)(priority + 1);
						}
						pstate.subsystem_repair_priority[sel_sys] = 0;
					}
				}
				redraw = 1;
			}
			mouse_prev = (int16_t)mouse_btn;

			render_again = redraw;
		}
#ifdef TIE_MODERN
		continuation->sel_sys = sel_sys;
		continuation->mouse_prev = mouse_prev;
		continuation->ret_dir = ret_dir;
		continuation->render = render_again != 0;
		return 0;
#endif
	}
}
