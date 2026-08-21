#include <stddef.h>
#include <stdint.h>

#include "tie/create.h" /* speciesconvert, genusconvert, familyconvert, diffmask, fgdiffmask */
#include "tie/fediskio.h"
#include "tie/fscript.h"
#include "tie/fsfx.h"
#include "tie/math2.h"
#include "tie/mission.h" /* RUNTIME_MissionState */
#include "tie/msg.h"
#include "tie/msg_templates.h"
#include "tie/score.h"
#include "tie/shipext.h" /* EFGStruct, EAIStruct */
#include "tie/tie.h"
#include "tie/user.h"

/* --- Module-owned globals (watdbg: score.c) -------------------------- */

/* 5-entry LUT mapping per-FG pct bucket (0..4) -> amount_op for checkcondition. */
int16_t percentcon[5] = { 0, 2, 4, 5, 6 };

/* Per-cond-code dispatch flag: 0 = mission-level direct check,
 *                              1 = FG-iteration evaluator.
 * Frozen at load time; mirrors the _conditiongrouprelated data table. */
uint8_t conditiongrouprelated[26] = { 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1,
									  0, 0, 0, 0, 0, 0, 1, 0, 1, 1, 1, 1, 0 };

/* Voiced "objectives complete / failed" announcements pull a matching pair
 * of craft-name strings from the .TIE file header. In the binary these alias
 * into _missionheader as byte_F8366 / byte_F83E6 / byte_F8466, but that
 * memory is actually EMissionStruct.win_msg1[2][64] (primary-complete),
 * win_msg2[2][64] (secondary-complete) and loss_msg[2][64]
 * (objectives-failed). */
#define PRI_COMPLETE_NAME(k) ((const uint8_t*)&mission_file_header.mission.win_msg1[(k)][0])
#define SEC_COMPLETE_NAME(k) ((const uint8_t*)&mission_file_header.mission.win_msg2[(k)][0])
#define OBJ_FAILED_NAME(k) ((const uint8_t*)&mission_file_header.mission.loss_msg[(k)][0])

/* ====================================================================
 * score_fgmemberofgroup
 * ==================================================================== */

// FUNCTION: TIE 0x524F0
int8_t score_fgmemberofgroup(uint16_t fg_idx, uint8_t group_type, uint16_t group_id) {
	const EFGStruct* const f = &fg_array[fg_idx];
	const uint8_t fg_spec = speciesconvert[f->species];

	switch (group_type) {
		case GTT_FG:
			return (int8_t)(group_id == fg_idx);
		case GTT_SPECIES:
			/* Binary uses byte_D1219 = speciesconvert+1: speciesconvert[gid+1]. */
			return (int8_t)(speciesconvert[group_id + 1] == fg_spec);
		case GTT_GENUS:
			return (int8_t)(genusconvert[group_id] == species_table[fg_spec].ship_class);
		case GTT_FAMILY:
			return (int8_t)(familyconvert[group_id] == species_table[fg_spec].category);
		case GTT_SIDE:
			return (int8_t)(group_id == f->side);
		case GTT_AI_ORDER:
			return (int8_t)(group_id == f->ai[0].order);
		case GTT_CRAFT_ATTR:
		case GTT_ALL_FG:
			return 1;
		case GTT_ALL_IN_SET:
			return (int8_t)(group_id == f->set);
		case GTT_SKILL:
			return (int8_t)(group_id == f->skill);
		case GTT_VERSION:
			return (int8_t)(group_id == f->version);
		default:
			return 0;
	}
}

/* ====================================================================
 * score_objectmemberofgroup
 * ==================================================================== */

// FUNCTION: TIE 0x526D0
int8_t score_objectmemberofgroup(uint16_t obj_idx, uint8_t group_type, uint8_t group_id) {
	uint16_t fg_idx;
	CraftData* craft_ptr = NULL;

	if (obj_idx >= 0x3800u) {
		fg_idx = staticobjects[obj_idx - 0x3800u].fg_idx;
	} else {
		fg_idx = objects[obj_idx].fg_idx;
		craft_ptr = objects[obj_idx].craft_ptr;
	}

	const EFGStruct* const f = &fg_array[fg_idx];
	const uint8_t fg_spec = speciesconvert[f->species];

	switch (group_type) {
		case GTT_FG:
			return (int8_t)(group_id == fg_idx);
		case GTT_SPECIES:
			return (int8_t)(speciesconvert[group_id + 1] == fg_spec);
		case GTT_GENUS:
			return (int8_t)(genusconvert[group_id] == species_table[fg_spec].ship_class);
		case GTT_FAMILY:
			return (int8_t)(familyconvert[group_id] == species_table[fg_spec].category);
		case GTT_SIDE:
			return (int8_t)(group_id == f->side);
		case GTT_AI_ORDER:
			return (int8_t)(group_id == f->ai[0].order);
		case GTT_CRAFT_ATTR:
			/* Craft-attribute predicate; only meaningful for regular objects. */
			if (obj_idx >= 0x3800u || craft_ptr == NULL)
				return 0;
			switch (group_id) {
				case 0:
					return (int8_t)(craft_ptr->dock_state_flags != 0);
				case 1:
					return (int8_t)(craft_ptr->inspected != 0);
				case 2:
					return (int8_t)(craft_ptr->board_count != 0);
				case 3:
					return (int8_t)(craft_ptr->capture_count != 0);
				case 4:
					return (int8_t)(craft_ptr->status_flags == 0);
				case 5:
					return (int8_t)(craft_ptr->was_hit_flag != 0);
				case 6:
					return (int8_t)(craft_ptr->hull_damage != 0);
				case 7:
					return (int8_t)(craft_ptr->craft_idx_in_fg == f->special_craft);
				case 8:
					return (int8_t)(craft_ptr->craft_idx_in_fg != f->special_craft);
				case 9:
					return (int8_t)(obj_idx == pstate.object_idx);
				case 10:
					return (int8_t)(obj_idx != pstate.object_idx);
				default:
					return 0;
			}
		case GTT_ALL_IN_SET:
			return (int8_t)(group_id == f->set);
		case GTT_SKILL:
			return (int8_t)(group_id == f->skill);
		case GTT_VERSION:
			return (int8_t)(group_id == f->version);
		case GTT_ALL_FG:
			return 1;
		default:
			return 0;
	}
}

/* ====================================================================
 * score_craftexitscoring
 * ==================================================================== */

// FUNCTION: TIE 0x52A9C
void score_craftexitscoring(uint16_t obj_idx, uint16_t fg_idx, uint16_t exit_kind) {
	FGStatus* const fs = &fgstatus[fg_idx];
	const EFGStruct* const f = &fg_array[fg_idx];
	CraftData* const cd = objects[obj_idx].craft_ptr;
	const uint8_t special = f->special_craft;

	/* Primary bucket (byte-indexed via exit_kind). */
	((&fs->cond[0].count))[exit_kind]++;
	if (cd->craft_idx_in_fg == special)
		((&fs->cond_id[0].count))[exit_kind] = 1;

/* Conditional buckets: each bumps its own cond[N].count when the
 * craft's flag is CLEAR (i.e. the exit qualifies for that bucket). */
#define BUMP(flag_expr, slot)                                                                                \
	if (!(flag_expr)) {                                                                                      \
		fs->cond[(slot)].count++;                                                                            \
		if (cd->craft_idx_in_fg == special)                                                                  \
			fs->cond_id[(slot)].count = 1;                                                                   \
	}

	BUMP(cd->inspected, 5)
	BUMP(cd->pad_0B6, 8)
	BUMP(cd->dock_state_flags, 4)
	BUMP(cd->was_hit_flag, 3)
	BUMP(cd->board_count, 6)
	BUMP(cd->capture_count, 7)

#undef BUMP

	/* Destruction (exit_kind == 2): link-code tick + propagation to other FGs
	 * whose arrival depends on this one. */
	if (exit_kind == 2) {
		if (f->link_flag) {
			uint8_t linked = mission.mission_linked_data[f->link_code] + 1;
			mission.mission_linked_data[f->link_code] = linked;
			if (linked == 0)
				mission.mission_linked_data[f->link_code] = 0xFFu; /* -1 */
		}

		for (uint16_t i = 0; i < (uint16_t)mission_file_header.num_fg; i++) {
			if (i == fg_idx)
				continue;
			if (!fg_array[i].start_fg_used)
				continue;
			if (fg_array[i].start_fg != (uint8_t)fg_idx)
				continue;

			FGStatus* const cs = &fgstatus[i];
			const int8_t d_cnt = (int8_t)(cs->cond[0].count - cs->cond[0].detail);
			const int8_t d_cid = (int8_t)(cs->cond_id[0].count - cs->cond_id[0].detail);

			/* Propagate both deltas across cond[1/3/4/5/6/8]. The binary
			 * interleaves the 12 writes; here we emit them in a cleaner
			 * order -- the end state is identical since none of the reads
			 * alias any of the writes. */
			cs->cond[1].count = (uint8_t)(cs->cond[1].count + d_cnt);
			cs->cond_id[1].count = (uint8_t)(cs->cond_id[1].count + d_cid);
			cs->cond[3].count = (uint8_t)(cs->cond[3].count + d_cnt);
			cs->cond_id[3].count = (uint8_t)(cs->cond_id[3].count + d_cid);
			cs->cond[4].count = (uint8_t)(cs->cond[4].count + d_cnt);
			cs->cond_id[4].count = (uint8_t)(cs->cond_id[4].count + d_cid);
			cs->cond[5].count = (uint8_t)(cs->cond[5].count + d_cnt);
			cs->cond_id[5].count = (uint8_t)(cs->cond_id[5].count + d_cid);
			cs->cond[6].count = (uint8_t)(cs->cond[6].count + d_cnt);
			cs->cond_id[6].count = (uint8_t)(cs->cond_id[6].count + d_cid);
			cs->cond[8].count = (uint8_t)(cs->cond[8].count + d_cnt);
			cs->cond_id[8].count = (uint8_t)(cs->cond_id[8].count + d_cid);

			cs->waves_remaining = 0;
			cs->active = 1;
		}
	}

	/* Clear any craft's attacker_idx that was targeting the exiting obj. */
	for (uint16_t j = 0; j < NUM_CRAFTS; j++) {
		if (!objects[j].ship_idx)
			continue;
		CraftData* oc = objects[j].craft_ptr;
		if (obj_idx == oc->attacker_idx)
			oc->attacker_idx = 255;
	}
}

/* ====================================================================
 * score_checkcondition (body)
 * ==================================================================== */

/* Helper: craft live-state predicate for cond 21..24. Returns nonzero when
 * the craft at object slot `i` satisfies the cond-specific filter. */
static int craft_pred_live(uint8_t cond, const CraftData* cd) {
	switch (cond) {
		case 21:
			/* Watcom emitted unaligned-dword-HIWORD reads on cargo[14] and
			 * forward_shield, which decode to forward_shield and rear_shield
			 * respectively (the int16 sitting two bytes past each named field). */
			return (cd->forward_shield + cd->rear_shield) > 0;
		case 22:
			return cd->hull_damage <= (uint16_t)(cd->hull_max / 2);
		case 23: {
			uint16_t ammo = 0;
			for (int k = 0; k < (int)cd->missile_group_cnt; k++) {
				ammo = (uint16_t)(ammo + cd->weapon_slots[spec_data[cd->species_idx].missile_end[k]].ammo +
								  cd->weapon_slots[spec_data[cd->species_idx].missile_start[k]].ammo);
			}
			return ammo != 0;
		}
		default: /* cond 24 and fallback */
			return (cd->status_flags & 0x10u) != 0;
	}
}

/* Mission-level direct-check path (used when conditiongrouprelated[cond]==0). */
static int8_t check_mission_level(uint8_t cond) {
	const int8_t INCOMPLETE = 4;

	if (cond < 15u) {
		if (cond < 10u)
			return (cond == 0) ? 1 : INCOMPLETE; /* 0=true; 1..9=incomplete */
		if (cond == 10u)
			return 0; /* always-false */
		if (cond < 13u)
			return INCOMPLETE; /* 11, 12: fall-through */
		if (cond == 13u) {
			if (mission.primary_complete == 1)
				return 1;
			if (mission.primary_complete == 2)
				return 2;
			return INCOMPLETE;
		}
		/* cond == 14: mirror of 13 with 1/2 swapped. */
		if (mission.primary_complete == 2)
			return 1;
		if (mission.primary_complete == 1)
			return 2;
		return INCOMPLETE;
	}

	if (cond == 15u) {
		if (mission.secondary_complete == 1)
			return 1;
		if (mission.secondary_complete == 2)
			return 2;
		return INCOMPLETE;
	}
	if (cond == 16u) {
		if (mission.secondary_complete == 2)
			return 1;
		if (mission.secondary_complete == 1)
			return 2;
		return INCOMPLETE;
	}
	if (cond == 17u) {
		if (mission.bonus_complete == 1)
			return 1;
		if (mission.bonus_complete == 2)
			return 2;
		return INCOMPLETE;
	}
	if (cond == 18u) {
		if (mission.bonus_complete == 2)
			return 1;
		if (mission.bonus_complete == 1)
			return 2;
		return INCOMPLETE;
	}
	if (cond == 20u)
		return mission.penalty_flag ? 1 : 2;

	return INCOMPLETE;
}

// FUNCTION: TIE 0x51698
int8_t score_checkcondition(uint8_t cond, uint8_t target_type, uint8_t target_id, uint8_t amount_op,
							int8_t exclude_player) {
	if (!conditiongrouprelated[cond])
		return check_mission_level(cond);

	if (!target_type)
		return 2; /* No target -> failed. */

	/* Accumulators (all uint16 running totals across the FG loop). */
	uint16_t total_craft = 0;             /* sum(cond[0].count)    */
	uint16_t destroyed_total = 0;         /* sum(cond[0].detail)   */
	uint16_t specific_total = 0;          /* sum(cond_id[0].count) */
	uint16_t cond_count = 0;              /* cond-specific count bucket */
	uint16_t cond_specific_total = 0;     /* parallel cond_id counter   */
	uint16_t cond_destroyed_count = 0;    /* "remainder" bucket          */
	uint16_t cond_destroyed_specific = 0; /* parallel cond_id remainder  */
	uint16_t player_matched = 0;

	for (uint16_t fg_idx = 0; fg_idx < (uint16_t)mission_file_header.num_fg; fg_idx++) {
		const EFGStruct* const f = &fg_array[fg_idx];
		if (!f->species)
			continue;
		if (!score_fgmemberofgroup(fg_idx, target_type, target_id))
			continue;

		const FGStatus* const fs = &fgstatus[fg_idx];
		total_craft = (uint16_t)(total_craft + fs->cond[0].count);
		specific_total = (uint16_t)(specific_total + fs->cond_id[0].count);
		destroyed_total = (uint16_t)(destroyed_total + fs->cond[0].detail);

		if (cond < 6u) {
			if (cond < 3u) {
				if (cond == 0u) {
					/* Handled by check_mission_level; unreachable here. */
				} else if (cond == 1u) {
					/* Captured/disabled: cond[0].detail + cond_id[0].detail;
					 * also flag player match. */
					cond_count = (uint16_t)(cond_count + fs->cond[0].detail);
					cond_specific_total = (uint16_t)(cond_specific_total + fs->cond_id[0].detail);
					if ((uint16_t)pstate.player_fg_idx == fg_idx)
						player_matched = 1;
				} else {
					/* cond == 2 (destroyed): cond[1].count + cond[1].detail + cond[2].count.
					 * exclude_player folds into same accumulator pair; else splits. */
					cond_count = (uint16_t)(cond_count + fs->cond[1].count);
					cond_specific_total = (uint16_t)(cond_specific_total + fs->cond_id[1].count);
					if (exclude_player) {
						cond_count = (uint16_t)(cond_count + fs->cond[1].detail + fs->cond[2].count);
						cond_specific_total =
							(uint16_t)(cond_specific_total + fs->cond_id[1].detail + fs->cond_id[2].count);
					} else {
						cond_destroyed_count =
							(uint16_t)(cond_destroyed_count + fs->cond[1].detail + fs->cond[2].count);
						cond_destroyed_specific = (uint16_t)(cond_destroyed_specific + fs->cond_id[1].detail +
															 fs->cond_id[2].count);
					}
				}
			} else if (cond == 3u) {
				cond_count = (uint16_t)(cond_count + fs->cond[2].detail);
				cond_specific_total = (uint16_t)(cond_specific_total + fs->cond_id[2].detail);
				cond_destroyed_count = (uint16_t)(cond_destroyed_count + fs->cond[3].count);
				cond_destroyed_specific = (uint16_t)(cond_destroyed_specific + fs->cond_id[3].count);
				if ((uint16_t)pstate.player_fg_idx == fg_idx && pstate.player_craft->was_hit_flag)
					player_matched = 1;
			} else if (cond == 4u) {
				cond_count = (uint16_t)(cond_count + fs->cond[3].detail);
				cond_specific_total = (uint16_t)(cond_specific_total + fs->cond_id[3].detail);
				cond_destroyed_count = (uint16_t)(cond_destroyed_count + fs->cond[4].count);
				cond_destroyed_specific = (uint16_t)(cond_destroyed_specific + fs->cond_id[4].count);
			} else {
				/* cond == 5 */
				cond_count = (uint16_t)(cond_count + fs->cond[4].detail);
				cond_specific_total = (uint16_t)(cond_specific_total + fs->cond_id[4].detail);
				cond_destroyed_count = (uint16_t)(cond_destroyed_count + fs->cond[5].count);
				cond_destroyed_specific = (uint16_t)(cond_destroyed_specific + fs->cond_id[5].count);
			}
		} else if (cond == 6u) {
			cond_count = (uint16_t)(cond_count + fs->cond[5].detail);
			cond_specific_total = (uint16_t)(cond_specific_total + fs->cond_id[5].detail);
			cond_destroyed_count = (uint16_t)(cond_destroyed_count + fs->cond[6].count);
			cond_destroyed_specific = (uint16_t)(cond_destroyed_specific + fs->cond_id[6].count);
		} else if (cond < 9u) {
			if (cond == 7u) {
				cond_count = (uint16_t)(cond_count + fs->cond[6].detail);
				cond_specific_total = (uint16_t)(cond_specific_total + fs->cond_id[6].detail);
				cond_destroyed_count = (uint16_t)(cond_destroyed_count + fs->cond[7].count);
				cond_destroyed_specific = (uint16_t)(cond_destroyed_specific + fs->cond_id[7].count);
			} else {
				/* cond == 8 */
				cond_count = (uint16_t)(cond_count + fs->cond[7].detail);
				cond_specific_total = (uint16_t)(cond_specific_total + fs->cond_id[7].detail);
				cond_destroyed_count = (uint16_t)(cond_destroyed_count + fs->cond[8].count);
				cond_destroyed_specific = (uint16_t)(cond_destroyed_specific + fs->cond_id[8].count);
			}
		} else if (cond == 9u) {
			/* cond[0].detail - cond[1].count (survivors), mirrored into cond_id. */
			cond_count = (uint16_t)(cond_count + fs->cond[0].detail - fs->cond[1].count);
			cond_specific_total =
				(uint16_t)(cond_specific_total + fs->cond_id[0].detail - fs->cond_id[1].count);
			cond_destroyed_count = (uint16_t)(cond_destroyed_count + fs->cond[1].count);
			cond_destroyed_specific = (uint16_t)(cond_destroyed_specific + fs->cond_id[1].count);
		} else if (cond < 12u) {
			/* cond 10, 11: no per-FG accumulation in the original (10 is always-false,
			 * 11 falls through the goto chain without writes). */
		} else if (cond == 12u) {
			cond_count = (uint16_t)(cond_count + fs->cond[1].detail + fs->cond[2].count);
			cond_specific_total =
				(uint16_t)(cond_specific_total + fs->cond_id[1].detail + fs->cond_id[2].count);
			cond_destroyed_count = (uint16_t)(cond_destroyed_count + fs->cond[1].count);
			cond_destroyed_specific = (uint16_t)(cond_destroyed_specific + fs->cond_id[1].count);
		} else if (cond >= 21u && cond <= 24u) {
			/* Scan live craft of this FG, apply a cond-specific predicate.
			 *
			 * craft_pred_live returns nonzero when the craft is "alive" by
			 * the cond-specific filter (cargo+shield > 0, shield <= max/2,
			 * ammo > 0, status&0x10 != 0). Retail's predicate-TRUE branch
			 * (++cond_count + ++player_matched) fires on the OPPOSITE
			 * condition, so we walk the alive branch into
			 * cond_destroyed_count and the dead branch into cond_count. */
			for (uint16_t i = 0; i < NUM_CRAFTS; i++) {
				if (!objects[i].ship_idx)
					continue;
				if ((uint16_t)objects[i].fg_idx != fg_idx)
					continue;
				const CraftData* cd = objects[i].craft_ptr;
				const int alive = craft_pred_live(cond, cd);
				if (!alive) {
					cond_count++;
					if (cd->craft_idx_in_fg == f->special_craft)
						cond_specific_total++;
					if (i == pstate.object_idx)
						player_matched++;
				} else {
					cond_destroyed_count++;
					if (cd->craft_idx_in_fg == f->special_craft)
						cond_destroyed_specific++;
				}
			}
		}
	}

	const int8_t INCOMPLETE = 4;
	if (total_craft == 0)
		return INCOMPLETE;

	switch (amount_op) {
		case 0:
			if (total_craft == cond_count)
				return 1;
			if (cond_destroyed_count)
				return 2;
			return INCOMPLETE;
		case 1:
			if (math2_percentage(cond_count, total_craft) >= 0xC000u)
				return 1;
			if (math2_percentage(cond_destroyed_count, total_craft) <= 0x4000u)
				return INCOMPLETE;
			return 2;
		case 2:
			if (math2_percentage(cond_count, total_craft) >= 0x8000u)
				return 1;
			if (math2_percentage(cond_destroyed_count, total_craft) <= 0x8000u)
				return INCOMPLETE;
			return 2;
		case 3:
			if (math2_percentage(cond_count, total_craft) >= 0x4000u)
				return 1;
			if (math2_percentage(cond_destroyed_count, total_craft) <= 0xC000u)
				return INCOMPLETE;
			return 2;
		case 4:
		case 14:
			if (cond_count)
				return 1;
			if (total_craft != cond_destroyed_count)
				return INCOMPLETE;
			return 2;
		case 5:
			if (cond_count == (uint16_t)(total_craft - 1))
				return 1;
			if (cond_destroyed_count < 2u)
				return INCOMPLETE;
			return 2;
		case 6:
			if (specific_total == 0)
				return INCOMPLETE;
			if (specific_total == cond_specific_total)
				return 1;
			if (cond_destroyed_specific == 0)
				return INCOMPLETE;
			return 2;
		case 7:
			if ((uint16_t)(total_craft - specific_total) == cond_count)
				return 1;
			if (cond_destroyed_count && cond_specific_total == 0)
				return 2;
			if (cond_destroyed_count <= 1u || cond_specific_total == 0)
				return INCOMPLETE;
			return 2;
		case 8:
			if ((uint16_t)(total_craft - player_matched) != cond_count)
				return INCOMPLETE;
			return 1;
		case 9:
			return player_matched ? 1 : INCOMPLETE;
		case 10:
			return (destroyed_total == cond_count) ? 1 : INCOMPLETE;
		case 11:
			return (math2_percentage(cond_count, destroyed_total) >= 0xC000u) ? 1 : INCOMPLETE;
		case 12:
			return (math2_percentage(cond_count, destroyed_total) >= 0x8000u) ? 1 : INCOMPLETE;
		case 13:
			return (math2_percentage(cond_count, destroyed_total) >= 0x4000u) ? 1 : INCOMPLETE;
		case 15:
			return (cond_count == (uint16_t)(destroyed_total - 1)) ? 1 : INCOMPLETE;
		default:
			return INCOMPLETE;
	}
}

/* ====================================================================
 * score_checkobjective
 * ==================================================================== */

/* Combine two subcondition results (1/2/4) with OR or AND semantics.
 * Returns 1, 2, or 4 (incomplete). */
static int8_t combine_subconds(int8_t a, int8_t b, int or_joined) {
	if (or_joined) {
		if (((a | b) & 1) != 0)
			return 1;
		if ((a & b & 2) != 0)
			return 2;
		return 4;
	}
	if ((a & b & 1) != 0)
		return 1;
	if (((a | b) & 2) != 0)
		return 2;
	return 4;
}

/* Play the "objectives complete" cue for primary/secondary: prints the
 * template, walks the two 64-byte name entries from the .TIE header, then
 * triggers voiced acknowledgement. The bonus variant has no name list
 * (name_kind == NULL). */
typedef enum { NAME_LIST_PRI, NAME_LIST_SEC, NAME_LIST_NONE } NameListKind;

static void play_objectives_complete(uint16_t cooldown_set, MsgTemplate complete_msg, NameListKind name_kind,
									 uint16_t voice_id_first, uint16_t speak_voice, int16_t script_seq,
									 int16_t* cooldown_slot) {
	if (*cooldown_slot == 0) {
		*cooldown_slot = (int16_t)cooldown_set;
		msg_messageprintf(complete_msg);
		/* The first name-list line carries the edition-specific primary
		 * or secondary objective voice cue; the second
		 * line plays silently. Bonus passes voice_id_first=0 because
		 * MSG_BONUS_COMPLETE doesn't pair with a per-mission cue. */
		for (uint16_t k = 0; k < 2u; k++) {
			const uint8_t* p = NULL;
			switch (name_kind) {
				case NAME_LIST_PRI:
					p = PRI_COMPLETE_NAME(k);
					break;
				case NAME_LIST_SEC:
					p = SEC_COMPLETE_NAME(k);
					break;
				case NAME_LIST_NONE:
					break;
			}
			if (p && *p) {
				msg_addmessageptr(0, (char*)p);
				pending_voice_id = (k == 0) ? voice_id_first : 0;
				msg_messageprintf(MSG_GENERIC_STAR);
			}
		}
	}
	fsfx_speakobjectives(speak_voice);
	fscript_MsSetSequence(script_seq);
}

// FUNCTION: TIE 0x50A70
int8_t score_checkobjective(void) {
	int8_t ret_al = (int8_t)mission.player_status;
	if (mission.player_status != 3)
		return ret_al;

	if (mission.train_craft_type || hyperspaceflag)
		return ret_al;

	ret_al = 0;

	int16_t pri_all_ok = 1;
	int16_t any_pri_goal = 0;
	int16_t sec_all_ok = 1;
	int16_t any_sec_goal = 0;
	int16_t bonus_all_ok = 1;
	int16_t any_bonus_goal = 0;

	if (!timers[TIMER_PRIMARY_CHECK] || mission.end_flag) {
		/* --- Phase 1: per-FG evaluation ---------------------------- */
		for (uint16_t fg_idx = 0; fg_idx < (uint16_t)mission_file_header.num_fg; fg_idx++) {
			const EFGStruct* const f = &fg_array[fg_idx];

			/* Diffmask filter keys off f->difficulty (byte at +0x49), NOT
			 * link_flag (+0x46). The binary's `*(int *)&fg.link_flag >> 24`
			 * is the Watcom unaligned-dword + sar 24 idiom that reads the
			 * fourth byte of the dword (link_flag, link_code, link_unused,
			 * difficulty), i.e. difficulty. Wrong field made every FG with
			 * link_flag != difficulty get filtered at medium/hard so its
			 * primary_win never knocked pri_all_ok down — primary objectives
			 * fired complete on frame 1 (B1M2FM Hammer cond=1 "arrived"). */
			if ((diffmask[mission.difficulty] & fgdiffmask[f->difficulty]) == 0) {
				fgstatus[fg_idx].primary_status = 0;
				fgstatus[fg_idx].secondary_status = 0;
				fgstatus[fg_idx].fg_complete = 0;
				continue;
			}

			/* Primary win */
			if (fgstatus[fg_idx].primary_status != 1 || f->pri_win_cond == 9) {
				const uint8_t pri_status = fgstatus[fg_idx].primary_status;
				if (pri_status == 2) {
					pri_all_ok = 0;
					if (!mission.primary_complete)
						mission.primary_complete = 2;
				} else if (pri_status) {
					if (f->pri_win_cond && f->pri_win_cond != 10) {
						any_pri_goal = 1;
						const int8_t r = score_checkcondition(f->pri_win_cond, GTT_FG, (uint8_t)fg_idx,
															  (uint8_t)percentcon[f->pri_win_pct], 0);
						if (r == 2 && !mission.primary_complete)
							mission.primary_complete = 2;
						if (r == 1) {
							mission.primary_fg[fg_idx] = 1;
						} else {
							pri_all_ok = 0;
							mission.primary_fg[fg_idx] = 2;
						}
						fgstatus[fg_idx].primary_status = (uint8_t)r;
					} else {
						mission.primary_fg[fg_idx] = 0;
						fgstatus[fg_idx].primary_status = 0;
					}
				}
			} else {
				any_pri_goal = 1;
			}

			/* Secondary win */
			if (fgstatus[fg_idx].secondary_status != 1 || f->sec_win_cond == 9) {
				const uint8_t sec_status = fgstatus[fg_idx].secondary_status;
				if (sec_status == 2) {
					sec_all_ok = 0;
					if (!mission.secondary_complete)
						mission.secondary_complete = 2;
				} else if (sec_status) {
					if (f->sec_win_cond && f->sec_win_cond != 10) {
						any_sec_goal = 1;
						const int8_t r = score_checkcondition(f->sec_win_cond, GTT_FG, (uint8_t)fg_idx,
															  (uint8_t)percentcon[f->sec_win_pct], 0);
						if (r == 2 && !mission.secondary_complete)
							mission.secondary_complete = 2;
						if (r == 1) {
							mission.secondary_fg[fg_idx] = 1;
						} else {
							sec_all_ok = 0;
							mission.secondary_fg[fg_idx] = 2;
						}
						fgstatus[fg_idx].secondary_status = (uint8_t)r;
					} else {
						mission.secondary_fg[fg_idx] = 0;
						fgstatus[fg_idx].secondary_status = 0;
					}
				}
			} else {
				any_sec_goal = 1;
			}

			/* Bonus */
			if (fgstatus[fg_idx].fg_complete != 1 || f->bonus_cond == 9) {
				const uint8_t fgc = fgstatus[fg_idx].fg_complete;
				if (fgc == 2) {
					bonus_all_ok = 0;
					if (!mission.bonus_complete)
						mission.bonus_complete = 2;
				} else if (fgc) {
					if (f->bonus_cond && f->bonus_cond != 10) {
						any_bonus_goal = 1;
						const int8_t r = score_checkcondition(f->bonus_cond, GTT_FG, (uint8_t)fg_idx,
															  (uint8_t)percentcon[f->bonus_pct], 0);
						if (fgstatus[fg_idx].fg_complete == 2 && !mission.bonus_complete &&
							f->bonus_points >= 0)
							mission.bonus_complete = 2;
						if (r == 1) {
							mission.bonus_fg[fg_idx] = 1;
						} else {
							if (f->bonus_points >= 0)
								bonus_all_ok = 0;
							mission.bonus_fg[fg_idx] = 2;
						}
						fgstatus[fg_idx].fg_complete = (uint8_t)r;
					} else {
						mission.bonus_fg[fg_idx] = 0;
					}
				}
			} else {
				any_bonus_goal = 1;
			}
		}

		/* --- Phase 2: mission-level primary aggregate -------------- */
		const EMissionGoal* const pri = &cut[0];
		int16_t pri_mission_status;
		if (pri->subcond[0].cond == 10 && pri->subcond[1].cond == 10) {
			pri_mission_status = 1;
			mission.primary_global = 0;
		} else {
			any_pri_goal = 1;
			const int8_t pri_a = score_checkcondition(pri->subcond[0].cond, pri->subcond[0].type,
													  pri->subcond[0].id, pri->subcond[0].pct, 0);
			const int8_t pri_b = score_checkcondition(pri->subcond[1].cond, pri->subcond[1].type,
													  pri->subcond[1].id, pri->subcond[1].pct, 0);
			pri_mission_status = combine_subconds(pri_a, pri_b, pri->or_joined == 1);
			mission.primary_global = (uint8_t)pri_mission_status;
		}
		if (pri_mission_status == 2 && !mission.primary_complete)
			mission.primary_complete = 2;
		if (pri_mission_status == 1 && pri_all_ok && any_pri_goal) {
			if (mission.primary_complete != 1) {
				play_objectives_complete(7080, MSG_PRIMARY_COMPLETE, NAME_LIST_PRI,
										 fsfx_mission_voice_id(FSFX_MISSION_VOICE_PRIMARY), 0x5Bu, 5,
										 &timers[TIMER_PRI_COMPLETE]);
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
		const EMissionGoal* const sec = &cut[1];
		int16_t sec_mission_status;
		{
			const int8_t sec_a = score_checkcondition(sec->subcond[0].cond, sec->subcond[0].type,
													  sec->subcond[0].id, sec->subcond[0].pct, 0);
			const int8_t sec_b = score_checkcondition(sec->subcond[1].cond, sec->subcond[1].type,
													  sec->subcond[1].id, sec->subcond[1].pct, 0);
			sec_mission_status = combine_subconds(sec_a, sec_b, sec->or_joined == 1);
			mission.secondary_global = (uint8_t)sec_mission_status;
			if (sec_mission_status == 2 && !mission.secondary_complete)
				mission.secondary_complete = 2;
			if (sec->subcond[0].cond == 10 && sec->subcond[1].cond == 10) {
				mission.secondary_global = 0;
				sec_mission_status = 1;
			} else {
				any_sec_goal = 1;
			}
			if (sec_mission_status == 1 && sec_all_ok == 1 && any_sec_goal) {
				if (mission.secondary_complete != 1) {
					play_objectives_complete(7080, MSG_SECONDARY_COMPLETE, NAME_LIST_SEC,
											 fsfx_mission_voice_id(FSFX_MISSION_VOICE_SECONDARY), 0x5Cu, 7,
											 &timers[TIMER_SEC_COMPLETE]);
				}
				mission.secondary_complete = 1;
			}
		}

		/* --- Phase 4: bonus aggregate ------------------------------ */
		const EMissionGoal* const bonus = &cut[2];
		{
			const int8_t bonus_a = score_checkcondition(bonus->subcond[0].cond, bonus->subcond[0].type,
														bonus->subcond[0].id, bonus->subcond[0].pct, 0);
			const int8_t bonus_b = score_checkcondition(bonus->subcond[1].cond, bonus->subcond[1].type,
														bonus->subcond[1].id, bonus->subcond[1].pct, 0);
			int16_t bonus_mission_status = combine_subconds(bonus_a, bonus_b, bonus->or_joined == 1);
			mission.bonus_global = (uint8_t)bonus_mission_status;
			if (bonus_mission_status == 2 && !mission.bonus_complete)
				mission.bonus_complete = 2;
			if (bonus->subcond[0].cond == 10 && bonus->subcond[1].cond == 10) {
				mission.bonus_global = 0;
				bonus_mission_status = 1;
			} else {
				any_bonus_goal = 1;
			}
			if (bonus_mission_status == 1 && bonus_all_ok == 1 && any_bonus_goal) {
				if (mission.bonus_complete != 1) {
					play_objectives_complete(14160, MSG_BONUS_COMPLETE, NAME_LIST_NONE, 0, 0x5Eu, 9,
											 &timers[TIMER_BONUS_COMPLETE]);
				}
				mission.bonus_complete = 1;
			}
		}

		/* "Objectives failed" banner when primary fails mid-flight. */
		ret_al = (int8_t)mission.primary_complete;
		if (mission.primary_complete == 2 && !hyperspaceflag && !timers[TIMER_OBJECTIVES_FAILED]) {
			msg_messageprintf(MSG_OBJECTIVES_FAILED);
			timers[TIMER_OBJECTIVES_FAILED] = 21240;
			for (uint16_t k = 0; k < 2u; k++) {
				const uint8_t* p = OBJ_FAILED_NAME(k);
				if (*p) {
					msg_addmessageptr(0, (char*)p);
					/* First failure-name line carries the edition-specific
					 * loss VO loaded by fsfx_loadvoicelfd;
					 * second line plays silently. */
					pending_voice_id = (k == 0) ? fsfx_mission_voice_id(FSFX_MISSION_VOICE_LOSS) : 0;
					msg_messageprintf(MSG_GENERIC_STAR);
				}
			}
			fsfx_triggervoicesfx(0x6Au);
			ret_al = (int8_t)fscript_MsSetSequence(6);
		}

		timers[TIMER_PRIMARY_CHECK] = 236;
	}

	/* --- Phase 5: radio-message trigger poll ---------------------- */
	if (timers[TIMER_RADIOMSG_POLL] <= 0) {
		const uint16_t msg_count = (uint16_t)mission_file_header.num_msg;
		for (uint16_t j = 0; j < msg_count; j++) {
			ret_al = (int8_t)j;
			uint8_t* const rec = &radiomsg[90u * j];

			if (mission.radiomsg_triggered[j]) {
				uint8_t cur = mission.radiomsg_countdown[j];
				if (cur) {
					mission.radiomsg_countdown[j] = (uint8_t)(cur - 1);
					if (cur == 1) {
						msg_addmessageptr(0, (char*)rec);
						/* Pair the radio text with the edition-specific
						 * per-mission voice cue loaded from the
						 * <NAME>.LFD by fsfx_loadvoicelfd. */
						pending_voice_id = fsfx_mission_voice_id(j);
						msg_messageprintf(MSG_GENERIC_STAR_INFO);
					}
				}
				continue;
			}

			/* Evaluate both subconditions. */
			const int8_t sa = score_checkcondition(rec[64], rec[65], rec[66], rec[67], 0);
			const int8_t sb = score_checkcondition(rec[68], rec[69], rec[70], rec[71], 0);
			const int8_t combined = (rec[89] == 1) ? (int8_t)(sb | sa) : (int8_t)(sb & sa);
			if ((combined & 1) == 0)
				continue;

			mission.radiomsg_triggered[j] = 1;
			const uint8_t cd_seed = rec[88];
			mission.radiomsg_countdown[j] = cd_seed;
			if (cd_seed == 0) {
				msg_addmessageptr(0, (char*)rec);
				pending_voice_id = fsfx_mission_voice_id(j);
				msg_messageprintf(MSG_GENERIC_STAR_INFO);
			}
		}
		timers[TIMER_RADIOMSG_POLL] = 1180;
	}

	return ret_al;
}
