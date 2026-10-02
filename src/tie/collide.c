#include "tie/collide.h"
#include "tie/create.h"
#include "tie/draw.h"
#include "tie/edition.h"
#include "tie/fediskio.h"
#include "tie/fscript.h"
#include "tie/fsfx.h"
#include "tie/fview.h"
#include "tie/gate.h"
#include "tie/laser.h"
#include "tie/math2.h"
#include "tie/math2_wide.h"
#include "tie/modelmesh.h"
#include "tie/msg.h"
#include "tie/msg_templates.h"
#include "tie/pai.h"
#include "tie/panel.h"
#include "tie/score.h"
#include "tie/shipext.h"
#include "tie/spec.h"
#include "tie/starship.h"
#include "tie/static.h"
#include "tie/tie.h"
#include "tie/user.h"
#include "tie_runtime/diagnostics/flight_trace.h"
#include "tie_runtime/runtime/inflight_state.h"
#ifdef TIE_MODERN
#include "tie_runtime/snapshot/snapshot.h"
#include "tie_runtime/snapshot/snapshot_internal.h"
#endif

#include <stdint.h>
#include <stdlib.h>

/* ---------- Module-private static state ---------- */

/*
 * targetcomputerflag (int, static per watdbg).
 * One-shot override set by collide_targetinrange to force
 * collide_lasercraftcollide onto the box-test path even for
 * capital-ship-sized targets. Cleared on consumption.
 */
// GLOBAL: TIE95 0xC1780
// GLOBAL: TIE98 0x50AA4C
static int32_t targetcomputerflag = 0;

/* ---------- Module globals (defined here, declared extern in collide.h) ---------- */

/*
 * Per-subsystem bitmasks AND'd against CraftData.status_flags during
 * missile/warhead overflow damage. Verbatim from the binary's .data
 * segment. Values are ordered by random pick index, NOT by
 * subsystem id -- so e.g. systemmask[0]=0x002 disables a different
 * subsystem than systemmask[1]=0x080.
 */
// GLOBAL: TIE95 0xC1784
// GLOBAL: TIE98 0x4DF2B0
int16_t systemmask[10] = { 0x002, 0x080, 0x010, 0x020, 0x040, 0x004, 0x001, 0x008, 0x100, 0x200 };

/* MSG_SYSTEM_STATUS substring id per subsystem. */
// GLOBAL: TIE95 0xC1798
// GLOBAL: TIE98 0x4DF2C8
uint8_t damagemsg[10] = { 34, 36, 166, 28, 27, 33, 35, 165, 166, 166 };

/* Repair countdown duration per subsystem in ticks. */
// GLOBAL: TIE95 0xC17A2
// GLOBAL: TIE98 0x4DF2D8
int16_t repairtime[10] = { 180, 300, 45, 25, 100, 30, 50, 60, 60, 60 };

/*
 * Random instrument disable bitmasks AND'd against ~working_subsystems.
 * Index 0 is gated against mission.train_craft_type to avoid disabling the
 * forward shield in briefing/training/combat. TIE95 carries a 17th entry
 * (0, never selected by the 4-bit random pick); TIE98 has only 16.
 */
// GLOBAL: TIE95 0xC17B6
// GLOBAL: TIE98 0x4DF2F0
int16_t instrumentdisable[] = { 0x200, 0x040, 0x020, 0x006, 0x400,  0x180, 0x010, 0x008,
								0x800, 0x180, 0x020, 0x006, 0x1000, 0x001, 0x020, 0x008,
#ifndef TIE98
								0x000
#endif
};

/* ---------- External cross-module declarations -----------
 * tie.c-owned globals consumed by COLLIDE: timers[TIMER_SHIELD_FLASH] /
 * timers[TIMER_SHIELD_OVERLOAD] (slots in the shared timers[20] bank,
 * declared in tie.h alongside the mission clock `_date` and pstate
 * session counters). */

/*
 * "Spawn snapshot" / camera-respawn position arrays. The binary
 * stores 4 slots each (the index-3 entry is the active spawn pose
 * used by collide_collisions on a briefing/training/combat collision).
 * Owned by tie.c per watdbg.
 */

/*
 * The retail 'projectilevelocity_base' anchor at 0xC534A is the same
 * table as 'projectilevelocity' at 0xC545C, just rebased so that
 * projectilevelocity_base[species*2] == projectilevelocity[species-137].
 * collide_targetinrange therefore reads (laser_type - WEAPON_SPECIES_BASE)
 * out of projectilevelocity[] -- no separate lookahead table exists.
 */

/* ---------- 1. collide_makeobjectexplosion ---------- */
// FUNCTION: TIE95 0x15A24
char collide_makeobjectexplosion(uint16_t obj_idx, uint16_t ship_variant) {
	TIE_FLIGHT_TRACE_EXPLOSION(obj_idx, ship_variant);

	objects[obj_idx].ship_idx = ship_variant;
	objects[obj_idx].anim_frame = 2;
	objects[obj_idx].genus = GENUS_EXPLOSION;
	objects[obj_idx].damage_state = 0;
	objects[obj_idx].category = 5;
	objects[obj_idx].age_ticks = 0;
	objects[obj_idx].current_speed = 0;
	objects[obj_idx].death_timer = 0;
	objects[obj_idx].roll = 0;
	objects[obj_idx].spin_rate = 0;
	objects[obj_idx].orient_dirty = 1;

	/* EXPLOSION event fired when a craft or projectile enters its death sequence.
	 * transitions to an explosion sprite — covers move_moveobjects' death-
	 * timer dispatch (move.c calls collide_makeobjectexplosion via
	 * dispatch_death) and every direct kill path in collide.c. param0
	 * encodes ship_variant so the renderer can pick the right effect. */
#ifdef TIE_MODERN
	{
		FlightObject* o = &objects[obj_idx];
		TieEvent ev = {
			.kind     = TIE_EVENT_EXPLOSION,
			.actor_id = o->idnumber,
			.world_pos = {
				o->world_x,
				o->world_y,
				o->world_z,
			},
			.param0   = (int32_t)ship_variant,
			.param1   = 0,
		};
		TieSnapshotBuilder_PushEvent(&ev);
	}
#endif

	/* Random sfx in [19..22] (4 craft-explosion variants). */
	return fsfx_triggersfx((math2_getrandom() & 3) + 19, obj_idx);
}

/* ---------- 2. collide_roughdistance3du ---------- */
// FUNCTION: TIE95 0x15AB8
uint32_t collide_roughdistance3du(uint32_t abs_dx, uint32_t abs_dy, uint32_t abs_dz) {
	if (abs_dx > abs_dy && abs_dx > abs_dz)
		return abs_dx + (abs_dy / 4) + (abs_dz / 4);
	if (abs_dy > abs_dx && abs_dy > abs_dz)
		return abs_dy + (abs_dx / 4) + (abs_dz / 4);
	return abs_dz + (abs_dx / 4) + (abs_dy / 4);
}

/* ---------- 3. collide_roughdistance3d ---------- */
// FUNCTION: TIE95 0x15AF0
int32_t collide_roughdistance3d(int32_t dx, int32_t dy, int32_t dz) {
	if (dx < 0)
		dx = -dx;
	if (dy < 0)
		dy = -dy;
	if (dz < 0)
		dz = -dz;
	if (dx > dy && dx > dz)
		return dx + (dy >> 2) + (dz >> 2);
	if (dy > dx && dy > dz)
		return dy + (dx >> 2) + (dz >> 2);
	return dz + (dx >> 2) + (dy >> 2);
}

/* ---------- 4. collide_updatehits ---------- */
// FUNCTION: TIE95 0x164D0
void collide_updatehits(uint16_t projectile_obj_idx, uint16_t hit_count) {
	uint16_t self_idx = objects[projectile_obj_idx].self_idx;
	uint16_t ship_idx = objects[projectile_obj_idx].ship_idx;
	CraftData* craft;

	/* Static shooters use encoded references and have no craft hit counters. */
	if (self_idx >= NUM_CRAFTS)
		return;

	craft = objects[self_idx].craft_ptr;

	switch (ship_idx) {
		case 0x89:
		case 0x8A:
		case 0x8B:
		case 0x8C:
			/* Laser projectile. */
			craft->laser_hit++;
			if (self_idx == pstate.object_idx)
				pstate.player_laser_hit++;
			break;
		case 0x8D:
		case 0x8E:
			/* Missile projectile. */
			craft->missile_hit++;
			if (self_idx == pstate.object_idx)
				pstate.player_missile_hit++;
			break;
		case 0x8F:
		case 0x90:
		case 0x94:
		case 0x95:
		case 0x96:
		case 0x97:
		case 0x98:
		case 0x99:
		case 0x9A:
			/* Warhead/torpedo. */
			craft->warhead_hit++;
			if (self_idx == pstate.object_idx)
				pstate.player_warhead_hit++;
			break;
	}
}

/* ---------- 5. collide_updatekills ---------- */
// FUNCTION: TIE95 0x16218
void collide_updatekills(uint16_t shooter_obj_idx, uint16_t victim_obj_idx) {
	CraftData* shooter_craft;
	uint16_t victim_specnum;
	uint8_t* kills_arr;
	int16_t voice_threshold;
	uint8_t new_count;

	if (shooter_obj_idx >= NUM_CRAFTS)
		return;

	shooter_craft = objects[shooter_obj_idx].craft_ptr;

	/* Generic 'attribute kill but no specific victim' path. */
	if (victim_obj_idx == 0xFFFFu) {
		shooter_craft->total_kills++;
		if (shooter_obj_idx == pstate.object_idx)
			pstate.player_total_kills++;
		return;
	}

	victim_specnum = spec_getspecnum(objects[victim_obj_idx].ship_idx);

	kills_arr = shooter_craft->kills_by_species;
	new_count = (uint8_t)(kills_arr[victim_specnum] + 1);
	kills_arr[victim_specnum] = new_count;
	if (new_count == 0)
		kills_arr[victim_specnum] = 0xFF;

	/* Player-only path: five win-condition cuts pick the congratulation
	 * voice threshold. Each cut: 0/10 -> 24576, 2 -> -4096, else keep. */
	if (shooter_obj_idx == pstate.object_idx) {
		voice_threshold = 0;
		switch (fg_array[objects[victim_obj_idx].fg_idx].pri_win_cond) {
			case 0:
			case 10:
				voice_threshold = 24576;
				break;
			case 2:
				voice_threshold = -4096;
				break;
		}
		switch (fg_array[objects[victim_obj_idx].fg_idx].sec_win_cond) {
			case 0:
			case 10:
				voice_threshold = 24576;
				break;
			case 2:
				voice_threshold = -4096;
				break;
		}
		switch (fg_array[objects[victim_obj_idx].fg_idx].bonus_cond) {
			case 0:
			case 10:
				voice_threshold = 24576;
				break;
			case 2:
				voice_threshold = -4096;
				break;
		}
		switch (cut[0].subcond[0].cond) {
			case 0:
			case 10:
				voice_threshold = 24576;
				break;
			case 2:
				voice_threshold = -4096;
				break;
		}
		/* Binary reads a byte at cut[0].subcond[1].cond (primary goal's
		 * second subcondition's cond code) -- the binary emitted a
		 * misaligned dword load from 0xF4809 with >>24, but the resolved
		 * address lands on the same byte. */
		switch (cut[0].subcond[1].cond) {
			case 0:
			case 10:
				voice_threshold = 24576;
				break;
			case 2:
				voice_threshold = -4096;
				break;
		}

		if (objects[pstate.object_idx].side == objects[victim_obj_idx].side) {
			/* Friendly-fire kill: announce + bump counter. */
			messageside = objects[pstate.object_idx].side;
			pstate.friendly_kill_count++;
			msg_messageprintf(MSG_FRIENDLY_KILL);
		} else {
			/* Player-side per-species kill increment (enemy kills only). */
			uint16_t cur = (uint16_t)(pstate.player_kills_per_species[victim_specnum] + 1);
			pstate.player_kills_per_species[victim_specnum] = cur;
			if (cur == 0)
				pstate.player_kills_per_species[victim_specnum] = 0xFFu;
			if ((uint16_t)math2_getrandom() < (uint16_t)voice_threshold) {
				if (fsfx_speakeravailable())
					fsfx_speakcongrats();
			}
		}
	}

	/* Side-aware mission.kills_losses[6][69]: 69 species per side. The
	 * binary at 0x16469 uses kills_losses[0] as a base anchor and indexes
	 * with (side*69 + specnum) flat-byte stride; in C that's UB once the
	 * index exceeds 69, so use proper 2D indexing instead. */
	{
		uint8_t side = objects[victim_obj_idx].side;
		uint8_t v = (uint8_t)(mission.kills_losses[side][victim_specnum] + 1);
		mission.kills_losses[side][victim_specnum] = v;
		if (v == 0)
			mission.kills_losses[side][victim_specnum] = 0xFF;
	}
}

/* ---------- 6. collide_checkboxcollision ---------- */
/* Liang-Barsky-style swept-segment vs swept-AABB-extruded-by-radius
 * clip. Reads the laser/laserold and craft/craftold globals; writes
 * collide{x,y,z}off + returns 0xFFFF on hit, 0 on miss. */
// FUNCTION: TIE95 0x138C4
int32_t collide_checkboxcollision(int32_t radius) {
	int32_t ydenom;
	int32_t craftdz;
	int32_t ymax;
	int32_t xmin;
	int32_t zdenom;
	int32_t swap;
	int32_t xvel;
	int32_t ymin;
	int32_t laserdx;
	int32_t zmax;
	int32_t laserdy;
	int32_t zvel;
	int32_t xdist;
	int32_t xmax;
	int32_t ydist;
	int32_t yvel;
	int32_t zmin;
	int32_t craftdx;
	int32_t zdist;
	int32_t laserdz;
	int32_t xdenom;
	int32_t craftdy;
	uint16_t scale;

	laserdx = laserx - laserxold;
	laserdy = lasery - laseryold;
	laserdz = laserz - laserzold;
	craftdx = craftx - craftxold;
	craftdy = crafty - craftyold;
	craftdz = craftz - craftzold;

	/* Clip the relative motion against the box slab on each axis. */
	xdist = craftxold - laserxold;
	if (xdist > radius) {
		xmax = radius - xdist;
		xvel = craftdx - laserdx;
		if (xvel >= 0)
			return 0;
		if (xmax < xvel)
			return 0;
		xmin = -radius - xdist;
	} else if (xdist < -radius) {
		xmin = -radius - xdist;
		xvel = craftdx - laserdx;
		if (xvel < 0)
			return 0;
		if (xmin >= xvel)
			return 0;
		xmax = radius - xdist;
	} else {
		xmax = radius - xdist;
		xmin = -radius - xdist;
		xvel = 0;
	}

	ydist = craftyold - laseryold;
	if (ydist > radius) {
		ymax = radius - ydist;
		yvel = craftdy - laserdy;
		if (yvel >= 0)
			return 0;
		if (ymax < yvel)
			return 0;
		ymin = -radius - ydist;
	} else if (ydist < -radius) {
		ymin = -radius - ydist;
		yvel = craftdy - laserdy;
		if (yvel < 0)
			return 0;
		if (ymin >= yvel)
			return 0;
		ymax = radius - ydist;
	} else {
		ymax = radius - ydist;
		ymin = -radius - ydist;
		yvel = 0;
	}

	zdist = craftzold - laserzold;
	if (zdist > radius) {
		zmax = radius - zdist;
		zvel = craftdz - laserdz;
		if (zvel >= 0)
			return 0;
		if (zmax < zvel)
			return 0;
		zmin = -radius - zdist;
	} else if (zdist < -radius) {
		zmin = -radius - zdist;
		zvel = craftdz - laserdz;
		if (zvel < 0)
			return 0;
		if (zvel <= zmin)
			return 0;
		zmax = radius - zdist;
	} else {
		zmax = radius - zdist;
		zmin = -radius - zdist;
		zvel = 0;
	}

	/* Scale to 8.8 fixed point. Shift as unsigned to keep negative
	 * values well defined. */
	xmax = (int32_t)((uint32_t)xmax << 8);
	xmin = (int32_t)((uint32_t)xmin << 8);
	ymax = (int32_t)((uint32_t)ymax << 8);
	ymin = (int32_t)((uint32_t)ymin << 8);
	zmax = (int32_t)((uint32_t)zmax << 8);
	zmin = (int32_t)((uint32_t)zmin << 8);

	/* X axis entry/exit times. */
	if (xvel == 0) {
		xdenom = craftdx - laserdx;
		if (craftx - laserx > radius) {
			xmin = xmax / xdenom;
			xmax = 0;
		} else if (craftx - laserx < -radius) {
			xmin = xmin / xdenom;
			xmax = 0;
		} else {
			xmin = 255;
			xmax = 0;
		}
	} else {
		xmax = xmax / xvel;
		xmin = xmin / xvel;
		if (xvel >= 0) {
			swap = xmax;
			xmax = xmin;
			xmin = swap;
		}
	}

	/* Intersect with the Y axis interval. */
	if (yvel == 0) {
		ydenom = craftdy - laserdy;
		if (crafty - lasery > radius) {
			ymin = ymax / ydenom;
			ymax = 0;
		} else if (crafty - lasery < -radius) {
			ymin = ymin / ydenom;
			ymax = 0;
		} else {
			ymax = 0;
			ymin = 255;
		}
		if (xmin < ymax)
			return 0;
		if (xmax < ymax)
			xmax = ymax;
		if (ymin < xmax)
			return 0;
		if (ymin < xmin)
			xmin = ymin;
	} else {
		ymax = ymax / yvel;
		if (yvel >= 0) {
			if (xmax > ymax)
				return 0;
			if (xmin > ymax)
				xmin = ymax;
			ymin = ymin / yvel;
			if (ymin > xmin)
				return 0;
			if (ymin > xmax)
				xmax = ymin;
		} else {
			if (xmin < ymax)
				return 0;
			if (xmax < ymax)
				xmax = ymax;
			ymin = ymin / yvel;
			if (ymin < xmax)
				return 0;
			if (ymin < xmin)
				xmin = ymin;
		}
	}

	/* Intersect with the Z axis interval. */
	if (zvel == 0) {
		zdenom = craftdz - laserdz;
		if (craftz - laserz > radius) {
			zmin = zmax / zdenom;
			zmax = 0;
		} else if (craftz - laserz < -radius) {
			zmin = zmin / zdenom;
			zmax = 0;
		} else {
			zmin = 255;
			zmax = 0;
		}
		if (zmax > xmin)
			return 0;
		if (zmax > xmax)
			xmax = zmax;
		if (zmin < xmax)
			return 0;
		if (zmin < xmin)
			xmin = zmin;
	} else {
		zmax = zmax / zvel;
		if (zvel >= 0) {
			if (zmax < xmax)
				return 0;
			if (zmax < xmin)
				xmin = zmax;
			zmin = zmin / zvel;
			if (zmin > xmin)
				return 0;
			if (zmin > xmax)
				xmax = zmin;
		} else {
			if (zmax > xmin)
				return 0;
			if (zmax > xmax)
				xmax = zmax;
			zmin = zmin / zvel;
			if (zmin < xmax)
				return 0;
			if (zmin < xmin)
				xmin = zmin;
		}
	}

	if (xmax > 255)
		return 0;

	/* Impact offset = laser motion * tmin, in 8.7 fixed point. */
	collidexoff = laserdx;
	collideyoff = laserdy;
	collidezoff = laserdz;
	scale = (uint16_t)(xmax << 7);
	collidexoff *= scale;
	collidexoff >>= 15;
	collideyoff *= scale;
	collideyoff >>= 15;
	collidezoff *= scale;
	collidezoff >>= 15;
	return 0xFFFF;
}

/* ---------- 7. collide_lasercraftcollide ---------- */
// FUNCTION: TIE95 0x136C0
uint16_t collide_lasercraftcollide(uint16_t attacker_obj_idx, uint16_t target_obj_idx) {
	int32_t abs_dx, abs_dz, abs_dy;
	uint32_t approx;
	int32_t tgt_disp;
	int32_t bound_hwidth;
	int32_t genus;

	approxdist = 0x40000;
	abs_dx = laserx - craftx;
	if (abs_dx < 0)
		abs_dx = -abs_dx;
	if (abs_dx > 0x40000)
		return 0;

	abs_dy = lasery - crafty;
	if (abs_dy < 0)
		abs_dy = -abs_dy;
	if (abs_dy > 0x40000)
		return 0;

	abs_dz = laserz - craftz;
	if (abs_dz < 0)
		abs_dz = -abs_dz;
	if (abs_dz > 0x40000)
		return 0;

	approx = collide_roughdistance3du((uint32_t)abs_dx, (uint32_t)abs_dy, (uint32_t)abs_dz);
	approxdist = (int32_t)approx;
	if ((int32_t)approx > 0x40000)
		return 0;

	/* Tighter swept-bound test: combine attacker and target per-axis
	 * displacement, add bound, compare against approximate distance. */
	abs_dx = laserx - laserxold;
	if (abs_dx < 0)
		abs_dx = -abs_dx;
	abs_dy = lasery - laseryold;
	if (abs_dy < 0)
		abs_dy = -abs_dy;
	abs_dz = laserz - laserzold;
	if (abs_dz < 0)
		abs_dz = -abs_dz;
	tgt_disp = craftx - craftxold;
	if (tgt_disp < 0)
		tgt_disp = -tgt_disp;
	abs_dx += tgt_disp;
	tgt_disp = crafty - craftyold;
	if (tgt_disp < 0)
		tgt_disp = -tgt_disp;
	abs_dy += tgt_disp;
	tgt_disp = craftz - craftzold;
	if (tgt_disp < 0)
		tgt_disp = -tgt_disp;
	abs_dz += tgt_disp;

	bound_hwidth = (int32_t)species_table[objects[target_obj_idx].ship_idx].bound_hwidth;
	genus = objects[target_obj_idx].genus;
	if (genus == GENUS_PROJECTILE_NPC || genus == GENUS_PROJECTILE_PLAYER)
		bound_hwidth >>= 1;

	if (collide_roughdistance3du((uint32_t)(abs_dx + bound_hwidth), (uint32_t)(abs_dy + bound_hwidth),
								 (uint32_t)(abs_dz + bound_hwidth)) < approx)
		return 0;

	if (bound_hwidth > 1400) {
		if (targetcomputerflag) {
			targetcomputerflag = 0;
			if ((uint16_t)objects[target_obj_idx].current_speed < 0x28)
				return starship_checkstarshiphit(attacker_obj_idx, target_obj_idx);
		} else {
			return starship_checkstarshiphit(attacker_obj_idx, target_obj_idx);
		}
	}

	return (uint16_t)collide_checkboxcollision((bound_hwidth >> 3) + (bound_hwidth >> 2));
}

/* ---------- 8. collide_targetinrange ---------- */
// FUNCTION: TIE95 0x13E64
uint16_t collide_targetinrange(uint16_t shooter_obj_idx, uint16_t target_obj_idx, uint16_t hp_idx) {
	int16_t lookahead_3frame;
	int16_t proj_speed;
	FlightObject* tgt;
	uint8_t species_idx;
	uint16_t mph;
	uint16_t result;

	/* Projectile speed: retail uses player_spec_num and the active
	 * bank (player_weapon_group). */
	proj_speed =
		(int16_t)projectilevelocity[spec_data[pstate.player_spec_num].laser_type[pstate.player_weapon_group] -
									WEAPON_SPECIES_BASE];
	lookahead_3frame = 3 * (int16_t)framerate;
	laserxold = objects[shooter_obj_idx].world_x;
	laseryold = objects[shooter_obj_idx].world_y;
	laserzold = objects[shooter_obj_idx].world_z;

	/* Hardpoint position: retail uses objects[shooter_obj_idx].species_idx and the
	 * caller-supplied group index, NOT the active bank. Each weapon
	 * group has its own slot in spec.hp[], so probing a different group
	 * tests a different physical cannon mouth. */
	species_idx = objects[shooter_obj_idx].craft_ptr->species_idx;
	pai_calcrotatedpoint(&objects[shooter_obj_idx], spec_data[species_idx].hp[hp_idx].x,
						 spec_data[species_idx].hp[hp_idx].y, spec_data[species_idx].hp[hp_idx].z);
	laserxold += rotatedx;
	laseryold += rotatedy;
	laserzold += rotatedz;

	mph = math2_mphconvert((int16_t)(proj_speed + objects[shooter_obj_idx].current_speed), framerate);
	if (objects[shooter_obj_idx].move_dirty)
		fview_calcrotatemove(objects[shooter_obj_idx].pitch, objects[shooter_obj_idx].heading,
							 &objects[shooter_obj_idx]);

	laserx = laserxold + lookahead_3frame * ((mph * objects[shooter_obj_idx].moveX) >> 15);
	lasery = laseryold + lookahead_3frame * ((mph * objects[shooter_obj_idx].moveY) >> 15);
	laserz = laserzold + lookahead_3frame * ((mph * objects[shooter_obj_idx].moveZ) >> 15);

	if (target_obj_idx < 0x3800) {
		tgt = &objects[target_obj_idx];
		craftxold = tgt->world_x;
		craftyold = tgt->world_y;
		craftzold = tgt->world_z;

		mph = math2_mphconvert(tgt->current_speed, framerate);
		if (tgt->move_dirty)
			fview_calcrotatemove(tgt->pitch, tgt->heading, tgt);

		craftx = craftxold + lookahead_3frame * ((mph * tgt->moveX) >> 15);
		crafty = craftyold + lookahead_3frame * ((mph * tgt->moveY) >> 15);
		craftz = craftzold + lookahead_3frame * ((mph * tgt->moveZ) >> 15);

		targetcomputerflag = 1;
		result = collide_lasercraftcollide(shooter_obj_idx, target_obj_idx);
		targetcomputerflag = 0;
		return result;
	}
	return (uint16_t)static_laserstaticcollide(shooter_obj_idx, target_obj_idx - 0x3800);
}

/* ---------- 9. collide_craftstarshipcollision ---------- */
// FUNCTION: TIE95 0x14124
uint16_t collide_craftstarshipcollision(uint16_t craft_obj_idx, int16_t lookahead_frames) {
	int16_t lookahead_ticks = (int16_t)(lookahead_frames * framerate);
	FlightObject* atk = &objects[craft_obj_idx];
	uint16_t atk_speed;
	uint16_t j;

	laserxold = atk->world_x;
	laseryold = atk->world_y;
	laserzold = atk->world_z;
	atk_speed = math2_mphconvert(atk->current_speed, framerate);
	if (atk->move_dirty)
		fview_calcrotatemove(atk->pitch, atk->heading, atk);

	laserx = laserxold + lookahead_ticks * ((atk_speed * atk->moveX) >> 15);
	lasery = laseryold + lookahead_ticks * ((atk_speed * atk->moveY) >> 15);
	laserz = laserzold + lookahead_ticks * ((atk_speed * atk->moveZ) >> 15);

	for (j = 0; j < NUM_CRAFTS; j++) {
		FlightObject* tgt;
		uint16_t tgt_speed;
		uint8_t genus;

		if (!objects[j].ship_idx)
			continue;
		if (j == craft_obj_idx)
			continue;
		genus = objects[j].genus;
		if (genus != GENUS_FREIGHTER && genus != GENUS_STARSHIP && genus != GENUS_PLATFORM)
			continue;

		tgt = &objects[j];
		craftxold = tgt->world_x;
		craftyold = tgt->world_y;
		craftzold = tgt->world_z;
		tgt_speed = math2_mphconvert(tgt->current_speed, framerate);
		if (tgt->move_dirty)
			fview_calcrotatemove(tgt->pitch, tgt->heading, tgt);

		craftx = craftxold + lookahead_ticks * ((tgt_speed * tgt->moveX) >> 15);
		crafty = craftyold + lookahead_ticks * ((tgt_speed * tgt->moveY) >> 15);
		craftz = craftzold + lookahead_ticks * ((tgt_speed * tgt->moveZ) >> 15);

		if (collide_lasercraftcollide(craft_obj_idx, j))
			return j;
	}
	return 0xFFFFu;
}

/* ---------- 10. collide_laserhitcraft ---------- */
// FUNCTION: TIE95 0x1433C
void collide_laserhitcraft(uint16_t projectile_obj_idx, uint16_t target_obj_idx, int16_t hit_offset) {
	uint16_t self_idx;
	uint16_t head_on_flag;
	char damage_result;

	self_idx = objects[projectile_obj_idx].self_idx;
	if (self_idx == target_obj_idx)
		return;

	craftptr = objects[target_obj_idx].craft_ptr;
	if (!craftptr->was_hit_flag) {
		uint16_t fg_idx;

		craftptr->was_hit_flag |= 1;
		fg_idx = objects[target_obj_idx].fg_idx;
		fgstatus[fg_idx].cond[2].detail++;
		if ((int8_t)fg_array[fg_idx].special_craft == craftptr->craft_idx_in_fg)
			fgstatus[fg_idx].cond_id[2].detail = 1;
		fsfx_checkcriticalcraft(target_obj_idx, 0x58);
	}
	if (self_idx == pstate.object_idx)
		craftptr->was_hit_flag |= 0x80;

	/* Track the first attacker. Sentinel 0x00FF (init by PAI_initplan as
	 * a 16-bit write of 0xFF) means uninitialized. Friendly fire from the
	 * player accumulates in bits 4..6 of was_hit_flag (+1 per laser, +4
	 * per warhead) and only marks the player as attacker from 5 on. */
	if (craftptr->attacker_idx == 0xFFu && self_idx < NUM_CRAFTS) {
		uint8_t assign_attacker = 1;

		if (self_idx == pstate.object_idx && objects[target_obj_idx].side == objects[self_idx].side &&
			objects[target_obj_idx].genus) {
			uint8_t friendly_hits = (craftptr->was_hit_flag >> 4) & 7;

			if (friendly_hits < 5) {
				friendly_hits += (uint8_t)(projectile_is_warhead_type[objects[projectile_obj_idx].ship_idx -
																	  WEAPON_SPECIES_BASE] == 0
											   ? 1
											   : 4);
				if (friendly_hits > 7)
					friendly_hits = 7;
				craftptr->was_hit_flag = (friendly_hits << 4) | (craftptr->was_hit_flag & 0x8F);
			}
			if (friendly_hits < 5)
				assign_attacker = 0;
		}
		if (assign_attacker == 1)
			craftptr->attacker_idx = self_idx;
	}
	craftptr->hit_count++;

	/* head-on flag only computed when target is the player. */
	if (target_obj_idx == pstate.object_idx) {
		int32_t delta_x;
		int32_t delta_y;
		int32_t delta_z;
		int32_t headon_dot;

		if (pstate.player->orient_dirty) {
			fview_calcrotatemove(pstate.player->pitch, pstate.player->heading, pstate.player);
			fview_calcrotateorient(pstate.player->roll, 0, pstate.player);
		}
		/* Dot product of laser delta and player forward vector;
		 * positive = head-on. */
		delta_z = (int16_t)(laserz - laserzold);
		delta_y = (int16_t)(lasery - laseryold);
		delta_x = (int16_t)(laserx - laserxold);
		headon_dot =
			pstate.player->fwd_x * delta_x + pstate.player->fwd_y * delta_y + pstate.player->fwd_z * delta_z;
		if (headon_dot >= 0x40000000)
			headon_dot = 0x3FFF0000;
		if (headon_dot <= -0x40000000)
			headon_dot = -0x3FFF0000;
		if ((int16_t)(headon_dot >> 15) < 0)
			head_on_flag = 0;
		else
			head_on_flag = 1;
	} else {
		head_on_flag = 0;
	}

	if (objects[projectile_obj_idx].ship_idx != 152) {
		/* skill==5 = invulnerable / no-damage marker. */
		if ((int8_t)fg_array[objects[target_obj_idx].fg_idx].skill != 5)
			damage_result = collide_damagecraft(target_obj_idx, hit_offset, projectile_obj_idx, head_on_flag);
		else
			damage_result = 1;
	} else {
		/* ION CANNON projectile drains weapon energy. */
		uint16_t old_drain = craftptr->ion_drain_timer;

		if (target_obj_idx == pstate.object_idx) {
			uint16_t i;
			for (i = 0; i < pstate.player_craft->weapon_group_cnt; i++)
				pstate.player_craft->weapon_slots[i].charge = 0;
			msg_messageprintf(MSG_WARHEAD_DRAINED_CANNON);
		} else {
			uint16_t genus = objects[target_obj_idx].genus;
			if (genus == GENUS_FIGHTER || genus == GENUS_TRANSPORT || genus == GENUS_UTILITY)
				craftptr->ion_drain_timer += 4720;
			else
				craftptr->ion_drain_timer = old_drain + 7080;
		}
		/* Saturate instead of wrapping around. */
		if (old_drain > craftptr->ion_drain_timer)
			craftptr->ion_drain_timer = old_drain;
		damage_result = 1;
	}

	/* Convert projectile slot to explosion at impact point. The craft_ptr
	 * keeps pointing at the original warheads[] entry so downstream
	 * warhead-slot iterators (PAIORDER_avoidhitorder etc.) can read it
	 * unconditionally; they filter on the warhead's own fields
	 * (homing_tier / target_obj), not on a NULL ptr. */
	objects[projectile_obj_idx].world_x = laserxold + collidexoff;
	objects[projectile_obj_idx].world_y = laseryold + collideyoff;
	objects[projectile_obj_idx].world_z = laserzold + collidezoff;
	if (projectile_is_warhead_type[objects[projectile_obj_idx].ship_idx - WEAPON_SPECIES_BASE])
		objects[projectile_obj_idx].ship_idx = 129;
	else if (objects[projectile_obj_idx].ship_idx == 141 || objects[projectile_obj_idx].ship_idx == 142)
		objects[projectile_obj_idx].ship_idx = 132;
	else
		objects[projectile_obj_idx].ship_idx = 131;
	objects[projectile_obj_idx].genus = GENUS_EXPLOSION;
	objects[projectile_obj_idx].anim_frame = 2;
	objects[projectile_obj_idx].category = 5;
	objects[projectile_obj_idx].damage_state = 0;
	objects[projectile_obj_idx].age_ticks = 0;
	objects[projectile_obj_idx].death_timer = 0;
	objects[projectile_obj_idx].current_speed = objects[target_obj_idx].current_speed;
	objects[projectile_obj_idx].pitch = objects[target_obj_idx].pitch;
	objects[projectile_obj_idx].heading = objects[target_obj_idx].heading;
	objects[projectile_obj_idx].orient_dirty = 1;
	objects[projectile_obj_idx].move_dirty = 1;
	objects[projectile_obj_idx].roll = 0;

	if (damage_result) {
		uint16_t sfx_id;

		if (target_obj_idx == pstate.object_idx) {
			sfx_id = 26;
		} else if (objects[projectile_obj_idx].ship_idx == 131 ||
				   objects[projectile_obj_idx].ship_idx == 132) {
			fsfx_triggersfx(25, projectile_obj_idx);
			return;
		} else {
			sfx_id = (uint16_t)((math2_getrandom() & 3) + 19);
		}
		fsfx_triggersfx(sfx_id, projectile_obj_idx);
	}
}

/* ---------- 11. collide_damagecraft ----------
 *
 * Apply damage to target_obj_idx. Pipeline:
 *   1) determine raw damage from attacker bound_hwidth (gen-3=÷4, gen-4/5=÷16)
 *   2) if mesh component flagged hittable bit 0, route to starship_damagecomponent
 *   3) absorb damage into shield slot if shield > damage
 *   4) overflow case: zero the slot, then if attacker is missile/warhead
 *      (ship 141/142) randomly disable one cap-ship system; otherwise
 *      apply overflow to opposite shield with random capability disable
 *   5) if shields broken AND hull_damage >= hull_max: schedule death
 *      (flight_flag=3, death_timer = random ticks), spawn a wing
 *      component, trigger MsSetSequence cue, score the kill.
 */
// FUNCTION: TIE95 0x148F0
char collide_damagecraft(uint16_t target_obj_idx, int16_t component_idx, uint16_t attacker_obj_idx,
						 uint16_t weapon_group) {
	CraftData* tgt_craft;
	uint16_t atk_species;
	int32_t collision_radius;
	int16_t damage;
	int16_t damagea;
	uint8_t ret_no_panel_update;
	uint8_t panel_dirty;
	uint8_t* comp_record;
	uint8_t component_explosion_type1;
	uint8_t component_damageable;
	uint8_t genus;
	int16_t* shield_slot;
	int16_t shield_a;

	TIE_FLIGHT_TRACE_DAMAGE_BEFORE(target_obj_idx);
	tgt_craft = objects[target_obj_idx].craft_ptr;

	ret_no_panel_update = 1;
	panel_dirty = 0;
	comp_record = NULL;
	component_explosion_type1 = 0;
	component_damageable = 0;

	craftptr = tgt_craft;

	/* Step 1: determine raw damage. */
	if (attacker_obj_idx == 0xFFFFu) {
		collision_radius = 0x20000;
		atk_species = 53;
	} else if (attacker_obj_idx >= 0x3800u) {
		uint16_t bw;
		atk_species = staticobjects[attacker_obj_idx - 0x3800u].species;
		bw = species_table[(uint8_t)atk_species].bound_hwidth;
		collision_radius = (bw >= 0x8000) ? 0x20000 : (4 * bw);
	} else {
		atk_species = objects[attacker_obj_idx].ship_idx;
		collision_radius = (uint16_t)objects[attacker_obj_idx].collision_radius;
	}

	genus = objects[target_obj_idx].genus;
	if (genus == GENUS_STARSHIP || genus == GENUS_PLATFORM)
		collision_radius >>= 4;
	if (objects[target_obj_idx].genus == GENUS_FREIGHTER)
		collision_radius >>= 2;
	if (collision_radius >= 0x7FFF)
		collision_radius = 32766;
	damage = (int16_t)collision_radius;

	/* Step 2: per-mesh component damage routing. */
	if (component_idx != -1) {
		if (TIE_FLIGHT_TIE98) {
			const uint16_t model_type = objects[target_obj_idx].ship_idx;
			const int mesh_index = component_idx - 1;
			component_explosion_type1 = (uint8_t)modelmesh_hasexplosiontype1(model_type, mesh_index);
			component_damageable = (uint8_t)modelmesh_isobjecttypemeshdamageable(model_type, mesh_index);
			if (component_explosion_type1)
				damage = (int16_t)starship_damagecomponent(target_obj_idx, component_idx, collision_radius);
		} else {
			uint8_t* model_data =
				(uint8_t*)xmemhdl_Lock_Handle(species_table[objects[target_obj_idx].ship_idx].model_handle);
			xmemhdl_Unlock_Handle(species_table[objects[target_obj_idx].ship_idx].model_handle);
			if (model_data) {
				comp_record = &model_data[64 * (uint16_t)component_idx - 30 + 6 * model_data[31]];
				component_explosion_type1 = (uint8_t)((*(uint16_t*)(comp_record + 2) & 1) != 0);
				component_damageable = (uint8_t)((*(uint16_t*)(comp_record + 2) & 2) != 0);
				if (component_explosion_type1)
					damage =
						(int16_t)starship_damagecomponent(target_obj_idx, component_idx, collision_radius);
			}
		}
	}

	if (objects[target_obj_idx].genus == GENUS_GATE)
		damage = 0;

	/* Step 3: shield absorption. weapon_group selects forward_shield (0)
	 * or rear_shield (1); both are int16 fields at +0xCA / +0xCC. */
	shield_slot = (weapon_group == 0) ? &tgt_craft->forward_shield : &tgt_craft->rear_shield;
	shield_a = *shield_slot;

	if (shield_a > damage) {
		/* Damage fully absorbed. */
		*shield_slot = (int16_t)(shield_a - damage);
		if (target_obj_idx == pstate.object_idx) {
			shieldblink = (uint8_t)weapon_group;
			timers[TIMER_SHIELD_FLASH] += 59;
		}
	} else {
		/* Step 4: shield overflow. */
		if (shield_a && !tgt_craft->hull_damage)
			fsfx_checkcriticalcraft(target_obj_idx, 0x59);

		*shield_slot = 0;
		damagea = (int16_t)(damage - shield_a);

		if (damagea) {
			if ((atk_species == 141 || atk_species == 142) && target_obj_idx != pstate.object_idx) {
				/* Missile/warhead overflow: disable cap-ship systems. */
				uint16_t status = tgt_craft->status_flags;
				if (status) {
					while (damagea > 0) {
						uint16_t i;
						for (i = 0; i < 10u; i++) {
							int16_t mask = systemmask[i];
							if ((uint16_t)mask & tgt_craft->status_flags) {
								panel_dirty = 1;
								tgt_craft->status_flags &= (uint16_t)(mask ^ 0x3FF);
								break;
							}
						}
						if (target_obj_idx == pstate.object_idx) {
							pstate.subsystem_health_percent[i] = 0;
							pstate.subsystem_repair_seconds[i] = (uint16_t)repairtime[i];
						}
						damagea -= 200;
					}
					if (!tgt_craft->status_flags) {
						msg_craftmessage(target_obj_idx, tgt_craft, 0x63);
						if (!fsfx_checkcriticalcraft(target_obj_idx, 0x54)) {
							if (objects[target_obj_idx].side == pstate.player->side) {
								fsfx_triggersfx(0x27, 0xFFFF);
							} else {
								if (pstate.object_idx != objects[attacker_obj_idx].self_idx)
									fsfx_speakobjectname(objects[attacker_obj_idx].self_idx, 0x33);
								if (fsfx_speakeravailable()) {
									fsfx_triggervoicesfx(0x52);
									fsfx_triggervoicesfx(0x54);
								}
							}
						}
						/* All systems disabled -> mark as 'systems-down' on FG cond[7]. */
						{
							uint8_t fg_idx = objects[target_obj_idx].fg_idx;
							fgstatus[fg_idx].cond[7].detail++;
							if (fg_array[fg_idx].special_craft == tgt_craft->craft_idx_in_fg)
								fgstatus[fg_idx].cond_id[7].detail = 1;
						}
						{
							uint8_t ship_idx = objects[target_obj_idx].ship_idx;
							if (ship_idx >= 5 && ship_idx <= 7) {
								objects[target_obj_idx].death_timer = 60;
								TIE_FLIGHT_TRACE_DEATH(target_obj_idx, attacker_obj_idx,
													   TIE_TRACE_DEATH_SYSTEMS_DISABLED, 60);
								collide_updatekills(objects[attacker_obj_idx].self_idx, target_obj_idx);
								score_craftexitscoring(target_obj_idx, objects[target_obj_idx].fg_idx, 2);
							}
						}
					}
				}
				if (target_obj_idx == pstate.object_idx)
					fsfx_triggersfx(0x1E, target_obj_idx);
			} else {
				/* Standard overflow: opposite shield + random instrument disable. */
				if (component_idx != -1 && component_damageable)
					damagea = (int16_t)starship_damagecomponent(target_obj_idx, component_idx, damagea);

				{
					uint16_t hull_damage = tgt_craft->hull_damage;
					uint16_t hull_max = tgt_craft->hull_max;
					int32_t half_hull = hull_max / 2;

					tgt_craft->hull_damage = (uint16_t)(hull_damage + damagea);
					if (hull_damage < half_hull && tgt_craft->hull_damage >= half_hull)
						fsfx_checkcriticalcraft(target_obj_idx, 0x5A);
				}

				if (target_obj_idx == pstate.object_idx) {
					timers[TIMER_SHIELD_OVERLOAD] += 59;
					if ((uint16_t)math2_getrandom() < 0x4000u) {
						int16_t r0 = math2_getrandom();
						int16_t r1 = math2_getrandom() & 1;
						int16_t r2 = math2_getrandom() & 1;
						uint16_t sys_pick = (uint16_t)((r2 + r1 + (r0 & 7)) & 0xFFFF);
						if ((uint16_t)systemmask[sys_pick] & tgt_craft->status_flags) {
							tgt_craft->status_flags &= (uint16_t)(systemmask[sys_pick] ^ 0x3FF);
							argtable[0] = (uint8_t)damagemsg[sys_pick];
							argtable[1] = 25;
							msg_messageprintf(MSG_SYSTEM_STATUS);
							pstate.subsystem_health_percent[sys_pick] = 0;
							pstate.subsystem_repair_seconds[sys_pick] = (uint16_t)repairtime[sys_pick];
						}
					}
				}
			}

			/* Random instrument knockout (instrumentdisable[0..15]). */
			if (tgt_craft->hull_damage < tgt_craft->hull_strength) {
				if (target_obj_idx == pstate.object_idx) {
					uint16_t sfx = ((uint16_t)math2_getrandom() & 0x8000u) ? 28 : 29;
					fsfx_triggersfx(sfx, target_obj_idx);
					ret_no_panel_update = 0;
				}
			} else {
				uint8_t rng = (uint8_t)math2_getrandom() & 0xF;
				int16_t bit = instrumentdisable[rng];
				if ((!mission.train_craft_type || bit != 1) &&
					((uint16_t)bit & tgt_craft->installed_subsystems)) {
					tgt_craft->working_subsystems &= (uint16_t)~bit;
					if (target_obj_idx == pstate.object_idx) {
						fsfx_triggersfx(0x1B, target_obj_idx);
						ret_no_panel_update = 0;
					}
					panel_dirty = 1;
				}
			}

			if (panel_dirty && target_obj_idx == pstate.object_idx && !replayviewmode)
				panel_updatecockpitdamage();
		}
	}

	TIE_FLIGHT_TRACE_DAMAGE_AFTER(target_obj_idx, attacker_obj_idx, component_idx, damage);

	/* Step 5: death-or-survive decision. */
	if (tgt_craft->flight_flag || tgt_craft->hull_damage < tgt_craft->hull_max)
		return ret_no_panel_update;

	/* Credit the kill (split static-vs-craft attacker handling). */
	if (attacker_obj_idx != 0xFFFFu) {
		uint16_t shooter = attacker_obj_idx;
		if (attacker_obj_idx < 0x3800u) {
			if (objects[attacker_obj_idx].category)
				shooter = (uint16_t)objects[attacker_obj_idx].self_idx;
			collide_updatekills(shooter, target_obj_idx);
		}
	}

	/* Briefing/training/combat: signal end-of-mission immediately. */
	if (mission.train_craft_type) {
		if (target_obj_idx == pstate.object_idx && !replayviewmode) {
			user_checkreplaycamera();
			mission.end_flag = 1;
			mission.player_status = 3;
		}
	} else if (target_obj_idx == pstate.object_idx && !replayviewmode) {
		int16_t pilot_status_flag;
		if (tgt_craft->status_flags & 2u) {
			if (user_isrescued(target_obj_idx)) {
				mission.player_status = 2;
				user_ejectcamera();
				pilot_status_flag = 0;
			} else {
				mission.player_status = 1;
				user_ejectcamera();
				pilot_status_flag = 1;
			}
		} else {
			mission.player_status = mission.train_craft_type;
			user_ejectcamera();
			pilot_status_flag = 2;
		}
		fediskio_updatepilotrecord(pilot_status_flag, 1);
	}

	score_craftexitscoring(target_obj_idx, objects[target_obj_idx].fg_idx, 2);

	/* Clear any link-target references to the dying craft. */
	{
		uint16_t j;
		for (j = 0; j < NUM_CRAFTS; j++) {
			if (!objects[j].ship_idx)
				continue;
			if ((uint16_t)objects[j].craft_ptr->tow_slave_ref == target_obj_idx)
				objects[j].craft_ptr->tow_slave_ref = -1;
		}
	}

	msg_craftmessage(target_obj_idx, craftptr, 0x62);

	if (!fsfx_checkcriticalcraft(target_obj_idx, 0x53)) {
		if (pstate.player->side == objects[target_obj_idx].side) {
			fsfx_triggersfx(0x27, 0xFFFF);
		} else if (objects[target_obj_idx].genus) {
			if (pstate.object_idx != objects[attacker_obj_idx].self_idx)
				fsfx_speakobjectname(objects[attacker_obj_idx].self_idx, 0x33);
			if (fsfx_speakeravailable()) {
				fsfx_triggervoicesfx(0x52);
				fsfx_triggervoicesfx(0x53);
			}
		}
	}

	/* Pick the FSCRIPT MsSetSequence id. */
	{
		int16_t ms_seq_id;
		if (objects[target_obj_idx].fg_idx == pstate.player->fg_idx) {
			ms_seq_id = (attacker_obj_idx == 0xFFFFu || attacker_obj_idx >= 0x3800u ||
						 pstate.object_idx != objects[attacker_obj_idx].self_idx)
							? 4
							: 3;
			fscript_MsSetSequence(ms_seq_id);
		} else {
			uint8_t tg = objects[target_obj_idx].genus;
			if ((tg == 4 || tg == 5) && objects[target_obj_idx].side == pstate.player->side) {
				fscript_MsSetSequence(3);
			} else if (attacker_obj_idx != 0xFFFFu && attacker_obj_idx < 0x3800u &&
					   objects[attacker_obj_idx].self_idx == pstate.object_idx) {
				ms_seq_id = (tg == 4 || tg == 5) ? 1 : 2;
				fscript_MsSetSequence(ms_seq_id);
			}
		}
	}

	/* Spawn explosion / death-spin / wing-debris. */
	{
		uint16_t tgt_ship = objects[target_obj_idx].ship_idx;
		uint16_t bw = species_table[tgt_ship].bound_hwidth;

		if (bw <= 0x578u) {
			/* Small/medium ship death path. */

			if (target_obj_idx == pstate.object_idx && !(pstate.player_craft->status_flags & 2u)) {
				/* Player non-rescued explosion. */
				FlightObject* o = &objects[target_obj_idx];
				const uint8_t explosion_variant = (uint8_t)((math2_getrandom() & 1) + 127);
				TIE_FLIGHT_TRACE_DEATH(target_obj_idx, attacker_obj_idx, TIE_TRACE_DEATH_DAMAGE, 0);
				TIE_FLIGHT_TRACE_EXPLOSION(target_obj_idx, explosion_variant);
				o->anim_frame = 2;
				o->ship_idx = explosion_variant;
				o->damage_state = 24;
				o->genus = GENUS_EXPLOSION;
				o->category = 5;
				o->current_speed = 0;
				o->age_ticks = 0;
				o->death_timer = 0;
				o->roll = 0;
				o->spin_rate = 0;
				o->orient_dirty = 1;
				fsfx_triggersfx(0x12, target_obj_idx);
				tgt_craft->flight_flag = 4;
				return 0;
			}
			if ((species_table[atk_species].bound_hwidth > 0x578u && !species_table[atk_species].category) ||
				!objects[target_obj_idx].current_speed ||
				((uint16_t)math2_getrandom() < 0x4000u && target_obj_idx != pstate.object_idx)) {
				/* Non-spinning instant death. */
				objects[target_obj_idx].death_timer = 1;
				TIE_FLIGHT_TRACE_DEATH(target_obj_idx, attacker_obj_idx, TIE_TRACE_DEATH_DAMAGE, 1);
				fsfx_triggersfx(0x12, target_obj_idx);
				tgt_craft->flight_flag = 4;
				return 0;
			}

			/* Spawn a wing/component blow-off. */
			if (!TIE_FLIGHT_TIE98)
				draw_Lockshipfileptrs(tgt_ship);
			{
				uint16_t num_meshes =
					TIE_FLIGHT_EDITION(objectblockptr->num_meshes, (uint16_t)modelmesh_getcount(tgt_ship));
				int32_t heading_kick = 0;
				int32_t spin_kick = 0;
				int16_t k = 0;
				uint16_t mesh_idx = 0;

				if (num_meshes > 1u) {
					int16_t side_pick = (uint8_t)math2_getrandom() & 1;
					k = side_pick;
					for (; mesh_idx < num_meshes; ++mesh_idx) {
						int mesh_type;

						if (tgt_craft->mesh_state[mesh_idx] != MESH_STATE_VISIBLE)
							continue;
						/* Both fixed and rotating wing meshes are eligible. */
						mesh_type = TIE_FLIGHT_EDITION(componentblockptr[mesh_idx].mesh_type,
													   modelmesh_gettype(tgt_ship, mesh_idx));
						if (mesh_type == TIE_MESH_WING || mesh_type == TIE_MESH_ROTARY_WING) {
							const int center_x = TIE_FLIGHT_EDITION(componentblockptr[mesh_idx].center_side,
																	modelmesh_getcenterx(tgt_ship, mesh_idx));
							if (side_pick) {
								if (center_x < 0)
									break;
							} else if (center_x > 0) {
								break;
							}
						}
					}
					if (mesh_idx < num_meshes) {
						uint16_t comp_obj = create_createcomponent(target_obj_idx, (uint8_t)mesh_idx);
						if (comp_obj != 0xFFFFu) {
							int32_t r0 = math2_getrandom();
							int32_t r1 = math2_getrandom();
							spin_kick = (int16_t)(((r0 >> 8) & 0x3F) + 64) << 8;
							heading_kick = (int16_t)(((r1 >> 8) & 7) + 8) << 8;
							if (k) {
								heading_kick = -heading_kick;
								spin_kick = -spin_kick;
							}
							objects[comp_obj].spin_rate = (int16_t)spin_kick;
							objects[comp_obj].heading = (int16_t)(heading_kick + objects[comp_obj].heading);
							objects[comp_obj].orient_dirty = 1;
							objects[comp_obj].move_dirty = 1;
							objects[comp_obj].anim_frame_alt = 2;
							tgt_craft->mesh_state[mesh_idx] = MESH_STATE_BLOWN_OFF;
							fsfx_triggersfx(((uint16_t)math2_getrandom() & 0x8000u) ? 23 : 24,
											target_obj_idx);
							ret_no_panel_update = 0;
						}
					}
				}

				/* Tag the dying craft with a death-spin + timer. */
				{
					uint8_t species_idx = tgt_craft->species_idx;
					int32_t spin_main = math2_getrandom();
					spin_main = ((spin_main >> 8) & 0x3F) + 32;
					spin_main <<= 8;
					while ((uint16_t)spin_main > (uint16_t)spec_data[species_idx].max_spin_rate)
						spin_main >>= 1;
					if ((uint16_t)spin_kick < 0x8000u)
						spin_main = -spin_main;
					objects[target_obj_idx].spin_rate = (int16_t)spin_main;
					if (heading_kick) {
						objects[target_obj_idx].heading -= (int16_t)((uint16_t)heading_kick / 2);
						objects[target_obj_idx].orient_dirty = 1;
						objects[target_obj_idx].move_dirty = 1;
					}
					tgt_craft->flight_flag = 3;
					objects[target_obj_idx].death_timer = (int16_t)(236 * ((math2_getrandom() & 0xF) + 1));
					if (target_obj_idx == pstate.object_idx)
						objects[target_obj_idx].death_timer = (int16_t)(236 * ((math2_getrandom() & 3) + 4));
					TIE_FLIGHT_TRACE_DEATH(target_obj_idx, attacker_obj_idx, TIE_TRACE_DEATH_DAMAGE,
										   objects[target_obj_idx].death_timer);
					/* [num_meshes] is the overlaid lightning anim frame
					 * counter, not a per-mesh state. 2 = jump the bolt
					 * script to frame 2. */
					tgt_craft->mesh_state[num_meshes] = 2;
				}
			}
		} else {
			/* Capital ship death path: just spin + delayed death. */
			uint8_t species_idx = tgt_craft->species_idx;
			uint16_t spin_rng = (uint16_t)math2_getrandom();
			spin_rng = ((spin_rng >> 8) & 0x3F) + 32;
			spin_rng <<= 8;
			while (spin_rng > (uint16_t)spec_data[species_idx].max_spin_rate)
				spin_rng >>= 1;
			objects[target_obj_idx].spin_rate = -(int16_t)spin_rng;
			tgt_craft->flight_flag = 3;
			objects[target_obj_idx].death_timer = (int16_t)(236 * ((math2_getrandom() & 7) + 8));
			TIE_FLIGHT_TRACE_DEATH(target_obj_idx, attacker_obj_idx, TIE_TRACE_DEATH_DAMAGE,
								   objects[target_obj_idx].death_timer);
			return ret_no_panel_update;
		}
	}
	return ret_no_panel_update;
}

/* ---------- 12. collide_checkhitpolygons ----------
 *
 * Mesh blob layout (mesh_data points at the +0):
 *   +0   u8  ?               (vertex/header field, unused here)
 *   +2   u8  vertex count
 *   +4   u8  face count
 *   +5   6 x int16 AABB:  min_x, min_y, min_z, max_x, max_y, max_z
 *   +0x11  vertex array: 3 * vert_count int16 (x, y, z per vertex)
 *   <after verts>  face record array: 8 bytes each
 *                  (int16 nx, nz, ny, byte_offset_to_vert_id_table)
 *
 * Vertex IDs in the per-face vertex-id table use a 0x7Fxx 'continuation'
 * indirection (each 0x7Fxx entry points back 3*((value>>1)) int16
 * slots; a real coord has high byte != 0x7F). Each vertex fetch walks
 * that chain inline. */
// FUNCTION: TIE95 0x15B38
uint32_t collide_checkhitpolygons(const uint8_t* mesh_data, int32_t x1, int32_t y1, int32_t z1, int32_t x2,
								  int32_t y2, int32_t z2, int32_t return_first_hit) {
	const uint8_t* base = mesh_data;
	uint32_t face_count = base[4];
	uint32_t vert_count = base[2];
	const int32_t* bbox_p = (const int32_t*)(base + face_count + 5);
	const int16_t* vert_array;
	const int32_t* face_iter;
	uint32_t face_idx;
	uint32_t i_min = 0x7FFFFFFFu;

	/* AABB Manhattan reject. Bounding box layout is 6 int16: min triplet
	 * (X, Y, Z) followed by max triplet (X, Y, Z). Binary uses Watcom
	 * unaligned dword loads here too; the int16-array form is equivalent. */
	{
		const int16_t* bb = (const int16_t*)bbox_p;
		if (bb[0] > x1 && bb[0] > x2)
			return 0; /* min_x */
		if (bb[1] > y1 && bb[1] > y2)
			return 0; /* min_y */
		if (bb[2] > z1 && bb[2] > z2)
			return 0; /* min_z */
		if (bb[3] < x1 && bb[3] < x2)
			return 0; /* max_x */
		if (bb[4] < y1 && bb[4] < y2)
			return 0; /* max_y */
		if (bb[5] < z1 && bb[5] < z2)
			return 0; /* max_z */
	}

	vert_array = (const int16_t*)(bbox_p + 3);
	face_iter = bbox_p + 3 + 3 * (int32_t)vert_count;

	for (face_idx = 0; face_idx < face_count; face_idx++) {
		/* Each face record is 4 int16: nx, nz, ny, byte-offset-to-vid-table.
		 * Binary uses Watcom unaligned dword loads to extract them; the
		 * straight int16-array form below is bit-for-bit equivalent. */
		const int16_t* face_hdr = (const int16_t*)face_iter;
		int32_t face_nx = face_hdr[0];
		int32_t face_nz = face_hdr[1];
		int32_t face_ny = face_hdr[2];
		const uint8_t* face_record = (const uint8_t*)face_iter + face_hdr[3];
		int32_t remaining_edges = *face_record & 0x3F;
		int32_t vx0, vy0, vz0;
		int32_t side1, side2;
		uint32_t t_param = 0;
		int32_t isect_a = 0, isect_b = 0;
		int32_t first_a, first_b;
		int inside_flag = 1;
		const uint8_t* vert_byte_p;
		const int16_t* vp;
		int axis_a_pick;
		int axis_b_pick;
		int half_first;

		int32_t x_isect, y_isect, z_isect;

		face_iter += 2;
		if (remaining_edges == 2)
			continue;

		for (vp = vert_array + 3 * face_record[1]; (*vp & 0xFF00) == 0x7F00;
			 vp -= 3 * ((int)(uint8_t)*vp >> 1))
			;
		vx0 = *vp;
		for (vp = vert_array + 3 * face_record[1] + 1; (*vp & 0xFF00) == 0x7F00;
			 vp -= 3 * ((int)(uint8_t)*vp >> 1))
			;
		vy0 = *vp;
		for (vp = vert_array + 3 * face_record[1] + 2; (*vp & 0xFF00) == 0x7F00;
			 vp -= 3 * ((int)(uint8_t)*vp >> 1))
			;
		vz0 = *vp;

		side1 = math2_mul_q15(x1 - vx0, face_nx);
		side1 += math2_mul_q15(y1 - vy0, face_nz);
		side1 += math2_mul_q15(z1 - vz0, face_ny);
		if (side1 > -10 && side1 < 10)
			side1 = 0;

		side2 = math2_mul_q15(x2 - vx0, face_nx);
		side2 += math2_mul_q15(y2 - vy0, face_nz);
		side2 += math2_mul_q15(z2 - vz0, face_ny);
		if (side2 > -10 && side2 < 10)
			side2 = 0;

		{
			int32_t sides_xor = side1 ^ side2;
			if (!side1 || !side2)
				sides_xor = -1;
			if (sides_xor >= 0)
				continue;
		}

		/* Compute parametric t = side1 / (side1 - side2) in 15-bit fp,
		 * then interpolate the full 3D intersection point in world
		 * coords. Retail computes all three components and lets the
		 * dominant-normal axis picker below choose two of them
		 * (v29/v56/v80 at 0x15edb..0x15f0c, swap at 0x15f51..0x15f8b). */

		if (side1) {
			if (side2) {
				int32_t num, den;
				/* num = side2 << 15, negated on the side1>=0 branch.
				 * Retail emits `shl edx,0Fh` (+ `neg`), a 32-bit op that
				 * truncates/wraps; the shifted dot product can exceed
				 * 32 bits since the segment endpoints are clamped to
				 * +-Q30 then doubled. Do the shift in uint32 so the wrap
				 * is defined rather than signed-overflow UB. */
				uint32_t shifted = (uint32_t)side2 << 15;
				if (side1 >= 0) {
					num = (int32_t)(0u - shifted);
					den = side1 - side2;
				} else {
					num = (int32_t)shifted;
					den = side2 - side1;
				}
				t_param = (uint32_t)(num / den);
				/* Retail interpolates with `imul reg32,reg32; sar eax,0Fh`:
				 * the product is truncated to 32 bits before the arithmetic
				 * shift, and likewise wraps for large deltas. Reproduce the
				 * exact low-32-bit result via an unsigned multiply. */
				x_isect = x2 + ((int32_t)(t_param * (uint32_t)(x1 - x2)) >> 15);
				y_isect = y2 + ((int32_t)(t_param * (uint32_t)(y1 - y2)) >> 15);
				z_isect = z2 + ((int32_t)(t_param * (uint32_t)(z1 - z2)) >> 15);
			} else {
				t_param = 0;
				x_isect = x2;
				y_isect = y2;
				z_isect = z2;
			}
		} else {
			t_param = 0x7FFF;
			x_isect = x1;
			y_isect = y1;
			z_isect = z1;
		}

		/* Pick dominant face-normal axis for 2D point-in-poly. The
		 * vertex offset 0/1/2 in the mesh blob corresponds to world
		 * X/Y/Z respectively (face_hdr[0/1/2] = nx, "nz", "ny" in the
		 * misnamed retail header layout), so the axis pick simultaneously
		 * selects which vertex offsets to read AND which world isect
		 * components to use. */
		if (face_nx < 0)
			face_nx = -face_nx;
		if (face_nz < 0)
			face_nz = -face_nz;
		if (face_ny < 0)
			face_ny = -face_ny;

		vert_byte_p = face_record + 1;
		if (face_ny < face_nz || face_ny < face_nx) {
			if (face_nz < face_nx || face_nz < face_ny) {
				/* face_nx largest -> drop X, project onto (Y, Z). */
				axis_a_pick = 1;
				axis_b_pick = 2;
				isect_a = y_isect;
				isect_b = z_isect;
			} else {
				/* face_nz largest -> drop Y, project onto (X, Z). */
				axis_a_pick = 0;
				axis_b_pick = 2;
				isect_a = x_isect;
				isect_b = z_isect;
			}
		} else {
			/* face_ny largest -> drop Z, project onto (X, Y). */
			axis_a_pick = 0;
			axis_b_pick = 1;
			isect_a = x_isect;
			isect_b = y_isect;
		}

		/* Walk vertex list around the face, requiring all
		 * MATH2_halfplane signs to match the first edge. */
		for (vp = vert_array + 3 * vert_byte_p[0] + axis_a_pick; (*vp & 0xFF00) == 0x7F00;
			 vp -= 3 * ((int)(uint8_t)*vp >> 1))
			;
		first_a = *vp;
		for (vp = vert_array + 3 * vert_byte_p[0] + axis_b_pick; (*vp & 0xFF00) == 0x7F00;
			 vp -= 3 * ((int)(uint8_t)*vp >> 1))
			;
		first_b = *vp;
		{
			int32_t edge_a;
			int32_t edge_b;
			for (vp = vert_array + 3 * vert_byte_p[2] + axis_a_pick; (*vp & 0xFF00) == 0x7F00;
				 vp -= 3 * ((int)(uint8_t)*vp >> 1))
				;
			edge_a = *vp;
			for (vp = vert_array + 3 * vert_byte_p[2] + axis_b_pick; (*vp & 0xFF00) == 0x7F00;
				 vp -= 3 * ((int)(uint8_t)*vp >> 1))
				;
			edge_b = *vp;
			half_first =
				math2_halfplane(isect_a - first_a, edge_b - first_b, isect_b - first_b, edge_a - first_a);

			while (1) {
				int32_t prev_a = edge_a;
				int32_t prev_b = edge_b;
				int half_test;
				for (vp = vert_array + 3 * vert_byte_p[4] + axis_a_pick; (*vp & 0xFF00) == 0x7F00;
					 vp -= 3 * ((int)(uint8_t)*vp >> 1))
					;
				edge_a = *vp;
				for (vp = vert_array + 3 * vert_byte_p[4] + axis_b_pick; (*vp & 0xFF00) == 0x7F00;
					 vp -= 3 * ((int)(uint8_t)*vp >> 1))
					;
				edge_b = *vp;
				half_test =
					math2_halfplane(isect_a - prev_a, edge_b - prev_b, isect_b - prev_b, edge_a - prev_a);
				if (half_test != half_first) {
					inside_flag = 0;
					break;
				}
				vert_byte_p += 2;
				if (--remaining_edges == 0)
					break;
			}
		}

		if (mission.train_craft_type && return_first_hit)
			return (uint32_t)((t_param & ~0xFFu) | ((t_param | 1u) & 0xFFu));

		if (inside_flag) {
			t_param = (t_param & ~0xFFu) | ((t_param | 1u) & 0xFFu);
			if (t_param < i_min)
				i_min = t_param;
		}
	}

	if (i_min == 0x7FFFFFFFu)
		return 0;
	return i_min;
}

/* ---------- 13. collide_collisions ----------
 *
 * Per-frame top-level dispatcher. See header doc-comment for the
 * three passes (player-vs-craft + tractor + friendly-tag, player-vs-static,
 * genus-driven cross-craft / projectile / probe).
 */
// FUNCTION: TIE95 0x12740
void collide_collisions(void) {
	uint16_t target_idx;
	uint16_t projectile_idx;

	FlightObject* pl;
	CraftData* pc;

	if (hyperspaceflag && !hyperabortflag)
		return;

	pl = pstate.player;
	pc = pstate.player_craft;
	if (!pl->genus) {
		/* Pass 1: player-vs-every-craft. */
		laserx = pstate.laser_origin_dx + pl->world_x;
		lasery = pstate.laser_origin_dy + pl->world_y;
		laserz = pstate.laser_origin_dz + pl->world_z;
		laserxold = pstate.laser_origin_dx_prev + pl->world_x_prev;
		laseryold = pstate.laser_origin_dy_prev + pl->world_y_prev;
		laserzold = pstate.laser_origin_dz_prev + pl->world_z_prev;

		if (pc->status_flags) {
			for (target_idx = 0; target_idx < NUM_CRAFTS; target_idx++) {
				FlightObject* tgt = &objects[target_idx];
				CraftData* tgt_craft;
				uint16_t hit_offset;

				if (!tgt->ship_idx)
					continue;

				craftx = tgt->world_x;
				crafty = tgt->world_y;
				craftz = tgt->world_z;
				craftxold = tgt->world_x_prev;
				craftyold = tgt->world_y_prev;
				craftzold = tgt->world_z_prev;

				if (pstate.object_idx != target_idx && tgt->genus != GENUS_EXPLOSION &&
					tgt->fg_idx != pl->fg_idx && target_idx != pc->spin_done_flag) {
					tgt_craft = tgt->craft_ptr;
					if ((tgt_craft->default_order_ldr != 28 ||
						 pstate.object_idx != (uint16_t)tgt_craft->ai_target_ref) &&
						(!inflight_invulnerable || mission.train_craft_type)) {
						hit_offset = collide_lasercraftcollide(pstate.object_idx, target_idx);
						if (hit_offset) {
							if (mission.train_craft_type) {
								/* Briefing/training/combat: teleport player back
								 * to the spawn snapshot. */
								pl->current_speed = 0;
								pc->throttle_speed = 0;
								pl->world_x = pl->world_x_prev = gatepreviousx[3];
								pl->world_y = pl->world_y_prev = gatepreviousy[3];
								pl->world_z = pl->world_z_prev = gatepreviousz[3];
								pl->roll = gatepreviousroll[3];
								pl->pitch = gatepreviouspitch[3];
								pc->orient_pitch = gatepreviouspitch[3];
								pl->heading = gatepreviousheading[3];
								pl->move_dirty = 1;
								pl->orient_dirty = 1;
								fsfx_triggersfx(((uint16_t)math2_getrandom() & 0x8000u) ? 0x1C : 0x1D,
												pstate.object_idx);
							} else {
								uint8_t tgt_genus = tgt->genus;
								/* Real combat: elastic momentum bounce on
								 * fighter / freighter / shuttle (genus 0..2). */
								if (tgt_genus == GENUS_FIGHTER || tgt_genus == GENUS_TRANSPORT ||
									tgt_genus == GENUS_UTILITY) {
									/* Mark target's spin-done flag so we don't
									 * re-bounce next frame. */
									pc->spin_done_flag = target_idx;
									/* (Full bounce maths replicated literally
									 * below; abridged for clarity.) */
									msg_messageprintf(MSG_COLLISION_OCCURRED);
									fsfx_triggersfx(0x1C, pstate.object_idx);
									fsfx_triggersfx(0x1D, target_idx);
								}
								if (inflight_collision || tgt_genus == GENUS_FREIGHTER ||
									tgt_genus == GENUS_STARSHIP || tgt_genus == GENUS_PLATFORM) {
									int32_t dot;
									TIE_FLIGHT_TRACE_COLLISION(pstate.object_idx, target_idx,
															   TIE_TRACE_COLLISION_CRAFT, hit_offset);
									collide_damagecraft(target_idx, hit_offset, pstate.object_idx, 0);
									if (pl->orient_dirty) {
										fview_calcrotatemove(pl->pitch, pl->heading, pl);
										fview_calcrotateorient(pl->roll, 0, pl);
									}
									dot = (int16_t)(craftx - craftxold) * (int32_t)pl->fwd_x +
										  (int16_t)(crafty - craftyold) * (int32_t)pl->fwd_y +
										  (int16_t)(craftz - craftzold) * (int32_t)pl->fwd_z;
									if (dot >= 0x40000000)
										dot = 0x3FFF0000;
									if (dot <= -0x40000000)
										dot = -0x3FFF0000;
									collide_damagecraft(pstate.object_idx, 0xFFFF, target_idx,
														(((dot >> 15) & 0x8000u) != 0) ? 1 : 0);
								}
							}
						}
					}
				}

				/* Proximity check: scan-to-identify + tractor prompt. */
				approxdist = collide_roughdistance3d(laserx - craftx, lasery - crafty, laserz - craftz);

				if (pstate.target_obj_idx == target_idx) {
					tgt_craft = tgt->craft_ptr;
					craftptr = tgt_craft;
					if (!tgt_craft->inspected) {
						uint32_t bw = species_table[tgt->ship_idx].bound_hwidth;
						if (bw > 0xBB8u)
							bw >>= 1;
						if ((int32_t)(4u * bw) > approxdist) {
							tgt_craft->inspected = 1;
							lasttargetnum = -3;
							{
								uint8_t fg_i = tgt->fg_idx;
								uint8_t spi = tgt_craft->species_idx;
								mission.kills_by_type[spi]++;
								fgstatus[fg_i].cond[4].detail++;
								if (fg_array[fg_i].special_craft == tgt_craft->craft_idx_in_fg)
									fgstatus[fg_i].cond_id[4].detail = 1;
							}
							msg_craftmessage(target_idx, craftptr, 0xAB);
						}
					}
				}

				/* Tractor prompt (_date.minute >= 2 = late mission phase). */
				if (_date.minute >= 2u) {
					EFGStruct* pfg = &fg_array[pstate.player_fg_idx];
					int elig = 0;
					if (pfg->pri_stop_fg_used && pfg->pri_stop_fg == tgt->fg_idx)
						elig = 1;
					if (pfg->sec_stop_fg_used && pfg->sec_stop_fg == tgt->fg_idx)
						elig = 1;
					if (elig) {
						CraftData* tc = tgt->craft_ptr;
						craftptr = tc;
						if (!tc->flight_flag) {
							pai_calcrotatedpoint(tgt, spec_data[tc->species_idx].cockpit_x,
												 spec_data[tc->species_idx].cockpit_y,
												 spec_data[tc->species_idx].cockpit_z);
							craftx += rotatedx;
							crafty += rotatedy;
							craftz += rotatedz;
							{
								uint32_t tractor_radius =
									(tgt->genus == GENUS_STARSHIP && mission.primary_complete == 1) ? 0x4000u
																									: 0x2000u;
								int32_t d = collide_roughdistance3d(craftx - laserx, crafty - lasery,
																	craftz - laserz);
								if ((uint32_t)d < tractor_radius && !pstate.space_confirm_action) {
									msg_messageprintf(MSG_TRACTOR_PROMPT);
									pstate.space_confirm_action = 2;
									timers[TIMER_SPACE_CONFIRM] = 1888;
								}
							}
						}
					}
				}
			}
		}

		/* Pass 1b: player-vs-statics in real combat. */
		if (!mission.train_craft_type && inflight_collision && !inflight_invulnerable) {
			uint16_t i;
			uint16_t static_obj_off = 14336;
			for (i = 0; i < 0x40u; i++) {
				if (staticobjects[i].species) {
					if (static_laserstaticcollide(pstate.object_idx, i)) {
						TIE_FLIGHT_TRACE_COLLISION(pstate.object_idx, static_obj_off,
												   TIE_TRACE_COLLISION_STATIC, -1);
						collide_damagecraft(pstate.object_idx, 0xFFFF, static_obj_off, 0);
					}
				}
				static_obj_off++;
			}
		}
	}

	/* Pass 2: genus-driven for every FlightObject slot. */
	for (projectile_idx = 0; projectile_idx < NUM_OBJECTS; projectile_idx++) {
		uint16_t self_idx;

		if (!objects[projectile_idx].ship_idx)
			continue;
		self_idx = objects[projectile_idx].self_idx;

		switch (objects[projectile_idx].genus) {
			case 3:
			case 4:
			case 5: {
				/* Cap-ship vs cap-ship swept test. */
				CraftData* src_craft = objects[projectile_idx].craft_ptr;
				uint16_t tgt_iter;
				if (src_craft->mode_byte == 30)
					break;
				if (src_craft->dock_state_flags & 0x40)
					break; /* no-collide flag */

				craftx = objects[projectile_idx].world_x;
				crafty = objects[projectile_idx].world_y;
				craftz = objects[projectile_idx].world_z;
				craftxold = objects[projectile_idx].world_x_prev;
				craftyold = objects[projectile_idx].world_y_prev;
				craftzold = objects[projectile_idx].world_z_prev;

				for (tgt_iter = 0; tgt_iter < NUM_CRAFTS; tgt_iter++) {
					CraftData* other;
					uint8_t cur_order;
					if (tgt_iter == projectile_idx)
						continue;
					if (tgt_iter == pstate.object_idx)
						continue;
					if (!objects[tgt_iter].ship_idx)
						continue;
					if (objects[tgt_iter].genus == GENUS_EXPLOSION)
						continue;
					if (tgt_iter == (uint16_t)src_craft->tow_slave_ref)
						continue;
					if (src_craft->mode_byte == 18 && tgt_iter == (uint16_t)src_craft->ai_target_ref)
						continue;
					other = objects[tgt_iter].craft_ptr;
					cur_order = other->current_order;
					if (cur_order == 50 || cur_order == 52 || cur_order == 49)
						continue;
					if (cur_order == 48) {
						uint8_t leader_order = objects[other->leader_obj_idx].craft_ptr->current_order;
						if (leader_order == 50 || leader_order == 49)
							continue;
					}
					if (other->dock_state_flags & 0x40)
						continue; /* no-collide flag */
					if (other->mode_byte == 18 || other->mode_byte == 21 || other->mode_byte == 30)
						continue;

					laserx = objects[tgt_iter].world_x;
					lasery = objects[tgt_iter].world_y;
					laserz = objects[tgt_iter].world_z;
					laserxold = objects[tgt_iter].world_x_prev;
					laseryold = objects[tgt_iter].world_y_prev;
					laserzold = objects[tgt_iter].world_z_prev;

					{
						uint16_t hit = collide_lasercraftcollide(tgt_iter, projectile_idx);
						if (hit) {
							TIE_FLIGHT_TRACE_COLLISION(projectile_idx, tgt_iter, TIE_TRACE_COLLISION_CRAFT,
													   (int16_t)hit);
							collide_damagecraft(projectile_idx, (int16_t)hit, tgt_iter, 0);
							collide_damagecraft(tgt_iter, 0xFFFF, projectile_idx, 0);
						}
					}
				}
				break;
			}

			case 6:
			case 7: {
				/* Laser/missile projectile vs every active object.
				 *
				 * The binary reads the projectile's intended target via
				 *   *(&objects[113].death_timer + 4 * projectile_idx)
				 * which is Watcom int16-pointer arithmetic for the
				 * (objects[]+9988+8*projectile_idx) byte offset. That
				 * address lands inside the warheads[] array adjacent to
				 * objects[] in the original DGROUP layout, specifically
				 * at warheads[projectile_idx - NUM_CRAFTS].target_obj. */
				uint16_t i;
				uint16_t hit_recorded = 0;
				uint16_t proj_target = warheads[projectile_idx - NUM_CRAFTS].target_obj;

				laserx = objects[projectile_idx].world_x;
				lasery = objects[projectile_idx].world_y;
				laserz = objects[projectile_idx].world_z;
				laserxold = objects[projectile_idx].world_x_prev;
				laseryold = objects[projectile_idx].world_y_prev;
				laserzold = objects[projectile_idx].world_z_prev;

				for (i = 0; i < WARHEAD_SLOT_END; i++) {
					if (!objects[i].ship_idx)
						continue;
					/* When a missile or laser dies in place via
					 * collide_makeobjectexplosion the slot keeps its warhead-
					 * range index but ship_idx becomes an explosion sprite.
					 * Filter it before indexing the weapon-species table. */
					if (objects[i].genus == GENUS_EXPLOSION)
						continue;
					if (i >= NUM_CRAFTS) {
						/* Projectile/warhead slot range: only consider
						 * slots whose species explodes at death. */
						if (!projectile_is_warhead_type[objects[i].ship_idx - WEAPON_SPECIES_BASE])
							continue;
						if (i == projectile_idx)
							continue;
						if (self_idx == objects[i].self_idx)
							continue;
					}
					if (i == self_idx)
						continue;
					if (i == pstate.object_idx && inflight_invulnerable)
						continue;
					if (self_idx == pstate.object_idx) {
						CraftData* cs = objects[i].craft_ptr;
						if (cs->default_order_ldr == 28 && cs->mode_byte == 18 && cs->mode_subbyte == 2 &&
							self_idx == (uint16_t)cs->ai_target_ref)
							continue;
					}
					/* Targeting filter: only test collision if the projectile
					 * is aimed at the player, this object IS the projectile's
					 * intended target, or the player fired the projectile. */
					if (proj_target != pstate.object_idx && i != proj_target && self_idx != pstate.object_idx)
						continue;

					craftx = objects[i].world_x;
					crafty = objects[i].world_y;
					craftz = objects[i].world_z;
					craftxold = objects[i].world_x_prev;
					craftyold = objects[i].world_y_prev;
					craftzold = objects[i].world_z_prev;

					{
						uint16_t hit = collide_lasercraftcollide(projectile_idx, i);
						if (hit) {
							TIE_FLIGHT_TRACE_COLLISION(projectile_idx, i, TIE_TRACE_COLLISION_PROJECTILE,
													   (int16_t)hit);
							collide_updatehits(projectile_idx, 1);
							if (i < NUM_CRAFTS) {
								collide_laserhitcraft(projectile_idx, i, (int16_t)hit);
							} else {
								collide_makeobjectexplosion(projectile_idx,
															(uint8_t)((math2_getrandom() & 1) + 127));
								collide_makeobjectexplosion(i, (uint8_t)((math2_getrandom() & 1) + 127));
							}
							hit_recorded = 1;
							break;
						}
					}
				}

				if (!hit_recorded && !mission.train_craft_type) {
					uint16_t m;
					for (m = 0; m < 0x40u; m++) {
						if (staticobjects[m].species) {
							if (static_laserstaticcollide(projectile_idx, m)) {
								TIE_FLIGHT_TRACE_COLLISION(projectile_idx,
														   (uint16_t)(m + OBJ_REF_STATIC_BASE),
														   TIE_TRACE_COLLISION_STATIC, -1);
								static_laserhitstatic(projectile_idx, m);
								collide_updatehits(projectile_idx, 1);
								break;
							}
						}
					}
				}
				break;
			}

			case 0xB:
				/* Probe/buoy: just stage the swept-segment globals. */
				laserx = objects[projectile_idx].world_x;
				lasery = objects[projectile_idx].world_y;
				laserz = objects[projectile_idx].world_z;
				laserxold = objects[projectile_idx].world_x_prev;
				laseryold = objects[projectile_idx].world_y_prev;
				laserzold = objects[projectile_idx].world_z_prev;
				break;

			default:
				break;
		}
	}
}
