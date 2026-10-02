/*
 * BACKDRP2 — skybox backdrop renderer.
 *
 * Rebuilds per-frame rotation-matrix lookup tables (rtsvga2 shift*mul,
 * stareye*), then scans backdropposition[] to project and blit up to
 * three visible walls of the skybox cube.
 */

#include "tie/backdrp2.h"
#include "tie/draw.h"
#include "tie/edition.h"
#include "tie/logbuf2.h"
#include "tie/render_scene_tie98.h"
#include "tie/rtsvga2.h"
#include "tie/tie.h"
#include "tie/transfm2.h"
#include "tie/trig2.h"
#include "tie_runtime/runtime/profile.h"
#include "tie_runtime/runtime/wide_arithmetic.h"

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
uint16_t backdropfrontcnt = 0;
// GLOBAL: TIE95 0xC1776
// GLOBAL: TIE98 0x4FA544
uint16_t backdropbackcnt = 0;
// GLOBAL: TIE95 0xC1778
// GLOBAL: TIE98 0x4FA548
uint16_t backdroptopcnt = 0;
// GLOBAL: TIE95 0xC177A
// GLOBAL: TIE98 0x4FA54C
uint16_t backdropbottomcnt = 0;
// GLOBAL: TIE95 0xC177C
// GLOBAL: TIE98 0x4FA550
uint16_t backdropleftcnt = 0;
// GLOBAL: TIE95 0xC177E
// GLOBAL: TIE98 0x4FA554
uint16_t backdroprightcnt = 0;

// FUNCTION: TIE95 0x11C90
// FUNCTION: TIE98 0x4034B0
void backdrp2_backdrop(void) {
	/* 1) Refresh shift tables:
	 *      shift*Kmul[i] = (i * worldeye*K) >> 5,  i in [0..15]. */
	int32_t x;
	int32_t z;
	int nx;
	int s;
	int index;
	int32_t y;
	uint8_t position;
	int i;
	int count;
	uint16_t angle;

	for (i = 0; i < 16; i++) {
		shiftA1mul[i] = (i * worldeyeA1) >> 5;
		shiftA2mul[i] = (i * worldeyeA2) >> 5;
		shiftA3mul[i] = (i * worldeyeA3) >> 5;
		shiftB1mul[i] = (i * worldeyeB1) >> 5;
		shiftB2mul[i] = (i * worldeyeB2) >> 5;
		shiftB3mul[i] = (i * worldeyeB3) >> 5;
		shiftC1mul[i] = (i * worldeyeC1) >> 5;
		shiftC2mul[i] = (i * worldeyeC2) >> 5;
		shiftC3mul[i] = (i * worldeyeC3) >> 5;
	}

	/* 2) Refresh 5x5x5 parallax-star grid in eye space:
	 *      stareye[s] = (nx*A + ny*B + nz*C) >> 7,
	 *      nx,ny,nz in [-2..2]. 125 populated slots. */
	s = 0;
	for (nx = -2; nx < 3; nx++) {
		int32_t ax = nx * worldeyeA1;
		int32_t ay = nx * worldeyeA2;
		int32_t az = nx * worldeyeA3;
		int ny;

		for (ny = -2; ny < 3; ny++) {
			int32_t bx = ax + ny * worldeyeB1;
			int32_t by = ay + ny * worldeyeB2;
			int32_t bz = az + ny * worldeyeB3;
			int nz;

			for (nz = -2; nz < 3; nz++) {
				stareyex[s] = (bx + nz * worldeyeC1) >> 7;
				stareyey[s] = (by + nz * worldeyeC2) >> 7;
				stareyez[s] = (bz + nz * worldeyeC3) >> 7;
				s++;
			}
		}
	}

	if (drawbackdropflag) {
		/* Walls are stored front, back, left, right, top, bottom; only the
		 * wall of each opposing pair that faces the eye is scanned. */
		index = 0;
		angle = (uint16_t)-trig2_arctan(worldeyeA2, worldeyeA1);
		if (worldeyeB3 >= 0) {
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
		} else {
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
		}
		angle = (uint16_t)-trig2_arctan(worldeyeB2, worldeyeB1);
		if (worldeyeA3 >= 0) {
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
		} else {
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
		}
		angle = (uint16_t)-trig2_arctan(worldeyeA2, worldeyeA1);
		if (worldeyeC3 >= 0) {
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
		} else {
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
	/* Each axis is projected in place: x and y become screen offsets. */
	if (x < 0) {
		x = (int32_t)(0u - (uint32_t)x);
		if (x > z)
			return;
		x = (int32_t)math2_project_persp((uint32_t)x, (uint32_t)z);
		x = (int32_t)(0u - (uint32_t)x);
	} else {
		if (x > z)
			return;
		x = (int32_t)math2_project_persp((uint32_t)x, (uint32_t)z);
	}
	if (y < 0) {
		y = (int32_t)(0u - (uint32_t)y);
		if (y > z)
			return;
		y = (int32_t)math2_project_persp((uint32_t)y, (uint32_t)z);
		y = (int32_t)(0u - (uint32_t)y);
	} else {
		if (y > z)
			return;
		y = (int32_t)math2_project_persp((uint32_t)y, (uint32_t)z);
	}

	x = (int32_t)((uint32_t)x + halfpixelswide);
	y = (int32_t)(pixelsdeep - ((uint32_t)y + halfpixelsdeep + (uint32_t)transfm2_screenyoffset));
	if (TIE_FLIGHT_TIE98)
		draw_drawbackdropimage_tie98(backdropspecies[tile_idx - 1], (int16_t)x, (int16_t)y, angle);
	else
		draw_drawbackdropimage(backdropspecies[tile_idx - 1], (int16_t)x, (int16_t)y, angle);
}
