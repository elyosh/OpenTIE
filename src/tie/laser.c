#include "tie/laser.h"
#include "tie/create.h"
#include "tie/edition.h"
#include "tie/fsfx.h"
#include "tie/math2.h"
#include "tie/msg.h"
#include "tie/msg_templates.h"
#include "tie/pai.h"
#include "tie/panel.h"
#include "tie/spec.h"
#include "tie/starship.h"
#include "tie/static.h"
#include "tie/tie.h"
#include "tie/trig2.h"
#include "tie/user.h"
#include "tie_runtime/audio/config.h"
#include "tie_runtime/diagnostics/diagnostics.h"
#include "tie_runtime/diagnostics/flight_trace.h"
#include "tie_runtime/display/classic_display.h"
#include "tie_runtime/display/classic_framebuffer.h"
#include "tie_runtime/flight_assets/model_types.h"
#include "tie_runtime/input/input.h"
#include "tie_runtime/runtime/exports.h"
#include "tie_runtime/runtime/inflight_state.h"
#include "tie_runtime/runtime/profile.h"
#ifdef TIE_MODERN
#include "tie_runtime/snapshot/snapshot.h"
#include "tie_runtime/snapshot/snapshot_internal.h"
#endif
#include "tie_runtime/storage/storage.h"
#ifdef TIE_MODERN
#include "tie_runtime/timing/flight_timing.h"
#endif

#include <stdint.h>

/* spec_data declared in tie.h; no local extern needed. */

/* user_targetincross and user_mapmissiletomessage are declared in
 * user.h (both are stubs there until the USER module is ported). */

/* ------------------------------------------------------------------ */
/* Per-projectile-type parameter tables.                              */
/* ------------------------------------------------------------------ */

/* Kinetic mass for collision physics. Light lasers ~200-500; torps
 * and capital-ship munitions ~10000-65000. */
// GLOBAL: TIE95 0xC542C
// GLOBAL: TIE98 0x4E4520
const uint16_t projectileweight[NUM_PROJECTILE_TYPES] = {
	250,  500,   200,   400,  200,  400,  10000, 3000, 1000, 800, 800, 15000,
	6000, 65000, 35000, 3000, 6000, 9000, 0,     0,    0,    0,   0,   0,
};

/* Movement speed in world units per tick.
 *
 * The alternating HIBYTE / LOBYTE of entries 3..23 is read by nine
 * sites as a 'projectile explodes on death' boolean (see note in
 * laser.h). We don't break that by tweaking values. */
// GLOBAL: TIE95 0xC545C
// GLOBAL: TIE98 0x4E4550
const uint16_t projectilevelocity[NUM_PROJECTILE_TYPES] = {
	1000, 1000, 900, 900, 700, 800, 250, 500, 1000, 900, 400, 300,
	600,  25,   175, 300, 350, 400, 0,   0,   0,    0,   0,   0,
};

/* Lifetime in ticks. Lasers live 2-5; warheads 30-120. */
// GLOBAL: TIE95 0xC548C
// GLOBAL: TIE98 0x4E4580
const uint16_t projectilelife[NUM_PROJECTILE_TYPES] = {
	2, 3, 2, 3, 3, 4, 60, 30, 3, 3, 5, 50, 25, 120, 90, 45, 40, 35, 0, 0, 0, 0, 0, 0,
};

/* Forward displacement from the hardpoint to the projectile model origin.
 * The model extends backward by this distance so its tail begins at the
 * muzzle. The two game versions use different model dimensions. */
// GLOBAL: TIE95 0xC54BC
const uint16_t projectilelength[NUM_PROJECTILE_TYPES] = {
	2048, 2048, 2048, 2048, 2048, 2048, 512, 512, /* species 137..144 */
	2048, 2048, 2048, 512,  512,  48,   512, 512, /* species 145..152 */
	512,  512,  0,    0,    0,    0,    0,   0,   /* species 153..160 */
};

// GLOBAL: TIE98 0x4E45B0
const uint16_t tie98_projectilelength[NUM_PROJECTILE_TYPES] = {
	921, 921, 921, 921, 921, 921, 512, 512, /* species 137..144 */
	921, 921, 921, 512, 512, 48,  512, 512, /* species 145..152 */
	512, 512, 0,   0,   0,   0,   0,   0,   /* species 153..160 */
};

/* Per-weapon-species 'explodes on death' flag (the demo's
 * _projectilewarhead), indexed by (species - 137). 0 = silent removal;
 * 1/2 = full explosion (picks chunk variant). Entries [18..23] (species
 * 155..160) are zero in the original data. */
// GLOBAL: TIE95 0xC54EC
// GLOBAL: TIE98 0x4E45E0
const uint8_t projectile_is_warhead_type[WARHEAD_TYPE_COUNT] = {
	/* 137 */ 0, /* 138 */ 0, /* 139 */ 0, /* 140 */ 0,
	/* 141 */ 0, /* 142 */ 0, /* 143 */ 2, /* 144 */ 1,
	/* 145 */ 0, /* 146 */ 0, /* 147 */ 0, /* 148 */ 2,
	/* 149 */ 1, /* 150 */ 2, /* 151 */ 2, /* 152 */ 1,
	/* 153 */ 1, /* 154 */ 2,
	/* 155 */ 0, /* 156 */ 0, /* 157 */ 0, /* 158 */ 0,
	/* 159 */ 0, /* 160 */ 0,
};

/* ------------------------------------------------------------------ */
/* Runtime globals.                                                   */
/* ------------------------------------------------------------------ */

/* ------------------------------------------------------------------ */
/* Lock-range constants used by laser_weaponsfire's missile gauge.    */
/*                                                                    */
/* The binary emits these as plain 32-bit immediates (see comment in  */
/* IDA at 0x2b5fe / 0x2b631). Values happen to fall inside the code   */
/* segment so IDA originally displayed them as offsets into cseg01,   */
/* but they are plain distance thresholds in world units.             */
/* ------------------------------------------------------------------ */
enum {
	LOCK_RANGE_FIGHTER = 101805u, /* genus != 3/4/5 */
	LOCK_RANGE_CAPSHIP = 244332u, /* genus 3,4,5 (freighter/starship/platform) */
};

/* Overdrive-off (SLAM) sentinel. */
enum {
	SLAM_DISENGAGED = ((uint16_t)-1),
};

/* ================================================================== */
/* laser_weaponsfire -- per-frame master dispatch.                     */
/* ================================================================== */
// FUNCTION: TIE95 0x2CDD0
void laser_weaponsfire(void) {
	uint16_t beam_target;
	uint32_t best_beamdist;
	uint16_t i;
	uint16_t n;
	uint16_t s;

	/* ----- Phase 1: missile lock gauge --------------------------- */
	if (pstate.player_weapon_mode) {
		uint16_t missile_hp = spec_data[pstate.player_spec_num].missile_start[pstate.player_weapon_group];
		uint16_t ammo = pstate.player_craft->weapon_slots[missile_hp].ammo;
		ammo += pstate.player_craft->weapon_slots[missile_hp + 1].ammo;

		if (pstate.target_obj_idx != 0xFFFF && ammo) {
			uint32_t lock_range;

			pai_distancebetween(pstate.object_idx, pstate.target_obj_idx);

			lock_range = LOCK_RANGE_FIGHTER;
			if (pstate.target_obj_idx < NUM_CRAFTS) {
				uint8_t tgenus = objects[pstate.target_obj_idx].genus;
				if (tgenus == 4 || tgenus == 5 || tgenus == 3)
					lock_range = LOCK_RANGE_CAPSHIP;
			}

			if (lock_range > (uint32_t)trig2_polardistance && user_targetincross(pstate.target_obj_idx, 0)) {
				uint16_t thresh;

				pstate.player_craft->missile_count_total += frameticks;

				thresh = (spec_getspecnum(0xC) == pstate.player_spec_num) ? (uint16_t)590 : (uint16_t)1180;
				if ((int16_t)pstate.player_craft->missile_count_total >= thresh)
					pstate.radar_subtarget_state = 2;
				else
					pstate.radar_subtarget_state = 1;
			} else {
				if ((int16_t)pstate.player_craft->missile_count_total > 0) {
					pstate.player_craft->missile_count_total -= frameticks;
					if ((int16_t)pstate.player_craft->missile_count_total < 0)
						pstate.player_craft->missile_count_total = 0;
				}
				pstate.radar_subtarget_state = 0;
			}
		} else {
			pstate.radar_subtarget_state = 0;
			pstate.player_craft->missile_count_total = 0;
		}
	}

	/* ----- Phase 2: beam weapon -------------------------------- */
	beam_target = 0xFFFF;
	best_beamdist = 0x20000;

	/* Retail also requires status_flags & 0x100 (beam subsystem online —
	 * cleared when ion-drained or boarded) and !pstate.hyperin_state (post-eject
	 * the cockpit is gone). Demo had only the inner two checks. */
	if ((pstate.player_craft->status_flags & 0x100) && (pstate.player_craft->beam_state & 0x80) &&
		pstate.player_craft->beam_charge > 0 && !pstate.hyperin_state) {
		if (!timers[TIMER_LASER_BEAM_DRAIN]) {
			int16_t bc = (int16_t)(pstate.player_craft->beam_charge - 83);
			timers[TIMER_LASER_BEAM_DRAIN] = 59;
			if (bc < 0)
				bc = 0;
			pstate.player_craft->beam_charge = bc;
		}

		for (i = 0; i < NUM_CRAFTS; ++i) {
			if (!objects[i].ship_idx || i == pstate.object_idx)
				continue;

			objects[i].craft_ptr->beam_state &= 0x80;

			if ((species_table[objects[i].ship_idx].side & 1) && user_targetincross(i, 0) &&
				best_beamdist > (uint32_t)roughdistance) {
				beam_target = i;
				best_beamdist = (uint32_t)roughdistance;
			}
		}

		if (beam_target != 0xFFFF) {
			CraftData* tgt_cp = objects[beam_target].craft_ptr;
			int beam_type = pstate.player_craft->beam_type;
			if (beam_type == 1)
				tgt_cp->beam_state |= 1;
			else if (beam_type == 2)
				tgt_cp->beam_state |= 2;
			bluetarget = beam_target;
		} else {
			bluetarget = 0xFFFF;
		}
		fsfx_triggerbeamsfx(1);
	} else {
		bluetarget = 0xFFFF;
		fsfx_triggerbeamsfx(0);
	}

	/* ----- Phase 3: per-second status update -------------------- */
	if (!timers[TIMER_LASER_STATUS]) {
		timers[TIMER_LASER_STATUS] = 236;

		for (i = 0; i < NUM_CRAFTS; ++i) {
			if (!objects[i].ship_idx)
				continue;
			if (objects[i].category)
				continue;
			if (objects[i].genus != 0 /* GENUS_FIGHTER */ && objects[i].genus != 1 /* GENUS_TRANSPORT */)
				continue;

			craftptr = objects[i].craft_ptr;

			if (i != pstate.object_idx) {
				uint16_t charge_sum;
				uint16_t weap_cnt;
				uint16_t j;

				if (craftptr->subsystem_active & 1) {
					uint16_t base = (uint16_t)(2 * spec_data[craftptr->species_idx].shield_points);
					if (craftptr->forward_shield > 0) {
						uint16_t pct = math2_percentage((uint16_t)craftptr->forward_shield, base);
						if (pct < 0x4000)
							craftptr->shield_power = 4;
						else if (pct < 0xFFFF)
							craftptr->shield_power = 3;
						else
							craftptr->shield_power = 2;
					} else {
						craftptr->shield_power = 4;
					}
				}

				charge_sum = 0;
				weap_cnt = 0;
				for (j = 0; j < craftptr->weapon_group_cnt; ++j) {
					if (craftptr->weapon_slots[j].type) {
						weap_cnt++;
						charge_sum += (int8_t)craftptr->weapon_slots[j].charge;
					}
				}
				if (weap_cnt) {
					int16_t avg = (int16_t)(charge_sum / weap_cnt);
					if (avg < 32)
						craftptr->laser_power = 4;
					else if (avg < 96)
						craftptr->laser_power = 3;
					else
						craftptr->laser_power = 2;
				}
			}

			/* Shield regen (if the subsystem bit is set). */
			if (craftptr->status_flags & 1) {
				int16_t delta = (int16_t)(20 * (craftptr->shield_power - 2));
				if (delta) {
					if (!craftptr->is_player_craft) {
						laser_chargeshields(i, 0, delta);
					} else if (craftptr->is_player_craft == 2) {
						/* All to rear. */
						laser_chargeshields(i, 1, delta);
					} else {
						/* Half to front, half to rear. */
						int16_t half = (int16_t)(delta / 2);
						laser_chargeshields(i, 0, half);
						laser_chargeshields(i, 1, half);
					}
				}
			}

			/* Laser-charge regen. */
			if (craftptr->status_flags & 0x10) {
				uint16_t k;

				for (k = 0; k < craftptr->weapon_group_cnt; ++k) {
					int16_t step;

					if (!craftptr->weapon_slots[k].type || craftptr->weapon_slots[k].type == 2)
						continue;

					step = (int16_t)(craftptr->laser_power - 2);
					if (!craftptr->slam_active)
						step -= 4;
					step *= 2;

					craftptr->weapon_slots[k].charge += step;
					if (step < 0 && (int8_t)craftptr->weapon_slots[k].charge < 0)
						craftptr->weapon_slots[k].charge = 0;
					if (step > 0 && (int8_t)craftptr->weapon_slots[k].charge < 0)
						craftptr->weapon_slots[k].charge = 127;
				}
			}

			/* SLAM overdrive: when it's off (==0) and all weapon
			 * charges have drained, latch it DISENGAGED (-1). */
			if (!craftptr->slam_active) {
				int16_t any = 0;
				uint16_t j;

				for (j = 0; j < craftptr->weapon_group_cnt; ++j) {
					if ((int8_t)craftptr->weapon_slots[j].charge > 0)
						any = 1;
				}
				if (!any) {
					craftptr->slam_active = SLAM_DISENGAGED;
					msg_messageprintf(MSG_OVERDRIVE_DISENGAGED);
					fsfx_triggersfx(0x6C, 0xFFFF);
				}
			}
		}

		/* Player beam regen. */
		if (pstate.player_craft->status_flags & 0x100) {
			int16_t bc =
				(int16_t)(pstate.player_craft->beam_charge + 125 * (pstate.player_craft->beam_power - 2));
			if (bc < 0)
				bc = 0;
			if (bc > 9999)
				bc = 9999;
			pstate.player_craft->beam_charge = bc;
		}
	}

	/* ----- Phase 4: per-frame AI fire loop ---------------------- */
	for (n = 0; n < NUM_CRAFTS; ++n) {
		uint16_t g;
		uint16_t k;
		uint16_t m;

		if (!objects[n].ship_idx)
			continue;
		if (objects[n].category)
			continue;

		craftptr = objects[n].craft_ptr;
		if ((craftptr->beam_state & 2) || craftptr->ion_drain_timer)
			continue;

		for (g = 0; g < craftptr->laser_group_cnt; ++g) {
			int16_t cd = (int16_t)craftptr->laser_cooldown[g];
#ifdef TIE_MODERN
			const int16_t cooldown_before = cd;
			bool ready;
#endif

			if (cd) {
				cd -= (int16_t)frameticks;
				if (cd < 0)
					cd = 0;
				craftptr->laser_cooldown[g] = (uint16_t)cd;
			}
			if (n == pstate.object_idx)
				continue;
#ifdef TIE_MODERN
			if (TieFlightTiming_IsHighRate())
				ready = cooldown_before <= (int16_t)frameticks;
			else
				ready = cd < (int16_t)frameticks;
			if (ready && craftptr->laser_owner_player[g]) {
#else
			if (cd < (int16_t)frameticks && craftptr->laser_owner_player[g]) {
#endif
				if ((craftptr->status_flags & 0x10) && !craftptr->flight_flag)
					laser_firelasersystem(n, g);

				--craftptr->laser_burst_remaining[g];
#ifdef TIE_MODERN
				if (TieFlightTiming_IsHighRate())
					craftptr->laser_cooldown[g] += 2 * TieFlightTiming_CompatibilityTicks();
				else
#endif
					craftptr->laser_cooldown[g] += 2 * frameticks;
				if (!craftptr->laser_burst_remaining[g])
					craftptr->laser_owner_player[g] = 0; /* burst spent: stop AI firing this group */
			}
		}

		/* Turrets (weapon_slots[k].type == 2). */
		for (k = 0; k < craftptr->weapon_group_cnt; ++k) {
			if (craftptr->weapon_slots[k].type == 2 && craftptr->weapon_slots[k].target_obj != 0xFFFF)
				starship_firelasergunner(n, k, craftptr->weapon_slots[k].target_obj);
		}

		/* Missile cooldown decrement. */
		for (m = 0; m < craftptr->missile_group_cnt; ++m) {
			int16_t mcd = (int16_t)craftptr->missile_state[m];
			if (mcd) {
				mcd -= (int16_t)frameticks;
				if (mcd < 0)
					mcd = 0;
				craftptr->missile_state[m] = (uint16_t)mcd;
			}
		}
	}

	/* ----- Phase 5: mine-turret update ------------------------- */
	for (s = 0; s < 0x40; ++s) {
		if (staticobjects[s].species && staticobjects[s].ship_class == 8)
			static_updatemineguns(s);
	}
}

/* ================================================================== */
/* laser_chargeshields                                                 */
/* ================================================================== */
// FUNCTION: TIE95 0x2D6D8
void laser_chargeshields(uint16_t shooter_obj_idx, uint16_t shield_side, int16_t delta) {
	/*
	 * _craftptr is the SHOOTER here (not the defender). On Easy
	 * difficulty the shield cap is boosted for hostile shooters, so
	 * the player takes lighter damage. See LASER_weaponsfire status
	 * phase (is_player_craft path) for how this is called.
	 */
	int16_t cap = (int16_t)(2 * spec_data[craftptr->species_idx].shield_points);

	int16_t* shields;
	int16_t new_shield;

	if (!mission.difficulty) {
		if (shooter_obj_idx == pstate.object_idx) {
			/* Player-as-shooter: double the base cap. */
			cap = (int16_t)(4 * spec_data[craftptr->species_idx].shield_points);
		} else {
			uint8_t side = objects[shooter_obj_idx].side;
			if (side == 1) {
				int32_t v = (int32_t)cap + ((int32_t)cap >> 1);
				cap = (v >= 0x8000) ? (int16_t)30000 : (int16_t)v;
			} else if (side == 0 || side == 4) {
				cap = (int16_t)math2_fraction((uint16_t)cap, 0xA000);
			}
		}
	}

	/* CraftData.forward_shield (+0xCA) and rear_shield (+0xCC) are
	 * adjacent int16 fields; shield_side selects which (0=front, 1=rear). */
	shields = &craftptr->forward_shield;
	new_shield = (int16_t)(shields[shield_side] + delta);
	shields[shield_side] = new_shield;
	if (new_shield < 0)
		shields[shield_side] = 0;

	if (cap < shields[shield_side])
		shields[shield_side] = cap;
}

/* ================================================================== */
/* laser_fireplayerweapon                                              */
/* ================================================================== */
// FUNCTION: TIE95 0x2D7C8
void laser_fireplayerweapon(void) {
	int16_t cd;

	if (!pstate.player_weapon_mode) {
		cd = (int16_t)pstate.player_craft->laser_cooldown[pstate.player_weapon_group];
		if (cd) {
			cd -= (int16_t)frameticks;
			if (cd < 0)
				cd = 0;
		}
		if (cd < (int16_t)frameticks) {
			if (cd)
				pstate.player_craft->laser_cooldown[pstate.player_weapon_group] -= frameticks;
			else
				pstate.player_craft->laser_cooldown[pstate.player_weapon_group] = 0;

			if (pstate.player_craft->status_flags & 0x10) {
				laser_firelasersystem(pstate.object_idx, pstate.player_weapon_group);
				pstate.player_craft->laser_cooldown[pstate.player_weapon_group] += frameticks;
			} else {
				argtable[0] = (uint16_t)(pstate.player_weapon_group + 29);
				argtable[1] = 25;
				msg_messageprintf(MSG_SYSTEM_STATUS);
			}
		}
	} else {
		/* Missile mode. */
		cd = (int16_t)pstate.player_craft->missile_state[pstate.player_weapon_group];
		if (cd) {
			cd -= (int16_t)frameticks;
			if (cd < 0)
				cd = 0;
		}
		if (cd < (int16_t)frameticks) {
			if (cd)
				pstate.player_craft->missile_state[pstate.player_weapon_group] -= frameticks;
			else
				pstate.player_craft->missile_state[pstate.player_weapon_group] = 0;

			if (pstate.player_craft->status_flags & 8) {
				laser_firerocketsystem(pstate.object_idx, pstate.player_weapon_group);
				pstate.player_craft->missile_state[pstate.player_weapon_group] += frameticks;
			} else {
				argtable[1] = 25;
				argtable[0] = 31;
				msg_messageprintf(MSG_SYSTEM_STATUS);
			}
		}
	}
}

/* ================================================================== */
/* laser_firelasersystem                                               */
/* ================================================================== */
// FUNCTION: TIE95 0x2D9AC
void laser_firelasersystem(uint16_t shooter_obj_idx, uint16_t group_idx) {
	uint16_t slot_stride;
	uint16_t i;
	uint16_t end_slot;
	uint16_t species_idx;
	uint16_t laser_type;
	uint16_t shots_remaining;
	uint16_t start_slot;
	uint16_t shots_fired;

	craftptr = objects[shooter_obj_idx].craft_ptr;
	species_idx = craftptr->species_idx;
	if (craftptr->ai_anim_flags)
		return; /* craft is locked in animation */

	/* Fire mode per bank:
	 *   1 = single-shot (cycle through the bank's hardpoints)
	 *   2 = alternating (every other hardpoint, flipping parity)
	 *   3 = full burst (every hardpoint in the bank)
	 *
	 * The player cycles it in USER_inputforplane; AI sets it to 1..3.
	 */
	shots_fired = 0;
	switch (craftptr->laser_owner_player[group_idx]) {
		case 3:
			start_slot = spec_data[species_idx].laser_start[group_idx];
			end_slot = spec_data[species_idx].laser_end[group_idx];
			shots_remaining = end_slot - start_slot + 1;
			slot_stride = 1;
			break;
		case 1:
			start_slot = craftptr->laser_first_slot[group_idx];
			end_slot = start_slot;
			if (++craftptr->laser_first_slot[group_idx] > spec_data[species_idx].laser_end[group_idx])
				craftptr->laser_first_slot[group_idx] = spec_data[species_idx].laser_start[group_idx];
			shots_remaining = 1;
			slot_stride = 1;
			break;
		case 2:
			start_slot = craftptr->laser_first_slot[group_idx];
			craftptr->laser_first_slot[group_idx] = (uint8_t)(start_slot ^ 1);
			if (craftptr->laser_first_slot[group_idx] > spec_data[species_idx].laser_end[group_idx])
				craftptr->laser_first_slot[group_idx] = spec_data[species_idx].laser_start[group_idx];
			end_slot = spec_data[species_idx].laser_end[group_idx];
			shots_remaining = (spec_data[species_idx].laser_end[group_idx] -
							   spec_data[species_idx].laser_start[group_idx] + 1) /
							  2;
			slot_stride = 2;
			break;
	}

	for (i = start_slot; i <= end_slot; i += slot_stride) {
		if (craftptr->weapon_slots[i].type != 0 && (int8_t)craftptr->weapon_slots[i].charge > 0) {
			uint16_t pslot;

			laser_type = spec_data[species_idx].laser_type[group_idx];
			if ((int8_t)craftptr->weapon_slots[i].charge >= 64)
				laser_type++; /* charged variant */

			pslot = laser_createprojectile(shooter_obj_idx, i, laser_type);
			if (pslot != 0xFFFF) {
				if (shooter_obj_idx == pstate.object_idx) {
					if (!inflight_unlimited)
						craftptr->weapon_slots[i].charge -= 4;
				} else {
					craftptr->weapon_slots[i].charge--;
				}

				if (shots_fired < 2)
					fsfx_triggerlasersfx(pslot);

				if ((int8_t)craftptr->weapon_slots[i].charge < 0)
					craftptr->weapon_slots[i].charge = 0;

				if (shooter_obj_idx == pstate.object_idx)
					warheads[(uint16_t)(pslot - NUM_CRAFTS)].target_obj = pstate.target_obj_idx;
				else
					warheads[(uint16_t)(pslot - NUM_CRAFTS)].target_obj = (uint16_t)craftptr->ai_target_ref;
				shots_fired++;
			}
		}
		if (--shots_remaining == 0)
			break;
	}

	if (laser_type == 141 || laser_type == 142) {
		/* Ion cannon / disruptor types counted as 'missile_fired'. */
		craftptr->missile_fired += shots_fired;
		if (shooter_obj_idx == pstate.object_idx)
			pstate.player_missile_fired += shots_fired;
	} else {
		craftptr->laser_fired += shots_fired;
		if (shooter_obj_idx == pstate.object_idx)
			pstate.player_laser_fired += shots_fired;
	}

	craftptr->laser_cooldown[group_idx] = (uint16_t)(78 * shots_fired + 2);
}

/* ================================================================== */
/* laser_firerocketsystem                                              */
/* ================================================================== */
// FUNCTION: TIE95 0x2DD50
void laser_firerocketsystem(uint16_t shooter_obj_idx, uint16_t group_idx) {
	int16_t ammo_but_no_pool_slot;
	uint16_t shots_fired;
	uint16_t first_slot;
	uint16_t slot;
	uint16_t mode;

	craftptr = objects[shooter_obj_idx].craft_ptr;
	mode = craftptr->missile_armed[group_idx];
	first_slot = spec_data[craftptr->species_idx].missile_start[group_idx];
	shots_fired = 0;
	ammo_but_no_pool_slot = 0;

	if ((mode & 0x7F) == 3) {
		/* DUAL: fire first_slot, then first_slot+1. */
		slot = first_slot;
		if (laser_firemissile(shooter_obj_idx, slot, craftptr->warhead_type[group_idx], group_idx) !=
			(uint16_t)-1)
			shots_fired = 1;
		else if (craftptr->weapon_slots[slot].ammo > 0)
			ammo_but_no_pool_slot = 1;

		slot = first_slot + 1;
		if (laser_firemissile(shooter_obj_idx, slot, craftptr->warhead_type[group_idx], group_idx) !=
			(uint16_t)-1)
			shots_fired++;
		else if (craftptr->weapon_slots[slot].ammo > 0)
			ammo_but_no_pool_slot = 1;
	} else if (mode & 0x80) {
		/* ALTERNATING: fire first_slot+1 (toggle set by previous shot). */
		slot = first_slot + 1;
		if (laser_firemissile(shooter_obj_idx, slot, craftptr->warhead_type[group_idx], group_idx) !=
			(uint16_t)-1)
			shots_fired = 1;
		else if (craftptr->weapon_slots[slot].ammo > 0)
			ammo_but_no_pool_slot = 1;
	} else {
		/* SINGLE / initial shot. */
		slot = first_slot;
		if (laser_firemissile(shooter_obj_idx, slot, craftptr->warhead_type[group_idx], group_idx) !=
			(uint16_t)-1)
			shots_fired = 1;
		else if (craftptr->weapon_slots[slot].ammo > 0)
			ammo_but_no_pool_slot = 1;
	}

	craftptr->missile_state[group_idx] = 472; /* ~2s reload cooldown */

	if (shooter_obj_idx == pstate.object_idx && !ammo_but_no_pool_slot) {
		argtable[0] =
			(uint16_t)user_mapmissiletomessage(pstate.player_craft->warhead_type[pstate.player_weapon_group]);
		msg_messageprintf(shots_fired + 106);
	}
}

/* ================================================================== */
/* laser_firemissile                                                   */
/* ================================================================== */
// FUNCTION: TIE95 0x2DF68
uint16_t laser_firemissile(uint16_t shooter_obj_idx, uint16_t weapon_slot_idx, uint16_t projectile_type,
						   uint16_t group_idx) {
	uint16_t wh = 0xFFFF;

	/* Need a loaded weapon rack with ammo left. */
	if (craftptr->weapon_slots[weapon_slot_idx].type != 0 &&
		craftptr->weapon_slots[weapon_slot_idx].ammo > 0) {
		wh = laser_createprojectile(shooter_obj_idx, weapon_slot_idx, projectile_type);
		if (wh != (uint16_t)-1) {
			/* Left/right tube toggle (only meaningful for group 0/1 racks). */
			if (group_idx < 2)
				craftptr->missile_armed[group_idx] ^= 0x80u;

			craftptr->warhead_fired++;
			if (shooter_obj_idx == pstate.object_idx)
				pstate.player_warhead_fired++;

			fsfx_triggerlasersfx(wh);

			if (!inflight_unlimited || shooter_obj_idx != pstate.object_idx)
				craftptr->weapon_slots[weapon_slot_idx].ammo--;

			wh -= NUM_CRAFTS;

			if (group_idx < 2) {
				/* Homing tier = current lock strength (missile_count_total, a
				 * frame-accumulated counter) divided by 236 (ticks-per-sec).
				 * Clamp at 6 tiers. */
				warheads[wh].homing_tier = (uint8_t)((int16_t)craftptr->missile_count_total / 236);
				if (warheads[wh].homing_tier > 6)
					warheads[wh].homing_tier = 6;
			}

			if (shooter_obj_idx == pstate.object_idx) {
				warheads[wh].target_obj = pstate.target_obj_idx;
				warheads[wh].sub_obj_idx = (uint16_t)pstate.radar_target1;
			} else {
				warheads[wh].target_obj = (uint16_t)craftptr->ai_target_ref;
				warheads[wh].sub_obj_idx = (uint16_t)craftptr->link_target_2E;
			}

			TIE_FLIGHT_TRACE_TARGET_CHANGE((uint16_t)(wh + NUM_CRAFTS), 0xFFFFu, warheads[wh].target_obj);
			laser_warnplayer(wh);
		}
	}
	return wh;
}

/* ================================================================== */
/* laser_createprojectile                                              */
/* ================================================================== */
// FUNCTION: TIE95 0x2E0E4
uint16_t laser_createprojectile(uint16_t shooter_obj_idx, uint16_t hp_idx, uint16_t projectile_type) {
	FlightObject* shooter;
	uint16_t shooter_ship_idx;
	uint16_t spec_num;
	uint16_t slot;
	uint16_t radius;
	int32_t world_x;
	int32_t world_y;
	int32_t world_z;
	int16_t hp_x;
	int16_t hp_y;
	int16_t hp_z;
	uint16_t wh;

	/* Genus: 6 (GENUS_PROJECTILE_PLAYER) if shooter is the player, 7
	 * (GENUS_PROJECTILE_NPC) otherwise. The game uses this to tag "player's
	 * shots" vs "NPC shots" for scoring and collision routing. */
	uint16_t proj_genus = (shooter_obj_idx != pstate.object_idx) + 6;

	slot = create_findslot(proj_genus);
	if (slot == 0xFFFF)
		return slot;

	shooter = &objects[shooter_obj_idx];
	shooter_ship_idx = shooter->ship_idx;

	objects[slot].category = 1;
	objects[slot].genus = proj_genus;
	objects[slot].self_idx = shooter_obj_idx;
	objects[slot].ship_type_override = (uint8_t)shooter_ship_idx;
	objects[slot].ship_idx = (uint8_t)projectile_type;
	objects[slot].age_ticks = 1;

	spec_num = spec_getspecnum(shooter_ship_idx);

	objects[slot].side = shooter->side;
	objects[slot].pitch = shooter->pitch;
	objects[slot].roll = shooter->roll;
	objects[slot].heading = shooter->heading;

	/* Species-indexed weapon tables: projectilevelocity / projectileweight
	 * / projectilelife are keyed by (species - WEAPON_SPECIES_BASE).
	 * Callers guarantee species in [137, 154] — matches retail contract;
	 * no bounds check. */
	objects[slot].current_speed =
		(int16_t)(shooter->current_speed + projectilevelocity[projectile_type - WEAPON_SPECIES_BASE]);

	/* Cache initial speed into warheads[slot-NUM_CRAFTS].min_speed so
	 * MOVE's homing floor has a value even before the warhead record is
	 * otherwise written. */
	warheads[slot - NUM_CRAFTS].min_speed = (uint16_t)objects[slot].current_speed;

	/* The collision radius never drops below the weapon's base radius. */
	radius = (uint16_t)(shooter->current_speed + projectileweight[projectile_type - WEAPON_SPECIES_BASE]);
	objects[slot].collision_radius = (int16_t)radius;
	if (radius < projectileweight[projectile_type - WEAPON_SPECIES_BASE])
		objects[slot].collision_radius = (int16_t)projectileweight[projectile_type - WEAPON_SPECIES_BASE];
	objects[slot].death_timer = (int16_t)(236 * projectilelife[projectile_type - WEAPON_SPECIES_BASE]);

	world_x = shooter->world_x;
	world_y = shooter->world_y;
	world_z = shooter->world_z;

	hp_x = spec_data[spec_num].hp[hp_idx].x;
	hp_y = spec_data[spec_num].hp[hp_idx].y;
	hp_z = spec_data[spec_num].hp[hp_idx].z;
	pai_calcrotatedpoint((struct FlightObject*)shooter, hp_x, hp_y, hp_z);

	world_x += rotatedx;
	world_y += rotatedy;
	world_z += rotatedz;

	objects[slot].world_x_prev = world_x;
	objects[slot].world_y_prev = world_y;
	objects[slot].world_z_prev = world_z;

	if (projectile_is_warhead_type[projectile_type - WEAPON_SPECIES_BASE] &&
		(shooter->genus == 4 || shooter->genus == 3 || shooter->genus == 5)) {
		/* Capship turret branch: hp_y < 0 means the gun is mounted on the
		 * underside -> projectile points straight down (pitch 0x8000) and
		 * world_z is offset by -length instead of +length. */
		if (hp_y >= 0) {
			world_z += (int16_t)TIE_FLIGHT_EDITION(
				projectilelength, tie98_projectilelength)[projectile_type - WEAPON_SPECIES_BASE];
			objects[slot].pitch = 0;
		} else {
			world_z -= (int16_t)TIE_FLIGHT_EDITION(
				projectilelength, tie98_projectilelength)[projectile_type - WEAPON_SPECIES_BASE];
			objects[slot].pitch = (int16_t)0x8000;
		}
		objects[slot].orient_dirty = 1;
		objects[slot].move_dirty = 1;
		objects[slot].world_x = world_x;
		objects[slot].world_z = world_z;
		objects[slot].world_y = world_y;
	} else {
		/* Normal branch: inherit shooter's full rotation basis and
		 * advance the spawn point by muzzle_length along the forward
		 * vector (each axis scaled by fwd_component * length / 2^15). */
		world_x += ((int32_t)(int16_t)TIE_FLIGHT_EDITION(
						projectilelength, tie98_projectilelength)[projectile_type - WEAPON_SPECIES_BASE] *
					shooter->fwd_x) >>
				   15;
		world_y += ((int32_t)(int16_t)TIE_FLIGHT_EDITION(
						projectilelength, tie98_projectilelength)[projectile_type - WEAPON_SPECIES_BASE] *
					shooter->fwd_y) >>
				   15;
		world_z += ((int32_t)(int16_t)TIE_FLIGHT_EDITION(
						projectilelength, tie98_projectilelength)[projectile_type - WEAPON_SPECIES_BASE] *
					shooter->fwd_z) >>
				   15;

		objects[slot].world_x = world_x;
		objects[slot].world_z = world_z;
		objects[slot].world_y = world_y;
		objects[slot].moveX = shooter->moveX;
		objects[slot].moveY = shooter->moveY;
		objects[slot].moveZ = shooter->moveZ;
		objects[slot].side_x = shooter->side_x;
		objects[slot].side_y = shooter->side_y;
		objects[slot].side_z = shooter->side_z;
		objects[slot].up_x = shooter->up_x;
		objects[slot].up_y = shooter->up_y;
		objects[slot].up_z = shooter->up_z;
		objects[slot].fwd_x = shooter->fwd_x;
		objects[slot].fwd_y = shooter->fwd_y;
		objects[slot].fwd_z = shooter->fwd_z;
		objects[slot].orient_dirty = 0;
		objects[slot].move_dirty = 0;
	}

	/* Pair the slot with a warhead record: no homing (tier 0), no
	 * target. MOVE's genus-6/7 step skips homing when target_obj is
	 * 0xFFFF. */
	wh = slot - NUM_CRAFTS;
	warheads[wh].homing_tier = 0;
	warheads[wh].target_obj = 0xFFFF;
	objects[slot].craft_ptr = (CraftData*)&warheads[wh];

	/* actor_id identifies the projectile; param0 and param1 identify its
	 * projectile type and shooter slot for the renderer's spawn effect. */
#ifdef TIE_MODERN
	{
		TieEvent ev = {
			.kind     = TIE_EVENT_LASER_SPAWN,
			.actor_id = objects[slot].idnumber,
			.world_pos = {
				objects[slot].world_x,
				objects[slot].world_y,
				objects[slot].world_z,
			},
			.param0   = (int32_t)projectile_type,
			.param1   = (int32_t)shooter_obj_idx,
		};
		TieSnapshotBuilder_PushEvent(&ev);
	}
#endif
	TIE_FLIGHT_TRACE_WEAPON_SPAWN(slot, shooter_obj_idx, 0xFFFFu);

	return slot;
}

/* ================================================================== */
/* laser_createprojectilefromstatic                                    */
/* ================================================================== */
// FUNCTION: TIE95 0x2E514
uint16_t laser_createprojectilefromstatic(uint16_t static_obj_idx, uint16_t shooter_obj_idx) {
	uint16_t fg_idx = staticobjects[static_obj_idx].fg_idx;
	uint16_t ptype = warheadconvert[(int8_t)fg_array[fg_idx].warhead];
	uint16_t obj_ref;
	uint16_t slot;
	int16_t proj_speed;
	uint16_t wh;

	if (ptype == 0)
		return 0xFFFF;

	slot = create_findslot(7 /* GENUS_PROJECTILE_NPC */);
	if (slot == 0xFFFF) {
		/* Fallback: scan the upper half of the warhead slot range for a
		 * non-warhead same-side occupant to evict. Retaliation
		 * pre-empts ordinary lasers of the same faction.
		 * Demo: [44, 76); retail: [48, 80). = NUM_CRAFTS+16..WARHEAD_SLOT_END. */
		for (slot = NUM_CRAFTS + 16; slot < WARHEAD_SLOT_END; ++slot) {
			/* Projectile slots can still contain in-place impact animations. */
			if (
#ifdef TIE_MODERN
				/* PORT: impact animations (species 129-132) index before the
				 * table; the original reads zero bytes there. */
				(unsigned int)(objects[slot].ship_idx - WEAPON_SPECIES_BASE) >= WARHEAD_TYPE_COUNT ||
#endif
				!projectile_is_warhead_type[objects[slot].ship_idx - WEAPON_SPECIES_BASE]) {
				int occupant_side = objects[slot].side;
				if (occupant_side == (int8_t)fg_array[fg_idx].side)
					break;
			}
		}
	}
	if (slot == WARHEAD_SLOT_END)
		return 0xFFFF;

	objects[slot].category = 1;
	objects[slot].genus = 7; /* GENUS_PROJECTILE_NPC */
	objects[slot].ship_idx = (uint8_t)ptype;
	objects[slot].age_ticks = 1;

	/* Tag self_idx with +0x3800 so create_getworldposition (and
	 * downstream resolvers) route the reference through the static
	 * table instead of the craft table. */
	obj_ref = static_obj_idx + 0x3800;
	objects[slot].self_idx = (int16_t)obj_ref;
	objects[slot].ship_type_override = staticobjects[static_obj_idx].species;
	objects[slot].side = fg_array[fg_idx].side;
	objects[slot].pitch = 0;
	objects[slot].roll = 0;
	objects[slot].heading = 0;

	proj_speed = (int16_t)projectilevelocity[ptype - WEAPON_SPECIES_BASE];
	warheads[slot - NUM_CRAFTS].min_speed = (uint16_t)proj_speed;
	objects[slot].collision_radius = (int16_t)projectileweight[ptype - WEAPON_SPECIES_BASE];
	objects[slot].death_timer = (int16_t)(236 * projectilelife[ptype - WEAPON_SPECIES_BASE]);
	objects[slot].current_speed = proj_speed;

	create_getworldposition(obj_ref, 0);
	objects[slot].world_x = objects[slot].world_x_prev = worldlocx;
	objects[slot].world_y = objects[slot].world_y_prev = worldlocy;
	objects[slot].world_z = objects[slot].world_z_prev = worldlocz + 384;

	wh = slot - NUM_CRAFTS;
	warheads[wh].homing_tier = (uint8_t)((math2_getrandom() & 3) + 3);
	warheads[wh].target_obj = shooter_obj_idx;
	objects[slot].craft_ptr = (CraftData*)&warheads[wh];

	TIE_FLIGHT_TRACE_WEAPON_SPAWN(slot, (uint16_t)(static_obj_idx + OBJ_REF_STATIC_BASE), shooter_obj_idx);
	laser_warnplayer(wh);
	return slot;
}

/* ================================================================== */
/* laser_warnplayer                                                    */
/* ================================================================== */
// FUNCTION: TIE95 0x2E768
void laser_warnplayer(uint16_t warhead_slot) {
	if (warheads[warhead_slot].target_obj != pstate.object_idx)
		return;
	if (pstate.space_confirm_action)
		return;

	msg_messageprintf(MSG_MISSILE_WARNING_PROMPT);
	fsfx_triggervoicesfx(0x28);
	fsfx_triggervoicesfx(0x28);
	if (fsfx_speakeravailable()) {
		fsfx_speakobjectname(pstate.object_idx, 0);
		fsfx_triggervoicesfx(0x4B);
	}
	/* Message-system argument slot — receives the firing object's idx
	 * for the laser-warning prompt; consumed by msg.c on auto-cancel
	 * and by user.c on SPACE-confirm. */
	pstate.msg_arg_obj_idx = (int16_t)(warhead_slot + NUM_CRAFTS);
	pstate.space_confirm_action = 1;
	timers[TIMER_SPACE_CONFIRM] = 1416;
}
