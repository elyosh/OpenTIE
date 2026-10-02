/*
 * TRANSFM2.C — 3D coordinate transformation pipeline
 *
 * Transforms vertices from world/object space through eye space to
 * screen space for the 3D flight engine. Handles:
 * - World-to-eye rotation via a 3×3 matrix in 1.15 fixed-point
 * - Perspective projection with overflow protection
 * - Near-plane (z=0) clipping with edge interpolation
 * - Per-vertex lighting interpolation at clip points
 * - Edge classification for the polygon rasterizer
 */

#include "tie/transfm2.h"
#include "tie/math2.h"
#include "tie/math2_wide.h"
#include "tie/xtrans2.h"

/* ---- Module-owned globals (from watdbg "static") ---- */

/* Cached per-component multiply tables (16 entries each, indexed by vertex & 0xF) */
// GLOBAL: TIE95 0xEBF30
static int32_t lastC3mul[16];
// GLOBAL: TIE95 0xEBF70
static int32_t lastC2mul[16];
// GLOBAL: TIE95 0xEBFB0
static int32_t lastB1mul[16];
// GLOBAL: TIE95 0xEBFF0
static int32_t lastB3mul[16];
// GLOBAL: TIE95 0xEC030
static int32_t lastB2mul[16];
// GLOBAL: TIE95 0xEC070
static int32_t lastA1mul[16];
// GLOBAL: TIE95 0xEC0B0
static int32_t lastA3mul[16];
// GLOBAL: TIE95 0xEC0F0
static int32_t lastA2mul[16];
// GLOBAL: TIE95 0xEC130
static int32_t lastC1mul[16];

/* Rotation matrix (set by FVIEW) */
// GLOBAL: TIE95 0xEC174
int32_t worldeyeA1;
// GLOBAL: TIE95 0xEC178
int32_t worldeyeA2;
// GLOBAL: TIE95 0xEC17C
int32_t worldeyeA3;
// GLOBAL: TIE95 0xEC18C
int32_t worldeyeB1;
// GLOBAL: TIE95 0xEC190
int32_t worldeyeB2;
// GLOBAL: TIE95 0xEC194
int32_t worldeyeB3;
// GLOBAL: TIE95 0xEC180
int32_t worldeyeC1;
// GLOBAL: TIE95 0xEC184
int32_t worldeyeC2;
// GLOBAL: TIE95 0xEC188
int32_t worldeyeC3;
// GLOBAL: TIE95 0xEC170
// GLOBAL: TIE98 0x58E064
int32_t transfm2_screenyoffset;

/* Working state */
// GLOBAL: TIE95 0xEC198
static uint16_t zratio;
// GLOBAL: TIE95 0xEC19A
static int16_t eyeysign;
// GLOBAL: TIE95 0xEC19C
static int16_t eyexsign;
// GLOBAL: TIE95 0xEC19E
static uint8_t offleftcnt;
// GLOBAL: TIE95 0xEC19F
static uint8_t offscreencnt;
// GLOBAL: TIE95 0xEC1A0
static uint8_t validcnt;
// GLOBAL: TIE95 0xEC1A1
static uint8_t slivercnt;
// GLOBAL: TIE95 0xEC1A2
static uint8_t offrightcnt;

#include "tie/drawpol.h" /* authoritative types for DRAWPOL-owned globals:
                         * PolyVert, calcflag[], firsteyexyz, firstvertnorm,
                         * firstvertptr, vertexlight[], newscreenxy,
                         * min/maxscreen*, numpoints, numeyezpos, counter,
                         * samexcnt/sameycnt, rotlightX/Y/Z, objectx/y/z. */
#include "tie/drawln2.h" /* point1ptr (drawln2.c-owned) */
#include "tie/logbuf2.h" /* pixelswide, halfpixelswide, pixelsdeep, halfpixelsdeep */
#include "tie/tie.h"     /* yAspect (watdbg-owned by tie.c) */
#include "tie/trace2.h"  /* someznegflag */

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

/* ================================================================== */

// FUNCTION: TIE95 0x59CA0
void transfm2_clipobjecteyez(int32_t x, int32_t y, int32_t z) {
	int32_t zneg = -objecteyez;
	if (x > objecteyex)
		objecteyex += math2_ABoverC32(zneg, x - objecteyex, z + zneg);
	else
		objecteyex -= math2_ABoverC32(zneg, objecteyex - x, z + zneg);
	if (y > objecteyey)
		objecteyey += math2_ABoverC32(zneg, y - objecteyey, z + zneg);
	else
		objecteyey -= math2_ABoverC32(zneg, objecteyey - y, z + zneg);
	objecteyez = 1;
}

/* ================================================================== */

// FUNCTION: TIE95 0x59D20
int32_t transfm2_geteyex(int32_t x, int32_t y, int32_t z) {
	return math2_mul_q15(x, worldeyeA1) + math2_mul_q15(y, worldeyeB1) + math2_mul_q15(z, worldeyeC1);
}

// FUNCTION: TIE95 0x59D68
int32_t transfm2_geteyey(int32_t x, int32_t y, int32_t z) {
	return math2_mul_q15(x, worldeyeA2) + math2_mul_q15(y, worldeyeB2) + math2_mul_q15(z, worldeyeC2);
}

// FUNCTION: TIE95 0x59DB0
int32_t transfm2_geteyez(int32_t x, int32_t y, int32_t z) {
	return math2_mul_q15(x, worldeyeA3) + math2_mul_q15(y, worldeyeB3) + math2_mul_q15(z, worldeyeC3);
}

/* ================================================================== */

/*
 * Batch transform: world vertices → eye coordinates.
 * Source: packed int16 (x, y_hi | z_lo) triples (6 bytes each).
 * Dest: int32 (eyex, eyey, eyez) triples.
 * Uses cached multiply tables for vertices with 0x7F00 reference codes.
 */
// FUNCTION: TIE95 0x59DF8
int32_t* transfm2_geteyecoords(const int16_t* source, int32_t* dest) {
	int32_t ptIndex = (uint16_t)numpoints;
	numeyezpos = 0;

	while (ptIndex) {
		int32_t multIndex = ptIndex & 0xF;
		int32_t xCoord = source[0];

		int32_t yCoord;
		int32_t zCoord;
		int32_t eyez;

		if ((xCoord & 0xFF00) == 0x7F00) {
			int32_t ref = (2 * (uint8_t)ptIndex + (uint8_t)xCoord) & 0x1F;
			lastA1mul[multIndex] = lastA1mul[ref >> 1];
			lastA2mul[multIndex] = lastA2mul[ref >> 1];
			lastA3mul[multIndex] = lastA3mul[ref >> 1];
		} else {
			lastA1mul[multIndex] = rotworldeyeA1 * xCoord;
			lastA2mul[multIndex] = rotworldeyeA2 * xCoord;
			lastA3mul[multIndex] = rotworldeyeA3 * xCoord;
		}

		yCoord = source[1];
		if ((yCoord & 0xFF00) == 0x7F00) {
			int32_t ref = (2 * (uint8_t)ptIndex + (uint8_t)yCoord) & 0x1F;
			lastB1mul[multIndex] = lastB1mul[ref >> 1];
			lastB2mul[multIndex] = lastB2mul[ref >> 1];
			lastB3mul[multIndex] = lastB3mul[ref >> 1];
		} else {
			lastB1mul[multIndex] = rotworldeyeB1 * yCoord;
			lastB2mul[multIndex] = rotworldeyeB2 * yCoord;
			lastB3mul[multIndex] = rotworldeyeB3 * yCoord;
		}

		zCoord = source[2];
		if ((zCoord & 0xFF00) == 0x7F00) {
			int32_t ref = (2 * (uint8_t)ptIndex + (uint8_t)zCoord) & 0x1F;
			lastC1mul[multIndex] = lastC1mul[ref >> 1];
			lastC2mul[multIndex] = lastC2mul[ref >> 1];
			lastC3mul[multIndex] = lastC3mul[ref >> 1];
		} else {
			lastC1mul[multIndex] = rotworldeyeC1 * zCoord;
			lastC2mul[multIndex] = rotworldeyeC2 * zCoord;
			lastC3mul[multIndex] = rotworldeyeC3 * zCoord;
		}

		dest[0] =
			objectx +
			(int16_t)((uint32_t)(lastA1mul[multIndex] + lastB1mul[multIndex] + lastC1mul[multIndex]) >> 16);
		dest[1] =
			objecty +
			(int16_t)((uint32_t)(lastA2mul[multIndex] + lastB2mul[multIndex] + lastC2mul[multIndex]) >> 16);
		eyez =
			objectz +
			(int16_t)((uint32_t)(lastA3mul[multIndex] + lastB3mul[multIndex] + lastC3mul[multIndex]) >> 16);
		if (eyez >= 0)
			numeyezpos++;
		dest[2] = eyez;

		dest += 3;
		source += 3;
		ptIndex--;
	}
	return dest;
}

/* S2 variant: same algorithm with shift-2 scaling */
// FUNCTION: TIE95 0x5A050
int32_t* transfm2_geteyecoordsS2(const int16_t* source, int32_t* dest) {
	/* Scale summed products by 14 bits instead of the standard 16. */
	int32_t ptIndex = (uint16_t)numpoints;
	numeyezpos = 0;

	while (ptIndex) {
		int32_t multIndex = ptIndex & 0xF;
		int32_t xCoord = source[0];

		int32_t yCoord;
		int32_t zCoord;
		int32_t eyez;

		if ((xCoord & 0xFF00) == 0x7F00) {
			int32_t ref = (2 * (uint8_t)ptIndex + (uint8_t)xCoord) & 0x1F;
			lastA1mul[multIndex] = lastA1mul[ref >> 1];
			lastA2mul[multIndex] = lastA2mul[ref >> 1];
			lastA3mul[multIndex] = lastA3mul[ref >> 1];
		} else {
			lastA1mul[multIndex] = rotworldeyeA1 * xCoord;
			lastA2mul[multIndex] = rotworldeyeA2 * xCoord;
			lastA3mul[multIndex] = rotworldeyeA3 * xCoord;
		}

		yCoord = source[1];
		if ((yCoord & 0xFF00) == 0x7F00) {
			int32_t ref = (2 * (uint8_t)ptIndex + (uint8_t)yCoord) & 0x1F;
			lastB1mul[multIndex] = lastB1mul[ref >> 1];
			lastB2mul[multIndex] = lastB2mul[ref >> 1];
			lastB3mul[multIndex] = lastB3mul[ref >> 1];
		} else {
			lastB1mul[multIndex] = rotworldeyeB1 * yCoord;
			lastB2mul[multIndex] = rotworldeyeB2 * yCoord;
			lastB3mul[multIndex] = rotworldeyeB3 * yCoord;
		}

		zCoord = source[2];
		if ((zCoord & 0xFF00) == 0x7F00) {
			int32_t ref = (2 * (uint8_t)ptIndex + (uint8_t)zCoord) & 0x1F;
			lastC1mul[multIndex] = lastC1mul[ref >> 1];
			lastC2mul[multIndex] = lastC2mul[ref >> 1];
			lastC3mul[multIndex] = lastC3mul[ref >> 1];
		} else {
			lastC1mul[multIndex] = rotworldeyeC1 * zCoord;
			lastC2mul[multIndex] = rotworldeyeC2 * zCoord;
			lastC3mul[multIndex] = rotworldeyeC3 * zCoord;
		}

		dest[0] = objectx + ((lastA1mul[multIndex] + lastB1mul[multIndex] + lastC1mul[multIndex]) >> 14);
		dest[1] = objecty + ((lastA2mul[multIndex] + lastB2mul[multIndex] + lastC2mul[multIndex]) >> 14);
		eyez = objectz + ((lastA3mul[multIndex] + lastB3mul[multIndex] + lastC3mul[multIndex]) >> 14);
		if (eyez >= 0)
			numeyezpos++;
		dest[2] = eyez;

		dest += 3;
		source += 3;
		ptIndex--;
	}
	return dest;
}

/* ================================================================== */

// FUNCTION: TIE95 0x5A2A8
void transfm2_geteyeminmax(const int16_t* source, int32_t* dest) {
	int32_t y1 = source[1];
	int32_t x1 = source[0];
	int32_t z1 = source[2];
	int32_t x2 = source[3];
	int32_t y2 = source[4];
	int32_t z2 = source[5];

	/* X axis (row 1: A1, B1, C1) */
	int32_t mn = 0, mx = 0;
	int32_t a = rotworldeyeA1 * x1, b = rotworldeyeA1 * x2;
	if (b < a) {
		mn = b;
		mx = a;
	} else {
		mn = a;
		mx = b;
	}
	a = rotworldeyeB1 * y1;
	b = rotworldeyeB1 * y2;
	if (b < a) {
		mn += b;
		mx += a;
	} else {
		mn += a;
		mx += b;
	}
	a = rotworldeyeC1 * z1;
	b = rotworldeyeC1 * z2;
	if (b < a) {
		mn += b;
		mx += a;
	} else {
		mn += a;
		mx += b;
	}
	dest[0] = objectx + (int16_t)((uint32_t)mn >> 16);
	dest[1] = objectx + (int16_t)((uint32_t)mx >> 16);

	/* Y axis (row 2: A2, B2, C2) */
	mn = 0;
	mx = 0;
	a = rotworldeyeA2 * x1;
	b = rotworldeyeA2 * x2;
	if (b < a) {
		mn = b;
		mx = a;
	} else {
		mn = a;
		mx = b;
	}
	a = rotworldeyeB2 * y1;
	b = rotworldeyeB2 * y2;
	if (b < a) {
		mn += b;
		mx += a;
	} else {
		mn += a;
		mx += b;
	}
	a = rotworldeyeC2 * z1;
	b = rotworldeyeC2 * z2;
	if (b < a) {
		mn += b;
		mx += a;
	} else {
		mn += a;
		mx += b;
	}
	dest[2] = objecty + (int16_t)((uint32_t)mn >> 16);
	dest[3] = objecty + (int16_t)((uint32_t)mx >> 16);

	/* Z axis (row 3: A3, B3, C3) */
	mn = 0;
	mx = 0;
	a = rotworldeyeA3 * x1;
	b = rotworldeyeA3 * x2;
	if (b < a) {
		mn = b;
		mx = a;
	} else {
		mn = a;
		mx = b;
	}
	a = rotworldeyeB3 * y1;
	b = rotworldeyeB3 * y2;
	if (b < a) {
		mn += b;
		mx += a;
	} else {
		mn += a;
		mx += b;
	}
	a = rotworldeyeC3 * z1;
	b = rotworldeyeC3 * z2;
	if (b < a) {
		mn += b;
		mx += a;
	} else {
		mn += a;
		mx += b;
	}
	dest[4] = objectz + (int16_t)((uint32_t)mn >> 16);
	dest[5] = objectz + (int16_t)((uint32_t)mx >> 16);
}

// FUNCTION: TIE95 0x5A458
void transfm2_geteyeminmaxS2(const int16_t* source, int32_t* dest) {
	int32_t y1 = source[1];
	int32_t x1 = source[0];
	int32_t z1 = source[2];
	int32_t x2 = source[3];
	int32_t y2 = source[4];
	int32_t z2 = source[5];
	int32_t mn, mx;

	mn = x1 * rotworldeyeA1;
	mx = x2 * rotworldeyeA1;
	if (mx < mn) {
		int32_t t = mn;
		mn = mx;
		mx = t;
	}
	{
		int32_t a = y1 * rotworldeyeB1;
		int32_t b = y2 * rotworldeyeB1;
		if (a > b) {
			mn += b;
			mx += a;
		} else {
			mn += a;
			mx += b;
		}
	}
	{
		int32_t a = z1 * rotworldeyeC1;
		int32_t b = z2 * rotworldeyeC1;
		if (a > b) {
			mn += b;
			mx += a;
		} else {
			mn += a;
			mx += b;
		}
	}
	*dest++ = objectx + (mn >> 14);
	*dest++ = objectx + (mx >> 14);

	mn = x1 * rotworldeyeA2;
	mx = x2 * rotworldeyeA2;
	if (mx < mn) {
		int32_t t = mn;
		mn = mx;
		mx = t;
	}
	{
		int32_t a = y1 * rotworldeyeB2;
		int32_t b = y2 * rotworldeyeB2;
		if (a > b) {
			mn += b;
			mx += a;
		} else {
			mn += a;
			mx += b;
		}
	}
	{
		int32_t a = z1 * rotworldeyeC2;
		int32_t b = z2 * rotworldeyeC2;
		if (a > b) {
			mn += b;
			mx += a;
		} else {
			mn += a;
			mx += b;
		}
	}
	*dest++ = objecty + (mn >> 14);
	*dest++ = objecty + (mx >> 14);

	mn = x1 * rotworldeyeA3;
	mx = x2 * rotworldeyeA3;
	if (mx < mn) {
		int32_t t = mn;
		mn = mx;
		mx = t;
	}
	{
		int32_t a = y1 * rotworldeyeB3;
		int32_t b = y2 * rotworldeyeB3;
		if (a > b) {
			mn += b;
			mx += a;
		} else {
			mn += a;
			mx += b;
		}
	}
	{
		int32_t a = z1 * rotworldeyeC3;
		int32_t b = z2 * rotworldeyeC3;
		if (a > b) {
			mn += b;
			mx += a;
		} else {
			mn += a;
			mx += b;
		}
	}
	*dest++ = objectz + (mn >> 14);
	*dest = objectz + (mx >> 14);
}

// FUNCTION: TIE95 0x5A608
void transfm2_getworldminmax(const int16_t* source, int16_t* dest) {
	int32_t x1 = source[0];
	int32_t z1 = source[2];
	int32_t x2 = source[3];
	int32_t z2 = source[5];
	int32_t y1 = -source[1];
	int32_t y2 = -source[4];

	int32_t mn, mx, a, b;

	/* Row 1: craftS1, craftf1, craftU1 */
	a = craftS1 * x1;
	b = craftS1 * x2;
	if (b < a) {
		mn = b;
		mx = a;
	} else {
		mn = a;
		mx = b;
	}
	a = craftf1 * y1;
	b = craftf1 * y2;
	if (b < a) {
		mn += b;
		mx += a;
	} else {
		mn += a;
		mx += b;
	}
	a = craftU1 * z1;
	b = craftU1 * z2;
	if (b < a) {
		mn += b;
		mx += a;
	} else {
		mn += a;
		mx += b;
	}
	dest[0] += (int16_t)((mn + 0x100000) >> 21);
	dest[3] += (int16_t)((mx - 0x100000) >> 21);

	/* Row 2: craftS2, craftf2, craftU2 */
	a = craftS2 * x1;
	b = craftS2 * x2;
	if (b < a) {
		mn = b;
		mx = a;
	} else {
		mn = a;
		mx = b;
	}
	a = craftf2 * y1;
	b = craftf2 * y2;
	if (b < a) {
		mn += b;
		mx += a;
	} else {
		mn += a;
		mx += b;
	}
	a = craftU2 * z1;
	b = craftU2 * z2;
	if (b < a) {
		mn += b;
		mx += a;
	} else {
		mn += a;
		mx += b;
	}
	dest[1] += (int16_t)((mn + 0x100000) >> 21);
	dest[4] += (int16_t)((mx - 0x100000) >> 21);

	/* Row 3: craftS3, craftf3, craftU3 */
	a = craftS3 * x1;
	b = craftS3 * x2;
	if (b < a) {
		mn = b;
		mx = a;
	} else {
		mn = a;
		mx = b;
	}
	a = craftf3 * y1;
	b = craftf3 * y2;
	if (b < a) {
		mn += b;
		mx += a;
	} else {
		mn += a;
		mx += b;
	}
	a = craftU3 * z1;
	b = craftU3 * z2;
	if (b < a) {
		mn += b;
		mx += a;
	} else {
		mn += a;
		mx += b;
	}
	dest[2] += (int16_t)((mn + 0x100000) >> 21);
	dest[5] += (int16_t)((mx - 0x100000) >> 21);
}

// FUNCTION: TIE95 0x5A7F4
void transfm2_getworldminmaxS2(const int16_t* source, int16_t* dest) {
	int32_t x1 = source[0];
	int32_t z1 = source[2];
	int32_t x2 = source[3];
	int32_t z2 = source[5];
	int32_t y1 = -source[1];
	int32_t y2 = -source[4];

	int32_t mn, mx, a, b;

	a = craftS1 * x1;
	b = craftS1 * x2;
	if (b < a) {
		mn = b;
		mx = a;
	} else {
		mn = a;
		mx = b;
	}
	a = craftf1 * y1;
	b = craftf1 * y2;
	if (b < a) {
		mn += b;
		mx += a;
	} else {
		mn += a;
		mx += b;
	}
	a = craftU1 * z1;
	b = craftU1 * z2;
	if (b < a) {
		mn += b;
		mx += a;
	} else {
		mn += a;
		mx += b;
	}
	dest[0] += (int16_t)((mn + 0x40000) >> 19);
	dest[3] += (int16_t)((mx - 0x40000) >> 19);

	a = craftS2 * x1;
	b = craftS2 * x2;
	if (b < a) {
		mn = b;
		mx = a;
	} else {
		mn = a;
		mx = b;
	}
	a = craftf2 * y1;
	b = craftf2 * y2;
	if (b < a) {
		mn += b;
		mx += a;
	} else {
		mn += a;
		mx += b;
	}
	a = craftU2 * z1;
	b = craftU2 * z2;
	if (b < a) {
		mn += b;
		mx += a;
	} else {
		mn += a;
		mx += b;
	}
	dest[1] += (int16_t)((mn + 0x40000) >> 19);
	dest[4] += (int16_t)((mx - 0x40000) >> 19);

	a = craftS3 * x1;
	b = craftS3 * x2;
	if (b < a) {
		mn = b;
		mx = a;
	} else {
		mn = a;
		mx = b;
	}
	a = craftf3 * y1;
	b = craftf3 * y2;
	if (b < a) {
		mn += b;
		mx += a;
	} else {
		mn += a;
		mx += b;
	}
	a = craftU3 * z1;
	b = craftU3 * z2;
	if (b < a) {
		mn += b;
		mx += a;
	} else {
		mn += a;
		mx += b;
	}
	dest[2] += (int16_t)((mn + 0x40000) >> 19);
	dest[5] += (int16_t)((mx - 0x40000) >> 19);
}

/* ================================================================== */

/* Z=0 plane transforms: 2D points (x,y), z assumed 0 */
// FUNCTION: TIE95 0x5A9E0
int32_t* transfm2_geteyecoordsZ0(const int16_t* source, int32_t* dest) {
	int32_t count;

	for (count = (uint16_t)numpoints; count; count--) {
		int32_t x = source[0];
		int32_t y = source[1];
		int32_t coord;

		coord = objectx + ((rotworldeyeA1 * x + rotworldeyeB1 * y + 0x4000) >> 15);
		*dest++ = coord;
		coord = objecty + ((rotworldeyeA2 * x + rotworldeyeB2 * y + 0x4000) >> 15);
		*dest++ = coord;
		coord = objectz + ((rotworldeyeA3 * x + rotworldeyeB3 * y + 0x4000) >> 15);
		if (coord >= 0)
			numeyezpos++;
		*dest++ = coord;
		source += 2;
	}
	return dest;
}

// FUNCTION: TIE95 0x5AA94
int32_t* transfm2_geteyecoordsZ0s16(const int16_t* source, int32_t* dest) {
	int32_t count;

	for (count = (uint16_t)numpoints; count; count--) {
		int32_t x = source[0];
		int32_t y = source[1];
		int32_t coord;

		dest[0] = objectx + 2 * (rotworldeyeA1 * x + rotworldeyeB1 * y + 0x4000);
		dest[1] = objecty + 2 * (rotworldeyeA2 * x + rotworldeyeB2 * y + 0x4000);
		coord = objectz + 2 * (rotworldeyeA3 * x + rotworldeyeB3 * y + 0x4000);
		if (coord >= 0)
			numeyezpos++;
		dest[2] = coord;
		dest += 3;
		source += 2;
	}
	return dest;
}

// FUNCTION: TIE95 0x5AB44
int32_t* transfm2_geteyecoordsZ0s8(const int16_t* source, int32_t* dest) {
	int32_t count;

	for (count = (uint16_t)numpoints; count; count--) {
		int32_t x = source[0];
		int32_t y = source[1];
		int32_t coord;

		dest[0] = objectx + ((rotworldeyeA1 * x + rotworldeyeB1 * y + 0x4000) >> 7);
		dest[1] = objecty + ((rotworldeyeA2 * x + rotworldeyeB2 * y + 0x4000) >> 7);
		coord = objectz + ((rotworldeyeA3 * x + rotworldeyeB3 * y + 0x4000) >> 7);
		if (coord >= 0)
			numeyezpos++;
		dest[2] = coord;
		dest += 3;
		source += 2;
	}
	return dest;
}

/* ================================================================== */

/* Batch screen projection with z-clipping */
// FUNCTION: TIE95 0x5ABF8
int32_t* transfm2_getscreencoords(const DRAWPOL_EyeVertex* source, int32_t* dest) {
	int vertex = 0;

	while (counter) {
		if (source[vertex].z < 0) {
			dest = transfm2_clipeyez(source, vertex, dest);
		} else {
			int32_t sx;
			int32_t sy;

			sx = transfm2_getscreenx(source[vertex].x, source[vertex].z);
			dest[0] = sx;
			if (sx > *minscreenx) {
				if (sx > *maxscreenx)
					maxscreenx = dest;
			} else if (sx < *minscreenx) {
				minscreenx = dest;
			} else {
				samexcnt++;
			}

			sy = transfm2_getscreeny(source[vertex].y, source[vertex].z);
			dest[1] = sy;
			if (sy > minscreeny[1]) {
				if (sy > maxscreeny[1])
					maxscreeny = dest;
			} else if (sy < minscreeny[1]) {
				minscreeny = dest;
			} else {
				sameycnt++;
			}
			dest += 2;
		}
		vertex++;
		counter--;
	}
	return dest;
}

/* ================================================================== */

/* Eye-space → screen projection. Saturates at 0x7FFFFF00 when the
 * 64-bit numerator's high half is >= eye-Z (point near or behind the
 * eye plane); callers treat the resulting out-of-range coordinate as
 * off-screen and clip it. All arithmetic is bit-pattern-faithful to
 * the binary (single 64-bit unsigned divide, plain wrapping add for
 * the screen-center offset). */
// FUNCTION: TIE95 0x5ACC4
int32_t transfm2_getscreenx(int32_t eyex, int32_t eyez) {
	bool neg = (eyex < 0);
	uint32_t mag = neg ? -(uint32_t)eyex : (uint32_t)eyex;

	uint32_t result = math2_project_u32(mag, perspShift, halfPerspFactor, (uint32_t)eyez);
	if (neg)
		result = -result;
	return (int32_t)(halfpixelswide + result);
}

// FUNCTION: TIE95 0x5AD28
void transfm2_doxminmax(int32_t screenx, int32_t* ptr) {
	if (screenx > *minscreenx) {
		if (screenx > *maxscreenx)
			maxscreenx = ptr;
	} else if (screenx < *minscreenx) {
		minscreenx = ptr;
	} else {
		samexcnt++;
	}
}

// FUNCTION: TIE95 0x5AD60
int32_t transfm2_getscreeny(int32_t eyey, int32_t eyez) {
	bool neg = (eyey < 0);
	uint32_t mag = neg ? -(uint32_t)eyey : (uint32_t)eyey;

	uint32_t result = math2_project_u32(mag, perspShift, halfPerspFactor, (uint32_t)eyez);
	if (neg)
		result = -result;

	if (yAspect) {
		int32_t r = (int32_t)result;
		r = (r >= 0) ? math2_longfraction(r, yAspect) : -math2_longfraction(-r, yAspect);
		result = (uint32_t)r;
	}
	return (int32_t)((uint32_t)transfm2_screenyoffset + halfpixelsdeep + result);
}

// FUNCTION: TIE95 0x5ADF8
void transfm2_doyminmax(int32_t screeny, int32_t* ptr) {
	if (screeny > minscreeny[1]) {
		if (screeny > maxscreeny[1])
			maxscreeny = ptr;
	} else if (screeny < minscreeny[1]) {
		minscreeny = ptr;
	} else {
		sameycnt++;
	}
}

// FUNCTION: TIE95 0x5AE30
int32_t* transfm2_clipeyez(const DRAWPOL_EyeVertex* source, int vertex, int32_t* dest) {
	int32_t* result = dest;

	numpoints--;

	/* Previous ring vertex */
	if (source[vertex - 1].z >= 0) {
		numpoints++;
		result = transfm2_calczintersect(&source[vertex], &source[vertex - 1], dest);
	}

	/* Next ring vertex */
	if (source[vertex + 1].z >= 0) {
		numpoints++;
		return transfm2_calczintersect(&source[vertex], &source[vertex + 1], result);
	}

	return result;
}

/* ================================================================== */

// FUNCTION: TIE95 0x5AE84
int32_t* transfm2_calczintersect(const DRAWPOL_EyeVertex* source1, const DRAWPOL_EyeVertex* source2,
								 int32_t* dest) {
	int32_t zneg;
	int32_t ztotal;
	int32_t diff;
	int32_t interp;
	int32_t val;
	int32_t limit;
	int32_t screen;

	/* Z-ratio for linear interpolation between two vertices straddling
	 * the z=0 plane. */
	zneg = -source1->z;
	ztotal = source2->z - source1->z;
	while (zneg & 0xFFFF0000) {
		zneg >>= 1;
		ztotal >>= 1;
	}
	zratio = (uint16_t)((zneg << 16) / ztotal);

	/* Project the clipped point (interpolated at z=0) to screen x,
	 * clamping to avoid overflow. */
	eyexsign = 0;
	diff = source2->x - source1->x;
	if (diff < 0) {
		eyexsign = 1;
		diff = -diff;
	}
	interp = math2_longfraction(diff, zratio);
	if (eyexsign)
		interp = -interp;
	val = source1->x + interp;

	limit = 0x7FFFFFFF >> perspShift;

	if (val > limit)
		screen = 0x7FFFFFFF - perspFactor;
	else if (val < -limit)
		screen = perspFactor - 0x7FFFFFFF;
	else
		/* Retail emits `shl eax, cl` (logical bit-shift, sign-agnostic).
		 * C signed left-shift of a negative value is UB even when the
		 * result fits — go through uint32 to match the asm exactly. */
		screen = (int32_t)((uint32_t)val << perspShift);

	dest[0] = halfpixelswide + screen;
	transfm2_doxminmax(dest[0], dest);

	eyeysign = 0;
	diff = source2->y - source1->y;
	if (diff < 0) {
		eyeysign = 1;
		diff = -diff;
	}
	interp = math2_longfraction(diff, zratio);
	if (eyeysign)
		interp = -interp;
	val = source1->y + interp;

	if (yAspect) {
		eyeysign = 0;
		if (val < 0) {
			eyeysign = 1;
			val = -val;
		}
		val = math2_longfraction(val, yAspect);
		if (eyeysign)
			val = -val;
	}

	limit = 0x7FFFFFFF >> perspShift;

	if (val > limit)
		screen = 0x7FFFFFFF - perspFactor;
	else if (val < -limit)
		screen = perspFactor - 0x7FFFFFFF;
	else
		screen = (int32_t)((uint32_t)val << perspShift);

	dest[1] = transfm2_screenyoffset + halfpixelsdeep + screen;
	transfm2_doyminmax(dest[1], dest);

	return dest + 2;
}

/* ================================================================== */

/*
 * Z=0 intersection for face edges with vertex lighting interpolation.
 * This is the most complex function — handles per-vertex normal-based
 * lighting computation and interpolation at the clip point.
 */
// FUNCTION: TIE95 0x5B090
TRANSFM2_ScreenPoint* transfm2_facezintersect(int16_t negV, int16_t posV, const DRAWPOL_EyeVertex* source1,
											  const DRAWPOL_EyeVertex* source2, TRANSFM2_ScreenPoint* dest) {
	int16_t lightVal;
	int32_t zneg;
	int32_t ztotal;
	int32_t diff;
	int32_t interp;
	int32_t val;
	int32_t limit;
	int32_t screen;

	/* Z-ratio for linear interpolation between two vertices straddling
	 * the z=0 plane. */
	zneg = -source1->z;
	ztotal = source2->z - source1->z;
	while (zneg & 0xFFFF0000) {
		zneg >>= 1;
		ztotal >>= 1;
	}
	zratio = (uint16_t)((zneg << 16) / ztotal);

	/* Vertex lighting (if face has lighting flag 0x40) */
	lightVal = 0;
	if (firstvertptr[-1] & DRAWPOL_FACE_GOURAUD) {
		/* Compute lighting for negV if not cached */
		int16_t negLight;
		int16_t posLight;

		if ((int16_t)vertexlight[negV] == -1) {
			PolyVert* norm = &firstvertnorm[negV];
			int32_t dot = rotlightX * norm->x + rotlightY * norm->y + rotlightZ * norm->z;
			if (dot >= 0x40000000)
				dot = 0x3FFF0000;
			if (dot <= -0x40000000)
				dot = (int32_t)0xC0010000;
			vertexlight[negV] = (uint16_t)(dot >> 15);
			if ((int16_t)vertexlight[negV] < 0 &&
				firstvertptr[-1] != (DRAWPOL_FACE_TWOSIDED | DRAWPOL_FACE_GOURAUD | 2))
				vertexlight[negV] = 0;
		}

		/* Compute lighting for posV if not cached */
		if ((int16_t)vertexlight[posV] == -1) {
			PolyVert* norm = &firstvertnorm[posV];
			int32_t dot = rotlightX * norm->x + rotlightY * norm->y + rotlightZ * norm->z;
			if (dot >= 0x40000000)
				dot = 0x3FFF0000;
			if (dot <= -0x40000000)
				dot = (int32_t)0xC0010000;
			vertexlight[posV] = (uint16_t)(dot >> 15);
			if ((int16_t)vertexlight[posV] < 0 &&
				firstvertptr[-1] != (DRAWPOL_FACE_TWOSIDED | DRAWPOL_FACE_GOURAUD | 2))
				vertexlight[posV] = 0;
		}

		/* Interpolate lighting at clip point */
		negLight = (int16_t)vertexlight[negV];
		posLight = (int16_t)vertexlight[posV];
		lightVal = negLight + (int16_t)(((int32_t)(zratio >> 1) * (posLight - negLight)) >> 15);
	}

	dest->light = lightVal;

	/* Project the clipped point (interpolated at z=0) to screen x,
	 * clamping to avoid overflow. */
	eyexsign = 0;
	diff = source2->x - source1->x;
	if (diff < 0) {
		eyexsign = 1;
		diff = -diff;
	}
	interp = math2_longfraction(diff, zratio);
	if (eyexsign)
		interp = -interp;
	val = source1->x + interp;

	limit = 0x7FFFFFFF >> perspShift;

	if (val > limit)
		screen = 0x7FFFFFFF - perspFactor;
	else if (val < -limit)
		screen = perspFactor - 0x7FFFFFFF;
	else
		/* Retail emits `shl eax, cl` (logical bit-shift, sign-agnostic).
		 * C signed left-shift of a negative value is UB even when the
		 * result fits — go through uint32 to match the asm exactly. */
		screen = (int32_t)((uint32_t)val << perspShift);

	dest->xy[0] = halfpixelswide + screen;

	eyeysign = 0;
	diff = source2->y - source1->y;
	if (diff < 0) {
		eyeysign = 1;
		diff = -diff;
	}
	interp = math2_longfraction(diff, zratio);
	if (eyeysign)
		interp = -interp;
	val = source1->y + interp;

	if (yAspect) {
		eyeysign = 0;
		if (val < 0) {
			eyeysign = 1;
			val = -val;
		}
		val = math2_longfraction(val, yAspect);
		if (eyeysign)
			val = -val;
	}

	limit = 0x7FFFFFFF >> perspShift;

	if (val > limit)
		screen = 0x7FFFFFFF - perspFactor;
	else if (val < -limit)
		screen = perspFactor - 0x7FFFFFFF;
	else
		screen = (int32_t)((uint32_t)val << perspShift);

	dest->xy[1] = transfm2_screenyoffset + halfpixelsdeep + screen;

	return dest;
}

/* ================================================================== */

// FUNCTION: TIE95 0x5B41C
TRANSFM2_ScreenPoint* transfm2_calclinepts(const uint8_t* source) {
	TRANSFM2_ScreenPoint* dest;
	int32_t eyez;
	int32_t eyex;
	int32_t* e;

	dest = calcflag[source[2]];
	if (!dest) {
		dest = (TRANSFM2_ScreenPoint*)newscreenxy;
		newscreenxy += 2;
		e = (int32_t*)firsteyexyz + (int16_t)(source[2] * 3);
		eyez = e[2];
		eyex = e[0];
		if (eyez < 0) {
			if (((int32_t*)firsteyexyz + (int16_t)(source[3] * 3))[2] < 0)
				return NULL;
			dest = transfm2_facezintersect(
				source[2], source[3], (DRAWPOL_EyeVertex*)e,
				(DRAWPOL_EyeVertex*)((int32_t*)firsteyexyz + (int16_t)(source[3] * 3)), dest);
		} else {
			dest->xy[0] = transfm2_getscreenx(eyex, eyez);
			calcflag[source[2]] = dest;
			dest->xy[1] = transfm2_getscreeny(((int32_t*)firsteyexyz)[(int16_t)(source[2] * 3) + 1], eyez);
		}
	}

	point1ptr = dest->xy;

	dest = calcflag[source[3]];
	if (dest)
		return dest;

	dest = (TRANSFM2_ScreenPoint*)newscreenxy;
	newscreenxy += 2;
	e = (int32_t*)firsteyexyz + (int16_t)(source[3] * 3);
	eyez = e[2];
	eyex = e[0];
	if (eyez < 0)
		return transfm2_facezintersect(source[3], source[2], (DRAWPOL_EyeVertex*)e,
									   (DRAWPOL_EyeVertex*)((int32_t*)firsteyexyz + (int16_t)(source[2] * 3)),
									   dest);

	dest->xy[0] = transfm2_getscreenx(eyex, eyez);
	calcflag[source[3]] = dest;
	dest->xy[1] = transfm2_getscreeny(((int32_t*)firsteyexyz)[(int16_t)(source[3] * 3) + 1], eyez);
	return dest;
}

/* ================================================================== */

// FUNCTION: TIE95 0x5B578
int16_t transfm2_getfacescreenxy(uint16_t ptCnt) {
	numpoints = ptCnt;
	counter = ptCnt;
	validcnt = 0;
	offrightcnt = 0;
	offscreencnt = -(int8_t)ptCnt;
	offleftcnt = -(int8_t)ptCnt;
	slivercnt = 2 - ptCnt;

	if (transfm2_classifyedges() && (validcnt || offrightcnt))
		return 4;

	if (someznegflag) {
		int16_t i;

		for (i = 0; i < (uint16_t)numpoints; i++) {
			if (!calcflag[firstvertptr[2 * i]])
				return 4;
		}
	}
	return 0;
}

/* ================================================================== */

// FUNCTION: TIE95 0x5B658
int16_t transfm2_classifyedges(void) {
	uint8_t i;

	for (i = 0; i < (uint16_t)numpoints; i++) {
		uint8_t edgeNum;
		uint8_t edgeFlag;
		TRANSFM2_ScreenPoint* pt;
		uint32_t ydiff;
		uint32_t xdiff;

		edgeNum = firstvertptr[2 * i + 1];
		edgeFlag = edgeflags[edgeNum];

		if (edgeFlag == 0x80) {
			offrightcnt++;
			continue;
		}

		/* Pre-classified edge flags: reverse direction and count */
		if (edgeFlag & 1) {
			TRANSFM2_ScreenPoint* tmp;

			edgexsign[edgeNum] = -edgexsign[edgeNum];
			edgeysign[edgeNum] = -edgeysign[edgeNum];
			tmp = edgept2[edgeNum];
			edgept2[edgeNum] = edgept1[edgeNum];
			edgept1[edgeNum] = tmp;
			offrightcnt++;
			if (!++offscreencnt)
				return 0;
			continue;
		}
		if (edgeFlag & 2) {
			TRANSFM2_ScreenPoint* tmp;

			edgexsign[edgeNum] = -edgexsign[edgeNum];
			edgeysign[edgeNum] = -edgeysign[edgeNum];
			tmp = edgept2[edgeNum];
			edgept2[edgeNum] = edgept1[edgeNum];
			edgept1[edgeNum] = tmp;
			if (!++offleftcnt)
				return 0;
			continue;
		}
		if (edgeFlag & 4) {
			TRANSFM2_ScreenPoint* tmp;

			edgexsign[edgeNum] = -edgexsign[edgeNum];
			edgeysign[edgeNum] = -edgeysign[edgeNum];
			tmp = edgept1[edgeNum];
			edgept1[edgeNum] = edgept2[edgeNum];
			edgept2[edgeNum] = tmp;
			if (!++offscreencnt)
				return 0;
			continue;
		}
		if (edgeFlag & 8) {
			TRANSFM2_ScreenPoint* tmp;

			edgexsign[edgeNum] = -edgexsign[edgeNum];
			edgeysign[edgeNum] = -edgeysign[edgeNum];
			tmp = edgept1[edgeNum];
			edgept1[edgeNum] = edgept2[edgeNum];
			edgept2[edgeNum] = tmp;
			validcnt++;
			continue;
		}

		/* New edge: project both vertices. Retail indexes the eye-space
		 * buffer by an 8-bit (vertex * 3) int32 offset. */
		pt = calcflag[firstvertptr[2 * i]];
		if (!pt) {
			int32_t* eye = (int32_t*)firsteyexyz + (uint8_t)(firstvertptr[2 * i] * 3);
			int32_t eyez = eye[2];
			int32_t eyex = eye[0];

			if (eyez < 0) {
				int32_t* other = (int32_t*)firsteyexyz + (uint8_t)(firstvertptr[2 * i + 2] * 3);
				TRANSFM2_ScreenPoint* dest;

				if (other[2] < 0) {
					edgeflags[edgeNum] = 0x80;
					offrightcnt++;
					continue;
				}
				dest = (TRANSFM2_ScreenPoint*)newscreenxy;
				newscreenxy += 2;
				pt = transfm2_facezintersect(firstvertptr[2 * i], firstvertptr[2 * i + 2],
											 (DRAWPOL_EyeVertex*)eye, (DRAWPOL_EyeVertex*)other, dest);
			} else {
				pt = (TRANSFM2_ScreenPoint*)newscreenxy;
				newscreenxy += 2;
				pt->xy[0] = transfm2_getscreenx(eyex, eyez);
				calcflag[firstvertptr[2 * i]] = pt;
				pt->xy[1] = transfm2_getscreeny(
					((int32_t*)firsteyexyz)[(uint8_t)(firstvertptr[2 * i] * 3) + 1], eyez);
			}
		}
		edgept1[edgeNum] = pt;

		pt = calcflag[firstvertptr[2 * i + 2]];
		if (!pt) {
			int32_t* eye = (int32_t*)firsteyexyz + (uint8_t)(firstvertptr[2 * i + 2] * 3);
			int32_t eyez = eye[2];
			int32_t eyex = eye[0];

			if (eyez < 0) {
				int32_t* other = (int32_t*)firsteyexyz + (uint8_t)(firstvertptr[2 * i] * 3);
				TRANSFM2_ScreenPoint* dest;

				if (other[2] < 0) {
					edgeflags[edgeNum] = 0x80;
					offrightcnt++;
					continue;
				}
				dest = (TRANSFM2_ScreenPoint*)newscreenxy;
				newscreenxy += 2;
				pt = transfm2_facezintersect(firstvertptr[2 * i + 2], firstvertptr[2 * i],
											 (DRAWPOL_EyeVertex*)eye, (DRAWPOL_EyeVertex*)other, dest);
			} else {
				pt = (TRANSFM2_ScreenPoint*)newscreenxy;
				newscreenxy += 2;
				pt->xy[0] = transfm2_getscreenx(eyex, eyez);
				calcflag[firstvertptr[2 * i + 2]] = pt;
				pt->xy[1] = transfm2_getscreeny(
					((int32_t*)firsteyexyz)[(uint8_t)(firstvertptr[2 * i + 2] * 3) + 1], eyez);
			}
		}

		/* Classify the new edge */
		edgeflags[edgeNum] = 0;
		edgept2[edgeNum] = pt;
		edgeysign[edgeNum] = 1;

		/* Retail emits `sub` then sign-tests SF and `neg` -- both modular
		 * 32-bit ops. When one endpoint is clamped to ~0x7FFFFFA0 by
		 * transfm2_getscreen[xy] the subtract overflows and the wrapped
		 * sign feeds the bounds classifier below. Use uint32 to match
		 * `sub`/`neg` bit-exactly without signed-overflow UB. */
		ydiff = (uint32_t)pt->xy[1] - (uint32_t)edgept1[edgeNum]->xy[1];
		if ((int32_t)ydiff < 0) {
			edgeysign[edgeNum] = -edgeysign[edgeNum];
			ydiff = -ydiff;
		}
		edgeydiff[edgeNum] = (int32_t)ydiff;

		edgexsign[edgeNum] = 1;
		xdiff = (uint32_t)pt->xy[0] - (uint32_t)edgept1[edgeNum]->xy[0];
		if ((int32_t)xdiff < 0) {
			edgexsign[edgeNum] = -edgexsign[edgeNum];
			xdiff = -xdiff;
		}
		edgexdiff[edgeNum] = (int32_t)xdiff;

		/* Check if edge is off-screen vertically */
		if (edgeysign[edgeNum] < 0) {
			if (edgept1[edgeNum]->xy[1] < 0 || pixelsdeep <= pt->xy[1]) {
				edgeflags[edgeNum] |= 4;
				offscreencnt++;
				continue;
			}
		} else {
			if (pt->xy[1] < 0 || pixelsdeep <= edgept1[edgeNum]->xy[1] || !ydiff) {
				edgeflags[edgeNum] |= 4;
				offscreencnt++;
				continue;
			}
		}

		/* Check horizontal classification */
		if (edgexsign[edgeNum] < 0) {
			if (edgept1[edgeNum]->xy[0] <= 0) {
				edgeflags[edgeNum] |= 2;
				if (!++offleftcnt)
					return 0;
			} else if (pixelswide > pt->xy[0]) {
				edgeflags[edgeNum] |= 8;
				validcnt++;
			} else {
				edgeflags[edgeNum] |= 1;
				if (!++offscreencnt)
					return 0;
				offrightcnt++;
			}
		} else {
			if (pt->xy[0] <= 0) {
				edgeflags[edgeNum] |= 2;
				if (!++offleftcnt)
					return 0;
			} else if (pixelswide > edgept1[edgeNum]->xy[0]) {
				edgeflags[edgeNum] |= 8;
				validcnt++;
			} else {
				edgeflags[edgeNum] |= 1;
				if (!++offscreencnt)
					return 0;
				offrightcnt++;
			}
		}
	}
	return 1;
}
