#include "tie/xtrans2.h"
#include "tie/draw.h"
#include "tie/drawpol.h"
#include "tie/fediskio.h" /* flightbuf_small_handle / flightbuf_big_handle */
#include "tie/logbuf2.h"
#include "tie/rtsvga2.h"
#include "tie/tie.h"
#include "tie/trace2.h"
#include "tie_runtime/audio/config.h"
#include "tie_runtime/diagnostics/diagnostics.h"
#include "tie_runtime/display/classic_display.h"
#include "tie_runtime/display/classic_framebuffer.h"
#include "tie_runtime/flight_assets/model_types.h"
#include "tie_runtime/input/input.h"
#include "tie_runtime/runtime/exports.h"
#include "tie_runtime/runtime/profile.h"
#include "tie_runtime/storage/storage.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ============================================================================
 * Module-owned globals (watdbg-attributed to xtrans2.c).
 * ========================================================================== */

/* Lazy-init flags. */
// GLOBAL: TIE95 0xCDDE4
uint8_t xtrans2_dithercolorinitflag;
// GLOBAL: TIE95 0xCDDE5
uint8_t xtrans2_materialrgbinitflag;

/* Linear framebuffer base. Other modules (LOGBUF2, PANEL) assign this at
 * mode-set time; XTRANS2 only reads. */
// GLOBAL: TIE95 0xCDDEC
// GLOBAL: TIE98 0x4F2A6C
uint8_t* xtrans2_videobaseptr;

/* Data-segment initial value matches the binary at 0xdd02e: -16384.
 * Mask buffer lives at xtransdataptr + (uint16_t)maskbufptr = +0xC000,
 * placing it past the polygon-data region drawpol writes via newobjectdef.
 * If left at 0, drawpol's writes overlap and corrupt the mask before
 * drawxtrans runs. */
// GLOBAL: TIE95 0xCDDE6
// GLOBAL: TIE98 0x4E44B4
uint16_t maskbufptr = 0xC000;

// GLOBAL: TIE95 0xED210
int32_t leftsidedata1[480];
// GLOBAL: TIE95 0xED990
int32_t leftsidedata2[480];
// GLOBAL: TIE95 0xEE990
int32_t rightsidedata1[480];
// GLOBAL: TIE95 0xEE210
int32_t rightsidedata2[480];

/* The binary pre-links these pointers to the "1" buffers at data-segment
 * init time (leftside = &leftsidedata1 at 0xcddf0, rightside = &rightsidedata1
 * at 0xcddf4 in Z_TIE__.EXE). logbuf2_startPIP swaps them to the "2" buffers
 * for nested PIP rendering, logbuf2_finishPIP restores them. Without this
 * initialisation xtrans2_clearruntable crashes the first time BPFLIGHT runs
 * without a prior startPIP call (e.g. tech/blueprint room). */
// GLOBAL: TIE95 0xCDDF0
int32_t* leftside = leftsidedata1;
// GLOBAL: TIE95 0xCDDF4
int32_t* rightside = rightsidedata1;

// GLOBAL: TIE95 0xF4FA0
uint8_t* logbufbaseptr;
// GLOBAL: TIE95 0xF4FA4
int32_t logbufypos;

/* Retail edge tables hold 256 entries (edgept1/edgept2 are 1024 bytes). */
// GLOBAL: TIE95 0xEC610
TRANSFM2_ScreenPoint* edgept1[256];
// GLOBAL: TIE95 0xEC210
TRANSFM2_ScreenPoint* edgept2[256];
// GLOBAL: TIE95 0xEF910
int32_t edgexdiff[256];
// GLOBAL: TIE95 0xEF110
int32_t edgeydiff[256];
// GLOBAL: TIE95 0xF0190
int8_t edgexsign[256];
// GLOBAL: TIE95 0xF0090
int8_t edgeysign[256];
// GLOBAL: TIE95 0xEF510
int16_t edgeslopehi[256];
// GLOBAL: TIE95 0xEFD10
int16_t edgeslopelo[256];
// GLOBAL: TIE95 0xEF710
int16_t edgeslopefrac[256];
// GLOBAL: TIE95 0xF4390
uint8_t edgeflags[256];
// GLOBAL: TIE95 0xF0290
void* edgeflagptr[256];

// GLOBAL: TIE95 0xD404C
uint16_t flatobjnum;
// GLOBAL: TIE95 0xF4B90
uint8_t flatcolors[128];
// GLOBAL: TIE95 0xF4D10
uint8_t flatcomponentnum[128];
// GLOBAL: TIE95 0xF4A90
uint16_t flatparentobj[128];
// GLOBAL: TIE95 0xF4C10
int16_t flatz[128];
// GLOBAL: TIE95 0xF4D90
int16_t flatx[128];
// GLOBAL: TIE95 0xF4E90
int16_t flaty[128];

// GLOBAL: TIE95 0xEE110
uint8_t objflag[256];
// GLOBAL: TIE95 0xF0690
uint8_t objectcount[128];
// GLOBAL: TIE95 0xF0010
uint8_t objectminface[128];
// GLOBAL: TIE95 0xF0710
void* objectminedgeptr[128];
// GLOBAL: TIE95 0xEFF10
uint16_t objheap[128];

// GLOBAL: TIE95 0xF1C90
uint32_t dithercolors[39][16][4];
// GLOBAL: TIE95 0xF0910
uint8_t materialrgbhi[2496];
// GLOBAL: TIE95 0xF12D0
uint8_t materialrgblo[2496];
// GLOBAL: TIE95 0xECA10
int32_t starhashtable[512];
// GLOBAL: TIE95 0xF4490
int32_t eyexyzdata[384]; /* 1536 bytes; stored int32 (3/vtx = 12B) */

// GLOBAL: TIE95 0xF4FB0
int32_t newx;
// GLOBAL: TIE95 0xF4FB4
uint8_t* maskptr;
// GLOBAL: TIE95 0xF4F90
uint32_t videoypos;
// GLOBAL: TIE95 0xF4F94
int32_t startx_mod_54;
// GLOBAL: TIE95 0xF4FBC
int32_t newlt;
// GLOBAL: TIE95 0xF4FC0
uint32_t objid;
// GLOBAL: TIE95 0xF4FC4
uint32_t face2;
// GLOBAL: TIE95 0xF4FC8
uint32_t face1;
// GLOBAL: TIE95 0xF4FCC
int32_t maskx;
// GLOBAL: TIE95 0xF4F9C
int32_t currentypos;
// GLOBAL: TIE95 0xF4FD0
int32_t runx;
// GLOBAL: TIE95 0xF4FA8
int32_t endx;
// GLOBAL: TIE95 0xF4FD4
uint32_t edgeid;
// GLOBAL: TIE95 0xF4FB8
uint32_t pixdeepshft24;
// GLOBAL: TIE95 0xF4FD8
trace2_EdgeHeader* tempptr;
// GLOBAL: TIE95 0xF4FDC
trace2_EdgeHeader* currptr;
// GLOBAL: TIE95 0xF4FE0
trace2_EdgeHeader* currptr2;
// GLOBAL: TIE95 0xF4FE4
trace2_EdgeHeader* currentedgeptr;
// GLOBAL: TIE95 0xF4FE8
trace2_EdgeHeader* lastptr;
// GLOBAL: TIE95 0xF4FEC
trace2_EdgeHeader* headerlist;

// GLOBAL: TIE95 0xF4FFA
int16_t numlastrow;
// GLOBAL: TIE95 0xF4FF2
uint16_t curobjid;
// GLOBAL: TIE95 0xF4FFC
uint16_t twicepixelsdeep;
// GLOBAL: TIE95 0xF4FF8
uint16_t lastheap;
// GLOBAL: TIE95 0xF4FFE
uint16_t pixdeepshft8;
// GLOBAL: TIE95 0xF5000
uint16_t pixwideshft7;

// GLOBAL: TIE95 0xF5002
int8_t maskflag;
// GLOBAL: TIE95 0xF5003
uint8_t popflag;
// GLOBAL: TIE95 0xF5004
uint8_t xtflagvalue;

/* ============================================================================
 * Cross-module externs.
 * ========================================================================== */

/* TRACE2 pool + cursors. */

/* Viewport geometry (defined in logbuf2.c / xvesa). */

/* xtransdataptr is the blob owning the per-frame object / edge records.
 * objectptrs[] and markingptr[] hold u16 offsets from its base.
 * Declared in tie.h. */

/* ============================================================================
 * Local helpers.
 * ========================================================================== */

/* ============================================================================
 * xtrans2_clearruntable
 * ----------------------------------------------------------------------------
 * Reset per-scanline run extents and lazy-init the two global shade tables.
 *   - dithercolors[9984]: 39 materials x 16 steps x 4 permutations of
 *     (current_color, next_color) bytes into a packed dword.
 *   - materialrgbhi[]/materialrgblo[]: 39 materials x 64 blend steps,
 *     interpolating R/G/B between adjacent material colors in 5-6-5 space
 *     with 64-step granularity.
 *   - leftside[y] = 0, rightside[y] = pixelswide for y in [0, pixelsdeep).
 *
 * Callers: logbuf2_startPIP, TIE_updatescreen, BPFLIGHT_draw_Engine.
 * ========================================================================== */
// FUNCTION: TIE95 0x622D0
void xtrans2_clearruntable(void) {
	int i, j;
	int row;

	if (!xtrans2_dithercolorinitflag) {
		/* 39 materials x 16 steps x 4 dither permutations of the step
		 * colour and its successor, packed one byte per pixel. */
		for (i = 0; i < 39; ++i) {
			for (j = 0; j < 16; ++j) {
				uint8_t c = materialcolors[i * 16 + j];
				uint8_t d;

				if (j == 15) {
					d = c;
				} else {
					d = materialcolors[i * 16 + j + 1];
				}
				dithercolors[i][j][0] = ((((c << 8) | c) << 8 | c) << 8) | c;
				dithercolors[i][j][1] = ((((c << 8) | c) << 8 | d) << 8) | c;
				dithercolors[i][j][2] = ((((c << 8) | d) << 8 | c) << 8) | d;
				dithercolors[i][j][3] = ((((d << 8) | c) << 8 | d) << 8) | c;
			}
		}
		xtrans2_dithercolorinitflag = 1;
	}

	if (!xtrans2_materialrgbinitflag) {
		/* 39 materials x 64 blend steps from the ramp's first colour to
		 * its last, interpolated in RGB565 space. */
		for (i = 0; i < 39; ++i) {
			uint8_t* pal;
			unsigned rgb1, rgb2;
			uint8_t r1, g1, b1, r2, g2, b2;

			pal = &rtsvga2_vgapalette[3 * materialcolors[i * 16]];
			rgb1 = (uint16_t)((uint16_t)((pal[0] >> 1) << 11) | (uint16_t)(pal[1] << 5) |
							  (uint16_t)(pal[2] >> 1));
			pal = &rtsvga2_vgapalette[3 * materialcolors[i * 16 + 15]];
			rgb2 = (uint16_t)((uint16_t)((pal[0] >> 1) << 11) | (uint16_t)(pal[1] << 5) |
							  (uint16_t)(pal[2] >> 1));
			r1 = (rgb1 >> 11) & 0x1F;
			r2 = (rgb2 >> 11) & 0x1F;
			g1 = (rgb1 >> 5) & 0x3F;
			g2 = (rgb2 >> 5) & 0x3F;
			b1 = rgb1 & 0x1F;
			b2 = rgb2 & 0x1F;
			r1 *= 2;
			r2 *= 2;
			b1 *= 2;
			b2 *= 2;
			for (j = 0; j < 64; ++j) {
				uint8_t r = r1 + (((r2 - r1) * j) >> 6);
				uint8_t g = g1 + (((g2 - g1) * j) >> 6);
				uint8_t b = b1 + (((b2 - b1) * j) >> 6);
				uint8_t hi, lo;

				hi = (r & 0x3E) << 2;
				hi += g >> 3;
				lo = (g & 7) << 5;
				lo += b >> 1;
				materialrgbhi[i * 64 + j] = hi;
				materialrgblo[i * 64 + j] = lo;
			}
		}
		xtrans2_materialrgbinitflag = 1;
	}

	for (row = 0; row < pixelsdeep; ++row) {
		leftside[row] = 0;
		rightside[row] = pixelswide;
	}
}

/* ============================================================================
 * xtrans2_initxtrans
 * ----------------------------------------------------------------------------
 * Derive viewport-size-dependent scalars, bind the TRACE2 EdgeInfo /
 * EdgeHeader pools onto fediskio's flightbuf_small_handle /
 * flightbuf_big_handle, and clear the active-edge row heads.
 *
 * Must be called after fediskio_Init_Buffers_and_Fonts or BPFLIGHT has
 * allocated the pool handles.
 * ========================================================================== */
// FUNCTION: TIE95 0x6258C
void xtrans2_initxtrans(void) {
	uint16_t pd = pixelsdeep;

	int row;

	pixwideshft7 = (uint16_t)(pixelswide << 7);
	twicepixelsdeep = (uint16_t)(2 * pixelsdeep);
	halfpixelsdeep = (uint16_t)((int)pixelsdeep >> 1);
	halfpixelswide = (uint16_t)((int)pixelswide >> 1);
	pixdeepshft8 = (uint16_t)(pixelsdeep << 8);
	pixelsdeepmin1 = (uint16_t)(pixelsdeep - 1);
	pixdeepshft24 = (uint32_t)pixelsdeep << 24;
	pixelswidemin1 = (uint16_t)(pixelswide - 1);

	for (row = 0; row < pd; ++row)
		trace2_rowheaders[row] = NULL;

	/* Root the pool pointers on the fediskio-owned flight buffers.
	 * Retail re-derives dword_EBF10 / dword_EBF14 here as
	 *   base + 0x3BFF8   (last EdgeInfo slot,   EdgeInfo   = 8 B)
	 *   base + 0xCB1E0   (last EdgeHeader slot, EdgeHeader = 32 B)
	 * which are the cursor clamps consulted by TRACE2 on pool overflow. */
	trace2_newedgeinfo = xmemhdl_Lock_Handle(flightbuf_small_handle);
	trace2_newedgeheader = xmemhdl_Lock_Handle(flightbuf_big_handle);
	trace2_lastedgeheader = &trace2_newedgeheader[TRACE2_EDGEHEADER_CAP - 1];
	trace2_lastedgeinfo = &trace2_newedgeinfo[TRACE2_EDGEINFO_CAP - 1];

	newobjectdef = 0; /* drawpol-owned global; binary resets it here. */
	parentobject = 0;
	nummarks = 0;
	objectnum = 1;
	edgeindex = 1;
	flatobjnum = 1;

	pixelsdeep = pd;
}

/* ============================================================================
 * xtrans2_drawxtrans
 * ----------------------------------------------------------------------------
 * Main per-frame scanline loop. Walks y = [0, pixelsdeep) and for each
 * scanline:
 *   1. VESA bank advance if cursor crossed a page boundary.
 *   2. Age the headerlist active-edge chain (decrement numscanlines,
 *      unlink on zero, advance info ptr otherwise).
 *   3. Bubble-sort remaining active edges by current info->x.
 *   4. Merge in rowheaders[y] (edges starting at this scanline).
 *   5. Walk the mask run-length stream and emit runs via outputxt; every
 *      edge transition calls processedge.
 *   6. After the last edge, finish the scanline with any remaining runs.
 *   7. Clear marking / objflag / objectcount state touched this scanline.
 *   8. Advance videoypos / logbufypos / currentypos.
 * ========================================================================== */
// FUNCTION: TIE95 0x626C0
void xtrans2_drawxtrans(void) {
	uint32_t page;
	int i;

	headerlist = NULL;
	lastheap = 0;
	page = (displaycorner_lines * screenMemWidth + displaycorner_columns) / vesa_page_size;
	videoypos = (displaycorner_lines * screenMemWidth + displaycorner_columns) % vesa_page_size;
	if (flightResolution != (int16_t)0x13)
		rtsvga2_SetCurrentPage(vesa_window, (uint16_t)page);
	logbufbaseptr = (uint8_t*)buffer_ptr;
	logbufypos = 0;
	xtflagvalue = 0;
	numlastrow = 0;
	markcnt = 0;
	for (i = 0; i < 256; ++i)
		objflag[i] = 0;
	for (i = 0; i < 128; ++i)
		objectcount[i] = 0;
	maskptr = (uint8_t*)xtransdataptr + maskbufptr;

	for (currentypos = 0; currentypos < pixelsdeep;
		 videoypos += screenMemWidth, logbufypos += bytesPerPixel * pixelswide, ++currentypos) {
		uint16_t run;

		if (videoypos >= vesa_page_size) {
			++page;
			videoypos -= vesa_page_size;
			rtsvga2_SetCurrentPage(vesa_window, (uint16_t)page);
		}

		/* Age the active edges, dropping the ones that end on this line. */
		currptr = headerlist;
		while (currptr) {
			if (--currptr->numscanlines != 0) {
				++currptr->info;
				headerlist = currptr;
				lastptr = currptr;
				currptr = currptr->next;
				while (currptr) {
					if (--currptr->numscanlines != 0) {
						++currptr->info;
						lastptr = currptr;
						currptr = currptr->next;
					} else {
						currptr = currptr->next;
						lastptr->next = currptr;
					}
				}
				break;
			}
			currptr = currptr->next;
			headerlist = currptr;
		}

		/* Bubble-sort the active edges by x. */
		if (headerlist && headerlist->next) {
			int swapped;

			do {
				tempptr = NULL;
				swapped = 0;
				currptr = headerlist->next;
				lastptr = headerlist;
				while (currptr) {
					if (currptr->info->x < lastptr->info->x) {
						swapped = 1;
						if (tempptr) {
							tempptr->next = currptr;
							lastptr->next = currptr->next;
							currptr->next = lastptr;
							tempptr = currptr;
							currptr = lastptr->next;
						} else {
							headerlist = currptr;
							tempptr = currptr;
							lastptr->next = currptr->next;
							currptr = currptr->next;
							tempptr->next = lastptr;
						}
					} else {
						currptr = currptr->next;
						tempptr = lastptr;
						lastptr = lastptr->next;
					}
				}
			} while (swapped);
		}

		/* Merge in the edges starting on this line. */
		currptr = trace2_rowheaders[currentypos];
		while (currptr) {
			currptr2 = headerlist;
			lastptr = NULL;
			while (currptr2) {
				if (currptr2->info->x >= currptr->info->x) {
					if (lastptr) {
						tempptr = lastptr->next;
						lastptr->next = currptr;
						currptr2 = currptr;
						currptr = currptr->next;
						currptr2->next = tempptr;
					} else {
						tempptr = headerlist;
						headerlist = currptr;
						currptr = currptr->next;
						headerlist->next = tempptr;
					}
					break;
				}
				lastptr = currptr2;
				currptr2 = currptr2->next;
			}
			if (!currptr2) {
				if (lastptr) {
					lastptr->next = currptr;
					tempptr = currptr;
					currptr = currptr->next;
					tempptr->next = NULL;
				} else {
					headerlist = currptr;
					currptr = currptr->next;
					headerlist->next = NULL;
				}
			}
		}

		/* Read the first mask run of the line. */
		lastheap = 0;
		curobjid = 0;
		maskflag = *maskptr++;
		maskx = *maskptr++;
		popflag = 0;
		if (maskx == 0) {
			maskx = *maskptr++;
			if (maskx == 0)
				maskx = *maskptr++ + 256;
			maskx += 255;
		}

		if (maskflag >= 0 || (startx_mod_54 = maskx) < pixelswide) {
			if (headerlist) {
				runx = headerlist->info->x;
				runx >>= 8;
				if (runx > leftside[currentypos] && leftside[currentypos] < pixelswide) {
					startx_mod_54 = leftside[currentypos];
					while (startx_mod_54 >= maskx) {
						run = *maskptr++;
						maskflag = -maskflag;
						if (run == 0) {
							run = *maskptr++;
							if (run == 0)
								run = *maskptr++ + 256;
							run += 255;
						}
						maskx += run;
					}
					if (maskflag < 0) {
						startx_mod_54 = maskx;
						if (pixelswide <= maskx)
							continue;
					}
					while (runx >= maskx) {
						if (maskx >= pixelswide) {
							runx = pixelswide;
							break;
						}
						if (maskflag < 0) {
							startx_mod_54 = maskx;
						} else {
							endx = maskx;
							xtrans2_outputxt();
						}
						maskflag = -maskflag;
						run = *maskptr++;
						if (run == 0) {
							run = *maskptr++;
							if (run == 0)
								run = *maskptr++ + 256;
							run += 255;
						}
						maskx += run;
					}
					if (maskflag < 0) {
						startx_mod_54 = maskx;
					} else {
						endx = runx;
						xtrans2_outputxt();
					}
					if (startx_mod_54 >= pixelswide)
						continue;
				} else {
					if (runx > pixelswide)
						runx = pixelswide;
					if (runx < 0)
						runx = 0;
				}
				leftside[currentypos] = runx;
			}

			for (startx_mod_54 = leftside[currentypos]; startx_mod_54 >= maskx; maskx += run) {
				run = *maskptr++;
				maskflag = -maskflag;
				if (run == 0) {
					run = *maskptr++;
					if (run == 0)
						run = *maskptr++ + 256;
					run += 255;
				}
			}

			if (maskflag >= 0 || (startx_mod_54 = maskx) < pixelswide) {
				int done = 0;

				for (currentedgeptr = headerlist; currentedgeptr; currentedgeptr = currentedgeptr->next) {
					objid = currentedgeptr->objectid;
					edgeid = currentedgeptr->edgeid;
					face1 = currentedgeptr->face1;
					face2 = currentedgeptr->face2;
					endx = currentedgeptr->info->x;
					endx >>= 8;
					newx = endx;
					newlt = currentedgeptr->info->lt >> 1;
					runx = endx;
					while (runx >= maskx) {
						maskflag = -maskflag;
						if (maskflag < 0) {
							endx = maskx;
							xtrans2_outputxt();
							if (maskx >= pixelswide) {
								done = 1;
								break;
							}
							run = *maskptr++;
							if (run == 0) {
								run = *maskptr++;
								if (run == 0)
									run = *maskptr++ + 256;
								run += 255;
							}
							startx_mod_54 += run;
							maskx += run;
							if (maskx >= pixelswide) {
								done = 1;
								break;
							}
						} else {
							if (curobjid == (uint16_t)0xFFFF)
								curobjid = xtrans2_findnearest();
							objflag[curobjid] = 0xFF;
							run = *maskptr++;
							if (run == 0) {
								run = *maskptr++;
								if (run == 0)
									run = *maskptr++ + 256;
								run += 255;
							}
							maskx += run;
						}
					}
					if (done)
						break;
					endx = runx;
					xtrans2_processedge();
				}

				if (done) {
					rightside[currentypos] = pixelswide;
				} else {
					objid = 0;
					while (maskx < pixelswide) {
						if (maskflag < 0) {
							startx_mod_54 = maskx;
						} else {
							if (curobjid == (uint16_t)0xFFFF)
								curobjid = xtrans2_findnearest();
							if (curobjid == 0)
								break;
							endx = maskx;
							xtrans2_outputxt();
						}
						maskflag = -maskflag;
						run = *maskptr++;
						if (run == 0) {
							run = *maskptr++;
							if (run == 0)
								run = *maskptr++ + 256;
							run += 255;
						}
						maskx += run;
					}
					if (maskflag < 0) {
						rightside[currentypos] = pixelswide;
					} else {
						if (curobjid == (uint16_t)0xFFFF)
							curobjid = xtrans2_findnearest();
						if (curobjid == 0) {
							runx = rightside[currentypos];
							rightside[currentypos] = startx_mod_54;
							while (maskx < pixelswide) {
								if (maskflag < 0) {
									startx_mod_54 = maskx;
								} else if (maskx < runx) {
									endx = maskx;
									xtrans2_outputxt();
								} else if (startx_mod_54 < runx) {
									endx = runx;
									xtrans2_outputxt();
								}
								run = *maskptr++;
								maskflag = -maskflag;
								if (run == 0) {
									run = *maskptr++;
									if (run == 0)
										run = *maskptr++ + 256;
									run += 255;
								}
								maskx += run;
							}
							if (maskflag > 0) {
								if (maskx < runx) {
									endx = maskx;
									xtrans2_outputxt();
								} else if (runx > startx_mod_54) {
									endx = runx;
									xtrans2_outputxt();
								}
							}
						} else {
							endx = pixelswide;
							xtrans2_outputxt();
							rightside[currentypos] = pixelswide;
						}
					}
				}

				/* Restore the marking records touched on this line. */
				if (markcnt) {
					int mark;

					markcnt = 0;
					for (mark = nummarks; mark != 0; --mark) {
						if (markingnumber[mark]) {
							uint8_t* record;
							uint8_t* object;
							uint8_t slot;
							uint8_t swap;

							markingnumber[mark] = 0;
							record = (uint8_t*)xtransdataptr + markingptr[mark];
							slot = record[18];
							((uint16_t*)record)[9] = 0;
							((uint16_t*)record)[10] = 0;
							((uint16_t*)record)[11] = 0;
							((uint16_t*)record)[12] = 0;
							((uint16_t*)record)[13] = 0;
							((uint16_t*)record)[14] = 0;
							((uint16_t*)record)[15] = 0;
							((uint16_t*)record)[16] = 0;
							object = (uint8_t*)xtransdataptr + objectptrs[record[0]] + 2 * record[1];
							swap = record[slot + 1];
							record[slot + 1] = object[534];
							object[534] = swap;
						}
					}
				}

				if (curobjid != (uint16_t)0xFFFF) {
					objflag[curobjid] = 0;
					if (curobjid < 0x80)
						objectcount[curobjid] = 0;
				}
				while (lastheap) {
					objflag[objheap[lastheap]] = 0;
					if (objheap[lastheap] < 0x80)
						objectcount[objheap[lastheap]] = 0;
					--lastheap;
				}
			}
		}
	}

	xmemhdl_Unlock_Handle(flightbuf_small_handle);
	xmemhdl_Unlock_Handle(flightbuf_big_handle);
}

/* ============================================================================
 * xtrans2_processedge
 * ----------------------------------------------------------------------------
 * Process one edge coming in from the active list for the current scanline.
 * Dispatches on objid's range:
 *   >= 0xF0 : transparent / marking record — walk the marking-list chain.
 *   >= 0x80 : flat-polygon marker — call openobject if not already in heap.
 *   [1,0x7F]: mesh face transition — maintain face_ypos[] and
 *              objectminface[]/objectminedgeptr[]; may call open/close.
 * ========================================================================== */
// FUNCTION: TIE95 0x633DC
void xtrans2_processedge(void) {
	/* --- Branch 1: marking-list handling for objid >= 0xF0. */
	xtrans2_ObjectRecord* rec;

	if (objid >= 0xF0) {
		uint16_t cur_pos = (uint16_t)(256 - objid);
		uint8_t* mark = (uint8_t*)xtransdataptr + markingptr[edgeid];
		uint8_t* mark_base = mark;

		uint8_t mark_obj;
		uint8_t* face_flag_ptr;
		uint8_t* slot_ptr;
		uint8_t sb;

		if ((uint16_t)(256 - objid) < mark[18]) {
			/* Walk chain; insert cur_pos at sorted position. */
			uint16_t nxt_cur;
			uint8_t chain_byte;

			do {
				do {
					nxt_cur = mark[19];
					++mark;
				} while (cur_pos < nxt_cur);
				if (cur_pos == nxt_cur)
					break;
				mark[18] = (uint8_t)cur_pos;
				cur_pos = nxt_cur;
				if (nxt_cur == 0)
					return;
			} while (1);
			/* After break: skip duplicate; slide remainder. */

			do {
				chain_byte = mark[19];
				mark[18] = chain_byte;
				++mark;
			} while (chain_byte);
			return;
		}

		if (cur_pos == mark[18]) {
			if (mark[19]) {
				uint8_t slot_next = mark[19];
				uint8_t nxt_byte;
				uint8_t tmp;

				do {
					nxt_byte = mark[19];
					mark[18] = nxt_byte;
					++mark;
				} while (nxt_byte);
				tmp = mark_base[cur_pos + 1];
				mark_base[cur_pos + 1] = mark_base[slot_next + 1];
				mark_base[slot_next + 1] = tmp;
			} else {
				mark[18] = 0;
				markingnumber[edgeid] = 0;
				--markcnt;
			}
		} else {
			uint8_t slot_b = (uint8_t)(-(int)(uint8_t)objid);
			uint8_t slot_a = mark[18];
			uint8_t nxt_b;
			do {
				nxt_b = (++mark)[17];
				mark[17] = (uint8_t)cur_pos;
				cur_pos = nxt_b;
			} while (nxt_b);
			if (slot_a) {
				uint8_t tmp;

				cur_pos = slot_a;
				tmp = mark_base[slot_a + 1];
				mark_base[slot_a + 1] = mark_base[slot_b + 1];
				mark_base[slot_b + 1] = tmp;
			} else {
				markingnumber[edgeid] = 1;
				cur_pos = slot_b;
				markcnt = (uint8_t)(markcnt + 1);
			}
		}

		mark_obj = mark_base[0];
		if (curobjid == mark_obj && objectminface[mark_obj] == mark_base[1])
			xtrans2_outputxt();

		face_flag_ptr = (uint8_t*)xtransdataptr + 2 * mark_base[1] + objectptrs[mark_base[0]] + 534;
		slot_ptr = &mark_base[cur_pos];
		sb = slot_ptr[1];
		slot_ptr[1] = *face_flag_ptr;
		*face_flag_ptr = sb;
		return;
	}

	/* --- Branch 2: flat-poly marker. */
	if (objid >= 0x80) {
		if (objflag[objid]) {
			xtrans2_closeobject();
			return;
		}
		xtrans2_openobject();
		return;
	}

	/* --- Branch 3: regular mesh face transition. */
	rec = (xtrans2_ObjectRecord*)((uint8_t*)xtransdataptr + objectptrs[objid]);

	if (!objectcount[objid]) {
		/* First time we see this object this scanline. */
		rec->face_ypos[face1] = currentypos;
		objectminface[objid] = (uint8_t)face1;
		objectminedgeptr[objid] = currentedgeptr;
		++objectcount[objid];
		if (face2) {
			rec->face_ypos[face2] = currentypos;
			++objectcount[objid];
			if (face2 < face1)
				objectminface[objid] = (uint8_t)face2;
		}
		xtrans2_openobject();
		return;
	}

	if (objectcount[objid] == 1) {
		int32_t cy = currentypos;
		if (currentypos == rec->face_ypos[face1]) {
			rec->face_ypos[face1] = currentypos - 1;
			if (face2) {
				if (curobjid == objid && maskflag >= 0)
					xtrans2_outputxt();
				rec->face_ypos[face2] = currentypos;
				objectminface[objid] = (uint8_t)face2;
				objectminedgeptr[objid] = currentedgeptr;
			} else {
				objectcount[objid] = 0;
				xtrans2_closeobject();
			}
			return;
		}

		rec->face_ypos[face1] = currentypos;
		if (face2) {
			if (cy == rec->face_ypos[face2]) {
				if (curobjid == objid && maskflag >= 0)
					xtrans2_outputxt();
				rec->face_ypos[face2] = currentypos - 1;
				objectminface[objid] = (uint8_t)face1;
				objectminedgeptr[objid] = currentedgeptr;
			} else {
				rec->face_ypos[face2] = cy;
				objectcount[objid] = (uint8_t)(objectcount[objid] + 2);
				if (face2 <= face1) {
					if (objectminface[objid] > face2) {
						if (curobjid == objid && maskflag >= 0)
							xtrans2_outputxt();
						objectminface[objid] = (uint8_t)face2;
						objectminedgeptr[objid] = currentedgeptr;
					}
				} else if (objectminface[objid] > face1) {
					if (curobjid == objid && maskflag >= 0)
						xtrans2_outputxt();
					objectminface[objid] = (uint8_t)face1;
					objectminedgeptr[objid] = currentedgeptr;
				}
			}
		} else {
			uint32_t minf = objectminface[objid];
			++objectcount[objid];
			if (minf > face1) {
				if (curobjid == objid && maskflag >= 0)
					xtrans2_outputxt();
				objectminface[objid] = (uint8_t)face1;
				objectminedgeptr[objid] = currentedgeptr;
			}
		}
		return;
	}

	/* objectcount >= 2 : more than one face already open. */
	if (currentypos == rec->face_ypos[face1]) {
		/* Closing face1. */
		uint32_t minf;

		rec->face_ypos[face1] = currentypos - 1;
		minf = objectminface[objid];
		--objectcount[objid];

		if (minf == face1) {
			uint16_t fit;

			if (curobjid == objid && maskflag >= 0)
				xtrans2_outputxt();

			if (face2 && face2 < face1) {
				rec->face_ypos[face2] = currentypos;
				++objectcount[objid];
				objectminface[objid] = (uint8_t)face2;
				objectminedgeptr[objid] = currentedgeptr;
				return;
			}

			/* Rescan for the next-lowest open face owned by this obj.
			 * First range: [face1, face2) — stop on a shared edge. */
			fit = (uint16_t)face1;
			if (face2) {
				uint8_t oc_m;

				while (fit < face2) {
					if (currentypos == rec->face_ypos[fit]) {
						trace2_EdgeHeader* hdr = (trace2_EdgeHeader*)headerlist;
						while (hdr) {
							if (hdr->objectid == objid &&
								((uint32_t)fit == hdr->face1 || (uint32_t)fit == hdr->face2)) {
								objectminedgeptr[objid] = hdr;
								objectminface[objid] = (uint8_t)fit;
								/* Also adjust face2's face_ypos:
								 * binary computes *(DWORD*)(f2rec+24)
								 * which equals rec->face_ypos[face2]. */
								if (currentypos == rec->face_ypos[face2]) {
									rec->face_ypos[face2] = currentypos - 1;
									--objectcount[objid];
								} else {
									rec->face_ypos[face2] = currentypos;
									++objectcount[objid];
								}
								return;
							}
							hdr = hdr->next;
						}
					}
					++fit;
				}

				if (currentypos != rec->face_ypos[face2]) {
					uint8_t oc;

					rec->face_ypos[face2] = currentypos;
					oc = objectcount[objid];
					objectminface[objid] = (uint8_t)face2;
					objectminedgeptr[objid] = currentedgeptr;
					objectcount[objid] = (uint8_t)(oc + 1);
					return;
				}
				rec->face_ypos[face2] = currentypos - 1;
				oc_m = (uint8_t)(objectcount[objid] - 1);
				objectcount[objid] = oc_m;
				if (!oc_m) {
					xtrans2_closeobject();
					return;
				}
				++fit;
			}

			/* Second range: [face2+1, 0x80) when face2 was set; otherwise
			 * [face1, 0x80). The face1 iteration is harmless (its
			 * face_ypos was just set to currentypos-1 above). */
			while (fit < 0x80) {
				if (currentypos == rec->face_ypos[fit]) {
					trace2_EdgeHeader* hdr = (trace2_EdgeHeader*)headerlist;
					while (hdr) {
						if (hdr->objectid == objid &&
							((uint32_t)fit == hdr->face1 || (uint32_t)fit == hdr->face2)) {
							objectminface[objid] = (uint8_t)fit;
							objectminedgeptr[objid] = hdr;
							return;
						}
						hdr = hdr->next;
					}
				}
				++fit;
			}
		}
	} else {
		/* face1 didn't match last y — fresh entry, advance minface. */
		rec->face_ypos[face1] = currentypos;
		if (objectminface[objid] > face1) {
			if (curobjid == objid && maskflag >= 0)
				xtrans2_outputxt();
			objectminface[objid] = (uint8_t)face1;
			objectminedgeptr[objid] = currentedgeptr;
		}
		++objectcount[objid];
	}

	if (face2) {
		if (currentypos == rec->face_ypos[face2]) {
			uint32_t minf;

			rec->face_ypos[face2] = currentypos - 1;
			minf = objectminface[objid];
			--objectcount[objid];
			if (minf == face2) {
				uint16_t fit;

				if (curobjid == objid && maskflag >= 0)
					xtrans2_outputxt();
				for (fit = (uint16_t)face2; fit < 0x80; ++fit) {
					if (currentypos == rec->face_ypos[fit]) {
						trace2_EdgeHeader* hdr = (trace2_EdgeHeader*)headerlist;
						while (hdr) {
							if (hdr->objectid == objid &&
								((uint32_t)fit == hdr->face1 || (uint32_t)fit == hdr->face2)) {
								objectminface[objid] = (uint8_t)fit;
								objectminedgeptr[objid] = hdr;
								return;
							}
							hdr = hdr->next;
						}
					}
				}
			}
		} else {
			rec->face_ypos[face2] = currentypos;
			if (objectminface[objid] > face2) {
				if (curobjid == objid && maskflag >= 0)
					xtrans2_outputxt();
				objectminface[objid] = (uint8_t)face2;
				objectminedgeptr[objid] = currentedgeptr;
			}
			++objectcount[objid];
		}
	}
}

/* ============================================================================
 * xtrans2_closeobject
 * ----------------------------------------------------------------------------
 * Remove _objid from the active object heap.
 * ========================================================================== */
// FUNCTION: TIE95 0x63CD0
void xtrans2_closeobject(void) {
	uint16_t removed_pos = objflag[objid];

	objflag[objid] = 0;

	if (objid == curobjid) {
		/* Removing the frontmost. */
		if (popflag) {
			uint16_t top_pos;

			if (maskflag >= 0)
				xtrans2_outputxt();
			top_pos = lastheap;
			curobjid = objheap[lastheap];
			objflag[curobjid] = 0xFF;
			lastheap = (uint16_t)(top_pos - 1);
			--popflag;
			return;
		}
		if (maskflag < 0) {
			if (lastheap == 0) {
				curobjid = 0;
				return;
			}
			curobjid = 0xFFFF;
			return;
		}
		/* maskflag >= 0 and no pending pop. */
		xtrans2_outputxt();
		curobjid = xtrans2_findnearest();
		objflag[curobjid] = 0xFF;
		return;
	}

	/* Removing a flat-poly marker that lives below the top. */
	if (objid == 128 && curobjid && curobjid != (uint16_t)0xFFFF) {
		/* objflag[128] reused to remember the removed flat's heap slot
		 * across the subsequent re-sort (see getinfront usage). */
		objflag[objid] = (uint8_t)removed_pos;
		objheap[removed_pos] = (uint16_t)objid; /* re-stamp slot */
		if (maskflag < 0) {
			if (lastheap == 0) {
				curobjid = 0;
				return;
			}
			curobjid = 0xFFFF;
			return;
		}
		xtrans2_outputxt();
		curobjid = xtrans2_findnearest();
		objflag[curobjid] = 0xFF;
		return;
	}

	/* Mid-heap swap with top-of-heap, then shrink. */
	if (removed_pos != lastheap) {
		objheap[removed_pos] = objheap[lastheap];
		objflag[objheap[removed_pos]] = (uint8_t)removed_pos;
		popflag = 0;
	} else if (popflag > 0) {
		--popflag;
	}
	--lastheap;
}

/* ============================================================================
 * xtrans2_openobject
 * ----------------------------------------------------------------------------
 * Push _objid onto the active object heap. If the newcomer is in front of
 * the current top, it becomes the new frontmost.
 * ========================================================================== */
// FUNCTION: TIE95 0x63E78
void xtrans2_openobject(void) {
	if (curobjid == 0) {
		/* Empty heap — first object in. */
		objflag[0] = 0;
		if (maskflag >= 0)
			xtrans2_outputxt();
		curobjid = (uint16_t)objid;
		objflag[curobjid] = 0xFF;
		return;
	}

	if (curobjid != (uint16_t)0xFFFF && xtrans2_getinfront((uint16_t)objid, curobjid) == objid) {
		/* Newcomer wins — push old front onto the heap. */
		objheap[++lastheap] = curobjid;
		objflag[curobjid] = (uint8_t)lastheap;
		++popflag;
		if (maskflag >= 0)
			xtrans2_outputxt();
		curobjid = (uint16_t)objid;
		objflag[curobjid] = 0xFF;
		return;
	}

	/* Newcomer stays behind: push it onto the heap. */
	++lastheap;
	popflag = 0;
	objflag[objid] = (uint8_t)lastheap;
	objheap[lastheap] = (uint16_t)objid;

	/* Flat-poly marker (id 128) with something already in front:
	 * re-evaluate in case the flat should demote the current front. */
	if (objid == 128 && lastheap != 1 && curobjid != (uint16_t)0xFFFF) {
		popflag = 0;
		objid = xtrans2_findnearest();
		if (xtrans2_getinfront((uint16_t)objid, curobjid) == objid) {
			objheap[++lastheap] = curobjid;
			objflag[curobjid] = (uint8_t)lastheap;
			if (maskflag >= 0)
				xtrans2_outputxt();
			curobjid = (uint16_t)objid;
			objflag[curobjid] = 0xFF;
			return;
		}
		++lastheap;
		objflag[objid] = (uint8_t)lastheap;
		objheap[lastheap] = (uint16_t)objid;
	}
}

/* ============================================================================
 * xtrans2_outputxt
 * ----------------------------------------------------------------------------
 * Emit one shaded pixel run for the current frontmost object.
 *
 * Range: [startx_mod_54, endx) in pixel columns. Destination is
 *   xtrans2_videobaseptr + videoypos + startx_mod_54, 1 or 2 bytes/pixel.
 *
 * Lighting:
 *   curobjid == 0           : deep-space background (solid fill or
 *                             logbuf passthrough).
 *   curobjid >= 0x80        : flat polygon — solid fill from flatcolors[].
 *   curobjid in [1, 0x7F]   : mesh face — face_flags[] slot c decides
 *                             Gouraud (c < 0x40) vs solid (c >= 0x40).
 *
 * Gouraud path interpolates lt_cursor across the span with per-pixel
 * dither (alternating dither accumulator by scanline parity).
 * ========================================================================== */
// FUNCTION: TIE95 0x64088
void xtrans2_outputxt(void) {
	uint8_t c;

	if (endx <= startx_mod_54)
		return;

	if (curobjid == 0) {
		c = deepspacecolor;
	} else if (curobjid >= 0x80) {
		/* byte_20A2C8 in the binary = flatcolors - 128. */
		c = flatcolors[curobjid - 128];
	} else {
		/* Binary reads at offset (obj + 0x216 + 2*minface). face_flags is
		 * declared at +0x218, so the actual slot for 1-indexed facenumber
		 * N is face_flags[2*(N-1)]. Matches the 0x216-based write in
		 * xtrans2_processedge's marking-swap logic. */
		c = ((xtrans2_ObjectRecord*)((uint8_t*)xtransdataptr + objectptrs[curobjid]))
				->face_flags[2 * (objectminface[curobjid] - 1)];
		if (c < 0x40) {
			/* --- Gouraud path. */
			trace2_EdgeHeader* left_edge = (trace2_EdgeHeader*)objectminedgeptr[curobjid];
			int32_t right_lt, right_x;
			int32_t dx_span;
			int32_t left_x;
			int32_t inv_left_lt;
			int32_t lt_cursor;
			int32_t dlt;
			int32_t odd_row;
			uint8_t* shade;
			uint8_t* vga_dst;
			uint8_t* vga_end;

			if (curobjid == objid && (objectminface[curobjid] == face1 || objectminface[curobjid] == face2)) {
				right_lt = newlt;
				right_x = newx;
			} else {
				currptr2 = left_edge->rightedge;
				if (currptr2 && !currptr2->numscanlines)
					currptr2 = NULL;
				if (!currptr2) {
					for (currptr2 = (trace2_EdgeHeader*)currentedgeptr; currptr2; currptr2 = currptr2->next) {
						if (curobjid == currptr2->objectid &&
							(objectminface[currptr2->objectid] == currptr2->face1 ||
							 objectminface[currptr2->objectid] == currptr2->face2))
							break;
					}
					left_edge->rightedge = currptr2;
				}
				if (!currptr2) {
					right_x = screenXRes;
					right_lt = left_edge->info->lt >> 1;
					if (special_features_flag)
						printf("Error! Unmatched Edge!\n");
				} else {
					right_lt = currptr2->info->lt >> 1;
					right_x = currptr2->info->x >> 8;
				}
			}

			left_x = left_edge->info->x >> 8;
			dx_span = right_x - left_x;
			inv_left_lt = (63 - ((left_edge->info->lt >> 9) & 0x3F)) << 9;
			lt_cursor = inv_left_lt;
			dlt = ((63 - ((right_lt >> 8) & 0x3F)) << 9) - inv_left_lt;
			odd_row = currentypos & 1;
			shade = &materialcolors[16 * c - 16];
			vga_dst = xtrans2_videobaseptr + videoypos + startx_mod_54;
			vga_end = xtrans2_videobaseptr + videoypos + endx;

			if (bytesPerPixel == 2) {
				uint8_t* lo_base = &materialrgblo[64 * c - 64];
				uint8_t* hi_base = &materialrgbhi[64 * c - 64];

				vga_dst += startx_mod_54;
				vga_end += endx;
				if (dlt) {
					uint8_t saved_lo;
					uint8_t saved_hi;
					int32_t dith;

					if (dx_span)
						dlt /= dx_span;
					if (left_x < startx_mod_54)
						lt_cursor = dlt * (startx_mod_54 - left_x) + inv_left_lt;

					/* The inner loop can compute index 64 when lt_cursor
					 * exits at the top step; stash slot 63 into slot 64
					 * for the run. */
					saved_lo = lo_base[64];
					lo_base[64] = lo_base[63];
					saved_hi = hi_base[64];
					hi_base[64] = hi_base[63];

					for (dith = (currentypos & 1) << 8; vga_dst < vga_end; lt_cursor += dlt) {
						int32_t idx = (lt_cursor + dith) >> 9;
						dith = (lt_cursor + dith) & 0x1FF;
						*vga_dst++ = lo_base[idx];
						*vga_dst++ = hi_base[idx];
					}
					lo_base[64] = saved_lo;
					hi_base[64] = saved_hi;
				} else if ((inv_left_lt >> 9) == 63 || (inv_left_lt & 0x1FF) == 0) {
					uint8_t pix_lo = lo_base[lt_cursor >> 9];
					uint8_t pix_hi = hi_base[lt_cursor >> 9];
					while (vga_dst < vga_end) {
						*vga_dst++ = pix_lo;
						*vga_dst++ = pix_hi;
					}
				} else {
					int32_t dith = odd_row << 8;
					while (vga_dst < vga_end) {
						int32_t idx = (lt_cursor + dith) >> 9;
						dith = (lt_cursor + dith) & 0x1FF;
						*vga_dst++ = lo_base[idx];
						*vga_dst++ = hi_base[idx];
					}
				}
			} else if (dlt) {
				uint16_t saved;
				int32_t dith;

				if (dx_span)
					dlt /= dx_span;
				if (left_x < startx_mod_54)
					lt_cursor = dlt * (startx_mod_54 - left_x) + inv_left_lt;

				/* The inner-loop index reaches 16 at the brightest end of
				 * the ramp; shade[16] reads as shade[15] for the run. */
				saved = shade[16];
#ifdef TIE_MODERN
				/* c == 45 would write one past materialcolors; clamp
				 * the index instead. */
				for (dith = (currentypos & 1) << 10; vga_dst < vga_end; ++vga_dst) {
					int32_t comb = lt_cursor + dith;
					lt_cursor += dlt;
					dith = comb & 0x7FF;
					*vga_dst = shade[(comb >> 11) > 15 ? 15 : comb >> 11];
				}
#else
				shade[16] = shade[15];
				for (dith = (currentypos & 1) << 10; vga_dst < vga_end; ++vga_dst) {
					int32_t comb = lt_cursor + dith;
					lt_cursor += dlt;
					dith = comb & 0x7FF;
					*vga_dst = shade[comb >> 11];
				}
				shade[16] = (uint8_t)saved;
#endif
			} else if ((inv_left_lt >> 11) == 15 || (inv_left_lt & 0x7FF) == 0) {
				uint8_t fill = shade[lt_cursor >> 11];
				while (vga_dst < vga_end)
					*vga_dst++ = fill;
			} else {
				int32_t dith = odd_row << 10;
				while (vga_dst < vga_end) {
					*vga_dst++ = shade[(lt_cursor + dith) >> 11];
					dith = (lt_cursor + dith) & 0x7FF;
				}
			}
			startx_mod_54 = endx;
			return;
		}
	}

	/* --- Solid-fill / background path. */
	{
		int run_span = endx - startx_mod_54;
		uint8_t* base = xtrans2_videobaseptr + videoypos + startx_mod_54;

		if (bytesPerPixel == 2) {
			uint8_t* dst = base + startx_mod_54;
			if (c) {
				uint8_t* pal = &rtsvga2_vgapalette[3 * c];
				uint16_t rgb565 = ((pal[0] >> 1) << 11) | ((pal[1]) << 5) | ((pal[2] >> 1));
				uint8_t* end2 = dst + 2 * (int16_t)run_span;
				while (dst < end2) {
					*(uint16_t*)dst = rgb565;
					dst += 2;
				}
			} else if (deepspacecolor) {
				uint8_t* end2 = dst + 2 * (int16_t)run_span;
				uint8_t* src = logbufbaseptr + logbufypos + 2 * startx_mod_54;
				while (dst < end2) {
					/* Rotate 2 bytes left by 6 — mimics the Watcom
					 * __ROL2__ used to remap a 1bpp logbuf byte into
					 * its 5-6-5 equivalent. */
					uint16_t v = *(uint16_t*)src;
					*(uint16_t*)dst = (uint16_t)((v << 6) | (v >> 10));
					dst += 2;
					src += 2;
				}
			}
		} else {
			if (c) {
				memset(base, c, (int16_t)run_span);
			} else if (deepspacecolor) {
				uint8_t* end1 = base + (int16_t)run_span;
				uint8_t* src = logbufbaseptr + logbufypos + startx_mod_54;
				while (base < end1)
					*base++ = *src++;
			}
		}
	}

	startx_mod_54 = endx;
}

/* ============================================================================
 * xtrans2_findnearest
 * ----------------------------------------------------------------------------
 * Pop the frontmost object from the heap. Walks top-down pairing each
 * entry through getinfront; the winner is removed and returned.
 *
 * Returns 0 if the heap is empty.
 * ========================================================================== */
// FUNCTION: TIE95 0x64668
uint16_t xtrans2_findnearest(void) {
	uint16_t i;
	uint16_t result;

	if (lastheap == 0)
		return 0;

	result = objheap[lastheap];
	for (i = lastheap - 1; i != 0; --i)
		result = xtrans2_getinfront(result, objheap[i]);

	i = objflag[result];
	if (i != lastheap) {
		objheap[i] = objheap[lastheap];
		objflag[objheap[i]] = (uint8_t)i;
		popflag = 0;
	} else if (popflag > 0) {
		--popflag;
	}

	--lastheap;
	return result;
}

/* ============================================================================
 * xtrans2_getinfront
 * ----------------------------------------------------------------------------
 * Depth-compare two object ids and return the one that should render in
 * front. Four branches:
 *   1. Either id == 128 (flat-poly sentinel): the other wins.
 *   2. At least one id is a flat-poly marker (>=0x80, except 128): mixed
 *      mesh-vs-flat compare using flatx/flaty/flatz vs the mesh bbox.
 *   3. Two meshes with the same parent_category: smaller id wins.
 *   4. Two meshes: check face_covers cache, then six bbox axes; fall back
 *      to draw_polydepthsort on a full bbox overlap.
 * ========================================================================== */
// FUNCTION: TIE95 0x64720
uint16_t xtrans2_getinfront(uint16_t obj_a, uint16_t obj_b) {
	xtrans2_ObjectRecord* ra;
	xtrans2_ObjectRecord* rb;
	xtrans2_ObjectRecord* rec;
	uint8_t* a_face;
	uint8_t* b_face;
	uint16_t mesh_obj;
	uint16_t flat_idx;

	if (obj_a == 128)
		return obj_b;
	if (obj_b == 128)
		return obj_a;

	if (obj_a < 0x80 && obj_b < 0x80) {
		/* Two meshes. */
		ra = (xtrans2_ObjectRecord*)((uint8_t*)xtransdataptr + objectptrs[obj_a]);
		rb = (xtrans2_ObjectRecord*)((uint8_t*)xtransdataptr + objectptrs[obj_b]);

		if (ra->parent_category == rb->parent_category) {
			if (obj_a < obj_b)
				return obj_a;
			return obj_b;
		}

		/* face_covers cache: ids whose bbox is known to be occluded by
		 * this one. */
		if (obj_a == rb->face_covers[0])
			return obj_b;
		if (obj_a == rb->face_covers[1])
			return obj_b;
		if (obj_a == rb->face_covers[2])
			return obj_b;
		if (obj_a == rb->face_covers[3])
			return obj_b;
		if (obj_a == rb->face_covers[4])
			return obj_b;
		if (ra->face_covers[0] == obj_b)
			return obj_a;
		if (ra->face_covers[1] == obj_b)
			return obj_a;
		if (ra->face_covers[2] == obj_b)
			return obj_a;
		if (ra->face_covers[3] == obj_b)
			return obj_a;
		if (ra->face_covers[4] == obj_b)
			return obj_a;

		/* Six axis-aligned separating-plane tests. */
		if (rb->bbox_xmax <= ra->bbox_xmin) {
			if (ra->bbox_xmin < 0)
				return obj_a;
			if (rb->bbox_xmax >= 0)
				return obj_b;
		}
		if (rb->bbox_xmin >= ra->bbox_xmax) {
			if (ra->bbox_xmax >= 0)
				return obj_a;
			if (rb->bbox_xmin < 0)
				return obj_b;
		}
		if (rb->bbox_ymax <= ra->bbox_ymin) {
			if (ra->bbox_ymin < 0)
				return obj_a;
			if (rb->bbox_ymax >= 0)
				return obj_b;
		}
		if (rb->bbox_ymin >= ra->bbox_ymax) {
			if (ra->bbox_ymax >= 0)
				return obj_a;
			if (rb->bbox_ymin < 0)
				return obj_b;
		}
		if (rb->bbox_zmax <= ra->bbox_zmin) {
			if (ra->bbox_zmin < 0)
				return obj_a;
			if (rb->bbox_zmax >= 0)
				return obj_b;
		}
		if (rb->bbox_zmin >= ra->bbox_zmax) {
			if (ra->bbox_zmax >= 0)
				return obj_a;
			if (rb->bbox_zmin < 0)
				return obj_b;
		}

		/* Tie: fall back to per-polygon depth sort.
		 * NOTE: binary reads at offset (obj + 0x217 + 2*facenum), one byte
		 * before face_flags (declared at +0x218): 1-indexed facenumber N
		 * maps to face_flags[2*N - 1], the [1] byte of face N-1. Processedge
		 * writes via the same base, so they stay coherent. */
		a_face = &ra->face_flags[2 * objectminface[obj_a] - 1];
		b_face = &rb->face_flags[2 * objectminface[obj_b] - 1];
		if (draw_polydepthsort(*a_face, obj_a, ra->parent_category, ra->obj_id_field, *b_face, obj_b,
							   rb->parent_category, rb->obj_id_field) == obj_a)
			return obj_a;
		return obj_b;
	}

	if (obj_a > 0x80) {
		if (obj_b > 0x80) {
			/* Two flat-polys. objflag[128] marks a recent flat-poly
			 * "demoted" by openobject — when set, flatparentobj's 0x1000
			 * sentinel overrides the natural id ordering. */
			if (objflag[128]) {
				if (flatparentobj[obj_b - 128] == 0x1000)
					return obj_a;
				if (flatparentobj[obj_a - 128] == 0x1000)
					return obj_b;
			}
			if (obj_a >= obj_b)
				return obj_a;
			return obj_b;
		}
		flat_idx = obj_a - 128;
		mesh_obj = obj_b;
	} else if (obj_b > 0x80) {
		flat_idx = obj_b - 128;
		mesh_obj = obj_a;
	}

	/* flatz of INT16_MIN means "unresolved" — mesh wins. */
	if (flatz[flat_idx] == (int16_t)0x8000)
		return mesh_obj;

	rec = (xtrans2_ObjectRecord*)((uint8_t*)xtransdataptr + objectptrs[mesh_obj]);

	/* High byte of parent_category is the mesh's category flag. */
	if (objflag[128] && (rec->parent_category & 0xFF00) == 0x1000)
		return flat_idx + 128;

	if (flatz[flat_idx] == (int16_t)0x8001)
		return mesh_obj;

	/* Flat belongs to this parent mesh: component index decides. */
	if (flatparentobj[flat_idx] == rec->parent_category) {
		if (mesh_obj < flatcomponentnum[flat_idx])
			return mesh_obj;
		return flat_idx + 128;
	}

	/* Axis-aligned containment tests against the mesh bbox. */
	if (flatx[flat_idx] <= rec->bbox_xmin) {
		if (rec->bbox_xmin < 0)
			return mesh_obj;
		return flat_idx + 128;
	}
	if (flatx[flat_idx] >= rec->bbox_xmax) {
		if (rec->bbox_xmax < 0)
			return flat_idx + 128;
		return mesh_obj;
	}
	if (flaty[flat_idx] <= rec->bbox_ymin) {
		if (rec->bbox_ymin < 0)
			return mesh_obj;
		return flat_idx + 128;
	}
	if (flaty[flat_idx] >= rec->bbox_ymax) {
		if (rec->bbox_ymax < 0)
			return flat_idx + 128;
		return mesh_obj;
	}
	if (flatz[flat_idx] <= rec->bbox_zmin) {
		if (rec->bbox_zmin < 0)
			return mesh_obj;
		return flat_idx + 128;
	}
	if (flatz[flat_idx] >= rec->bbox_zmax) {
		if (rec->bbox_zmax < 0)
			return flat_idx + 128;
		return mesh_obj;
	}
	return flat_idx + 128;
}
