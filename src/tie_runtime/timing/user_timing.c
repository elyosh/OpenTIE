#include "tie_runtime/timing/user_timing.h"

#include "tie/tie.h"
#include "tie_runtime/timing/flight_timing.h"
#include "tie_runtime/timing/flight_timing_state.h"

int16_t TieUserTiming_ScaleValue(int32_t value, int32_t* remainder) {
	const int64_t numerator = (int64_t)value * frameticks + *remainder;
	const int16_t scaled = (int16_t)(numerator / 236);
	*remainder = (int32_t)(numerator % 236);
	return scaled;
}

int32_t TieUserTiming_ScaleCompatibilityIncrement(int32_t value, uint16_t* remainder, int8_t* previous_sign) {
	const int8_t sign = value < 0 ? -1 : value > 0 ? 1 : 0;
	if (!sign) {
		*remainder = 0;
		*previous_sign = 0;
		return 0;
	}
	if (*previous_sign != sign) {
		*remainder = 0;
		*previous_sign = sign;
	}
	const uint32_t magnitude = (uint32_t)(value < 0 ? -value : value);
	const uint32_t numerator = magnitude * frameticks + *remainder;
	const uint16_t compatibility_ticks = TieFlightTiming_CompatibilityTicks();
	*remainder = (uint16_t)(numerator % compatibility_ticks);
	const int32_t scaled = (int32_t)(numerator / compatibility_ticks);
	return sign < 0 ? -scaled : scaled;
}

int16_t TieUserTiming_SlewAxis(int16_t current, int16_t target, unsigned int axis) {
	if (current == target) {
		if (TieFlightTiming_IsHighRate()) {
			TieUserTimingState* state = TieFlightTimingState_User();
			state->slew_remainder[axis] = 0;
			state->slew_sign[axis] = 0;
		}
		return current;
	}
	const int16_t delta = (int16_t)(target - current);
	if (!TieFlightTiming_IsHighRate()) {
		const int16_t abs_delta = (int16_t)(delta < 0 ? -delta : delta);
		if (abs_delta < 8)
			return target;
		int32_t step = abs_delta;
		if (framerate > 4) {
			int32_t per_frame = abs_delta / framerate;
			if (!per_frame)
				per_frame = 1;
			step = 4 * per_frame;
		}
		return (int16_t)(current + (delta < 0 ? -step : step));
	}

	TieUserTimingState* state = TieFlightTimingState_User();
	const int32_t abs_delta = delta < 0 ? -(int32_t)delta : delta;
	if (abs_delta < 8) {
		state->slew_remainder[axis] = 0;
		state->slew_sign[axis] = 0;
		return target;
	}
	const uint16_t compatibility_ticks = TieFlightTiming_CompatibilityTicks();
	const uint16_t compatibility_rate = (uint16_t)(236u / compatibility_ticks);
	int32_t compatibility_step = abs_delta;
	if (compatibility_rate > 4) {
		int32_t per_frame = abs_delta / compatibility_rate;
		if (!per_frame)
			per_frame = 1;
		compatibility_step = 4 * per_frame;
	}
	int32_t step =
		TieUserTiming_ScaleCompatibilityIncrement(delta < 0 ? -compatibility_step : compatibility_step,
												  &state->slew_remainder[axis], &state->slew_sign[axis]);
	if ((step > 0 && step >= abs_delta) || (step < 0 && -step >= abs_delta)) {
		state->slew_remainder[axis] = 0;
		return target;
	}
	return (int16_t)(current + step);
}
