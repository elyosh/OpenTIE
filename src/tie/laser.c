#include <stdint.h>

#include "tie/create.h"
#include "tie/fsfx.h"
#include "tie/laser.h"
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
#include "tie_runtime/snapshot/snapshot.h"
#include "tie_runtime/snapshot/snapshot_internal.h"
#include "tie_runtime/storage/storage.h"
#include "tie_runtime/timing/flight_timing.h"

/* spec_data declared in tie.h; no local extern needed. */

/* user_targetincross and user_mapmissiletomessage are declared in
 * user.h (both are stubs there until the USER module is ported). */

/* ------------------------------------------------------------------ */
/* Per-projectile-type parameter tables.                              */
/* ------------------------------------------------------------------ */

/* Kinetic mass for collision physics. Light lasers ~200-500; torps
 * and capital-ship munitions ~10000-65000. */
// GLOBAL: TIE 0xC542C
const uint16_t projectileweight[NUM_PROJECTILE_TYPES] = {
	250,  500,   200,   400,  200,  400,  10000, 3000, 1000, 800, 800, 15000,
	6000, 65000, 35000, 3000, 6000, 9000, 0,     0,    0,    0,   0,   0,
};

/* Movement speed in world units per tick.
 *
 * The alternating HIBYTE / LOBYTE of entries 3..23 is read by nine
 * sites as a 'projectile explodes on death' boolean (see note in
 * laser.h). We don't break that by tweaking values. */
// GLOBAL: TIE 0xC545C
const uint16_t projectilevelocity[NUM_PROJECTILE_TYPES] = {
	1000, 1000, 900, 900, 700, 800, 250, 500, 1000, 900, 400, 300,
	600,  25,   175, 300, 350, 400, 0,   0,   0,    0,   0,   0,
};

/* Lifetime in ticks. Lasers live 2-5; warheads 30-120. */
// GLOBAL: TIE 0xC548C
const uint16_t projectilelife[NUM_PROJECTILE_TYPES] = {
	2, 3, 2, 3, 3, 4, 60, 30, 3, 3, 5, 50, 25, 120, 90, 45, 40, 35, 0, 0, 0, 0, 0, 0,
};

/* Forward displacement from the hardpoint to the projectile model origin.
 * The model extends backward by this distance so its tail begins at the
 * muzzle. The two game versions use different model dimensions. */
// GLOBAL: TIE 0xC53A8 + 2*species
static const uint16_t s_projectile_launch_offset_tie95[NUM_PROJECTILE_TYPES] = {
	0,   2048, 2048, 2048, 2048, 2048, 2048, 512, /* species 137..144 */
	512, 2048, 2048, 2048, 512,  512,  48,   512, /* species 145..152 */
	512, 512,  512,  0,    0,    0,    0,    0,   /* species 153..160 */
};

// GLOBAL: TIE98 0x4E449E + 2*species
static const uint16_t s_projectile_launch_offset_tie98[NUM_PROJECTILE_TYPES] = {
	921, 921, 921, 921, 921, 921, 512, 512, /* species 137..144 */
	921, 921, 921, 512, 512, 48,  512, 512, /* species 145..152 */
	512, 512, 0,   0,   0,   0,   0,   0,   /* species 153..160 */
};

uint16_t TieProjectileLaunchOffset_Get(unsigned int projectile_type_idx) {
	const uint16_t* offsets =
		TieProfile_UsesTie98Logic() ? s_projectile_launch_offset_tie98 : s_projectile_launch_offset_tie95;
	return offsets[projectile_type_idx];
}

/* Warhead flags. Only 12 entries; never referenced in demo. */
const uint16_t projectilewarhead[12] = {
	0, 0, 0, 258, 0, 512, 513, 258, 513, 0, 0, 0,
};

/* Per-weapon-species 'explodes on death' flag. Retail byte_C5463 bytes
 * [137..154] copied here, indexed by (species - 137). 0 = silent
 * removal; 1/2 = full explosion (picks chunk variant).
 *
 * Sized to 24 entries to match the demo's _projectilewarhead at
 * 0xD4DB8 (and the matching 24-byte slot the retail leaves between
 * the projectile launch-offset table and deepspacecolor). Entries [18..23] (species
 * 155..160) are zero in the original data — kept for callers that
 * iterate the full projectile species range. */
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
#define LOCK_RANGE_FIGHTER 101805u /* genus != 3/4/5 */
#define LOCK_RANGE_CAPSHIP 244332u /* genus 3,4,5 (freighter/starship/platform) */

/* Overdrive-off (SLAM) sentinel. */
#define SLAM_DISENGAGED ((uint16_t)-1)

/* ================================================================== */
/* laser_warnplayer                                                    */
/* ================================================================== */
// FUNCTION: TIE 0x2E768
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

/* ================================================================== */
/* laser_chargeshields                                                 */
/* ================================================================== */
// FUNCTION: TIE 0x2D6D8
void laser_chargeshields(uint16_t shooter_obj_idx, uint16_t shield_side, int16_t delta) {
	/*
	 * _craftptr is the SHOOTER here (not the defender). On Easy
	 * difficulty the shield cap is boosted for hostile shooters, so
	 * the player takes lighter damage. See LASER_weaponsfire status
	 * phase (is_player_craft path) for how this is called.
	 */
	int16_t cap = (int16_t)(2 * spec_data[craftptr->species_idx].shield_points);

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
	int16_t* shields = &craftptr->forward_shield;
	int16_t new_shield = (int16_t)(shields[shield_side] + delta);
	shields[shield_side] = new_shield;
	if (new_shield < 0)
		shields[shield_side] = 0;

	if (cap < shields[shield_side])
		shields[shield_side] = cap;
}

/* ================================================================== */
/* laser_createprojectile                                              */
/* ================================================================== */
// FUNCTION: TIE 0x2E0E4
uint16_t laser_createprojectile(uint16_t shooter_obj_idx, uint16_t hp_idx, uint16_t projectile_type) {
	/* Genus: 6 (GENUS_PROJECTILE_PLAYER) if shooter is the player, 7
	 * (GENUS_PROJECTILE_NPC) otherwise. The game uses this to tag "player's
	 * shots" vs "NPC shots" for scoring and collision routing. */
	uint8_t proj_genus = (shooter_obj_idx == pstate.object_idx) ? 6 : 7;

	uint16_t slot = create_findslot(proj_genus);
	if (slot == 0xFFFF)
		return 0xFFFF;

	FlightObject* shooter = &objects[shooter_obj_idx];
	FlightObject* proj = &objects[slot];
	uint8_t shooter_ship_idx = shooter->ship_idx;

	proj->genus = proj_genus;
	proj->self_idx = shooter_obj_idx;
	proj->category = 1;
	proj->ship_idx = (uint8_t)projectile_type;
	proj->ship_type_override = shooter_ship_idx;
	proj->age_ticks = 1;

	uint16_t spec_num = spec_getspecnum(shooter_ship_idx);

	proj->side = shooter->side;
	proj->heading = shooter->heading;
	proj->roll = shooter->roll;
	proj->pitch = shooter->pitch;

	/* Species-indexed weapon tables: projectilevelocity / projectileweight
	 * / projectilelife are keyed by (species - WEAPON_SPECIES_BASE).
	 * Callers guarantee species in [137, 154] — matches retail contract;
	 * no bounds check. */
	const unsigned int spec_idx = laser_species_idx(projectile_type);

	int16_t proj_speed = (int16_t)(projectilevelocity[spec_idx] + shooter->current_speed);
	proj->current_speed = proj_speed;

	/* Cache initial speed into warheads[slot-NUM_CRAFTS].min_speed so
	 * MOVE's homing floor has a value even before the warhead record is
	 * otherwise written. */
	warheads[slot - NUM_CRAFTS].min_speed = (uint16_t)proj_speed;

	proj->collision_radius = (int16_t)(projectileweight[spec_idx] + shooter->current_speed);
	proj->death_timer = (int16_t)(236 * projectilelife[spec_idx]);

	int32_t world_x = shooter->world_x;
	int32_t world_y = shooter->world_y;
	int32_t world_z = shooter->world_z;

	int16_t hp_x = spec_data[spec_num].hp[hp_idx].x;
	int16_t hp_y = spec_data[spec_num].hp[hp_idx].y;
	int16_t hp_z = spec_data[spec_num].hp[hp_idx].z;

	pai_calcrotatedpoint((struct FlightObject*)shooter, hp_x, hp_y, hp_z);

	int32_t muzzle_x = rotatedx + world_x;
	int32_t muzzle_y = rotatedy + world_y;
	int32_t muzzle_z = rotatedz + world_z;

	proj->world_x_prev = muzzle_x;
	proj->world_y_prev = muzzle_y;
	proj->world_z_prev = muzzle_z;

	/* "Explodes at death" flag + visual muzzle length: species-indexed. */
	uint8_t is_warhead_type = projectile_is_warhead_type[spec_idx];

	uint8_t s_genus = shooter->genus;
	int capship = (s_genus == 3 || s_genus == 4 || s_genus == 5);

	if (is_warhead_type && capship) {
		/* Capship turret branch: projectile fires straight up or
		 * down (heading forced to 0 / 0x8000); world_z offset by
		 * the species' muzzle length. */
		int16_t mlen = (int16_t)TieProjectileLaunchOffset_Get(spec_idx);
		/* Capship turret muzzle: hp_y < 0 means the gun is mounted in the
		 * negative-up direction (turret on the underside) -> projectile
		 * inherits a flipped heading (0x8000) and world_z is offset by
		 * -mlen instead of +mlen. Binary tests v33 = hp.y at LASER_createprojectile
		 * 0x2e304. */
		int32_t new_z = (hp_y < 0) ? (muzzle_z - mlen) : (muzzle_z + mlen);

		proj->heading = (hp_y < 0) ? (int16_t)0x8000 : (int16_t)0;
		proj->orient_dirty = 1;
		proj->move_dirty = 1;
		proj->world_x = muzzle_x;
		proj->world_y = muzzle_y;
		proj->world_z = new_z;
	} else {
		/* Normal branch: inherit shooter's full rotation basis and
		 * advance the spawn point by muzzle_length along the forward
		 * vector (each axis scaled by fwd_component * length / 2^15).
		 *
		 * The binary reads fwd_x/y/z via the Watcom
		 * unaligned-dword-load trick *(int*)&ff>>16; the port reads
		 * them directly. */
		int16_t mlen = (int16_t)TieProjectileLaunchOffset_Get(spec_idx);
		int32_t ox = ((int32_t)shooter->fwd_x * mlen) >> 15;
		int32_t oy = ((int32_t)shooter->fwd_y * mlen) >> 15;
		int32_t oz = ((int32_t)shooter->fwd_z * mlen) >> 15;

		proj->world_x = muzzle_x + ox;
		proj->world_y = muzzle_y + oy;
		proj->world_z = muzzle_z + oz;

		proj->moveX = shooter->moveX;
		proj->moveY = shooter->moveY;
		proj->moveZ = shooter->moveZ;
		proj->side_x = shooter->side_x;
		proj->side_y = shooter->side_y;
		proj->side_z = shooter->side_z;
		proj->up_x = shooter->up_x;
		proj->up_y = shooter->up_y;
		proj->up_z = shooter->up_z;
		proj->fwd_x = shooter->fwd_x;
		proj->fwd_y = shooter->fwd_y;
		proj->fwd_z = shooter->fwd_z;
		proj->orient_dirty = 0;
		proj->move_dirty = 0;
	}

	/* Pair the slot with a warhead record: no homing (tier 0), no
	 * target. MOVE's genus-6/7 step skips homing when target_obj is
	 * 0xFFFF. */
	uint16_t wh = slot - NUM_CRAFTS;
	warheads[wh].homing_tier = 0;
	warheads[wh].target_obj = 0xFFFF;
	proj->craft_ptr = (CraftData*)&warheads[wh];

	/* actor_id identifies the projectile; param0 and param1 identify its
	 * projectile type and shooter slot for the renderer's spawn effect. */
	{
		TieEvent ev = {
			.kind     = TIE_EVENT_LASER_SPAWN,
			.actor_id = proj->idnumber,
			.world_pos = {
				proj->world_x,
				proj->world_y,
				proj->world_z,
			},
			.param0   = (int32_t)projectile_type,
			.param1   = (int32_t)shooter_obj_idx,
		};
		TieSnapshotBuilder_PushEvent(&ev);
	}
	TIE_FLIGHT_TRACE_WEAPON_SPAWN(slot, shooter_obj_idx, 0xFFFFu);

	return slot;
}

/* ================================================================== */
/* laser_createprojectilefromstatic                                    */
/* ================================================================== */
// FUNCTION: TIE 0x2E514
uint16_t laser_createprojectilefromstatic(uint16_t static_obj_idx, uint16_t shooter_obj_idx) {
	uint16_t fg_idx = staticobjects[static_obj_idx].fg_idx;
	uint8_t warhead_kind = fg_array[fg_idx].warhead;
	uint16_t ptype = warheadconvert[warhead_kind];
	if (ptype == 0)
		return 0xFFFF;

	uint16_t slot = create_findslot(7 /* GENUS_PROJECTILE_NPC */);
	if (slot == 0xFFFF) {
		/* Fallback: scan the upper half of the warhead slot range for a
		 * non-warhead same-side occupant to evict. Retaliation
		 * pre-empts ordinary lasers of the same faction.
		 * Demo: [44, 76); retail: [48, 80). = NUM_CRAFTS+16..WARHEAD_SLOT_END. */
		uint8_t want_side = fg_array[fg_idx].side;
		for (slot = NUM_CRAFTS + 16; slot < WARHEAD_SLOT_END; ++slot) {
			const uint16_t occupant_species = objects[slot].ship_idx;
			const unsigned int occupant_projectile_type_idx =
				(unsigned int)occupant_species - WEAPON_SPECIES_BASE;
			/* Projectile slots can still contain in-place impact animations. */
			const uint8_t occupant_is_warhead =
				occupant_projectile_type_idx < WARHEAD_TYPE_COUNT &&
				projectile_is_warhead_type[occupant_projectile_type_idx];
			if (!occupant_is_warhead && objects[slot].side == want_side)
				break;
		}
	}
	if (slot == WARHEAD_SLOT_END)
		return 0xFFFF;

	FlightObject* p = &objects[slot];
	p->category = 1;
	p->genus = 7; /* GENUS_PROJECTILE_NPC */
	p->ship_idx = (uint8_t)ptype;
	p->age_ticks = 1;

	/* Tag self_idx with +0x3800 so create_getworldposition (and
	 * downstream resolvers) route the reference through the static
	 * table instead of the craft table. */
	p->self_idx = (int16_t)(static_obj_idx + 0x3800);
	p->ship_type_override = staticobjects[static_obj_idx].species;
	p->side = fg_array[fg_idx].side;
	p->heading = 0;
	p->roll = 0;
	p->pitch = 0;

	const unsigned int ptype_idx = laser_species_idx(ptype);
	int16_t proj_speed = (int16_t)projectilevelocity[ptype_idx];
	warheads[slot - NUM_CRAFTS].min_speed = (uint16_t)proj_speed;
	p->collision_radius = (int16_t)projectileweight[ptype_idx];
	p->death_timer = (int16_t)(236 * projectilelife[ptype_idx]);
	p->current_speed = proj_speed;

	create_getworldposition((uint16_t)(static_obj_idx + 0x3800), 0);
	p->world_x_prev = worldlocx;
	p->world_y_prev = worldlocy;
	p->world_z_prev = worldlocz + 384;
	p->world_x = worldlocx;
	p->world_y = worldlocy;
	p->world_z = worldlocz + 384;

	uint16_t wh = slot - NUM_CRAFTS;
	warheads[wh].homing_tier = (uint8_t)((math2_getrandom() & 3) + 3);
	warheads[wh].target_obj = shooter_obj_idx;
	p->craft_ptr = (CraftData*)&warheads[wh];

	TIE_FLIGHT_TRACE_WEAPON_SPAWN(slot, (uint16_t)(static_obj_idx + OBJ_REF_STATIC_BASE), shooter_obj_idx);
	laser_warnplayer(wh);
	return slot;
}

/* ================================================================== */
/* laser_firemissile                                                   */
/* ================================================================== */
// FUNCTION: TIE 0x2DF68
uint16_t laser_firemissile(uint16_t shooter_obj_idx, uint16_t weapon_slot_idx, uint16_t projectile_type,
						   uint16_t group_idx) {
	WeaponSlot* ws = &craftptr->weapon_slots[weapon_slot_idx];
	if (ws->type == 0)
		return 0xFFFF; /* no weapon */
	if (ws->ammo == 0)
		return 0xFFFF; /* empty rack */

	uint16_t slot = laser_createprojectile(shooter_obj_idx, weapon_slot_idx, projectile_type);
	if (slot == 0xFFFF)
		return 0xFFFF;

	/* Left/right tube toggle (only meaningful for group 0/1 racks). */
	if (group_idx < 2)
		craftptr->missile_armed[group_idx] ^= 0x80u;

	craftptr->warhead_fired++;
	if (shooter_obj_idx == pstate.object_idx)
		pstate.player_warhead_fired++;

	fsfx_triggerlasersfx(slot);

	if (!inflight_unlimited || shooter_obj_idx != pstate.object_idx)
		craftptr->weapon_slots[weapon_slot_idx].ammo--;

	uint16_t wh = slot - NUM_CRAFTS;

	if (group_idx < 2) {
		/* Homing tier = current lock strength (missile_count_total, a
		 * frame-accumulated counter) divided by 236 (ticks-per-sec).
		 * Clamp at 6 tiers. */
		int16_t t = (int16_t)(craftptr->missile_count_total / 236);
		if (t > 6)
			t = 6;
		warheads[wh].homing_tier = (uint8_t)t;
	}

	if (shooter_obj_idx == pstate.object_idx) {
		warheads[wh].target_obj = pstate.target_obj_idx;
		warheads[wh].sub_obj_idx = (uint16_t)pstate.radar_target1;
	} else {
		warheads[wh].target_obj = (uint16_t)craftptr->ai_target_ref;
		warheads[wh].sub_obj_idx = (uint16_t)craftptr->link_target_2E;
	}

	TIE_FLIGHT_TRACE_TARGET_CHANGE(slot, 0xFFFFu, warheads[wh].target_obj);
	laser_warnplayer(wh);
	return wh;
}

/* ================================================================== */
/* laser_firelasersystem                                               */
/* ================================================================== */
// FUNCTION: TIE 0x2D9AC
void laser_firelasersystem(uint16_t shooter_obj_idx, uint16_t group_idx) {
	craftptr = objects[shooter_obj_idx].craft_ptr;
	uint8_t species_idx = craftptr->species_idx;
	if (craftptr->ai_anim_flags)
		return; /* craft is locked in animation */

	/* The byte at +0xD5 + group_idx is a fire mode:
	 *   0 = not firing
	 *   1 = single-shot
	 *   2 = alternating
	 *   3 = full burst
	 *
	 * The player cycles it in USER_inputforplane; AI sets it to 1..3.
	 */
	uint8_t* cfg = (uint8_t*)craftptr + group_idx; /* + {213,215,217} style */
	uint8_t fire_mode = cfg[0xD5];                 /* aka laser_owner_player[group_idx] */

	uint16_t start_slot = 0;
	uint16_t end_slot = 0;
	uint16_t slot_stride = 1;
	int32_t shots_remaining = 0;

	uint8_t laser_start = spec_data[species_idx].laser_start[group_idx];
	uint8_t laser_end = spec_data[species_idx].laser_end[group_idx];

	if (fire_mode == 1) {
		/* single-shot: fire laser_first_slot[group], bump/wrap. */
		start_slot = cfg[0xD9]; /* laser_first_slot[group_idx] */
		end_slot = start_slot;
		cfg[0xD9] = (uint8_t)(start_slot + 1);
		if (cfg[0xD9] > laser_end)
			cfg[0xD9] = laser_start;
		shots_remaining = 1;
		slot_stride = 1;
	} else if (fire_mode == 2) {
		/* alternating: flip even/odd and fire every-other. */
		start_slot = cfg[0xD9];
		cfg[0xD9] = (uint8_t)(start_slot ^ 1);
		if (cfg[0xD9] > laser_end)
			cfg[0xD9] = laser_start;
		end_slot = laser_end;
		shots_remaining = (laser_end - laser_start + 1) / 2;
		slot_stride = 2;
	} else if (fire_mode == 3) {
		/* full burst: all slots in the group. */
		start_slot = laser_start;
		end_slot = laser_end;
		shots_remaining = laser_end - start_slot + 1;
		slot_stride = 1;
	} else {
		return; /* mode 0 or unknown -- no fire */
	}

	uint16_t shots_fired = 0;
	uint16_t final_laser_type = 0;

	for (uint16_t i = start_slot; i <= end_slot; i += slot_stride) {
		WeaponSlot* ws = &craftptr->weapon_slots[i];
		if (ws->type == 0 || (int8_t)ws->charge <= 0)
			goto next;

		uint16_t ltype = spec_data[species_idx].laser_type[group_idx];
		if (ws->charge >= 64)
			ltype++; /* charged variant */
		final_laser_type = ltype;

		uint16_t pslot = laser_createprojectile(shooter_obj_idx, i, ltype);
		if (pslot == 0xFFFF)
			goto next;

		if (shooter_obj_idx == pstate.object_idx) {
			if (!inflight_unlimited)
				craftptr->weapon_slots[i].charge -= 4;
		} else {
			craftptr->weapon_slots[i].charge -= 1;
		}
		if ((int8_t)craftptr->weapon_slots[i].charge < 0)
			craftptr->weapon_slots[i].charge = 0;

		if (shots_fired < 2)
			fsfx_triggerlasersfx(pslot);

		uint16_t wh = pslot - NUM_CRAFTS;
		if (shooter_obj_idx == pstate.object_idx)
			warheads[wh].target_obj = pstate.target_obj_idx;
		else
			warheads[wh].target_obj = (uint16_t)craftptr->ai_target_ref;

		shots_fired++;
	next:
		if (--shots_remaining == 0)
			break;
	}

	if (final_laser_type == 141 || final_laser_type == 142) {
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
// FUNCTION: TIE 0x2DD50
void laser_firerocketsystem(uint16_t shooter_obj_idx, uint16_t group_idx) {
	craftptr = objects[shooter_obj_idx].craft_ptr;

	/* first_slot = hp_idx of the group's first tube. */
	uint16_t first_slot = spec_data[craftptr->species_idx].missile_start[group_idx];

	uint8_t* cfg = (uint8_t*)craftptr + group_idx;
	uint8_t mode_byte = cfg[0xE3];    /* missile_armed[group_idx] */
	uint8_t warhead_kind = cfg[0xE1]; /* warhead_type[group_idx]  */

	uint16_t shots_fired = 0;
	int ammo_but_no_pool_slot = 0;

	if ((mode_byte & 0x7F) == 3) {
		/* DUAL: fire first_slot, then first_slot+1. */
		if (laser_firemissile(shooter_obj_idx, first_slot, warhead_kind, group_idx) == 0xFFFF) {
			if (craftptr->weapon_slots[first_slot].ammo)
				ammo_but_no_pool_slot = 1;
		} else {
			shots_fired = 1;
		}

		if (laser_firemissile(shooter_obj_idx, (uint16_t)(first_slot + 1), craftptr->warhead_type[group_idx],
							  group_idx) != 0xFFFF) {
			shots_fired++;
		} else if (craftptr->weapon_slots[first_slot + 1].ammo) {
			ammo_but_no_pool_slot = 1;
		}
	} else if (mode_byte & 0x80) {
		/* ALTERNATING: fire first_slot+1 (toggle set by previous shot). */
		if (laser_firemissile(shooter_obj_idx, (uint16_t)(first_slot + 1), warhead_kind, group_idx) !=
			0xFFFF) {
			shots_fired = 1;
		} else if (craftptr->weapon_slots[first_slot + 1].ammo) {
			ammo_but_no_pool_slot = 1;
		}
	} else {
		/* SINGLE / initial shot. */
		if (laser_firemissile(shooter_obj_idx, first_slot, warhead_kind, group_idx) != 0xFFFF) {
			shots_fired = 1;
		} else if (craftptr->weapon_slots[first_slot].ammo) {
			ammo_but_no_pool_slot = 1;
		}
	}

	craftptr->missile_state[group_idx] = 472; /* ~2s reload cooldown */

	if (shooter_obj_idx == pstate.object_idx && !ammo_but_no_pool_slot) {
		argtable[0] = (uint16_t)user_mapmissiletomessage(
			pstate.player_craft->warhead_type[pstate.player_weapon_group], 0);
		msg_messageprintf((MsgTemplate)(shots_fired + 106));
	}
}

/* ================================================================== */
/* laser_fireplayerweapon                                              */
/* ================================================================== */
// FUNCTION: TIE 0x2D7C8
void laser_fireplayerweapon(void) {
	CraftData* pc = pstate.player_craft;
	if (pstate.player_weapon_mode) {
		/* Missile mode. */
		int16_t cd = (int16_t)pc->missile_state[pstate.player_weapon_group];
		if (cd) {
			cd -= (int16_t)frameticks;
			if (cd < 0)
				cd = 0;
		}
		if (cd < (int16_t)frameticks) {
			if (cd)
				pc->missile_state[pstate.player_weapon_group] -= frameticks;
			else
				pc->missile_state[pstate.player_weapon_group] = 0;

			if (pc->status_flags & 8) {
				laser_firerocketsystem(pstate.object_idx, pstate.player_weapon_group);
				pc->missile_state[pstate.player_weapon_group] += frameticks;
			} else {
				argtable[0] = 31;
				argtable[1] = 25;
				msg_messageprintf(MSG_SYSTEM_STATUS);
			}
		}
	} else {
		int16_t cd = (int16_t)pc->laser_cooldown[pstate.player_weapon_group];
		if (cd) {
			cd -= (int16_t)frameticks;
			if (cd < 0)
				cd = 0;
		}
		if (cd < (int16_t)frameticks) {
			if (cd)
				pc->laser_cooldown[pstate.player_weapon_group] -= frameticks;
			else
				pc->laser_cooldown[pstate.player_weapon_group] = 0;

			if (pc->status_flags & 0x10) {
				laser_firelasersystem(pstate.object_idx, pstate.player_weapon_group);
				pc->laser_cooldown[pstate.player_weapon_group] += frameticks;
			} else {
				argtable[0] = (uint16_t)(pstate.player_weapon_group + 29);
				argtable[1] = 25;
				msg_messageprintf(MSG_SYSTEM_STATUS);
			}
		}
	}
}

/* ================================================================== */
/* laser_weaponsfire -- per-frame master dispatch.                     */
/* ================================================================== */
// FUNCTION: TIE 0x2CDD0
void laser_weaponsfire(void) {
	CraftData* saved_craftptr = craftptr;

	/* ----- Phase 1: missile lock gauge --------------------------- */
	if (pstate.player_weapon_mode) {
		uint16_t missile_hp = spec_data[pstate.player_spec_num].missile_start[pstate.player_weapon_group];
		int has_ammo = (int)(pstate.player_craft->weapon_slots[missile_hp].ammo +
							 pstate.player_craft->weapon_slots[missile_hp + 1].ammo);

		if (pstate.target_obj_idx == 0xFFFF || !has_ammo) {
			pstate.radar_subtarget_state = 0;
			pstate.player_craft->missile_count_total = 0;
		} else {
			pai_distancebetween(pstate.object_idx, pstate.target_obj_idx);

			uint32_t lock_range = LOCK_RANGE_FIGHTER;
			if (pstate.target_obj_idx < NUM_CRAFTS) {
				uint8_t tgenus = objects[pstate.target_obj_idx].genus;
				if (tgenus == 3 || tgenus == 4 || tgenus == 5)
					lock_range = LOCK_RANGE_CAPSHIP;
			}

			if (lock_range > (uint32_t)trig2_polardistance && user_targetincross(pstate.target_obj_idx, 0)) {
				pstate.player_craft->missile_count_total += frameticks;

				uint16_t thresh =
					(pstate.player_spec_num == spec_getspecnum(0xC)) ? (uint16_t)590 : (uint16_t)1180;
				pstate.radar_subtarget_state = (pstate.player_craft->missile_count_total < thresh) ? 1 : 2;
			} else {
				if ((int16_t)pstate.player_craft->missile_count_total > 0) {
					int16_t mct = (int16_t)(pstate.player_craft->missile_count_total - frameticks);
					pstate.player_craft->missile_count_total = mct;
					if (mct < 0)
						pstate.player_craft->missile_count_total = 0;
				}
				pstate.radar_subtarget_state = 0;
			}
		}
	}
	craftptr = saved_craftptr;

	/* ----- Phase 2: beam weapon -------------------------------- */
	uint16_t beam_target = 0xFFFF;
	uint32_t best_beamdist = 0x20000;
	int beam_firing_now = 0;

	/* Retail also requires status_flags & 0x100 (beam subsystem online —
	 * cleared when ion-drained or boarded) and !player_ejected (post-eject
	 * the cockpit is gone). Demo had only the inner two checks. */
	if ((pstate.player_craft->status_flags & 0x100) && (pstate.player_craft->beam_state & 0x80) &&
		pstate.player_craft->beam_charge > 0 && !player_ejected) {
		if (!timers[TIMER_LASER_BEAM_DRAIN]) {
			int16_t bc = (int16_t)(pstate.player_craft->beam_charge - 83);
			timers[TIMER_LASER_BEAM_DRAIN] = 59;
			if (bc < 0)
				bc = 0;
			pstate.player_craft->beam_charge = bc;
		}

		for (uint16_t i = 0; i < NUM_CRAFTS; ++i) {
			uint8_t ship_idx = objects[i].ship_idx;
			craftptr = saved_craftptr;
			if (!ship_idx || i == pstate.object_idx)
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
		beam_firing_now = 1;
	} else {
		bluetarget = 0xFFFF;
	}
	craftptr = saved_craftptr;
	fsfx_triggerbeamsfx(beam_firing_now);

	/* ----- Phase 3: per-second status update -------------------- */
	CraftData* cp_outer = craftptr;
	if (!timers[TIMER_LASER_STATUS]) {
		timers[TIMER_LASER_STATUS] = 236;

		for (uint16_t obj_i = 0; obj_i < NUM_CRAFTS; ++obj_i) {
			craftptr = cp_outer;
			if (!objects[obj_i].ship_idx)
				continue;
			if (objects[obj_i].category)
				continue;

			uint8_t g = objects[obj_i].genus;
			if (g != 0 /* GENUS_FIGHTER */ && g != 1 /* GENUS_TRANSPORT */)
				continue;

			CraftData* c = objects[obj_i].craft_ptr;

			if (obj_i != pstate.object_idx) {
				if (c->subsystem_active & 1) {
					uint16_t base = (uint16_t)(2 * spec_data[c->species_idx].shield_points);
					uint8_t pwr;
					if (c->forward_shield > 0) {
						int pct = math2_percentage((uint16_t)c->forward_shield, base);
						if (pct >= 0x4000)
							pwr = (pct >= 0xFFFF) ? 2 : 3;
						else
							pwr = 4;
					} else {
						pwr = 4;
					}
					c->shield_power = pwr;
				}

				/* Charges can be transiently negative (a shot fires by
				 * driving the byte below 0; regen pulls it back). Retail
				 * sign-extends each byte (movsx) and divides signed.
				 * Using uint16_t here would force unsigned division and
				 * mis-classify drained banks as fully charged. */
				int16_t charge_sum = 0;
				uint16_t weap_cnt = 0;
				for (uint16_t j = 0; j < (uint16_t)c->weapon_group_cnt; ++j) {
					WeaponSlot* w = &c->weapon_slots[j];
					if (w->type) {
						weap_cnt++;
						charge_sum += (int8_t)w->charge;
					}
				}
				if (weap_cnt) {
					int16_t avg = (int16_t)(charge_sum / (int16_t)weap_cnt);
					uint8_t pwr = (avg >= 32) ? ((avg >= 96) ? 2 : 3) : 4;
					c->laser_power = pwr;
				}
			}

			craftptr = c;

			/* Shield regen (if the subsystem bit is set). */
			if (c->status_flags & 1) {
				int16_t delta = (int16_t)(20 * (c->shield_power - 2));
				if (delta) {
					uint16_t shield_side;
					int16_t shield_delta;
					if (c->is_player_craft) {
						if (c->is_player_craft == 2) {
							/* All to rear. */
							shield_delta = delta;
							shield_side = 1;
						} else {
							/* Half to front, half to rear. */
							int16_t half = (int16_t)(delta / 2);
							laser_chargeshields(obj_i, 0, half);
							shield_delta = half;
							shield_side = 1;
						}
					} else {
						shield_delta = delta;
						shield_side = 0;
					}
					laser_chargeshields(obj_i, shield_side, shield_delta);
				}
			}

			CraftData* cl = craftptr;

			/* Laser-charge regen. */
			if (cl->status_flags & 0x10) {
				for (uint16_t k = 0; k < (uint16_t)cl->weapon_group_cnt; ++k) {
					uint8_t type = cl->weapon_slots[k].type;
					if (!type || type == 2)
						continue;

					int16_t power = (int16_t)(cl->laser_power - 2);
					if (!cl->slam_active)
						power = (int16_t)(cl->laser_power - 6);
					int16_t step = (int16_t)(2 * power);

					int8_t before = (int8_t)cl->weapon_slots[k].charge;
					int8_t after = (int8_t)(before + step);
					cl->weapon_slots[k].charge = (uint8_t)after;

					if (step < 0 && after < 0)
						cl->weapon_slots[k].charge = 0;
					if (step > 0 && (cl->weapon_slots[k].charge & 0x80u))
						cl->weapon_slots[k].charge = 127;
				}
			}

			/* SLAM overdrive: when it's off (==0) and all weapon
			 * charges have drained, latch it DISENGAGED (-1). */
			if (!cl->slam_active) {
				int any = 0;
				for (uint16_t j = 0; j < (uint16_t)cl->weapon_group_cnt; ++j) {
					if ((int8_t)cl->weapon_slots[j].charge > 0) {
						any = 1;
						break;
					}
				}
				if (!any) {
					cl->slam_active = SLAM_DISENGAGED;
					msg_messageprintf(MSG_OVERDRIVE_DISENGAGED);
					fsfx_triggersfx(0x6C, 0xFFFF);
				}
			}
			cp_outer = cl;
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
	for (uint16_t n = 0; n < NUM_CRAFTS; ++n) {
		if (!objects[n].ship_idx)
			continue;
		if (objects[n].category)
			continue;

		CraftData* c = objects[n].craft_ptr;
		if ((c->beam_state & 2) || c->ion_drain_timer)
			continue;

		craftptr = c;

		for (uint16_t g = 0; g < (uint16_t)c->laser_group_cnt; ++g) {
			const int16_t cooldown_before = (int16_t)c->laser_cooldown[g];
			int16_t cd = cooldown_before;
			if (cd) {
				cd -= (int16_t)frameticks;
				if (cd < 0)
					cd = 0;
				c->laser_cooldown[g] = (uint16_t)cd;
			}
			const bool ready = TieFlightTiming_IsHighRate() ? cooldown_before <= (int16_t)frameticks
															: cd < (int16_t)frameticks;
			if (n != pstate.object_idx && ready && c->laser_owner_player[g]) {
				if ((c->status_flags & 0x10) && !c->flight_flag)
					laser_firelasersystem(n, g);
				c = craftptr;

				--c->laser_burst_remaining[g];
				c->laser_cooldown[g] +=
					2 * (TieFlightTiming_IsHighRate() ? TieFlightTiming_CompatibilityTicks() : frameticks);
				if (c->laser_burst_remaining[g] == 0)
					c->laser_owner_player[g] = 0; /* burst spent: stop AI firing this group */
			}
		}

		/* Turrets (weapon_slots[k].type == 2). */
		for (uint16_t k = 0; k < (uint16_t)c->weapon_group_cnt; ++k) {
			uint8_t ttype = c->weapon_slots[k].type;
			craftptr = c;
			if (ttype == 2) {
				uint16_t ttarget = c->weapon_slots[k].target_obj;
				if (ttarget != 0xFFFF)
					starship_firelasergunner(n, k, ttarget);
			}
			c = craftptr;
		}

		/* Missile cooldown decrement. */
		for (uint16_t m = 0; m < (uint16_t)c->missile_group_cnt; ++m) {
			uint16_t mcd = c->missile_state[m];
			if (mcd) {
				int16_t nmcd = (int16_t)(mcd - frameticks);
				if (nmcd < 0)
					nmcd = 0;
				c->missile_state[m] = (uint16_t)nmcd;
			}
		}
	}

	/* ----- Phase 5: mine-turret update ------------------------- */
	for (uint16_t s = 0; s < 0x40u; ++s) {
		if (staticobjects[s].species && staticobjects[s].ship_class == 8)
			static_updatemineguns(s);
	}
}
