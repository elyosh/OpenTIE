#include "tie/score.h"
#include "tie/create.h" /* speciesconvert, genusconvert, familyconvert, diffmask, fgdiffmask */
#include "tie/edition.h"
#include "tie/fediskio.h"
#include "tie/fscript.h"
#include "tie/fsfx.h"
#include "tie/math2.h"
#include "tie/mission.h" /* RUNTIME_MissionState */
#include "tie/msg.h"
#include "tie/msg_templates.h"
#include "tie/shipext.h" /* EFGStruct, EAIStruct */
#include "tie/tie.h"
#include "tie/user.h"
#include "tie_runtime/diagnostics/flight_trace.h"

#include <stddef.h>
#include <stdint.h>

/* --- Module-owned globals (watdbg: score.c) -------------------------- */

/* 5-entry LUT mapping per-FG pct bucket (0..4) -> amount_op for checkcondition. */
// GLOBAL: TIE95 0xC7A90
// GLOBAL: TIE98 0x4EB7B8
int16_t percentcon[5] = { 0, 2, 4, 5, 6 };

/* Per-cond-code dispatch flag: 0 = mission-level direct check,
 *                              1 = FG-iteration evaluator.
 * Frozen at load time; mirrors the _conditiongrouprelated data table. */
// GLOBAL: TIE95 0xC7A9A
// GLOBAL: TIE98 0x4EB7C8
uint8_t conditiongrouprelated[26] = { 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1,
									  0, 0, 0, 0, 0, 0, 1, 0, 1, 1, 1, 1, 0 };

/* ====================================================================
 * score_checkobjective
 * ==================================================================== */

// FUNCTION: TIE95 0x50A70
void score_checkobjective(void) {
	uint16_t r;
	uint16_t any_sec_goal;
	uint16_t any_bonus_goal;
	uint16_t sec_all_ok;
	uint16_t bonus_all_ok;
	uint16_t pri_all_ok;
	uint16_t any_pri_goal;
	uint16_t i;
	uint16_t k;
	uint16_t status;

	if (mission.player_status != 3)
		return;
	if (mission.train_craft_type || hyperspaceflag)
		return;

	pri_all_ok = 1;
	any_pri_goal = 0;
	sec_all_ok = 1;
	any_sec_goal = 0;
	bonus_all_ok = 1;
	any_bonus_goal = 0;

	if (!timers[TIMER_PRIMARY_CHECK] || mission.end_flag) {
		/* --- Phase 1: per-FG evaluation ---------------------------- */
		for (i = 0; i < mission_file_header.num_fg; i++) {
			if (diffmask[mission.difficulty] & fgdiffmask[fg_array[i].difficulty]) {
				/* Primary win */
				if (fgstatus[i].primary_status == 1 && (int8_t)fg_array[i].pri_win_cond != 9) {
					any_pri_goal = 1;
				} else if (fgstatus[i].primary_status == 2) {
					pri_all_ok = 0;
					if (!mission.primary_complete)
						mission.primary_complete = 2;
				} else if (fgstatus[i].primary_status) {
					if (!fg_array[i].pri_win_cond || (int8_t)fg_array[i].pri_win_cond == 10) {
						mission.primary_fg[i] = 0;
						fgstatus[i].primary_status = 0;
					} else {
						any_pri_goal = 1;
						r = score_checkcondition((int8_t)fg_array[i].pri_win_cond, GTT_FG, i,
												 percentcon[(int8_t)fg_array[i].pri_win_pct], 0);
						if (r == 2 && !mission.primary_complete)
							mission.primary_complete = 2;
						if (r != 1) {
							pri_all_ok = 0;
							mission.primary_fg[i] = 2;
						} else {
							mission.primary_fg[i] = 1;
						}
						fgstatus[i].primary_status = (uint8_t)r;
					}
				}

				/* Secondary win */
				if (fgstatus[i].secondary_status == 1 && (int8_t)fg_array[i].sec_win_cond != 9) {
					any_sec_goal = 1;
				} else if (fgstatus[i].secondary_status == 2) {
					sec_all_ok = 0;
					if (!mission.secondary_complete)
						mission.secondary_complete = 2;
				} else if (fgstatus[i].secondary_status) {
					if (!fg_array[i].sec_win_cond || (int8_t)fg_array[i].sec_win_cond == 10) {
						mission.secondary_fg[i] = 0;
						fgstatus[i].secondary_status = 0;
					} else {
						any_sec_goal = 1;
						r = score_checkcondition((int8_t)fg_array[i].sec_win_cond, GTT_FG, i,
												 percentcon[(int8_t)fg_array[i].sec_win_pct], 0);
						if (r == 2 && !mission.secondary_complete)
							mission.secondary_complete = 2;
						if (r != 1) {
							sec_all_ok = 0;
							mission.secondary_fg[i] = 2;
						} else {
							mission.secondary_fg[i] = 1;
						}
						fgstatus[i].secondary_status = (uint8_t)r;
					}
				}

				/* Bonus */
				if (fgstatus[i].fg_complete == 1 && (int8_t)fg_array[i].bonus_cond != 9) {
					any_bonus_goal = 1;
				} else if (fgstatus[i].fg_complete == 2) {
					bonus_all_ok = 0;
					if (!mission.bonus_complete)
						mission.bonus_complete = 2;
				} else if (fgstatus[i].fg_complete) {
					if (!fg_array[i].bonus_cond || (int8_t)fg_array[i].bonus_cond == 10) {
						mission.bonus_fg[i] = 0;
						fgstatus[i].fg_complete = 0;
					} else {
						any_bonus_goal = 1;
						r = score_checkcondition((int8_t)fg_array[i].bonus_cond, GTT_FG, i,
												 percentcon[(int8_t)fg_array[i].bonus_pct], 0);
						if (fgstatus[i].fg_complete == 2 && !mission.bonus_complete &&
							fg_array[i].bonus_points >= 0)
							mission.bonus_complete = 2;
						if (r != 1) {
							if (fg_array[i].bonus_points >= 0)
								bonus_all_ok = 0;
							mission.bonus_fg[i] = 2;
						} else {
							mission.bonus_fg[i] = 1;
						}
						fgstatus[i].fg_complete = (uint8_t)r;
					}
				}
			} else {
				fgstatus[i].primary_status = 0;
				fgstatus[i].secondary_status = 0;
				fgstatus[i].fg_complete = 0;
			}
		}

		/* --- Phase 2: mission-level primary aggregate -------------- */
		if ((int8_t)cut[0].subcond[0].cond != 10 || (int8_t)cut[0].subcond[1].cond != 10) {
			uint16_t pri_a;
			uint16_t pri_b;

			any_pri_goal = 1;
			pri_a = score_checkcondition((int8_t)cut[0].subcond[0].cond, (int8_t)cut[0].subcond[0].type,
										 (int8_t)cut[0].subcond[0].id, (int8_t)cut[0].subcond[0].pct, 0);
			pri_b = score_checkcondition((int8_t)cut[0].subcond[1].cond, (int8_t)cut[0].subcond[1].type,
										 (int8_t)cut[0].subcond[1].id, (int8_t)cut[0].subcond[1].pct, 0);
			if ((int8_t)cut[0].or_joined == 1) {
				if ((pri_a | pri_b) & 1)
					status = 1;
				else if (pri_a & pri_b & 2)
					status = 2;
				else
					status = 4;
			} else if (pri_a & pri_b & 1) {
				status = 1;
			} else if ((pri_a | pri_b) & 2) {
				status = 2;
			} else {
				status = 4;
			}
			mission.primary_global = (uint8_t)status;
		} else {
			status = 1;
			mission.primary_global = 0;
		}
		if (status == 2 && !mission.primary_complete)
			mission.primary_complete = 2;
		if (status == 1 && pri_all_ok && any_pri_goal) {
			if (mission.primary_complete != 1) {
				if (timers[TIMER_PRI_COMPLETE] == 0) {
					timers[TIMER_PRI_COMPLETE] = 7080;
					msg_messageprintf(MSG_PRIMARY_COMPLETE);
					for (k = 0; k < 2; k++) {
						if (mission_file_header.mission.win_msg1[k][0]) {
							msg_addmessageptr(0, (char*)mission_file_header.mission.win_msg1[k]);
							/* The first name-list line carries the edition-specific
							 * objective voice cue; the second line plays silently. */
							if (k == 0)
								pending_voice_id = TIE_FLIGHT_EDITION(FSFX_TIE95_MISSION_VOICE_BASE,
																	  FSFX_TIE98_MISSION_VOICE_BASE) +
												   FSFX_MISSION_VOICE_PRIMARY;
							else
								pending_voice_id = 0;
							msg_messageprintf(MSG_GENERIC_STAR);
						}
					}
				}
				fsfx_speakobjectives(0x5Bu);
				fscript_MsSetSequence(5);
			}
			mission.primary_complete = 1;
			/* Battle 12 / mission 7: force end-of-mission on primary complete. */
			if (currentbattle == 12 && currentmission == 7) {
				user_checkreplaycamera();
				mission.end_flag = 1;
				mission.player_status = 3;
			}
		}

		/* --- Phase 3: secondary aggregate -------------------------- */
		{
			const uint16_t sec_a =
				score_checkcondition((int8_t)cut[1].subcond[0].cond, (int8_t)cut[1].subcond[0].type,
									 (int8_t)cut[1].subcond[0].id, (int8_t)cut[1].subcond[0].pct, 0);
			const uint16_t sec_b =
				score_checkcondition((int8_t)cut[1].subcond[1].cond, (int8_t)cut[1].subcond[1].type,
									 (int8_t)cut[1].subcond[1].id, (int8_t)cut[1].subcond[1].pct, 0);

			status = 4;
			if ((int8_t)cut[1].or_joined == 1) {
				if ((sec_a | sec_b) & 1)
					status = 1;
				else if (sec_a & sec_b & 2)
					status = 2;
			} else if (sec_a & sec_b & 1) {
				status = 1;
			} else if ((sec_a | sec_b) & 2) {
				status = 2;
			}
		}
		mission.secondary_global = (uint8_t)status;
		if (status == 2 && !mission.secondary_complete)
			mission.secondary_complete = 2;
		if ((int8_t)cut[1].subcond[0].cond != 10 || (int8_t)cut[1].subcond[1].cond != 10) {
			any_sec_goal = 1;
		} else {
			mission.secondary_global = 0;
			status = 1;
		}
		if (status == 1 && sec_all_ok == 1 && any_sec_goal) {
			if (mission.secondary_complete != 1) {
				if (timers[TIMER_SEC_COMPLETE] == 0) {
					timers[TIMER_SEC_COMPLETE] = 7080;
					msg_messageprintf(MSG_SECONDARY_COMPLETE);
					for (k = 0; k < 2; k++) {
						if (mission_file_header.mission.win_msg2[k][0]) {
							msg_addmessageptr(0, (char*)mission_file_header.mission.win_msg2[k]);
							/* The first name-list line carries the edition-specific
							 * objective voice cue; the second line plays silently. */
							if (k == 0)
								pending_voice_id = TIE_FLIGHT_EDITION(FSFX_TIE95_MISSION_VOICE_BASE,
																	  FSFX_TIE98_MISSION_VOICE_BASE) +
												   FSFX_MISSION_VOICE_SECONDARY;
							else
								pending_voice_id = 0;
							msg_messageprintf(MSG_GENERIC_STAR);
						}
					}
				}
				fsfx_speakobjectives(0x5Cu);
				fscript_MsSetSequence(7);
			}
			mission.secondary_complete = 1;
		}

		/* --- Phase 4: bonus aggregate ------------------------------ */
		{
			const uint16_t bonus_a =
				score_checkcondition((int8_t)cut[2].subcond[0].cond, (int8_t)cut[2].subcond[0].type,
									 (int8_t)cut[2].subcond[0].id, (int8_t)cut[2].subcond[0].pct, 0);
			const uint16_t bonus_b =
				score_checkcondition((int8_t)cut[2].subcond[1].cond, (int8_t)cut[2].subcond[1].type,
									 (int8_t)cut[2].subcond[1].id, (int8_t)cut[2].subcond[1].pct, 0);

			status = 4;
			if ((int8_t)cut[2].or_joined == 1) {
				if ((bonus_a | bonus_b) & 1)
					status = 1;
				else if (bonus_a & bonus_b & 2)
					status = 2;
			} else if (bonus_a & bonus_b & 1) {
				status = 1;
			} else if ((bonus_a | bonus_b) & 2) {
				status = 2;
			}
		}
		mission.bonus_global = (uint8_t)status;
		if (status == 2 && !mission.bonus_complete)
			mission.bonus_complete = 2;
		if ((int8_t)cut[2].subcond[0].cond != 10 || (int8_t)cut[2].subcond[1].cond != 10) {
			any_bonus_goal = 1;
		} else {
			mission.bonus_global = 0;
			status = 1;
		}
		if (status == 1 && bonus_all_ok == 1 && any_bonus_goal) {
			if (mission.bonus_complete != 1) {
				if (timers[TIMER_BONUS_COMPLETE] == 0) {
					msg_messageprintf(MSG_BONUS_COMPLETE);
					timers[TIMER_BONUS_COMPLETE] = 14160;
				}
				fsfx_speakobjectives(0x5Eu);
				fscript_MsSetSequence(9);
			}
			mission.bonus_complete = 1;
		}

		/* "Objectives failed" banner when primary fails mid-flight. */
		if (mission.primary_complete == 2 && !hyperspaceflag && !timers[TIMER_OBJECTIVES_FAILED]) {
			msg_messageprintf(MSG_OBJECTIVES_FAILED);
			timers[TIMER_OBJECTIVES_FAILED] = 21240;
			for (k = 0; k < 2; k++) {
				if (mission_file_header.mission.loss_msg[k][0]) {
					msg_addmessageptr(0, (char*)mission_file_header.mission.loss_msg[k]);
					/* First failure-name line carries the edition-specific
					 * loss VO loaded by fsfx_loadvoicelfd;
					 * second line plays silently. */
					if (k == 0)
						pending_voice_id =
							TIE_FLIGHT_EDITION(FSFX_TIE95_MISSION_VOICE_BASE, FSFX_TIE98_MISSION_VOICE_BASE) +
							FSFX_MISSION_VOICE_LOSS;
					else
						pending_voice_id = 0;
					msg_messageprintf(MSG_GENERIC_STAR);
				}
			}
			fsfx_triggervoicesfx(0x6Au);
			fscript_MsSetSequence(6);
		}

		timers[TIMER_PRIMARY_CHECK] = 236;
	}

	/* --- Phase 5: radio-message trigger poll ---------------------- */
	if (timers[TIMER_RADIOMSG_POLL] <= 0) {
		for (i = 0; i < mission_file_header.num_msg; i++) {
			if (!mission.radiomsg_triggered[i]) {
				/* Evaluate both subconditions. */
				const uint16_t sa =
					score_checkcondition((int8_t)radiomsg[90 * i + 64], (int8_t)radiomsg[90 * i + 65],
										 (int8_t)radiomsg[90 * i + 66], (int8_t)radiomsg[90 * i + 67], 0);
				const uint16_t sb =
					score_checkcondition((int8_t)radiomsg[90 * i + 68], (int8_t)radiomsg[90 * i + 69],
										 (int8_t)radiomsg[90 * i + 70], (int8_t)radiomsg[90 * i + 71], 0);

				if ((int8_t)radiomsg[90 * i + 89] == 1)
					status = sa | sb;
				else
					status = sa & sb;
				if (status & 1) {
					mission.radiomsg_triggered[i] = 1;
					mission.radiomsg_countdown[i] = radiomsg[90 * i + 88];
					if (mission.radiomsg_countdown[i] == 0) {
						msg_addmessageptr(0, (char*)&radiomsg[90 * i]);
						pending_voice_id =
							TIE_FLIGHT_EDITION(FSFX_TIE95_MISSION_VOICE_BASE, FSFX_TIE98_MISSION_VOICE_BASE) +
							i;
						msg_messageprintf(MSG_GENERIC_STAR_INFO);
					}
				}
			} else {
				if (mission.radiomsg_countdown[i] && --mission.radiomsg_countdown[i] == 0) {
					msg_addmessageptr(0, (char*)&radiomsg[90 * i]);
					/* Pair the radio text with the edition-specific
					 * per-mission voice cue loaded from the
					 * <NAME>.LFD by fsfx_loadvoicelfd. */
					pending_voice_id =
						TIE_FLIGHT_EDITION(FSFX_TIE95_MISSION_VOICE_BASE, FSFX_TIE98_MISSION_VOICE_BASE) + i;
					msg_messageprintf(MSG_GENERIC_STAR_INFO);
				}
			}
		}
		timers[TIMER_RADIOMSG_POLL] = 1180;
	}
}

/* ====================================================================
 * score_checkcondition (body)
 * ==================================================================== */

// FUNCTION: TIE95 0x51698
uint16_t score_checkcondition(uint16_t cond, uint16_t target_type, uint16_t target_id, uint16_t amount_op,
							  int16_t exclude_player) {
	uint16_t fg_idx;
	uint16_t total_craft;
	uint16_t specific_total;
	uint16_t destroyed_total;
	uint16_t cond_count;
	uint16_t cond_specific_total;
	uint16_t player_matched;
	uint16_t cond_destroyed_count;
	uint16_t cond_destroyed_specific;
	uint16_t result;

	if (conditiongrouprelated[cond]) {
		if (!target_type)
			return 2; /* No target -> failed. */

		total_craft = 0;
		specific_total = 0;
		destroyed_total = 0;
		cond_count = 0;
		cond_specific_total = 0;
		player_matched = 0;
		cond_destroyed_count = 0;
		cond_destroyed_specific = 0;

		for (fg_idx = 0; fg_idx < mission_file_header.num_fg; fg_idx++) {
			if (!fg_array[fg_idx].species)
				continue;
			if (!score_fgmemberofgroup(fg_idx, target_type, target_id))
				continue;

			total_craft += fgstatus[fg_idx].counts[FG_COUNT_TOTAL];
			specific_total += fgstatus[fg_idx].special_counts[FG_COUNT_TOTAL];
			destroyed_total += fgstatus[fg_idx].counts[FG_COUNT_ARRIVED];

			switch (cond) {
				case 1:
					cond_count += fgstatus[fg_idx].counts[FG_COUNT_ARRIVED];
					cond_specific_total += fgstatus[fg_idx].special_counts[FG_COUNT_ARRIVED];
					if (pstate.player_fg_idx == fg_idx)
						player_matched = 1;
					break;
				case 2:
					cond_count += fgstatus[fg_idx].counts[FG_COUNT_DESTROYED];
					cond_specific_total += fgstatus[fg_idx].special_counts[FG_COUNT_DESTROYED];
					if (exclude_player) {
						cond_count += fgstatus[fg_idx].counts[FG_COUNT_HYPERSPACED];
						cond_specific_total += fgstatus[fg_idx].special_counts[FG_COUNT_HYPERSPACED];
						cond_count += fgstatus[fg_idx].counts[FG_COUNT_HANGAR];
						cond_specific_total += fgstatus[fg_idx].special_counts[FG_COUNT_HANGAR];
					} else {
						cond_destroyed_count += fgstatus[fg_idx].counts[FG_COUNT_HYPERSPACED];
						cond_destroyed_specific += fgstatus[fg_idx].special_counts[FG_COUNT_HYPERSPACED];
						cond_destroyed_count += fgstatus[fg_idx].counts[FG_COUNT_HANGAR];
						cond_destroyed_specific += fgstatus[fg_idx].special_counts[FG_COUNT_HANGAR];
					}
					break;
				case 3:
					cond_count += fgstatus[fg_idx].counts[FG_COUNT_ATTACKED];
					cond_specific_total += fgstatus[fg_idx].special_counts[FG_COUNT_ATTACKED];
					cond_destroyed_count += fgstatus[fg_idx].counts[FG_COUNT_LEFT_UNATTACKED];
					cond_destroyed_specific += fgstatus[fg_idx].special_counts[FG_COUNT_LEFT_UNATTACKED];
					if (pstate.player_fg_idx == fg_idx && pstate.player_craft->was_hit_flag)
						player_matched = 1;
					break;
				case 4:
					cond_count += fgstatus[fg_idx].counts[FG_COUNT_CAPTURED];
					cond_specific_total += fgstatus[fg_idx].special_counts[FG_COUNT_CAPTURED];
					cond_destroyed_count += fgstatus[fg_idx].counts[FG_COUNT_LEFT_UNCAPTURED];
					cond_destroyed_specific += fgstatus[fg_idx].special_counts[FG_COUNT_LEFT_UNCAPTURED];
					break;
				case 5:
					cond_count += fgstatus[fg_idx].counts[FG_COUNT_INSPECTED];
					cond_specific_total += fgstatus[fg_idx].special_counts[FG_COUNT_INSPECTED];
					cond_destroyed_count += fgstatus[fg_idx].counts[FG_COUNT_LEFT_UNINSPECTED];
					cond_destroyed_specific += fgstatus[fg_idx].special_counts[FG_COUNT_LEFT_UNINSPECTED];
					break;
				case 6:
					cond_count += fgstatus[fg_idx].counts[FG_COUNT_BOARDED];
					cond_specific_total += fgstatus[fg_idx].special_counts[FG_COUNT_BOARDED];
					cond_destroyed_count += fgstatus[fg_idx].counts[FG_COUNT_LEFT_UNBOARDED];
					cond_destroyed_specific += fgstatus[fg_idx].special_counts[FG_COUNT_LEFT_UNBOARDED];
					break;
				case 7:
					cond_count += fgstatus[fg_idx].counts[FG_COUNT_DOCKED];
					cond_specific_total += fgstatus[fg_idx].special_counts[FG_COUNT_DOCKED];
					cond_destroyed_count += fgstatus[fg_idx].counts[FG_COUNT_LEFT_UNDOCKED];
					cond_destroyed_specific += fgstatus[fg_idx].special_counts[FG_COUNT_LEFT_UNDOCKED];
					break;
				case 8:
					cond_count += fgstatus[fg_idx].counts[FG_COUNT_DISABLED];
					cond_specific_total += fgstatus[fg_idx].special_counts[FG_COUNT_DISABLED];
					cond_destroyed_count += fgstatus[fg_idx].counts[FG_COUNT_LEFT_UNDISABLED];
					cond_destroyed_specific += fgstatus[fg_idx].special_counts[FG_COUNT_LEFT_UNDISABLED];
					break;
				case 9:
					/* Survivors: arrived minus destroyed. */
					cond_count += fgstatus[fg_idx].counts[FG_COUNT_ARRIVED] -
								  fgstatus[fg_idx].counts[FG_COUNT_DESTROYED];
					cond_specific_total += fgstatus[fg_idx].special_counts[FG_COUNT_ARRIVED] -
										   fgstatus[fg_idx].special_counts[FG_COUNT_DESTROYED];
					cond_destroyed_count += fgstatus[fg_idx].counts[FG_COUNT_DESTROYED];
					cond_destroyed_specific += fgstatus[fg_idx].special_counts[FG_COUNT_DESTROYED];
					break;
				case 12:
					cond_count += fgstatus[fg_idx].counts[FG_COUNT_HYPERSPACED];
					cond_specific_total += fgstatus[fg_idx].special_counts[FG_COUNT_HYPERSPACED];
					cond_count += fgstatus[fg_idx].counts[FG_COUNT_HANGAR];
					cond_specific_total += fgstatus[fg_idx].special_counts[FG_COUNT_HANGAR];
					cond_destroyed_count += fgstatus[fg_idx].counts[FG_COUNT_DESTROYED];
					cond_destroyed_specific += fgstatus[fg_idx].special_counts[FG_COUNT_DESTROYED];
					break;
				case 21:
				case 22:
				case 23:
				case 24: {
					/* Scan the FG's live craft and test a per-craft predicate. */
					uint16_t i;

					for (i = 0; i < NUM_CRAFTS; i++) {
						CraftData* cd;
						int16_t failed;

						if (!objects[i].ship_idx || fg_idx != objects[i].fg_idx)
							continue;
						cd = objects[i].craft_ptr;
						failed = 0;
						if (cond == 21) {
							/* Watcom emitted unaligned-dword-HIWORD reads on cargo[14] and
							 * forward_shield, which decode to forward_shield and rear_shield
							 * respectively (the int16 sitting two bytes past each named field). */
							if (cd->forward_shield + cd->rear_shield <= 0)
								failed = 1;
						} else if (cond == 22) {
							if (cd->hull_damage > cd->hull_max / 2)
								failed = 1;
						} else if (cond == 23) {
							uint16_t ammo = 0;
							uint16_t k;

							for (k = 0; k < cd->missile_group_cnt; k++) {
								ammo += cd->weapon_slots[spec_data[cd->species_idx].missile_start[k]].ammo;
								ammo += cd->weapon_slots[spec_data[cd->species_idx].missile_end[k]].ammo;
							}
							if (!ammo)
								failed = 1;
						} else {
							if (!(cd->status_flags & 0x10))
								failed = 1;
						}
						if (failed) {
							cond_count++;
							if ((int8_t)fg_array[fg_idx].special_craft == cd->craft_idx_in_fg)
								cond_specific_total++;
							if (i == pstate.object_idx)
								player_matched++;
						} else {
							cond_destroyed_count++;
							if ((int8_t)fg_array[fg_idx].special_craft == cd->craft_idx_in_fg)
								cond_destroyed_specific++;
						}
					}
					break;
				}
			}
		}

		result = 4;
		if (total_craft) {
			switch (amount_op) {
				case 0:
					if (total_craft == cond_count)
						result = 1;
					else if (cond_destroyed_count)
						result = 2;
					break;
				case 1:
					if (math2_percentage(cond_count, total_craft) >= 0xC000)
						result = 1;
					else if (math2_percentage(cond_destroyed_count, total_craft) > 0x4000)
						result = 2;
					break;
				case 2:
					if (math2_percentage(cond_count, total_craft) >= 0x8000)
						result = 1;
					else if (math2_percentage(cond_destroyed_count, total_craft) > 0x8000)
						result = 2;
					break;
				case 3:
					if (math2_percentage(cond_count, total_craft) >= 0x4000)
						result = 1;
					else if (math2_percentage(cond_destroyed_count, total_craft) > 0xC000)
						result = 2;
					break;
				case 4:
				case 14:
					if (cond_count)
						result = 1;
					else if (total_craft == cond_destroyed_count)
						result = 2;
					break;
				case 5:
					if (cond_count == total_craft - 1)
						result = 1;
					else if (cond_destroyed_count >= 2)
						result = 2;
					break;
				case 6:
					if (specific_total) {
						if (specific_total == cond_specific_total)
							result = 1;
						else if (cond_destroyed_specific)
							result = 2;
					}
					break;
				case 7:
					if (total_craft - specific_total == cond_count)
						result = 1;
					else if (cond_destroyed_count && !cond_specific_total)
						result = 2;
					else if (cond_destroyed_count > 1 && cond_specific_total)
						result = 2;
					break;
				case 8:
					if (total_craft - player_matched == cond_count)
						result = 1;
					break;
				case 9:
					if (player_matched)
						result = 1;
					break;
				case 10:
					if (destroyed_total == cond_count)
						result = 1;
					break;
				case 11:
					if (math2_percentage(cond_count, destroyed_total) >= 0xC000)
						result = 1;
					break;
				case 12:
					if (math2_percentage(cond_count, destroyed_total) >= 0x8000)
						result = 1;
					break;
				case 13:
					if (math2_percentage(cond_count, destroyed_total) >= 0x4000)
						result = 1;
					break;
				case 15:
					if (cond_count == destroyed_total - 1)
						result = 1;
					break;
			}
		}
	} else {
		/* Mission-level direct check. */
		result = 4;
		switch (cond) {
			case 0:
				result = 1;
				break;
			case 10:
				result = 0;
				break;
			case 13:
				if (mission.primary_complete == 1)
					result = 1;
				else if (mission.primary_complete == 2)
					result = 2;
				break;
			case 14:
				if (mission.primary_complete == 2)
					result = 1;
				else if (mission.primary_complete == 1)
					result = 2;
				break;
			case 15:
				if (mission.secondary_complete == 1)
					result = 1;
				else if (mission.secondary_complete == 2)
					result = 2;
				break;
			case 16:
				if (mission.secondary_complete == 2)
					result = 1;
				else if (mission.secondary_complete == 1)
					result = 2;
				break;
			case 17:
				if (mission.bonus_complete == 1)
					result = 1;
				else if (mission.bonus_complete == 2)
					result = 2;
				break;
			case 18:
				if (mission.bonus_complete == 2)
					result = 1;
				else if (mission.bonus_complete == 1)
					result = 2;
				break;
			case 20:
				if (mission.penalty_flag)
					result = 1;
				else
					result = 2;
				break;
		}
	}
	return result;
}

/* Voiced "objectives complete / failed" announcements pull a matching pair
 * of craft-name strings from the .TIE file header. In the binary these alias
 * into _missionheader as byte_F8366 / byte_F83E6 / byte_F8466, but that
 * memory is actually EMissionStruct.win_msg1[2][64] (primary-complete),
 * win_msg2[2][64] (secondary-complete) and loss_msg[2][64]
 * (objectives-failed). */

/* ====================================================================
 * score_fgmemberofgroup
 * ==================================================================== */

// FUNCTION: TIE95 0x524F0
int16_t score_fgmemberofgroup(uint16_t fg_idx, uint16_t group_type, uint16_t group_id) {
	uint16_t fg_spec;
	int16_t result;

	result = 0;
	fg_spec = speciesconvert[(int8_t)fg_array[fg_idx].species];

	switch (group_type) {
		case 0:
			break;
		case GTT_FG:
			if (group_id == fg_idx)
				result = 1;
			break;
		case GTT_SPECIES:
			if (speciesconvert[group_id + 1] == fg_spec)
				result = 1;
			break;
		case GTT_GENUS:
			if (genusconvert[group_id] == species_table[fg_spec].ship_class)
				result = 1;
			break;
		case GTT_FAMILY:
			if (familyconvert[group_id] == species_table[fg_spec].category)
				result = 1;
			break;
		case GTT_SIDE:
			if (group_id == (int8_t)fg_array[fg_idx].side)
				result = 1;
			break;
		case GTT_AI_ORDER:
			if (group_id == (int8_t)fg_array[fg_idx].ai[0].order)
				result = 1;
			break;
		case GTT_CRAFT_ATTR:
			break;
		case GTT_ALL_IN_SET:
			if (group_id == (int8_t)fg_array[fg_idx].set)
				result = 1;
			break;
		case GTT_SKILL:
			if (group_id == (int8_t)fg_array[fg_idx].skill)
				result = 1;
			break;
		case GTT_VERSION:
			if (group_id == (int8_t)fg_array[fg_idx].version)
				result = 1;
			break;
		case GTT_ALL_FG:
			result = 1;
	}
	return result;
}

/* ====================================================================
 * score_objectmemberofgroup
 * ==================================================================== */

// FUNCTION: TIE95 0x526D0
int16_t score_objectmemberofgroup(uint16_t obj_idx, uint16_t group_type, uint16_t group_id) {
	uint16_t fg_idx;
	uint16_t fg_spec;
	CraftData* craft_ptr;
	int16_t result;

	if (obj_idx < 0x3800) {
		fg_idx = objects[obj_idx].fg_idx;
		craft_ptr = objects[obj_idx].craft_ptr;
	} else {
		fg_idx = staticobjects[obj_idx - 0x3800].fg_idx;
	}

	result = 0;
	fg_spec = speciesconvert[(int8_t)fg_array[fg_idx].species];

	switch (group_type) {
		case 0:
			break;
		case GTT_FG:
			if (group_id == fg_idx)
				result = 1;
			break;
		case GTT_SPECIES:
			if (speciesconvert[group_id + 1] == fg_spec)
				result = 1;
			break;
		case GTT_GENUS:
			if (genusconvert[group_id] == species_table[fg_spec].ship_class)
				result = 1;
			break;
		case GTT_FAMILY:
			if (familyconvert[group_id] == species_table[fg_spec].category)
				result = 1;
			break;
		case GTT_SIDE:
			if (obj_idx >= 0x3800) {
				if (group_id == (int8_t)fg_array[fg_idx].side)
					result = 1;
			} else if (group_id == objects[obj_idx].side) {
				result = 1;
			}
			break;
		case GTT_AI_ORDER:
			if (group_id == (int8_t)fg_array[fg_idx].ai[0].order)
				result = 1;
			break;
		case GTT_CRAFT_ATTR:
			/* Craft-attribute predicate; only meaningful for regular objects. */
			if (obj_idx >= 0x3800)
				break;
			switch (group_id) {
				case 0:
					if (craft_ptr->dock_state_flags != 0)
						result = 1;
					break;
				case 1:
					if (craft_ptr->inspected != 0)
						result = 1;
					break;
				case 2:
					if (craft_ptr->board_count != 0)
						result = 1;
					break;
				case 3:
					if (craft_ptr->capture_count != 0)
						result = 1;
					break;
				case 4:
					if (craft_ptr->status_flags == 0)
						result = 1;
					break;
				case 5:
					if (craft_ptr->was_hit_flag != 0)
						result = 1;
					break;
				case 6:
					if (craft_ptr->hull_damage != 0)
						result = 1;
					break;
				case 7:
					if (craft_ptr->craft_idx_in_fg == (int8_t)fg_array[fg_idx].special_craft)
						result = 1;
					break;
				case 8:
					if (craft_ptr->craft_idx_in_fg != (int8_t)fg_array[fg_idx].special_craft)
						result = 1;
					break;
				case 9:
					if (obj_idx == pstate.object_idx)
						result = 1;
					break;
				case 10:
					if (obj_idx != pstate.object_idx)
						result = 1;
					break;
			}
			break;
		case GTT_ALL_IN_SET:
			if (group_id == (int8_t)fg_array[fg_idx].set)
				result = 1;
			break;
		case GTT_SKILL:
			if (group_id == (int8_t)fg_array[fg_idx].skill)
				result = 1;
			break;
		case GTT_VERSION:
			if (group_id == (int8_t)fg_array[fg_idx].version)
				result = 1;
			break;
		case GTT_ALL_FG:
			result = 1;
	}
	return result;
}

/* ====================================================================
 * score_craftexitscoring
 * ==================================================================== */

// FUNCTION: TIE95 0x52A9C
void score_craftexitscoring(uint16_t obj_idx, uint16_t fg_idx, uint16_t exit_kind) {
	uint16_t j;
	CraftData* cd = objects[obj_idx].craft_ptr;

	TIE_FLIGHT_TRACE_FG_EXIT(obj_idx, exit_kind);

	/* exit_kind is the FG_COUNT_* slot for this exit. */
	fgstatus[fg_idx].counts[exit_kind]++;
	if ((int8_t)fg_array[fg_idx].special_craft == cd->craft_idx_in_fg)
		fgstatus[fg_idx].special_counts[exit_kind] = 1;

	/* FG_COUNT_LEFT_* slots: the craft left without the matching event. */
	if (!cd->inspected) {
		fgstatus[fg_idx].counts[FG_COUNT_LEFT_UNINSPECTED]++;
		if ((int8_t)fg_array[fg_idx].special_craft == cd->craft_idx_in_fg)
			fgstatus[fg_idx].special_counts[FG_COUNT_LEFT_UNINSPECTED] = 1;
	}
	if (!cd->pad_0B6) {
		fgstatus[fg_idx].counts[FG_COUNT_LEFT_UNDISABLED]++;
		if ((int8_t)fg_array[fg_idx].special_craft == cd->craft_idx_in_fg)
			fgstatus[fg_idx].special_counts[FG_COUNT_LEFT_UNDISABLED] = 1;
	}
	if (!cd->dock_state_flags) {
		fgstatus[fg_idx].counts[FG_COUNT_LEFT_UNCAPTURED]++;
		if ((int8_t)fg_array[fg_idx].special_craft == cd->craft_idx_in_fg)
			fgstatus[fg_idx].special_counts[FG_COUNT_LEFT_UNCAPTURED] = 1;
	}
	if (!cd->was_hit_flag) {
		fgstatus[fg_idx].counts[FG_COUNT_LEFT_UNATTACKED]++;
		if ((int8_t)fg_array[fg_idx].special_craft == cd->craft_idx_in_fg)
			fgstatus[fg_idx].special_counts[FG_COUNT_LEFT_UNATTACKED] = 1;
	}
	if (!cd->board_count) {
		fgstatus[fg_idx].counts[FG_COUNT_LEFT_UNBOARDED]++;
		if ((int8_t)fg_array[fg_idx].special_craft == cd->craft_idx_in_fg)
			fgstatus[fg_idx].special_counts[FG_COUNT_LEFT_UNBOARDED] = 1;
	}
	if (!cd->capture_count) {
		fgstatus[fg_idx].counts[FG_COUNT_LEFT_UNDOCKED]++;
		if ((int8_t)fg_array[fg_idx].special_craft == cd->craft_idx_in_fg)
			fgstatus[fg_idx].special_counts[FG_COUNT_LEFT_UNDOCKED] = 1;
	}

	/* Destruction (exit_kind == 2): link-code tick + propagation to other FGs
	 * whose arrival depends on this one. */
	if (exit_kind == 2) {
		uint16_t i;

		if (fg_array[fg_idx].link_flag) {
			if (++mission.mission_linked_data[fg_array[fg_idx].link_code] == 0)
				mission.mission_linked_data[fg_array[fg_idx].link_code] = 0xFFu; /* -1 */
		}

		for (i = 0; i < mission_file_header.num_fg; i++) {
			uint16_t d_cnt;
			uint16_t d_cid;

			if (i == fg_idx)
				continue;
			if (!fg_array[i].start_fg_used)
				continue;
			if ((int8_t)fg_array[i].start_fg != fg_idx)
				continue;

			d_cnt = fgstatus[i].counts[FG_COUNT_TOTAL] - fgstatus[i].counts[FG_COUNT_ARRIVED];
			d_cid = fgstatus[i].special_counts[FG_COUNT_TOTAL] - fgstatus[i].special_counts[FG_COUNT_ARRIVED];

			/* Craft that will no longer arrive count as destroyed and as never
			 * attacked, captured, inspected, boarded or disabled. */
			fgstatus[i].counts[FG_COUNT_DESTROYED] += d_cnt;
			fgstatus[i].special_counts[FG_COUNT_DESTROYED] += d_cid;
			fgstatus[i].counts[FG_COUNT_LEFT_UNINSPECTED] += d_cnt;
			fgstatus[i].special_counts[FG_COUNT_LEFT_UNINSPECTED] += d_cid;
			fgstatus[i].counts[FG_COUNT_LEFT_UNDISABLED] += d_cnt;
			fgstatus[i].special_counts[FG_COUNT_LEFT_UNDISABLED] += d_cid;
			fgstatus[i].counts[FG_COUNT_LEFT_UNCAPTURED] += d_cnt;
			fgstatus[i].special_counts[FG_COUNT_LEFT_UNCAPTURED] += d_cid;
			fgstatus[i].counts[FG_COUNT_LEFT_UNATTACKED] += d_cnt;
			fgstatus[i].special_counts[FG_COUNT_LEFT_UNATTACKED] += d_cid;
			fgstatus[i].counts[FG_COUNT_LEFT_UNBOARDED] += d_cnt;
			fgstatus[i].special_counts[FG_COUNT_LEFT_UNBOARDED] += d_cid;

			fgstatus[i].active = 1;
			fgstatus[i].waves_remaining = 0;
		}
	}

	/* Clear any craft's attacker_idx that was targeting the exiting obj. */
	for (j = 0; j < NUM_CRAFTS; j++) {
		CraftData* oc;

		if (!objects[j].ship_idx)
			continue;
		oc = objects[j].craft_ptr;
		if (oc->attacker_idx == obj_idx)
			oc->attacker_idx = 255;
	}
}
