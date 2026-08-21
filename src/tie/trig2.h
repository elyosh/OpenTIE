#ifndef __TRIG2_H__
#define __TRIG2_H__

#include <stdint.h>

/*
 * Fixed-point trigonometry library.
 * Angles are 16-bit: 0x0000 = 0°, 0x4000 = 90°, 0x8000 = 180°, 0xFFFF ≈ 360°.
 * Sine values are unsigned 0..65534 (0 = 0.0, 65534 = 1.0).
 * Signed variants return -32767..+32767.
 */

/* Sine / Cosine lookups */
uint16_t trig2_getsine(uint16_t angle);
uint16_t trig2_calcsineofangle(uint16_t angle);
int16_t trig2_getsignedsin(uint16_t angle);
uint16_t trig2_getcosine(uint16_t angle);
int16_t trig2_getsignedcos(int16_t angle);

/* Multiply by trig (16-bit value × sin/cos, returns 16-bit) */
int16_t trig2_sinewordmult(int16_t val, uint16_t angle);
int16_t trig2_cosinewordmult(int16_t val, uint16_t angle);

/* Multiply by trig (32-bit value × sin/cos, returns 32-bit) */
int32_t trig2_sinedwordmult(int32_t val, uint16_t angle);
int32_t trig2_cosinedwordmult(int32_t val, uint16_t angle);

/* Inverse trig */
int16_t trig2_arcsin(int16_t val);
int16_t trig2_arccos(int16_t val);
int16_t trig2_arctan(int32_t y, int32_t x);
void trig2_calcarctan(int32_t a, int32_t b);

/* Polar ↔ Cartesian conversions (operate on globals) */
void trig2_ptoc3dim(void);
void trig2_ptoc2dim(void);
void trig2_movexyz(uint16_t distance, int16_t pitch, uint16_t heading);
void trig2_ctop(int32_t x, int32_t y, int32_t z);
void trig2_ctop2dim(int32_t x, int32_t y);

/* Working globals (set by ptoc/ctop/movexyz functions) */
extern int32_t trig2_xoffset, trig2_yoffset, trig2_zoffset;
extern int32_t trig2_xmovedist, trig2_ymovedist, trig2_zmovedist;
extern int32_t trig2_rho, trig2_distanceplane, trig2_polardistance;
extern int32_t trig2_cartesianxoffset, trig2_cartesianyoffset;
extern int16_t trig2_theta, trig2_phi;
extern int16_t trig2_xyangle, trig2_zangle, trig2_angleplane;
extern int16_t trig2_signx, trig2_signy, trig2_signz;

#endif
