#include "tie/move.h"
#include "tie/bpflight.h"
#include "tie/collide.h"
#include "tie/create.h"
#include "tie/draw.h"
#include "tie/fview.h"
#include "tie/gate.h"
#include "tie/laser.h"
#include "tie/math2.h"
#include "tie/modelbounds.h"
#include "tie/modelmesh.h"
#include "tie/pai.h"
#include "tie/paiman.h"
#include "tie/spec.h"
#include "tie/starship.h"
#include "tie/tie.h"
#include "tie/trig2.h"
#include "tie_runtime/timing/flight_timing.h"
#include "tie_runtime/timing/flight_timing_state.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* MOVE-owned globals (watdbg: D:\GAMES\XTIE\CODE\move.c)              */
/* ------------------------------------------------------------------ */

/* Unreferenced in the demo binary; 4 of 12 entries populated (may be used
 * in retail or by code not exercised in the demo). */
static const uint16_t homingindex[12] = {
	0, 0, 0, 7, 0, 0, 5390, 3612, 3598, 0, 0, 0,
};

/* Pitch/heading angular slew rate for genus 6/7 missiles, indexed by:
 *    idx = homing_idx_lookup[obj->ship_idx] + wh->homing_tier
 *
 * Values extracted from the shipped binary. There's a visible pattern --
 * zero entries at indices 0, 7, 14, 21, 28 with ascending values in
 * between -- that LOOKS like five 7-entry tiers, but the index values
 * produced by the formula above don't land cleanly on those boundaries
 * (they scatter across tier interiors), so the tier interpretation is a
 * guess and is not used by this code. The table is just indexed
 * linearly. */
static const uint16_t maxhomingrate[35] = {
	0,    1024, 2048, 3072, 5120, 7168, 9216,  0,     512,   1024, 2048, 3072,
	4608, 6144, 0,    2048, 4096, 5120, 10240, 14336, 18432, 0,    32,   64,
	80,   96,   112,  128,  0,    512,  1024,  1280,  1536,  1792, 2048,
};

/* Paired deceleration / acceleration rate (speed units per second /
 * framerate) used when the missile is off-track (decel) or below
 * wh->min_speed (accel). Same indexing as maxhomingrate. The last 14
 * entries are all zero -- missiles whose formula lands there simply
 * don't change speed. */
static const uint16_t maxdeccelrate[35] = {
	0,   50,  100,  200, 300, 400, 500, 0, 25, 50, 100, 150, 200, 250, 0, 100, 200, 400,
	600, 800, 1000, 0,   0,   0,   0,   0, 0,  0,  0,   0,   0,   0,   0, 0,   0,
};

/* Defined image of the flat data-segment lookup addressed by ship_idx.
 * It intentionally preserves the boot-time radar bytes and therefore does
 * not reflect later 640×480 radar-table mutations for indices 57..130. */
static const uint8_t homing_idx_lookup[256] = {
	0x10, 0x09, 0x10, 0x0A, 0x10, 0x0A, 0x0F, 0x0B, 0x0F, 0x0C, 0x0F, 0x0C, 0x0E, 0x0D, 0x0E, 0x0E,
	0x0E, 0x0E, 0x0D, 0x0F, 0x0D, 0x0F, 0x0C, 0x10, 0x0C, 0x10, 0x0B, 0x11, 0x0B, 0x11, 0x0A, 0x12,
	0x0A, 0x12, 0x09, 0x12, 0x08, 0x13, 0x08, 0x13, 0x07, 0x13, 0x06, 0x14, 0x06, 0x14, 0x05, 0x15,
	0x04, 0x15, 0x03, 0x15, 0x02, 0x15, 0x01, 0x15, 0x00, 0x00, 0x12, 0x01, 0x12, 0x02, 0x12, 0x03,
	0x12, 0x04, 0x11, 0x05, 0x11, 0x06, 0x11, 0x07, 0x11, 0x08, 0x10, 0x09, 0x10, 0x0A, 0x10, 0x0A,
	0x0F, 0x0B, 0x0F, 0x0C, 0x0F, 0x0C, 0x0E, 0x0D, 0x0E, 0x0E, 0x0E, 0x0E, 0x0D, 0x0F, 0x0D, 0x0F,
	0x0C, 0x10, 0x0C, 0x10, 0x0B, 0x11, 0x0B, 0x11, 0x0A, 0x12, 0x0A, 0x12, 0x09, 0x12, 0x08, 0x13,
	0x08, 0x13, 0x07, 0x13, 0x06, 0x14, 0x06, 0x14, 0x05, 0x15, 0x04, 0x15, 0x03, 0x15, 0x02, 0x15,
	0x01, 0x15, 0x00, 0x69, 0x63, 0x13, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x07,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x0E, 0x15, 0x1C, 0x0E, 0x0E, 0x0E, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x08, 0x00, 0x0C, 0x00, 0x14, 0x00, 0x1C, 0x00, 0x24, 0x00,
	0x00, 0x00, 0x02, 0x00, 0x04, 0x00, 0x08, 0x00, 0x0C, 0x00, 0x12, 0x00, 0x18, 0x00, 0x00, 0x00,
	0x08, 0x00, 0x10, 0x00, 0x14, 0x00, 0x28, 0x00, 0x38, 0x00, 0x48, 0x00, 0x00, 0x20, 0x00, 0x40,
	0x00, 0x50, 0x00, 0x60, 0x00, 0x70, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x04, 0x00,
	0x05, 0x00, 0x06, 0x00, 0x07, 0x00, 0x08, 0x00, 0x00, 0x32, 0x00, 0x64, 0x00, 0xC8, 0x00, 0x2C,
	0x01, 0x90, 0x01, 0xF4, 0x01, 0x00, 0x00, 0x19, 0x00, 0x32, 0x00, 0x64, 0x00, 0x96, 0x00, 0xC8,
};

/* Formation position tables + throttle LUT are now owned by paiman.c
 * (their watdbg origin). MOVE only needs the externs. */

/* ------------------------------------------------------------------ */
/* External state consumed by MOVE (owned by other modules)           */
/* ------------------------------------------------------------------ */

/* tie.c: current cutscene focus */
/* tie.c: pai output */
/* tie.c: fview output */

/* tie.c: player craft spec_num */

/* World-box clamp: +/- 2^24 world units (the playable cube). */
#define WORLD_CLAMP_POS 0x01000000
#define WORLD_CLAMP_NEG (-0x01000000)

/* ------------------------------------------------------------------ */
/* move_updatexyz                                                      */
/* ------------------------------------------------------------------ */

// FUNCTION: TIE 0x32E48
void move_updatexyz(FlightObject* obj) {
	obj->world_x += trig2_xmovedist;
	if (obj->world_x < WORLD_CLAMP_NEG)
		obj->world_x = WORLD_CLAMP_NEG;
	if (obj->world_x > WORLD_CLAMP_POS)
		obj->world_x = WORLD_CLAMP_POS;

	obj->world_y += trig2_ymovedist;
	if (obj->world_y < WORLD_CLAMP_NEG)
		obj->world_y = WORLD_CLAMP_NEG;
	if (obj->world_y > WORLD_CLAMP_POS)
		obj->world_y = WORLD_CLAMP_POS;

	obj->world_z += trig2_zmovedist;
	if (obj->world_z < WORLD_CLAMP_NEG)
		obj->world_z = WORLD_CLAMP_NEG;
	if (obj->world_z > WORLD_CLAMP_POS)
		obj->world_z = WORLD_CLAMP_POS;
}

/* ------------------------------------------------------------------ */
/* Helpers for move_moveobjects                                        */
/* ------------------------------------------------------------------ */

/* One of the 3 external-push velocity accumulators at CraftData+0x92/96/9A.
 * Bleeds |accum| toward 0 at min(|accum|, cap) / framerate per tick, adds
 * the bled amount to *out_vel. If the step rounds to zero, dump the entire
 * remaining accum. */
static void apply_push_accum(uint16_t obj_idx, unsigned int axis, int32_t* accum_ptr, int32_t cap,
							 int32_t* out_vel) {
	int32_t accum = *accum_ptr;
	if (accum == 0)
		return;

	int32_t clamped = accum;
	if (clamped > cap)
		clamped = cap;
	if (clamped < -cap)
		clamped = -cap;

	int32_t step;
	if (TieFlightTiming_IsHighRate()) {
		TieMoveTimingState* state = TieFlightTimingState_Move(obj_idx, &objects[obj_idx]);
		const int8_t sign = clamped < 0 ? -1 : 1;
		if (state->push_sign[axis] != sign) {
			state->push_remainder[axis] = 0;
			state->push_sign[axis] = sign;
		}
		step = TieFlightTiming_ScaleWithRemainder(clamped, frameticks, 236, &state->push_remainder[axis]);
		if ((accum > 0 && step > accum) || (accum < 0 && step < accum))
			step = accum;
	} else {
		step = (int32_t)((int16_t)clamped / (int16_t)framerate);
		if (step == 0)
			step = accum;
	}

	*out_vel += step;
	*accum_ptr = accum - step;
	if (*accum_ptr == 0 && TieFlightTiming_IsHighRate()) {
		TieMoveTimingState* state = TieFlightTimingState_Move(obj_idx, &objects[obj_idx]);
		state->push_remainder[axis] = 0;
		state->push_sign[axis] = 0;
	}
}

/* Destruction dispatch when obj->reserved_2c hits zero. Returns:
 *   1 -> slot killed, caller must skip the rest of the per-obj body
 *   0 -> continue normally
 * Mirrors the first genus-switch at 0x30b99. */
static int dispatch_death(uint16_t obj_idx, FlightObject* obj) {
	uint8_t genus = obj->genus;

	switch (genus) {
		case GENUS_FIGHTER: {
			/* Retail order: blowoff first (which itself consumes RNG),
			 * then read the random byte for the 127/128 ember pick. */
			create_blowoffcomponent(obj_idx, 1);
			int16_t rnd = math2_getrandom();
			collide_makeobjectexplosion(obj_idx, (char)(127 + (rnd & 1)));
			return 0;
		}
		case GENUS_TRANSPORT:
		case GENUS_UTILITY:
		case GENUS_FREIGHTER:
		case GENUS_STARSHIP:
		case GENUS_PLATFORM: {
			/* Capital-class ships always use the dedicated 130 explosion;
			 * the size split only adds the model lock + damage_state seed
			 * for ships large enough that the engine renders an oversized
			 * ember. No RNG consumption in retail. */
			uint16_t bound = species_table[obj->ship_idx].bound_hwidth;
			if (bound > 0x578) {
				if (TieProfile_UsesTie98Logic()) {
					collide_makeobjectexplosion(obj_idx, (char)130);
					obj->damage_state = (uint8_t)(modelbounds_getmaxextent(obj->ship_idx) >> 9);
				} else {
					draw_lockshipfileptrs(obj->ship_idx);
					collide_makeobjectexplosion(obj_idx, (char)130);
					int16_t length = (int16_t)objectblockptr->length;
					uint8_t lod = objectblockptr->model_scale_shift;
					obj->damage_state = (uint8_t)(length >> (9 - lod));
				}
			} else {
				collide_makeobjectexplosion(obj_idx, (char)130);
			}
			return 0;
		}
		case GENUS_PROJECTILE_PLAYER:
		case GENUS_PROJECTILE_NPC: {
			/* Missile/projectile death gate. Retail reads byte_C5463[ship_idx]
			 * (indexed by weapon species 137..154): 0 = silent removal,
			 * nonzero = full explosion (always picks the 129 chunk variant). */
			if (projectile_is_warhead_type[laser_species_idx(obj->ship_idx)] == 0) {
				obj->ship_idx = 0;
				return 1;
			}
			collide_makeobjectexplosion(obj_idx, (char)129);
			return 0;
		}
		case GENUS_DEBRIS: {
			/* Debris ember randomises between the 127 and 128 sprites. */
			int16_t rnd = math2_getrandom();
			collide_makeobjectexplosion(obj_idx, (char)(127 + (rnd & 1)));
			return 0;
		}
		default:
			obj->ship_idx = 0;
			return 1;
	}
}

/* Death-spin roll decay. Bleeds |spin_rate| toward zero at 4096/framerate
 * per tick; when decay crosses zero, writes 0xFFFF to craft->spin_done_flag
 * marking the spin complete. Always contributes (spin_pre_decay/framerate)*4
 * to obj->roll. */
static void apply_roll_decay(uint16_t obj_idx, FlightObject* obj) {
	int16_t spin = obj->spin_rate;
	if (spin == 0)
		return;
	if (TieFlightTiming_IsHighRate()) {
		TieMoveTimingState* state = TieFlightTimingState_Move(obj_idx, obj);
		const int16_t sign = spin < 0 ? -1 : 1;
		if (state->spin_sign != sign) {
			state->spin_remainder[0] = 0;
			state->spin_remainder[1] = 0;
			state->spin_sign = sign;
		}
	}

	if (obj_idx < NUM_CRAFTS) {
		CraftData* craft = objects[obj_idx].craft_ptr;

		if (craft->spin_done_flag != 0xFFFF) {
			int16_t step;
			if (TieFlightTiming_IsHighRate()) {
				TieMoveTimingState* state = TieFlightTimingState_Move(obj_idx, obj);
				step = (int16_t)TieFlightTiming_ScaleWithRemainder(4096, frameticks, 236,
																   &state->spin_remainder[0]);
			} else {
				step = (int16_t)((uint16_t)4096 / framerate);
			}
			int16_t new_spin;
			int crossed;

			if ((spin & 0x8000) == 0) { /* spin > 0: decay toward 0 */
				new_spin = (int16_t)(spin - step);
				crossed = (new_spin <= 0);
			} else { /* spin < 0: decay toward 0 */
				new_spin = (int16_t)(spin + step);
				crossed = (new_spin >= 0);
			}
			obj->spin_rate = new_spin;
			if (crossed) {
				obj->spin_rate = 0;
				spin = 0; /* affect the roll update below */
				craft->spin_done_flag = 0xFFFF;
				if (TieFlightTiming_IsHighRate()) {
					TieMoveTimingState* state = TieFlightTimingState_Move(obj_idx, obj);
					state->spin_remainder[0] = 0;
					state->spin_remainder[1] = 0;
					state->spin_sign = 0;
				}
			}
		}
	}

	/* Roll contribution uses the pre-decay spin value (or 0 if decay
	 * just crossed zero). Matches the binary's common-tail semantics. */
	int16_t roll_delta;
	if (TieFlightTiming_IsHighRate()) {
		TieMoveTimingState* state = TieFlightTimingState_Move(obj_idx, obj);
		roll_delta = (int16_t)TieFlightTiming_ScaleWithRemainder((int32_t)spin * 4, frameticks, 236,
																 &state->spin_remainder[1]);
	} else {
		roll_delta = (int16_t)((spin / (int16_t)framerate) * 4);
	}
	obj->orient_dirty = 1;
	obj->roll = (int16_t)(obj->roll + roll_delta);
}

/* Homing slew for genus 6/7. Returns:
 *   1  -> normal path (either homing was skipped or completed), caller
 *         should fall through to the move-vector integration tail.
 *  -1  -> target object is onworld but dead -> explode, caller must break.
 *
 * Fills rotatedx/y/z with target world position minus source world position
 * and xyangle/zangle via trig2_ctop before computing pitch/heading deltas.
 *
 * wh points to an 8-byte WarheadRecord; reading beyond that is UB. Only
 * homing_tier, target_obj, sub_obj_idx, and min_speed are touched.
 */
static int apply_homing(uint16_t obj_idx, FlightObject* obj, WarheadRecord* wh) {
	/* homing_tier == 0 => craft-fired projectile with no homing. */
	if (wh->homing_tier == 0)
		return 1;

	uint16_t target_idx_w = wh->target_obj;
	if (target_idx_w == 0xFFFF)
		return 1;

	/* Target slot dead (and onworld)? Caller should explode us. */
	if (target_idx_w < (uint16_t)NUM_OBJECTS && !objects[target_idx_w].ship_idx)
		return -1;

	/* ship_idx is a projectile type, not a species-table index. */
	uint32_t idx = (uint32_t)homing_idx_lookup[obj->ship_idx] + wh->homing_tier;

	uint16_t target_idx = target_idx_w;
	uint16_t sub_obj = wh->sub_obj_idx;

	create_getworldposition(target_idx, 0);

	if (target_idx >= (uint16_t)NUM_CRAFTS) {
		rotatedx = worldlocx;
		rotatedy = worldlocy;
		rotatedz = worldlocz;
	} else if (TieProfile_UsesTie98Logic()) {
		/* TIE98 0x455942-0x455A70. */
		if (sub_obj != 0xFFFF) {
			const uint8_t model_type = objects[target_idx].ship_idx;
			int side = modelmesh_getcenterx(model_type, sub_obj);
			int longitudinal = modelmesh_getcentery(model_type, sub_obj);
			int vertical = modelmesh_getcenterz(model_type, sub_obj);
			if (model_type == 53) {
				pai_calcrotatedpoint(&objects[target_idx], side >> 1, vertical >> 1, (-longitudinal) >> 1);
				rotatedx = (int32_t)((uint32_t)rotatedx << 1);
				rotatedy = (int32_t)((uint32_t)rotatedy << 1);
				rotatedz = (int32_t)((uint32_t)rotatedz << 1);
			} else {
				pai_calcrotatedpoint(&objects[target_idx], side, vertical, -longitudinal);
			}
		} else {
			rotatedx = 0;
			rotatedy = 0;
			rotatedz = 0;
		}
		rotatedx += worldlocx;
		rotatedy += worldlocy;
		rotatedz += worldlocz;
	} else {
		draw_lockshipfileptrs(objects[target_idx].ship_idx);
		ShipModelMesh* mesh = (sub_obj == 0xFFFF) ? &componentblockptr[-1] /* mirrors binary fallback */
												  : &componentblockptr[sub_obj];
		/* Unaligned dword loads in the binary:
		 *   (dword at mesh+14) >> 17 = center_side >> 1
		 *   (dword at mesh+18) >> 17 = center_up   >> 1
		 *   (dword at mesh+16) >> 17 = center_fwd  >> 1
		 * See CLAUDE.md 'Watcom unaligned dword load pattern'. */
		int16_t ofs_side = (int16_t)(mesh->center_side >> 1);
		int16_t ofs_up = (int16_t)(mesh->center_up >> 1);
		int16_t ofs_fwd = (int16_t)(mesh->center_fwd >> 1);
		pai_calcrotatedpoint(&objects[target_idx], ofs_side, ofs_up, (int16_t)-ofs_fwd);

		/* ShipModelData.model_scale_shift byte scales the rotated offsets.
		 * Binary emits `shl reg, cl` (sign-agnostic); shifting a
		 * negative int32_t in C is UB, so route through uint32_t. */
		const int shift = objectblockptr->model_scale_shift;
		rotatedy = (int32_t)((uint32_t)rotatedy << shift);
		rotatedx = worldlocx + (int32_t)((uint32_t)rotatedx << shift);
		rotatedy += worldlocy;
		rotatedz = worldlocz + (int32_t)((uint32_t)rotatedz << shift);
	}

	/* Source: this missile's world position. */
	create_getworldposition(obj_idx, 0);
	rotatedx -= worldlocx;
	rotatedz -= worldlocz;
	rotatedy -= worldlocy;
	trig2_ctop(rotatedx, rotatedy, rotatedz);

	/* Pitch slew. xyangle = angle to target in X-Y (vertical) plane. */
	int16_t pitch_delta = (int16_t)(trig2_xyangle - obj->pitch);
	TieMoveTimingState* high_rate =
		TieFlightTiming_IsHighRate() ? TieFlightTimingState_Move(obj_idx, obj) : NULL;
	int16_t pitch_rate = high_rate ? (int16_t)TieFlightTiming_ScaleWithRemainder(
										 maxhomingrate[idx], frameticks, 236, &high_rate->homing_remainder[0])
								   : (int16_t)(maxhomingrate[idx] / framerate);
	int16_t abs_pd = (pitch_delta < 0) ? (int16_t)-pitch_delta : pitch_delta;
	if (abs_pd > pitch_rate) {
		int16_t step = (pitch_delta < 0) ? (int16_t)-pitch_rate : pitch_rate;
		obj->pitch = (int16_t)(obj->pitch + step);
		/* 200 is the missile homing-decel floor: while off-track and above
		 * the floor, bleed speed; clamp back up if decel overshoots below. */
		if ((uint16_t)obj->current_speed > 200) {
			if (high_rate && high_rate->homing_speed_sign != -1) {
				high_rate->homing_remainder[2] = 0;
				high_rate->homing_speed_sign = -1;
			}
			int16_t decel =
				high_rate ? (int16_t)TieFlightTiming_ScaleWithRemainder(maxdeccelrate[idx], frameticks, 236,
																		&high_rate->homing_remainder[2])
						  : (int16_t)(maxdeccelrate[idx] / framerate);
			int16_t ns = (int16_t)(obj->current_speed - decel);
			obj->current_speed = ns;
			if ((uint16_t)ns < 200)
				obj->current_speed = 200;
		}
	} else {
		obj->pitch = trig2_xyangle;
		if (high_rate)
			high_rate->homing_remainder[0] = 0;
		if ((uint16_t)obj->current_speed < wh->min_speed) {
			if (high_rate && high_rate->homing_speed_sign != 1) {
				high_rate->homing_remainder[2] = 0;
				high_rate->homing_speed_sign = 1;
			}
			const int16_t accel =
				high_rate ? (int16_t)TieFlightTiming_ScaleWithRemainder(maxdeccelrate[idx], frameticks, 236,
																		&high_rate->homing_remainder[2])
						  : (int16_t)(maxdeccelrate[idx] / framerate);
			obj->current_speed = (int16_t)(obj->current_speed + accel);
		}
	}

	/* Heading slew. zangle = angle in horizontal plane. */
	int16_t heading_rate =
		high_rate ? (int16_t)TieFlightTiming_ScaleWithRemainder(maxhomingrate[idx], frameticks, 236,
																&high_rate->homing_remainder[1])
				  : (int16_t)(maxhomingrate[idx] / framerate);
	int16_t heading_delta = (int16_t)(trig2_zangle - obj->heading);
	int16_t abs_hd = (heading_delta < 0) ? (int16_t)-heading_delta : heading_delta;
	if (abs_hd > heading_rate) {
		int16_t step = (heading_delta < 0) ? (int16_t)-heading_rate : heading_rate;
		obj->heading = (int16_t)(obj->heading + step);
	} else {
		obj->heading = trig2_zangle;
		if (high_rate)
			high_rate->homing_remainder[1] = 0;
	}

	obj->orient_dirty = 1;
	obj->move_dirty = obj->orient_dirty;
	fview_calcrotatemove(obj->heading, obj->pitch, obj);
	obj->moveX = (int16_t)craftmoveX;
	obj->moveY = (int16_t)craftmoveY;
	obj->moveZ = (int16_t)craftmoveZ;
	return 1;
}

static void compute_move_distances(uint16_t obj_idx, FlightObject* obj, uint16_t speed_per_tick) {
	if (!TieFlightTiming_IsHighRate()) {
		trig2_xmovedist = ((int32_t)speed_per_tick * obj->moveX) >> 15;
		trig2_ymovedist = ((int32_t)speed_per_tick * obj->moveY) >> 15;
		trig2_zmovedist = ((int32_t)speed_per_tick * obj->moveZ) >> 15;
		return;
	}

	TieMoveTimingState* state = TieFlightTimingState_Move(obj_idx, obj);
	const int64_t speed = ((int64_t)4660 * obj->current_speed + 128) >> 8;
	const int64_t divisor = (int64_t)236 << 15;
	const int16_t axes[3] = { obj->moveX, obj->moveY, obj->moveZ };
	int32_t* distances[3] = { &trig2_xmovedist, &trig2_ymovedist, &trig2_zmovedist };
	for (unsigned int axis = 0; axis < 3; ++axis) {
		const int64_t numerator = speed * frameticks * axes[axis] + state->position_remainder[axis];
		*distances[axis] = (int32_t)(numerator / divisor);
		state->position_remainder[axis] = numerator % divisor;
	}
}

/* Shared integration tail for genus 6/7, 11, 13. */
static void integrate_from_move_vec(uint16_t obj_idx, FlightObject* obj, uint16_t speed_per_tick) {
	if (obj->move_dirty)
		fview_calcrotatemove(obj->heading, obj->pitch, obj);

	compute_move_distances(obj_idx, obj, speed_per_tick);
	move_updatexyz(obj);
}

/* Manned-craft dispatch (genus 0..4). Resolves move vector from current
 * orientation, applies 3 external-push accumulators, integrates, then
 * pins a linked slave craft (tow_slave_ref) to a fixed world-frame offset. */
static void dispatch_craft(uint16_t obj_idx, FlightObject* obj, uint16_t speed_per_tick) {
	CraftData* craft = obj->craft_ptr;

	if (obj->move_dirty)
		fview_calcrotatemove(obj->heading, obj->pitch, obj);

	compute_move_distances(obj_idx, obj, speed_per_tick);

	if (craft->status_flags != 0) {
		int32_t cap;
		if (craft->mode_byte == 18)
			cap = 250;
		else if (craft->mode_byte == 30)
			cap = 750;
		else
			cap = (uint16_t)spec_data[craft->species_idx].max_push_rate;

		apply_push_accum(obj_idx, 0, &craft->push_accum_x, cap, &trig2_xmovedist);
		apply_push_accum(obj_idx, 1, &craft->push_accum_y, cap, &trig2_ymovedist);
		apply_push_accum(obj_idx, 2, &craft->push_accum_z, cap, &trig2_zmovedist);
	}

	move_updatexyz(obj);

	/* Linked slave craft: pin to a fixed offset behind the leader.
	 * tow_slave_ref is an obj-ref (see OBJ_REF_* in tie.h); only FlightObject
	 * slot refs are valid leaders — static refs would index past the array. */
	uint16_t link_idx = (uint16_t)craft->tow_slave_ref;
	if (link_idx < OBJ_REF_STATIC_BASE) {
		CraftData* tgt_craft = objects[link_idx].craft_ptr;
		int16_t ofs_z;
		/* genus > GENUS_UTILITY: tgt is freighter or larger -> use cap-ship offset table. */
		if (objects[link_idx].genus > GENUS_UTILITY) {
			if (objects[obj_idx].genus > GENUS_UTILITY)
				ofs_z = (int16_t)(spec_data[craft->species_idx].dock_active_heavy -
								  spec_data[tgt_craft->species_idx].dock_passive_heavy);
			else
				ofs_z = (int16_t)(spec_data[craft->species_idx].dock_active_heavy -
								  spec_data[tgt_craft->species_idx].dock_passive_light);
		} else {
			ofs_z = (int16_t)(spec_data[craft->species_idx].dock_active_light -
							  spec_data[tgt_craft->species_idx].dock_passive_light);
		}
		pai_calcrotatedpoint(obj, 0, ofs_z, spec_data[tgt_craft->species_idx].dock_fwd);

		FlightObject* slave = &objects[link_idx];
		slave->world_x_prev = slave->world_x;
		slave->world_y_prev = slave->world_y;
		slave->world_z_prev = slave->world_z;
		slave->world_x = rotatedx + obj->world_x;
		slave->world_y = rotatedy + obj->world_y;
		slave->world_z = rotatedz + obj->world_z;
	}
}

/* ------------------------------------------------------------------ */
/* move_moveobjects                                                    */
/* ------------------------------------------------------------------ */

/* Flight-sim frame counter. Incremented once per move_moveobjects call,
 * i.e. once per logical flight frame (one position-integration step) —
 * not per host tick. Exposed via move_flight_frame() so the snapshot
 * emitter can stamp it; consumers compare it across snapshots to tell
 * whether the sim actually advanced (see TieSnapshot.flight_frame). */
static uint32_t s_flight_frame = 0;

uint32_t move_flight_frame(void) { return s_flight_frame; }

/* Deterministic integer sqrt (Newton). Avoids float in the sim path. */
static int32_t move_isqrt(int64_t n) {
	if (n <= 0)
		return 0;
	int64_t x = n, y = (x + 1) / 2;
	while (y < x) {
		x = y;
		y = (x + n / x) / 2;
	}
	return (int32_t)x;
}

/* OPTIONAL (pai_friendly_separation, default on): gently push apart same-FG
 * AI craft whose hulls overlap, so wingmen ganging up on one target don't
 * interpenetrate. Position-only -- heading/velocity are untouched, so guns
 * stay on target. The player is never moved. Runs after integration so it
 * works on final positions; both world_* and world_*_prev are shifted by the
 * same nudge, so no spurious velocity is introduced. Diverges from the binary
 * by design (the original has no fighter-vs-fighter separation). */
static void move_separate_friendly_overlap(void) {
	if (!pai_friendly_separation)
		return;

	for (uint16_t a = 0; a < NUM_CRAFTS; ++a) {
		uint8_t ga = objects[a].genus;
		if (!objects[a].ship_idx || objects[a].category)
			continue;
		if (ga != GENUS_FIGHTER && ga != GENUS_TRANSPORT && ga != GENUS_UTILITY)
			continue;
		if (a == (uint16_t)pstate.object_idx)
			continue;

		for (uint16_t b = (uint16_t)(a + 1); b < NUM_CRAFTS; ++b) {
			uint8_t gb = objects[b].genus;
			if (!objects[b].ship_idx || objects[b].category)
				continue;
			if (gb != GENUS_FIGHTER && gb != GENUS_TRANSPORT && gb != GENUS_UTILITY)
				continue;
			if (b == (uint16_t)pstate.object_idx)
				continue;
			if (objects[a].fg_idx != objects[b].fg_idx)
				continue;

			int64_t dx = (int64_t)objects[a].world_x - objects[b].world_x;
			int64_t dy = (int64_t)objects[a].world_y - objects[b].world_y;
			int64_t dz = (int64_t)objects[a].world_z - objects[b].world_z;
			int64_t d2 = dx * dx + dy * dy + dz * dz;

			/* Minimum centre spacing = sum of hull half-extents
			 * (collision_radius/4 each), i.e. just touching. */
			int32_t min_sep = (objects[a].collision_radius + objects[b].collision_radius) / 4;
			if (min_sep <= 0 || d2 >= (int64_t)min_sep * min_sep)
				continue;

			int32_t d = move_isqrt(d2);
			if (d == 0) {
				/* Exactly coincident: shove along +X so they part. */
				dx = 1;
				dy = 0;
				dz = 0;
				d = 1;
			}

			/* Resolve half the overlap per craft, clamped per frame so the
			 * correction is gradual rather than a teleport. */
			int32_t push = (min_sep - d) / 2;
			if (push > 256)
				push = 256;

			int32_t nx = (int32_t)(dx * push / d);
			int32_t ny = (int32_t)(dy * push / d);
			int32_t nz = (int32_t)(dz * push / d);

			objects[a].world_x += nx;
			objects[a].world_x_prev += nx;
			objects[a].world_y += ny;
			objects[a].world_y_prev += ny;
			objects[a].world_z += nz;
			objects[a].world_z_prev += nz;
			objects[b].world_x -= nx;
			objects[b].world_x_prev -= nx;
			objects[b].world_y -= ny;
			objects[b].world_y_prev -= ny;
			objects[b].world_z -= nz;
			objects[b].world_z_prev -= nz;
		}
	}
}

// FUNCTION: TIE 0x32448
void move_moveobjects(void) {
	s_flight_frame++;

	/* Step 1: snapshot the player's rotated laser-muzzle offset in world
	 * frame. laser_origin_d{x,y,z}_prev holds last frame; _d{x,y,z} is new.
	 * COLLIDE_collisions later uses (shooter.world + laser_origin_d*)
	 * to build the swept laser segment. */
	pai_calcrotatedpoint(pstate.player, 0, spec_data[pstate.player_spec_num].gun_muzzle_up,
						 spec_data[pstate.player_spec_num].gun_muzzle_fwd);
	pstate.laser_origin_dx_prev = pstate.laser_origin_dx;
	pstate.laser_origin_dy_prev = pstate.laser_origin_dy;
	pstate.laser_origin_dz_prev = pstate.laser_origin_dz;
	pstate.laser_origin_dx = rotatedx;
	pstate.laser_origin_dy = rotatedy;
	pstate.laser_origin_dz = rotatedz;

	/* Step 2: per-object integration. */
	for (uint16_t i = 0; i < (uint16_t)NUM_OBJECTS; i++) {
		FlightObject* obj = &objects[i];
		if (obj->ship_idx == 0)
			continue;

		/* Death countdown. */
		int16_t dt = obj->death_timer;
		if (dt != 0) {
			int16_t new_dt = (int16_t)(dt - (int16_t)frameticks);
			if (new_dt < 0)
				new_dt = 0;
			obj->death_timer = new_dt;
			if (new_dt == 0) {
				if (dispatch_death(i, obj))
					continue;
			}
		}

		/* Cutscene focus snapshot. */
		if (mission.train_craft_type && i == pstate.object_idx)
			gate_savegatelastpos();

		/* Save previous world pos. */
		obj->world_x_prev = obj->world_x;
		obj->world_y_prev = obj->world_y;
		obj->world_z_prev = obj->world_z;

		/* Death-spin roll decay. Always runs after the prev-save; may
		 * increment roll every tick even after decay has finished. */
		apply_roll_decay(i, obj);

		/* Per-tick distance per unit move vector. */
		uint16_t speed_per_tick = obj->current_speed && !TieFlightTiming_IsHighRate()
									  ? math2_mphconvert(obj->current_speed, framerate)
									  : 0;

		/* Genus dispatch -- different integration paths. */
		switch (obj->genus) {
			case GENUS_FIGHTER:
			case GENUS_TRANSPORT:
			case GENUS_UTILITY:
			case GENUS_FREIGHTER:
			case GENUS_STARSHIP:
				dispatch_craft(i, obj, speed_per_tick);
				break;

			case GENUS_PROJECTILE_PLAYER:
			case GENUS_PROJECTILE_NPC: {
				/* For projectile slots, obj->craft_ptr addresses an 8-byte
				 * WarheadRecord, not a CraftData. Cast explicitly so we only
				 * touch the valid 8 bytes. */
				WarheadRecord* wh = (WarheadRecord*)obj->craft_ptr;
				int r = apply_homing(i, obj, wh);
				if (r < 0) {
					collide_makeobjectexplosion(i, 0x7F);
					break;
				}
				integrate_from_move_vec(i, obj, speed_per_tick);
				break;
			}

			case GENUS_DEBRIS:
			case GENUS_EXPLOSION:
				integrate_from_move_vec(i, obj, speed_per_tick);
				break;

			default:
				break;
		}
	}

	if (TieFlightTiming_LegacyDue())
		move_separate_friendly_overlap();
}
