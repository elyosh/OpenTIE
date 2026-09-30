#include "tie/goals.h"
#ifdef TIE_MODERN
#include "tie_runtime/display/classic_display.h"
#include "tie_runtime/runtime/goals_task.h"
#endif
#include "tie/flight_surface_tie98.h"
#include "tie/frontend_display_tie98.h"

#ifdef TIE_MODERN
#include "tie_runtime/storage/string_table.h"
#endif
#include "tie/create.h" /* diffmask, fgdiffmask, genusconvert, familyconvert */
#include "tie/feinput.h"
#include "tie/festring.h"
#include "tie/mission.h" /* RUNTIME_MissionState */
#include "tie/score.h"
#include "tie/shipext.h" /* EFGStruct, MissionFile */
#include "tie/spec.h"
#include "tie/sys2.h"
#include "tie/tie.h"
#include "tie/user.h" /* user_submodal_result */
#include "tie_runtime/display/classic_display.h"
#include "tie_runtime/runtime/profile.h"

/* --- External globals populated by fediskio_loadstringdata ----------- */

/* All eight *string pointers below are singletons (each a char* to a single
 * string in the loaded string table). The *strings variants are base addresses of
 * 2..30-entry char* groups in that table. */
// GLOBAL: TIE95 0xD4BB8
// GLOBAL: TIE98 0x5FE848
void* condstrings; /* const char *[21] */
// GLOBAL: TIE95 0xD4BB0
// GLOBAL: TIE98 0x5FE81C
void* condverbstrings; /* const char *[20] */
// GLOBAL: TIE95 0xD4BC0
// GLOBAL: TIE98 0x5FE858
void* percentstrings; /* const char *[16] */
// GLOBAL: TIE95 0xD4BC4
// GLOBAL: TIE98 0x5FE83C
void* goaloperatorstrings; /* const char *[2]  (GOP_AND=0, GOP_OR=1)   */
// GLOBAL: TIE95 0xD4BAC
// GLOBAL: TIE98 0x5FE844
void* goaltitlestrings; /* const char *[9]  (3 cats x 3 status)     */
// GLOBAL: TIE95 0xD4BB4
// GLOBAL: TIE98 0x5FE87C
void* goalescapestr; /* const char *     "[ESC]" banner          */
// GLOBAL: TIE95 0xD4BC8
// GLOBAL: TIE98 0x5FE84C
void* goal_of_string; /* const char *     " of "                  */
// GLOBAL: TIE95 0xD4BBC
// GLOBAL: TIE98 0x5FE870
void* goal_ofall_string; /* const char *     " of all "              */
// GLOBAL: TIE95 0xD4B64
// GLOBAL: TIE98 0x5FE878
void* goalskillstrings; /* const char *[6]  skill-level names       */
// GLOBAL: TIE95 0xD4B94
// GLOBAL: TIE98 0x5FE880
void* goal_group_string; /* const char *     " group "               */
// GLOBAL: TIE95 0xD4B7C
// GLOBAL: TIE98 0x5FE854
void* goalaistrings; /* const char *[30] AI-order names          */
// GLOBAL: TIE95 0xD4B68
// GLOBAL: TIE98 0x5FE888
void* goal_allbut_string; /* const char *     "all but "              */
// GLOBAL: TIE95 0xD4B78
// GLOBAL: TIE98 0x5FE884
void* goalsidestrings; /* const char *[3]  rebel/imperial/craft    */
// GLOBAL: TIE95 0xD4B6C
// GLOBAL: TIE98 0x5FE820
void* goal_and_string; /* const char *     " and "                 */
// GLOBAL: TIE95 0xD4B98
// GLOBAL: TIE98 0x5FE85C
void* goalfamilystrings; /* const char *[7]  family-category names   */
// GLOBAL: TIE95 0xD4B70
// GLOBAL: TIE98 0x5FE850
void* goal_comma_string; /* const char *     ", "                    */
// GLOBAL: TIE95 0xD4B8C
// GLOBAL: TIE98 0x5FE86C
void* goalgenusstrings; /* const char *[16] genus-category names    */
// GLOBAL: TIE95 0xD4B74
// GLOBAL: TIE98 0x5FE874
void* goalallfgstring; /* const char *     "all FG"                */

/* buoy/navigation names (species 70..84) come from panelrts.c globals. */
#include "tie/panelrts.h"
#include <stdbool.h>
#ifdef TIE_MODERN
#include <landru/task.h>
#endif

#include <stdint.h>

/* --- Module-owned globals (watdbg: goals.c) -------------------------- */

/* "---" placeholder printed by outputgoal when target_type in {7,10}
 * (the condstr0 alias at 0xD4C78). Four bytes including the NUL. */
static const char condstr0[4] = { '-', '-', '-', '\0' };

/* Status-priority display order: {failed, incomplete, completed}. The room
 * iterates status_prio=0..2, matching goal.status against goalstatusorder[j]
 * to decide whether to emit the goal at that row. */
static const uint8_t goalstatusorder[3] = { 2, 4, 1 };

/* Palette indices for status text color. Category-subcond calls use
 * goalstatuscolor[prio] + 1 (one shade lighter); per-FG calls use the base
 * value. The matching backgrounds live in the front-end palette. */
static const uint8_t goalstatuscolor[3] = { 0x4A, 0x4E, 0x52 };

/* 21-entry LUT indexed by condition code (0..20). A value of 5 selects
 * the active-perfect verb group (has/have/must have); zero selects the
 * passive verb group (was/were/must be). Mirrors byte_C53B6 in TIE95. */
static const uint8_t tenseflag[21] = { 0, 5, 0, 0, 0, 0, 0, 5, 0, 5, 0, 0, 5, 0, 0, 0, 0, 0, 0, 0, 0 };

int32_t goalsTop;
int32_t goalsBottom;
uint8_t showbonusgoals;
int32_t goalsCount[3];
int32_t goalsCompletedCount[3];

/* ====================================================================
 * goals_checkidflag
 * ==================================================================== */

// FUNCTION: TIE95 0x2C6BC
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

// FUNCTION: TIE95 0x2C6D8
uint8_t goals_outputspeciesname(uint16_t species_idx, int16_t plural_flag) {
	const uint8_t* name_ptr;
	const uint16_t spec_num = spec_getspecnum(species_idx);

	uint8_t wrap;

	if (spec_num == 0xFF) {
		/* Species not in spec_data[]; fall back to the buoy/navigation
		 * strings. Only species 70..84 are valid indices into buoystr[15].
		 * The binary's bounds check (cmp edx,0x46 / cmp edx,0x54) is
		 * compile-time dead because both jumps land back in the buoystr
		 * path -- out-of-range species yield undefined reads there. We
		 * match the shipped behavior verbatim. */
		name_ptr = (const uint8_t*)(((char**)buoystr)[species_idx - 70]);
	} else {
#ifdef TIE_MODERN
		name_ptr = (const uint8_t*)spec_name_ptrs[spec_num];
#else
		name_ptr = (const uint8_t*)spec_data[spec_num].name_ptr;
#endif
	}

	wrap = goals_checkwrap(name_ptr);
	festring_outstring(name_ptr);
	if (outchar)
		outchar(plural_flag ? 's' : ' ');
	return wrap;
}

/* Render one localized goal line for a target, condition, status, and
 * quantifier. Returns the accumulated vertical space added by wrapping. */

// FUNCTION: TIE95 0x2BF50
int32_t goals_outputgoal(uint16_t target, uint16_t cond, int16_t target_type, uint16_t status, uint16_t op) {
	int32_t total = fontheight; /* accumulator starts at fontheight */
	uint16_t tense_offset = 10;

	if (op == 4 || op == 14 || op == 9)
		tense_offset = 0;

	/* ----- target_type == 1: a specific flight group --------------- */
	if (target_type == 1) {
		if (op == 6) {
			/* "species FG_name N" -- craft-specific reference. */
			int ch;

			total += goals_outputspeciesname(fg_array[target].species, 0);
			festring_outstring((const uint8_t*)&fg_array[target]);
			if (outchar)
				outchar(' ');
			ch = fgstatus[target].cond_id[4].detail ? (fg_array[target].special_craft + '1') : '?';
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
		uint16_t in_set;
		uint16_t i;
		uint16_t j;

		festring_outstring(((const uint8_t**)percentstrings)[op]);
		festring_outstring((const uint8_t*)goal_of_string);
		festring_outstring((const uint8_t*)goalallfgstring);
		if (outchar)
			outchar(' ');

		/* First pass: count matching FGs to decide " and " placement and
		 * tense_offset (single match -> 0, multiple -> 10). */
		in_set = 0;
		for (i = 0; i < (uint16_t)mission_file_header.num_fg; i++) {
			if (fg_array[i].set == (uint8_t)target)
				in_set++;
		}
		tense_offset = (in_set == 1) ? 0 : 10;

		/* Second pass: emit each FG, with ", " / " and " separators. */
		for (j = 0; j < (uint16_t)mission_file_header.num_fg; j++) {
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
		const uint8_t* cat_name;

		festring_outstring(((const uint8_t**)percentstrings)[op]);
		festring_outstring((const uint8_t*)goal_ofall_string);

		cat_name = NULL;
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

// FUNCTION: TIE95 0x2AD90
// FUNCTION: TIE98 0x42DCA0
int32_t goals_missiongoalsroom(void) {
	int16_t scroll_y;
	int16_t content_height;
	int16_t nav_code;
	int render_again = 1;
#ifdef TIE_MODERN
	GoalsRoomState* continuation = landru_task_top();
	const bool tie98_display = TieClassicDisplay_UsesDx5();
	scroll_y = continuation->scroll_y;
	content_height = continuation->content_height;
	nav_code = continuation->nav_code;
	render_again = continuation->render;
	if (!continuation->started)
#elif defined(TIE98)
	const bool tie98_display = true;
#else
	const bool tie98_display = false;
#endif
	{

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

		scroll_y = (int16_t)goalsTop;
		content_height = 0;
		nav_code = 0;

#ifdef TIE_MODERN
		continuation->scroll_y = scroll_y;
		continuation->content_height = content_height;
		continuation->nav_code = nav_code;
		continuation->started = true;
		continuation->render = true;
		return 0;
#endif
	}
	for (;;) {
		if (render_again) {
			if (tie98_display)
				FlightSurface_Lock();
			{
				int16_t cur_y = scroll_y;
				uint16_t category;

				festring_settextcolor(0x4E);
				for (category = 0; category < 3; category++) {
					goalsCompletedCount[category] = 0;
					goalsCount[category] = 0;
				}

				for (category = 0; category < 3; category++) {
					uint16_t status_prio;

					for (status_prio = 0; status_prio < 3; status_prio++) {
						int16_t title_pending = 1;

						/* -- Category-level mission status + complete cache --- */
						uint16_t goal_status;
						uint16_t cat_complete_cache;
						const EMissionGoal* g;
						const ECondStruct* a;
						const ECondStruct* b;
						int16_t fg_idx;

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

						g = &cut[category];
						a = &g->subcond[0];
						b = &g->subcond[1];

						/* -- Category subconditions --------------------------- */
						if (g->or_joined == 1) {
							/* OR-joined pair: render on one line. */
							if (a->cond == 9 || b->cond == 9) {
								if (cat_complete_cache == 1 || goal_status != 1) {
									if (cat_complete_cache == 1)
										goal_status = 1;
								} else {
									goal_status = 4;
								}
							}
							if (category == 2 && !showbonusgoals && (goal_status == 4 || goal_status == 2)) {
								goal_status = 0;
								if (status_prio == 2)
									goalsCount[category]++;
							}

							if (goal_status == goalstatusorder[status_prio]) {
								if (a->cond != 10 && a->cond != 0) {
									{
										festring_setcursor(0, cur_y);
										if (outchar)
											outchar('\n');
										festring_setcursor(0, cur_y);
										festring_settextcolor(0x46);
										festring_outstringcenter(
											((const uint8_t**)goaltitlestrings)[3u * category + status_prio]);
										cur_y = (int16_t)(cur_y + fontheight);
									}
									title_pending = 0;
									festring_setcursor(6, cur_y);
									festring_settextcolor((uint8_t)(goalstatuscolor[status_prio] + 1));
									cur_y = (int16_t)(cur_y + goals_outputgoal(a->id, a->cond, a->type,
																			   goal_status, a->pct));
									goalsCount[category]++;
									if (status_prio == 2)
										goalsCompletedCount[category]++;
								}

								if (b->cond != 10 && b->cond != 0) {
									if (a->cond != 10 && a->cond != 0) {
										/* Center the "OR" joiner on its own line. */
										festring_setcursor(6, cur_y);
										if (outchar)
											outchar('\n');
										festring_setcursor(6, cur_y);
										festring_settextcolor(0x43);
										festring_outstringcenter(
											((const uint8_t**)goaloperatorstrings)[g->or_joined]);
										cur_y = (int16_t)(cur_y + fontheight);
									}
									festring_setcursor(6, cur_y);
									festring_settextcolor((uint8_t)(goalstatuscolor[status_prio] + 1));
									cur_y = (int16_t)(cur_y + goals_outputgoal(b->id, b->cond, b->type,
																			   goal_status, b->pct));
									goalsCount[category]++;
									if (status_prio == 2)
										goalsCompletedCount[category]++;
								}
							}
						} else {
							/* Independent subconditions: evaluate + render each. */
							uint16_t pri_calc =
								(uint16_t)score_checkcondition(a->cond, a->type, a->id, a->pct, 0);
							uint16_t sec_calc;

							if (a->cond == 10)
								pri_calc = 0;
							if (a->cond == 9) {
								if (cat_complete_cache == 1 || pri_calc != 1) {
									if (cat_complete_cache == 1)
										pri_calc = 1;
								} else {
									pri_calc = 4;
								}
							}
							if (category == 2 && !showbonusgoals && (pri_calc == 4 || pri_calc == 2)) {
								pri_calc = 0;
								if (status_prio == 2)
									goalsCount[category]++;
							}

							if (pri_calc == goalstatusorder[status_prio] && a->cond != 10 && a->cond != 0) {
								{
									festring_setcursor(0, cur_y);
									if (outchar)
										outchar('\n');
									festring_setcursor(0, cur_y);
									festring_settextcolor(0x46);
									festring_outstringcenter(
										((const uint8_t**)goaltitlestrings)[3u * category + status_prio]);
									cur_y = (int16_t)(cur_y + fontheight);
								}
								title_pending = 0;
								festring_setcursor(6, cur_y);
								festring_settextcolor((uint8_t)(goalstatuscolor[status_prio] + 1));
								cur_y = (int16_t)(cur_y + goals_outputgoal(a->id, a->cond, a->type, pri_calc,
																		   a->pct));
								goalsCount[category]++;
								if (status_prio == 2)
									goalsCompletedCount[category]++;
							}

							sec_calc = (uint16_t)score_checkcondition(b->cond, b->type, b->id, b->pct, 0);
							if (b->cond == 10)
								sec_calc = 0;
							if (b->cond == 9) {
								if (cat_complete_cache == 1 || sec_calc != 1) {
									if (cat_complete_cache == 1)
										sec_calc = 1;
								} else {
									sec_calc = 4;
								}
							}
							if (category == 2 && !showbonusgoals && (sec_calc == 4 || sec_calc == 2)) {
								sec_calc = 0;
								if (status_prio == 2)
									goalsCount[category]++;
							}
							goal_status = sec_calc;

							if (sec_calc == goalstatusorder[status_prio] && b->cond != 10 && b->cond != 0) {
								if (title_pending) {
									{
										festring_setcursor(0, cur_y);
										if (outchar)
											outchar('\n');
										festring_setcursor(0, cur_y);
										festring_settextcolor(0x46);
										festring_outstringcenter(
											((const uint8_t**)goaltitlestrings)[3u * category + status_prio]);
										cur_y = (int16_t)(cur_y + fontheight);
									}
									title_pending = 0;
								}
								festring_setcursor(6, cur_y);
								festring_settextcolor((uint8_t)(goalstatuscolor[status_prio] + 1));
								cur_y = (int16_t)(cur_y + goals_outputgoal(b->id, b->cond, b->type, sec_calc,
																		   b->pct));
								goalsCount[category]++;
								if (status_prio == 2)
									goalsCompletedCount[category]++;
							}
						}

						/* -- Per-FG goals in this (category, status_prio) cell - */
						for (fg_idx = 0; fg_idx < mission_file_header.num_fg; fg_idx++) {
							/* Watcom unaligned-dword pattern: binary reads
							 * dword at fg.link_flag and shifts right 24, which
							 * extracts the byte at offset +3 = `difficulty`. */
							uint16_t fg_status;
							uint16_t fg_cond;
							uint8_t fg_pct;

							if ((diffmask[mission.difficulty] & fgdiffmask[fg_array[fg_idx].difficulty]) == 0)
								continue;

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
								if (fg_array[fg_idx].bonus_points < 0)
									fg_status = 0;
								if (!showbonusgoals && (fg_status == 4 || fg_status == 2)) {
									fg_status = 0;
									if (status_prio == 2)
										goalsCount[category]++;
								}
								fg_pct = fg_array[fg_idx].bonus_pct;
							}

							if (fg_cond == 9 &&
								fgstatus[fg_idx].cond[1].detail != fgstatus[fg_idx].cond[0].count) {
								if (cat_complete_cache == 1 || fg_status != 1) {
									if (cat_complete_cache == 1)
										fg_status = 1;
								} else {
									fg_status = 4;
								}
							}

							if (fg_cond != 0 && fg_cond != 10 && fg_status == goalstatusorder[status_prio]) {
								if (title_pending) {
									{
										festring_setcursor(0, cur_y);
										if (outchar)
											outchar('\n');
										festring_setcursor(0, cur_y);
										festring_settextcolor(0x46);
										festring_outstringcenter(
											((const uint8_t**)goaltitlestrings)[3u * category + status_prio]);
										cur_y = (int16_t)(cur_y + fontheight);
									}
									title_pending = 0;
								}
								festring_setcursor(6, cur_y);
								festring_settextcolor(goalstatuscolor[status_prio]);
								cur_y = (int16_t)(cur_y + goals_outputgoal((uint16_t)fg_idx, fg_cond,
																		   /*target_type=*/1, fg_status,
																		   (uint16_t)percentcon[fg_pct]));
								goalsCount[category]++;
								if (status_prio == 2)
									goalsCompletedCount[category]++;
							}
						}
					}
				}

				content_height = (int16_t)(cur_y - scroll_y - fontheight);

				/* Pad out remaining space with blank lines. */
				while ((uint16_t)cur_y < (uint32_t)goalsBottom - fontheight) {
					festring_setcursor(6, cur_y);
					if (outchar)
						outchar('\n');
					cur_y = (int16_t)(cur_y + fontheight);
				}
			}
			if (tie98_display) {
				FlightSurface_Unlock();
				FrontendDisplay_BlitOffscreenToRenderSurface();
				FrontendDisplay_PresentFrame();
			}
#ifdef TIE_MODERN
			continuation->scroll_y = scroll_y;
			continuation->content_height = content_height;
			continuation->nav_code = nav_code;
			continuation->render = false;
			return 0;
#endif
		}
		{
			enum {
				ACT_NONE,
				ACT_SCROLL_DOWN_1,
				ACT_SCROLL_UP_1,
				ACT_SCROLL_DOWN_16,
				ACT_SCROLL_UP_16,
				ACT_NAV_PREV,
				ACT_NAV_NEXT,
				ACT_EXIT_0,
				ACT_ACCEPT_IF_END
			} action;
			uint16_t key;
			int redraw;

			feinput_getrawinput();
			feinput_checkinput();
			feinput_degitterinput();
			inputdeltay = (int16_t)(inputdeltay * 2);

			key = (uint16_t)inputkey;
			/* Directions describe the viewport, not movement of the rendered text. */
			action = ACT_NONE;

			switch (key) {
				case KEY_LEFT_ARROW:
					action = ACT_NAV_PREV;
					break;
				case KEY_RIGHT_ARROW:
					action = ACT_NAV_NEXT;
					break;
				case KEY_UP_ARROW:
					action = ACT_SCROLL_UP_1;
					break;
				case KEY_DOWN_ARROW:
					action = ACT_SCROLL_DOWN_1;
					break;
				case 0x32:
					action = ACT_SCROLL_DOWN_1;
					break; /* keypad '2' (50) */
				case 0x33:
					action = ACT_SCROLL_DOWN_16;
					break; /* keypad '3': page down */
				case 0x38:
					action = ACT_SCROLL_UP_1;
					break; /* keypad '8' (56) */
				case 0x39:
					action = ACT_SCROLL_UP_16;
					break; /* keypad '9': page up */
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

			redraw = 0;
			switch (action) {
				case ACT_NAV_PREV:
					nav_code = -1;
					{
#ifdef TIE_MODERN
						continuation->finished = true;
#endif
						return (uint16_t)nav_code;
					}
				case ACT_NAV_NEXT:
					nav_code = 1;
					{
#ifdef TIE_MODERN
						continuation->finished = true;
#endif
						return (uint16_t)nav_code;
					}
				case ACT_EXIT_0:
					nav_code = 0;
					{
#ifdef TIE_MODERN
						continuation->finished = true;
#endif
						return (uint16_t)nav_code;
					}
				case ACT_ACCEPT_IF_END:
					if (mission.end_flag) {
						nav_code = 0;
						{
#ifdef TIE_MODERN
							continuation->finished = true;
#endif
							return (uint16_t)nav_code;
						}
					}
					break;
				case ACT_SCROLL_DOWN_1:
					scroll_y = (int16_t)(scroll_y - fontheight);
					if (scroll_y < (int16_t)(goalsTop - content_height))
						scroll_y = (int16_t)(goalsTop - content_height);
					redraw = 1;
					break;
				case ACT_SCROLL_UP_1:
					scroll_y = (int16_t)(scroll_y + fontheight);
					if (scroll_y > (int16_t)goalsTop)
						scroll_y = (int16_t)goalsTop;
					redraw = 1;
					break;
				case ACT_SCROLL_DOWN_16:
					scroll_y = (int16_t)(scroll_y - 16 * fontheight);
					if (scroll_y < (int16_t)(goalsTop - content_height))
						scroll_y = (int16_t)(goalsTop - content_height);
					redraw = 1;
					break;
				case ACT_SCROLL_UP_16:
					scroll_y = (int16_t)(scroll_y + 16 * fontheight);
					if (scroll_y > (int16_t)goalsTop)
						scroll_y = (int16_t)goalsTop;
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
					nav_code = 0;
					{
#ifdef TIE_MODERN
						continuation->finished = true;
#endif
						return (uint16_t)nav_code;
					}
				}
			}

			render_again = redraw;
		}
#ifdef TIE_MODERN
		continuation->scroll_y = scroll_y;
		continuation->content_height = content_height;
		continuation->nav_code = nav_code;
		continuation->render = render_again != 0;
		return 0;
#endif
	}
}
