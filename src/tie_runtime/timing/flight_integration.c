#include "tie_runtime/timing/flight_integration.h"

#include "tie/pai.h"
#include "tie/tie.h"
#include "tie/trig2.h"
#include "tie_runtime/timing/flight_timing.h"
#include "tie_runtime/timing/flight_timing_state.h"

void TieFlightIntegration_Move(uint16_t obj_idx, FlightObject* obj) {
	TieMoveTimingState* state;
	int64_t speed;
	int64_t divisor;
	unsigned int axis;

	state = TieFlightTimingState_Move(obj_idx, obj);
	speed = ((int64_t)4660 * obj->current_speed + 128) >> 8;
	divisor = (int64_t)236 << 15;
	const int16_t axes[3] = { obj->moveX, obj->moveY, obj->moveZ };
	int32_t* distances[3] = { &trig2_xmovedist, &trig2_ymovedist, &trig2_zmovedist };
	for (axis = 0; axis < 3; ++axis) {
		const int64_t numerator = speed * frameticks * axes[axis] + state->position_remainder[axis];
		*distances[axis] = (int32_t)(numerator / divisor);
		state->position_remainder[axis] = numerator % divisor;
	}
}

/* Flight-sim frame counter. Incremented once per move_moveobjects call,
 * i.e. once per logical flight frame (one position-integration step) —
 * not per host tick. Exposed via TieFlightIntegration_Frame() so the snapshot
 * emitter can stamp it; consumers compare it across snapshots to tell
 * whether the sim actually advanced (see TieSnapshot.flight_frame). */
static uint32_t s_flight_frame = 0;

uint32_t TieFlightIntegration_Frame(void) { return s_flight_frame; }

/* Deterministic integer sqrt (Newton). Avoids float in the sim path. */
static int32_t move_isqrt(int64_t n) {
	int64_t x, y;

	if (n <= 0)
		return 0;
	x = n;
	y = (x + 1) / 2;
	while (y < x) {
		x = y;
		y = (x + n / x) / 2;
	}
	return (int32_t)x;
}

/* OPTIONAL enhancement (non-faithful), default 1 = on. Set to 0 for
 * byte-faithful behaviour (the original has no fighter-vs-fighter
 * separation). */
static int8_t pai_friendly_separation = 1;

/* OPTIONAL (pai_friendly_separation, default on): gently push apart same-FG
 * AI craft whose hulls overlap, so wingmen ganging up on one target don't
 * interpenetrate. Position-only -- heading/velocity are untouched, so guns
 * stay on target. The player is never moved. Runs after integration so it
 * works on final positions; both world_* and world_*_prev are shifted by the
 * same nudge, so no spurious velocity is introduced. Diverges from the binary
 * by design (the original has no fighter-vs-fighter separation). */
void TieFlightIntegration_SeparateFriendly(void) {
	uint16_t a;

	if (!pai_friendly_separation)
		return;

	for (a = 0; a < NUM_CRAFTS; ++a) {
		uint8_t ga = objects[a].genus;
		uint16_t b;

		if (!objects[a].ship_idx || objects[a].category)
			continue;
		if (ga != GENUS_FIGHTER && ga != GENUS_TRANSPORT && ga != GENUS_UTILITY)
			continue;
		if (a == (uint16_t)pstate.object_idx)
			continue;

		for (b = (uint16_t)(a + 1); b < NUM_CRAFTS; ++b) {
			uint8_t gb = objects[b].genus;
			int64_t dx;
			int64_t dy;
			int64_t dz;
			int64_t d2;
			int32_t min_sep;
			int32_t d;
			int32_t push;
			int32_t nx;
			int32_t ny;
			int32_t nz;

			if (!objects[b].ship_idx || objects[b].category)
				continue;
			if (gb != GENUS_FIGHTER && gb != GENUS_TRANSPORT && gb != GENUS_UTILITY)
				continue;
			if (b == (uint16_t)pstate.object_idx)
				continue;
			if (objects[a].fg_idx != objects[b].fg_idx)
				continue;

			dx = (int64_t)objects[a].world_x - objects[b].world_x;
			dy = (int64_t)objects[a].world_y - objects[b].world_y;
			dz = (int64_t)objects[a].world_z - objects[b].world_z;
			d2 = dx * dx + dy * dy + dz * dz;

			/* Minimum centre spacing = sum of hull half-extents
			 * (collision_radius/4 each), i.e. just touching. */
			min_sep = (objects[a].collision_radius + objects[b].collision_radius) / 4;
			if (min_sep <= 0 || d2 >= (int64_t)min_sep * min_sep)
				continue;

			d = move_isqrt(d2);
			if (d == 0) {
				/* Exactly coincident: shove along +X so they part. */
				dx = 1;
				dy = 0;
				dz = 0;
				d = 1;
			}

			/* Resolve half the overlap per craft, clamped per frame so the
			 * correction is gradual rather than a teleport. */
			push = (min_sep - d) / 2;
			if (push > 256)
				push = 256;

			nx = (int32_t)(dx * push / d);
			ny = (int32_t)(dy * push / d);
			nz = (int32_t)(dz * push / d);

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

void TieFlightIntegration_BeginFrame(void) { ++s_flight_frame; }

int32_t TieFlightIntegration_PushStep(uint16_t obj_idx, unsigned int axis, int32_t accum, int32_t clamped) {
	TieMoveTimingState* state = TieFlightTimingState_Move(obj_idx, &objects[obj_idx]);
	const int8_t sign = clamped < 0 ? -1 : 1;
	int32_t step;

	if (state->push_sign[axis] != sign) {
		state->push_remainder[axis] = 0;
		state->push_sign[axis] = sign;
	}
	step = TieFlightTiming_ScaleWithRemainder(clamped, frameticks, 236, &state->push_remainder[axis]);
	if ((accum > 0 && step > accum) || (accum < 0 && step < accum))
		step = accum;
	if (accum - step == 0) {
		state->push_remainder[axis] = 0;
		state->push_sign[axis] = 0;
	}
	return step;
}

uint16_t TieFlightIntegration_AutopilotStep(uint16_t obj_idx, unsigned int axis, int16_t rate_cap,
											int16_t pacing, uint16_t axis_scale) {
	TieDynamicsTimingState* state = TieFlightTimingState_Dynamics(obj_idx);
	const uint64_t pacing_factor = (uint16_t)pacing == 0xFFFFu ? 65536u : (uint16_t)pacing;
	const uint64_t axis_factor = axis_scale == 0xFFFFu ? 65536u : axis_scale;
	const uint64_t divisor = (uint64_t)236u * 65536u * 65536u;
	const uint64_t numerator = (uint64_t)(uint16_t)rate_cap * frameticks * pacing_factor * axis_factor +
							   state->autopilot_remainder[axis];
	state->autopilot_remainder[axis] = numerator % divisor;
	return (uint16_t)(numerator / divisor);
}
