#ifndef TIE_DYNAMIX_H
#define TIE_DYNAMIX_H

/*
 * dynamix -- per-frame AI flight-dynamics tick.
 *
 * Runs each frame from TIE_doframe (after PAI_updateplaneai and
 * LASER_weaponsfire). Iterates objects[0..NUM_CRAFTS-1] and:
 *   1) drives the three orientation autopilots (roll, heading, pitch)
 *      whose state / target / step fields live in CraftData;
 *   2) handles altitude recovery (climb-to-waypoint-z, dive pullout);
 *   3) slews current_speed toward a target value determined by
 *      craftptr->flight_flag (cruise / brake / coast / boost / reset);
 *   4) propagates h/p/r to the tow_slave_ref-linked buddy craft.
 *
 * Setters for the ai_*_state / ai_target_* / ai_*_step fields live in
 * PAIMAN_* (initimmelmann, cruisemaneuver, attackmaneuver, etc.). The
 * speed/rate caps come from SpecData (max_accel, decel_gain_frac,
 * roll_per_pitch_frac, spec_20/24/26_cache).
 */

#include "tie/tie.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Module-owned global: species index of the craft currently being
 * processed by planedynamics. Consumed by drawpol_setmarkingcolors
 * during the subsequent drawcraft path for the same ship. */
extern uint16_t pspecnum;

/* Per-frame driver. Called once per game tick from TIE_doframe. */
void dynamix_planedynamics(void);

/* Speed controllers. All take a FlightObject slot index. The `accel` /
 * `decel` arguments are in speed-units per second and are divided by
 * `framerate` via math2_divide (carry kept in FlightObject.speed_remainder). */
void dynamix_addvelocity(uint16_t obj_idx, uint16_t accel);
void dynamix_subvelocity(uint16_t obj_idx, uint16_t decel);

/*
 * dynamix_adjustvelocity -- slew current_speed toward target_speed.
 *
 * If delta > 0 (accelerate):
 *   base = max_accel * 0.25 (floor 1)
 *   step = base + (max_accel - base) * throttle_frac / 65536
 *   if slam_active == 0 step *= 3
 *   cap to delta, then addvelocity(step).
 *
 * If delta < 0 AND allow_decel == 1 (proportional brake):
 *   step = |delta| * decel_gain_frac / 65536 (floor 1)
 *   subvelocity(step).
 *
 * delta is tested via the unsigned wraparound idiom (>= 0x8000 = negative).
 */
void dynamix_adjustvelocity(uint16_t obj_idx, int16_t target_speed, int16_t allow_decel,
							uint16_t throttle_frac);

/*
 * dynamix_pulloutdive -- altitude-recovery autopilot.
 *
 * altitude = obj.world_z - craftptr->waypoint_z_cache.
 * If altitude <= 256  : snap orient_heading to level (0x4000),
 *                       clear ai_heading_state, mark ai_dive_state = 2 (done).
 * Else                : estimate Z descent per frame from obj.moveZ * framerate
 *                       (2x scale for genus != 0, 3x for genus == 0).
 *                       If one frame would overshoot the target, start levelling
 *                       by halving the heading offset from 0x4000 and flipping
 *                       ai_heading_state = 1 (turn left).
 */
void dynamix_pulloutdive(uint16_t obj_idx);

#endif /* TIE_DYNAMIX_H */
