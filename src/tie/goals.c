#include <stdint.h>

#include "tie/create.h" /* diffmask, fgdiffmask, genusconvert, familyconvert */
#include "tie/feinput.h"
#include "tie/festring.h"
#include "tie/flight_surface_tie98.h"
#include "tie/frontend_display_tie98.h"
#include "tie/goals.h"
#include "tie/mission.h" /* RUNTIME_MissionState */
#include "tie/score.h"
#include "tie/shipext.h" /* EFGStruct, MissionFile */
#include "tie/spec.h"
#include "tie/sys2.h"
#include "tie/tie.h"
#include "tie/user.h" /* user_submodal_result */
#include "tie_runtime/display/classic_display.h"
#include "tie_runtime/runtime/profile.h"
#include <landru/task.h>

/* --- External globals populated by fediskio_loadstringdata ----------- */

/* All eight *string pointers below are singletons (each a char* to a single
 * string in stringdata_buf). The *strings variants are base addresses of
 * 2..30-entry char* arrays also inside stringdata_buf. */
void* condstrings;         /* const char *[21] */
void* condverbstrings;     /* const char *[20] */
void* percentstrings;      /* const char *[16] */
void* goaloperatorstrings; /* const char *[2]  (GOP_AND=0, GOP_OR=1)   */
void* goaltitlestrings;    /* const char *[9]  (3 cats x 3 status)     */
void* goalescapestr;       /* const char *     "[ESC]" banner          */
void* goal_of_string;      /* const char *     " of "                  */
void* goal_ofall_string;   /* const char *     " of all "              */
void* goalskillstrings;    /* const char *[6]  skill-level names       */
void* goal_group_string;   /* const char *     " group "               */
void* goalaistrings;       /* const char *[30] AI-order names          */
void* goal_allbut_string;  /* const char *     "all but "              */
void* goalsidestrings;     /* const char *[3]  rebel/imperial/craft    */
void* goal_and_string;     /* const char *     " and "                 */
void* goalfamilystrings;   /* const char *[7]  family-category names   */
void* goal_comma_string;   /* const char *     ", "                    */
void* goalgenusstrings;    /* const char *[16] genus-category names    */
void* goalallfgstring;     /* const char *     "all FG"                */

/* buoy/navigation names (species 70..84) come from panelrts.c globals. */
#include "tie/panelrts.h"

/* --- Module-owned globals (watdbg: goals.c) -------------------------- */

/* "---" placeholder printed by outputgoal when target_type in {7,10}
 * (the condstr0 alias at 0xD4C78). Four bytes including the NUL. */
static const char condstr0[4] = { '-', '-', '-', '\0' };

/* Status-priority display order: {completed, incomplete, failed}. The room
 * iterates status_prio=0..2, matching goal.status against goalstatusorder[j]
 * to decide whether to emit the goal at that row. */
static const uint8_t goalstatusorder[3] = { 2, 4, 1 };

/* Palette indices for status text color. Category-subcond calls use
 * goalstatuscolor[prio] + 1 (one shade lighter); per-FG calls use the base
 * value. The matching backgrounds live in the front-end palette. */
static const uint8_t goalstatuscolor[3] = { 0x4A, 0x4E, 0x52 };

/* 21-entry LUT indexed by condition-code (0..20). Non-zero (==5) shifts
 * the verb into a different tense slot in condverbstrings, used for
 * conditions that only make sense at mission-end ("complete", "captured",
 * "picked up"). Raw values mirror the original data (_tenseflag at
 * 0xD4C82). */
static const uint8_t tenseflag[21] = { 0, 5, 0, 0, 0, 0, 0, 0, 5, 0, 0, 0, 5, 0, 0, 0, 0, 0, 0, 0, 0 };

int32_t goalsTop;
int32_t goalsBottom;
uint8_t showbonusgoals;

/* ====================================================================
 * goals_checkidflag
 * ==================================================================== */

// FUNCTION: TIE 0x2C6BC
uint8_t goals_checkidflag(uint16_t fg_index) { return fgstatus[fg_index].cond_id[4].detail; }

/* ====================================================================
 * goals_checkwrap
 * ==================================================================== */

uint8_t goals_checkwrap(const uint8_t* s) {
	const uint16_t width = (uint16_t)sys2_calclength(s);
	if ((uint16_t)cursorx + (uint32_t)width <= (uint32_t)screenXRes - 11u)
		return 0;

	if (outchar)
		outchar('\n');
	festring_setcursor(6, cursory);
	return fontheight;
}

/* ====================================================================
 * goals_outputspeciesname
 * ==================================================================== */

// FUNCTION: TIE 0x2C6D8
uint8_t goals_outputspeciesname(uint16_t species_idx, int16_t plural_flag) {
	const uint8_t* name_ptr;
	const uint16_t spec_num = spec_getspecnum(species_idx);

	if (spec_num == 0xFF) {
		/* Species not in spec_data[]; fall back to the buoy/navigation
		 * strings. Only species 70..84 are valid indices into buoystr[15].
		 * The binary's bounds check (cmp edx,0x46 / cmp edx,0x54) is
		 * compile-time dead because both jumps land back in the buoystr
		 * path -- out-of-range species yield undefined reads there. We
		 * match the shipped behavior verbatim. */
		name_ptr = (const uint8_t*)(((char**)buoystr)[species_idx - 70]);
	} else {
		name_ptr = (const uint8_t*)spec_name_ptrs[spec_num];
	}

	const uint8_t wrap = goals_checkwrap(name_ptr);
	festring_outstring(name_ptr);
	if (outchar)
		outchar(plural_flag ? 's' : ' ');
	return wrap;
}

/* Render one localized goal line for a target, condition, status, and
 * quantifier. Returns the accumulated vertical space added by wrapping. */

// FUNCTION: TIE 0x2BF50
int32_t goals_outputgoal(uint16_t target, uint16_t cond, int16_t target_type, uint16_t status, uint16_t op) {
	int32_t total = fontheight; /* accumulator starts at fontheight */
	uint16_t tense_offset = 10;

	if (op == 4 || op == 14 || op == 9)
		tense_offset = 0;

	/* ----- target_type == 1: a specific flight group --------------- */
	if (target_type == 1) {
		if (op == 6) {
			/* "species FG_name N" -- craft-specific reference. */
			total += goals_outputspeciesname(fg_array[target].species, 0);
			festring_outstring((const uint8_t*)&fg_array[target]);
			if (outchar)
				outchar(' ');
			const int ch = fgstatus[target].cond_id[4].detail ? (fg_array[target].special_craft + '1') : '?';
			if (outchar)
				outchar(ch);
			tense_offset = 0;
		} else if (op == 7) {
			/* "all but species FG_name N". */
			festring_outstring((const uint8_t*)goal_allbut_string);
			total += goals_outputspeciesname(fg_array[target].species, 0);
			festring_outstring((const uint8_t*)&fg_array[target]);
			if (outchar)
				outchar(' ');
			if (fgstatus[target].cond_id[4].detail) {
				if (outchar)
					outchar(fg_array[target].special_craft + '1');
			} else {
				if (outchar)
					outchar('?');
			}
			/* tense_offset keeps whatever op/default set it to. */
		} else {
			/* Generic flight-group reference. Count <= 1 -> single craft,
			 * just print "species FG_name". Otherwise "X%% of species of
			 * group FG_name". */
			const EFGStruct* fg_ptr = &fg_array[target];
			if (fgstatus[target].cond[0].count <= 1u) {
				total += goals_outputspeciesname(fg_array[target].species, 0);
				festring_outstring((const uint8_t*)fg_ptr);
				tense_offset = 0;
			} else {
				festring_outstring(((const uint8_t**)percentstrings)[op]);
				festring_outstring((const uint8_t*)goal_of_string);
				total += goals_outputspeciesname(fg_array[target].species, 0);
				festring_outstring((const uint8_t*)goal_group_string);
				festring_outstring((const uint8_t*)fg_ptr);
				tense_offset = (op == 5 || op == 8) ? 10 : 0;
			}
		}
	}
	/* ----- target_type == 8: all FGs in set = target --------------- */
	else if (target_type == 8) {
		festring_outstring(((const uint8_t**)percentstrings)[op]);
		festring_outstring((const uint8_t*)goal_of_string);
		festring_outstring((const uint8_t*)goalallfgstring);
		if (outchar)
			outchar(' ');

		/* First pass: count matching FGs to decide " and " placement and
		 * tense_offset (single match -> 0, multiple -> 10). */
		uint16_t in_set = 0;
		for (uint16_t i = 0; i < (uint16_t)mission_file_header.num_fg; i++) {
			if (fg_array[i].set == (uint8_t)target)
				in_set++;
		}
		tense_offset = (in_set == 1) ? 0 : 10;

		/* Second pass: emit each FG, with ", " / " and " separators. */
		for (uint16_t j = 0; j < (uint16_t)mission_file_header.num_fg; j++) {
			if (fg_array[j].set != (uint8_t)target)
				continue;

			if (fg_array[j].count <= 1) {
				/* Single-craft FG: "species FG_name". */
				total += goals_outputspeciesname(fg_array[j].species, 0);
				total += goals_checkwrap((const uint8_t*)&fg_array[j]);
				festring_outstring((const uint8_t*)&fg_array[j]);
			} else {
				/* Multi-craft FG: "species group FG_name". */
				total += goals_outputspeciesname(fg_array[j].species, 0);
				total += goals_checkwrap((const uint8_t*)goal_group_string);
				festring_outstring((const uint8_t*)goal_group_string);
				total += goals_checkwrap((const uint8_t*)&fg_array[j]);
				festring_outstring((const uint8_t*)&fg_array[j]);
				tense_offset = 10;
			}

			/* Separator between entries: penultimate -> " and " with
			 * optional leading space when no wrap occurred; preceding ->
			 * ", "; last (zero remaining) -> nothing. */
			if (--in_set == 1) {
				const uint8_t wrap = goals_checkwrap((const uint8_t*)goal_and_string);
				total += wrap;
				if (wrap == 0 && outchar)
					outchar(' ');
				festring_outstring((const uint8_t*)goal_and_string);
			} else if (in_set > 1) {
				festring_outstring((const uint8_t*)goal_comma_string);
			}
			/* in_set == 0 (last entry): no separator. */
		}
	}
	/* ----- Category targets (species/genus/family/side/ai/skill) --- */
	else {
		festring_outstring(((const uint8_t**)percentstrings)[op]);
		festring_outstring((const uint8_t*)goal_ofall_string);

		const uint8_t* cat_name = NULL;
		switch (target_type) {
			case 2:
				/* Species-category (plural): species name with trailing 's'. */
				total += goals_outputspeciesname(target + 1, 1);
				break;
			case 3:
				cat_name = ((const uint8_t**)goalgenusstrings)[genusconvert[target]];
				break;
			case 4:
				cat_name = ((const uint8_t**)goalfamilystrings)[familyconvert[target]];
				break;
			case 5:
				if (target < 2u) {
					cat_name = ((const uint8_t**)goalsidestrings)[target];
				} else {
					/* Third-party side (SIDE >= 2). Each entry in
					 * mission_file_header.mission.neutral_name is a 12-byte
					 * string; if the first byte is '1' we skip it. Name is then
					 * followed by goalsidestrings[2] ("Craft"). */
					const char* side_name = mission_file_header.mission.neutral_name[target - 2];
					if ((uint8_t)side_name[0] == '1')
						side_name++;
					festring_outstring((const uint8_t*)side_name);
					cat_name = ((const uint8_t**)goalsidestrings)[2];
				}
				break;
			case 6:
				cat_name = ((const uint8_t**)goalaistrings)[target];
				break;
			case 9:
				cat_name = ((const uint8_t**)goalskillstrings)[target];
				break;
			case 11:
				cat_name = (const uint8_t*)goalallfgstring;
				break;
			case 7:
			case 8:
			case 10:
				/* Empty target: "---" placeholder. */
				cat_name = (const uint8_t*)condstr0;
				break;
			default:
				break;
		}

		if (cat_name)
			festring_outstring(cat_name);
	}

	/* ----- Common tail: verb + condition clause ------------------- */
	{
		const uint32_t verb_idx = (uint32_t)tenseflag[cond] + (uint32_t)status + (uint32_t)tense_offset;
		const uint8_t* verb = ((const uint8_t**)condverbstrings)[verb_idx];

		const uint8_t verb_wrap = goals_checkwrap(verb);
		total += verb_wrap;
		if (verb_wrap == 0 && outchar)
			outchar(' ');
		festring_outstring(verb);
	}

	{
		const uint8_t* clause = ((const uint8_t**)condstrings)[cond];
		total += goals_checkwrap(clause);
		festring_outstring(clause);
	}

	if (outchar)
		outchar('\n');
	return total;
}

/* Scrollable primary, secondary, bonus, and flight-group objectives display.
 * Mission-end conditions remain incomplete until end_flag is set. Returns
 * -1/0/+1 for previous mission, exit, or next mission navigation. */

/* Helper to emit the (category, status_prio) title centered at `y`. */
static int16_t emit_goal_title(uint16_t category, uint16_t status_prio, int16_t y) {
	festring_setcursor(0, y);
	if (outchar)
		outchar('\n');
	festring_setcursor(0, y);
	festring_settextcolor(0x46);
	festring_outstringcenter(((const uint8_t**)goaltitlestrings)[3u * category + status_prio]);
	return (int16_t)fontheight;
}

/* Helper: apply the cond==9 late-complete clamp. Mirrors the three copies
 * of the check in the binary. */
static uint16_t clamp_late_complete(uint16_t status, uint16_t cond, uint16_t cat_complete_cache) {
	if (cond == 9 && cat_complete_cache != 1 && status == 1)
		return 4;
	return status;
}

typedef enum {
	GOALS_PHASE_RENDER = 0,
	GOALS_PHASE_POLL,
} GoalsPhase;

typedef struct GoalsTask {
	int16_t scroll_y;
	int16_t content_height;
	int16_t nav_code;
	GoalsPhase phase;
} GoalsTask;

/* Walk all goals + per-FG entries, emit a single page-render of the
 * goals view at scroll_y. Updates *out_content_height in pixels.
 * Identical to the original outer-loop body's render block. */
static void goals_render_page(int16_t scroll_y, int16_t* out_content_height) {
	int16_t cur_y = scroll_y;
	festring_settextcolor(0x4E);

	for (uint16_t category = 0; category < 3; category++) {
		for (uint16_t status_prio = 0; status_prio < 3; status_prio++) {
			int16_t title_pending = 1;

			/* -- Category-level mission status + complete cache --- */
			uint16_t goal_status;
			uint16_t cat_complete_cache;
			if (category == 0) {
				goal_status = mission.primary_global;
				cat_complete_cache = mission.primary_complete;
			} else if (category == 1) {
				goal_status = mission.secondary_global;
				cat_complete_cache = mission.secondary_complete;
			} else { /* category == 2 (bonus) */
				goal_status = mission.bonus_global;
				if (!showbonusgoals && goal_status != 1)
					goal_status = 0;
				cat_complete_cache = mission.bonus_complete;
			}

			const EMissionGoal* const g = &cut[category];
			const ECondStruct* const a = &g->subcond[0];
			const ECondStruct* const b = &g->subcond[1];

			/* -- Category subconditions --------------------------- */
			if (g->or_joined == 1) {
				/* OR-joined pair: render on one line. */
				goal_status = clamp_late_complete(goal_status, (a->cond == 9 || b->cond == 9) ? 9 : 0,
												  cat_complete_cache);

				if (goal_status == goalstatusorder[status_prio]) {
					cur_y = (int16_t)(cur_y + emit_goal_title(category, status_prio, cur_y));
					title_pending = 0;

					if (a->cond != 10 && a->cond != 0) {
						festring_setcursor(6, cur_y);
						festring_settextcolor((uint8_t)(goalstatuscolor[status_prio] + 1));
						cur_y =
							(int16_t)(cur_y + goals_outputgoal(a->id, a->cond, a->type, goal_status, a->pct));
					}

					if (b->cond != 10 && b->cond != 0) {
						if (a->cond != 10 && a->cond != 0) {
							/* Center the "OR" joiner on its own line. */
							festring_setcursor(6, cur_y);
							if (outchar)
								outchar('\n');
							festring_setcursor(6, cur_y);
							festring_settextcolor(0x43);
							festring_outstringcenter(((const uint8_t**)goaloperatorstrings)[g->or_joined]);
							cur_y = (int16_t)(cur_y + fontheight);
						}
						festring_setcursor(6, cur_y);
						festring_settextcolor((uint8_t)(goalstatuscolor[status_prio] + 1));
						cur_y =
							(int16_t)(cur_y + goals_outputgoal(b->id, b->cond, b->type, goal_status, b->pct));
					}
				}
			} else {
				/* Independent subconditions: evaluate + render each. */
				uint16_t pri_calc = (uint16_t)score_checkcondition(a->cond, a->type, a->id, a->pct, 0);
				if (a->cond == 10)
					pri_calc = 0;
				pri_calc = clamp_late_complete(pri_calc, a->cond, cat_complete_cache);

				if (pri_calc == goalstatusorder[status_prio]) {
					cur_y = (int16_t)(cur_y + emit_goal_title(category, status_prio, cur_y));
					title_pending = 0;

					if (a->cond != 10 && a->cond != 0) {
						festring_setcursor(6, cur_y);
						festring_settextcolor((uint8_t)(goalstatuscolor[status_prio] + 1));
						cur_y =
							(int16_t)(cur_y + goals_outputgoal(a->id, a->cond, a->type, pri_calc, a->pct));
					}
				}

				uint16_t sec_calc = (uint16_t)score_checkcondition(b->cond, b->type, b->id, b->pct, 0);
				if (b->cond == 10)
					sec_calc = 0;
				sec_calc = clamp_late_complete(sec_calc, b->cond, cat_complete_cache);
				goal_status = sec_calc;

				if (sec_calc == goalstatusorder[status_prio]) {
					if (title_pending) {
						cur_y = (int16_t)(cur_y + emit_goal_title(category, status_prio, cur_y));
						title_pending = 0;
					}
					if (b->cond != 10 && b->cond != 0) {
						festring_setcursor(6, cur_y);
						festring_settextcolor((uint8_t)(goalstatuscolor[status_prio] + 1));
						cur_y =
							(int16_t)(cur_y + goals_outputgoal(b->id, b->cond, b->type, sec_calc, b->pct));
					}
				}
			}

			/* -- Per-FG goals in this (category, status_prio) cell - */
			for (int16_t fg_idx = 0; fg_idx < mission_file_header.num_fg; fg_idx++) {
				/* Watcom unaligned-dword pattern: binary reads
				 * dword at fg.link_flag and shifts right 24, which
				 * extracts the byte at offset +3 = `difficulty`. */
				if ((diffmask[mission.difficulty] & fgdiffmask[fg_array[fg_idx].difficulty]) == 0)
					continue;

				uint16_t fg_status;
				uint16_t fg_cond;
				uint8_t fg_pct;
				if (category == 0) {
					fg_status = fgstatus[fg_idx].primary_status;
					fg_cond = fg_array[fg_idx].pri_win_cond;
					fg_pct = fg_array[fg_idx].pri_win_pct;
				} else if (category == 1) {
					fg_status = fgstatus[fg_idx].secondary_status;
					fg_cond = fg_array[fg_idx].sec_win_cond;
					fg_pct = fg_array[fg_idx].sec_win_pct;
				} else { /* bonus */
					fg_cond = fg_array[fg_idx].bonus_cond;
					fg_status = fgstatus[fg_idx].fg_complete;
					if (!showbonusgoals && fg_status != 1)
						fg_status = 0;
					if (fg_array[fg_idx].bonus_points < 0)
						fg_status = 0;
					fg_pct = fg_array[fg_idx].bonus_pct;
				}

				fg_status = clamp_late_complete(fg_status, fg_cond, cat_complete_cache);

				if (fg_cond != 0 && fg_cond != 10 && fg_status == goalstatusorder[status_prio]) {
					if (title_pending) {
						cur_y = (int16_t)(cur_y + emit_goal_title(category, status_prio, cur_y));
						title_pending = 0;
					}
					festring_setcursor(6, cur_y);
					festring_settextcolor(goalstatuscolor[status_prio]);
					cur_y = (int16_t)(cur_y + goals_outputgoal((uint16_t)fg_idx, fg_cond, /*target_type=*/1,
															   fg_status, (uint16_t)percentcon[fg_pct]));
				}
			}
		}
	}

	*out_content_height = (int16_t)(cur_y - scroll_y - fontheight);

	/* Pad out remaining space with blank lines. */
	while ((uint16_t)cur_y < (uint32_t)goalsBottom - fontheight) {
		festring_setcursor(6, cur_y);
		if (outchar)
			outchar('\n');
		cur_y = (int16_t)(cur_y + fontheight);
	}
}

/* Single input-poll iteration. Returns 1 if exit_room fires (caller
 * pops with nav_code latched), 2 if scroll changed (page needs
 * redraw), 0 if nothing happened. */
static int goals_poll_once(GoalsTask* t) {
	feinput_getrawinput();
	feinput_checkinput();
	feinput_degitterinput();
	inputdeltay = (int16_t)(inputdeltay * 2);

	const uint16_t key = (uint16_t)inputkey;
	enum {
		ACT_NONE,
		ACT_SCROLL_UP_1,    /* one line up   */
		ACT_SCROLL_DOWN_1,  /* one line down */
		ACT_SCROLL_UP_16,   /* page up       */
		ACT_SCROLL_DOWN_16, /* page down     */
		ACT_NAV_PREV,       /* return -1     */
		ACT_NAV_NEXT,       /* return +1     */
		ACT_EXIT_0,         /* return 0      */
		ACT_ACCEPT_IF_END   /* Enter/Space: exit only when end_flag */
	} action = ACT_NONE;

	switch (key) {
		case 1:
			action = ACT_NAV_PREV;
			break; /* arrow-left / ESC-like */
		case 2:
			action = ACT_NAV_NEXT;
			break; /* arrow-right */
		case 3:
			action = ACT_SCROLL_DOWN_1;
			break; /* arrow-down (LABEL_131) */
		case 4:
			action = ACT_SCROLL_UP_1;
			break; /* arrow-up   (LABEL_135) */
		case 0x32:
			action = ACT_SCROLL_UP_1;
			break; /* keypad '2' (50) */
		case 0x33:
			action = ACT_SCROLL_UP_16;
			break; /* '3' -> PgUp-ish (51) */
		case 0x38:
			action = ACT_SCROLL_DOWN_1;
			break; /* keypad '8' (56) */
		case 0x39:
			action = ACT_SCROLL_DOWN_16;
			break; /* '9' -> PgDn-ish (57) */
		case 0x0D: /* Enter */
		case 0x20:
			action = ACT_ACCEPT_IF_END;
			break; /* Space */
		case 0x1B: /* ESC         */
		case 0x47: /* 'G' upper   */
		case 0x51: /* 'Q' upper   */
		case 0x67: /* 'g' lower   */
		case 0x71: /* 'q' lower   */
		case 0xBB: /* F1 scancode */
			action = ACT_EXIT_0;
			break;
		default:
			break;
	}

	int redraw = 0;
	switch (action) {
		case ACT_NAV_PREV:
			t->nav_code = -1;
			return 1;
		case ACT_NAV_NEXT:
			t->nav_code = 1;
			return 1;
		case ACT_EXIT_0:
			t->nav_code = 0;
			return 1;
		case ACT_ACCEPT_IF_END:
			if (mission.end_flag) {
				t->nav_code = 0;
				return 1;
			}
			break;
		case ACT_SCROLL_UP_1:
			t->scroll_y = (int16_t)(t->scroll_y - fontheight);
			if (t->scroll_y < (int16_t)(goalsTop - t->content_height))
				t->scroll_y = (int16_t)(goalsTop - t->content_height);
			redraw = 1;
			break;
		case ACT_SCROLL_DOWN_1:
			t->scroll_y = (int16_t)(t->scroll_y + fontheight);
			if (t->scroll_y > (int16_t)goalsTop)
				t->scroll_y = (int16_t)goalsTop;
			redraw = 1;
			break;
		case ACT_SCROLL_UP_16:
			t->scroll_y = (int16_t)(t->scroll_y - 16 * fontheight);
			if (t->scroll_y < (int16_t)(goalsTop - t->content_height))
				t->scroll_y = (int16_t)(goalsTop - t->content_height);
			redraw = 1;
			break;
		case ACT_SCROLL_DOWN_16:
			t->scroll_y = (int16_t)(t->scroll_y + 16 * fontheight);
			if (t->scroll_y > (int16_t)goalsTop)
				t->scroll_y = (int16_t)goalsTop;
			redraw = 1;
			break;
		case ACT_NONE:
			break;
	}

	/* Mouse-button fallback (end-of-mission only): LMB/RMB exits
	 * with nav_code=0. */
	if (mission.end_flag) {
		const int btn = inputbuttons & 0xF;
		if (btn == 1 || btn == 2) {
			t->nav_code = 0;
			return 1;
		}
	}

	return redraw ? 2 : 0;
}

// FUNCTION: TIE 0x2AD90, TIE98 0x42DCA0 (task-split recovery)
static LandruTaskStepResult goals_task_step(void* self) {
	GoalsTask* t = (GoalsTask*)self;

	if (t->phase == GOALS_PHASE_RENDER) {
		const bool tie98_display = TieClassicDisplay_UsesDx5();
		if (tie98_display)
			FlightSurface_Lock();
		goals_render_page(t->scroll_y, &t->content_height);
		if (tie98_display) {
			FlightSurface_Unlock();
			FrontendDisplay_BlitOffscreenToRenderSurface();
			FrontendDisplay_PresentFrame();
		}
		t->phase = GOALS_PHASE_POLL;
		return LANDRU_TASK_STEP_CONTINUE;
	}

	int r = goals_poll_once(t);
	if (r == 1) {
		user_submodal_result = (int32_t)t->nav_code;
		return LANDRU_TASK_STEP_DONE;
	}
	if (r == 2)
		t->phase = GOALS_PHASE_RENDER;
	return LANDRU_TASK_STEP_CONTINUE;
}

static const LandruTaskVtable goals_task_vt = {
	.step = goals_task_step,
};

void goals_Push_MissionGoalsRoom_Task(void) {
	const bool tie98_display = TieClassicDisplay_UsesDx5();
	if (tie98_display)
		FlightSurface_Lock();
	/* ----- Window bounds from flightResolution --------------------- */
	if (flightResolution == TIE_FLIGHT_RES_VGA) {
		goalsTop = 19;
		goalsBottom = 182;
	} else if (tie_is_high_resolution_flight()) {
		goalsTop = 44;
		goalsBottom = 444;
	} else {
		goalsTop = 19;
		goalsBottom = 182;
	}

	showbonusgoals = (uint8_t)(mission.mission_mode != 4);

	festring_setlinewrap(0);
	festring_setautofill(1);
	festring_setfontsize(1);

	/* ----- Optional "[ESC]" banner when mission has already ended --- */
	if (mission.end_flag) {
		const uint16_t esc_w = (uint16_t)sys2_calclength((const uint8_t*)goalescapestr);
		const uint32_t esc_y_base = (((uint32_t)(screenYRes - goalsBottom)) / 2) + (uint32_t)goalsBottom -
									(((uint32_t)(fontheight)) / 2);
		const int16_t esc_x = (int16_t)((screenXRes - (fontheight + (uint32_t)esc_w)) / 2);

		festring_setbound(esc_x, (int16_t)esc_y_base, (int16_t)(screenXRes - esc_x),
						  (int16_t)(esc_y_base + fontheight));
		festring_setbackcolor(0x40);
		if (clearwindow)
			clearwindow();
		dropflag = 0;
		festring_setdropcolor(0x41);
		festring_setcursor((int16_t)(esc_x + fontheight / 2), (int16_t)(esc_y_base + 1));
		festring_settextcolor(0x4E);
		festring_outstring((const uint8_t*)goalescapestr);
		festring_setdropcolor(0x40);
	}

	/* ----- Main goals window --------------------------------------- */
	dropflag = 1;
	festring_setbound(0, (int16_t)goalsTop, (int16_t)screenXRes, (int16_t)goalsBottom);
	festring_setbackcolor(0x44);
	if (tie98_display)
		FlightSurface_Unlock();

	GoalsTask* t = (GoalsTask*)landru_task_push(&goals_task_vt);
	if (!t)
		return;
	t->scroll_y = (int16_t)goalsTop;
	t->content_height = 0;
	t->nav_code = 0;
	t->phase = GOALS_PHASE_RENDER;
}
