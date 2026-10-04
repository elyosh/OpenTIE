/* Captured relative mouse motion: OpenTIE's classic adapter and OpenXW's
 * virtual stick, with OpenXW's sensitivity notches and Y inversion. */

#include "tie_runtime/input/mouse_flight.h"

#include <math.h>

#include "aeron/aeron.h"

/* Host-unit multiplier and four-PIT-tick normalization interval tuned for
 * TIE95; the stick drops motion collected across a longer gap. */
enum {
	TIE_MOUSE_MOTION_MULTIPLIER = 4,
	TIE_MOUSE_REFERENCE_INTERVAL_US = 16000,
	TIE_MOUSE_MAX_INTERVAL_US = 250000,
	TIE_MOUSE_STICK_MAX_GAP_US = 100000,
};

static const float sensitivity_scale[] = { .0625f, .125f, .25f, .5f, 1.0f, 2.0f, 4.0f, 8.0f, 16.0f };

static struct {
	TieMouseFlightOptions options;
	bool flight;
	bool stick_sampled;
	float motion_x, motion_y;
	float fraction_x, fraction_y;
	float stick_x, stick_y;
	uint64_t interval_us;
} mouse = {
	.options = { .enabled = true, .mode = TIE_MOUSE_CLASSIC, .sensitivity = 5, .invert_y = true },
};

bool TieMouseFlight_OptionsValid(const TieMouseFlightOptions* options) {
	return options && (options->mode == TIE_MOUSE_VIRTUAL_STICK || options->mode == TIE_MOUSE_CLASSIC) &&
		   options->sensitivity >= TIE_MOUSE_SENSITIVITY_MIN &&
		   options->sensitivity <= TIE_MOUSE_SENSITIVITY_MAX;
}

void TieMouseFlight_Reset(void) {
	mouse.stick_sampled = false;
	mouse.motion_x = mouse.motion_y = 0.0f;
	mouse.fraction_x = mouse.fraction_y = 0.0f;
	mouse.stick_x = mouse.stick_y = 0.0f;
	mouse.interval_us = 0;
}

void TieMouseFlight_SetOptions(const TieMouseFlightOptions* options) {
	if (!TieMouseFlight_OptionsValid(options))
		return;
	mouse.options = *options;
	TieMouseFlight_Reset();
}

const TieMouseFlightOptions* TieMouseFlight_Options(void) { return &mouse.options; }

void TieMouseFlight_SetFlightActive(bool active) {
	if (active == mouse.flight)
		return;
	mouse.flight = active;
	TieMouseFlight_Reset();
}

void TieMouseFlight_ClearFraction(void) { mouse.fraction_x = mouse.fraction_y = 0.0f; }

static bool TieMouseFlight_StickActive(void) {
	return mouse.flight && mouse.options.enabled && mouse.options.mode == TIE_MOUSE_VIRTUAL_STICK;
}

/* Flight motion follows the selected polarity; the original is unnegated. */
static float TieMouseFlight_SignY(void) { return !mouse.flight || mouse.options.invert_y ? 1.0f : -1.0f; }

void TieMouseFlight_Collect(const AeronInputSnapshot* input, int32_t delta_us) {
	if (!input)
		return;
	/* Check each host interval, then collect that frame's motion even after a reset. */
	if (!input->has_focus || delta_us <= 0 || delta_us > TIE_MOUSE_MAX_INTERVAL_US) {
		mouse.motion_x = mouse.motion_y = 0.0f;
		mouse.fraction_x = mouse.fraction_y = 0.0f;
		mouse.interval_us = 0;
		mouse.stick_sampled = false;
	} else {
		mouse.interval_us += (uint32_t)delta_us;
	}
	mouse.motion_x += input->mouse.relative_x;
	mouse.motion_y += input->mouse.relative_y;
}

static int16_t TieMouseFlight_ClampInt16(int32_t value) {
	return (int16_t)(value < INT16_MIN ? INT16_MIN : value > INT16_MAX ? INT16_MAX : value);
}

void TieMouseFlight_ReadMovement(int16_t* dx, int16_t* dy) {
	int32_t value_x = 0, value_y = 0;
	if (TieMouseFlight_StickActive()) {
		if (dx)
			*dx = 0;
		if (dy)
			*dy = 0;
		return;
	}
	if (mouse.interval_us) {
		/* Preserve the existing TIE95 four-PIT-tick mouse tuning while
		 * consuming motion gathered across an arbitrary host interval. */
		const float sensitivity = mouse.flight ? sensitivity_scale[mouse.options.sensitivity - 1] : 1.0f;
		const float scale = TIE_MOUSE_MOTION_MULTIPLIER * sensitivity *
							(float)TIE_MOUSE_REFERENCE_INTERVAL_US / (float)mouse.interval_us;
		const float scaled_x = mouse.motion_x * scale + mouse.fraction_x;
		const float scaled_y = mouse.motion_y * scale * TieMouseFlight_SignY() + mouse.fraction_y;
		value_x = (int32_t)scaled_x;
		value_y = (int32_t)scaled_y;
		mouse.fraction_x = scaled_x - (float)value_x;
		mouse.fraction_y = scaled_y - (float)value_y;
	}
	if (dx)
		*dx = TieMouseFlight_ClampInt16(value_x);
	if (dy)
		*dy = TieMouseFlight_ClampInt16(value_y);
	mouse.motion_x = mouse.motion_y = 0.0f;
	mouse.interval_us = 0;
}

static float TieMouseFlight_ClampStick(float value) { return fmaxf(-127.0f, fminf(127.0f, value)); }

static int16_t TieMouseFlight_RoundStick(float value) { return (int16_t)floorf(value + 0.5f); }

bool TieMouseFlight_ReadStick(int16_t* x, int16_t* y) {
	if (!TieMouseFlight_StickActive())
		return false;
	if (mouse.stick_sampled && mouse.interval_us <= TIE_MOUSE_STICK_MAX_GAP_US) {
		const float gain = 127.0f / 256.0f * sensitivity_scale[mouse.options.sensitivity - 1];
		mouse.stick_x = TieMouseFlight_ClampStick(mouse.stick_x + mouse.motion_x * gain);
		mouse.stick_y =
			TieMouseFlight_ClampStick(mouse.stick_y + mouse.motion_y * gain * TieMouseFlight_SignY());
	}
	mouse.stick_sampled = true;
	mouse.motion_x = mouse.motion_y = 0.0f;
	mouse.interval_us = 0;
	if (x)
		*x = TieMouseFlight_RoundStick(mouse.stick_x);
	if (y)
		*y = TieMouseFlight_RoundStick(mouse.stick_y);
	return true;
}

bool TieMouseFlight_GetHudMarker(int* yaw, int* pitch) {
	if (!TieMouseFlight_StickActive() || !mouse.stick_sampled)
		return false;
	if (yaw)
		*yaw = TieMouseFlight_RoundStick(mouse.stick_x);
	if (pitch)
		*pitch = TieMouseFlight_RoundStick(mouse.stick_y);
	return true;
}
