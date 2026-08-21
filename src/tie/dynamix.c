#include "tie/dynamix.h"
#include "tie/fview.h"
#include "tie/math2.h"
#include "tie_runtime/audio/config.h"
#include "tie_runtime/diagnostics/diagnostics.h"
#include "tie_runtime/display/classic_display.h"
#include "tie_runtime/display/classic_framebuffer.h"
#include "tie_runtime/flight_assets/model_types.h"
#include "tie_runtime/input/input.h"
#include "tie_runtime/runtime/exports.h"
#include "tie_runtime/runtime/profile.h"
#include "tie_runtime/storage/storage.h"
#include "tie_runtime/timing/flight_timing.h"
#include "tie_runtime/timing/flight_timing_state.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Per-frame "active species" latch. Written at the top of each
 * planedynamics iteration and consumed by drawpol_setmarkingcolors
 * during the same frame's render. watdbg places it in dynamix.c. */
// GLOBAL: TIE 0xD4058
uint16_t pspecnum;

/* ======================================================================
 * dynamix_addvelocity / dynamix_subvelocity
 *
 * Integrate an accel / decel rate (units/sec) into current_speed,
 * carrying the 16-bit fractional remainder of the division between
 * frames in FlightObject.speed_remainder.
 *
 * math2_divide writes the fractional part to `math2_remainder`; we
 * add/subtract that into speed_remainder and detect carry/borrow
 * via an unsigned comparison on the pre-update value.
 * ================================================================== */

// FUNCTION: TIE 0x1FDB0
void dynamix_addvelocity(uint16_t obj_idx, uint16_t accel) {
	FlightObject* obj = &objects[obj_idx];
	if (TieFlightTiming_IsHighRate()) {
		const uint32_t delta = TieFlightTimingState_AccumulateVelocityDelta(obj_idx, accel, 1, frameticks);
		const uint32_t fixed_speed =
			((uint32_t)(uint16_t)obj->current_speed << 16) + obj->speed_remainder + delta;
		obj->current_speed = (int16_t)(fixed_speed >> 16);
		obj->speed_remainder = (uint16_t)fixed_speed;
		if ((uint16_t)obj->current_speed > 0x0E10u)
			obj->current_speed = 3600;
		return;
	}
	int16_t dv = math2_divide(accel, framerate);
	uint16_t dv_rem = (uint16_t)math2_remainder;
	uint16_t old_rem = obj->speed_remainder;

	obj->speed_remainder = (uint16_t)(old_rem + dv_rem);
	if (old_rem > obj->speed_remainder) {
		/* unsigned wrap = remainder carry */
		obj->current_speed++;
	}
	obj->current_speed += dv;
	if ((uint16_t)obj->current_speed > 0x0E10u) {
		obj->current_speed = 3600;
	}
}

// FUNCTION: TIE 0x1FE44
void dynamix_subvelocity(uint16_t obj_idx, uint16_t decel) {
	FlightObject* obj = &objects[obj_idx];
	if (TieFlightTiming_IsHighRate()) {
		const uint32_t delta = TieFlightTimingState_AccumulateVelocityDelta(obj_idx, decel, -1, frameticks);
		const uint32_t fixed_speed = ((uint32_t)(uint16_t)obj->current_speed << 16) + obj->speed_remainder;
		if (delta >= fixed_speed) {
			obj->current_speed = 0;
			obj->speed_remainder = 0;
		} else {
			const uint32_t result = fixed_speed - delta;
			obj->current_speed = (int16_t)(result >> 16);
			obj->speed_remainder = (uint16_t)result;
		}
		return;
	}
	int16_t dv = math2_divide(decel, framerate);
	uint16_t dv_rem = (uint16_t)math2_remainder;
	uint16_t old_rem = obj->speed_remainder;

	obj->speed_remainder = (uint16_t)(old_rem - dv_rem);
	if (old_rem < dv_rem) {
		/* unsigned borrow */
		obj->current_speed--;
	}
	obj->current_speed -= dv;
	if ((uint16_t)obj->current_speed > 0x8000u) {
		/* underflow: >0x8000 unsigned == negative when reinterpreted signed */
		obj->current_speed = 0;
	}
}

/* ======================================================================
 * dynamix_adjustvelocity
 *
 * delta is computed with 16-bit truncation; its sign is probed via the
 * unsigned >= 0x8000 idiom to match the binary exactly.
 * ================================================================== */

// FUNCTION: TIE 0x1FC78
void dynamix_adjustvelocity(uint16_t obj_idx, int16_t target_speed, int16_t allow_decel,
							uint16_t throttle_frac) {
	FlightObject* obj = &objects[obj_idx];
	const SpecData* sp = &spec_data[pspecnum];

	uint16_t delta = (uint16_t)(target_speed - obj->current_speed);
	if (delta == 0) {
		return;
	}

	if (delta >= 0x8000u) {
		/* current > target: brake (only if caller opted in) */
		if (allow_decel != 1) {
			return;
		}
		uint16_t overshoot = (uint16_t)(obj->current_speed - target_speed);
		uint16_t step = math2_fraction(overshoot, (uint16_t)sp->decel_gain_frac);
		if (step == 0) {
			step = 1;
		}
		dynamix_subvelocity(obj_idx, step);
	} else {
		/* current < target: accelerate */
		uint16_t base = math2_fraction((uint16_t)sp->max_accel, 0x4000u);
		if (base == 0) {
			base = 1;
		}
		uint16_t step = (uint16_t)(math2_fraction((uint16_t)(sp->max_accel - base), throttle_frac) + base);
		if (!obj->craft_ptr->slam_active) {
			step = (uint16_t)(step * 3);
		}
		if (delta >= step) {
			delta = step;
		}
		dynamix_addvelocity(obj_idx, delta);
	}
}

/* ======================================================================
 * dynamix_pulloutdive
 * ================================================================== */

// FUNCTION: TIE 0x1FED8
void dynamix_pulloutdive(uint16_t obj_idx) {
	FlightObject* obj = &objects[obj_idx];
	int32_t altitude = obj->world_z - craftptr->waypoint_z_cache;

	if (altitude <= 256) {
		/* At target altitude: level off and mark the dive as done. */
		craftptr->orient_heading = 0x4000;
		craftptr->ai_heading_state = 0;
		craftptr->ai_dive_state = 2;
		return;
	}

	/* Re-derive move basis if the cached one is stale (obj orientation
	 * changed since last FVIEW_transformcraft). */
	if (obj->move_dirty) {
		fview_calcrotatemove(obj->heading, obj->pitch, obj);
	}

	int32_t z_descent;
	if (obj->genus) {
		z_descent = 2 * -obj->moveZ * (int32_t)framerate;
	} else {
		z_descent = 3 * (int32_t)framerate * -obj->moveZ;
	}

	if (altitude <= z_descent) {
		/* One frame would overshoot: start levelling. Halve the
		 * current offset from 0x4000 so we approach the horizon
		 * smoothly instead of snapping to it. */
		uint16_t oh = craftptr->orient_heading;
		if (oh > 0x4000u) {
			craftptr->ai_heading_state = 1;
			craftptr->ai_target_heading = (uint16_t)(((oh - 0x4000u) / 2) + 0x4000u);
		}
	}
}

/* ======================================================================
 * dynamix_planedynamics
 *
 * Called once per game tick. Processes objects[0..NUM_CRAFTS-1]; the
 * player's own craft (objects[object_idx]) is exempted from the
 * orientation autopilots and the climb/dive recovery.
 * ================================================================== */

/* CraftData.status_flags bits used by the AI autopilot gates. */
#define CSF_ALIVE 0x0020u    /* gate: only alive craft run AI */
#define CSF_THROTTLE 0x0040u /* gate: reports cockpit throttle via throttle_speed */

/* CraftData.beam_state bit 0 = capture beam currently latched on this
 * craft -- when set, AI maneuvers freeze. */
#define CBS_CAPTURED 0x01u

/* Per-axis autopilot helpers factor out the shared "step toward target"
 * scaffolding used by roll / heading / pitch. They work on objects[i]
 * via the supplied pointers to keep the body of planedynamics readable. */

/* Returns the base step value used by all three axes:
 *   step = (rate_cap / framerate) * pacing * axis_scale, all fractional. */
static uint16_t ap_step(uint16_t obj_idx, unsigned int axis, int16_t rate_cap, int16_t pacing,
						uint16_t axis_scale) {
	if (TieFlightTiming_IsHighRate()) {
		TieDynamicsTimingState* state = TieFlightTimingState_Dynamics(obj_idx);
		const uint64_t pacing_factor = (uint16_t)pacing == 0xFFFFu ? 65536u : (uint16_t)pacing;
		const uint64_t axis_factor = axis_scale == 0xFFFFu ? 65536u : axis_scale;
		const uint64_t divisor = (uint64_t)236u * 65536u * 65536u;
		const uint64_t numerator = (uint64_t)(uint16_t)rate_cap * frameticks * pacing_factor * axis_factor +
								   state->autopilot_remainder[axis];
		state->autopilot_remainder[axis] = numerator % divisor;
		return (uint16_t)(numerator / divisor);
	}
	uint16_t tick_rate = math2_fraction((uint16_t)((uint16_t)rate_cap / framerate), (uint16_t)pacing);
	return math2_fraction(tick_rate, axis_scale);
}

// FUNCTION: TIE 0x1F4FC
void dynamix_planedynamics(void) {
	for (uint16_t i = 0; i < NUM_CRAFTS; i++) {
		FlightObject* obj = &objects[i];
		if (!obj->ship_idx || obj->category) {
			continue;
		}

		craftptr = obj->craft_ptr;
		if (TieFlightTiming_IsHighRate()) {
			TieDynamicsTimingState* state = TieFlightTimingState_Dynamics(i);
			if (craftptr->ai_roll_state < 1 || craftptr->ai_roll_state > 3)
				state->autopilot_remainder[0] = 0;
			if (!craftptr->ai_heading_state)
				state->autopilot_remainder[1] = 0;
			if (craftptr->flight_flag == 2 || !craftptr->ai_pitch_state)
				state->autopilot_remainder[2] = 0;
		}
		int16_t saved_heading = obj->heading;
		int16_t saved_roll = obj->roll;
		int16_t saved_pitch = obj->pitch;
		pspecnum = craftptr->species_idx;

		/* Throttle fraction driven from the cockpit throttle_speed when
		 * the "throttle-valid" bit is on, else 0 (hands-off idle). */
		uint16_t throttle_frac = 0;
		if (craftptr->status_flags & CSF_THROTTLE) {
			throttle_frac = craftptr->throttle_speed;
		}

		/* AI autopilot block: exempts the player craft and any craft
		 * that is dead (~CSF_ALIVE) or being captured (CBS_CAPTURED). */
		if (i != pstate.object_idx && (craftptr->status_flags & CSF_ALIVE) &&
			!(craftptr->beam_state & CBS_CAPTURED)) {
			/* ---- Roll autopilot ---- */
			if (craftptr->ai_roll_state >= 1 && craftptr->ai_roll_state <= 3) {
				uint16_t delta_roll = (uint16_t)(craftptr->ai_target_roll - obj->roll);
				uint16_t roll_step =
					ap_step(i, 0, craftptr->roll_rate_cache, craftptr->ai_target_c, craftptr->ai_roll_step);
				if (craftptr->ai_roll_state == 3) {
					/* "Bias" state used once ai_target_roll is known
					 * to be on the +ve half; just slew one step. */
					if (craftptr->ai_target_roll >= 0x8000u) {
						obj->roll -= roll_step;
					} else {
						obj->roll += roll_step;
					}
				} else if (delta_roll < 0x8000u) {
					/* target >= current: step up */
					if (delta_roll <= roll_step) {
						obj->roll = craftptr->ai_target_roll;
						craftptr->ai_roll_state = 4;
					} else {
						obj->roll += roll_step;
					}
				} else {
					/* target < current: step down */
					if ((uint16_t)-delta_roll <= roll_step) {
						obj->roll = craftptr->ai_target_roll;
						craftptr->ai_roll_state = 4;
					} else {
						obj->roll -= roll_step;
					}
				}
			}

			/* ---- Heading autopilot ---- */
			if (craftptr->ai_heading_state) {
				/* |delta|, with underflow-test idiom. */
				int32_t d = (int16_t)(craftptr->ai_target_heading - craftptr->orient_heading);
				if ((uint16_t)d >= 0x8000u) {
					d = -d;
				}
				uint16_t step = ap_step(i, 1, craftptr->heading_rate_cache, craftptr->ai_target_b,
										craftptr->ai_heading_step);

				if (craftptr->ai_heading_state == 1) {
					/* Turn left: decrement orient_heading. */
					if ((uint16_t)d > step || craftptr->ai_heading_force) {
						craftptr->orient_heading -= step;
						if (craftptr->orient_heading >= 0xE000u) {
							/* Crossed upper pole: mirror
							 * attitude and flip direction. */
							craftptr->orient_heading = (uint16_t)-craftptr->orient_heading;
							obj->pitch ^= (int16_t)0x8000;
							obj->roll ^= (int16_t)0x8000;
							craftptr->ai_heading_force = 0;
							craftptr->ai_heading_state = 2;
						}
					} else {
						/* Within one step: snap to target. */
						craftptr->orient_heading = craftptr->ai_target_heading;
						craftptr->ai_heading_state = 3;
					}
				} else if (craftptr->ai_heading_state == 2) {
					/* Turn right: increment. */
					if ((uint16_t)d > step || craftptr->ai_heading_force) {
						uint16_t nh = (uint16_t)(craftptr->orient_heading + step);
						craftptr->orient_heading = nh;
						if (nh >= 0x8000u) {
							craftptr->orient_heading = (uint16_t)-nh;
							obj->pitch ^= (int16_t)0x8000;
							obj->roll ^= (int16_t)0x8000;
							craftptr->ai_heading_force = 0;
							craftptr->ai_heading_state = 1;
						}
					} else {
						craftptr->orient_heading = craftptr->ai_target_heading;
						craftptr->ai_heading_state = 3;
					}
				}
			}

			/* ---- Pitch autopilot ---- */
			if (craftptr->flight_flag != 2 && craftptr->ai_pitch_state) {
				uint16_t delta_pitch = (uint16_t)(craftptr->ai_target_pitch - obj->pitch);
				if (delta_pitch) {
					uint16_t pitch_step = ap_step(i, 2, craftptr->pitch_rate_cache, craftptr->ai_target_d,
												  craftptr->ai_pitch_step);
					uint16_t step_for_bleed = pitch_step;
					bool stepped = false;

					if (delta_pitch >= 0x8000u) {
						/* target < current: pitch down */
						if ((uint16_t)-delta_pitch > pitch_step) {
							obj->pitch -= pitch_step;
							stepped = true;
						}
					} else {
						/* target > current: pitch up */
						if (delta_pitch > pitch_step) {
							obj->pitch += pitch_step;
							stepped = true;
						}
					}

					if (!stepped) {
						/* Within one step of target: snap there. */
						obj->pitch = craftptr->ai_target_pitch;
						craftptr->ai_pitch_state = 3;
						step_for_bleed = 0;
					}

					/* Pitch->roll visual coupling: only when the roll
					 * autopilot isn't actively steering. */
					uint8_t rs = craftptr->ai_roll_state;
					if (rs == 0 || rs == 4) {
						const SpecData* sp = &spec_data[pspecnum];
						uint16_t roll_bleed =
							math2_fraction(step_for_bleed, (uint16_t)sp->roll_per_pitch_frac);
						if (delta_pitch >= 0x8000u) {
							/* pitching down -> bank one way */
							obj->roll += roll_bleed;
						} else {
							obj->roll -= roll_bleed;
						}
					}
				}
			}
		}

		/* ---- Altitude recovery (non-player only) ---- */
		if (i != pstate.object_idx && craftptr->ai_climb_state == 1 && (craftptr->status_flags & CSF_ALIVE) &&
			!(craftptr->beam_state & CBS_CAPTURED)) {
			/* Climb finished once we cross the target altitude. */
			if (craftptr->waypoint_z_cache <= obj->world_z) {
				craftptr->ai_climb_state = 0;
				craftptr->orient_heading = 0x4000;
			}
		}
		if (i != pstate.object_idx && craftptr->ai_dive_state == 1 && (craftptr->status_flags & CSF_ALIVE) &&
			!(craftptr->beam_state & CBS_CAPTURED)) {
			dynamix_pulloutdive(i);
		}

		/* ---- Speed controller (dispatch on flight_flag) ---- */
		switch (craftptr->flight_flag) {
			case 0: {
				/* Cruise: track max_speed_cache scaled by power balance and
				 * throttle. Extra power (6 - (laser + beam + shield))
				 * converts into a per-power-point bonus; the TIE Advanced
				 * (ship_idx == 7) uses a larger bonus slice than other
				 * craft. Non-slamming craft get a x2 scale. */
				obj->heading = craftptr->orient_heading;
				int16_t cap = craftptr->max_speed_cache;
				int16_t margin =
					(int16_t)(6 - (craftptr->laser_power + craftptr->beam_power + craftptr->shield_power));
				uint16_t bonus_frac = (obj->ship_idx == 7) ? 4096 : 0x2000;
				uint16_t bonus_unit = math2_fraction((uint16_t)cap, bonus_frac);
				uint16_t scaled = math2_fraction((uint16_t)(margin * bonus_unit + cap), throttle_frac);
				uint16_t target = scaled;
				if (!craftptr->slam_active) {
					target = (uint16_t)(2 * scaled);
				}

				if (target < (uint16_t)obj->current_speed) {
					dynamix_subvelocity(i, 20);
				} else {
					dynamix_adjustvelocity(i, (int16_t)target, 1, throttle_frac);
				}
				break;
			}
			case 1:
			case 3:
			case 4:
			case 6:
				/* Inert flight modes (hangar, hyperspace, etc.): reset the
				 * per-axis AI state so the craft doesn't keep trying to
				 * steer while out of engine-controlled play. */
				craftptr->ai_climb_state = 0;
				craftptr->ai_dive_state = 0;
				craftptr->ai_roll_state = 0;
				craftptr->ai_heading_state = 0;
				break;
			case 2:
				/* Coast: bleed speed if still moving. */
				if (obj->current_speed) {
					dynamix_subvelocity(i, 20);
				}
				break;
			case 5:
				/* Two-stage ramp driven by the maneuver timers:
				 *   ai_plan_state != 0  → slow  (50 u/s) - first stage
				 *   maneuver_timer != 0 → medium (200 u/s) - second stage
				 *   both zero           → full  (500 u/s). */
				if (craftptr->status_flags) {
					if (craftptr->ai_plan_state) {
						dynamix_addvelocity(i, 50);
					} else if (craftptr->maneuver_timer) {
						dynamix_addvelocity(i, 200);
					} else {
						dynamix_addvelocity(i, 500);
					}
				} else {
					if (obj->current_speed)
						dynamix_subvelocity(i, 20);
					if (!obj->current_speed)
						craftptr->flight_flag = 0;
				}
				break;
			default:
				break;
		}

		/* Mirror the (possibly-updated) pitch back to the craft shadow. */
		craftptr->orient_pitch = obj->pitch;
		if (saved_heading != obj->heading || saved_pitch != obj->pitch || saved_roll != obj->roll) {
			obj->move_dirty = 1;
			obj->orient_dirty = 1;
		}

		/* Propagate orientation to the tow_slave_ref buddy (e.g. cargo box
		 * docked to a transport, or wingman formation tether). We only
		 * walk the FlightObject range of the 16-bit ref namespace. */
		uint16_t link = (uint16_t)craftptr->tow_slave_ref;
		if (link != 0xFFFFu && link < OBJ_REF_STATIC_BASE) {
			FlightObject* bud = &objects[link];
			bud->heading = obj->heading;
			bud->pitch = obj->pitch;
			bud->roll = obj->roll;
			bud->craft_ptr->orient_heading = craftptr->orient_heading;
			bud->move_dirty = 1;
			bud->orient_dirty = 1;
		}
	}
}
