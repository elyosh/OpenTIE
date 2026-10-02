#include "tie/paifight.h"
#include "tie/collide.h"
#include "tie/create.h"
#include "tie/draw.h"
#include "tie/edition.h"
#include "tie/laser.h"
#include "tie/math2.h"
#include "tie/mission.h"
#include "tie/modelmesh.h"
#include "tie/pai.h"
#include "tie/score.h"
#include "tie/shipext.h"
#include "tie/tie.h"
#include "tie/trig2.h"

#include <stddef.h>
#include <stdint.h>

/* (pstate.radio_target lives in pstate, tie.h.) */

/* =====================================================================
 *                          Module-owned globals
 * ===================================================================== */

/* Per-skill gunner tuning (watdbg: paifight.c). frwdgunnerranges is a
 * 24.8 fixed-point max gunner engagement range for each of the three
 * ai.skill_tier buckets (0/1/2); demo symbol at
 * dseg02:0x5170. */
// GLOBAL: TIE95 0xC58A8
// GLOBAL: TIE98 0x4E6388
const uint32_t frwdgunnerranges[3] = { 0x06000u, 0x08000u, 0x0A000u };

/* Burst-shot count per skill tier. The demo symbol (dseg02:0x517C) is a
 * 4-byte LUT; only tiers 0..2 are consulted, the 4th entry is padding. */
// GLOBAL: TIE95 0xC58B4
// GLOBAL: TIE98 0x4E6394
const uint8_t frwdgunnerbursts[4] = { 3, 4, 5, 0 };

/* Public shooter-origin (24.8 world-space). Written by gunnerself/offense
 * handlers after computing turret hardpoint + pai_world; consumed by
 * starship_firelasergunner and static_updatemineguns when firing. */
// GLOBAL: TIE95 0xD5058
// GLOBAL: TIE98 0x5FCE34
int32_t shootery;
// GLOBAL: TIE95 0xD505C
// GLOBAL: TIE98 0x5FCE38
int32_t shooterx;
// GLOBAL: TIE95 0xD5060
// GLOBAL: TIE98 0x5FCE3C
int32_t shooterz;

/* Scratch FG idx: last FG that yielded a match inside searchforclosest-
 * ingroup. Consumed by paifight_checkescortorder to stamp
 * craftptr->escortee_fg_idx. */
// GLOBAL: TIE95 0xD5064
// GLOBAL: TIE98 0x5FCE30
uint8_t escortfg;

// FUNCTION: TIE95 0x36A90
int16_t paifight_scanfortargetorder(void) {
	/* Plan slot 9 -- target-acquire step. Bails when the craft's mode
	 * has drifted from the plan's declared order. */
	uint16_t order_class;
	uint16_t target;

	if (craftptr->mode_byte == ai.plan_order) {
		/* Fast path: re-use the last target if it is still worthy. */
		if ((uint16_t)craftptr->pending_radio_command != 0xFFu &&
			(uint16_t)craftptr->pending_radio_command != 0xFBu) {
			target = craftptr->pending_radio_command;
			if (pai_worthytarget(target)) {
				craftptr->ai_target_ref = (int16_t)target;
				return 1;
			}
			craftptr->pending_radio_command = 0xFF;
		}

		order_class = craftptr->default_order_ldr;
		ai.live_target_only = order_class == 19;
		ai.search_flags = 7u;

		if (order_class == 7 || order_class == 19) {
			target = (uint16_t)paifight_checkfortargets(ai.ai_entry_count);
		} else if (order_class == 8) {
			target = (uint16_t)paifight_checkforescortertargets(ai.ai_entry_count);
		} else {
			target = (uint16_t)paifight_checkforattackedtargets(ai.ai_entry_count);
		}
		if (target != 0xFFFFu) {
			craftptr->ai_target_ref = (int16_t)target;
			return 1;
		}
	}
	return 0;
}

// FUNCTION: TIE95 0x36B74
int16_t paifight_checkfortargets(uint16_t ai_entry) {
	int16_t result = paifight_findtargetingroup(
		fg_array[ai.fg_idx].ai[ai_entry].pri_type, fg_array[ai.fg_idx].ai[ai_entry].pri_id,
		fg_array[ai.fg_idx].ai[ai_entry].pri_sec_op, fg_array[ai.fg_idx].ai[ai_entry].sec_type,
		fg_array[ai.fg_idx].ai[ai_entry].sec_id);
	if ((uint16_t)result != 0xFFFFu)
		return result;
	return paifight_findtargetingroup(
		fg_array[ai.fg_idx].ai[ai_entry].target_type[0], fg_array[ai.fg_idx].ai[ai_entry].target_id[0],
		fg_array[ai.fg_idx].ai[ai_entry].target_op, fg_array[ai.fg_idx].ai[ai_entry].target_type[1],
		fg_array[ai.fg_idx].ai[ai_entry].target_id[1]);
}

/* =====================================================================
 *                           Target finders
 * ===================================================================== */

// FUNCTION: TIE95 0x36C3C
int16_t paifight_findtargetingroup(uint16_t pri_type, uint16_t pri_id, uint16_t op, uint16_t sec_type,
								   uint16_t sec_id) {
	uint16_t o;
	uint16_t stat_ref;
	uint16_t s;
	uint16_t best_obj = 0xFFFF;
	uint32_t best_dist = 0xFFFFFFFFu;

	/* Moving-object pass (NUM_CRAFTS slots). */

	for (o = 0; o < NUM_CRAFTS; ++o) {
		int16_t pri_hit;
		int16_t hit;
		CraftData* cp;

		if (!objects[o].ship_idx)
			continue;
		if (objects[o].fg_idx == ai.fg_idx)
			continue; /* skip same FG */

		pri_hit = score_objectmemberofgroup(o, pri_type, pri_id);
		hit = score_objectmemberofgroup(o, sec_type, sec_id);
		if (op == 1)
			hit |= pri_hit;
		else
			hit &= pri_hit;
		if (!hit)
			continue;
		if (!pai_worthytarget(o))
			continue;

		cp = objects[o].craft_ptr;
		/* ai.live_target_only gate: when set, require status_flags non-zero AND
		 * either no dock-state OR different side from us. */
		if (ai.live_target_only && cp->status_flags == 0)
			continue;
		if (ai.live_target_only && cp->dock_state_flags != 0 &&
			objects[ai.active_obj_idx].side == objects[o].side)
			continue;

		if ((ai.search_flags & 0x04u) && !pai_checkcombatarea(o))
			continue;

		if ((ai.search_flags & 0x01u) && !paifight_countattackers(o))
			continue;

		if (ai.search_flags & 0x20u) {
			roughdistance =
				collide_roughdistance3d(objects[o].world_x - ai.search_x, objects[o].world_y - ai.search_y,
										objects[o].world_z - ai.search_z);
		} else {
			pai_roughdistancebetween(ai.active_obj_idx, o);
		}
		if ((uint32_t)roughdistance < best_dist) {
			best_dist = (uint32_t)roughdistance;
			best_obj = (int16_t)o;
		}
	}

	/* Static-object pass (64 slots; side-flag bit 2 gates hostile/
	 * structure class). obj_ref = 0x3800 + slot. */
	stat_ref = 0x3800u;
	for (s = 0; s < 0x40; ++s, ++stat_ref) {
		uint16_t species = staticobjects[s].species;
		int16_t pri_hit;
		int16_t hit;

		if (species == 0)
			continue;
		if ((species_table[species].side & 2u) == 0u)
			continue;

		pri_hit = score_objectmemberofgroup(stat_ref, pri_type, pri_id);
		hit = score_objectmemberofgroup(stat_ref, sec_type, sec_id);
		if (op == 1)
			hit |= pri_hit;
		else
			hit &= pri_hit;
		if (!hit)
			continue;
		if (!pai_worthytarget(stat_ref))
			continue;

		if ((ai.search_flags & 0x04u) && !pai_checkcombatarea(stat_ref))
			continue;

		if ((ai.search_flags & 0x01u) && !paifight_countattackers(stat_ref))
			continue;

		pai_roughdistancebetween(ai.active_obj_idx, stat_ref);
		if ((uint32_t)roughdistance < best_dist) {
			best_dist = (uint32_t)roughdistance;
			best_obj = (int16_t)stat_ref;
		}
	}
	return best_obj;
}

// FUNCTION: TIE95 0x36F1C
int16_t paifight_checkforescortertargets(uint16_t ai_entry) {
	int16_t result = paifight_findescorterofgroup(
		fg_array[ai.fg_idx].ai[ai_entry].pri_type, fg_array[ai.fg_idx].ai[ai_entry].pri_id,
		fg_array[ai.fg_idx].ai[ai_entry].pri_sec_op, fg_array[ai.fg_idx].ai[ai_entry].sec_type,
		fg_array[ai.fg_idx].ai[ai_entry].sec_id);
	if ((uint16_t)result != 0xFFFFu)
		return result;
	return paifight_findescorterofgroup(
		fg_array[ai.fg_idx].ai[ai_entry].target_type[0], fg_array[ai.fg_idx].ai[ai_entry].target_id[0],
		fg_array[ai.fg_idx].ai[ai_entry].target_op, fg_array[ai.fg_idx].ai[ai_entry].target_type[1],
		fg_array[ai.fg_idx].ai[ai_entry].target_id[1]);
}

// FUNCTION: TIE95 0x36FE4
int16_t paifight_findescorterofgroup(uint16_t pri_type, uint16_t pri_id, uint16_t op, uint16_t sec_type,
									 uint16_t sec_id) {
	uint16_t best_obj = 0xFFFF;
	uint32_t best_dist = 0xFFFFFFFFu;
	uint16_t f;

	for (f = 0; f < mission_file_header.num_fg; ++f) {
		int16_t pri_hit = score_fgmemberofgroup(f, pri_type, pri_id);
		int16_t hit = score_fgmemberofgroup(f, sec_type, sec_id);
		uint16_t o;

		if (op == 1)
			hit |= pri_hit;
		else
			hit &= pri_hit;
		if (!hit)
			continue;

		/* Pick the closest active craft whose default_order_ldr is 20
		 * (Escort) and whose escortee_fg_idx points at the selector-
		 * matching FG. */
		for (o = 0; o < NUM_CRAFTS; ++o) {
			CraftData* cp;

			if (!objects[o].ship_idx)
				continue;
			cp = objects[o].craft_ptr;
			if (cp->default_order_ldr != 20)
				continue;
			if (cp->escortee_fg_idx != f)
				continue;

			if (!pai_worthytarget(o))
				continue;
			if ((ai.search_flags & 0x04u) && !pai_checkcombatarea(o))
				continue;
			if ((ai.search_flags & 0x01u) && !paifight_countattackers(o))
				continue;
			pai_roughdistancebetween(ai.active_obj_idx, o);
			if ((uint32_t)roughdistance < best_dist) {
				best_dist = (uint32_t)roughdistance;
				best_obj = o;
			}
		}
	}

	/* Side effect: stamp the found escorter as our current link target. */
	if (best_obj != 0xFFFF)
		craftptr->ai_target_ref = best_obj;
	return best_obj;
}

// FUNCTION: TIE95 0x37148
int16_t paifight_checkforattackedtargets(uint16_t ai_entry) {
	int16_t result = paifight_findattackedtargetingroup(
		fg_array[ai.fg_idx].ai[ai_entry].pri_type, fg_array[ai.fg_idx].ai[ai_entry].pri_id,
		fg_array[ai.fg_idx].ai[ai_entry].pri_sec_op, fg_array[ai.fg_idx].ai[ai_entry].sec_type,
		fg_array[ai.fg_idx].ai[ai_entry].sec_id);
	if ((uint16_t)result != 0xFFFFu)
		return result;
	return paifight_findattackedtargetingroup(
		fg_array[ai.fg_idx].ai[ai_entry].target_type[0], fg_array[ai.fg_idx].ai[ai_entry].target_id[0],
		fg_array[ai.fg_idx].ai[ai_entry].target_op, fg_array[ai.fg_idx].ai[ai_entry].target_type[1],
		fg_array[ai.fg_idx].ai[ai_entry].target_id[1]);
}

// FUNCTION: TIE95 0x37210
int16_t paifight_findattackedtargetingroup(uint16_t pri_type, uint16_t pri_id, uint16_t op, uint16_t sec_type,
										   uint16_t sec_id) {
	uint16_t best_obj = 0xFFFF;
	uint32_t best_dist = 0xFFFFFFFFu;
	uint16_t target;

	for (target = 0; target < NUM_CRAFTS; ++target) {
		CraftData* tc;
		int16_t pri_hit;
		int16_t hit;
		uint16_t a;
		CraftData* ac;
		uint16_t mode;

		if (!objects[target].ship_idx)
			continue;
		tc = objects[target].craft_ptr;
		if (!tc->was_hit_flag)
			continue;

		pri_hit = score_objectmemberofgroup(target, pri_type, pri_id);
		hit = score_objectmemberofgroup(target, sec_type, sec_id);
		if (op == 1)
			hit |= pri_hit;
		else
			hit &= pri_hit;
		if (!hit)
			continue;

		/* Inner: pick the closest live attacker. An attacker qualifies
		 * when (a) its mode is 11/12/23 AND it is targeting `target`,
		 * OR (b) was_hit_flag bit 0x80 is set AND the attacker slot is
		 * the player's own object. */
		for (a = 0; a < NUM_CRAFTS; ++a) {

			if (!objects[a].ship_idx)
				continue;
			ac = objects[a].craft_ptr;

			mode = ac->mode_byte;
			if (!(((mode == 11 || mode == 12 || mode == 23) && ac->ai_target_ref == (int16_t)target) ||
				  ((tc->was_hit_flag & 0x80u) && a == pstate.object_idx)))
				continue;

			if (!pai_worthytarget(a))
				continue;
			if ((ai.search_flags & 0x04u) && !pai_checkcombatarea(a))
				continue;
			if ((ai.search_flags & 0x01u) && !paifight_countattackers(a))
				continue;

			if (ai.search_flags & 0x20u) {
				roughdistance = collide_roughdistance3d(objects[a].world_x - ai.search_x,
														objects[a].world_y - ai.search_y,
														objects[a].world_z - ai.search_z);
			} else {
				pai_roughdistancebetween(ai.active_obj_idx, a);
			}
			/* Bit 0x10 caps distance at 0x10000 fixed-point units. */
			if ((ai.search_flags & 0x10u) && roughdistance > 0x10000)
				continue;

			if ((uint32_t)roughdistance < best_dist) {
				best_dist = (uint32_t)roughdistance;
				best_obj = (int16_t)a;
			}
		}
	}
	return best_obj;
}

/* search_flags / search_x/y/z live in the shared AiContext struct
 * (ai.search_*); watdbg's _ai[52] is a single symbol spanning both PAI
 * and PAIFIGHT fields. Declared in pai.h — no separate definitions. */

/* =====================================================================
 *                              Leaf helpers
 * ===================================================================== */

// FUNCTION: TIE95 0x3741C
int16_t paifight_countattackers(uint16_t target_obj_idx) {
	/* Count active objects currently attacking target_obj_idx in
	 * combat modes 11/12/23. Self-attackers are skipped. */
	uint16_t i;
	uint16_t attackers;
	uint16_t cap;

	attackers = 0;

	for (i = 0; i < NUM_CRAFTS; ++i) {
		CraftData* cp;

		if (!objects[i].ship_idx)
			continue;
		cp = objects[i].craft_ptr;
		if (cp->ai_target_ref == (int16_t)target_obj_idx && cp->ai_target_ref != (int16_t)i &&
			(cp->mode_byte == 11 || cp->mode_byte == 12 || cp->mode_byte == 23))
			++attackers;
	}

	/* Cap keyed on the candidate's genus, side, and mission difficulty
	 * when the candidate is the player. Retail short-circuits to cap=2
	 * for non-craft slots (>= NUM_CRAFTS) — without this, the genus byte
	 * of a warhead/debris slot could mis-map to cap 6/4/3. */

	if (target_obj_idx < NUM_CRAFTS) {
		int genus = objects[target_obj_idx].genus;
		if (genus == GENUS_STARSHIP || genus == GENUS_PLATFORM) {
			cap = 6;
		} else if (genus == GENUS_FREIGHTER) {
			cap = 4;
		} else if (objects[target_obj_idx].side == objects[pstate.object_idx].side) {
			cap = 3;
		} else {
			cap = 2;
		}
	} else {
		cap = 2;
	}

	if (target_obj_idx == pstate.object_idx) {
		/* Player-as-target override; scales aggression with difficulty.
		 * mission.difficulty > 2 keeps the genus-derived cap. */
		switch (mission.difficulty) {
			case 0:
				cap = 2; /* easy */
				break;
			case 1:
				cap = 3; /* medium */
				break;
			case 2:
				cap = 4; /* hard */
				break;
		}
	}

	/* Guardrail: wingmen do not dogpile the player's chosen radio target. */
	if ((objects[ai.active_obj_idx].fg_idx != objects[pstate.object_idx].fg_idx ||
		 (int16_t)target_obj_idx != pstate.radio_target) &&
		attackers < cap)
		return 1;
	else
		return 0;
}

// FUNCTION: TIE95 0x37570
int16_t paifight_scanfortargetswitch(uint16_t ai_entry) {
	const EAIStruct* cur_ai = &fg_array[ai.fg_idx].ai[ai_entry];
#ifdef TIE_MODERN
	// HARDENING: orders past the 33-entry tables (retail HI1W.TIE uses 35) take the null plan.
	uint8_t order_class = cur_ai->order < sizeof(ordersldr) ? ordersldr[cur_ai->order] : 0;
#else
	uint8_t order_class = ordersldr[cur_ai->order];
#endif

	int16_t result;

	ai.live_target_only = order_class == 19;
	ai.search_flags = 7u;

	if (order_class == 7 || order_class == 19) {
		result = paifight_checkfortargets(ai_entry);
	} else {
		if (order_class == 8)
			return (uint16_t)paifight_checkforescortertargets(ai_entry) != 0xFFFFu;
		result = paifight_checkforattackedtargets(ai_entry);
	}
	return (uint16_t)result != 0xFFFFu;
}

// FUNCTION: TIE95 0x37624
int16_t paifight_scanfortargetsallgone(uint16_t ai_entry) {
	const EAIStruct* cur_ai = &fg_array[ai.fg_idx].ai[ai_entry];
#ifdef TIE_MODERN
	// HARDENING: orders past the 33-entry tables (retail HI1W.TIE uses 35) take the null plan.
	uint8_t order_class = cur_ai->order < sizeof(ordersldr) ? ordersldr[cur_ai->order] : 0;
#else
	uint8_t order_class = ordersldr[cur_ai->order];
#endif

	int16_t result;

	ai.live_target_only = order_class == 19;
	ai.search_flags = 2u;

	if (order_class == 8) {
		result = paifight_checkforescortertargets(ai_entry);
	} else {
		if (order_class == 9)
			return (uint16_t)paifight_checkforattackedtargets(ai_entry) != 0xFFFFu;
		result = paifight_checkfortargets(ai_entry);
	}
	return (uint16_t)result != 0xFFFFu;
}

/* =====================================================================
 *                        Plan-VM combat handlers
 * ===================================================================== */

// FUNCTION: TIE95 0x376D4
int16_t paifight_escorttargetorder(void) {
	/* Plan slot 23: escort target picker. */
	uint16_t self = ai.active_obj_idx;
	uint16_t best_obj;
	uint32_t best_dist;
	uint16_t escortee_fg;
	uint16_t obj;

	if (craftptr->mode_byte != ai.plan_order)
		return 0;

	/* Fast path: cached target still worthy. */
	obj = (uint16_t)craftptr->pending_radio_command;
	if (obj != 0xFFu && obj != 0xFBu) {
		if (pai_worthytarget(obj)) {
			craftptr->ai_target_ref = (int16_t)obj;
			return 1;
		}
		craftptr->pending_radio_command = 0xFF;
	}

	escortee_fg = craftptr->escortee_fg_idx;
	best_obj = 0xFFFFu;
	best_dist = 0xFFFFFFFFu;

	for (obj = 0; obj < NUM_CRAFTS; ++obj) {
		CraftData* cp;
		int lnk;
		int16_t in_fg;

		if (!objects[obj].ship_idx || obj == ai.active_obj_idx)
			continue;
		cp = objects[obj].craft_ptr;

		/* Branch A: obj is the player or a player-FG wingman and the
		 * escort_protect flag is set. */
		if (fg_array[ai.fg_idx].ai[ai.ai_entry_count].var[1] &&
			(obj == pstate.object_idx ||
			 (cp->leader_obj_idx != 255u && objects[obj].fg_idx == pstate.player->fg_idx))) {
			if (!(int16_t)paifight_countattackers(obj))
				continue;
			pai_roughdistancebetween(self, obj);
			if ((uint32_t)roughdistance < best_dist && (uint32_t)roughdistance < 0x40000u) {
				best_dist = (uint32_t)roughdistance;
				best_obj = obj;
			}
			continue;
		}

		/* Branch B: obj is an enemy whose current ai_target_ref points at
		 * something in our escortee FG. */
		in_fg = 0;
		lnk = (uint16_t)cp->ai_target_ref;
		if (lnk < (int)OBJ_REF_STATIC_BASE) {
			if (objects[lnk].ship_idx && objects[lnk].fg_idx == escortee_fg)
				in_fg = 1;
		} else if (lnk < (int)OBJ_REF_WAYPOINT_BASE && lnk != 0xFFFF) {
			if (staticobjects[lnk - OBJ_REF_STATIC_BASE].species &&
				staticobjects[lnk - OBJ_REF_STATIC_BASE].fg_idx == escortee_fg)
				in_fg = 1;
		}
		if (!in_fg)
			continue;

		if (!(int16_t)paifight_countattackers(obj))
			continue;
		pai_roughdistancebetween(self, obj);
		if ((uint32_t)roughdistance < best_dist && (uint32_t)roughdistance < 0x40000u) {
			best_dist = (uint32_t)roughdistance;
			best_obj = obj;
		}
	}

	if (best_obj != 0xFFFFu) {
		craftptr->ai_target_ref = (int16_t)best_obj;
		return 1;
	}
	return 0;
}

// FUNCTION: TIE95 0x37900
int16_t paifight_fightershootorder(void) {
	/* Plan slot 5: fighter laser + missile fire control. */
	uint16_t target;
	uint32_t aim_range;
	uint16_t xy_delta;
	uint16_t z_delta;
	int16_t burst_tier;
	uint16_t burst_count;
	uint16_t laser_cnt;
	uint16_t order_ldr;
	uint16_t g;
	uint16_t warhead_class;
	uint16_t my_missile_cap;
	uint16_t max_inbound;
	uint32_t max_engage_range;
	uint32_t damage_limit;
	uint32_t incoming_dmg;
	uint16_t missiles_inbound;
	uint16_t obj;
	uint16_t j;

	if (!craftptr->status_flags)
		return 0;

	target = (uint16_t)craftptr->ai_target_ref;

	if (pai_worthytarget(target)) {
		/* --- Aim-cone range (tightens as the heading delta to the target grows). --- */
		aim_range = frwdgunnerranges[(uint16_t)ai.skill_tier];
		if (target < NUM_OBJECTS) {
			uint16_t heading_delta = (uint16_t)(objects[ai.active_obj_idx].heading - objects[target].heading);
			if (heading_delta >= 0x8000)
				heading_delta = -heading_delta;
			if (heading_delta < 0x2000)
				aim_range -= 0x4000;
			else if (heading_delta < 0x5000)
				aim_range -= 0x2000;
		}

		/* --- Line-up + burst tier computation. --- */
		pai_distancebetween(ai.active_obj_idx, target);
		xy_delta = (uint16_t)(trig2_xyangle - objects[ai.active_obj_idx].heading);
		if (xy_delta >= 0x8000)
			xy_delta = -xy_delta;
		z_delta = (uint16_t)(trig2_zangle - craftptr->orient_pitch);
		if (z_delta >= 0x8000)
			z_delta = -z_delta;

#ifdef TIE_MODERN
		/* The original leaves burst_count unset when no burst is due. */
		burst_count = 0;
#endif
		if (xy_delta < 0x800 && z_delta < 0x800 && aim_range > trig2_polardistance) {
			if (trig2_polardistance < 0x2000)
				burst_tier = 3;
			else if (trig2_polardistance < 0x4000)
				burst_tier = 2;
			else
				burst_tier = 1;
			burst_count = frwdgunnerbursts[(uint16_t)ai.skill_tier];
		} else {
			burst_tier = 0;
		}

		/* --- Per-laser-group owner/cooldown state. Order 19 (disable for
		 * capture) fires only ion cannons (141); other orders fire only
		 * conventional lasers. --- */
		laser_cnt = craftptr->laser_group_cnt;
		order_ldr = craftptr->default_order_ldr;
		for (g = 0; g < laser_cnt; ++g) {
			int16_t owner_val;

			if (burst_tier) {
				if (craftptr->laser_type[g] != 141) {
					if (order_ldr != 19)
						owner_val = burst_tier;
					else
						owner_val = 0;
				} else if (order_ldr == 19)
					owner_val = burst_tier;
				else
					owner_val = 0;
			} else
				owner_val = 0;
			craftptr->laser_owner_player[g] = owner_val;
			craftptr->laser_burst_remaining[g] = (uint8_t)burst_count;
		}

		/* --- Missile logic gate: bail when target is out of slot range. --- */
		if (target >= NUM_CRAFTS)
			return 0;

		/* Warhead class (1 = light, 2 = heavy for starship/platform/freighter
		 * targets) selects the per-class caps and the max lock range. */
		if (objects[target].genus == GENUS_STARSHIP || objects[target].genus == GENUS_PLATFORM ||
			objects[target].genus == GENUS_FREIGHTER) {
			warhead_class = 2;
			my_missile_cap = 2;
			max_inbound = 6;
			max_engage_range = 203610u;
		} else {
			warhead_class = 1;
			my_missile_cap = 1;
			max_inbound = 2;
			max_engage_range = 101805u;
		}

		/* Order 19 disables a target for capture. Avoid launching enough
		 * ordnance to destroy it, including projectiles already in flight. */
#ifdef TIE_MODERN
		/* The original only initializes these on the order-19 path. */
		damage_limit = 0;
		incoming_dmg = 0;
#endif
		if (order_ldr == 19) {
			damage_limit = objects[target].craft_ptr->forward_shield;
			damage_limit += objects[target].craft_ptr->hull_damage / 4;
			incoming_dmg = 0;
			for (g = 0; g < craftptr->missile_group_cnt; ++g) {
				if (projectile_is_warhead_type[craftptr->warhead_type[g] - WEAPON_SPECIES_BASE] ==
					warhead_class) {
					uint16_t dmg = projectileweight[craftptr->warhead_type[g] - WEAPON_SPECIES_BASE];

					if (objects[target].genus == GENUS_STARSHIP || objects[target].genus == GENUS_PLATFORM)
						dmg >>= 4;
					if (objects[target].genus == GENUS_FREIGHTER)
						dmg >>= 2;
					incoming_dmg += dmg;
				}
			}
		}
		for (obj = NUM_CRAFTS; obj < WARHEAD_SLOT_END; ++obj) {
			if (objects[obj].ship_idx && objects[obj].craft_ptr->species_idx &&
				target == objects[obj].craft_ptr->missile_target
#ifdef TIE_MODERN
				/* PORT: missiles that exploded on impact keep their warhead record
				 * but carry species 129-132; the original adds unrelated bytes from
				 * before projectileweight. Count them as weight 0. */
				&& (unsigned int)(objects[obj].ship_idx - WEAPON_SPECIES_BASE) < NUM_PROJECTILE_TYPES
#endif
			)
				incoming_dmg += projectileweight[objects[obj].ship_idx - WEAPON_SPECIES_BASE];
		}
		if (order_ldr == 19 && incoming_dmg >= damage_limit)
			return 0;

		/* Count live incoming missiles of the same class. */
		missiles_inbound = 0;
		for (obj = NUM_CRAFTS; obj < WARHEAD_SLOT_END; ++obj) {
			if (objects[obj].ship_idx &&
#ifdef TIE_MODERN
				/* PORT: in-place explosion sprites (species 129-132) keep their
				 * projectile slot; the original reads zero bytes before the table. */
				(unsigned int)(objects[obj].ship_idx - WEAPON_SPECIES_BASE) < WARHEAD_TYPE_COUNT &&
#endif
				warhead_class == projectile_is_warhead_type[objects[obj].ship_idx - WEAPON_SPECIES_BASE] &&
				objects[obj].craft_ptr->species_idx && target == objects[obj].craft_ptr->missile_target)
				++missiles_inbound;
		}
		if (missiles_inbound >= max_inbound)
			return 0;

		/* Per-missile gates. */
		if (craftptr->mode_byte != 23)
			return 0;
		if (craftptr->ion_drain_timer)
			return 0;
		if (craftptr->beam_state & 2)
			return 0;
		if (craftptr->missile_count >= my_missile_cap)
			return 0;

		/* --- Lock accumulator. Out-of-angle / out-of-range: decay the lock
		 * counter. In-window: accumulate at ai_update_rate scaled by 0xC000
		 * and fire once it reaches 236 * (2*skill_tier + 2). --- */
		if (xy_delta < 0x300 && z_delta < 0x300 && max_engage_range > trig2_polardistance) {
			craftptr->missile_count_total += math2_fraction(craftptr->ai_update_rate, 0xC000u);
			if ((int16_t)craftptr->missile_count_total < (uint16_t)(236 * (2 * ai.skill_tier + 2)))
				return 0;

			/* --- Fire loop. Arm every idle group of matching class; pick the
			 * hull aim-point; call laser_firerocketsystem. --- */
			for (j = 0; j < craftptr->missile_group_cnt; ++j) {
				uint8_t slot_idx;

				if (craftptr->missile_state[j])
					continue;
				if (projectile_is_warhead_type[craftptr->warhead_type[j] - WEAPON_SPECIES_BASE] !=
					warhead_class)
					continue;

				/* Fire from the pod with more ammo first: -127 when the left
				 * pod has less than the right, else 1. */
				slot_idx = spec_data[craftptr->species_idx].missile_start[j];
				if (craftptr->weapon_slots[slot_idx].ammo >= craftptr->weapon_slots[slot_idx + 1].ammo)
					craftptr->missile_armed[j] = 1u;
				else
					craftptr->missile_armed[j] = (uint8_t)(-127);

				craftptr->link_target_2E = paifight_gethullcomponent(target);
				laser_firerocketsystem(ai.active_obj_idx, j);
				++craftptr->missile_count;

				if (objects[target].genus == GENUS_FIGHTER || objects[target].genus == GENUS_TRANSPORT)
					craftptr->missile_count_total = 0;
			}
		} else {
			craftptr->missile_count_total -= craftptr->ai_update_rate;
			if ((int16_t)craftptr->missile_count_total < 0)
				craftptr->missile_count_total = 0;
		}
	} else {
		/* Clear the per-group laser-owner state so no spurious shots
		 * fire next frame. */
		for (g = 0; g < craftptr->laser_group_cnt; ++g)
			craftptr->laser_owner_player[g] = 0;
	}
	return 0;
}

// FUNCTION: TIE95 0x37F64
int16_t paifight_gethullcomponent(uint16_t target_obj_idx) {
	uint8_t hull_list[40];
	uint16_t hull_count = 0;
	uint16_t m;

	/* An empty list picks hull_list[0], since create_maxrandom(0) returns 0. */
	hull_list[0] = 0;
	if (target_obj_idx < NUM_CRAFTS) {
		if (!TIE_FLIGHT_TIE98)
			draw_Lockshipfileptrs(objects[target_obj_idx].ship_idx);
		for (m = 0; m < TIE_FLIGHT_EDITION(objectblockptr->num_meshes,
										   modelmesh_getcount(objects[target_obj_idx].ship_idx));
			 ++m) {
			uint16_t mesh_type = TIE_FLIGHT_EDITION(componentblockptr[m].mesh_type,
													modelmesh_gettype(objects[target_obj_idx].ship_idx, m));
			if (mesh_type == 1 || mesh_type == 3) {
#ifdef TIE_MODERN
				// HARDENING: models with more than 40 hull meshes would overflow the list.
				if (hull_count >= sizeof(hull_list))
					continue;
#endif
				hull_list[hull_count++] = (uint8_t)m;
			}
		}
		return hull_list[create_maxrandom(hull_count)];
	}
	return 0;
}

// FUNCTION: TIE95 0x38000
int16_t paifight_missiledefenseorder(void) {
	/* Plan slot 8: countermeasure firing. */
	uint16_t g;
	uint16_t slot;

	if (craftptr->flight_flag == 3)
		return 0;
	if (!craftptr->status_flags)
		return 0;
	if (craftptr->ion_drain_timer)
		return 0;

	for (g = 0; g < craftptr->missile_group_cnt; ++g) {
		for (slot = spec_data[craftptr->species_idx].missile_start[g];
			 slot < spec_data[craftptr->species_idx].missile_end[g] + 1; ++slot) {
			uint8_t wtype;
			uint16_t best_target;
			uint32_t best_dist;
			uint16_t obj;
			uint16_t wh;
			uint16_t fire_target;
			int16_t saved_link;
			uint16_t new_wh;

			if (craftptr->mesh_state[spec_data[craftptr->species_idx].hp[slot].component] !=
				MESH_STATE_VISIBLE)
				continue;

			wtype = craftptr->weapon_slots[slot].type;
			if (wtype != 144 && wtype != 149)
				continue;

			if (!craftptr->weapon_slots[slot].ammo)
				continue; /* not yet armed */
			if (craftptr->weapon_slots[slot]._pad_03) {
				--craftptr->weapon_slots[slot]._pad_03;
				continue;
			}

			/* Arm: clear current target and compute shooter origin. */
			craftptr->weapon_slots[slot].target_obj = 0xFFFFu;
			shooterx = ai.world_x;
			shootery = ai.world_y;
			shooterz = ai.world_z;
			pai_calcrotatedpoint(&objects[ai.active_obj_idx], spec_data[craftptr->species_idx].hp[slot].x,
								 spec_data[craftptr->species_idx].hp[slot].y,
								 spec_data[craftptr->species_idx].hp[slot].z);
#ifdef TIE_MODERN
			/* PORT: TIE98 stores model 53 (ISD) hardpoints halved; the original
			 * measures the countermeasure origin from the halved offset.
			 * Restore the full offset. */
			if (TIE_FLIGHT_TIE98 && objects[ai.active_obj_idx].ship_idx == 53) {
				rotatedx = (int32_t)((uint32_t)rotatedx << 1);
				rotatedy = (int32_t)((uint32_t)rotatedy << 1);
				rotatedz = (int32_t)((uint32_t)rotatedz << 1);
			}
#endif
			shooterx += rotatedx;
			shootery += rotatedy;
			shooterz += rotatedz;

			/* --- Pass A: pick the closest inbound homing missile that
			 * is not already being targeted by another countermeasure. */
			best_target = 0xFFFFu;
			best_dist = 0x40000u; /* cap */
			for (obj = NUM_CRAFTS, wh = 0; obj < WARHEAD_SLOT_END; ++wh, ++obj) {
				uint16_t already_shot;
				uint16_t p;
				uint16_t wh2;

				if (!objects[obj].ship_idx)
					continue;
				if (!warheads[wh].homing_tier)
					continue;
				if (warheads[wh].target_obj != ai.active_obj_idx)
					continue;

				/* Skip if another missile is already targeting obj with
				 * homing_tier >= 5. */
				already_shot = 0;
				for (p = NUM_CRAFTS, wh2 = 0; p < WARHEAD_SLOT_END; ++wh2, ++p) {
					if (!objects[p].ship_idx)
						continue;
					if (obj == p)
						continue;
					if (warheads[wh2].homing_tier < 5)
						continue;
					if (obj == warheads[wh2].target_obj)
						++already_shot;
				}
				if (already_shot >= 1)
					continue;

				roughdistance =
					collide_roughdistance3d(objects[obj].world_x - shooterx, objects[obj].world_y - shootery,
											objects[obj].world_z - shooterz);
				if (roughdistance > 0x4000 && roughdistance < best_dist) {
					best_target = obj;
					best_dist = roughdistance;
				}
			}

			if (best_target != 0xFFFFu) {
				craftptr->weapon_slots[slot].target_obj = best_target;
			} else {
				/* --- Pass B: fall back to the closest active craft
				 * attacking us (mode 12/23 with ai_target_ref==ai.active_obj_idx, or
				 * attacker_idx==that obj), with <2 existing inbound shots. */
				uint32_t best_dist_a;
				uint16_t a;

				best_target = 0xFFFFu;
				best_dist_a = 0x40000u;
				for (a = 0; a < NUM_CRAFTS; ++a) {
					CraftData* ac;
					uint16_t inbound;
					uint16_t p;
					uint16_t wh2;

					if (!objects[a].ship_idx)
						continue;
					ac = objects[a].craft_ptr;
					if (!(((uint16_t)ac->ai_target_ref == ai.active_obj_idx &&
						   (ac->mode_byte == 12 || ac->mode_byte == 23)) ||
						  a == craftptr->attacker_idx))
						continue;

					if (!pai_worthytarget(a))
						continue;
					/* Count homing warheads already in flight toward
					 * this attacker. */
					inbound = 0;
					for (p = NUM_CRAFTS, wh2 = 0; p < WARHEAD_SLOT_END; ++wh2, ++p) {
						if (!objects[p].ship_idx)
							continue;
						if (a == p)
							continue;
						if (!warheads[wh2].homing_tier)
							continue;
						if (a == warheads[wh2].target_obj)
							++inbound;
					}
					if (inbound >= 2)
						continue;

					roughdistance =
						collide_roughdistance3d(objects[a].world_x - shooterx, objects[a].world_y - shootery,
												objects[a].world_z - shooterz);
					if (roughdistance < best_dist_a) {
						best_dist_a = roughdistance;
						best_target = a;
					}
				}
			}

			if (best_target != 0xFFFFu)
				craftptr->weapon_slots[slot].target_obj = best_target;

			fire_target = craftptr->weapon_slots[slot].target_obj;
			if (fire_target == 0xFFFFu)
				continue;

			/* Fire: route through LASER_firemissile. ai_target_ref is
			 * temporarily repointed so the fire helper reads the right
			 * target, then restored. */
			saved_link = craftptr->ai_target_ref;
			craftptr->ai_target_ref = (int16_t)fire_target;
			craftptr->link_target_2E = paifight_gethullcomponent(fire_target);

			if ((new_wh = laser_firemissile(ai.active_obj_idx, slot, craftptr->weapon_slots[slot].type,
											0xFFFFu)) != 0xFFFFu) {
				warheads[new_wh].homing_tier = (uint8_t)((math2_getrandom() & 3) + 3);
				craftptr->weapon_slots[slot]._pad_03 = 20u; /* relock cooldown */
			}
			craftptr->ai_target_ref = saved_link;
		}
	}
	return 0;
}

// FUNCTION: TIE95 0x384F8
int16_t paifight_gunnerselfdefenseorder(void) {
	/* Plan slot 6: turret picks a defensive target. */
	uint16_t g;

	ai.live_target_only = 0;

	for (g = 0; craftptr->weapon_group_cnt > g; ++g) {
		/* Only type-2 (gunner) slots participate. */
		int16_t atk;
		uint32_t best_dist;
		uint16_t best_obj;
		uint16_t o;

		if (craftptr->weapon_slots[g].type != 2)
			continue;

		/* Reset slot target / armed bit up front. */
		craftptr->weapon_slots[g].target_obj = 0xFFFFu;
		craftptr->weapon_slots[g].ammo = 0;

		if (craftptr->flight_flag == 3)
			continue;
		if (!craftptr->status_flags)
			continue;
		if (craftptr->ion_drain_timer)
			continue;

		shooterx = ai.world_x;
		shootery = ai.world_y;
		shooterz = ai.world_z;
		pai_calcrotatedpoint(&objects[ai.active_obj_idx], spec_data[craftptr->species_idx].hp[g].x,
							 spec_data[craftptr->species_idx].hp[g].y,
							 spec_data[craftptr->species_idx].hp[g].z);
#ifdef TIE_MODERN
		/* PORT: TIE98 stores model 53 (ISD) hardpoints halved; the original
		 * measures the gunner range from the halved offset. Restore the full
		 * offset. */
		if (TIE_FLIGHT_TIE98 && objects[ai.active_obj_idx].ship_idx == 53) {
			rotatedx = (int32_t)((uint32_t)rotatedx << 1);
			rotatedy = (int32_t)((uint32_t)rotatedy << 1);
			rotatedz = (int32_t)((uint32_t)rotatedz << 1);
		}
#endif
		shooterx += rotatedx;
		shootery += rotatedy;
		shooterz += rotatedz;

		/* Prefer the attacker_idx when in range and worthy. */
		atk = (int16_t)craftptr->attacker_idx;
		if (pai_worthytarget((uint16_t)atk)) {
			/* attacker_idx >= 0x3800 (14336) addresses a staticobjects[]
			 * slot; reading objects[atk] there walks past NUM_CRAFTS
			 * into unrelated memory. */
			if (atk < 0x3800) {
				roughdistance =
					collide_roughdistance3d(objects[atk].world_x - shooterx, objects[atk].world_y - shootery,
											objects[atk].world_z - shooterz);
			} else {
				roughdistance = collide_roughdistance3d(staticobjects[atk - 0x3800].world_x - shooterx,
														staticobjects[atk - 0x3800].world_y - shootery,
														staticobjects[atk - 0x3800].world_z - shooterz);
			}
			if (roughdistance < 0x10000) {
				/* Two same-link rejection paths:
				 *   order 35 (board)     : reject outright
				 *   order 19 (disable+capture): require forward shield
				 *                              still up, else fall back */
				if (craftptr->default_order_ldr == 35 && (uint16_t)craftptr->ai_target_ref == atk) {
					/* fall back */
				} else if (craftptr->default_order_ldr == 19 && atk == (uint16_t)craftptr->ai_target_ref) {
					if (objects[atk].craft_ptr->forward_shield) {
						craftptr->weapon_slots[g].target_obj = (uint16_t)atk;
						continue;
					}
				} else {
					craftptr->weapon_slots[g].target_obj = (uint16_t)atk;
					continue;
				}
			}
		} else {
			craftptr->attacker_idx = 0xFFu;
		}

		/* Fallback: closest active craft attacking us in mode 12/23. */
		best_obj = 0xFFFFu;
		best_dist = 0xFFFFFFFFu;
		for (o = 0; o < NUM_CRAFTS; ++o) {
			CraftData* cp;
			uint32_t d;

			if (!objects[o].ship_idx)
				continue;
			cp = objects[o].craft_ptr;
			if ((uint16_t)cp->ai_target_ref != ai.active_obj_idx)
				continue;
			if (cp->mode_byte != 12 && cp->mode_byte != 23)
				continue;
			if (!pai_worthytarget(o))
				continue;
			d = (uint32_t)collide_roughdistance3d(
				objects[o].world_x - shooterx, objects[o].world_y - shootery, objects[o].world_z - shooterz);
			roughdistance = (int32_t)d;
			if (d < best_dist) {
				best_dist = d;
				best_obj = o;
			}
		}
		if (best_obj != 0xFFFFu && best_dist < 0x10000u)
			craftptr->weapon_slots[g].target_obj = best_obj;
	}
	return 0;
}

// FUNCTION: TIE95 0x38848
int16_t paifight_gunneroffenseorder(void) {
	/* Plan slot 7: turret picks an offensive target via the gunner
	 * target finder. */
	uint16_t g;

	if (craftptr->flight_flag == 3)
		return 0;
	if (!craftptr->status_flags)
		return 0;

	ai.live_target_only = 0;
	/* 0x30 = use search origin (bit 5) + cap range at 0x10000 (bit 4). */
	ai.search_flags = 0x30u;

	for (g = 0; g < craftptr->weapon_group_cnt; ++g) {
		uint8_t ord;
		int16_t found;

		if (craftptr->weapon_slots[g].type != 2)
			continue;
		if (craftptr->weapon_slots[g].target_obj != 0xFFFFu)
			continue;

		shooterx = ai.world_x;
		shootery = ai.world_y;
		shooterz = ai.world_z;
		pai_calcrotatedpoint(&objects[ai.active_obj_idx], spec_data[craftptr->species_idx].hp[g].x,
							 spec_data[craftptr->species_idx].hp[g].y,
							 spec_data[craftptr->species_idx].hp[g].z);
#ifdef TIE_MODERN
		/* PORT: TIE98 stores model 53 (ISD) hardpoints halved; the original
		 * measures the gunner range from the halved offset. Restore the full
		 * offset. */
		if (TIE_FLIGHT_TIE98 && objects[ai.active_obj_idx].ship_idx == 53) {
			rotatedx = (int32_t)((uint32_t)rotatedx << 1);
			rotatedy = (int32_t)((uint32_t)rotatedy << 1);
			rotatedz = (int32_t)((uint32_t)rotatedz << 1);
		}
#endif
		shooterx += rotatedx;
		ai.search_x = shooterx;
		shootery += rotatedy;
		ai.search_y = shootery;
		shooterz += rotatedz;
		ai.search_z = shooterz;

		ord = craftptr->default_order_ldr;
		if (ord == 60 || ord == 61) {
			/* Retaliation mode: just scan attacked targets. */
			craftptr->weapon_slots[g].target_obj =
				(uint16_t)paifight_checkforattackedtargets(ai.ai_entry_count);
			continue;
		}

		/* Normal mode: two-phase probe through findgunnertargetingroup.
		 * ai.live_target_only gates to status-flagged targets when the leader is
		 * on order 63 (Hunt) or order 19 (Disable+Capture). The same
		 * orders also arm (ammo=1) the slot once a target is acquired. */
		if (ord == 63 || ord == 19)
			ai.live_target_only = 1;
		else
			ai.live_target_only = 0;

		/* The original compares the sign-extended result against 0xFFFF,
		 * so the fallback probe is never taken. */
		found = paifight_findgunnertargetingroup(fg_array[ai.fg_idx].ai[ai.ai_entry_count].pri_type,
												 fg_array[ai.fg_idx].ai[ai.ai_entry_count].pri_id,
												 fg_array[ai.fg_idx].ai[ai.ai_entry_count].pri_sec_op,
												 fg_array[ai.fg_idx].ai[ai.ai_entry_count].sec_type,
												 fg_array[ai.fg_idx].ai[ai.ai_entry_count].sec_id);
		if (found != 0xFFFF) {
			craftptr->weapon_slots[g].target_obj = found;
			if (craftptr->default_order_ldr == 63 || craftptr->default_order_ldr == 19)
				craftptr->weapon_slots[g].ammo = 1; /* arm flag */
		} else {
			found = paifight_findgunnertargetingroup(fg_array[ai.fg_idx].ai[ai.ai_entry_count].target_type[0],
													 fg_array[ai.fg_idx].ai[ai.ai_entry_count].target_id[0],
													 fg_array[ai.fg_idx].ai[ai.ai_entry_count].target_op,
													 fg_array[ai.fg_idx].ai[ai.ai_entry_count].target_type[1],
													 fg_array[ai.fg_idx].ai[ai.ai_entry_count].target_id[1]);
			if (found != 0xFFFF) {
				craftptr->weapon_slots[g].target_obj = found;
				if (craftptr->default_order_ldr == 63 || craftptr->default_order_ldr == 19)
					craftptr->weapon_slots[g].ammo = 1; /* arm flag */
			}
		}
	}
	return 0;
}

// FUNCTION: TIE95 0x38B08
int16_t paifight_findgunnertargetingroup(uint16_t pri_type, uint16_t pri_id, uint16_t op, uint16_t sec_type,
										 uint16_t sec_id) {
	uint16_t o;
	uint16_t best_obj = 0xFFFFu;
	uint32_t best_dist = 0xFFFFFFFFu;
	uint16_t stat_ref;
	uint16_t s;

	/* Moving-object pass. */

	for (o = 0; o < NUM_CRAFTS; ++o) {
		int16_t pri_hit;
		int16_t hit;
		CraftData* cp;
		uint32_t d;

		if (!objects[o].ship_idx)
			continue;

		pri_hit = score_objectmemberofgroup(o, pri_type, pri_id);
		hit = score_objectmemberofgroup(o, sec_type, sec_id);
		if (op == 1)
			hit |= pri_hit;
		else
			hit &= pri_hit;
		if (!hit)
			continue;

		cp = objects[o].craft_ptr;
		if (ai.live_target_only && cp->status_flags == 0)
			continue;

		if (!pai_worthytarget(o))
			continue;

		d = (uint32_t)collide_roughdistance3d(objects[o].world_x - shooterx, objects[o].world_y - shootery,
											  objects[o].world_z - shooterz);
		roughdistance = (int32_t)d;
		if (d < best_dist) {
			best_dist = d;
			best_obj = o;
		}
	}

	/* Static-object pass, gated on hostile/structure side bit. */
	stat_ref = 0x3800u;
	for (s = 0; s < 0x40; ++s, ++stat_ref) {
		uint16_t species = staticobjects[s].species;
		int16_t pri_hit;
		int16_t hit;
		uint32_t d;

		if (species == 0)
			continue;
		if ((species_table[species].side & 2u) == 0)
			continue;

		pri_hit = score_objectmemberofgroup(stat_ref, pri_type, pri_id);
		hit = score_objectmemberofgroup(stat_ref, sec_type, sec_id);
		if (op == 1)
			hit |= pri_hit;
		else
			hit &= pri_hit;
		if (!hit)
			continue;
		if (!pai_worthytarget(stat_ref))
			continue;

		/* Static world position via create_getworldposition (packs
		 * StaticObject.world_* << 8 into worldlocx/y/z). */
		create_getworldposition(stat_ref, 0);
		d = (uint32_t)collide_roughdistance3d(worldlocx - shooterx, worldlocy - shootery,
											  worldlocz - shooterz);
		roughdistance = (int32_t)d;
		if (d < best_dist) {
			best_dist = d;
			best_obj = stat_ref;
		}
	}

	/* Gunner max engagement range: reject when best is beyond 0x10000
	 * (fixed-point 24.8). */
	if (best_dist > 0x10000u)
		best_obj = 0xFFFFu;
	return best_obj;
}

// FUNCTION: TIE95 0x38D28
int16_t paifight_coverleaderorder(void) {
	/* Plan slot 13: wingman-cover target picker. */
	uint16_t leader_idx = ai.leader_obj_idx;
	CraftData* leader_craft = objects[leader_idx].craft_ptr;
	uint16_t i;

	/* Validate the leader's attacker as a primary target. */
	if (leader_craft->attacker_idx != 0xFF && objects[leader_craft->attacker_idx].ship_idx &&
		!leader_craft->flight_flag) {
		if (objects[ai.active_obj_idx].fg_idx != objects[pstate.object_idx].fg_idx ||
			(int16_t)leader_craft->attacker_idx != pstate.radio_target) {
			if (objects[leader_craft->attacker_idx].genus == GENUS_FIGHTER ||
				objects[leader_craft->attacker_idx].genus == GENUS_TRANSPORT) {
				craftptr->ai_target_ref = leader_craft->attacker_idx;
				return 1;
			}
		}
	}

	/* Secondary: hunt a FIGHTER/TRANSPORT that is currently targeting
	 * the leader, only when default_order_ldr == 7 (Hunt). */
	if (craftptr->default_order_ldr != 7)
		return 0;

	for (i = 0; i < NUM_CRAFTS; ++i) {
		if (!objects[i].ship_idx)
			continue;
		if (objects[ai.active_obj_idx].fg_idx == objects[pstate.object_idx].fg_idx &&
			i == (uint16_t)pstate.radio_target)
			continue;
		if (objects[i].genus != GENUS_FIGHTER && objects[i].genus != GENUS_TRANSPORT)
			continue;
		if (leader_idx != (uint16_t)objects[i].craft_ptr->ai_target_ref)
			continue;
		/* Avoid self-referential cover loops when the leader is the
		 * player: skip candidates whose default_order_ldr is 28
		 * (already responding to the player) so we don't end up
		 * "covering" a wingman that's covering us back. */
		if (leader_idx == pstate.object_idx && objects[i].craft_ptr->default_order_ldr == 28)
			continue;
		craftptr->ai_target_ref = i;
		return 1;
	}
	return 0;
}

// FUNCTION: TIE95 0x38EF4
int16_t paifight_followleadatkorder(void) {
	/* Plan slot 14: attack leader's current target. */
	uint16_t leader_mode = ai.leader_craft->mode_byte;
	uint16_t leader_idx = ai.leader_obj_idx;
	uint16_t default_order_ldr = craftptr->default_order_ldr;
	uint16_t target;
	uint16_t iter;

	ai.live_target_only = craftptr->default_order_ldr == 19;

	if (leader_mode != 12 && leader_mode != 23 && leader_idx != pstate.object_idx)
		return 0;

	/* Fast path: cached target still worthy; cap at 0x50000 when the
	 * leader is the player. */
	if ((uint16_t)craftptr->pending_radio_command != 0xFF &&
		(uint16_t)craftptr->pending_radio_command != 0xFB) {
		target = craftptr->pending_radio_command;
		if (pai_worthytarget(target)) {
			if (leader_idx == pstate.object_idx) {
				pai_roughdistancebetween(ai.active_obj_idx, target);
				if (roughdistance > 0x50000)
					return 0;
			}
			craftptr->ai_target_ref = target;
			return 1;
		}
		craftptr->pending_radio_command = 0xFF;
	}

	/* Seed target: leader's current ai_target_ref (if leader is an AI), or
	 * the LAST enemy scanned that attacks the player (binary picks the
	 * last rather than the closest). */
	target = 0xFF;
	if (leader_idx != pstate.object_idx) {
		target = ai.leader_craft->ai_target_ref;
	} else {
		for (iter = 0; iter < NUM_CRAFTS; ++iter) {
			CraftData* cp;

			if (!objects[iter].ship_idx)
				continue;
			cp = objects[iter].craft_ptr;
			if (ai.live_target_only && !cp->status_flags)
				continue;
			if (iter == (uint16_t)pstate.radio_target)
				continue;
			if (pstate.object_idx != cp->attacker_idx)
				continue;
			if (objects[iter].side == pstate.player->side)
				continue;
			target = iter;
		}
		if (target == 0xFF) {
			/* The binary keeps an empty 64-entry loop here. */
			for (iter = 0; iter < 0x40; ++iter) {
			}
		}
	}

	if (target != 0xFF) {
		if (target < NUM_CRAFTS) {
			/* Moving-object pass. Scan the seed's FG starting at
			 * (craft_idx_in_fg + seed) for per-wingman spread. */
			uint16_t fg = objects[target].fg_idx;

			target += craftptr->craft_idx_in_fg;
			if (target >= NUM_CRAFTS)
				target = 0;

			for (iter = 0; iter < NUM_CRAFTS; ++iter) {
				CraftData* cp = objects[target].craft_ptr;

#ifdef TIE_MODERN
				/* Empty slots have no craft data; the binary reads garbage
				 * through NULL+0xAE. Treat them as passing the gate. */
				if ((ai.live_target_only && cp && !cp->status_flags) ||
#else
				if ((ai.live_target_only && !cp->status_flags) ||
#endif
					(objects[ai.active_obj_idx].fg_idx == objects[pstate.object_idx].fg_idx &&
					 iter == (uint16_t)pstate.radio_target)) {
					if (++target >= NUM_CRAFTS)
						target = 0;
					continue;
				}
				if (objects[target].ship_idx && fg == objects[target].fg_idx &&
					pai_checktargetforattack(ai.active_obj_idx, target, 1) &&
					((default_order_ldr != 7 && default_order_ldr != 19) ||
					 pai_isobjectvalidtarget(target))) {
					craftptr->ai_target_ref = target;
					return 1;
				}
				if (++target >= NUM_CRAFTS)
					target = 0;
			}
			return 0;
		} else {
			/* Static-object pass: only advance the scan slot past entries
			 * that pass the gate. */
			uint16_t fg;

			target -= 0x3800;
			fg = staticobjects[target].fg_idx;
			if (++target >= 0x40)
				target = 0;

			for (iter = 0; iter < 0x40; ++iter) {
				if (ai.live_target_only && !staticobjects[target].status_flags)
					continue;
				if (objects[pstate.object_idx].fg_idx == objects[ai.active_obj_idx].fg_idx &&
					target + 0x3800 == (uint16_t)pstate.radio_target)
					continue;
				if (staticobjects[target].species && fg == staticobjects[target].fg_idx &&
					pai_checktargetforattack(ai.active_obj_idx, target + 0x3800, 1) &&
					pai_isobjectvalidtarget(target + 0x3800)) {
					craftptr->ai_target_ref = target + 0x3800;
					return 1;
				}
				if (++target >= 0x40)
					target = 0;
			}
		}
	}
	return 0;
}

/* checkescortorder -- plan order handler, also called directly by
 * pai_updatecraftplan when the player's craft has default_order_ldr == 20
 * (Escort). Stamps craftptr->escortee_fg_idx with the closest matching FG. */
// FUNCTION: TIE95 0x392CC
// FUNCTION: TIE98 0x45CAB0
int16_t paifight_checkescortorder(void) {
	craftptr->escortee_fg_idx = 0xFFu; /* clear escortee FG */

	if ((uint16_t)paifight_searchforclosestingroup(fg_array[ai.fg_idx].ai[ai.ai_entry_count].pri_type,
												   fg_array[ai.fg_idx].ai[ai.ai_entry_count].pri_id,
												   fg_array[ai.fg_idx].ai[ai.ai_entry_count].pri_sec_op,
												   fg_array[ai.fg_idx].ai[ai.ai_entry_count].sec_type,
												   fg_array[ai.fg_idx].ai[ai.ai_entry_count].sec_id) !=
			0xFFFFu ||
		(uint16_t)paifight_searchforclosestingroup(fg_array[ai.fg_idx].ai[ai.ai_entry_count].target_type[0],
												   fg_array[ai.fg_idx].ai[ai.ai_entry_count].target_id[0],
												   fg_array[ai.fg_idx].ai[ai.ai_entry_count].target_op,
												   fg_array[ai.fg_idx].ai[ai.ai_entry_count].target_type[1],
												   fg_array[ai.fg_idx].ai[ai.ai_entry_count].target_id[1]) !=
			0xFFFFu)
		craftptr->escortee_fg_idx = escortfg;
	return 0;
}

/* searchforclosestingroup -- closest moving or static object in a FG
 * that matches the (pri_type, pri_id)/(sec_type, sec_id) selector under
 * op (1=AND, else OR). Side effect: writes `escortfg`. */
// FUNCTION: TIE95 0x393CC
int16_t paifight_searchforclosestingroup(uint16_t pri_type, uint16_t pri_id, uint16_t op, uint16_t sec_type,
										 uint16_t sec_id) {
	uint16_t best_obj = 0xFFFF;
	uint32_t best_dist = 0xFFFFFFFFu;
	uint16_t fg_scan;
	uint16_t pri_hit;
	uint16_t sec_hit;
	uint16_t hit;

	for (fg_scan = 0; fg_scan < mission_file_header.num_fg; ++fg_scan) {
		uint16_t obj_slot;
		uint16_t stat_slot;
		uint16_t s;

		pri_hit = score_fgmemberofgroup(fg_scan, pri_type, pri_id);
		sec_hit = score_fgmemberofgroup(fg_scan, sec_type, sec_id);
		if (op == 1)
			hit = pri_hit | sec_hit;
		else
			hit = pri_hit & sec_hit;
		if (!hit)
			continue;

		/* Moving-object scan. */
		for (obj_slot = 0; obj_slot < NUM_CRAFTS; ++obj_slot) {
			if (!objects[obj_slot].ship_idx)
				continue;
			if (fg_scan != objects[obj_slot].fg_idx)
				continue;
			pai_roughdistancebetween(ai.active_obj_idx, obj_slot);
			if ((uint32_t)roughdistance < best_dist) {
				best_dist = (uint32_t)roughdistance;
				best_obj = obj_slot;
				escortfg = (uint8_t)fg_scan;
			}
		}
		/* Static-object scan (obj_ref = 0x3800 + static_idx). */
		stat_slot = 0x3800;
		for (s = 0; s < 0x40; ++stat_slot, ++s) {
			if (!staticobjects[s].species)
				continue;
			if (staticobjects[s].fg_idx != fg_scan)
				continue;
			pai_roughdistancebetween(ai.active_obj_idx, stat_slot);
			if ((uint32_t)roughdistance < best_dist) {
				best_dist = (uint32_t)roughdistance;
				best_obj = stat_slot;
				escortfg = (uint8_t)fg_scan;
			}
		}
	}
	return best_obj;
}

// FUNCTION: TIE95 0x3950C
int16_t paifight_checkforfuturetargets(uint16_t ai_entry) {
	if (paifight_futuretargets(
			fg_array[ai.fg_idx].ai[ai_entry].pri_type, fg_array[ai.fg_idx].ai[ai_entry].pri_id,
			fg_array[ai.fg_idx].ai[ai_entry].pri_sec_op, fg_array[ai.fg_idx].ai[ai_entry].sec_type,
			fg_array[ai.fg_idx].ai[ai_entry].sec_id))
		return 1;
	if (paifight_futuretargets(
			fg_array[ai.fg_idx].ai[ai_entry].target_type[0], fg_array[ai.fg_idx].ai[ai_entry].target_id[0],
			fg_array[ai.fg_idx].ai[ai_entry].target_op, fg_array[ai.fg_idx].ai[ai_entry].target_type[1],
			fg_array[ai.fg_idx].ai[ai_entry].target_id[1]))
		return 1;
	return 0;
}

/* futuretargets -- look-ahead predicate: does any inactive/waves-
 * remaining FG match the selector under the current difficulty mask? */
// FUNCTION: TIE95 0x395D8
int16_t paifight_futuretargets(uint16_t pri_type, uint16_t pri_id, uint16_t op, uint16_t sec_type,
							   uint16_t sec_id) {
	uint16_t f;

	for (f = 0; f < mission_file_header.num_fg; ++f) {
		int16_t pri_hit;
		int16_t sec_hit;

		if ((fgdiffmask[fg_array[f].difficulty] & diffmask[mission.difficulty]) == 0)
			continue;

		pri_hit = score_fgmemberofgroup(f, pri_type, pri_id);
		sec_hit = score_fgmemberofgroup(f, sec_type, sec_id);
		if (op == 1)
			pri_hit |= sec_hit;
		else
			pri_hit &= sec_hit;
		if (pri_hit == 0)
			continue;

		if (!fgstatus[f].active)
			return 1;
		if (fgstatus[f].waves_remaining)
			return 1;
	}
	return 0;
}
