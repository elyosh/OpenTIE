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
#ifdef TIE_MODERN
#include "tie_runtime/timing/flight_integration.h"
#include "tie_runtime/timing/flight_timing.h"
#include "tie_runtime/timing/flight_timing_state.h"
#endif

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* MOVE-owned globals (watdbg: D:\GAMES\XTIE\CODE\move.c)              */
/* ------------------------------------------------------------------ */

/* Homing-profile base per projectile species, indexed by
 * (ship_idx - WEAPON_SPECIES_BASE). Added to WarheadRecord.homing_tier to
 * select the maxhomingrate / maxdeccelrate entry. */
// GLOBAL: TIE95 0xC57AC
// GLOBAL: TIE98 0x4E6248
static const uint8_t homingindex[24] = {
	0, 0, 0, 0, 0, 0, 7, 0, 0, 0, 0, 0, 14, 21, 28, 14, 14, 14, 0, 0, 0, 0, 0, 0,
};

/* Pitch/heading angular slew rate for genus 6/7 missiles, indexed by
 *    idx = homingindex[ship_idx - WEAPON_SPECIES_BASE] + wh->homing_tier
 * i.e. five 7-entry tiers selected by homingindex (0, 7, 14, 21, 28). */
// GLOBAL: TIE95 0xC57C4
// GLOBAL: TIE98 0x4E6260
static const uint16_t maxhomingrate[35] = {
	0,    1024, 2048, 3072, 5120, 7168, 9216,  0,     512,   1024, 2048, 3072,
	4608, 6144, 0,    2048, 4096, 5120, 10240, 14336, 18432, 0,    32,   64,
	80,   96,   112,  128,  0,    512,  1024,  1280,  1536,  1792, 2048,
};

/* Paired deceleration / acceleration rate (speed units per second /
 * framerate) used when the missile is off-track (decel) or below
 * wh->min_speed (accel). Same indexing as maxhomingrate. */
// GLOBAL: TIE95 0xC580A
// GLOBAL: TIE98 0x4E62A8
static const uint16_t maxdeccelrate[35] = {
	0,   50,  100,  200, 300, 400, 500, 0, 25, 50, 100, 150, 200, 250, 0, 100, 200, 400,
	600, 800, 1000, 0,   0,   0,   0,   0, 0,  0,  0,   0,   0,   0,   0, 0,   0,
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
enum {
	WORLD_CLAMP_POS = 0x01000000,
	WORLD_CLAMP_NEG = (-0x01000000),
};

/* ------------------------------------------------------------------ */
/* move_updatexyz                                                      */
/* ------------------------------------------------------------------ */

// FUNCTION: TIE95 0x32E48
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
/* move_moveobjects                                                    */
/* ------------------------------------------------------------------ */

// FUNCTION: TIE95 0x32448
void move_moveobjects(void) {
	uint16_t i;

#ifdef TIE_MODERN
	TieFlightIntegration_BeginFrame();
#endif

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
	for (i = 0; i < (uint16_t)NUM_OBJECTS; i++) {
		FlightObject* obj = &objects[i];
		int16_t dt;
		int16_t spin;
		uint16_t speed_per_tick;

		if (obj->ship_idx == 0)
			continue;

		/* Death countdown. */
		dt = obj->death_timer;
		if (dt != 0) {
			int16_t new_dt = (int16_t)(dt - (int16_t)frameticks);
			if (new_dt < 0)
				new_dt = 0;
			obj->death_timer = new_dt;
			if (new_dt == 0) {
				switch (obj->genus) {
					case GENUS_FIGHTER: {
						/* Retail order: blowoff first (which itself consumes RNG),
						 * then read the random byte for the 127/128 ember pick. */
						int16_t rnd;

						create_blowoffcomponent(i, 1);
						rnd = math2_getrandom();
						collide_makeobjectexplosion(i, (char)(127 + (rnd & 1)));
						break;
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
								collide_makeobjectexplosion(i, (char)130);
								obj->damage_state = (uint8_t)(modelbounds_getmaxextent(obj->ship_idx) >> 9);
							} else {
								int16_t length;
								uint8_t lod;

								draw_lockshipfileptrs(obj->ship_idx);
								collide_makeobjectexplosion(i, (char)130);
								length = (int16_t)objectblockptr->length;
								lod = objectblockptr->model_scale_shift;
								obj->damage_state = (uint8_t)(length >> (9 - lod));
							}
						} else {
							collide_makeobjectexplosion(i, (char)130);
						}
						break;
					}
					case GENUS_PROJECTILE_PLAYER:
					case GENUS_PROJECTILE_NPC:
						/* Missile/projectile death gate: 0 = silent removal,
						 * nonzero = full explosion (always the 129 chunk variant). */
						if (projectile_is_warhead_type[obj->ship_idx - WEAPON_SPECIES_BASE] == 0) {
							obj->ship_idx = 0;
							continue;
						}
						collide_makeobjectexplosion(i, (char)129);
						break;
					case GENUS_DEBRIS: {
						/* Debris ember randomises between the 127 and 128 sprites. */
						int16_t rnd = math2_getrandom();
						collide_makeobjectexplosion(i, (char)(127 + (rnd & 1)));
						break;
					}
					default:
						obj->ship_idx = 0;
						continue;
				}
			}
		}

		/* Cutscene focus snapshot. */
		if (mission.train_craft_type && i == pstate.object_idx)
			gate_savegatelastpos();

		/* Save previous world pos. */
		obj->world_x_prev = obj->world_x;
		obj->world_y_prev = obj->world_y;
		obj->world_z_prev = obj->world_z;

		/* Death-spin roll decay. Bleeds |spin_rate| toward zero at
		 * 4096/framerate per tick; when decay crosses zero, writes 0xFFFF to
		 * craft->spin_done_flag marking the spin complete. The roll always
		 * advances by (spin/framerate)*4 using the pre-decay spin (or 0 if
		 * the decay just crossed zero). */
		spin = obj->spin_rate;
		if (spin != 0) {
			int16_t roll_delta;

#ifdef TIE_MODERN
			if (TieFlightTiming_IsHighRate()) {
				TieMoveTimingState* state = TieFlightTimingState_Move(i, obj);
				const int16_t sign = spin < 0 ? -1 : 1;
				if (state->spin_sign != sign) {
					state->spin_remainder[0] = 0;
					state->spin_remainder[1] = 0;
					state->spin_sign = sign;
				}
			}
#endif
			if (i < NUM_CRAFTS) {
				CraftData* craft = objects[i].craft_ptr;

				if (craft->spin_done_flag != 0xFFFF) {
					int16_t step;
					int16_t new_spin;
					int crossed;

#ifdef TIE_MODERN
					if (TieFlightTiming_IsHighRate()) {
						TieMoveTimingState* state = TieFlightTimingState_Move(i, obj);
						step = (int16_t)TieFlightTiming_ScaleWithRemainder(4096, frameticks, 236,
																		   &state->spin_remainder[0]);
					} else
#endif
					{
						step = (int16_t)((uint16_t)4096 / framerate);
					}

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
						spin = obj->spin_rate;
						craft->spin_done_flag = 0xFFFF;
#ifdef TIE_MODERN
						if (TieFlightTiming_IsHighRate()) {
							TieMoveTimingState* state = TieFlightTimingState_Move(i, obj);
							state->spin_remainder[0] = 0;
							state->spin_remainder[1] = 0;
							state->spin_sign = 0;
						}
#endif
					}
				}
			}

#ifdef TIE_MODERN
			if (TieFlightTiming_IsHighRate()) {
				TieMoveTimingState* state = TieFlightTimingState_Move(i, obj);
				roll_delta = (int16_t)TieFlightTiming_ScaleWithRemainder((int32_t)spin * 4, frameticks, 236,
																		 &state->spin_remainder[1]);
			} else
#endif
			{
				roll_delta = (int16_t)((spin / (int16_t)framerate) * 4);
			}
			obj->orient_dirty = 1;
			obj->roll = (int16_t)(obj->roll + roll_delta);
		}

		/* Per-tick distance per unit move vector. */
#ifdef TIE_MODERN
		if (TieFlightTiming_IsHighRate())
			speed_per_tick = 0;
		else
#endif
			speed_per_tick = obj->current_speed ? math2_mphconvert(obj->current_speed, framerate) : 0;

		/* Genus dispatch -- different integration paths. */
		switch (obj->genus) {
			case GENUS_FIGHTER:
			case GENUS_TRANSPORT:
			case GENUS_UTILITY:
			case GENUS_FREIGHTER:
			case GENUS_STARSHIP: {
				/* Manned craft: resolve the move vector, apply the three
				 * external-push accumulators, integrate, then pin a linked
				 * slave craft (tow_slave_ref) to a fixed world-frame offset. */
				CraftData* craft = obj->craft_ptr;
				uint16_t link_idx;

				if (obj->move_dirty)
					fview_calcrotatemove(obj->heading, obj->pitch, obj);

#ifdef TIE_MODERN
				if (TieFlightTiming_IsHighRate()) {
					TieFlightIntegration_Move(i, obj);
				} else
#endif
				{
					trig2_xmovedist = ((int32_t)speed_per_tick * obj->moveX) >> 15;
					trig2_ymovedist = ((int32_t)speed_per_tick * obj->moveY) >> 15;
					trig2_zmovedist = ((int32_t)speed_per_tick * obj->moveZ) >> 15;
				}

				if (craft->status_flags != 0) {
					int32_t cap;
					int32_t accum;
					int32_t clamped;
					int32_t step;

					if (craft->mode_byte == 18)
						cap = 250;
					else if (craft->mode_byte == 30)
						cap = 750;
					else
						cap = (uint16_t)spec_data[craft->species_idx].max_push_rate;

					/* Each accumulator bleeds toward 0 at min(|accum|, cap) /
					 * framerate per tick into the move distance; a step that
					 * rounds to zero dumps the whole remaining accumulator. */
					accum = craft->push_accum_x;
					if (accum != 0) {
						clamped = accum;
						if (clamped < -cap)
							clamped = -cap;
						else if (clamped > cap)
							clamped = cap;
#ifdef TIE_MODERN
						if (TieFlightTiming_IsHighRate())
							step = TieFlightIntegration_PushStep(i, 0, accum, clamped);
						else
#endif
						{
							step = (int32_t)((int16_t)clamped / (int16_t)framerate);
							if (step == 0)
								step = accum;
						}
						craft->push_accum_x = accum - step;
						trig2_xmovedist += step;
					}

					accum = craft->push_accum_y;
					if (accum != 0) {
						clamped = accum;
						if (clamped < -cap)
							clamped = -cap;
						else if (clamped > cap)
							clamped = cap;
#ifdef TIE_MODERN
						if (TieFlightTiming_IsHighRate())
							step = TieFlightIntegration_PushStep(i, 1, accum, clamped);
						else
#endif
						{
							step = (int32_t)((int16_t)clamped / (int16_t)framerate);
							if (step == 0)
								step = accum;
						}
						craft->push_accum_y = accum - step;
						trig2_ymovedist += step;
					}

					accum = craft->push_accum_z;
					if (accum != 0) {
						clamped = accum;
						if (clamped < -cap)
							clamped = -cap;
						else if (clamped > cap)
							clamped = cap;
#ifdef TIE_MODERN
						if (TieFlightTiming_IsHighRate())
							step = TieFlightIntegration_PushStep(i, 2, accum, clamped);
						else
#endif
						{
							step = (int32_t)((int16_t)clamped / (int16_t)framerate);
							if (step == 0)
								step = accum;
						}
						craft->push_accum_z = accum - step;
						trig2_zmovedist += step;
					}
				}

				move_updatexyz(obj);

				/* Linked slave craft: pin to a fixed offset behind the leader.
				 * tow_slave_ref is an obj-ref (see OBJ_REF_* in tie.h); only
				 * FlightObject slot refs are valid leaders. */
				link_idx = (uint16_t)craft->tow_slave_ref;
				if (link_idx < OBJ_REF_STATIC_BASE) {
					CraftData* tgt_craft = objects[link_idx].craft_ptr;
					int16_t ofs_z;
					FlightObject* slave;

					/* genus > GENUS_UTILITY: freighter or larger -> heavy dock offsets. */
					if (objects[link_idx].genus > GENUS_UTILITY) {
						if (objects[i].genus > GENUS_UTILITY)
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

					slave = &objects[link_idx];
					slave->world_x_prev = slave->world_x;
					slave->world_y_prev = slave->world_y;
					slave->world_z_prev = slave->world_z;
					slave->world_x = rotatedx + obj->world_x;
					slave->world_y = rotatedy + obj->world_y;
					slave->world_z = rotatedz + obj->world_z;
				}
				break;
			}

			case GENUS_PROJECTILE_PLAYER:
			case GENUS_PROJECTILE_NPC: {
				/* For projectile slots, obj->craft_ptr addresses an 8-byte
				 * WarheadRecord, not a CraftData. homing_tier == 0 is a
				 * craft-fired projectile with no homing. */
				WarheadRecord* wh = (WarheadRecord*)obj->craft_ptr;

				if (wh->homing_tier != 0 && wh->target_obj != 0xFFFF) {
					uint16_t target_idx = wh->target_obj;
					uint16_t sub_obj;
					uint32_t idx;
					int16_t pitch_delta;
					int16_t pitch_rate;
					int16_t abs_pd;
					int16_t heading_rate;
					int16_t heading_delta;
					int16_t abs_hd;
#ifdef TIE_MODERN
					TieMoveTimingState* high_rate;
#endif

					/* Target slot dead (and onworld)? Explode. */
					if (target_idx < (uint16_t)NUM_OBJECTS && !objects[target_idx].ship_idx) {
						collide_makeobjectexplosion(i, (char)129);
						break;
					}

					idx = (uint32_t)homingindex[obj->ship_idx - WEAPON_SPECIES_BASE] + wh->homing_tier;
					sub_obj = wh->sub_obj_idx;

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
								pai_calcrotatedpoint(&objects[target_idx], side >> 1, vertical >> 1,
													 (-longitudinal) >> 1);
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
						ShipModelMesh* mesh;
						int16_t ofs_side;
						int16_t ofs_up;
						int16_t ofs_fwd;
						int shift;

						draw_lockshipfileptrs(objects[target_idx].ship_idx);
						/* 0xFFFF selects the record slot immediately before the
						 * component table (retail `sub ebx, 40h`). */
						if (sub_obj == 0xFFFF)
							mesh = componentblockptr - 1;
						else
							mesh = componentblockptr + sub_obj;
						/* Unaligned dword loads in the binary:
						 *   (dword at mesh+14) >> 17 = center_side >> 1
						 *   (dword at mesh+18) >> 17 = center_up   >> 1
						 *   (dword at mesh+16) >> 17 = center_fwd  >> 1 */
						ofs_side = (int16_t)(mesh->center_side >> 1);
						ofs_up = (int16_t)(mesh->center_up >> 1);
						ofs_fwd = (int16_t)(mesh->center_fwd >> 1);
						pai_calcrotatedpoint(&objects[target_idx], ofs_side, ofs_up, (int16_t)-ofs_fwd);

						/* ShipModelData.model_scale_shift scales the rotated
						 * offsets; the binary's `shl reg, cl` is routed through
						 * uint32_t to avoid shifting a negative int32_t. */
						shift = objectblockptr->model_scale_shift;
						rotatedy = (int32_t)((uint32_t)rotatedy << shift);
						rotatedx = worldlocx + (int32_t)((uint32_t)rotatedx << shift);
						rotatedy += worldlocy;
						rotatedz = worldlocz + (int32_t)((uint32_t)rotatedz << shift);
					}

					/* Source: this missile's world position. */
					create_getworldposition(i, 0);
					rotatedx -= worldlocx;
					rotatedz -= worldlocz;
					rotatedy -= worldlocy;
					trig2_ctop(rotatedx, rotatedy, rotatedz);

					/* Pitch slew. xyangle = angle to target in X-Y (vertical) plane. */
					pitch_delta = (int16_t)(trig2_xyangle - obj->pitch);
#ifdef TIE_MODERN
					high_rate = TieFlightTiming_IsHighRate() ? TieFlightTimingState_Move(i, obj) : NULL;
					if (high_rate)
						pitch_rate = (int16_t)TieFlightTiming_ScaleWithRemainder(
							maxhomingrate[idx], frameticks, 236, &high_rate->homing_remainder[0]);
					else
#endif
						pitch_rate = (int16_t)(maxhomingrate[idx] / framerate);
					abs_pd = (pitch_delta < 0) ? (int16_t)-pitch_delta : pitch_delta;
					if (abs_pd > pitch_rate) {
						int16_t step = (pitch_delta < 0) ? (int16_t)-pitch_rate : pitch_rate;
						obj->pitch = (int16_t)(obj->pitch + step);
						/* 200 is the missile homing-decel floor: while off-track and
						 * above the floor, bleed speed; clamp back up on overshoot. */
						if ((uint16_t)obj->current_speed > 200) {
							int16_t decel;
							int16_t ns;

#ifdef TIE_MODERN
							if (high_rate && high_rate->homing_speed_sign != -1) {
								high_rate->homing_remainder[2] = 0;
								high_rate->homing_speed_sign = -1;
							}
							if (high_rate)
								decel = (int16_t)TieFlightTiming_ScaleWithRemainder(
									maxdeccelrate[idx], frameticks, 236, &high_rate->homing_remainder[2]);
							else
#endif
								decel = (int16_t)(maxdeccelrate[idx] / framerate);
							ns = (int16_t)(obj->current_speed - decel);
							obj->current_speed = ns;
							if ((uint16_t)ns < 200)
								obj->current_speed = 200;
						}
					} else {
						obj->pitch = trig2_xyangle;
#ifdef TIE_MODERN
						if (high_rate)
							high_rate->homing_remainder[0] = 0;
#endif
						if ((uint16_t)obj->current_speed < wh->min_speed) {
							int16_t accel;

#ifdef TIE_MODERN
							if (high_rate && high_rate->homing_speed_sign != 1) {
								high_rate->homing_remainder[2] = 0;
								high_rate->homing_speed_sign = 1;
							}
							if (high_rate)
								accel = (int16_t)TieFlightTiming_ScaleWithRemainder(
									maxdeccelrate[idx], frameticks, 236, &high_rate->homing_remainder[2]);
							else
#endif
								accel = (int16_t)(maxdeccelrate[idx] / framerate);
							obj->current_speed = (int16_t)(obj->current_speed + accel);
						}
					}

					/* Heading slew. zangle = angle in horizontal plane. */
#ifdef TIE_MODERN
					if (high_rate)
						heading_rate = (int16_t)TieFlightTiming_ScaleWithRemainder(
							maxhomingrate[idx], frameticks, 236, &high_rate->homing_remainder[1]);
					else
#endif
						heading_rate = (int16_t)(maxhomingrate[idx] / framerate);
					heading_delta = (int16_t)(trig2_zangle - obj->heading);
					abs_hd = (heading_delta < 0) ? (int16_t)-heading_delta : heading_delta;
					if (abs_hd > heading_rate) {
						int16_t step = (heading_delta < 0) ? (int16_t)-heading_rate : heading_rate;
						obj->heading = (int16_t)(obj->heading + step);
					} else {
						obj->heading = trig2_zangle;
#ifdef TIE_MODERN
						if (high_rate)
							high_rate->homing_remainder[1] = 0;
#endif
					}

					obj->orient_dirty = 1;
					obj->move_dirty = obj->orient_dirty;
					fview_calcrotatemove(obj->heading, obj->pitch, obj);
					obj->moveX = (int16_t)craftmoveX;
					obj->moveY = (int16_t)craftmoveY;
					obj->moveZ = (int16_t)craftmoveZ;
				}

				if (obj->move_dirty)
					fview_calcrotatemove(obj->heading, obj->pitch, obj);
#ifdef TIE_MODERN
				if (TieFlightTiming_IsHighRate()) {
					TieFlightIntegration_Move(i, obj);
				} else
#endif
				{
					trig2_xmovedist = ((int32_t)speed_per_tick * obj->moveX) >> 15;
					trig2_ymovedist = ((int32_t)speed_per_tick * obj->moveY) >> 15;
					trig2_zmovedist = ((int32_t)speed_per_tick * obj->moveZ) >> 15;
				}
				move_updatexyz(obj);
				break;
			}

			case GENUS_DEBRIS:
			case GENUS_EXPLOSION:
				if (obj->move_dirty)
					fview_calcrotatemove(obj->heading, obj->pitch, obj);
#ifdef TIE_MODERN
				if (TieFlightTiming_IsHighRate()) {
					TieFlightIntegration_Move(i, obj);
				} else
#endif
				{
					trig2_xmovedist = ((int32_t)speed_per_tick * obj->moveX) >> 15;
					trig2_ymovedist = ((int32_t)speed_per_tick * obj->moveY) >> 15;
					trig2_zmovedist = ((int32_t)speed_per_tick * obj->moveZ) >> 15;
				}
				move_updatexyz(obj);
				break;

			default:
				break;
		}
	}

#ifdef TIE_MODERN
	if (TieFlightTiming_LegacyDue())
		TieFlightIntegration_SeparateFriendly();
#endif
}
