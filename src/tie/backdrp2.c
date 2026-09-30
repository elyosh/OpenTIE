/*
 * BACKDRP2 — skybox backdrop renderer.
 *
 * Rebuilds per-frame rotation-matrix lookup tables (rtsvga2 shift*mul,
 * stareye*), then scans backdropposition[] to project and blit up to
 * three visible walls of the skybox cube.
 */

#include "tie/backdrp2.h"
#include "tie/draw.h"
#include "tie/logbuf2.h"
#include "tie/math2_wide.h"
#include "tie/render_scene_tie98.h"
#include "tie/rtsvga2.h"
#include "tie/tie.h"
#include "tie/transfm2.h"
#include "tie/trig2.h"
#include "tie_runtime/runtime/profile.h"

#include <stdint.h>

/* --- Module-owned globals --- */

// GLOBAL: TIE95 0xC16F4
// GLOBAL: TIE98 0x4FA4C0
uint8_t backdropspecies[64];
// GLOBAL: TIE95 0xC1734
// GLOBAL: TIE98 0x4FA500
uint8_t backdropposition[64];
// GLOBAL: TIE95 0xC1774
// GLOBAL: TIE98 0x4FA540
uint16_t backdropfrontcnt;
// GLOBAL: TIE95 0xC1776
// GLOBAL: TIE98 0x4FA544
uint16_t backdropbackcnt;
// GLOBAL: TIE95 0xC1778
// GLOBAL: TIE98 0x4FA548
uint16_t backdroptopcnt;
// GLOBAL: TIE95 0xC177A
// GLOBAL: TIE98 0x4FA54C
uint16_t backdropbottomcnt;
// GLOBAL: TIE95 0xC177C
// GLOBAL: TIE98 0x4FA550
uint16_t backdropleftcnt;
// GLOBAL: TIE95 0xC177E
// GLOBAL: TIE98 0x4FA554
uint16_t backdroprightcnt;

// FUNCTION: TIE95 0x11C90
// FUNCTION: TIE98 0x4034B0
void backdrp2_backdrop(void) {
	/* 1) Refresh shift tables:
	 *      shift*Kmul[i] = (i * worldeye*K) >> 5,  i in [0..15]. */
	int32_t a1 = 0, a2 = 0, a3 = 0;
	int32_t b1 = 0, b2 = 0, b3 = 0;
	int32_t c1 = 0, c2 = 0, c3 = 0;
	int i;
	int s;
	int32_t ax;
	int32_t ay;
	int32_t az;
	int nx;
	int32_t x;
	int32_t y;
	int32_t z;
	uint16_t count;
	uint16_t index;
	uint16_t angle;
	uint8_t position;

	for (i = 0; i < 16; i++) {
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
	s = 0;
	ax = -2 * worldeyeA1;
	ay = -2 * worldeyeA2;
	az = -2 * worldeyeA3;
	for (nx = -2; nx <= 2; nx++) {
		int32_t bx = ax + -2 * worldeyeB1;
		int32_t by = ay + -2 * worldeyeB2;
		int32_t bz = az + -2 * worldeyeB3;
		int ny;

		for (ny = -2; ny <= 2; ny++) {
			int32_t cx = bx + -2 * worldeyeC1;
			int32_t cy = by + -2 * worldeyeC2;
			int32_t cz = bz + -2 * worldeyeC3;
			int nz;

			for (nz = -2; nz <= 2; nz++) {
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

	if (drawbackdropflag) {
		/* Walls are stored front, back, left, right, top, bottom; only the
		 * wall of each opposing pair that faces the eye is scanned. */
		index = 0;
		angle = (uint16_t)-trig2_arctan(worldeyeA2, worldeyeA1);
		if (worldeyeB3 < 0) {
			index += backdropfrontcnt;
			count = backdropbackcnt;
			while (count--) {
				position = backdropposition[index++];
				x = shiftA1mul[position & 7];
				y = shiftA2mul[position & 7];
				z = shiftA3mul[position & 7];
				if (position & 0x08) {
					x = -x;
					y = -y;
					z = -z;
				}
				if (position & 0x80) {
					x -= shiftC1mul[(position >> 4) & 7];
					y -= shiftC2mul[(position >> 4) & 7];
					z -= shiftC3mul[(position >> 4) & 7];
				} else {
					x += shiftC1mul[(position >> 4) & 7];
					y += shiftC2mul[(position >> 4) & 7];
					z += shiftC3mul[(position >> 4) & 7];
				}
				x -= worldeyeB1 >> 2;
				y -= worldeyeB2 >> 2;
				z -= worldeyeB3 >> 2;
				if (z >= 0)
					backdrp2_backdrawbitmap(x, y, z, angle, index);
			}
		} else {
			count = backdropfrontcnt;
			while (count--) {
				position = backdropposition[index++];
				x = shiftA1mul[position & 7];
				y = shiftA2mul[position & 7];
				z = shiftA3mul[position & 7];
				if (position & 0x08) {
					x = -x;
					y = -y;
					z = -z;
				}
				if (position & 0x80) {
					x -= shiftC1mul[(position >> 4) & 7];
					y -= shiftC2mul[(position >> 4) & 7];
					z -= shiftC3mul[(position >> 4) & 7];
				} else {
					x += shiftC1mul[(position >> 4) & 7];
					y += shiftC2mul[(position >> 4) & 7];
					z += shiftC3mul[(position >> 4) & 7];
				}
				x += worldeyeB1 >> 2;
				y += worldeyeB2 >> 2;
				z += worldeyeB3 >> 2;
				if (z >= 0)
					backdrp2_backdrawbitmap(x, y, z, angle, index);
			}
			index += backdropbackcnt;
		}
		angle = (uint16_t)-trig2_arctan(worldeyeB2, worldeyeB1);
		if (worldeyeA3 < 0) {
			index += backdropleftcnt;
			count = backdroprightcnt;
			while (count--) {
				position = backdropposition[index++];
				x = shiftB1mul[position & 7];
				y = shiftB2mul[position & 7];
				z = shiftB3mul[position & 7];
				if (position & 0x08) {
					x = -x;
					y = -y;
					z = -z;
				}
				if (position & 0x80) {
					x -= shiftC1mul[(position >> 4) & 7];
					y -= shiftC2mul[(position >> 4) & 7];
					z -= shiftC3mul[(position >> 4) & 7];
				} else {
					x += shiftC1mul[(position >> 4) & 7];
					y += shiftC2mul[(position >> 4) & 7];
					z += shiftC3mul[(position >> 4) & 7];
				}
				x -= worldeyeA1 >> 2;
				y -= worldeyeA2 >> 2;
				z -= worldeyeA3 >> 2;
				if (z >= 0)
					backdrp2_backdrawbitmap(x, y, z, angle, index);
			}
		} else {
			count = backdropleftcnt;
			while (count--) {
				position = backdropposition[index++];
				x = shiftB1mul[position & 7];
				y = shiftB2mul[position & 7];
				z = shiftB3mul[position & 7];
				if (position & 0x08) {
					x = -x;
					y = -y;
					z = -z;
				}
				if (position & 0x80) {
					x -= shiftC1mul[(position >> 4) & 7];
					y -= shiftC2mul[(position >> 4) & 7];
					z -= shiftC3mul[(position >> 4) & 7];
				} else {
					x += shiftC1mul[(position >> 4) & 7];
					y += shiftC2mul[(position >> 4) & 7];
					z += shiftC3mul[(position >> 4) & 7];
				}
				x += worldeyeA1 >> 2;
				y += worldeyeA2 >> 2;
				z += worldeyeA3 >> 2;
				if (z >= 0)
					backdrp2_backdrawbitmap(x, y, z, angle, index);
			}
			index += backdroprightcnt;
		}
		angle = (uint16_t)-trig2_arctan(worldeyeA2, worldeyeA1);
		if (worldeyeC3 < 0) {
			index += backdroptopcnt;
			count = backdropbottomcnt;
			while (count--) {
				position = backdropposition[index++];
				x = shiftB1mul[position & 7];
				y = shiftB2mul[position & 7];
				z = shiftB3mul[position & 7];
				if (position & 0x08) {
					x = -x;
					y = -y;
					z = -z;
				}
				if (position & 0x80) {
					x -= shiftA1mul[(position >> 4) & 7];
					y -= shiftA2mul[(position >> 4) & 7];
					z -= shiftA3mul[(position >> 4) & 7];
				} else {
					x += shiftA1mul[(position >> 4) & 7];
					y += shiftA2mul[(position >> 4) & 7];
					z += shiftA3mul[(position >> 4) & 7];
				}
				x -= worldeyeC1 >> 2;
				y -= worldeyeC2 >> 2;
				z -= worldeyeC3 >> 2;
				if (z >= 0)
					backdrp2_backdrawbitmap(x, y, z, angle, index);
			}
		} else {
			count = backdroptopcnt;
			while (count--) {
				position = backdropposition[index++];
				x = shiftB1mul[position & 7];
				y = shiftB2mul[position & 7];
				z = shiftB3mul[position & 7];
				if (position & 0x08) {
					x = -x;
					y = -y;
					z = -z;
				}
				if (position & 0x80) {
					x -= shiftA1mul[(position >> 4) & 7];
					y -= shiftA2mul[(position >> 4) & 7];
					z -= shiftA3mul[(position >> 4) & 7];
				} else {
					x += shiftA1mul[(position >> 4) & 7];
					y += shiftA2mul[(position >> 4) & 7];
					z += shiftA3mul[(position >> 4) & 7];
				}
				x += worldeyeC1 >> 2;
				y += worldeyeC2 >> 2;
				z += worldeyeC3 >> 2;
				if (z >= 0)
					backdrp2_backdrawbitmap(x, y, z, angle, index);
			}
		}
	}
}

/* Project one backdrop tile and draw its species image. tile_idx has
 * already been advanced past the tile's backdropposition[] entry. Each axis
 * projects (|n| << perspShift) + halfPerspFactor over z, with 0x7FFFFF00 as
 * the overflow result. */
// FUNCTION: TIE95 0x125D4
// FUNCTION: TIE98 0x403D20
void backdrp2_backdrawbitmap(int32_t x, int32_t y, int32_t z, uint16_t angle, int tile_idx) {
	int32_t screenx;
	int32_t screeny;

	if (x >= 0) {
		if (x > z)
			return;
		screenx = (int32_t)math2_project_u32((uint32_t)x, perspShift, halfPerspFactor, (uint32_t)z);
	} else {
		if (-x > z)
			return;
		screenx = -(int32_t)math2_project_u32((uint32_t)-x, perspShift, halfPerspFactor, (uint32_t)z);
	}
	if (y >= 0) {
		if (y > z)
			return;
		screeny = (int32_t)math2_project_u32((uint32_t)y, perspShift, halfPerspFactor, (uint32_t)z);
	} else {
		if (-y > z)
			return;
		screeny = -(int32_t)math2_project_u32((uint32_t)-y, perspShift, halfPerspFactor, (uint32_t)z);
	}

	if (TieProfile_UsesTie98Logic())
		draw_drawbackdropimage_tie98(
			backdropspecies[tile_idx - 1], (int16_t)(halfpixelswide + screenx),
			(int16_t)(pixelsdeep - (transfm2_screenyoffset + halfpixelsdeep + screeny)), angle);
	else
		draw_drawbackdropimage(backdropspecies[tile_idx - 1], (int16_t)(halfpixelswide + screenx),
							   (int16_t)(pixelsdeep - (transfm2_screenyoffset + halfpixelsdeep + screeny)),
							   angle);
}
