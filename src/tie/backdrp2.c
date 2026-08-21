/*
 * BACKDRP2 — skybox backdrop renderer.
 *
 * Rebuilds per-frame rotation-matrix lookup tables (rtsvga2 shift*mul,
 * stareye*), then scans backdropposition[] to project and blit up to
 * three visible walls of the skybox cube.
 */

#include <stdint.h>

#include "tie/backdrp2.h"
#include "tie/draw.h"
#include "tie/logbuf2.h"
#include "tie/render_scene_tie98.h"
#include "tie/rtsvga2.h"
#include "tie/tie.h"
#include "tie/transfm2.h"
#include "tie/trig2.h"
#include "tie_runtime/runtime/profile.h"

/* Debug tile-cull counters (file-local). Updated per backdrawbitmap call;
 * read externally via debugger. */
static int g_dbg_bd_tiles_drawn;
static int g_dbg_bd_tiles_culled_x;
static int g_dbg_bd_tiles_culled_y;

/* --- Module-owned globals --- */

uint8_t backdropposition[64];
uint8_t backdropspecies[64];
uint16_t backdropfrontcnt;
uint16_t backdropbackcnt;
uint16_t backdroptopcnt;
uint16_t backdropbottomcnt;
uint16_t backdropleftcnt;
uint16_t backdroprightcnt;

/* --- One wall's tile scan.
 *
 * primary* and secondary* are the two shift*mul tables selected for this
 * wall (see wall-to-axis mapping in backdrp2_backdrop). out_* is the
 * fixed outward offset (worldeye*_{1,2,3} >> 2, negated for the far wall
 * of each pair). */
static void draw_wall(int start, int count, uint16_t angle, const int32_t* prim_x, const int32_t* prim_y,
					  const int32_t* prim_z, const int32_t* sec_x, const int32_t* sec_y, const int32_t* sec_z,
					  int32_t out_x, int32_t out_y, int32_t out_z) {
	int pos = start;
	for (int i = 0; i < count; i++, pos++) {
		uint8_t bits = backdropposition[pos];
		int pi = bits & 0x07;
		int32_t px = prim_x[pi], py = prim_y[pi], pz = prim_z[pi];
		if (bits & 0x08) {
			px = -px;
			py = -py;
			pz = -pz;
		}

		int si = (bits >> 4) & 0x07;
		int32_t ex, ey, ez;
		if (bits & 0x80) {
			ex = px - sec_x[si];
			ey = py - sec_y[si];
			ez = pz - sec_z[si];
		} else {
			ex = px + sec_x[si];
			ey = py + sec_y[si];
			ez = pz + sec_z[si];
		}

		int32_t wx = ex + out_x;
		int32_t wy = ey + out_y;
		int32_t wz = ez + out_z;
		if (wz >= 0)
			backdrp2_backdrawbitmap(wx, wy, wz, angle, pos);
	}
}

// FUNCTION: TIE 0x11C90
void backdrp2_backdrop(void) {
	/* 1) Refresh shift tables:
	 *      shift*Kmul[i] = (i * worldeye*K) >> 5,  i in [0..15]. */
	int32_t a1 = 0, a2 = 0, a3 = 0;
	int32_t b1 = 0, b2 = 0, b3 = 0;
	int32_t c1 = 0, c2 = 0, c3 = 0;
	for (int i = 0; i < 16; i++) {
		shiftA1mul[i] = a1 >> 5;
		shiftA2mul[i] = a2 >> 5;
		shiftA3mul[i] = a3 >> 5;
		shiftB1mul[i] = b1 >> 5;
		shiftB2mul[i] = b2 >> 5;
		shiftB3mul[i] = b3 >> 5;
		shiftC1mul[i] = c1 >> 5;
		shiftC2mul[i] = c2 >> 5;
		shiftC3mul[i] = c3 >> 5;
		a1 += worldeyeA1;
		a2 += worldeyeA2;
		a3 += worldeyeA3;
		b1 += worldeyeB1;
		b2 += worldeyeB2;
		b3 += worldeyeB3;
		c1 += worldeyeC1;
		c2 += worldeyeC2;
		c3 += worldeyeC3;
	}

	/* 2) Refresh 5x5x5 parallax-star grid in eye space:
	 *      stareye[s] = (nx*A + ny*B + nz*C) >> 7,
	 *      nx,ny,nz in [-2..2]. 125 populated slots. */
	int s = 0;
	int32_t ax = -2 * worldeyeA1;
	int32_t ay = -2 * worldeyeA2;
	int32_t az = -2 * worldeyeA3;
	for (int nx = -2; nx <= 2; nx++) {
		int32_t bx = ax + -2 * worldeyeB1;
		int32_t by = ay + -2 * worldeyeB2;
		int32_t bz = az + -2 * worldeyeB3;
		for (int ny = -2; ny <= 2; ny++) {
			int32_t cx = bx + -2 * worldeyeC1;
			int32_t cy = by + -2 * worldeyeC2;
			int32_t cz = bz + -2 * worldeyeC3;
			for (int nz = -2; nz <= 2; nz++) {
				stareyex[s] = cx >> 7;
				stareyey[s] = cy >> 7;
				stareyez[s] = cz >> 7;
				s++;
				cx += worldeyeC1;
				cy += worldeyeC2;
				cz += worldeyeC3;
			}
			bx += worldeyeB1;
			by += worldeyeB2;
			bz += worldeyeB3;
		}
		ax += worldeyeA1;
		ay += worldeyeA2;
		az += worldeyeA3;
	}

	if (!drawbackdropflag)
		return;

	/* front/back: primary=A, secondary=C, outward along world-Y (worldeyeB/4).
	 * Sequence of arctan calls matters — trig2_arctan has global side
	 * effects (trig2_xyangle, trig2_signx/y/z). The binary calls
	 * arctan(A2,A1) both here AND again before the top/bottom pass so
	 * post-return globals reflect world-X orientation. */
	{
		const uint16_t angle = (uint16_t)(-trig2_arctan(worldeyeA2, worldeyeA1));
		const int back_face = (worldeyeB3 < 0);
		const int sign = back_face ? -1 : +1;
		const int count = back_face ? backdropbackcnt : backdropfrontcnt;
		const int start = back_face ? backdropfrontcnt : 0;
		draw_wall(start, count, angle, shiftA1mul, shiftA2mul, shiftA3mul, shiftC1mul, shiftC2mul, shiftC3mul,
				  sign * (worldeyeB1 >> 2), sign * (worldeyeB2 >> 2), sign * (worldeyeB3 >> 2));
	}

	/* left/right: primary=B, secondary=C, outward along world-X (worldeyeA/4) */
	{
		const uint16_t angle = (uint16_t)(-trig2_arctan(worldeyeB2, worldeyeB1));
		const int right_face = (worldeyeA3 < 0);
		const int sign = right_face ? -1 : +1;
		const int count = right_face ? backdroprightcnt : backdropleftcnt;
		const int sides_base = backdropfrontcnt + backdropbackcnt;
		const int start = sides_base + (right_face ? backdropleftcnt : 0);
		draw_wall(start, count, angle, shiftB1mul, shiftB2mul, shiftB3mul, shiftC1mul, shiftC2mul, shiftC3mul,
				  sign * (worldeyeA1 >> 2), sign * (worldeyeA2 >> 2), sign * (worldeyeA3 >> 2));
	}

	/* top/bottom: primary=B, secondary=A, outward along world-Z (worldeyeC/4).
	 * Second arctan(A2,A1) call is intentional — see above. */
	{
		const uint16_t angle = (uint16_t)(-trig2_arctan(worldeyeA2, worldeyeA1));
		const int bottom_face = (worldeyeC3 < 0);
		const int sign = bottom_face ? -1 : +1;
		const int count = bottom_face ? backdropbottomcnt : backdroptopcnt;
		const int caps_base = backdropfrontcnt + backdropbackcnt + backdropleftcnt + backdroprightcnt;
		const int start = caps_base + (bottom_face ? backdroptopcnt : 0);
		draw_wall(start, count, angle, shiftB1mul, shiftB2mul, shiftB3mul, shiftA1mul, shiftA2mul, shiftA3mul,
				  sign * (worldeyeC1 >> 2), sign * (worldeyeC2 >> 2), sign * (worldeyeC3 >> 2));
	}
}

/* Project one axis (x or y) against the eye-space depth z. Returns the
 * signed screen offset, or a sentinel 0x7FFFFF00 when the 64-bit numerator
 * exceeds z (the binary's overflow clamp). Caller has already culled the
 * |n| > z case. Matches the Watcom asm:
 *
 *   num = halfPerspFactor + (|n| << perspShift)   (64-bit unsigned)
 *   if (num >> 32) < z:  quot = num / z   (unsigned)
 *   else:                quot = 0x7FFFFF00
 *   screen = (n >= 0) ? quot : -quot  */
static int32_t project_axis(int32_t n, int32_t z) {
	const int32_t abs_n = (n >= 0) ? n : -n;
	const uint64_t num = (uint32_t)halfPerspFactor + ((uint64_t)(uint32_t)abs_n << (perspShift & 0x1F));
	int32_t q;
	if ((uint32_t)(num >> 32) < (uint32_t)z)
		q = (int32_t)(num / (uint32_t)z);
	else
		q = 0x7FFFFF00;
	return (n >= 0) ? q : -q;
}

// FUNCTION: TIE 0x125D4
void backdrp2_backdrawbitmap(int32_t x, int32_t y, int32_t z, uint16_t angle, int tile_idx) {
	/* Frustum cull (symmetric 90° FOV in the eye XY plane). */
	const int32_t ax = (x >= 0) ? x : -x;
	if (ax > z) {
		g_dbg_bd_tiles_culled_x++;
		return;
	}
	const int32_t ay = (y >= 0) ? y : -y;
	if (ay > z) {
		g_dbg_bd_tiles_culled_y++;
		return;
	}

	const int32_t proj_x = project_axis(x, z);
	const int32_t proj_y = project_axis(y, z);

	const int32_t sx = (int32_t)halfpixelswide + proj_x;
	const int32_t sy = (int32_t)pixelsdeep - ((int32_t)halfpixelsdeep + transfm2_screenyoffset + proj_y);

	g_dbg_bd_tiles_drawn++;
	if (TieProfile_UsesTie98Logic())
		draw_drawbackdropimage_tie98(backdropspecies[tile_idx], (int16_t)sx, (int16_t)sy, angle);
	else
		draw_drawbackdropimage(backdropspecies[tile_idx], (int16_t)sx, (int16_t)sy, angle);
}
