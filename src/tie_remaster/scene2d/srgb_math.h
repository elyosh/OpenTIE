#ifndef TIE_SCENE2D_SRGB_MATH_H
#define TIE_SCENE2D_SRGB_MATH_H

/*
 * sRGB → linear helpers — pure math, shared by the compose layer and the
 * applications' GPU helper layers.
 *
 * The engine palette + every classic UI / cutscene / cockpit byte source is
 * sRGB-authored. Anywhere a CPU-sampled byte is pushed as a shader uniform /
 * clear colour / vertex attribute it MUST be linearised first — the
 * downstream RTs are _SRGB-formatted, so the HW re-encodes linear → sRGB at
 * store. Uses the exact piecewise sRGB EOTF, the same curve the HW _SRGB
 * decoder applies on texture sample, so CPU-pushed tints and texture-sampled
 * colours from the same palette byte land at identical linear values.
 */

#include <math.h>
#include <stdint.h>

/* Normalized sRGB-encoded channel to linear light. */
static inline float TieScene2dSrgb_ToLinear(float c) {
	return (c <= 0.04045f) ? (c / 12.92f) : powf((c + 0.055f) / 1.055f, 2.4f);
}

static inline float TieScene2dSrgb_ByteToLinear(uint8_t b) {
	return TieScene2dSrgb_ToLinear((float)b / 255.0f);
}

/* Unpack an ARGB8888 palette entry (`0xAARRGGBB`) into 3 linear-light
 * floats. Alpha is data, not colour — dropped here since no call site
 * needs it. */
static inline void TieScene2dSrgb_PalToLinearRgb(uint32_t argb, float* r, float* g, float* b) {
	*r = TieScene2dSrgb_ByteToLinear((uint8_t)((argb >> 16) & 0xFFu));
	*g = TieScene2dSrgb_ByteToLinear((uint8_t)((argb >> 8) & 0xFFu));
	*b = TieScene2dSrgb_ByteToLinear((uint8_t)(argb & 0xFFu));
}

#endif
