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

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include "tie/math2.h"
#include "tie/transfm2.h"
#include "tie/xtrans2.h"

/* ---- Module-owned globals (from watdbg "static") ---- */

/* Cached per-component multiply tables (16 entries each, indexed by vertex & 0xF) */
static int32_t lastA1mul[16], lastA2mul[16], lastA3mul[16];
static int32_t lastB1mul[16], lastB2mul[16], lastB3mul[16];
static int32_t lastC1mul[16], lastC2mul[16], lastC3mul[16];

/* Rotation matrix (set by FVIEW) */
// GLOBAL: TIE 0xEC174
// GLOBAL: TIE 0xEC178
// GLOBAL: TIE 0xEC17C
int32_t worldeyeA1, worldeyeA2, worldeyeA3;
// GLOBAL: TIE 0xEC18C
// GLOBAL: TIE 0xEC190
// GLOBAL: TIE 0xEC194
int32_t worldeyeB1, worldeyeB2, worldeyeB3;
// GLOBAL: TIE 0xEC180
// GLOBAL: TIE 0xEC184
// GLOBAL: TIE 0xEC188
int32_t worldeyeC1, worldeyeC2, worldeyeC3;
int32_t transfm2_screenyoffset;

/* Working state */
static uint16_t zratio;
static int16_t eyexsign, eyeysign;
static uint8_t offleftcnt, offrightcnt, offscreencnt, validcnt, slivercnt;

#include "tie/drawpol.h" /* authoritative types for DRAWPOL-owned globals:
                         * PolyVert, calcflag[], firsteyexyz, firstvertnorm,
                         * firstvertptr, vertexlight[], newscreenxy,
                         * min/maxscreen*, numpoints, numeyezpos, counter,
                         * samexcnt/sameycnt, rotlightX/Y/Z, objectx/y/z. */
#include "tie/drawln2.h" /* point1ptr (drawln2.c-owned) */
#include "tie/logbuf2.h" /* pixelswide, halfpixelswide, pixelsdeep, halfpixelsdeep */
#include "tie/tie.h"     /* yAspect (watdbg-owned by tie.c) */
#include "tie/trace2.h"  /* someznegflag */

/* ---- Helper: 32×32 fixed-point multiply with 15-bit shift ---- */
static inline int32_t fpmul15(int32_t a, int32_t b) { return (int32_t)(((int64_t)a * b) >> 15); }

/* ================================================================== */

// FUNCTION: TIE 0x59D20
int32_t transfm2_geteyex(int32_t x, int32_t y, int32_t z) {
	return fpmul15(x, worldeyeA1) + fpmul15(y, worldeyeB1) + fpmul15(z, worldeyeC1);
}

// FUNCTION: TIE 0x59D68
int32_t transfm2_geteyey(int32_t x, int32_t y, int32_t z) {
	return fpmul15(x, worldeyeA2) + fpmul15(y, worldeyeB2) + fpmul15(z, worldeyeC2);
}

// FUNCTION: TIE 0x59DB0
int32_t transfm2_geteyez(int32_t x, int32_t y, int32_t z) {
	return fpmul15(x, worldeyeA3) + fpmul15(y, worldeyeB3) + fpmul15(z, worldeyeC3);
}

/* ================================================================== */

// FUNCTION: TIE 0x59CA0
void transfm2_clipobjecteyez(int32_t x, int32_t y, int32_t z) {
	int32_t zneg = -objecteyez;
	if (x <= objecteyex)
		objecteyex -= math2_ABoverC32(-objecteyez, objecteyex - x, z - objecteyez);
	else
		objecteyex += math2_ABoverC32(-objecteyez, x - objecteyex, z - objecteyez);
	if (y <= objecteyey)
		objecteyey -= math2_ABoverC32(zneg, objecteyey - y, zneg + z);
	else
		objecteyey += math2_ABoverC32(zneg, y - objecteyey, zneg + z);
	objecteyez = 1;
}

/* ================================================================== */

/*
 * Batch transform: world vertices → eye coordinates.
 * Source: packed int16 (x, y_hi | z_lo) triples (6 bytes each).
 * Dest: int32 (eyex, eyey, eyez) triples.
 * Uses cached multiply tables for vertices with 0x7F00 reference codes.
 */
// FUNCTION: TIE 0x59DF8
int32_t* transfm2_geteyecoords(const int16_t* source, int32_t* dest) {
	int32_t ptIndex = (uint16_t)numpoints;
	numeyezpos = 0;

	while (ptIndex) {
		int32_t multIndex = ptIndex & 0xF;
		int32_t xCoord = source[0];

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

		int32_t yCoord = source[1];
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

		int32_t zCoord = source[2];
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
		int32_t eyez =
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
// FUNCTION: TIE 0x5A050
int32_t* transfm2_geteyecoordsS2(const int16_t* source, int32_t* dest) {
	/* Scale summed products by 14 bits instead of the standard 16. */
	int32_t ptIndex = (uint16_t)numpoints;
	numeyezpos = 0;

	while (ptIndex) {
		int32_t multIndex = ptIndex & 0xF;
		int32_t xCoord = source[0];

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

		int32_t yCoord = source[1];
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

		int32_t zCoord = source[2];
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
		int32_t eyez = objectz + ((lastA3mul[multIndex] + lastB3mul[multIndex] + lastC3mul[multIndex]) >> 14);
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

/* Z=0 plane transforms: 2D points (x,y), z assumed 0 */
// FUNCTION: TIE 0x5A9E0
int32_t* transfm2_geteyecoordsZ0(const int16_t* source, int32_t* dest) {
	for (int32_t count = (uint16_t)numpoints; count; count--) {
		int32_t x = source[0];
		int32_t y = source[1];
		dest[0] = objectx + ((rotworldeyeA1 * x + rotworldeyeB1 * y + 0x4000) >> 15);
		dest[1] = objecty + ((rotworldeyeA2 * x + rotworldeyeB2 * y + 0x4000) >> 15);
		int32_t coord = objectz + ((rotworldeyeA3 * x + rotworldeyeB3 * y + 0x4000) >> 15);
		if (coord >= 0)
			numeyezpos++;
		dest[2] = coord;
		dest += 3;
		source += 2;
	}
	return dest;
}

// FUNCTION: TIE 0x5AA94
int32_t* transfm2_geteyecoordsZ0s16(const int16_t* source, int32_t* dest) {
	for (int32_t count = (uint16_t)numpoints; count; count--) {
		int32_t x = source[0];
		int32_t y = source[1];
		dest[0] = objectx + 2 * (rotworldeyeA1 * x + rotworldeyeB1 * y + 0x4000);
		dest[1] = objecty + 2 * (rotworldeyeA2 * x + rotworldeyeB2 * y + 0x4000);
		int32_t coord = objectz + 2 * (rotworldeyeA3 * x + rotworldeyeB3 * y + 0x4000);
		if (coord >= 0)
			numeyezpos++;
		dest[2] = coord;
		dest += 3;
		source += 2;
	}
	return dest;
}

// FUNCTION: TIE 0x5AB44
int32_t* transfm2_geteyecoordsZ0s8(const int16_t* source, int32_t* dest) {
	for (int32_t count = (uint16_t)numpoints; count; count--) {
		int32_t x = source[0];
		int32_t y = source[1];
		dest[0] = objectx + ((rotworldeyeA1 * x + rotworldeyeB1 * y + 0x4000) >> 7);
		dest[1] = objecty + ((rotworldeyeA2 * x + rotworldeyeB2 * y + 0x4000) >> 7);
		int32_t coord = objectz + ((rotworldeyeA3 * x + rotworldeyeB3 * y + 0x4000) >> 7);
		if (coord >= 0)
			numeyezpos++;
		dest[2] = coord;
		dest += 3;
		source += 2;
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
// FUNCTION: TIE 0x5ACC4
int32_t transfm2_getscreenx(int32_t eyex, int32_t eyez) {
	bool neg = (eyex < 0);
	uint32_t mag = neg ? -(uint32_t)eyex : (uint32_t)eyex;

	uint64_t prod = (uint32_t)halfPerspFactor + ((uint64_t)mag << (perspShift & 0x1F));
	uint32_t result = ((prod >> 32) >= (uint32_t)eyez) ? 0x7FFFFF00u : (uint32_t)(prod / (uint32_t)eyez);
	if (neg)
		result = -result;
	return (int32_t)(halfpixelswide + result);
}

// FUNCTION: TIE 0x5AD60
int32_t transfm2_getscreeny(int32_t eyey, int32_t eyez) {
	bool neg = (eyey < 0);
	uint32_t mag = neg ? -(uint32_t)eyey : (uint32_t)eyey;

	uint64_t prod = (uint32_t)halfPerspFactor + ((uint64_t)mag << (perspShift & 0x1F));
	uint32_t result = ((prod >> 32) >= (uint32_t)eyez) ? 0x7FFFFF00u : (uint32_t)(prod / (uint32_t)eyez);
	if (neg)
		result = -result;

	if (yAspect) {
		int32_t r = (int32_t)result;
		r = (r >= 0) ? math2_longfraction(r, yAspect) : -math2_longfraction(-r, yAspect);
		result = (uint32_t)r;
	}
	return (int32_t)((uint32_t)transfm2_screenyoffset + halfpixelsdeep + result);
}

int32_t transfm2_getscreencoordx(int32_t eyex, int32_t eyez) { return transfm2_getscreenx(eyex, eyez); }

int32_t transfm2_getscreencoordy(int32_t eyey, int32_t eyez) { return transfm2_getscreeny(eyey, eyez); }

// FUNCTION: TIE 0x5AD28
void transfm2_doxminmax(int32_t screenx, int32_t* ptr) {
	if (*minscreenx >= screenx) {
		if (*minscreenx <= screenx)
			samexcnt++;
		else
			minscreenx = ptr;
	} else if (*maxscreenx < screenx) {
		maxscreenx = ptr;
	}
}

// FUNCTION: TIE 0x5ADF8
void transfm2_doyminmax(int32_t screeny, int32_t* ptr) {
	if (minscreeny[1] >= screeny) {
		if (minscreeny[1] <= screeny)
			sameycnt++;
		else
			minscreeny = ptr;
	} else if (maxscreeny[1] < screeny) {
		maxscreeny = ptr;
	}
}

/* ================================================================== */

/* Batch screen projection with z-clipping */
// FUNCTION: TIE 0x5ABF8
int32_t* transfm2_getscreencoords(int32_t* source, int32_t* dest) {
	while (counter) {
		int32_t eyez = source[2];
		if (eyez >= 0) {
			int32_t sx = transfm2_getscreenx(source[0], eyez);
			dest[0] = sx;
			transfm2_doxminmax(sx, dest);
			int32_t sy = transfm2_getscreeny(source[1], eyez);
			dest[1] = sy;
			transfm2_doyminmax(sy, dest);
			dest += 2;
		} else {
			dest = transfm2_clipeyez(source, dest);
		}
		source += 3;
		counter--;
	}
	return dest;
}

/* ================================================================== */

/* Z-plane clipping helpers */

/*
 * Compute z-ratio for linear interpolation between two vertices
 * straddling the z=0 plane.
 */
static void compute_zratio(int32_t z1, int32_t z2) {
	int32_t zneg = -z1;
	int32_t ztotal = z2 - z1;
	while (zneg & 0xFFFF0000) {
		zneg >>= 1;
		ztotal >>= 1;
	}
	zratio = (uint16_t)((zneg << 16) / ztotal);
}

/*
 * Project a clipped point (interpolated at z=0) to screen coords.
 * Clamps to avoid overflow.
 */
static void project_clipped_x(const int32_t* source1, const int32_t* source2, int32_t* out) {
	eyexsign = 0;
	int32_t diff = *source2 - *source1;
	if (diff < 0) {
		eyexsign = 1;
		diff = -diff;
	}
	int32_t interp = math2_longfraction(diff, zratio);
	if (eyexsign)
		interp = -interp;
	int32_t val = *source1 + interp;

	int32_t limit = 0x7FFFFFFF >> perspShift;
	int32_t screen;
	if (val > limit)
		screen = 0x7FFFFFFF - perspFactor;
	else if (val < -limit)
		screen = perspFactor - 0x7FFFFFFF;
	else
		/* Retail emits `shl eax, cl` (logical bit-shift, sign-agnostic).
		 * C signed left-shift of a negative value is UB even when the
		 * result fits — go through uint32 to match the asm exactly. */
		screen = (int32_t)((uint32_t)val << perspShift);

	*out = halfpixelswide + screen;
}

static void project_clipped_y(const int32_t* source1, const int32_t* source2, int32_t* out) {
	eyeysign = 0;
	int32_t diff = source2[1] - source1[1];
	if (diff < 0) {
		eyeysign = 1;
		diff = -diff;
	}
	int32_t interp = math2_longfraction(diff, zratio);
	if (eyeysign)
		interp = -interp;
	int32_t val = source1[1] + interp;

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

	int32_t limit = 0x7FFFFFFF >> perspShift;
	int32_t screen;
	if (val > limit)
		screen = 0x7FFFFFFF - perspFactor;
	else if (val < -limit)
		screen = perspFactor - 0x7FFFFFFF;
	else
		/* See project_clipped_x — same `shl` UB workaround. */
		screen = (int32_t)((uint32_t)val << perspShift);

	*out = transfm2_screenyoffset + halfpixelsdeep + screen;
}

// FUNCTION: TIE 0x5AE84
int32_t* transfm2_calczintersect(int32_t* source1, int32_t* source2, int32_t* dest) {
	compute_zratio(source1[2], source2[2]);

	project_clipped_x(source1, source2, &dest[0]);
	transfm2_doxminmax(dest[0], dest);

	project_clipped_y(source1, source2, &dest[1]);
	transfm2_doyminmax(dest[1], dest);

	return dest + 2;
}

// FUNCTION: TIE 0x5AE30
int32_t* transfm2_clipeyez(int32_t* source, int32_t* dest) {
	int32_t* result = dest;
	numpoints--;

	/* Check previous vertex (source - 3) */
	if (source[-1] >= 0) {
		numpoints++;
		result = transfm2_calczintersect(source, source - 3, dest);
	}

	/* Check next vertex (source + 3) */
	if (source[5] >= 0) {
		numpoints++;
		return transfm2_calczintersect(source, source + 3, result);
	}

	return result;
}

/* ================================================================== */

/*
 * Z=0 intersection for face edges with vertex lighting interpolation.
 * This is the most complex function — handles per-vertex normal-based
 * lighting computation and interpolation at the clip point.
 */
// FUNCTION: TIE 0x5B090
int32_t* transfm2_facezintersect(int16_t negV, int16_t posV, int32_t* source1, int32_t* source2,
								 int32_t* dest) {
	compute_zratio(source1[2], source2[2]);

	/* Vertex lighting (if face has lighting flag 0x40) */
	int16_t lightVal = 0;
	if (*(firstvertptr - 1) & 0x40) {
		/* Compute lighting for negV if not cached */
		if (*(int16_t*)&vertexlight[2 * negV] == -1) {
			PolyVert* norm = &firstvertnorm[negV];
			int32_t dot = rotlightX * norm->x + rotlightY * norm->y + rotlightZ * norm->z;
			if (dot >= 0x40000000)
				dot = 0x3FFF0000;
			if (dot <= -0x40000000)
				dot = (int32_t)0xC0010000;
			*(int16_t*)&vertexlight[2 * negV] = (int16_t)(dot >> 15);
			if (*(int16_t*)&vertexlight[2 * negV] < 0 && *(firstvertptr - 1) != 0xC2)
				*(int16_t*)&vertexlight[2 * negV] = 0;
		}

		/* Compute lighting for posV if not cached */
		if (*(int16_t*)&vertexlight[2 * posV] == -1) {
			PolyVert* norm = &firstvertnorm[posV];
			int32_t dot = rotlightX * norm->x + rotlightY * norm->y + rotlightZ * norm->z;
			if (dot >= 0x40000000)
				dot = 0x3FFF0000;
			if (dot <= -0x40000000)
				dot = (int32_t)0xC0010000;
			*(int16_t*)&vertexlight[2 * posV] = (int16_t)(dot >> 15);
			if (*(int16_t*)&vertexlight[2 * posV] < 0 && *(firstvertptr - 1) != 0xC2)
				*(int16_t*)&vertexlight[2 * posV] = 0;
		}

		/* Interpolate lighting at clip point */
		int16_t negLight = *(int16_t*)&vertexlight[2 * negV];
		int16_t posLight = *(int16_t*)&vertexlight[2 * posV];
		lightVal = negLight + (int16_t)(((int32_t)(zratio >> 1) * (posLight - negLight)) >> 15);
	}

	/* Write interpolated light value */
	*(int16_t*)dest = lightVal;
	dest = (int32_t*)((char*)dest + 2);
	newscreenxy = (int32_t*)((char*)newscreenxy + 2);

	/* Project clipped x */
	project_clipped_x(source1, source2, &dest[0]);

	/* Project clipped y */
	project_clipped_y(source1, source2, &dest[1]);

	return dest;
}

/* ================================================================== */

// FUNCTION: TIE 0x5B41C
int32_t* transfm2_calclinepts(const uint8_t* source) {
	uint8_t idx1 = source[2];
	int32_t* dest = calcflag[idx1];

	if (!dest) {
		dest = newscreenxy;
		newscreenxy += 2;
		int eyez = firsteyexyz[idx1].z;

		if (eyez >= 0) {
			dest[0] = transfm2_getscreenx(firsteyexyz[idx1].x, eyez);
			calcflag[idx1] = dest;
			dest[1] = transfm2_getscreeny(firsteyexyz[idx1].y, eyez);
		} else {
			uint8_t idx2 = source[3];
			if (firsteyexyz[idx2].z < 0)
				return NULL;
			dest = transfm2_facezintersect(idx1, idx2, (int32_t*)&firsteyexyz[idx1],
										   (int32_t*)&firsteyexyz[idx2], dest);
		}
	}

	point1ptr = dest;

	uint8_t idx2 = source[3];
	if (calcflag[idx2])
		return calcflag[idx2];

	int32_t* dest2 = newscreenxy;
	newscreenxy += 2;
	int eyez2 = firsteyexyz[idx2].z;

	if (eyez2 < 0) {
		return transfm2_facezintersect(idx2, idx1, (int32_t*)&firsteyexyz[idx2],
									   (int32_t*)&firsteyexyz[source[2]], dest2);
	}

	dest2[0] = transfm2_getscreenx(firsteyexyz[idx2].x, eyez2);
	calcflag[idx2] = dest2;
	dest2[1] = transfm2_getscreeny(firsteyexyz[idx2].y, eyez2);
	return dest2;
}

/* ================================================================== */

// FUNCTION: TIE 0x5B578
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
		for (int32_t i = 0; i < (uint16_t)numpoints; i++) {
			if (!calcflag[firstvertptr[2 * i]])
				return 4;
		}
	}
	return 0;
}

/* ================================================================== */

// FUNCTION: TIE 0x5B658
int16_t transfm2_classifyedges(void) {
	for (uint8_t i = 0;; i++) {
		if (i >= (uint16_t)numpoints)
			return 1;

		uint8_t edgeNum = firstvertptr[2 * i + 1];
		uint8_t edgeFlag = edgeflags[edgeNum];

		if (edgeFlag == 0x80) {
			offrightcnt++;
			continue;
		}

		/* Pre-classified edge flags: reverse direction and count */
		if (edgeFlag & 1) {
			offrightcnt++;
			edgexsign[edgeNum] = -edgexsign[edgeNum];
			edgeysign[edgeNum] = -edgeysign[edgeNum];
			int32_t* tmp = edgept2[edgeNum];
			edgept2[edgeNum] = edgept1[edgeNum];
			edgept1[edgeNum] = tmp;
			if (!++offscreencnt)
				return 0;
			continue;
		}
		if (edgeFlag & 2) {
			edgexsign[edgeNum] = -edgexsign[edgeNum];
			edgeysign[edgeNum] = -edgeysign[edgeNum];
			int32_t* tmp = edgept2[edgeNum];
			edgept2[edgeNum] = edgept1[edgeNum];
			edgept1[edgeNum] = tmp;
			if (!++offleftcnt)
				return 0;
			continue;
		}
		if (edgeFlag & 4) {
			edgexsign[edgeNum] = -edgexsign[edgeNum];
			edgeysign[edgeNum] = -edgeysign[edgeNum];
			int32_t* tmp = edgept2[edgeNum];
			edgept2[edgeNum] = edgept1[edgeNum];
			edgept1[edgeNum] = tmp;
			if (!++offscreencnt)
				return 0;
			continue;
		}
		if (edgeFlag & 8) {
			edgexsign[edgeNum] = -edgexsign[edgeNum];
			edgeysign[edgeNum] = -edgeysign[edgeNum];
			int32_t* tmp = edgept2[edgeNum];
			edgept2[edgeNum] = edgept1[edgeNum];
			edgept1[edgeNum] = tmp;
			validcnt++;
			continue;
		}

		/* New edge: project both vertices */
		uint8_t idx = firstvertptr[2 * i];
		int32_t* pt1 = calcflag[idx];

		if (!pt1) {
			int32_t eyex = firsteyexyz[idx].x;
			int32_t eyez = firsteyexyz[idx].z;

			if (eyez >= 0) {
				pt1 = newscreenxy;
				newscreenxy += 2;
				pt1[0] = transfm2_getscreenx(eyex, eyez);
				calcflag[idx] = pt1;
				pt1[1] = transfm2_getscreeny(firsteyexyz[idx].y, eyez);
			} else {
				uint8_t nextIdx = firstvertptr[2 * i + 2];
				if (firsteyexyz[nextIdx].z < 0) {
					edgeflags[edgeNum] = 0x80;
					offrightcnt++;
					continue;
				}
				int32_t* tmp = newscreenxy;
				newscreenxy += 2;
				pt1 = transfm2_facezintersect(idx, nextIdx, (int32_t*)&firsteyexyz[idx],
											  (int32_t*)&firsteyexyz[nextIdx], tmp);
			}
		}

		edgept1[edgeNum] = pt1;

		uint8_t idx2 = firstvertptr[2 * i + 2];
		int32_t* pt2 = calcflag[idx2];

		if (!pt2) {
			int32_t eyex2 = firsteyexyz[idx2].x;
			int32_t eyez2 = firsteyexyz[idx2].z;

			if (eyez2 >= 0) {
				pt2 = newscreenxy;
				newscreenxy += 2;
				pt2[0] = transfm2_getscreenx(eyex2, eyez2);
				calcflag[idx2] = pt2;
				pt2[1] = transfm2_getscreeny(firsteyexyz[idx2].y, eyez2);
			} else {
				uint8_t prevIdx = firstvertptr[2 * i];
				if (firsteyexyz[prevIdx].z < 0) {
					edgeflags[edgeNum] = 0x80;
					offrightcnt++;
					continue;
				}
				int32_t* tmp = newscreenxy;
				newscreenxy += 2;
				pt2 = transfm2_facezintersect(idx2, prevIdx, (int32_t*)&firsteyexyz[idx2],
											  (int32_t*)&firsteyexyz[prevIdx], tmp);
			}
		}

		/* Classify the new edge */
		edgeflags[edgeNum] = 0;
		edgept2[edgeNum] = pt2;
		int32_t* ptPtr = edgept1[edgeNum];

		/* Retail emits `sub esi, [edx]` then sign-tests SF and `neg esi`
		 * — both modular 32-bit ops. C signed `-` is UB on overflow. When
		 * one endpoint is clamped to ~0x7FFFFFA0 by transfm2_getscreen[xy]
		 * and the other is a normal in-eye-space coord, the subtract
		 * overflows. The wrap is part of retail's observable behavior
		 * (the resulting [xy]diff carries a flipped sign vs. the
		 * mathematical value, which feeds the X/Y-bounds classifier
		 * below). Use uint32 to match `sub`/`neg` bit-exactly. */
		edgeysign[edgeNum] = 1;
		uint32_t ydiff = (uint32_t)pt2[1] - (uint32_t)ptPtr[1];
		if ((int32_t)ydiff < 0) {
			edgeysign[edgeNum] = -edgeysign[edgeNum];
			ydiff = -ydiff;
		}
		edgeydiff[edgeNum] = (int32_t)ydiff;

		edgexsign[edgeNum] = 1;
		uint32_t xdiff = (uint32_t)pt2[0] - (uint32_t)ptPtr[0];
		if ((int32_t)xdiff < 0) {
			edgexsign[edgeNum] = -edgexsign[edgeNum];
			xdiff = -xdiff;
		}
		edgexdiff[edgeNum] = (int32_t)xdiff;

		/* Check if edge is off-screen vertically */
		if (edgeysign[edgeNum] >= 0) {
			if (pt2[1] < 0 || pixelsdeep <= ptPtr[1] || !ydiff) {
				edgeflags[edgeNum] |= 4;
				offscreencnt++;
				continue;
			}
		} else {
			if (ptPtr[1] < 0 || pixelsdeep <= pt2[1]) {
				edgeflags[edgeNum] |= 4;
				offscreencnt++;
				continue;
			}
		}

		/* Check horizontal classification */
		if (edgexsign[edgeNum] < 0) {
			if (ptPtr[0] > 0) {
				if (pixelswide <= pt2[0]) {
					edgeflags[edgeNum] |= 1;
					if (!++offscreencnt)
						return 0;
					offrightcnt++;
				} else {
					edgeflags[edgeNum] |= 8;
					validcnt++;
				}
			} else {
				edgeflags[edgeNum] |= 2;
				if (!++offleftcnt)
					return 0;
			}
		} else {
			if (pt2[0] > 0) {
				if (pixelswide <= ptPtr[0]) {
					edgeflags[edgeNum] |= 1;
					if (!++offscreencnt)
						return 0;
					offrightcnt++;
				} else {
					edgeflags[edgeNum] |= 8;
					validcnt++;
				}
			} else {
				edgeflags[edgeNum] |= 2;
				if (!++offleftcnt)
					return 0;
			}
		}
	}
}

/* ================================================================== */

/*
 * Helper: compute min/max of (m * v1) and (m * v2), accumulate into
 * running min/max sums.
 */
static void minmax_axis(int32_t m, int32_t v1, int32_t v2, int32_t* acc_min, int32_t* acc_max) {
	int32_t a = m * v1;
	int32_t b = m * v2;
	if (b < a) {
		*acc_min += b;
		*acc_max += a;
	} else {
		*acc_min += a;
		*acc_max += b;
	}
}

// FUNCTION: TIE 0x5A2A8
void transfm2_geteyeminmax(const int16_t* source, int32_t* dest) {
	int32_t x1 = source[0];
	int32_t y1 = source[1];
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
	minmax_axis(rotworldeyeB1, y1, y2, &mn, &mx);
	minmax_axis(rotworldeyeC1, z1, z2, &mn, &mx);
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
	minmax_axis(rotworldeyeB2, y1, y2, &mn, &mx);
	minmax_axis(rotworldeyeC2, z1, z2, &mn, &mx);
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
	minmax_axis(rotworldeyeB3, y1, y2, &mn, &mx);
	minmax_axis(rotworldeyeC3, z1, z2, &mn, &mx);
	dest[4] = objectz + (int16_t)((uint32_t)mn >> 16);
	dest[5] = objectz + (int16_t)((uint32_t)mx >> 16);
}

// FUNCTION: TIE 0x5A458
void transfm2_geteyeminmaxS2(const int16_t* source, int32_t* dest) {
	int32_t x1 = source[0];
	int32_t y1 = source[1];
	int32_t z1 = source[2];
	int32_t x2 = source[3];
	int32_t y2 = source[4];
	int32_t z2 = source[5];

	int32_t mn, mx, a, b;

	a = rotworldeyeA1 * x1;
	b = rotworldeyeA1 * x2;
	if (b < a) {
		mn = b;
		mx = a;
	} else {
		mn = a;
		mx = b;
	}
	minmax_axis(rotworldeyeB1, y1, y2, &mn, &mx);
	minmax_axis(rotworldeyeC1, z1, z2, &mn, &mx);
	dest[0] = objectx + (mn >> 14);
	dest[1] = objectx + (mx >> 14);

	a = rotworldeyeA2 * x1;
	b = rotworldeyeA2 * x2;
	if (b < a) {
		mn = b;
		mx = a;
	} else {
		mn = a;
		mx = b;
	}
	minmax_axis(rotworldeyeB2, y1, y2, &mn, &mx);
	minmax_axis(rotworldeyeC2, z1, z2, &mn, &mx);
	dest[2] = objecty + (mn >> 14);
	dest[3] = objecty + (mx >> 14);

	a = rotworldeyeA3 * x1;
	b = rotworldeyeA3 * x2;
	if (b < a) {
		mn = b;
		mx = a;
	} else {
		mn = a;
		mx = b;
	}
	minmax_axis(rotworldeyeB3, y1, y2, &mn, &mx);
	minmax_axis(rotworldeyeC3, z1, z2, &mn, &mx);
	dest[4] = objectz + (mn >> 14);
	dest[5] = objectz + (mx >> 14);
}

// FUNCTION: TIE 0x5A608
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
	minmax_axis(craftf1, y1, y2, &mn, &mx);
	minmax_axis(craftU1, z1, z2, &mn, &mx);
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
	minmax_axis(craftf2, y1, y2, &mn, &mx);
	minmax_axis(craftU2, z1, z2, &mn, &mx);
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
	minmax_axis(craftf3, y1, y2, &mn, &mx);
	minmax_axis(craftU3, z1, z2, &mn, &mx);
	dest[2] += (int16_t)((mn + 0x100000) >> 21);
	dest[5] += (int16_t)((mx - 0x100000) >> 21);
}

// FUNCTION: TIE 0x5A7F4
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
	minmax_axis(craftf1, y1, y2, &mn, &mx);
	minmax_axis(craftU1, z1, z2, &mn, &mx);
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
	minmax_axis(craftf2, y1, y2, &mn, &mx);
	minmax_axis(craftU2, z1, z2, &mn, &mx);
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
	minmax_axis(craftf3, y1, y2, &mn, &mx);
	minmax_axis(craftU3, z1, z2, &mn, &mx);
	dest[2] += (int16_t)((mn + 0x40000) >> 19);
	dest[5] += (int16_t)((mx - 0x40000) >> 19);
}
