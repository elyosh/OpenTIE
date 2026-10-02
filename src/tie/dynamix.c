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
#ifdef TIE_MODERN
#include "tie_runtime/timing/flight_integration.h"
#include "tie_runtime/timing/flight_timing.h"
#include "tie_runtime/timing/flight_timing_state.h"
#endif

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Per-frame "active species" latch. Written at the top of each
 * planedynamics iteration and consumed by drawpol_setmarkingcolors
 * during the same frame's render. watdbg places it in dynamix.c. */
// GLOBAL: TIE95 0xD4058
uint16_t pspecnum;

/* ======================================================================
 * dynamix_planedynamics
 *
 * Called once per game tick. Processes objects[0..NUM_CRAFTS-1]; the
 * player's own craft (objects[object_idx]) is exempted from the
 * orientation autopilots and the climb/dive recovery.
 * ================================================================== */

/* CraftData.status_flags bits used by the AI autopilot gates. */
enum {
	CSF_ALIVE = 0x0020u,    /* gate: only alive craft run AI */
	CSF_THROTTLE = 0x0040u, /* gate: reports cockpit throttle via throttle_speed */
};
/* CraftData.beam_state bit 0 = capture beam currently latched on this
 * craft -- when set, AI maneuvers freeze. */
enum {
	CBS_CAPTURED = 0x01u,
};
/* Per-axis autopilot helpers factor out the shared "step toward target"
 * scaffolding used by roll / pitch / heading. They work on objects[i]
 * via the supplied pointers to keep the body of planedynamics readable. */

// FUNCTION: TIE95 0x1F4FC
void dynamix_planedynamics(void) {
	uint16_t i;

	for (i = 0; i < NUM_CRAFTS; i++) {
		int16_t saved_pitch;
		int16_t saved_roll;
		int16_t saved_heading;
		uint16_t throttle_frac;
		uint16_t link;

		if (!objects[i].ship_idx || objects[i].category) {
			continue;
		}

		craftptr = objects[i].craft_ptr;
#ifdef TIE_MODERN
		if (TieFlightTiming_IsHighRate()) {
			TieDynamicsTimingState* state = TieFlightTimingState_Dynamics(i);
			if (craftptr->ai_roll_state < 1 || craftptr->ai_roll_state > 3)
				state->autopilot_remainder[0] = 0;
			if (!craftptr->ai_pitch_state)
				state->autopilot_remainder[1] = 0;
			if (craftptr->flight_flag == 2 || !craftptr->ai_heading_state)
				state->autopilot_remainder[2] = 0;
		}
#endif
		saved_pitch = objects[i].pitch;
		saved_heading = objects[i].heading;
		saved_roll = objects[i].roll;
		pspecnum = craftptr->species_idx;

		/* Throttle fraction driven from the cockpit throttle_speed when
		 * the "throttle-valid" bit is on, else 0 (hands-off idle). */
		throttle_frac = 0;
		if (craftptr->status_flags & CSF_THROTTLE) {
			throttle_frac = craftptr->throttle_speed;
		}

		/* AI autopilot block: exempts the player craft and any craft
		 * that is dead (~CSF_ALIVE) or being captured (CBS_CAPTURED). */
		if (i != pstate.object_idx && (craftptr->status_flags & CSF_ALIVE) &&
			!(craftptr->beam_state & CBS_CAPTURED)) {
			/* ---- Roll autopilot ---- */
			if (craftptr->ai_roll_state >= 1 && craftptr->ai_roll_state <= 3) {
				uint16_t delta_roll = (uint16_t)(craftptr->ai_target_roll - objects[i].roll);
				uint16_t roll_step;

#ifdef TIE_MODERN
				if (TieFlightTiming_IsHighRate())
					roll_step = TieFlightIntegration_AutopilotStep(
						i, 0, craftptr->roll_rate_cache, craftptr->ai_target_c, craftptr->ai_roll_step);
				else
#endif
				{
					roll_step = math2_fraction((uint16_t)((uint16_t)craftptr->roll_rate_cache / framerate),
											   (uint16_t)craftptr->ai_target_c);
					roll_step = math2_fraction(roll_step, craftptr->ai_roll_step);
				}
				if (craftptr->ai_roll_state != 3) {
					if (delta_roll < 0x8000) {
						/* target >= current: step up */
						if (delta_roll <= roll_step) {
							objects[i].roll = craftptr->ai_target_roll;
							craftptr->ai_roll_state = 4;
						} else {
							objects[i].roll += roll_step;
						}
					} else {
						/* target < current: step down */
						if ((uint16_t)-delta_roll <= roll_step) {
							objects[i].roll = craftptr->ai_target_roll;
							craftptr->ai_roll_state = 4;
						} else {
							objects[i].roll -= roll_step;
						}
					}
				} else {
					/* "Bias" state used once ai_target_roll is known
					 * to be on the +ve half; just slew one step. */
					if (craftptr->ai_target_roll < 0x8000) {
						objects[i].roll += roll_step;
					} else {
						objects[i].roll -= roll_step;
					}
				}
			}

			/* ---- Pitch autopilot ---- */
			if (craftptr->ai_pitch_state > 0) {
				/* |delta|, with underflow-test idiom. */
				uint16_t d = (uint16_t)(craftptr->ai_target_pitch - craftptr->orient_pitch);
				uint16_t step;

				if (d >= 0x8000) {
					d = -d;
				}

#ifdef TIE_MODERN
				if (TieFlightTiming_IsHighRate())
					step = TieFlightIntegration_AutopilotStep(i, 1, craftptr->pitch_rate_cache,
															  craftptr->ai_target_b, craftptr->ai_pitch_step);
				else
#endif
				{
					step = math2_fraction((uint16_t)((uint16_t)craftptr->pitch_rate_cache / framerate),
										  (uint16_t)craftptr->ai_target_b);
					step = math2_fraction(step, craftptr->ai_pitch_step);
				}

				if (craftptr->ai_pitch_state == 1) {
					/* Nose up: decrement orient_pitch toward 0 (straight up). */
					if (d <= step && !craftptr->ai_pitch_force) {
						/* Within one step: snap to target. */
						craftptr->orient_pitch = craftptr->ai_target_pitch;
						craftptr->ai_pitch_state = 3;
					} else {
						uint16_t np = (uint16_t)(craftptr->orient_pitch - step);
						craftptr->orient_pitch = np;
						if (np >= 0xE000) {
							/* Crossed upper pole: mirror
							 * attitude and flip direction. */
							craftptr->orient_pitch = (uint16_t)-np;
							objects[i].heading -= 0x8000;
							objects[i].roll -= 0x8000;
							craftptr->ai_pitch_force = 0;
							craftptr->ai_pitch_state = 2;
						}
					}
				} else if (craftptr->ai_pitch_state == 2) {
					/* Nose down: increment toward 0x8000 (straight down). */
					if (d <= step && !craftptr->ai_pitch_force) {
						craftptr->orient_pitch = craftptr->ai_target_pitch;
						craftptr->ai_pitch_state = 3;
					} else {
						uint16_t np = (uint16_t)(craftptr->orient_pitch + step);
						craftptr->orient_pitch = np;
						if (np >= 0x8000) {
							craftptr->orient_pitch = (uint16_t)-np;
							objects[i].heading -= 0x8000;
							objects[i].roll -= 0x8000;
							craftptr->ai_pitch_force = 0;
							craftptr->ai_pitch_state = 1;
						}
					}
				}
			}

			/* ---- Heading autopilot ---- */
			if (craftptr->flight_flag != 2 && craftptr->ai_heading_state >= 1) {
				uint16_t delta_heading = (uint16_t)(craftptr->ai_target_heading - objects[i].heading);
				if (delta_heading) {
					uint16_t heading_step;
					uint8_t rs;

#ifdef TIE_MODERN
					if (TieFlightTiming_IsHighRate())
						heading_step = TieFlightIntegration_AutopilotStep(i, 2, craftptr->heading_rate_cache,
																		  craftptr->ai_target_d,
																		  craftptr->ai_heading_step);
					else
#endif
					{
						heading_step =
							math2_fraction((uint16_t)((uint16_t)craftptr->heading_rate_cache / framerate),
										   (uint16_t)craftptr->ai_target_d);
						heading_step = math2_fraction(heading_step, craftptr->ai_heading_step);
					}

					if (delta_heading < 0x8000) {
						/* target > current: increment heading */
						if (delta_heading > heading_step) {
							objects[i].heading += heading_step;
						} else {
							/* Within one step of target: snap there. */
							objects[i].heading = craftptr->ai_target_heading;
							craftptr->ai_heading_state = 3;
							heading_step = 0;
						}
					} else {
						/* target < current: decrement heading */
						if ((uint16_t)-delta_heading > heading_step) {
							objects[i].heading -= heading_step;
						} else {
							objects[i].heading = craftptr->ai_target_heading;
							craftptr->ai_heading_state = 3;
							heading_step = 0;
						}
					}

					/* Heading->roll visual coupling (bank into the turn):
					 * only when the roll autopilot isn't actively steering;
					 * a snapped heading contributes no bank. */
					rs = craftptr->ai_roll_state;
					if (rs == 0 || rs == 4) {
						uint16_t roll_bleed =
							math2_fraction(heading_step, spec_data[pspecnum].roll_per_heading_frac);
						if (delta_heading < 0x8000) {
							objects[i].roll -= roll_bleed;
						} else {
							/* decreasing heading -> bank one way */
							objects[i].roll += roll_bleed;
						}
					}
				}
			}
		}

		/* ---- Altitude recovery (non-player only) ---- */
		if (i != pstate.object_idx && craftptr->ai_climb_state == 1 && (craftptr->status_flags & CSF_ALIVE) &&
			!(craftptr->beam_state & CBS_CAPTURED)) {
			/* Climb finished once we cross the target altitude. */
			if (craftptr->waypoint_z_cache <= objects[i].world_z) {
				craftptr->ai_climb_state = 0;
				craftptr->orient_pitch = 0x4000;
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
				 * throttle. Extra power (6 - (beam + shield + laser))
				 * converts into a per-power-point bonus; the TIE Advanced
				 * (ship_idx == 7) uses a smaller bonus slice than other
				 * craft. Non-slamming craft get a x2 scale. */
				int16_t cap;
				int margin;
				uint16_t bonus;
				uint16_t target;

				objects[i].pitch = craftptr->orient_pitch;
				cap = craftptr->max_speed_cache;
				margin = 6 - craftptr->beam_power - craftptr->shield_power - craftptr->laser_power;
				if (objects[i].ship_idx == 7) {
					bonus = math2_fraction(cap, 0x1000);
				} else {
					bonus = math2_fraction(cap, 0x2000);
				}
				cap += margin * bonus;
				target = math2_fraction(cap, throttle_frac);
				if (!craftptr->slam_active) {
					target *= 2;
				}

				if (target < (uint16_t)objects[i].current_speed) {
					dynamix_subvelocity(i, 20);
				} else {
					dynamix_adjustvelocity(i, (int16_t)target, 1, throttle_frac);
				}
				break;
			}
			case 2:
				/* Coast: bleed speed if still moving. */
				if ((uint16_t)objects[i].current_speed > 0) {
					dynamix_subvelocity(i, 20);
				}
				break;
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
				craftptr->ai_pitch_state = 0;
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
					if ((uint16_t)objects[i].current_speed > 0)
						dynamix_subvelocity(i, 20);
					if (!objects[i].current_speed)
						craftptr->flight_flag = 0;
				}
				break;
			default:
				break;
		}

		/* Mirror the (possibly-updated) heading back to the craft shadow. */
		craftptr->orient_heading = objects[i].heading;
		if (saved_pitch != objects[i].pitch || saved_heading != objects[i].heading ||
			saved_roll != objects[i].roll) {
			objects[i].move_dirty = 1;
			objects[i].orient_dirty = 1;
		}

		/* Propagate orientation to the tow_slave_ref buddy (e.g. cargo box
		 * docked to a transport, or wingman formation tether). We only
		 * walk the FlightObject range of the 16-bit ref namespace. */
		link = (uint16_t)craftptr->tow_slave_ref;
		if (link != 0xFFFF && link < 0x3800 /* OBJ_REF_STATIC_BASE */) {
			objects[link].pitch = objects[i].pitch;
			objects[link].heading = objects[i].heading;
			objects[link].roll = objects[i].roll;
			objects[link].craft_ptr->orient_pitch = craftptr->orient_pitch;
			objects[link].move_dirty = 1;
			objects[link].orient_dirty = 1;
		}
	}
}

/* ======================================================================
 * dynamix_adjustvelocity
 *
 * delta is computed with 16-bit truncation; its sign is probed via the
 * unsigned >= 0x8000 idiom to match the binary exactly.
 * ================================================================== */

// FUNCTION: TIE95 0x1FC78
void dynamix_adjustvelocity(uint16_t obj_idx, uint16_t speed, uint16_t allow_decel, uint16_t throttle_frac) {
	uint16_t step;

	/* speed becomes the 16-bit delta from the current speed */
	speed -= objects[obj_idx].current_speed;
	if (speed == 0) {
		return;
	}

	if (speed < 0x8000) {
		/* current < target: accelerate */
		step = math2_fraction((uint16_t)spec_data[pspecnum].max_accel, 0x4000u);
		if (step == 0) {
			step = 1;
		}
		step += math2_fraction((uint16_t)(spec_data[pspecnum].max_accel - step), throttle_frac);
		if (!objects[obj_idx].craft_ptr->slam_active) {
			step *= 3;
		}
		if (speed >= step) {
			speed = step;
		}
		dynamix_addvelocity(obj_idx, speed);
	} else if (allow_decel == 1) {
		/* current > target: brake (only if caller opted in) */
		speed = math2_fraction((uint16_t)-speed, (uint16_t)spec_data[pspecnum].decel_gain_frac);
		if (speed == 0) {
			speed = 1;
		}
		dynamix_subvelocity(obj_idx, speed);
	}
}

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

// FUNCTION: TIE95 0x1FDB0
void dynamix_addvelocity(uint16_t obj_idx, uint16_t accel) {
	int16_t dv;
	uint16_t old_rem;

#ifdef TIE_MODERN
	if (TieFlightTiming_IsHighRate()) {
		FlightObject* obj = &objects[obj_idx];
		const uint32_t delta = TieFlightTimingState_AccumulateVelocityDelta(obj_idx, accel, 1, frameticks);
		const uint32_t fixed_speed =
			((uint32_t)(uint16_t)obj->current_speed << 16) + obj->speed_remainder + delta;
		obj->current_speed = (int16_t)(fixed_speed >> 16);
		obj->speed_remainder = (uint16_t)fixed_speed;
		if ((uint16_t)obj->current_speed > 0x0E10u)
			obj->current_speed = 3600;
		return;
	}
#endif
	dv = math2_divide(accel, framerate);
	old_rem = objects[obj_idx].speed_remainder;

	objects[obj_idx].speed_remainder += (uint16_t)math2_remainder;
	if (old_rem > objects[obj_idx].speed_remainder) {
		/* unsigned wrap = remainder carry */
		objects[obj_idx].current_speed++;
	}
	objects[obj_idx].current_speed += dv;
	if ((uint16_t)objects[obj_idx].current_speed > 3600) {
		objects[obj_idx].current_speed = 3600;
	}
}

// FUNCTION: TIE95 0x1FE44
void dynamix_subvelocity(uint16_t obj_idx, uint16_t decel) {
	FlightObject* obj = &objects[obj_idx];
	int16_t dv;
	uint16_t dv_rem;
	uint16_t old_rem;

#ifdef TIE_MODERN
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
#endif
	dv = math2_divide(decel, framerate);
	old_rem = objects[obj_idx].speed_remainder;

	objects[obj_idx].speed_remainder -= (uint16_t)math2_remainder;
	if (old_rem < objects[obj_idx].speed_remainder) {
		/* unsigned borrow */
		objects[obj_idx].current_speed--;
	}
	objects[obj_idx].current_speed -= dv;
	if ((uint16_t)objects[obj_idx].current_speed > 0x8000) {
		/* underflow: >0x8000 unsigned == negative when reinterpreted signed */
		objects[obj_idx].current_speed = 0;
	}
}

/* ======================================================================
 * dynamix_pulloutdive
 * ================================================================== */

// FUNCTION: TIE95 0x1FED8
void dynamix_pulloutdive(uint16_t obj_idx) {
	FlightObject* obj = &objects[obj_idx];
	int32_t altitude = obj->world_z - craftptr->waypoint_z_cache;

	int32_t z_descent;

	if (altitude < 0 || altitude <= 256) {
		/* At target altitude: level off and mark the dive as done. */
		craftptr->orient_pitch = 0x4000;
		craftptr->ai_pitch_state = 0;
		craftptr->ai_dive_state = 2;
		return;
	}

	/* Re-derive move basis if the cached one is stale (obj orientation
	 * changed since last FVIEW_transformcraft). */
	if (obj->move_dirty) {
		fview_calcrotatemove(obj->pitch, obj->heading, obj);
	}

	if (obj->genus == 0) {
		z_descent = -obj->moveZ * framerate * 3;
	} else {
		z_descent = -obj->moveZ * framerate * 2;
	}

	if (altitude <= z_descent) {
		/* One frame would overshoot: start levelling. Halve the
		 * current offset from 0x4000 so we approach the horizon
		 * smoothly instead of snapping to it. */
		int32_t pitch = craftptr->orient_pitch;
		if (pitch > 0x4000) {
			craftptr->ai_pitch_state = 1;
			craftptr->ai_target_pitch = ((pitch - 0x4000) >> 1) + 0x4000;
		}
	}
}
