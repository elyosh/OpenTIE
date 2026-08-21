#include <landru/joy.h>

#include "host_internal.h"

/*
 * Joystick driver over the host's normalized input service.
 *
 * The original DOS driver polled gameport 0x201 capacitor timings and
 * required a 3-point calibration to derive center / range values. Modern
 * hosts deliver pre-calibrated signed axes, so the calibration entry points
 * (Center / UpperLeft / LowerRight) collapse to no-ops here and the
 * normalization happens entirely inside the platform layer.
 *
 * Public API contract preserved:
 *   - Init returns 0..2 (joystick count).
 *   - Read returns normalized X/Y (and optionally roll on the 3-axis
 *     entry point) in the -127..+127 range with a small dead zone,
 *     plus the first four buttons in the low nibble of the return
 *     value.
 *   - ngstickflag forces silent zero output (used during calibration UI
 *     to lock the stick out of the input pipeline).
 */

#define DEAD_ZONE 12

int16_t ngstickflag;

int16_t ljoy_Joystick_Init(void) {
	int n = landru_host_joystick_count();
	if (n < 0)
		n = 0;
	if (n > 2)
		n = 2;
	return (int16_t)n;
}

static int16_t apply_dead_zone(int16_t v) {
	if (v >= 0) {
		v -= DEAD_ZONE;
		if (v < 0)
			v = 0;
	} else {
		v += DEAD_ZONE;
		if (v > 0)
			v = 0;
	}
	return v;
}

int16_t ljoy_Joystick_Read_Axes(int16_t* axes, int n_axes, int16_t port) {
	if (axes)
		for (int i = 0; i < n_axes; ++i)
			axes[i] = 0;
	if (ngstickflag)
		return 0;

	uint16_t buttons = 0;
	landru_host_joystick_read(port, axes, n_axes, &buttons);

	if (axes)
		for (int i = 0; i < n_axes; ++i)
			axes[i] = apply_dead_zone(axes[i]);

	return (int16_t)(buttons & 0x0F);
}

int16_t ljoy_Joystick_Read(int16_t* outX, int16_t* outY, int16_t port) {
	int16_t axes[2] = { 0, 0 };
	int16_t buttons = ljoy_Joystick_Read_Axes(axes, 2, port);
	if (outX)
		*outX = axes[0];
	if (outY)
		*outY = axes[1];
	return buttons;
}
