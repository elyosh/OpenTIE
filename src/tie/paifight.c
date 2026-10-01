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

/* search_flags / search_x/y/z live in the shared AiContext struct
 * (ai.search_*); watdbg's _ai[52] is a single symbol spanning both PAI
 * and PAIFIGHT fields. Declared in pai.h — no separate definitions. */

/* =====================================================================
 *                              Leaf helpers
 * ===================================================================== */

// FUNCTION: TIE95 0x3741C
int paifight_countattackers(uint16_t target_obj_idx) {
	/* Count active objects currently attacking target_obj_idx in
	 * combat modes 11/12/23. Self-attackers are skipped. */
	uint16_t attackers = 0;
	uint16_t i;
	uint16_t cap;
	int not_wingman_locked;

	for (i = 0; i < NUM_CRAFTS; ++i) {
		CraftData* cp;
		int16_t link;

		if (!objects[i].ship_idx)
			continue;
		cp = objects[i].craft_ptr;
		link = cp->ai_target_ref;
		if ((int16_t)target_obj_idx != link)
			continue;
		if ((int16_t)i == link)
			continue;
		if (cp->mode_byte == 11 || cp->mode_byte == 12 || cp->mode_byte == 23)
			++attackers;
	}

	/* Cap keyed on the candidate's genus, side, and mission difficulty
	 * when the candidate is the player. Retail short-circuits to cap=2
	 * for non-craft slots (>= NUM_CRAFTS) — without this, the genus byte
	 * of a warhead/debris slot could mis-map to cap 6/4/3. */

	if (target_obj_idx >= NUM_CRAFTS) {
		cap = 2;
	} else {
		uint8_t genus = objects[target_obj_idx].genus;
		if (genus == GENUS_STARSHIP || genus == GENUS_PLATFORM) {
			cap = 6;
		} else if (genus == GENUS_FREIGHTER) {
			cap = 4;
		} else if (objects[target_obj_idx].side == objects[pstate.object_idx].side) {
			cap = 3;
		} else {
			cap = 2;
		}
	}

	if (target_obj_idx == pstate.object_idx) {
		/* Player-as-target override; scales aggression with difficulty. */
		if (mission.difficulty == 0)
			cap = 2; /* easy */
		else if (mission.difficulty == 1)
			cap = 3; /* medium */
		else if (mission.difficulty == 2)
			cap = 4; /* hard */
					 /* mission.difficulty > 2 falls through with the genus-derived cap. */
	}

	/* Guardrail: wingmen do not dogpile the player's chosen radio target. */
	not_wingman_locked = (objects[ai.active_obj_idx].fg_idx != objects[pstate.object_idx].fg_idx) ||
						 ((int16_t)target_obj_idx != pstate.radio_target);

	return (not_wingman_locked && attackers < cap) ? 1 : 0;
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

/* =====================================================================
 *                           Target finders
 * ===================================================================== */

// FUNCTION: TIE95 0x36C3C
int16_t paifight_findtargetingroup(uint8_t pri_type, uint8_t pri_id, int16_t op, uint8_t sec_type,
								   uint8_t sec_id) {
	uint32_t best_dist = 0xFFFFFFFFu;
	uint16_t best_obj = 0xFFFF;

	/* Moving-object pass (NUM_CRAFTS slots). */
	uint16_t o;
	uint16_t stat_ref;
	uint16_t s;

	for (o = 0; o < NUM_CRAFTS; ++o) {
		int pri_hit;
		int sec_hit;
		CraftData* cp;

		if (!objects[o].ship_idx)
			continue;
		if (objects[o].fg_idx == ai.fg_idx)
			continue; /* skip same FG */

		pri_hit = score_objectmemberofgroup(o, pri_type, pri_id);
		sec_hit = score_objectmemberofgroup(o, sec_type, sec_id);
		if (!(op == 1 ? (pri_hit || sec_hit) : (pri_hit && sec_hit)))
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
	for (s = 0; s < 0x40u; ++s, ++stat_ref) {
		uint8_t species = staticobjects[s].species;
		int pri_hit;
		int sec_hit;

		if (species == 0)
			continue;
		if ((species_table[species].side & 2u) == 0u)
			continue;

		pri_hit = score_objectmemberofgroup(stat_ref, pri_type, pri_id);
		sec_hit = score_objectmemberofgroup(stat_ref, sec_type, sec_id);
		if (!(op == 1 ? (pri_hit || sec_hit) : (pri_hit && sec_hit)))
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

// FUNCTION: TIE95 0x36FE4
int16_t paifight_findescorterofgroup(uint8_t pri_type, uint8_t pri_id, int16_t op, uint8_t sec_type,
									 uint8_t sec_id) {
	uint32_t best_dist = 0xFFFFFFFFu;
	uint16_t best_obj = 0xFFFF;

	uint16_t f;

	for (f = 0; (int16_t)f < mission_file_header.num_fg; ++f) {
		int pri_hit = score_fgmemberofgroup(f, pri_type, pri_id);
		int sec_hit = score_fgmemberofgroup(f, sec_type, sec_id);
		uint16_t o;

		if (!(op == 1 ? (pri_hit || sec_hit) : (pri_hit && sec_hit)))
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
			if (cp->escortee_fg_idx != (uint8_t)f)
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
				best_obj = (int16_t)o;
			}
		}
	}

	/* Side effect: stamp the found escorter as our current link target. */
	if ((uint16_t)best_obj != 0xFFFFu)
		craftptr->ai_target_ref = best_obj;
	return best_obj;
}

// FUNCTION: TIE95 0x37210
int16_t paifight_findattackedtargetingroup(uint8_t pri_type, uint8_t pri_id, int16_t op, uint8_t sec_type,
										   uint8_t sec_id) {
	uint32_t best_dist = 0xFFFFFFFFu;
	uint16_t best_obj = 0xFFFF;

	uint16_t target;

	for (target = 0; target < NUM_CRAFTS; ++target) {
		CraftData* tc;
		int pri_hit;
		int sec_hit;
		uint16_t a;

		if (!objects[target].ship_idx)
			continue;
		tc = objects[target].craft_ptr;
		if (!tc->was_hit_flag)
			continue;

		pri_hit = score_objectmemberofgroup(target, pri_type, pri_id);
		sec_hit = score_objectmemberofgroup(target, sec_type, sec_id);
		if (!(op == 1 ? (pri_hit || sec_hit) : (pri_hit && sec_hit)))
			continue;

		/* Inner: pick the closest live attacker. An attacker qualifies
		 * when (a) its mode is 11/12/23 AND it is targeting `target`,
		 * OR (b) was_hit_flag bit 0x80 is set AND the attacker slot is
		 * the player's own object. */
		for (a = 0; a < NUM_CRAFTS; ++a) {
			CraftData* ac;
			int combat_atk;
			int player_atk;

			if (!objects[a].ship_idx)
				continue;
			ac = objects[a].craft_ptr;

			combat_atk = (ac->mode_byte == 11 || ac->mode_byte == 12 || ac->mode_byte == 23) &&
						 (int16_t)target == ac->ai_target_ref;
			player_atk = (tc->was_hit_flag & 0x80u) != 0 && a == pstate.object_idx;
			if (!(combat_atk || player_atk))
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

// FUNCTION: TIE95 0x38B08
uint16_t paifight_findgunnertargetingroup(uint8_t pri_type, uint8_t pri_id, int16_t op, uint8_t sec_type,
										  uint8_t sec_id) {
	uint32_t best_dist = 0xFFFFFFFFu;
	uint16_t best_obj = 0xFFFFu;

	/* Moving-object pass. */
	uint16_t o;
	uint16_t stat_ref;
	uint16_t s;

	for (o = 0; o < NUM_CRAFTS; ++o) {
		int pri_hit;
		int sec_hit;
		CraftData* cp;
		uint32_t d;

		if (!objects[o].ship_idx)
			continue;

		pri_hit = score_objectmemberofgroup(o, pri_type, pri_id);
		sec_hit = score_objectmemberofgroup(o, sec_type, sec_id);
		if (!(op == 1 ? (pri_hit || sec_hit) : (pri_hit && sec_hit)))
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
	for (s = 0; s < 0x40u; ++s, ++stat_ref) {
		uint8_t species = staticobjects[s].species;
		int pri_hit;
		int sec_hit;
		uint32_t d;

		if (species == 0)
			continue;
		if ((species_table[species].side & 2u) == 0)
			continue;

		pri_hit = score_objectmemberofgroup(stat_ref, pri_type, pri_id);
		sec_hit = score_objectmemberofgroup(stat_ref, sec_type, sec_id);
		if (!(op == 1 ? (pri_hit || sec_hit) : (pri_hit && sec_hit)))
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
		return 0xFFFFu;
	return best_obj;
}

// FUNCTION: TIE95 0x36B74
int16_t paifight_checkfortargets(uint16_t ai_entry) {
	const EAIStruct* cur_ai = &fg_array[ai.fg_idx].ai[ai_entry];
	int16_t result = paifight_findtargetingroup(cur_ai->pri_type, cur_ai->pri_id, cur_ai->pri_sec_op,
												cur_ai->sec_type, cur_ai->sec_id);
	if ((uint16_t)result == 0xFFFFu) {
		result = paifight_findtargetingroup(cur_ai->target_type[0], cur_ai->target_id[0], cur_ai->target_op,
											cur_ai->target_type[1], cur_ai->target_id[1]);
	}
	return result;
}

// FUNCTION: TIE95 0x36F1C
int16_t paifight_checkforescortertargets(uint16_t ai_entry) {
	const EAIStruct* cur_ai = &fg_array[ai.fg_idx].ai[ai_entry];
	int16_t result = paifight_findescorterofgroup(cur_ai->pri_type, cur_ai->pri_id, cur_ai->pri_sec_op,
												  cur_ai->sec_type, cur_ai->sec_id);
	if ((uint16_t)result == 0xFFFFu) {
		result = paifight_findescorterofgroup(cur_ai->target_type[0], cur_ai->target_id[0], cur_ai->target_op,
											  cur_ai->target_type[1], cur_ai->target_id[1]);
	}
	return result;
}

// FUNCTION: TIE95 0x37148
int16_t paifight_checkforattackedtargets(uint16_t ai_entry) {
	const EAIStruct* cur_ai = &fg_array[ai.fg_idx].ai[ai_entry];
	int16_t result = paifight_findattackedtargetingroup(cur_ai->pri_type, cur_ai->pri_id, cur_ai->pri_sec_op,
														cur_ai->sec_type, cur_ai->sec_id);
	if ((uint16_t)result == 0xFFFFu) {
		result = paifight_findattackedtargetingroup(cur_ai->target_type[0], cur_ai->target_id[0],
													cur_ai->target_op, cur_ai->target_type[1],
													cur_ai->target_id[1]);
	}
	return result;
}

// FUNCTION: TIE95 0x3950C
int16_t paifight_checkforfuturetargets(uint16_t ai_entry) {
	const EAIStruct* cur_ai = &fg_array[ai.fg_idx].ai[ai_entry];
	int16_t result;

	if (paifight_futuretargets(cur_ai->pri_type, cur_ai->pri_id, cur_ai->pri_sec_op, cur_ai->sec_type,
							   cur_ai->sec_id)) {
		return 1;
	}

	result = paifight_futuretargets(cur_ai->target_type[0], cur_ai->target_id[0], cur_ai->target_op,
									cur_ai->target_type[1], cur_ai->target_id[1]);
	if (result)
		return 1;
	return result;
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

// FUNCTION: TIE95 0x36A90
int16_t paifight_scanfortargetorder(void) {
	/* Plan slot 9 -- target-acquire step. Bails when the craft's mode
	 * has drifted from the plan's declared order. */
	uint16_t cached;
	uint8_t default_order_ldr;
	uint16_t found;

	if (craftptr->mode_byte != (uint8_t)ai.plan_order)
		return 0;

	/* Fast path: re-use the last target if it is still worthy. */
	cached = (uint16_t)craftptr->pending_radio_command;
	if (cached != 0xFFu && cached != 0xFBu) {
		if (pai_worthytarget(cached)) {
			craftptr->ai_target_ref = (int16_t)cached;
			return 1;
		}
		craftptr->pending_radio_command = 0xFF;
	}

	default_order_ldr = craftptr->default_order_ldr;
	ai.live_target_only = (default_order_ldr == 19) ? 1 : 0;
	ai.search_flags = 7u;

	if (default_order_ldr == 7 || default_order_ldr == 19) {
		found = (uint16_t)paifight_checkfortargets(ai.ai_entry_count);
	} else if (default_order_ldr == 8) {
		found = (uint16_t)paifight_checkforescortertargets(ai.ai_entry_count);
	} else {
		found = (uint16_t)paifight_checkforattackedtargets(ai.ai_entry_count);
	}
	if (found == 0xFFFFu)
		return 0;
	craftptr->ai_target_ref = (int16_t)found;
	return 1;
}

/* checkescortorder -- plan order handler, also called directly by
 * pai_updatecraftplan when the player's craft has default_order_ldr == 20
 * (Escort). Stamps craftptr->escortee_fg_idx with the closest matching FG. */
// FUNCTION: TIE95 0x392CC
// FUNCTION: TIE98 0x45CAB0
int16_t paifight_checkescortorder(void) {
	const EAIStruct* cur_ai = &fg_array[ai.fg_idx].ai[ai.ai_entry_count];

	int found_pri;
	int found_any;

	craftptr->escortee_fg_idx = 0xFFu; /* clear escortee FG */

	found_pri =
		(uint16_t)paifight_searchforclosestingroup(cur_ai->pri_type, cur_ai->pri_id, cur_ai->pri_sec_op,
												   cur_ai->sec_type, cur_ai->sec_id) != 0xFFFFu;
	found_any = found_pri;
	if (!found_pri) {
		found_any = (uint16_t)paifight_searchforclosestingroup(cur_ai->target_type[0], cur_ai->target_id[0],
															   cur_ai->target_op, cur_ai->target_type[1],
															   cur_ai->target_id[1]) != 0xFFFFu;
	}
	if (found_any)
		craftptr->escortee_fg_idx = escortfg;
	return 0;
}

/* =====================================================================
 *                        Plan-VM combat handlers
 * ===================================================================== */

// FUNCTION: TIE95 0x376D4
int16_t paifight_escorttargetorder(void) {
	/* Plan slot 23: escort target picker. */
	uint16_t cached;
	uint16_t best_obj;
	uint32_t best_dist;
	uint8_t escortee_fg;
	uint8_t escort_protect;
	uint16_t obj;

	if (craftptr->mode_byte != (uint8_t)ai.plan_order)
		return 0;

	/* Fast path: cached target still worthy. */
	cached = (uint16_t)craftptr->pending_radio_command;
	if (cached != 0xFFu && cached != 0xFBu) {
		if (pai_worthytarget(cached)) {
			craftptr->ai_target_ref = (int16_t)cached;
			return 1;
		}
		craftptr->pending_radio_command = 0xFF;
	}

	best_obj = 0xFFFFu;
	best_dist = 0xFFFFFFFFu;
	escortee_fg = craftptr->escortee_fg_idx;
	escort_protect = fg_array[ai.fg_idx].ai[ai.ai_entry_count].var[1];

	for (obj = 0; obj < NUM_CRAFTS; ++obj) {
		CraftData* cp;
		uint16_t lnk;
		int in_fg;

		if (!objects[obj].ship_idx || obj == ai.active_obj_idx)
			continue;
		cp = objects[obj].craft_ptr;

		/* Branch A: obj is the player or a player-FG wingman and the
		 * escort_protect flag is set. */
		if (escort_protect && (obj == pstate.object_idx || (cp->leader_obj_idx != 255u &&
															objects[obj].fg_idx == pstate.player->fg_idx))) {
			if (!paifight_countattackers(obj))
				continue;
			pai_roughdistancebetween(ai.active_obj_idx, obj);
			if ((uint32_t)roughdistance < best_dist && (uint32_t)roughdistance < 0x40000u) {
				best_dist = (uint32_t)roughdistance;
				best_obj = obj;
			}
			continue;
		}

		/* Branch B: obj is an enemy whose current ai_target_ref points at
		 * something in our escortee FG. */
		lnk = (uint16_t)cp->ai_target_ref;

		in_fg = 0;
		if (lnk >= OBJ_REF_STATIC_BASE) {
			uint16_t idx;

			if (lnk >= OBJ_REF_WAYPOINT_BASE || lnk == 0xFFFFu)
				continue;
			idx = lnk - OBJ_REF_STATIC_BASE;
			if (staticobjects[idx].species && staticobjects[idx].fg_idx == escortee_fg) {
				in_fg = 1;
			}
		} else {
			if (objects[lnk].ship_idx && objects[lnk].fg_idx == escortee_fg) {
				in_fg = 1;
			}
		}
		if (!in_fg)
			continue;

		if (!paifight_countattackers(obj))
			continue;
		pai_roughdistancebetween(ai.active_obj_idx, obj);
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
	uint8_t burst_count;
	uint16_t laser_cnt;
	uint16_t order_ldr;
	int16_t warhead_class;
	uint16_t max_inbound, my_missile_cap;
	uint32_t max_engage_range;
	uint8_t tgt_genus;
	uint32_t incoming_dmg;
	uint16_t missiles_inbound;
	uint16_t lock_threshold;
	uint16_t j;

	uint16_t g;
	uint16_t obj;
	uint16_t new_total;

	if (!craftptr->status_flags)
		return 0;

	target = (uint16_t)craftptr->ai_target_ref;

	if (!pai_worthytarget(target)) {
		/* Clear the per-group laser-owner state so no spurious shots
		 * fire next frame. */
		uint16_t g;

		for (g = 0; g < craftptr->laser_group_cnt; ++g)
			craftptr->laser_owner_player[g] = 0;
		return 0;
	}

	/* --- Aim-cone range (tightens as the heading delta to the target grows). --- */
	aim_range = frwdgunnerranges[(uint16_t)ai.skill_tier];
	if (target < NUM_OBJECTS) {
		uint16_t heading_delta = (uint16_t)(objects[ai.active_obj_idx].heading - objects[target].heading);
		if (heading_delta >= 0x8000u)
			heading_delta = (uint16_t)-(int16_t)heading_delta;
		if (heading_delta >= 0x2000u) {
			if (heading_delta < 0x5000u)
				aim_range -= 0x2000u;
		} else {
			aim_range -= 0x4000u;
		}
	}

	/* --- Line-up + burst tier computation. --- */
	pai_distancebetween(ai.active_obj_idx, target);
	xy_delta = (uint16_t)(trig2_xyangle - objects[ai.active_obj_idx].heading);
	if (xy_delta >= 0x8000u)
		xy_delta = (uint16_t)-(int16_t)xy_delta;
	z_delta = (uint16_t)(trig2_zangle - craftptr->orient_pitch);
	if (z_delta >= 0x8000u)
		z_delta = (uint16_t)-(int16_t)z_delta;

	burst_tier = 0;
	burst_count = 0;
	if (xy_delta < 0x800u && z_delta < 0x800u && aim_range > (uint32_t)trig2_polardistance) {
		if (trig2_polardistance >= 0x4000)
			burst_tier = 1;
		else if (trig2_polardistance >= 0x2000)
			burst_tier = 2;
		else
			burst_tier = 3;
		burst_count = frwdgunnerbursts[(uint16_t)ai.skill_tier];
	}

	/* --- Per-laser-group owner/cooldown state.
	 * The binary selects between burst_tier and 0 with the truth table
	 *   (laser_type == 141) XOR (default_order_ldr == 19)
	 * where the XOR path writes `default_order_ldr ^ default_order_ldr`
	 * (always 0). Order 19 pairs naturally with plasma (141); a
	 * conventional laser in order 19 or plasma outside order 19 is
	 * suppressed to 0. --- */
	laser_cnt = craftptr->laser_group_cnt;
	order_ldr = craftptr->default_order_ldr;
	for (g = 0; g < laser_cnt; ++g) {
		uint8_t owner_val = 0;
		if (burst_tier) {
			int is_plasma = craftptr->laser_type[g] == 141;
			int is_disable_capture = order_ldr == 19;
			if (is_plasma == is_disable_capture)
				owner_val = (uint8_t)burst_tier;
			/* else owner_val stays 0 */
		}
		craftptr->laser_owner_player[g] = owner_val;
		craftptr->laser_burst_remaining[g] = burst_count;
	}

	/* --- Missile logic gate: bail when target is out of slot range. --- */
	if (target >= NUM_CRAFTS)
		return 0;

	/* Warhead-class tier (1 = light / fighter/transport, 2 = heavy /
	 * freighter/capship). The binary switches per-genus caps and the
	 * max lock range via this classifier. */

	tgt_genus = objects[target].genus;
	if (tgt_genus == GENUS_STARSHIP || tgt_genus == GENUS_PLATFORM || tgt_genus == GENUS_FREIGHTER) {
		warhead_class = 2;
		max_inbound = 6;
		my_missile_cap = 2;
		max_engage_range = 203610u;
	} else {
		warhead_class = 1;
		max_inbound = 2;
		my_missile_cap = 1;
		max_engage_range = 101805u;
	}

	/* Order 19 disables a target for capture. Avoid launching enough
	 * ordnance to destroy it, including projectiles already in flight. */
	incoming_dmg = 0;
	if (order_ldr == 19) {
		CraftData* target_craft = objects[target].craft_ptr;
		uint32_t damage_limit =
			(uint32_t)((int32_t)(target_craft->hull_damage >> 2) + target_craft->forward_shield);
		uint16_t w;
		uint16_t obj;

		for (w = 0; w < craftptr->missile_group_cnt; ++w) {
			uint8_t wt = craftptr->warhead_type[w];
			uint16_t dmg;

			if (wt < WEAPON_SPECIES_BASE || wt >= WEAPON_SPECIES_BASE + WARHEAD_TYPE_COUNT ||
				projectile_is_warhead_type[wt - WEAPON_SPECIES_BASE] != warhead_class)
				continue;
			dmg = projectileweight[wt - WEAPON_SPECIES_BASE];
			if (tgt_genus == GENUS_STARSHIP || tgt_genus == GENUS_PLATFORM)
				dmg >>= 4;
			if (tgt_genus == GENUS_FREIGHTER)
				dmg >>= 2;
			incoming_dmg += dmg;
		}
		for (obj = NUM_CRAFTS; obj < WARHEAD_SLOT_END; ++obj) {
			uint8_t projectile_species = objects[obj].ship_idx;
			CraftData* projectile;

			if (projectile_species < WEAPON_SPECIES_BASE ||
				projectile_species >= WEAPON_SPECIES_BASE + WEAPON_SPECIES_COUNT)
				continue;
			projectile = objects[obj].craft_ptr;
			if (!projectile->species_idx || projectile->missile_target != target)
				continue;
			incoming_dmg += projectileweight[projectile_species - WEAPON_SPECIES_BASE];
		}
		if (incoming_dmg >= damage_limit)
			return 0;
	}

	/* Count live incoming missiles of the same class. Projectile slot
	 * range is [NUM_CRAFTS, WARHEAD_SLOT_END) = [32, 80) retail /
	 * [28, 76) demo (matches warheads[] index 0..47). Skips slots
	 * whose craft_ptr alias lacks species_idx (free). */
	missiles_inbound = 0;
	for (obj = NUM_CRAFTS; obj < WARHEAD_SLOT_END; ++obj) {
		uint8_t ship_idx = objects[obj].ship_idx;
		CraftData* ic;

		if (!ship_idx)
			continue;
		if (ship_idx < WEAPON_SPECIES_BASE || ship_idx >= WEAPON_SPECIES_BASE + WARHEAD_TYPE_COUNT ||
			projectile_is_warhead_type[ship_idx - WEAPON_SPECIES_BASE] != warhead_class)
			continue;
		ic = objects[obj].craft_ptr;
		if (ic->species_idx == 0)
			continue;
		if (target == ic->missile_target)
			++missiles_inbound;
	}
	if (missiles_inbound >= max_inbound)
		return 0;

	/* Per-missile gates. */
	if (craftptr->mode_byte != 23)
		return 0;
	if (craftptr->ion_drain_timer)
		return 0;
	if ((craftptr->beam_state & 2u) != 0)
		return 0;
	if (craftptr->missile_count >= my_missile_cap)
		return 0;

	/* --- Lock accumulator. Out-of-angle / out-of-range: decay the lock
	 * counter. In-window: accumulate at ai_update_rate scaled by 0xC000
	 * and compare to the per-skill-tier threshold
	 *   lock_threshold = 236 * (2*skill_tier + 2)
	 * before firing. --- */
	if (xy_delta >= 0x300u || z_delta >= 0x300u || max_engage_range <= (uint32_t)trig2_polardistance) {
		int16_t new_total = (int16_t)(craftptr->missile_count_total - craftptr->ai_update_rate);
		craftptr->missile_count_total = (uint16_t)new_total;
		if (new_total < 0)
			craftptr->missile_count_total = 0;
		return 0;
	}

	new_total = (uint16_t)(math2_fraction(craftptr->ai_update_rate, 0xC000u) + craftptr->missile_count_total);
	craftptr->missile_count_total = new_total;

	lock_threshold = (uint16_t)(236u * (2u * (uint16_t)ai.skill_tier + 2u));
	if (new_total < lock_threshold)
		return 0;

	/* --- Fire loop. Arm the first empty slot of matching class; pick
	 * hull aim-point; call laser_firerocketsystem. --- */
	for (j = 0; j < craftptr->missile_group_cnt; ++j) {
		uint8_t slot_idx;
		uint8_t final_genus;

		if (craftptr->missile_state[j])
			continue;
		if (craftptr->warhead_type[j] < WEAPON_SPECIES_BASE ||
			craftptr->warhead_type[j] >= WEAPON_SPECIES_BASE + WARHEAD_TYPE_COUNT ||
			projectile_is_warhead_type[craftptr->warhead_type[j] - WEAPON_SPECIES_BASE] != warhead_class)
			continue;

		/* Ammo balance across the two sub-pods (left/right). If the
		 * left pod has less than the right, set armed byte to -127
		 * (negative flag = left first); else 1 (right first). The
		 * spec_data.missile_start maps the group to the weapon_slots
		 * pair (v28 and v28+1). */
		slot_idx = spec_data[craftptr->species_idx].missile_start[j];
		if (craftptr->weapon_slots[slot_idx].ammo < craftptr->weapon_slots[slot_idx + 1u].ammo)
			craftptr->missile_armed[j] = (uint8_t)(-127);
		else
			craftptr->missile_armed[j] = 1u;

		craftptr->link_target_2E = paifight_gethullcomponent(target);
		laser_firerocketsystem(ai.active_obj_idx, j);
		++craftptr->missile_count;

		final_genus = objects[target].genus;
		if (final_genus == GENUS_FIGHTER || final_genus == GENUS_TRANSPORT)
			craftptr->missile_count_total = 0;
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

			if (craftptr->mesh_state[spec_data[craftptr->species_idx].hp[slot].component] != MESH_STATE_VISIBLE)
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

				roughdistance = collide_roughdistance3d(objects[obj].world_x - shooterx,
														objects[obj].world_y - shootery,
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

					roughdistance = collide_roughdistance3d(objects[a].world_x - shooterx,
															objects[a].world_y - shootery,
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

			if ((new_wh = laser_firemissile(ai.active_obj_idx, slot, craftptr->weapon_slots[slot].type, 0xFFFFu)) != 0xFFFFu) {
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

	for (g = 0; g < craftptr->weapon_group_cnt; ++g) {
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
		shooterx += rotatedx;
		shootery += rotatedy;
		shooterz += rotatedz;

		/* Prefer the attacker_idx when in range and worthy. */
		atk = (int16_t)craftptr->attacker_idx;
		if (pai_worthytarget((uint16_t)atk)) {
			/* attacker_idx >= 0x3800 (14336) addresses a staticobjects[]
			 * slot; reading objects[atk] there walks past NUM_CRAFTS
			 * into unrelated memory. */
			int32_t tx, ty, tz;
			uint32_t d;

			if ((uint16_t)atk >= 0x3800u) {
				StaticObject* sa = &staticobjects[(uint16_t)atk - 0x3800u];
				tx = sa->world_x;
				ty = sa->world_y;
				tz = sa->world_z;
			} else {
				tx = objects[atk].world_x;
				ty = objects[atk].world_y;
				tz = objects[atk].world_z;
			}
			d = (uint32_t)collide_roughdistance3d(tx - shooterx, ty - shootery, tz - shooterz);
			roughdistance = (int32_t)d;
			if (d < 0x10000u) {
				/* Two same-link rejection paths:
				 *   order 35 (board)     : reject outright
				 *   order 19 (disable+capture): require forward shield
				 *                              still up, else fall back */
				int reject = 0;
				uint8_t ord = craftptr->default_order_ldr;
				if (ord == 35 && (uint16_t)craftptr->ai_target_ref == (uint16_t)atk) {
					reject = 1;
				} else if (ord == 19 && (uint16_t)craftptr->ai_target_ref == (uint16_t)atk) {
					if ((uint16_t)atk < NUM_CRAFTS && objects[atk].craft_ptr->forward_shield == 0)
						reject = 1;
				}
				if (!reject) {
					craftptr->weapon_slots[g].target_obj = (uint16_t)atk;
					continue;
				}
			}
		} else {
			craftptr->attacker_idx = 0xFFu;
		}

		/* Fallback: closest active craft attacking us in mode 12/23. */
		best_dist = 0xFFFFFFFFu;
		best_obj = 0xFFFFu;
		for (o = 0; o < NUM_CRAFTS; ++o) {
			CraftData* cp;
			uint32_t d;

			if (!objects[o].ship_idx)
				continue;
			cp = objects[o].craft_ptr;
			if (ai.active_obj_idx != (uint16_t)cp->ai_target_ref)
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
		int special_order;
		const EAIStruct* cur_ai;
		uint16_t found;

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
		special_order = (ord == 63 || ord == 19);
		ai.live_target_only = special_order ? 1 : 0;

		cur_ai = &fg_array[ai.fg_idx].ai[ai.ai_entry_count];
		found = paifight_findgunnertargetingroup(cur_ai->pri_type, cur_ai->pri_id, cur_ai->pri_sec_op,
												 cur_ai->sec_type, cur_ai->sec_id);
		if (found == 0xFFFFu) {
			found = paifight_findgunnertargetingroup(cur_ai->target_type[0], cur_ai->target_id[0],
													 cur_ai->target_op, cur_ai->target_type[1],
													 cur_ai->target_id[1]);
		}
		craftptr->weapon_slots[g].target_obj = found;
		if (found != 0xFFFFu && special_order)
			craftptr->weapon_slots[g].ammo = 1; /* arm flag */
	}
	return 0;
}

// FUNCTION: TIE95 0x38D28
int16_t paifight_coverleaderorder(void) {
	/* Plan slot 13: wingman-cover target picker. */
	CraftData* leader_craft = objects[ai.leader_obj_idx].craft_ptr;
	uint16_t atk = leader_craft->attacker_idx;

	/* Validate the leader's attacker as a primary target. */
	uint16_t i;

	if (atk != 0xFFu && objects[atk].ship_idx && !leader_craft->flight_flag) {
		int wingman_locked = (objects[ai.active_obj_idx].fg_idx == objects[pstate.object_idx].fg_idx) &&
							 (pstate.radio_target == (int16_t)leader_craft->attacker_idx);
		uint8_t atk_genus = objects[leader_craft->attacker_idx].genus;
		if (!wingman_locked && (atk_genus == GENUS_FIGHTER || atk_genus == GENUS_TRANSPORT)) {
			craftptr->ai_target_ref = (int16_t)leader_craft->attacker_idx;
			return 1;
		}
	}

	/* Secondary: hunt a FIGHTER/TRANSPORT that is currently targeting
	 * the leader, only when default_order_ldr == 7 (Hunt). */
	if (craftptr->default_order_ldr != 7)
		return 0;

	for (i = 0; i < NUM_CRAFTS; ++i) {
		int wingman_skip;
		uint8_t g;

		if (!objects[i].ship_idx)
			continue;
		wingman_skip = (objects[ai.active_obj_idx].fg_idx == objects[pstate.object_idx].fg_idx) &&
					   (i == (uint16_t)pstate.radio_target);
		if (wingman_skip)
			continue;
		g = objects[i].genus;
		if (g != GENUS_FIGHTER && g != GENUS_TRANSPORT)
			continue;
		if (ai.leader_obj_idx != (uint16_t)objects[i].craft_ptr->ai_target_ref)
			continue;
		/* Avoid self-referential cover loops when the leader is the
		 * player: skip candidates whose default_order_ldr is 28
		 * (already responding to the player) so we don't end up
		 * "covering" a wingman that's covering us back. */
		if (ai.leader_obj_idx == pstate.object_idx && objects[i].craft_ptr->default_order_ldr == 28)
			continue;
		craftptr->ai_target_ref = (int16_t)i;
		return 1;
	}
	return 0;
}

// FUNCTION: TIE95 0x38EF4
int16_t paifight_followleadatkorder(void) {
	/* Plan slot 14: attack leader's current target. */
	uint8_t leader_mode = ai.leader_craft->mode_byte;
	uint16_t leader_idx = ai.leader_obj_idx;
	uint8_t default_order_ldr = craftptr->default_order_ldr;
	uint16_t cached;
	uint16_t seed_target;
	uint8_t seed_fg;
	uint16_t scan_obj;
	uint16_t iter;

	ai.live_target_only = (default_order_ldr == 19) ? 1 : 0;

	if (leader_mode != 12 && leader_mode != 23 && leader_idx != pstate.object_idx)
		return 0;

	/* Fast path: cached target still worthy; cap at 0x50000 when the
	 * leader is the player. */
	cached = (uint16_t)craftptr->pending_radio_command;
	if (cached != 0xFFu && cached != 0xFBu) {
		if (pai_worthytarget(cached)) {
			if (leader_idx == pstate.object_idx) {
				pai_roughdistancebetween(ai.active_obj_idx, cached);
				if (roughdistance > 0x50000)
					return 0;
			}
			craftptr->ai_target_ref = (int16_t)cached;
			return 1;
		}
		craftptr->pending_radio_command = 0xFF;
	}

	/* Seed target: leader's current ai_target_ref (if leader is an AI), or
	 * the LAST enemy scanned that attacks the player (binary picks the
	 * last rather than the closest). */
	seed_target = 0x00FFu;
	if (leader_idx == pstate.object_idx) {
		uint16_t i;

		for (i = 0; i < NUM_CRAFTS; ++i) {
			CraftData* cp;

			if (!objects[i].ship_idx)
				continue;
			cp = objects[i].craft_ptr;
			if (i == (uint16_t)pstate.radio_target)
				continue;
			if (ai.live_target_only && !cp->status_flags)
				continue;
			if (pstate.object_idx != cp->attacker_idx)
				continue;
			if (objects[i].side == pstate.player->side)
				continue;
			seed_target = i;
		}
		/* Dropped here: the binary emits a dead `for j=0..0x3F; ;` loop
		 * at 0x39045 after the scan. No side effects, no observable
		 * behaviour difference. */
	} else {
		seed_target = (uint16_t)ai.leader_craft->ai_target_ref;
	}

	if (seed_target == 0x00FFu)
		return 0;

	/* Two scan paths: static (seed_target >= NUM_CRAFTS) and moving. */
	if (seed_target >= NUM_CRAFTS) {
		uint16_t stat_idx = seed_target - 0x3800u;
		uint8_t stat_fg = staticobjects[stat_idx].fg_idx;
		uint16_t stat_scan = stat_idx + 1u;
		uint16_t k;

		if (stat_scan >= 0x40u)
			stat_scan = 0;

		for (k = 0; k < 0x40u; ++k) {
			uint16_t stat_ref = 0x3800u + stat_scan;

			int wingman_skip = (objects[pstate.object_idx].fg_idx == objects[ai.active_obj_idx].fg_idx) &&
							   (stat_ref == (uint16_t)pstate.radio_target);
			int passes_flag = !ai.live_target_only || staticobjects[stat_scan].status_flags;
			if (passes_flag && !wingman_skip) {
				if (staticobjects[stat_scan].species && stat_fg == staticobjects[stat_scan].fg_idx) {
					if (pai_checktargetforattack(ai.active_obj_idx, stat_ref, 1) && pai_isobjectvalidtarget(stat_ref)) {
						craftptr->ai_target_ref = (int16_t)stat_ref;
						return 1;
					}
				}
				if (++stat_scan >= 0x40u)
					stat_scan = 0;
			} else {
				/* Binary advances stat_scan only on the gate-pass path.
				 * Mirror it: on gate fail we retry the same slot next
				 * iteration. */
			}
		}
		return 0;
	}

	/* Moving-object pass. Scan seed_target's FG starting at
	 * (craftptr->craft_idx_in_fg + seed) for per-wingman spread. */
	seed_fg = objects[seed_target].fg_idx;
	scan_obj = (uint16_t)(craftptr->craft_idx_in_fg + seed_target);
	if (scan_obj >= NUM_CRAFTS)
		scan_obj = 0;

	for (iter = 0; iter < NUM_CRAFTS; ++iter) {
		int wingman_skip = (objects[ai.active_obj_idx].fg_idx == objects[pstate.object_idx].fg_idx) &&
						   (iter == (uint16_t)pstate.radio_target);
		/* Empty slot (no craft data): binary reads through a NULL+0xAE
		 * pointer which lands in DOS BIOS memory and returns garbage;
		 * here we treat the gate as "pass" so the slot falls through to
		 * the ship_idx check at line 1431 which filters it out. */
		CraftData* scan_cp = objects[scan_obj].craft_ptr;
		int flag_skip = ai.live_target_only && scan_cp && !scan_cp->status_flags;
		if (flag_skip || wingman_skip) {
			/* Gate fail: advance with wrap, matching the binary's
			 * conditional inc path. */
			if (++scan_obj >= NUM_CRAFTS)
				scan_obj = 0;
			continue;
		}
		if (objects[scan_obj].ship_idx && seed_fg == objects[scan_obj].fg_idx &&
			pai_checktargetforattack(ai.active_obj_idx, scan_obj, 1)) {
			if (default_order_ldr != 7 && default_order_ldr != 19) {
				craftptr->ai_target_ref = (int16_t)scan_obj;
				return 1;
			}
			if (pai_isobjectvalidtarget(scan_obj)) {
				craftptr->ai_target_ref = (int16_t)scan_obj;
				return 1;
			}
		}
		if (++scan_obj >= NUM_CRAFTS)
			scan_obj = 0;
	}
	return 0;
}
