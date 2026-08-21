/*
 * SCORE -- mission objective / condition evaluator.
 *
 * Ports of score.c from the Watcom binary (5 functions, 1:1 mapping):
 *   score_checkobjective     @ 0x4D530  -- per-frame mission objective state pass
 *   score_checkcondition     @ 0x4E0E8  -- evaluate one (cond, target, amount_op) goal
 *   score_fgmemberofgroup    @ 0x4EF40  -- FG membership test
 *   score_objectmemberofgroup@ 0x4F120  -- FlightObject membership test
 *   score_craftexitscoring   @ 0x4F4A8  -- credit one craft's exit to FG buckets
 *
 * The condition evaluator returns one of:
 *   1 = met   2 = failed   4 = incomplete   0 = always-false placeholder.
 */

#ifndef __SCORE_H__
#define __SCORE_H__

#include <stdint.h>

/*
 * Goal target-type selector (same enum is used by GOALS). Passed to every
 * group/membership function as the second argument.
 */
enum GoalTargetType_tag {
	GTT_FG = 1,         /* match by flight-group index                */
	GTT_SPECIES = 2,    /* match by species (through speciesconvert)  */
	GTT_GENUS = 3,      /* match by genus (species.ship_class)        */
	GTT_FAMILY = 4,     /* match by family (species.category)         */
	GTT_SIDE = 5,       /* match by IFF side                          */
	GTT_AI_ORDER = 6,   /* match by ai[0].order                       */
	GTT_CRAFT_ATTR = 7, /* per-craft attribute predicate (objectmember
						 * dispatches on group_id 0..10: docked,
						 * friendly, shielded, specific-craft, ...).
						 * In fgmemberofgroup it degenerates to
						 * "match any FG"; in goals_outputgoal it
						 * renders as '---' (no printable category). */
	GTT_ALL_IN_SET = 8, /* match by fg.set                            */
	GTT_SKILL = 9,      /* match by fg.skill                          */
	GTT_VERSION = 10,   /* match by fg.version (loadout variant); in
						 * goals_outputgoal also renders as '---'.    */
	GTT_ALL_FG = 11     /* matches any FG / any object (wildcard);
						 * goals_outputgoal renders as "all FG".      */
};

/*
 * Per-frame objective evaluator. Called by the in-flight tick. Polls cooldown
 * timers[] so the heavy pass only runs every ~1 second. Updates
 * mission.{pri,sec,bonus}_{complete,global}, mission.{pri,sec,bonus}_fg[]
 * state, triggers voiced announcements, and evaluates radio-message triggers.
 *
 * Returns mission.primary_complete at the end of the pass (0=unresolved,
 * 1=complete, 2=failed) in AL. Bails with the initial player_status value
 * when mission.player_status != 3 or training mode / hyperspace is active.
 */
int8_t score_checkobjective(void);

/*
 * Evaluate one goal condition. Two dispatch paths gated by
 * conditiongrouprelated[cond]:
 *   0 -- mission-level direct check (reads mission.{pri,sec,bonus}_complete
 *        or penalty_flag depending on cond).
 *   1 -- iterate every FG that passes score_fgmemberofgroup(fg, target_type,
 *        target_id) and aggregate fgstatus[].cond[]/cond_id[] buckets; final
 *        result picked by amount_op (see below).
 *
 * Params:
 *   cond         -- condition code 0..25.
 *                     0            : always-met (returns 1)
 *                     1..9, 11, 12 : specific cond[] bucket aggregator (see .c)
 *                     10           : always-not-met (returns 0)
 *                     13..18,20,25 : mission-level state check
 *                     19, 21..24   : FG iter + per-craft live-state predicate
 *   target_type  -- GoalTargetType group selector.
 *   target_id    -- group id payload.
 *   amount_op    -- 0..15 quantifier:
 *                     0  = all matched,  1 = >=75%,  2 = >=50%,  3 = >=25%
 *                     4  = any matched,  5 = all-but-one,  6 = specific craft
 *                     7  = all except "waiting"   8 = all except player
 *                     9  = player FG matched      10 = destroyed count
 *                     11..13 = pct of destroyed (75/50/25%)
 *                     14 = any (alt encoding)     15 = all-but-one destroyed
 *   exclude_player -- when non-zero and cond==2, shifts accumulation slots so
 *                     the player FG is excluded from the "was destroyed" tally
 *                     (used for arrival triggers vs departure triggers).
 *
 * Returns 1/2/4 per the table above (0 when cond==10).
 */
int8_t score_checkcondition(uint8_t cond, uint8_t target_type, uint8_t target_id, uint8_t amount_op,
							int8_t exclude_player);

/*
 * Tests whether FG `fg_idx` matches the (group_type, group_id) selector.
 * Invoked by score_checkcondition's FG-iteration path. See GoalTargetType
 * above for dispatch.
 */
int8_t score_fgmemberofgroup(uint16_t fg_idx, uint8_t group_type, uint16_t group_id);

/*
 * Tests whether FlightObject or StaticObject `obj_idx` matches
 * (group_type, group_id). obj_idx >= 0x3800 addresses a static object
 * (staticobjects[obj_idx - 0x3800]); otherwise a regular object
 * (objects[obj_idx]). group_type==7 is a special 'craft-attribute' predicate
 * (live objects only, see .c for group_id sub-cases 0..10).
 *
 * Consumed by fsfx_checkcriticalcraft to decide whether a destroyed craft
 * was mission-critical.
 */
int8_t score_objectmemberofgroup(uint16_t obj_idx, uint8_t group_type, uint8_t group_id);

/*
 * Credit craft `obj_idx`'s exit to its FG's cond[]/cond_id[] tally buckets.
 *
 * `exit_kind` is used as a RAW BYTE OFFSET into
 * fgstatus[fg_idx].cond[0].count (not as an array index), so
 *   0 -> cond[0].count   1 -> cond[0].detail   2 -> cond[1].count   etc.
 * Simultaneously bumps the six conditional bucket pairs 3..8 based on
 * craft attribute flags (inspected, pad_0B6, dock_state_flags,
 * was_hit_flag, board_count, capture_count).
 *
 * When exit_kind==2 (destroyed):
 *   - bumps mission.mission_linked_data[fg.link_code] if fg.link_flag set
 *     (wraps to -1 on overflow);
 *   - propagates a (cond[0].count - cond[0].detail) delta across cond[1/3/
 *     4/5/6/8] of every OTHER FG whose start_fg==fg_idx, marking them
 *     active=1 / waves_remaining=0 (fires their arrival trigger);
 *   - finally clears any craft's attacker_idx that pointed at the exiting
 *     obj_idx.
 */
void score_craftexitscoring(uint16_t obj_idx, uint16_t fg_idx, uint16_t exit_kind);

/* --- Module-owned globals (watdbg: score.c) ---------------------------- */

/*
 * percentcon[5] = {0, 2, 4, 5, 6}. Indirection from a per-FG percentage
 * bucket byte (fg.pri_win_pct / sec_win_pct / bonus_pct, values 0..4) to the
 * amount_op index forwarded to score_checkcondition.
 */
extern int16_t percentcon[5];

/*
 * conditiongrouprelated[26]: dispatch flag per condition code (0..25).
 * 0 -> mission-level direct check; 1 -> FG-iteration evaluator.
 * Initialiser: {0,1,1,1,1,1,1,1,1,1, 0,1,1,0,0,0,0,0,0,1, 0,1,1,1,1,0}.
 */
extern uint8_t conditiongrouprelated[26];

#endif /* __SCORE_H__ */
