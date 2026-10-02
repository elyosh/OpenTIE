/*
 * MATH2.C — Fixed-point arithmetic utilities
 *
 * Provides division, fractional multiply, percentage, random number
 * generation, and radar coordinate computation for the 3D engine.
 * All functions are leaf (no complex dependencies) except getradarcoord
 * which uses TRIG2.
 */

#include "tie/math2.h"
#include "tie/math2_wide.h"
#include "tie/panel.h" /* radarx / radary */
#include "tie/tie.h"
#include "tie/trig2.h"
#include "tie/user.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/* Globals */
// GLOBAL: TIE95 0xD4C6A
int16_t math2_remainder;
/* Retail initial value = 0x2357 (statically initialized at 0xC5710 in
 * Z_TIE__.EXE). Zero is a fixed point of this LFSR, so BSS-default 0
 * would make every call return 0 until CREATE_loadmission overwrote it. */
// GLOBAL: TIE95 0xC5710
// GLOBAL: TIE98 0x4E4C98
int16_t math2_randomseed = 0x2357;
// GLOBAL: TIE95 0xD4C68
// GLOBAL: TIE98 0x584CF8
static int16_t randomnumber;
#if defined(TIE_MODERN) || defined(TIE98)
/* TIE98 carries a second, independent LFSR state pair next to the gameplay
 * one; only TIE_simulator's CD start-track pick consumes it. */
// GLOBAL: TIE98 0x4E4C9C
static int16_t math2_altrandomseed = 0x2357;
// GLOBAL: TIE98 0x584CF4
static int16_t altrandomnumber;
#endif

/*
 * Radar boundary tables. The radar display is an ellipse (320×200) or
 * circle (640×480). Each table stores 37 byte pairs (x_max, y_max)
 * for angle steps 0..90° in increments of 443/16384.
 *
 * radarmax320: pre-baked for 320×200 (radii 21×18, non-square pixels)
 * radarmax:    runtime-computed for 640×480 (radius 44, square pixels)
 *              or copied from radarmax320 for 320×200 (resolution==19)
 */
// GLOBAL: TIE95 0xC5712
const uint8_t radarmax320[74] = {
	0,  18, 1,  18, 2,  18, 3,  18, 4,  17, 5,  17, 6,  17, 7,  17, 8,  16, 9,  16, 10, 16, 10, 15, 11,
	15, 12, 15, 12, 14, 13, 14, 14, 14, 14, 13, 15, 13, 15, 12, 16, 12, 16, 11, 17, 11, 17, 10, 18, 10,
	18, 9,  18, 8,  19, 8,  19, 7,  19, 6,  20, 6,  20, 5,  21, 4,  21, 3,  21, 2,  21, 1,  21, 0,
};
// GLOBAL: TIE95 0xC575C
static uint8_t radarmax[74] = {
	0,  18, 1,  18, 2,  18, 3,  18, 4,  17, 5,  17, 6,  17, 7,  17, 8,  16, 9,  16, 10, 16, 10, 15, 11,
	15, 12, 15, 12, 14, 13, 14, 14, 14, 14, 13, 15, 13, 15, 12, 16, 12, 16, 11, 17, 11, 17, 10, 18, 10,
	18, 9,  18, 8,  19, 8,  19, 7,  19, 6,  20, 6,  20, 5,  21, 4,  21, 3,  21, 2,  21, 1,  21, 0,
};
/* Resolution radarmax[] was last built for; the initial table is the
 * 320x200 one. */
// GLOBAL: TIE95 0xC57A8
static int32_t cached_radar_resolution = TIE_FLIGHT_RES_VGA;
/* Retail stores the projected radar coordinate in shared globals
 * (radarx / radary, declared in panel.h) that PANEL_addbliptoradar reads
 * immediately after the call. Bind the math2 outputs to those symbols
 * so the value actually propagates to the consumer instead of
 * dead-ending in an unused math2-local copy. */

/* ------------------------------------------------------------------ */

// FUNCTION: TIE95 0x31EE0
int32_t math2_ABoverC32(int32_t a, int32_t b, int32_t c) {
	uint32_t ua = (uint32_t)a;
	uint32_t ub = (uint32_t)b;
	uint32_t uc = (uint32_t)c;
	uint32_t result;
	int sign = 0;
	if (a < 0) {
		ua = 0u - ua;
		sign = 1;
	}
	if (b < 0) {
		ub = 0u - ub;
		sign = !sign;
	}
	if (c < 0) {
		uc = 0u - uc;
		sign = !sign;
	}
	result = math2_mul_div_u32(ua, ub, uc);
	return (int32_t)(sign ? 0u - result : result);
}

// FUNCTION: TIE95 0x31F40
uint16_t math2_fraction(uint16_t val, uint16_t frac) {
	if (frac == 0xFFFF)
		return val;
	return (uint16_t)(((uint32_t)frac * val + 0x8000) >> 16);
}

// FUNCTION: TIE95 0x31F68
int32_t math2_longfraction(int32_t val, uint16_t frac) {
	uint16_t lo;
	uint16_t hi;

	if (frac == 0xFFFF)
		return val;
	lo = (uint16_t)val;
	hi = (uint16_t)(val >> 16);
	/* Retail rounds half-up via the +0x8000 bias before the >>16. */
	return (int32_t)((uint32_t)frac * hi + (((uint32_t)frac * lo + 0x8000u) >> 16));
}

// FUNCTION: TIE95 0x31FA0
int16_t math2_divide(uint16_t a, uint16_t b) {
	math2_remainder = (int16_t)((((uint32_t)(a % b)) << 16) / b);
	return (int16_t)(a / b);
}

// FUNCTION: TIE95 0x31FE4
uint16_t math2_percentage(uint16_t a, uint16_t b) {
	if (a != b) {
		if (!b)
			return 0;
		if (a < b)
			return (uint16_t)(((uint32_t)a << 16) / b);
	}
	return 0xFFFF;
}

// FUNCTION: TIE95 0x32014
uint16_t math2_longpercentage(uint32_t a, uint32_t b) {
	if (a == b || !b || a >= b)
		return 0xFFFF;
	while (a > 0xFFFF || b > 0xFFFF) {
		a >>= 1;
		b >>= 1;
	}
	return (uint16_t)((a << 16) / b);
}

#if defined(TIE_MODERN) || defined(TIE98)
/* Same 16-bit LFSR as math2_getrandom on its own state, without the replay
 * return-address synchronisation, so it never perturbs the gameplay stream. */
// FUNCTION: TIE98 0x454070
int16_t math2_getrandomalt(void) {
	int i;

	for (i = 16; i != 0; i--) {
		int seed_sign = (math2_altrandomseed & 0x8000) != 0;
		int carry_out = (((math2_altrandomseed >> 8) & 0x80) ^ ((math2_altrandomseed & 0x40) << 1)) != 0;
		math2_altrandomseed = (int16_t)((uint16_t)math2_altrandomseed * 2 + carry_out);
		altrandomnumber = (int16_t)((uint16_t)altrandomnumber * 2 + seed_sign);
	}
	return altrandomnumber;
}
#endif

#ifdef __WATCOMC__
/* Return address of the math2_getrandom caller, past its four saved
 * registers. Replays log it to detect RNG call-order desyncs. */
uint32_t math2_callersite(void);
#pragma aux math2_callersite = "mov eax, [esp+16]" value[eax];
#endif

/* 16-bit LFSR pseudo-random number generator */
// FUNCTION: TIE95 0x32054
int16_t math2_getrandom(void) {
	uint16_t i;
	uint16_t carry_out;
	uint16_t seed_sign;

	if (special_features_flag) {
		if (recordingreplay) {
#ifdef __WATCOMC__
			*(uint32_t*)replayptr = math2_callersite();
#else
			*(uint32_t*)replayptr = 0;
#endif
			replayptr = (uint8_t*)replayptr + 8;
			user_nextreplaystore();
		} else if (replayviewmode) {
#ifdef __WATCOMC__
			if (math2_callersite() != *(uint32_t*)replayptr)
#else
			if (*(uint32_t*)replayptr != 0)
#endif
				printf("RETURN ADDRESS OUT OF SYNC IN GETRANDOM!\n");
			replayptr = (uint8_t*)replayptr + 8;
			user_nextreplaycount();
		}
	}

	for (i = 0; i < 16; i++) {
		carry_out = ((uint8_t)((((uint16_t)math2_randomseed >> 8) & 0xFF) ^
							   (uint16_t)((uint8_t)math2_randomseed << 1)) &
					 0x80) != 0;
		seed_sign = ((uint16_t)math2_randomseed & 0x8000) != 0;
		math2_randomseed = (int16_t)((uint16_t)math2_randomseed * 2 + carry_out);
		randomnumber = (int16_t)((uint16_t)randomnumber * 2 + seed_sign);
	}
	return randomnumber;
}

/* Empty in retail; MATH2_getrandom shares its ret. */
// FUNCTION: TIE95 0x32140
void math2_setrandomseed(void) {}

// FUNCTION: TIE95 0x32144
uint16_t math2_mphconvert(int16_t speed, uint16_t divisor) {
	uint32_t val = (uint32_t)(4660 * speed + 128);
	uint32_t shifted = val >> 8;
	uint16_t result = (uint16_t)(shifted / divisor);
	if ((divisor & shifted) > (val >> 9))
		result++;
	return result;
}

// FUNCTION: TIE95 0x32174
uint16_t math2_calcratio(uint16_t a, uint16_t b, uint16_t c) { return (uint16_t)((uint32_t)b * c / a); }

// FUNCTION: TIE95 0x32190
int32_t math2_convertwdw(uint16_t val) { return (int32_t)val << 16; }

// FUNCTION: TIE95 0x3219C
uint32_t math2_divide32u(uint32_t a, uint32_t b) {
	if (!b)
		return 0;
	return a / b;
}

// FUNCTION: TIE95 0x32394
int16_t math2_halfplane(int32_t x1, int32_t y1, int32_t x2, int32_t y2) { return (x2 * y2 - y1 * x1) >= 0; }

/* ------------------------------------------------------------------ */

/*
 * Compute radar display coordinates from 3D position deltas.
 * Projects (dx, dy) onto the screen with perspective, then clips
 * to a precomputed elliptical radar boundary.
 *
 * The boundary table stores 37 (sin*44, cos*44) byte pairs for
 * angle steps 0..90° (step = 443 out of 16384). At each angle,
 * the pair gives the maximum x and y extents of the radar circle.
 */
// FUNCTION: TIE95 0x321B4
void math2_getradarcoord(int32_t dx, int32_t dy, int32_t dz) {
	/* Rebuild radar boundary table if resolution changed */
	int32_t ax;
	int32_t ay;
	int shift;
	int16_t ratio;
	int16_t angle;
	uint16_t nav_angle;
	uint16_t table_idx;
	uint8_t max_x;
	uint8_t max_y;

	if (cached_radar_resolution != flightResolution) {
		if (flightResolution == TIE_FLIGHT_RES_VGA) {
			/* 320×200: restore pre-baked elliptical boundary table.
			 * The binary uses &randomseed[eax] with eax=2,4,... to
			 * address radarmax320 (randomseed sits 2 bytes before
			 * radarmax320, so randomseed[2] = radarmax320[0]). */
			int idx = 0;
			int angle;

			for (angle = 0; angle < 0x4000; angle += 443) {
				radarmax[idx] = radarmax320[idx];
				radarmax[idx + 1] = radarmax320[idx + 1];
				idx += 2;
			}
		} else {
			/* 640×480: compute circular boundary from sin/cos * 44 */
			int idx = 0;
			int angle;

			for (angle = 0; angle < 0x4000; angle += 443) {
				radarmax[idx] = (uint8_t)trig2_sinewordmult(44, (uint16_t)angle);
				radarmax[idx + 1] = (uint8_t)trig2_cosinewordmult(44, (uint16_t)angle);
				idx += 2;
			}
		}
		cached_radar_resolution = flightResolution;
	}

	/* Perspective projection: shift by (perspShift - 5), divide by dz */
	ax = (dx < 0) ? -dx : dx;
	ay = (dy < 0) ? -dy : dy;
	shift = perspShift - 5;

	ax <<= shift;
	if (dz)
		ax /= dz;
	if (ax > 0x7FFF)
		ax = 0x7FFF;

	ay <<= shift;
	if (dz)
		ay /= dz;
	if (ay > 0x7FFF)
		ay = 0x7FFF;

	radarx = (int16_t)ax;
	radary = (int16_t)ay;

	/* Compute angle via calcarctan(ax, ay) */
	trig2_calcarctan(ax, ay, &angle, &ratio);

	/* Convert to navigation angle: -angle + 90° */
	nav_angle = (uint16_t)(-angle + 0x4000);

	/* Look up radar boundary at this angle */
	table_idx = 2 * (nav_angle / 443);

	/* Clip x to boundary */
	max_x = radarmax[table_idx];
	if (max_x < (uint16_t)radarx)
		radarx = max_x;
	if (dx < 0)
		radarx = -radarx;

	/* Clip y to boundary */
	max_y = radarmax[table_idx + 1];
	if (max_y < (uint16_t)radary)
		radary = max_y;
	if (dy < 0)
		radary = -radary;
}
