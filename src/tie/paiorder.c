#include "tie/paiorder.h"
#include "tie/collide.h"
#include "tie/create.h"
#include "tie/laser.h" /* projectilevelocity — warhead-class byte at +7 */
#include "tie/math2.h"
#include "tie/mission.h"
#include "tie/msg.h"
#include "tie/pai.h"
#include "tie/paifight.h"
#include "tie/paiman.h"
#include "tie/score.h"
#include "tie/tie.h"
#include "tie/trig2.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

/* Per-skill awareness radii. Retail stores three dwords per table. */
// GLOBAL: TIE95 0xC5C5C
// GLOBAL: TIE98 0x4E6758
static const uint32_t planeranges[3] = { 0x8000, 0xC000, 0xE000 };
// GLOBAL: TIE95 0xC5C68
// GLOBAL: TIE98 0x4E6768
static const uint32_t noticeplaneranges[3] = { 0x2000, 0x3000, 0x4000 };
// GLOBAL: TIE95 0xC5C74
// GLOBAL: TIE98 0x4E6778
static const uint32_t noticemissileranges[3] = { 0x400, 0x800, 0x1000 };

/* Heading-difference octants: head-on (0), side (1), or tail (2). */
// GLOBAL: TIE95 0xC5C80
// GLOBAL: TIE98 0x4E6788
static const uint8_t approachtable[8] = { 0, 1, 1, 2, 2, 1, 1, 0 };
// GLOBAL: TIE95 0xC5C88
// GLOBAL: TIE98 0x4E6790
static const uint8_t sidemaneuvers[4] = { 0x0D, 0x0E, 0x0F, 0x03 };
// GLOBAL: TIE95 0xC5C8C
// GLOBAL: TIE98 0x4E6798
static const uint8_t sternmaneuvers[8] = { 0x01, 0x04, 0x01, 0x03, 0x01, 0x04, 0x0F, 0x04 };

/* ======================================================================
 *                   Slot 0 — nullorder (stub)
 * ====================================================================== */

// FUNCTION: TIE95 0x3D530
int16_t paiorder_nullorder(void) { return 0; }

/* ======================================================================
 *                   Slot 1 — updatecourseorder
 *
 * Dispatch through PAIMAN's runtime maneuver table. The binary form is
 * an indirect call to _manvrfunctionptrs[craftptr->mode_byte].
 * ====================================================================== */

// FUNCTION: TIE95 0x3D534
int16_t paiorder_updatecourseorder(void) {
	_manvrfunctionptr = _manvrfunctionptrs[craftptr->mode_byte];
	return _manvrfunctionptr();
}

/* ======================================================================
 *                   Slot 2 — underattackorder
 *
 * Detect incoming threats (missiles in [NUM_CRAFTS, WARHEAD_SLOT_END),
 * fighters in [0, NUM_CRAFTS)) and pick a reactive maneuver. Only runs
 * for FIGHTER/TRANSPORT while we're already in the plan maneuver. See
 * the dispatch description in the IDA comment.
 * ====================================================================== */

// FUNCTION: TIE95 0x3D554
int16_t paiorder_underattackorder(void) {
	uint16_t a_ref = ai.active_obj_idx;
	uint8_t genus = objects[a_ref].genus;

	uint16_t attacker;
	uint16_t my_speed;
	uint8_t bucket;
	uint16_t att_speed;
	uint16_t rnd16;
	uint8_t new_mv;

	if ((genus != GENUS_FIGHTER && genus != GENUS_TRANSPORT) || craftptr->mode_byte != ai.plan_order)
		return 0;

	/* ---- Acquire attacker ---------------------------------------- */
	if (craftptr->attacker_idx == 0xFF) {
		/* Missile scan. */
		int32_t miss_range = (int32_t)noticemissileranges[(uint16_t)ai.skill_tier];
		uint16_t miss_i;
		int32_t plane_range;

		for (miss_i = NUM_CRAFTS; miss_i < WARHEAD_SLOT_END; ++miss_i) {
			CraftData* mc;
			int32_t eff_range;

			if (!objects[miss_i].ship_idx)
				continue;
			mc = objects[miss_i].craft_ptr;
			if (!mc->species_idx)
				continue;
			if (mc->missile_target != a_ref)
				continue;
			eff_range = (objects[miss_i].ship_idx == 144) ? 3 * miss_range : miss_range;
			if (pai_roughproximitycheck(miss_i, eff_range) == 1) {
				craftptr->attacker_idx = miss_i;
				pai_distancebetween(a_ref, miss_i);
				craftptr->mode_byte =
					approachtable[(uint16_t)((int16_t)trig2_xyangle - objects[a_ref].heading) >> 13] ? 1 : 24;
				paiman_initmaneuver();
				return 0;
			}
		}

		/* Fighter scan. */
		plane_range = (int32_t)noticeplaneranges[(uint16_t)ai.skill_tier];
		if (craftptr->attacker_idx == 0xFF) {
			uint16_t fj;

			for (fj = 0; fj < NUM_CRAFTS; ++fj) {
				uint16_t heading_delta;
				uint16_t pitch_delta;

				if (fj == pstate.object_idx)
					continue;
				if (!objects[fj].ship_idx)
					continue;
				if (objects[fj].side == objects[a_ref].side)
					continue;
				if (craftptr->flight_flag)
					continue;
				if (objects[fj].genus != GENUS_FIGHTER)
					continue;
				if (pai_roughproximitycheck(fj, plane_range) != 1)
					continue;

				pai_distancebetween(fj, a_ref);
				heading_delta = (uint16_t)((int16_t)trig2_xyangle - objects[fj].heading);
				if (heading_delta >= 0x8000u)
					heading_delta = (uint16_t)-heading_delta;
				pitch_delta = (uint16_t)((int16_t)trig2_zangle - objects[fj].pitch);
				if (pitch_delta >= 0x8000u)
					pitch_delta = (uint16_t)-pitch_delta;
				if (heading_delta < 0x2000u && pitch_delta < 0x2000u) {
					craftptr->attacker_idx = fj;
					break;
				}
			}
		}
	}

	/* ---- Pick a maneuver against the acquired attacker ----------- */
	attacker = craftptr->attacker_idx;
	if (attacker == 0xFF)
		return 0;

	pai_distancebetween(a_ref, attacker);
	my_speed = spec_data[craftptr->species_idx].max_speed;
	bucket = approachtable[(uint16_t)((int16_t)trig2_xyangle - objects[a_ref].heading) >> 13];
	att_speed =
		objects[attacker].category ? 900 : spec_data[objects[attacker].craft_ptr->species_idx].max_speed;
	rnd16 = (uint16_t)math2_getrandom();

	if (bucket == 1) {
		new_mv = (trig2_polardistance < 0x2000 && rnd16 < 0x4000u) ? (uint8_t)1 : sidemaneuvers[rnd16 & 3];
	} else if (bucket) { /* bucket == 2, on tail */
		new_mv =
			(my_speed < att_speed || trig2_polardistance <= 0x8000) ? sternmaneuvers[rnd16 & 7] : (uint8_t)16;
	} else { /* bucket == 0, head-on */
		new_mv = (rnd16 <= 0x8000u) ? sidemaneuvers[rnd16 & 3] : (uint8_t)9;
	}
	craftptr->mode_byte = new_mv;
	paiman_initmaneuver();
	return 0;
}

/* ======================================================================
 *                   Slot 3 — stillattackorder
 *
 * If we drifted off the plan maneuver, verify the recorded attacker is
 * still a threat; otherwise clear and transition.
 * ====================================================================== */

// FUNCTION: TIE95 0x3D928
int16_t paiorder_stillattackorder(void) {
	uint16_t attacker;

	if (craftptr->mode_byte == ai.plan_order)
		return 0;

	attacker = craftptr->attacker_idx;
	if (attacker == 0xFF)
		return 0;

	if (attacker < NUM_CRAFTS) {
		if (!pai_roughproximitycheck(attacker, planeranges[(uint16_t)ai.skill_tier])) {
			craftptr->attacker_idx = 0xFF;
			return 1;
		}
	} else {
		/* Missile / non-craft attacker. */
		if (!objects[attacker].ship_idx || objects[attacker].craft_ptr->missile_target != ai.active_obj_idx) {
			craftptr->attacker_idx = 0xFF;
			return 1;
		}
	}
	return 0;
}

/* ======================================================================
 *                   Slot 4 — flyhomeorder
 *
 * Find a mothership / mission waypoint; fly there in a formation slot
 * driven by the mother's engine triplet. Return 1 only when polar
 * distance < 0x800 (final landing trigger).
 * ====================================================================== */

// FUNCTION: TIE95 0x3D9D8
int16_t paiorder_flyhomeorder(void) {
	uint16_t mother;
	EFGStruct* g;
	uint8_t mom_species;
	SpecData* ms;

	craftptr->formation_separation = 1;

	/* Mother lookup — same priority chain as enterhangarorder. */

	g = &fg_array[ai.fg_idx];
	if (craftptr->dock_state_flags) {
		mother = pai_searchformother(g->capture_fg);
	} else {
		mother = pai_searchformother(g->pri_stop_fg);
		if (mother == 0xFFFFu)
			mother = pai_searchformother(g->sec_stop_fg);
	}

	/* Don't fly home to ourselves. */
	if (mother != 0xFFFFu && objects[mother].fg_idx == objects[pstate.object_idx].fg_idx)
		mother = 0xFFFFu;

	if (mother == 0xFFFFu) {
		craftptr->ai_target_ref = g->way_used[13] ? (int16_t)0x800D : (int16_t)0x8000;
		pai_settarget();
		return 0;
	}

	/* Formation slot = mother + rotate(engine_x, engine_y, engine_z). */
	craftptr->ai_target_ref = (int16_t)mother;
	mom_species = objects[mother].craft_ptr->species_idx;
	ms = &spec_data[mom_species];
	pai_calcrotatedpoint(&objects[mother], ms->engine_x, /* side */
						 ms->engine_y,                   /* up   */
						 ms->engine_z /* fwd  */);
	craftptr->waypoint_x_cache = rotatedx + objects[mother].world_x;
	craftptr->waypoint_y_cache = rotatedy + objects[mother].world_y;
	craftptr->waypoint_z_cache = rotatedz + objects[mother].world_z;

	pai_targetdistance();

	if (trig2_polardistance < 0x10000)
		paiman_setspeed(ai.active_obj_idx, 0x96);
	if (trig2_polardistance < 0x8000)
		paiman_setspeed(ai.active_obj_idx, 0x64);
	if (trig2_polardistance < 0x4000)
		paiman_setspeed(ai.active_obj_idx, 0x4B);
	if (trig2_polardistance < 0x800)
		return 1;
	return 0;
}

/* ======================================================================
 *                   Slot 10 — waitrunorder
 * ====================================================================== */

// FUNCTION: TIE95 0x3DF88
int16_t paiorder_waitrunorder(void) {
	int32_t radius;

	if (craftptr->mode_byte != ai.plan_order)
		return 0;

	/* Proximity radius = skill_value + 2.0 (24.8 fixed) against ai_target_ref. */
	radius = (int32_t)craftptr->skill_value + 0x20000;
	if (pai_roughproximitycheck((uint16_t)craftptr->ai_target_ref, radius) != 1)
		return 0;
	return 1;
}

/* ======================================================================
 *                   Slot 11 — breakofforder
 * ====================================================================== */

// FUNCTION: TIE95 0x3DFD0
int16_t paiorder_breakofforder(void) {
	uint16_t link = (uint16_t)craftptr->ai_target_ref;

	/* Player's radio-target wingman order: drop target. */
	if (objects[ai.active_obj_idx].fg_idx == objects[pstate.object_idx].fg_idx &&
		link == (uint16_t)pstate.radio_target) {
		craftptr->ai_target_ref = (int16_t)0xFF;
		return 1;
	}

	if (!pai_worthytarget(link)) {
		craftptr->ai_target_ref = (int16_t)0xFF;
		return 1;
	}

	/* LEADER_GO_HOME (default_order_ldr 19): break off if target craft
	 * is in flight range and has no subsystem damage. */
	if (craftptr->default_order_ldr == 19 && link < NUM_CRAFTS && !objects[link].craft_ptr->status_flags)
		return 1;
	return 0;
}

/* ======================================================================
 *                   Slot 12 — leaderdeadorder
 *
 * Detect a lost leader and redistribute wingmen.
 * ====================================================================== */

// FUNCTION: TIE95 0x3E37C
int16_t paiorder_leaderdeadorder(void) {
	uint8_t leader_idx = craftptr->leader_obj_idx;
	CraftData* leader;
	int leader_gone;
	uint8_t new_leader;
	uint16_t i;

	if (leader_idx == 0xFF)
		return 0;
	if (leader_idx >= NUM_CRAFTS)
		return 0;

	leader = objects[leader_idx].craft_ptr;
	leader_gone = (objects[leader_idx].ship_idx == 0);
	if (leader->hull_damage >= leader->hull_strength)
		leader_gone = 1;
	if (leader->flight_flag == 3 || leader->flight_flag == 4)
		leader_gone = 1;

	if (!leader_gone) {
		if (ai.leader_craft->current_order == 49)
			craftptr->ai_update_rate = 59;
		return 0;
	}

	/* Promote: if the player slot is in our fg and is not the old leader,
	 * pick it; otherwise the active craft promotes itself. */

	if (objects[pstate.object_idx].fg_idx == objects[ai.active_obj_idx].fg_idx) {
		new_leader =
			(leader_idx == pstate.object_idx) ? (uint8_t)ai.active_obj_idx : (uint8_t)pstate.object_idx;
	} else {
		new_leader = (uint8_t)ai.active_obj_idx;
	}

	for (i = 0; i < NUM_CRAFTS; ++i) {
		CraftData* wing;

		if (!objects[i].ship_idx)
			continue;
		wing = objects[i].craft_ptr;
		if (wing->leader_obj_idx != leader_idx)
			continue;

		if (i == ai.active_obj_idx) {
			/* We promote: inherit leader's formation slot / waypoint. */
			wing->leader_obj_idx = 0xFF;
			wing->formation_separation = leader->formation_separation;
			wing->active_waypoint_idx = leader->active_waypoint_idx;
			wing->ai_target_ref = leader->ai_target_ref;
			pai_settarget();
		} else {
			wing->leader_obj_idx = new_leader;
		}
	}
	return 1;
}

/* ======================================================================
 *                   Slot 15 — abortatkorder
 *
 * Evaluate fg.stop_abort to decide whether to break off the attack.
 * ====================================================================== */

// FUNCTION: TIE95 0x3E0BC
int16_t paiorder_abortatkorder(void) {
	uint16_t active_idx = ai.active_obj_idx;

	uint16_t my_fg;
	int aborted;
	uint16_t t75;

	if (objects[active_idx].genus == GENUS_PLATFORM)
		return 0;

	my_fg = ai.fg_idx;
	craftptr->special_order_flag = 1;

	aborted = -1; /* -1 = fall through to default LABEL_28 */
	switch (fg_array[my_fg].stop_abort) {
		case 1: /* DAMAGED */
			if (craftptr->status_flags & 1) {
				uint8_t g = objects[active_idx].genus;
				if ((g == 3 || g == 4) && (craftptr->forward_shield + craftptr->rear_shield) == 0) {
					aborted = 1;
				}
				/* else fall through to LABEL_28 */
			} else {
				aborted = 1;
			}
			break;
		case 2: /* SUBSYSTEM_LOST */
			if (!(craftptr->status_flags & 0x10))
				aborted = 1;
			break;
		case 3: /* OUT_OF_AMMO */
			if (craftptr->status_flags & 8) {
				int has_ammo = 0;
				uint16_t grp;

				for (grp = 0; grp < craftptr->missile_group_cnt; ++grp) {
					const SpecData* sd;
					uint8_t cur;
					uint8_t end;

					if (!craftptr->warhead_type[grp])
						continue;
					sd = &spec_data[craftptr->species_idx];
					cur = sd->missile_start[grp];
					end = sd->missile_end[grp];
					for (; cur <= end; ++cur) {
						if (craftptr->weapon_slots[cur].ammo) {
							has_ammo = 1;
							break;
						}
					}
					if (has_ammo)
						break;
				}
				if (!has_ammo)
					aborted = 1;
			} else {
				aborted = 1;
			}
			break;
		case 4: /* HALF_HULL_DAMAGE */
			if (math2_fraction(craftptr->hull_max, 0x8000u) <= craftptr->hull_damage)
				aborted = 1;
			break;
		case 5: /* FIRST_HIT */
			if (craftptr->was_hit_flag)
				aborted = 1;
			break;
		default: /* stop_abort == 0 or out of range */
			break;
	}

	if (aborted == 1)
		return 1;

	/* LABEL_28 default path — stop_abort didn't match a trigger. */
	craftptr->special_order_flag = 0;
	if (objects[active_idx].genus)
		return 0;
	if ((craftptr->forward_shield + craftptr->rear_shield) != 0)
		return 0;

	/* Fighter with empty shield/cargo slot: retreat when hull_damage
	 * is >= 75% of hull_max. Preserved verbatim from the binary
	 * (asm `cmp ax, bx; jbe` where ax = 75% threshold, bx = hull_damage). */
	t75 = math2_fraction(craftptr->hull_max, 0xC000u);
	if (t75 <= craftptr->hull_damage) {
		craftptr->special_order_flag = 1;
		return 1;
	}
	return 0;
}

/* ======================================================================
 *                   Slot 16 — ontailorder
 * ====================================================================== */

// FUNCTION: TIE95 0x3E514
int16_t paiorder_ontailorder(void) {
	uint16_t attacker;
	uint16_t rnd;
	uint8_t new_mv;

	if (craftptr->mode_byte != ai.plan_order)
		return 0;

	attacker = craftptr->attacker_idx;
	if (attacker == 0xFF)
		return 0;

	pai_distancebetween(ai.active_obj_idx, attacker);
	if (approachtable[(uint16_t)((int16_t)trig2_xyangle - objects[ai.active_obj_idx].heading) >> 13] != 2)
		return 0; /* not on tail */

	rnd = (uint16_t)((uint8_t)math2_getrandom() & 3);

	switch (rnd) {
		case 0:
			new_mv = 1;
			break; /* turn_inside */
		case 1:
			new_mv = 13;
			break; /* zoom */
		case 2:
			new_mv = 4;
			break; /* scissors */
		default:
			new_mv = 14;
			break; /* zoom variant */
	}
	craftptr->mode_byte = new_mv;
	paiman_initmaneuver();
	return 0;
}

/* ======================================================================
 *                   Slot 17 — alwaysorder
 * ====================================================================== */

// FUNCTION: TIE95 0x3E5D0
int16_t paiorder_alwaysorder(void) { return 1; }

/* ======================================================================
 *                   Slot 19 — leadergohomeorder
 *
 * Plan bytecode dispatches on current_order==45 => flyhomeplan. */

// FUNCTION: TIE95 0x3E5D8
int16_t paiorder_leadergohomeorder(void) {
	/* 45 = flyhome, 47 = hyperspace-home — both count as "leader is on
	 * the way out" so wingmen know to follow the home leg. */
	uint8_t order = ai.leader_craft->current_order;
	return (order == 45 || order == 47) ? 1 : 0;
}

/* ======================================================================
 *                   Slot 20 — hyperspaceorder
 * ====================================================================== */

// FUNCTION: TIE95 0x3E5F8
int16_t paiorder_hyperspaceorder(void) {
	EFGStruct* g = &fg_array[ai.fg_idx];
	SpecData* sp = &spec_data[craftptr->species_idx];

	/* Leader-driven path. */
	if (craftptr->leader_obj_idx != 0xFF && !craftptr->special_order_flag && craftptr->current_order != 47) {

		int hyper_ok = 0;
		if (sp->has_hyperdrive) {
			uint8_t blocker = craftptr->dock_state_flags ? g->capture_fg_used : g->pri_stop_fg_used;
			if (!blocker)
				hyper_ok = 1;
		}

		if (hyper_ok && ai.leader_craft->flight_flag == 5) {
			/* Latch our own hyper state. */
			craftptr->current_order = 51;
			craftptr->flight_flag = 5;
			craftptr->ai_roll_state = 0;
			craftptr->ai_pitch_state = 0;
			craftptr->ai_heading_state = 0;
			craftptr->mode_byte = 21;
			craftptr->push_accum_x = 0;
			craftptr->mode_subbyte = 1;
			craftptr->ai_plan_state = 944;
			craftptr->maneuver_timer = 2360;
			craftptr->push_accum_y = craftptr->push_accum_x;
			craftptr->push_accum_z = craftptr->push_accum_x;
			paiman_setpower(0xFFFFu);
		}
		return 0;
	}

	/* Leaderless / special-order path. */
	if (sp->has_hyperdrive) {
		uint8_t blocker = craftptr->dock_state_flags ? g->capture_fg_used : g->pri_stop_fg_used;
		if (!blocker)
			return 1;
	}
	return 0;
}

/* ======================================================================
 *                   Slot 21 — enterhangarorder
 * ====================================================================== */

// FUNCTION: TIE95 0x3DC18
int16_t paiorder_enterhangarorder(void) {
	uint16_t mother;
	EFGStruct* g;
	int16_t ms_speed;
	uint16_t approach_speed;

	craftptr->formation_separation = 1;
	craftptr->ai_update_rate = 59;

	/* Mother lookup — like flyhomeorder, but retail gates the sec_stop_fg
	 * fallback on sec_stop_fg_used (avoids matching FG #0 when sec is
	 * unconfigured and defaulted to 0). */

	g = &fg_array[ai.fg_idx];
	if (craftptr->dock_state_flags) {
		mother = pai_searchformother(g->capture_fg);
	} else {
		mother = pai_searchformother(g->pri_stop_fg);
		if (mother == 0xFFFFu && g->sec_stop_fg_used)
			mother = pai_searchformother(g->sec_stop_fg);
	}

	if (mother != 0xFFFFu) {
		uint8_t mom_species;
		SpecData* ms;

		craftptr->ai_target_ref = (int16_t)mother;
		mom_species = objects[mother].craft_ptr->species_idx;
		ms = &spec_data[mom_species];
		/* (side, up, fwd) = (cockpit_x, cockpit_y, cockpit_z) — bay
		 * offset. Same triplet as collide.c's tractor-dock routine. */
		pai_calcrotatedpoint(&objects[mother], ms->cockpit_x, ms->cockpit_y, ms->cockpit_z);
		craftptr->waypoint_x_cache = rotatedx + objects[mother].world_x;
		craftptr->waypoint_y_cache = rotatedy + objects[mother].world_y;
		craftptr->waypoint_z_cache = rotatedz + objects[mother].world_z;
	}

	pai_targetdistance();

	/* No mother: set a slow drift speed and tell the planner the order
	 * is done (return 1 → advance to next plan node). Skip despawn —
	 * trig2_polardistance is stale without a waypoint update. */
	if (mother == 0xFFFFu) {
		paiman_setspeed(ai.active_obj_idx, 35);
		return 1;
	}

	/* Approach speed: drift at 40 if mother is nearly stopped, else
	 * trail at mother_speed + 25. */
	ms_speed = objects[mother].current_speed;
	approach_speed = (ms_speed < 25) ? (uint16_t)40 : (uint16_t)(ms_speed + 25);
	paiman_setspeed(ai.active_obj_idx, approach_speed);

	/* Docked: despawn friends in same fg that followed us in, then self,
	 * then any linked craft (tow_slave_ref). Return 0 either way — retail
	 * never advances this order while a mother exists; the despawned
	 * slot ends the order naturally. */
	if (trig2_polardistance < 0x200) {
		uint16_t fi;
		uint16_t link3E;

		for (fi = 0; fi < NUM_CRAFTS; ++fi) {
			CraftData* friend;

			if (!objects[fi].ship_idx)
				continue;
			if (objects[fi].fg_idx != ai.fg_idx)
				continue;
			friend = objects[fi].craft_ptr;
			if (friend->leader_obj_idx == 0xFF)
				continue;
			if (friend->mode_byte != 10)
				continue;
			msg_craftmessage(fi, friend, 0x6D);
			score_craftexitscoring(fi, ai.fg_idx, 4);
			objects[fi].ship_idx = 0;
		}
		msg_craftmessage(ai.active_obj_idx, craftptr, 0x6D);
		score_craftexitscoring(ai.active_obj_idx, ai.fg_idx, 4);
		objects[ai.active_obj_idx].ship_idx = 0;

		link3E = (uint16_t)craftptr->tow_slave_ref;
		if (link3E != 0xFFFFu && link3E < 0x3800u) {
			uint8_t linked_fg = objects[link3E].fg_idx;
			CraftData* linked;

			score_craftexitscoring(link3E, linked_fg, 7);
			linked = objects[link3E].craft_ptr;
			++fgstatus[linked_fg].cond[2].count;
			if (fg_array[linked_fg].special_craft == linked->craft_idx_in_fg)
				fgstatus[linked_fg].cond_id[2].count = 1;
			objects[link3E].ship_idx = 0;
		}
	}
	return 0;
}

/* ======================================================================
 *                   Slot 22 — mothershiporder
 * ====================================================================== */

// FUNCTION: TIE95 0x3E798
int16_t paiorder_mothershiporder(void) {
	if (objects[ai.active_obj_idx].genus == GENUS_PLATFORM)
		return 0;

	if (craftptr->leader_obj_idx == 0xFF) {
		return ((uint16_t)craftptr->ai_target_ref == 0x800Du) ? 1 : 0;
	}
	return ((uint16_t)ai.leader_craft->ai_target_ref == 0x800Du) ? 1 : 0;
}

/* ======================================================================
 *                   Slot 24 — lookfordisableorder
 * ====================================================================== */

// FUNCTION: TIE95 0x3E80C
int16_t paiorder_lookfordisableorder(void) {
	uint16_t cached = (uint16_t)craftptr->pending_radio_command;
	uint16_t t;

	if (cached != 0xFF && cached != 0xFB) {
		if (pai_worthytarget(cached)) {
			craftptr->ai_target_ref = (int16_t)cached;
			return 1;
		}
		craftptr->pending_radio_command = 0xFF;
	}

	t = pai_checkfortargetstodisable(ai.ai_entry_count);
	if (t == 0xFFFFu)
		return 0;
	craftptr->ai_target_ref = (int16_t)t;
	return 1;
}

/* ======================================================================
 *                   Slot 25 — abortboardorder
 * ====================================================================== */

// FUNCTION: TIE95 0x3E884
int16_t paiorder_abortboardorder(void) {
	uint16_t target;
	int do_abort;

	if (craftptr->mode_subbyte >= 3)
		return 0;

	target = (uint16_t)craftptr->ai_target_ref;
	do_abort = 0;

	if (target < 0x3800u) {
		if (!objects[target].ship_idx)
			do_abort = 1;
		/* Category 5 is the "can't-be-boarded" flag (platform-ish);
		 * not matching it falls through to the status-flag gate. */
		if (objects[target].category == 5)
			do_abort = 1;
	} else {
		if (!staticobjects[target - 0x3800u].species)
			do_abort = 1;
	}
	if (!craftptr->status_flags)
		do_abort = 1;

	if (!do_abort)
		return 0;

	craftptr->push_accum_z = 0;
	craftptr->ai_target_ref = (int16_t)0x8000;
	craftptr->push_accum_y = craftptr->push_accum_z;
	craftptr->push_accum_x = craftptr->push_accum_z;
	pai_settarget();
	return 1;
}

/* ======================================================================
 *                   Slot 26 — returnboardorder
 * ====================================================================== */

// FUNCTION: TIE95 0x3E960
// FUNCTION: TIE98 0x4621B0
int16_t paiorder_returnboardorder(void) {
	pai_targetdistance();
	return (trig2_polardistance < 0x4000) ? 1 : 0; /* retail: 16384, demo had 4096 */
}

/* ======================================================================
 *                   Slot 27 — awaitboardorder
 *
 * Tick the in-progress capture; latch status_flags once the capture
 * threshold is reached and emit the 'capture complete' radio line.
 * ====================================================================== */

// FUNCTION: TIE95 0x3E978
int16_t paiorder_awaitboardorder(void) {
	uint16_t ai_entry;
	uint16_t fg_idx;
	EFGStruct* g;
	uint8_t capture_goal;

	if (craftptr->boarding_state != 2)
		return 0;

	ai_entry = ai.ai_entry_count;
	fg_idx = ai.fg_idx;
	g = &fg_array[fg_idx];

	++craftptr->board_count;
	++craftptr->ai_goal_progress[ai_entry];

	capture_goal = g->ai[ai_entry].var[0];

	if (craftptr->board_count < capture_goal) {
		/* Clear the one-shot handshake; PAIMAN_boardmaneuver will
		 * re-raise it next tick while docking continues. */
		craftptr->boarding_state = 0;
		return 0;
	}

	craftptr->status_flags = craftptr->subsystem_active;

	if (craftptr->board_count == capture_goal && craftptr->default_order_ldr == 42) {
		msg_craftmessage(ai.active_obj_idx, craftptr, 100);
	}
	return 0;
}

/* ======================================================================
 *                   Slot 28 — makedisabledorder
 * ====================================================================== */

// FUNCTION: TIE95 0x3EA38
int16_t paiorder_makedisabledorder(void) {
	craftptr->status_flags = 0;
	return 0;
}

/* ======================================================================
 *                   Slot 29 — neartargetorder
 * ====================================================================== */

/* TIE95 has no separate body: its order table points slot 29 at
 * paiorder_returnboardorder's identical code at 0x3E960. */
// FUNCTION: TIE98 0x4622C0
int16_t paiorder_neartargetorder(void) {
	pai_targetdistance();
	return (trig2_polardistance < 0x4000) ? 1 : 0;
}

/* ======================================================================
 *                   Slot 30 — rocketsonboardorder
 *
 * Do we have a loaded warhead matching the target's required class and
 * any ammo for it?  Target class: heavy (2) for freighter / starship /
 * platform (genus 3/4/5); light (1) otherwise. Matching uses retail
 * byte_C5463[warhead_type] (indexed by weapon species 137..154).
 * ====================================================================== */

// FUNCTION: TIE95 0x3EA4C
int16_t paiorder_rocketsonboardorder(void) {
	/* ai_target_ref is only an objects[] index when it points at a live
	 * craft slot (< NUM_CRAFTS). Out-of-range values land in warhead
	 * slots or staticobjects, whose layouts share no .genus field, so
	 * fall back to "light" (1) instead of dereferencing garbage. */
	uint16_t needed = 1;
	uint16_t grp;

	if ((uint16_t)craftptr->ai_target_ref < NUM_CRAFTS) {
		uint8_t tgt_genus = objects[(uint16_t)craftptr->ai_target_ref].genus;
		if (tgt_genus == GENUS_FREIGHTER || tgt_genus == GENUS_PLATFORM || tgt_genus == GENUS_STARSHIP)
			needed = 2;
	}

	for (grp = 0; grp < craftptr->missile_group_cnt; ++grp) {
		uint8_t wt = craftptr->warhead_type[grp];
		uint8_t cls = projectile_is_warhead_type[wt - WEAPON_SPECIES_BASE];
		const SpecData* sd;
		uint8_t cur;
		uint8_t end;

		if (cls != needed)
			continue;

		sd = &spec_data[craftptr->species_idx];
		cur = sd->missile_start[grp];
		end = sd->missile_end[grp];
		for (; cur <= end; ++cur) {
			if (craftptr->weapon_slots[cur].ammo)
				return 1;
		}
	}
	return 0;
}

/* ======================================================================
 *                   Slot 31 — avoidhitorder
 *
 * Evasive pattern: acquire threat (same scan as underattackorder) then
 * drive push_accum_* with random per-axis jink.
 * ====================================================================== */

// FUNCTION: TIE95 0x3EB38
int16_t paiorder_avoidhitorder(void) {
	uint16_t active = ai.active_obj_idx;
	uint8_t genus = objects[active].genus;

	uint16_t speed_pct;
	uint16_t random_magnitude;
	int32_t push;

	if (genus == 3 || genus == 4 || craftptr->mode_byte != ai.plan_order)
		return 0;

	/* ---- Threat acquisition -------------------------------------- */
	if (craftptr->attacker_idx == 0xFF) {
		int32_t miss_range = (int32_t)noticemissileranges[(uint16_t)ai.skill_tier];
		uint16_t mi;
		uint8_t mode_byte;
		int32_t plane_range;
		uint8_t enemy_side;
		uint16_t fj;

		for (mi = NUM_CRAFTS; mi < WARHEAD_SLOT_END; ++mi) {
			CraftData* mc;
			int32_t eff;

			if (!objects[mi].ship_idx)
				continue;
			mc = objects[mi].craft_ptr;
			if (!mc->species_idx)
				continue;
			if (mc->missile_target != active)
				continue;
			eff = (objects[mi].ship_idx == 144) ? 3 * miss_range : miss_range;
			if (pai_roughproximitycheck(mi, eff) == 1) {
				craftptr->attacker_idx = mi;
				pai_distancebetween(active, mi);
				craftptr->mode_byte =
					approachtable[(uint16_t)((int16_t)trig2_xyangle - objects[active].heading) >> 13] ? 1
																									  : 24;
				paiman_initmaneuver();
				return 0;
			}
		}

		mode_byte = craftptr->mode_byte;
		if (mode_byte == 12 || mode_byte == 23) {
			/* Scissors/evasive already — only jink at long range. */
			pai_targetdistance();
			if (trig2_polardistance < 0x6000)
				return 0;
		} else {

			plane_range = (int32_t)noticeplaneranges[(uint16_t)ai.skill_tier];
			enemy_side = objects[active].side ^ 1;

			for (fj = 0; fj < NUM_CRAFTS; ++fj) {
				uint16_t heading_delta;
				uint16_t pitch_delta;

				if (!objects[fj].ship_idx)
					continue;
				if (enemy_side != objects[fj].side)
					continue;
				if (craftptr->flight_flag)
					continue;
				if (objects[fj].genus != GENUS_FIGHTER)
					continue;
				if (pai_roughproximitycheck(fj, plane_range) != 1)
					continue;

				pai_distancebetween(fj, active);
				heading_delta = (uint16_t)((int16_t)trig2_xyangle - objects[fj].heading);
				if (heading_delta >= 0x8000u)
					heading_delta = (uint16_t)-heading_delta;
				pitch_delta = (uint16_t)((int16_t)trig2_zangle - objects[fj].pitch);
				if (pitch_delta >= 0x8000u)
					pitch_delta = (uint16_t)-pitch_delta;
				if (heading_delta < 0x2000u && pitch_delta < 0x2000u) {
					craftptr->attacker_idx = fj;
					break;
				}
			}
		}
	}
	/* Retail gates jink on (attacker_idx != 0xFF && active.genus != 2) —
	 * utility craft (tugs) don't jink even when an attacker is locked. */
	if (craftptr->attacker_idx == 0xFF)
		return 0;
	if (objects[active].genus == GENUS_UTILITY)
		return 0;

	speed_pct = math2_percentage(objects[active].current_speed, craftptr->max_speed_cache);

	random_magnitude = (uint16_t)((uint8_t)math2_getrandom() | 0x100);
	push = math2_fraction(random_magnitude, speed_pct);
	if (((uint16_t)math2_getrandom() & 0x8000u) != 0)
		push = -push;
	craftptr->push_accum_x = (int16_t)push;

	random_magnitude = (uint16_t)((uint8_t)math2_getrandom() | 0x100);
	push = math2_fraction(random_magnitude, speed_pct);
	if (((uint16_t)math2_getrandom() & 0x8000u) != 0)
		push = -push;
	craftptr->push_accum_y = (int16_t)push;

	random_magnitude = (uint16_t)((uint8_t)math2_getrandom() | 0x100);
	push = math2_fraction(random_magnitude, speed_pct);
	if (((uint16_t)math2_getrandom() & 0x8000u) != 0)
		push = -push;
	craftptr->push_accum_z = (int16_t)push;
	return 0;
}

/* ======================================================================
 *                   Slot 32 — waitforkidsorder
 *
 * Wait for every child FG (whose pri_stop_fg points at us) to fully
 * finish: active, no waves remaining, no alive objects with that fg_idx.
 * ====================================================================== */

// FUNCTION: TIE95 0x3EF24
int16_t paiorder_waitforkidsorder(void) {
	uint16_t fi;

	for (fi = 0; fi < (uint16_t)mission_file_header.num_fg; ++fi) {
		EFGStruct* g = &fg_array[fi];
		uint16_t oi;

		if (!(diffmask[mission.difficulty] & fgdiffmask[g->difficulty]))
			continue;
		if (fi == ai.fg_idx)
			continue;
		if (!g->pri_stop_fg_used)
			continue;
		if (g->pri_stop_fg != (uint8_t)ai.fg_idx)
			continue;

		if (!fgstatus[fi].active || fgstatus[fi].waves_remaining)
			return 0;

		for (oi = 0; oi < NUM_CRAFTS; ++oi) {
			if (objects[oi].ship_idx && objects[oi].fg_idx == fi)
				return 0;
		}
	}
	return 1;
}

/* ======================================================================
 *                   Slot 33 — waitforallcreateorder
 *
 * Wait until every FG whose start_fg points at us has at least started
 * AND finished spawning all its waves.
 * ====================================================================== */

// FUNCTION: TIE95 0x3F000
int16_t paiorder_waitforallcreateorder(void) {
	uint16_t fi;

	for (fi = 0; fi < (uint16_t)mission_file_header.num_fg; ++fi) {
		EFGStruct* g = &fg_array[fi];
		if (!(diffmask[mission.difficulty] & fgdiffmask[g->difficulty]))
			continue;
		if (fi == ai.fg_idx)
			continue;
		if (!g->start_fg_used)
			continue;
		if (g->start_fg != (uint8_t)ai.fg_idx)
			continue;

		if (!fgstatus[fi].active || fgstatus[fi].waves_remaining)
			return 0;
	}
	return 1;
}

/* ======================================================================
 *                   Slot 34 — evasiveorder
 * ====================================================================== */

// FUNCTION: TIE95 0x3F0A0
int16_t paiorder_evasiveorder(void) {
	if (craftptr->mode_byte == ai.plan_order && (uint16_t)craftptr->pending_radio_command == 0xFB) {
		craftptr->ai_target_ref = (int16_t)0xFF;
		craftptr->pending_radio_command = 0xFF;
		return 1;
	}
	return 0;
}

/* ======================================================================
 *                   Slot 35 — newtargetorder (player FG only)
 * ====================================================================== */

// FUNCTION: TIE95 0x3F0E0
int16_t paiorder_newtargetorder(void) {
	uint16_t cached;
	int16_t cached_s;

	if (ai.fg_idx != pstate.player->fg_idx)
		return 0;

	cached = (uint16_t)craftptr->pending_radio_command;
	if (cached == 0xFF || cached == 0xFB)
		return 0;

	if (!pai_worthytarget(cached)) {
		craftptr->pending_radio_command = 0xFF;
		return 0;
	}

	cached_s = (int16_t)(uint8_t)craftptr->pending_radio_command;
	if (cached_s != craftptr->ai_target_ref) {
		craftptr->ai_target_ref = cached_s;
	}
	return 0;
}

/* ======================================================================
 *                   Slot 36 — avoidstarshiporder
 * ====================================================================== */

// FUNCTION: TIE95 0x3F148
int16_t paiorder_avoidstarshiporder(void) {
	/* Save the original craftptr so we can write back to OUR CraftData
	 * after the COLLIDE call. collide_craftstarshipcollision indirects
	 * through starship_checkstarshiphit (and STARSHIP_checkboxcollision)
	 * which clobber the global craftptr to the *target* craft's data —
	 * critically including our own FG's leader when the leader is a
	 * freighter/starship/platform genus and is enumerated as a candidate
	 * threat. The binary at PAIORDER_avoidstarshiporder+0x36 stashes
	 * craftptr in `v0` and restores it via `craftptr = v0;` immediately
	 * after the COLLIDE call. Without this, the `craftptr->mode_byte = 28`
	 * write further down corrupts whichever craft starship_checkstarshiphit
	 * last touched. */
	CraftData* saved_craftptr = craftptr;

	uint8_t saved_mode_byte;
	uint16_t threat;
	uint8_t mode_byte;
	uint16_t new_heading;
	uint16_t new_pitch;

	if (craftptr->mode_byte == 28) {
		return (craftptr->ai_plan_state == 0) ? 1 : 0;
	}
	saved_mode_byte = craftptr->mode_byte;

	threat = collide_craftstarshipcollision(ai.active_obj_idx, 6);
	craftptr = saved_craftptr; /* mirror binary's `craftptr = v0;` */
	if (threat == 0xFFFFu)
		return 0;

	/* Keep engaging target if we're already in scissors/evasive and the
	 * 'threat' is our intended target and it's not one of the dock-hull
	 * ship_idx values (49, 53). */
	mode_byte = saved_mode_byte;
	if ((uint16_t)craftptr->ai_target_ref == threat && (mode_byte == 12 || mode_byte == 23)) {
		uint8_t tship = objects[threat].ship_idx;
		if (tship != 49 && tship != 53)
			return 0;
	}

	if (threat == ai.active_obj_idx)
		return 0;
	if (threat == (uint16_t)craftptr->tow_slave_ref)
		return 0;

	/* Evasive orientation. craft_idx_in_fg parity picks the side; the
	 * pitch sign flips based on where our current target pitch sits. */
	new_heading = (craftptr->craft_idx_in_fg & 1) ? (uint16_t)(objects[ai.active_obj_idx].heading + 0x4000)
												  : (uint16_t)(objects[ai.active_obj_idx].heading - 0x4000);
	craftptr->ai_target_heading = new_heading;

	new_pitch = (craftptr->ai_target_pitch <= 0x4000u)
					? (uint16_t)(objects[ai.active_obj_idx].pitch + 0x4000)
					: (uint16_t)(objects[ai.active_obj_idx].pitch - 0x4000);
	craftptr->ai_target_pitch = new_pitch;

	craftptr->mode_byte = 28;
	paiman_initmaneuver();
	return 0;
}

/* ======================================================================
 *                   Slot 37 — checkhyperorder
 * ====================================================================== */

// FUNCTION: TIE95 0x3F2D4
int16_t paiorder_checkhyperorder(void) { return fg_array[ai.fg_idx].pri_stop_fg_used ? 1 : 0; }

/* ======================================================================
 *                   Slot 38 — stopgohomeorder
 *
 * Return 1 when our FG's stop criterion has fired (clock AND/OR goal).
 * ====================================================================== */

// FUNCTION: TIE95 0x3F2FC
int16_t paiorder_stopgohomeorder(void) {
	EFGStruct* g;
	uint8_t stop_min_b;
	uint8_t stop_sec_b;

	if (objects[ai.active_obj_idx].genus == GENUS_PLATFORM)
		return 0;

	g = &fg_array[ai.fg_idx];
	stop_min_b = g->stop_min;
	stop_sec_b = g->stop_sec;

	if ((stop_min_b + stop_sec_b) != 0) {
		if (_date.minute > stop_min_b)
			return 1;
		if (_date.minute == stop_min_b && _date.second >= stop_sec_b)
			return 1;
	}

	if (g->stop_cond.cond) {
		uint16_t met = (uint8_t)score_checkcondition(g->stop_cond.cond, g->stop_cond.type, g->stop_cond.id,
													 g->stop_cond.pct, 0);
		if (met & 1)
			return 1;
	}
	return 0;
}

/* ======================================================================
 *                   Slot 39 — completegohomeorder
 * ====================================================================== */

// FUNCTION: TIE95 0x3F3F4
int16_t paiorder_completegohomeorder(void) {
	EFGStruct* g;
	uint16_t e;

	if (!pai_aicompletioncheck(craftptr->default_order_ldr, ai.ai_entry_count))
		return 0;

	craftptr->ai_complete_state[ai.ai_entry_count] = 2;

	if (objects[ai.active_obj_idx].genus == GENUS_PLATFORM)
		return 0;

	g = &fg_array[ai.fg_idx];
	for (e = 0; e < 3; ++e) {
		if (g->ai[e].order != 0 && craftptr->ai_complete_state[e] != 2) {
			return 0;
		}
	}
	return 1;
}

/* ======================================================================
 *                   Slot 40 — completegootherorder
 * ====================================================================== */

// FUNCTION: TIE95 0x3F4B8
int16_t paiorder_completegootherorder(void) {
	uint16_t cur_entry = ai.ai_entry_count;
	uint16_t new_entry;
	uint8_t new_order;

	if (craftptr->ai_complete_state[cur_entry] != 2 || cur_entry == 2)
		return 0;

	/* Don't advance into an empty ai[] slot — keeps the craft on its
	 * current order rather than driving past the end of the plan. */
	new_entry = (uint16_t)(cur_entry + 1);
	if (!fg_array[ai.fg_idx].ai[new_entry].order)
		return 0;

	++ai.ai_entry_count;
	craftptr->ai_state_1C = (uint8_t)new_entry;

	new_order = fg_array[ai.fg_idx].ai[new_entry].order;
#ifdef TIE_MODERN
	// HARDENING: orders past the 33-entry tables (retail HI1W.TIE uses 35) take the null plan.
	if (new_order >= sizeof(ordersldr)) {
		craftptr->default_order_ldr = 0;
		ai.staged_next_order = 0;
		return 1;
	}
#endif
	craftptr->default_order_ldr = ordersldr[new_order];
	ai.staged_next_order = (craftptr->leader_obj_idx == 0xFF) ? ordersldr[new_order] : ordersflw[new_order];
	return 1;
}

/* ======================================================================
 *                   Slot 41 — completefolloworder
 * ====================================================================== */

// FUNCTION: TIE95 0x3F788
int16_t paiorder_completefolloworder(void) {
	uint8_t leader_entry = ai.leader_craft->ai_state_1C;
	uint8_t new_order;

	if (ai.ai_entry_count == leader_entry)
		return 0;

	ai.ai_entry_count = leader_entry;
	craftptr->ai_state_1C = leader_entry;

	new_order = fg_array[ai.fg_idx].ai[leader_entry].order;
#ifdef TIE_MODERN
	// HARDENING: orders past the 33-entry tables (retail HI1W.TIE uses 35) take the null plan.
	if (new_order >= sizeof(ordersldr)) {
		craftptr->default_order_ldr = 0;
		ai.staged_next_order = 0;
		return 1;
	}
#endif
	craftptr->default_order_ldr = ordersldr[new_order];
	ai.staged_next_order = (craftptr->leader_obj_idx == 0xFF) ? ordersldr[new_order] : ordersflw[new_order];
	return 1;
}

/* ======================================================================
 *                   Slot 42 — waitgootherorder
 *
 * Scan forward for an ai[] entry whose order is in [7..0x11], then
 * stamp it WITHOUT advancing ai.ai_entry_count.
 * ====================================================================== */

// FUNCTION: TIE95 0x3F59C
int16_t paiorder_waitgootherorder(void) {
	uint16_t entry = ai.ai_entry_count;
	uint8_t picked = 0xFF;

	uint8_t order;

	for (;;) {
		uint8_t order;

		++entry;
		if (entry >= 3)
			break;
		order = fg_array[ai.fg_idx].ai[entry].order;
		if (order >= 7 && order <= 0x11) {
			picked = (uint8_t)entry;
			break;
		}
	}
	if (picked == 0xFF)
		return 0;

	order = fg_array[ai.fg_idx].ai[picked].order;
	craftptr->ai_state_1C = picked;
#ifdef TIE_MODERN
	// HARDENING: orders past the 33-entry tables (retail HI1W.TIE uses 35) take the null plan.
	if (order >= sizeof(ordersldr)) {
		craftptr->default_order_ldr = 0;
		ai.staged_next_order = 0;
		return 1;
	}
#endif
	craftptr->default_order_ldr = ordersldr[order];
	ai.staged_next_order = (craftptr->leader_obj_idx == 0xFF) ? ordersldr[order] : ordersflw[order];
	return 1;
}

/* ======================================================================
 *                   Slot 43 — orderswitchorder
 * ====================================================================== */

// FUNCTION: TIE95 0x3F620
int16_t paiorder_orderswitchorder(void) {
	uint16_t scan;
	int matched;
	uint8_t picked;

	uint8_t order;

	if (!ai.ai_entry_count)
		return 0;

	matched = 0;
	for (scan = 0; scan < ai.ai_entry_count; ++scan) {
		uint8_t order;
		uint8_t mapped;

		if (craftptr->ai_complete_state[scan] == 2)
			continue;

		order = fg_array[ai.fg_idx].ai[scan].order;
#ifdef TIE_MODERN
		// HARDENING: orders past the 33-entry tables (retail HI1W.TIE uses 35) take the null plan.
		mapped = order < sizeof(ordersldr) ? ordersldr[order] : 0;
#else
		mapped = ordersldr[order];
#endif

		if (mapped < 0x1Cu) {
			if (mapped < 7 || (mapped > 9 && mapped != 19))
				continue;
			if (!paifight_scanfortargetswitch(scan))
				continue;
		} else if (mapped > 0x20u) {
			if (mapped < 0x22u || (mapped > 0x22u && mapped != 68))
				continue;
			if (!pai_lookfordisableswitch(scan))
				continue;
		} else {
			continue;
		}
		matched = 1;
		break;
	}
	if (!matched)
		return 0;

	picked = (uint8_t)scan;
	order = fg_array[ai.fg_idx].ai[picked].order;
	craftptr->ai_state_1C = picked;
#ifdef TIE_MODERN
	// HARDENING: orders past the 33-entry tables (retail HI1W.TIE uses 35) take the null plan.
	if (order >= sizeof(ordersldr)) {
		craftptr->default_order_ldr = 0;
		ai.staged_next_order = 0;
		return 1;
	}
#endif
	craftptr->default_order_ldr = ordersldr[order];
	ai.staged_next_order = (craftptr->leader_obj_idx == 0xFF) ? ordersldr[order] : ordersflw[order];
	return 1;
}

/* ======================================================================
 *                   Slot 45 — dropoffdestorder
 *
 * Fly the passenger FG to its drop point and release when close.
 * ====================================================================== */

// FUNCTION: TIE95 0x3F844
int16_t paiorder_dropoffdestorder(void) {
	uint16_t drop_fg = (uint16_t)((int8_t)fg_array[ai.fg_idx].ai[ai.ai_entry_count].var[1] - 1);
	if (fgstatus[drop_fg].cond[0].detail)
		return 0;

	create_getdropposition(drop_fg, 0, 0xFFFF);
	craftptr->waypoint_x_cache = worldlocx;
	craftptr->waypoint_y_cache = worldlocy;
	craftptr->waypoint_z_cache = worldlocz + 932;

	pai_targetdistance();
	if (trig2_polardistance < 0x4000)
		paiman_setpower(0xC000u);
	if (trig2_polardistance < 0x1000)
		paiman_setpower(0x6000u);
	if (trig2_polardistance >= 0x800)
		return 0;

	craftptr->active_waypoint_idx = 0;
	return 1;
}

/* Retail sub_3F934 — the unnamed 47th entry in ordersfunctionptrs (slot 46).
 * Used by flyhomeevadeplan at pair (46, 51) to gate the transition into
 * intohyperspaceplan: return true when this craft is ready + its
 * mothership FG has fully arrived. Species must have a hyperdrive and
 * the craft's own ai_target_ref word must be >= 0x20 (i.e. not a fresh
 * mission-load sentinel). Then one of two checks depending on whether
 * the craft is currently attached to a captor (dock_state_flags != 0):
 *   attached   -> only check capture_fg (count==detail) if capture_fg_used
 *   not-attached -> AND of (pri_stop_fg match if pri_stop_fg_used) and
 *                   (sec_stop_fg match if sec_stop_fg_used). */
// FUNCTION: TIE95 0x3F934
int16_t paiorder_mothershipreadyorder(void) {
	EFGStruct* fg = &fg_array[ai.fg_idx];

	int16_t primary_ok;
	int16_t secondary_ok;

	if ((uint16_t)craftptr->ai_target_ref < 0x20u)
		return 0;
	if (!spec_data[craftptr->species_idx].has_hyperdrive)
		return 0;

	if (craftptr->dock_state_flags) {
		if (fg->capture_fg_used) {
			FGStatus* fs = &fgstatus[fg->capture_fg];
			return fs->cond[0].count == fs->cond[0].detail;
		}
		return 0;
	}

	primary_ok = 1;
	secondary_ok = 1;
	if (fg->pri_stop_fg_used) {
		FGStatus* fs = &fgstatus[fg->pri_stop_fg];
		primary_ok = (fs->cond[0].count == fs->cond[0].detail);
	}
	if (fg->sec_stop_fg_used) {
		FGStatus* fs = &fgstatus[fg->sec_stop_fg];
		if (fs->cond[0].count != fs->cond[0].detail)
			secondary_ok = 0;
	}
	return secondary_ok & primary_ok;
}
