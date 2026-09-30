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
int16_t maskbufptr = (int16_t)0xC000;

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
int32_t* edgept1[256];
// GLOBAL: TIE95 0xEC210
int32_t* edgept2[256];
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
uint8_t dithercolors[9984];
// GLOBAL: TIE95 0xF0910
uint8_t materialrgbhi[2496];
// GLOBAL: TIE95 0xF12D0
uint8_t materialrgblo[2496];
// GLOBAL: TIE95 0xECA10
uint8_t starhashtable[2048];
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
void* tempptr;
// GLOBAL: TIE95 0xF4FDC
void* currptr;
// GLOBAL: TIE95 0xF4FE0
void* currptr2;
// GLOBAL: TIE95 0xF4FE4
void* currentedgeptr;
// GLOBAL: TIE95 0xF4FE8
void* lastptr;
// GLOBAL: TIE95 0xF4FEC
void* headerlist;

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
	int mat_base, step;
	int dither_out_base;
	int out_off;
	int row;

	if (!xtrans2_dithercolorinitflag) {
		dither_out_base = 0;
		/* 39 materials × 16 steps × 16 bytes = 9984. */
		for (mat_base = 0; mat_base != 624; mat_base += 16) {
			int mat_idx = mat_base;
			int dither_off = dither_out_base;
			for (step = 0; step < 16; ++step) {
				uint8_t cur_col = materialcolors[mat_idx];
				uint8_t nxt_col = (step == 15) ? materialcolors[mat_idx] : materialcolors[mat_idx + 1];
				uint32_t c = cur_col;
				uint32_t c_sh8 = (uint32_t)cur_col << 8;

				/* The 4 permutations of (cur,nxt) bytes packed into
				 * one dword per permutation. */
				*(uint32_t*)&dithercolors[dither_off + 0] = c | ((c | ((c | c_sh8) << 8)) << 8);
				*(uint32_t*)&dithercolors[dither_off + 4] = c | ((nxt_col | ((c | c_sh8) << 8)) << 8);
				*(uint32_t*)&dithercolors[dither_off + 8] = nxt_col | ((c | ((nxt_col | c_sh8) << 8)) << 8);
				*(uint32_t*)&dithercolors[dither_off + 12] =
					c | ((nxt_col | ((c | ((uint32_t)nxt_col << 8)) << 8)) << 8);

				++mat_idx;
				dither_off += 16;
			}
			dither_out_base += 256;
		}
		xtrans2_dithercolorinitflag = 1;
	}

	if (!xtrans2_materialrgbinitflag) {
		int mat_offs = 0;
		int rgb_out_base = 0;
		do {
			/* Read current material's palette entry (RGB 0..63) and pack
			 * into RGB565. */
			uint8_t* pal_cur = &rtsvga2_vgapalette[3 * materialcolors[mat_offs]];
			uint16_t rgb565_cur = ((pal_cur[0] >> 1) << 11) | ((pal_cur[1]) << 5) | ((pal_cur[2] >> 1));

			/* And the neighbour material (offset +15 in the table = next
			 * ramp base). */
			uint8_t* pal_nxt = &rtsvga2_vgapalette[3 * materialcolors[mat_offs + 15]];
			uint16_t rgb565_nxt = ((pal_nxt[0] >> 1) << 11) | ((pal_nxt[1]) << 5) | ((pal_nxt[2] >> 1));

			uint8_t r_cur = (rgb565_cur >> 11) & 0x1F;
			uint8_t g_cur = (rgb565_cur >> 5) & 0x3F;
			uint8_t b_cur2 = 2 * (rgb565_cur & 0x1F);
			uint8_t r_cur2 = 2 * r_cur;

			uint8_t r_nxt2 = 2 * ((rgb565_nxt >> 11) & 0x1F);
			uint8_t g_nxt = (rgb565_nxt >> 5) & 0x3F;
			uint8_t b_nxt2 = 2 * (rgb565_nxt & 0x1F);

			int blend_step;

			out_off = rgb_out_base;

			for (blend_step = 0; blend_step < 64; ++blend_step) {
				uint8_t g_interp = (uint8_t)(((blend_step * (g_nxt - g_cur)) >> 6) + g_cur);
				uint8_t r_interp = (uint8_t)(((blend_step * (r_nxt2 - r_cur2)) >> 6) + r_cur2);
				uint8_t b_interp = (uint8_t)(((blend_step * (b_nxt2 - b_cur2)) >> 6) + b_cur2);

				uint8_t hi_byte = (uint8_t)(((g_interp >> 3)) + 4 * (r_interp & 0x3E));
				uint8_t lo_byte = (uint8_t)((b_interp >> 1) + 32 * (g_interp & 7));

				materialrgbhi[out_off] = hi_byte;
				materialrgblo[out_off] = lo_byte;
				++out_off;
			}
			mat_offs += 16;
			rgb_out_base += 64;
		} while (mat_offs != 624);
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
 * xtrans2_findnearest
 * ----------------------------------------------------------------------------
 * Pop the frontmost object from the heap. Walks top-down pairing each
 * entry through getinfront; the winner is removed and returned.
 *
 * Returns 0 if the heap is empty.
 * ========================================================================== */
// FUNCTION: TIE95 0x64668
uint16_t xtrans2_findnearest(void) {
	uint16_t new_lastheap = lastheap;
	uint16_t result;

	uint16_t lh;
	uint16_t winner_pos16;

	if (lastheap == 0) {
		lastheap = new_lastheap;
		return 0;
	}

	result = objheap[lastheap];
	if (lastheap != 1) {
		uint16_t i = lastheap - 1;
		do {
			result = xtrans2_getinfront(result, objheap[i]);
			--i;
		} while (i != 0);
	}

	lh = lastheap;
	winner_pos16 = objflag[result]; /* unsigned byte, widened */

	if ((uint8_t)winner_pos16 == (uint8_t)lastheap) {
		if (popflag)
			--popflag;
	} else {
		uint16_t top_obj = objheap[lastheap];
		objheap[objflag[result]] = top_obj;
		popflag = (uint8_t)(winner_pos16 >> 8);
		objflag[top_obj] = (uint8_t)winner_pos16;
	}

	lastheap = (uint16_t)(lh - 1);
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
	int i;
	int16_t b_xmax;
	int16_t a_xmax;
	int16_t b_ymax;
	int16_t a_ymin;
	int16_t b_ymin;
	int16_t a_ymax;
	int16_t b_zmax;
	int16_t a_zmin;
	int16_t b_zmin;
	int16_t a_zmax;
	uint16_t a_face_info;
	uint16_t b_face_info;
	uint16_t winner;

	if (obj_a == 128)
		return obj_b;
	if (obj_b == 128)
		return obj_a;

	if (obj_a >= 0x80 || obj_b >= 0x80) {
		/* Mixed mesh/flat, or two flats, or two 'special' objects. */
		uint16_t mesh_obj;
		uint16_t flat_idx;

		xtrans2_ObjectRecord* rec;
		int16_t fx;

		if (obj_a <= 0x80) {
			if (obj_b > 0x80) {
				mesh_obj = obj_a;
				flat_idx = (uint16_t)(obj_b - 128);
			} else {
				/* Both below 0x80 can't happen here, short path safe. */
				return (obj_a < obj_b) ? obj_a : obj_b;
			}
		} else {
			if (obj_b > 0x80) {
				/* Two flat-polys. */
				/* objflag[128] (= byte_204BC8 in the binary) marks a
				 * recent flat-poly "demoted" by openobject — when set,
				 * consult flatparentobj[obj-128] for the 0x1000 sentinel
				 * that overrides the natural id ordering. */
				if (objflag[128]) {
					if (flatparentobj[obj_b - 128] == 4096)
						return obj_a;
					if (flatparentobj[obj_a - 128] == 4096)
						return obj_b;
				}
				return (obj_a < obj_b) ? obj_b : obj_a;
			}
			mesh_obj = obj_b;
			flat_idx = (uint16_t)(obj_a - 128);
		}

		/* flatz of INT16_MIN means "unresolved" — mesh wins. */
		if (flatz[flat_idx] == (int16_t)0x8000)
			return mesh_obj;

		rec = (xtrans2_ObjectRecord*)((uint8_t*)xtransdataptr + objectptrs[mesh_obj]);

		if (objflag[128]) {
			/* Upper byte of parent_category is the mesh's category flag;
			 * if masking-out the id-lane gives 0x1000, flat-poly wins. */
			uint16_t cat_masked = (uint16_t)(rec->parent_category & 0xFF00);
			if (cat_masked == 0x1000)
				return (uint16_t)(flat_idx + 128);
		}

		if (flatz[flat_idx] == -32767)
			return mesh_obj;

		/* Flat belongs to this parent mesh → component index decides. */
		if (flatparentobj[flat_idx] == rec->parent_category) {
			if (mesh_obj < (uint16_t)flatcomponentnum[flat_idx])
				return mesh_obj;
			return (uint16_t)(flat_idx + 128);
		}

		/* Axis-aligned containment tests against the mesh bbox. */
		fx = flatx[flat_idx];
		if (fx > rec->bbox_xmin) {
			int16_t xmax = rec->bbox_xmax;
			if (fx < xmax) {
				int16_t fy = flaty[flat_idx];
				int16_t ymin = rec->bbox_ymin;
				if (fy > ymin) {
					int16_t ymax = rec->bbox_ymax;
					if (fy < ymax) {
						int16_t zmin = rec->bbox_zmin;
						if (flatz[flat_idx] > zmin) {
							int16_t zmax = rec->bbox_zmax;
							if (flatz[flat_idx] < zmax || zmax < 0)
								return (uint16_t)(flat_idx + 128);
						} else if (zmin >= 0) {
							return (uint16_t)(flat_idx + 128);
						}
						return mesh_obj;
					}
					if (ymax < 0)
						return (uint16_t)(flat_idx + 128);
				} else if (ymin >= 0) {
					return (uint16_t)(flat_idx + 128);
				}
				return mesh_obj;
			}
			if (xmax >= 0)
				return mesh_obj;
		} else if (rec->bbox_xmin < 0) {
			return mesh_obj;
		}
		return (uint16_t)(flat_idx + 128);
	}

	/* Two meshes. */
	ra = (xtrans2_ObjectRecord*)((uint8_t*)xtransdataptr + objectptrs[obj_a]);
	rb = (xtrans2_ObjectRecord*)((uint8_t*)xtransdataptr + objectptrs[obj_b]);

	if (ra->parent_category == rb->parent_category)
		return (obj_a < obj_b) ? obj_a : obj_b;

	/* face_covers cache: a 5-slot list of ids whose bbox is known to
	 * entirely occlude / be occluded by this one. */
	for (i = 0; i < 5; ++i)
		if ((uint16_t)rb->face_covers[i] == obj_a)
			return obj_b;
	for (i = 0; i < 5; ++i)
		if ((uint16_t)ra->face_covers[i] == obj_b)
			return obj_a;

	/* Six axis-aligned separating-plane tests. */
	b_xmax = rb->bbox_xmax;
	if (b_xmax <= ra->bbox_xmin) {
		if (ra->bbox_xmin < 0)
			return obj_a;
		if (b_xmax >= 0)
			return obj_b;
	}
	a_xmax = ra->bbox_xmax;
	if (rb->bbox_xmin >= a_xmax) {
		if (a_xmax >= 0)
			return obj_a;
		if (rb->bbox_xmin < 0)
			return obj_b;
	}
	b_ymax = rb->bbox_ymax;
	a_ymin = ra->bbox_ymin;
	if (b_ymax <= a_ymin) {
		if (a_ymin < 0)
			return obj_a;
		if (b_ymax >= 0)
			return obj_b;
	}
	b_ymin = rb->bbox_ymin;
	a_ymax = ra->bbox_ymax;
	if (b_ymin >= a_ymax) {
		if (a_ymax >= 0)
			return obj_a;
		if (b_ymin < 0)
			return obj_b;
	}
	b_zmax = rb->bbox_zmax;
	a_zmin = ra->bbox_zmin;
	if (b_zmax <= a_zmin) {
		if (a_zmin < 0)
			return obj_a;
		if (b_zmax >= 0)
			return obj_b;
	}
	b_zmin = rb->bbox_zmin;
	a_zmax = ra->bbox_zmax;
	if (b_zmin >= a_zmax) {
		if (a_zmax >= 0)
			return obj_a;
		if (b_zmin < 0)
			return obj_b;
	}

	/* Tie: fall back to per-polygon depth sort.
	 * NOTE: binary reads at offset (obj + 0x216 + 2*facenum), which is
	 * TWO BYTES BEFORE face_flags (declared at +0x218). The effective slot
	 * for 1-indexed facenumber N is face_flags[2*(N-1)]. Same base-1 trick
	 * as materialcolors. Processedge writes via the same 0x216 base, so
	 * they stay coherent; the read here must use (minface - 1). */
	a_face_info = ra->face_flags[2 * (objectminface[obj_a] - 1) + 1];
	b_face_info = rb->face_flags[2 * (objectminface[obj_b] - 1) + 1];
	winner = draw_polydepthsort(a_face_info, obj_a, ra->obj_id_field, ra->parent_category, obj_a, obj_b,
								b_face_info, obj_b, rb->parent_category, rb->obj_id_field);
	return (winner != obj_a) ? obj_b : winner;
}

/* ============================================================================
 * xtrans2_closeobject
 * ----------------------------------------------------------------------------
 * Remove _objid from the active object heap.
 * ========================================================================== */
// FUNCTION: TIE95 0x63CD0
void xtrans2_closeobject(void) {
	uint32_t removed = objid;
	uint8_t removed_pos = objflag[removed];
	uint16_t cur = curobjid;

	objflag[removed] = 0;

	if (removed == cur) {
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
	if (removed == 128 && cur && cur != 0xFFFF) {
		/* objflag[128] reused to remember the removed flat's heap slot
		 * across the subsequent re-sort (see getinfront usage). */
		objflag[128] = removed_pos;
		objheap[removed_pos] = (uint16_t)removed; /* re-stamp slot */
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
	if (removed_pos == lastheap) {
		if (popflag)
			--popflag;
	} else {
		uint16_t top_obj = objheap[lastheap];
		objheap[removed_pos] = top_obj;
		popflag = 0;
		objflag[top_obj] = removed_pos;
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
	uint16_t cur = curobjid;

	/* Empty heap — first object in. */
	uint32_t new_id;
	uint16_t new_pos;

	if (cur == 0) {
		objflag[0] = 0;
		if (maskflag >= 0)
			xtrans2_outputxt();
		cur = (uint16_t)objid;
		objflag[objid] = 0xFF;
		curobjid = cur;
		return;
	}

	/* Resolve the front via getinfront unless the front is pending. */
	if (cur != 0xFFFF) {
		uint16_t frontmost = xtrans2_getinfront((uint16_t)objid, cur);
		cur = curobjid;
		if (frontmost == (uint16_t)objid) {
			/* Newcomer wins — push old front onto the heap. */
			uint16_t heap_pos = (uint16_t)(lastheap + 1);
			uint8_t saved_popflag = popflag;
			objheap[heap_pos] = curobjid;
			lastheap = heap_pos;
			objflag[cur] = (uint8_t)heap_pos;
			popflag = (uint8_t)(saved_popflag + 1);
			if (maskflag >= 0)
				xtrans2_outputxt();
			objflag[objid] = 0xFF;
			curobjid = (uint16_t)objid;
			return;
		}
	}

	/* Newcomer stays behind: push it onto the heap. */
	new_id = objid;
	new_pos = (uint16_t)(lastheap + 1);
	lastheap = new_pos;
	popflag = 0;
	objflag[new_id] = (uint8_t)new_pos;
	objheap[new_pos] = (uint16_t)new_id;

	/* Special case: flat-poly marker (id == 128) with something already
	 * in front — findnearest + getinfront re-evaluates in case the flat
	 * should demote the current front. */
	if (new_id == 128 && new_pos != 1 && cur != 0xFFFF) {
		uint16_t demoted;
		uint16_t fwin;
		uint16_t pos2;

		curobjid = cur;
		popflag = 0;
		demoted = xtrans2_findnearest();
		objid = demoted;
		fwin = xtrans2_getinfront(demoted, curobjid);
		cur = curobjid;
		pos2 = (uint16_t)(lastheap + 1);
		if (fwin != (uint16_t)objid) {
			++lastheap;
			objflag[objid] = (uint8_t)pos2;
			objheap[pos2] = (uint16_t)objid;
			curobjid = cur;
			return;
		}
		++lastheap;
		objheap[pos2] = curobjid;
		objflag[cur] = (uint8_t)lastheap;
		if (maskflag >= 0)
			xtrans2_outputxt();
		objflag[objid] = 0xFF;
		curobjid = (uint16_t)objid;
		return;
	}

	curobjid = cur;
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
	trace2_EdgeHeader* right_edge = (trace2_EdgeHeader*)currptr2;

	uint8_t c;
	int solid_fill;

	if (endx <= startx_mod_54) {
		currptr2 = right_edge;
		return;
	}

	c = 0;
	solid_fill = 1;

	if (curobjid == 0) {
		c = deepspacecolor;
	} else if (curobjid >= 0x80) {
		/* byte_20A2C8 in the binary = flatcolors - 128. */
		c = flatcolors[curobjid - 128];
	} else {
		xtrans2_ObjectRecord* rec = (xtrans2_ObjectRecord*)((uint8_t*)xtransdataptr + objectptrs[curobjid]);
		/* Binary reads at offset (obj + 0x216 + 2*minface). face_flags is
		 * declared at +0x218, so the actual slot for 1-indexed facenumber
		 * N is face_flags[2*(N-1)]. Matches the 0x216-based write in
		 * xtrans2_processedge's marking-swap logic. */
		c = rec->face_flags[2 * (objectminface[curobjid] - 1)];
		if (c < 0x40)
			solid_fill = 0;
	}

	if (!solid_fill) {
		/* --- Gouraud path. */
		trace2_EdgeHeader* left_edge = (trace2_EdgeHeader*)objectminedgeptr[curobjid];

		int32_t right_lt, right_x;

		int32_t left_x_fixed;
		int32_t left_lt_raw;
		int32_t dx_span;
		int32_t left_x;
		int32_t inv_left_lt;
		int32_t lt_cursor;
		int32_t dlt;
		uint8_t* vga_dst;
		uint8_t* vga_end;
		int32_t odd_row;

		if (curobjid == (uint16_t)objid &&
			(objectminface[curobjid] == face1 || objectminface[curobjid] == face2)) {
			right_lt = newlt;
			right_x = newx;
		} else {
			right_edge = left_edge->rightedge;
			if (right_edge && !right_edge->numscanlines)
				right_edge = NULL;
			if (!right_edge) {
				for (right_edge = (trace2_EdgeHeader*)currentedgeptr; right_edge;
					 right_edge = right_edge->next) {
					uint32_t chain_obj = right_edge->objectid;
					if (curobjid == chain_obj) {
						uint32_t mf = objectminface[chain_obj];
						if (mf == right_edge->face1 || mf == right_edge->face2)
							break;
					}
				}
				left_edge->rightedge = right_edge;
			}
			if (right_edge) {
				right_lt = right_edge->info->lt >> 1;
				right_x = right_edge->info->x >> 8;
			} else {
				right_x = screenXRes;
				right_lt = left_edge->info->lt >> 1;
			}
		}

		left_x_fixed = left_edge->info->x;
		left_lt_raw = left_edge->info->lt;
		dx_span = right_x - (left_x_fixed >> 8);
		left_x = left_x_fixed >> 8;
		inv_left_lt = (63 - ((left_lt_raw >> 9) & 0x3F)) << 9;
		lt_cursor = inv_left_lt;
		dlt = ((63 - ((right_lt >> 8) & 0x3F)) << 9) - inv_left_lt;

		vga_dst = xtrans2_videobaseptr + videoypos + startx_mod_54;
		vga_end = xtrans2_videobaseptr + videoypos + endx;
		odd_row = currentypos & 1;

#if 0
		/* PIP-bounds probe: log any run that escapes [0, pixelswide) on x
		 * or strays into the bottom-of-cockpit area. Rate-limited via a
		 * static counter so a long bad frame doesn't flood stderr. */
		{
			static int dbg_oob = 0;
			if (dbg_oob < 20) {
				if (startx_mod_54 < 0 || endx > (int32_t)pixelswide ||
				    endx < startx_mod_54) {
					dbg_oob++;
					TieDiagnostics_Log(TIE_LOG_INFO,
					        "[xt-oob] cy=%d videoypos=%u "
					        "startx=%d endx=%d (pixelswide=%u pixelsdeep=%u)\n",
					        (int)currentypos, videoypos,
					        (int)startx_mod_54, (int)endx,
					        (unsigned)pixelswide, (unsigned)pixelsdeep);
				}
			}
		}
#endif

		if (bytesPerPixel == 2) {
			int rgb_base = (int)c * 64 - 64;
			uint8_t* lo_base = &materialrgblo[rgb_base];
			uint8_t* hi_base = &materialrgbhi[rgb_base];
			uint8_t* vp = vga_dst + startx_mod_54;
			uint8_t* ve = vga_end + endx;

			if (dlt) {
				uint8_t saved_lo;
				uint8_t saved_hi;
				int32_t dith;

				if (dx_span)
					dlt /= dx_span;
				if (left_x < startx_mod_54)
					lt_cursor = dlt * (startx_mod_54 - left_x) + inv_left_lt;

				/* Guard against a 64-step index spill: the inner loop
				 * can compute index 64 when lt_cursor exits at the top
				 * step; stash slot 63 into slot 64 for the run. */
				saved_lo = lo_base[64];
				saved_hi = hi_base[64];
				lo_base[64] = lo_base[63];
				hi_base[64] = hi_base[63];

				dith = odd_row << 8;
				while (vp < ve) {
					int32_t idx = (lt_cursor + dith) >> 9;
					dith = (uint16_t)(lt_cursor + dith) & 0x1FF;
					vp[0] = lo_base[idx];
					vp[1] = hi_base[idx];
					vp += 2;
					lt_cursor += dlt;
				}
				lo_base[64] = saved_lo;
				hi_base[64] = saved_hi;
			} else if (((inv_left_lt >> 9) != 63) && ((inv_left_lt & 0x1FF) != 0)) {
				int32_t dith = odd_row << 8;
				while (vp < ve) {
					int32_t idx = (lt_cursor + dith) >> 9;
					dith = (uint16_t)(lt_cursor + dith) & 0x1FF;
					vp[0] = lo_base[idx];
					vp[1] = hi_base[idx];
					vp += 2;
				}
			} else {
				uint8_t pix_lo = lo_base[inv_left_lt >> 9];
				uint8_t pix_hi = hi_base[inv_left_lt >> 9];
				while (vp < ve) {
					vp[0] = pix_lo;
					vp[1] = pix_hi;
					vp += 2;
				}
			}
		} else {
			/* 1 byte per pixel. */
			uint8_t* shade = &materialcolors[16 * c - 16];

			if (dlt) {
				int32_t dith;

				if (dx_span)
					dlt /= dx_span;
				if (left_x < startx_mod_54)
					lt_cursor = dlt * (startx_mod_54 - left_x) + inv_left_lt;
				currptr2 = right_edge;

				/* Inner-loop index `comb >> 11` lands in [0, 16] -- the
				 * upper bound of 16 is reached when lt_cursor sits at the
				 * brightest end of its range (inv_left_lt = 63<<9) plus
				 * a maxed dither accumulator. The binary handles this
				 * with a save/modify/restore on materialcolors[16*c] so
				 * shade[16] reads as shade[15] for the duration of the
				 * loop. We achieve the same per-pixel result by clamping
				 * the index here -- avoids the 1-byte OOB write when
				 * c == 45 (the brightest highlight-remap output). */
				dith = odd_row << 10;
				while (vga_dst < vga_end) {
					int32_t comb = lt_cursor + dith;
					int32_t idx;

					dith = comb & 0x7FF;
					idx = comb >> 11;
					*vga_dst = shade[idx > 15 ? 15 : idx];
					++vga_dst;
					lt_cursor += dlt;
				}
				right_edge = (trace2_EdgeHeader*)currptr2;
			} else if (((inv_left_lt >> 11) != 15) && ((inv_left_lt & 0x7FF) != 0)) {
				int32_t dith = odd_row << 10;
				while (vga_dst < vga_end) {
					*vga_dst = shade[(lt_cursor + dith) >> 11];
					dith = (uint16_t)(lt_cursor + dith) & 0x7FF;
					++vga_dst;
				}
			} else {
				uint8_t fill = shade[inv_left_lt >> 11];
				while (vga_dst < vga_end) {
					*vga_dst++ = fill;
				}
			}
		}
	} else {
		/* --- Solid-fill / background path. */
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
	currptr2 = right_edge;
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
	/* Running locals mirroring the decompiler's register spills for
	 * tempptr / currptr / maskptr. These are written back to the matching
	 * globals at points where callees read them. */
	void* saved_tempptr;
	void* saved_currptr;
	uint8_t* mask_cursor;
	uint32_t vesa_page;
	int32_t delta;

	headerlist = NULL;
	lastheap = 0;
	vesa_page = ((uint32_t)screenMemWidth * displaycorner_lines + displaycorner_columns) / vesa_page_size;
	videoypos = ((uint32_t)screenMemWidth * displaycorner_lines + displaycorner_columns) % vesa_page_size;
	rtsvga2_setcurrentpage(vesa_window, (uint16_t)vesa_page);
	numlastrow = 0;
	logbufbaseptr = (uint8_t*)buffer_ptr;
	markcnt = 0;
	logbufypos = 0;
	xtflagvalue = 0;
	memset(objflag, 0, 256);
	memset(objectcount, 0, 128);
	currentypos = 0;

	mask_cursor = (uint8_t*)xtransdataptr + (uint16_t)maskbufptr;

	while ((int32_t)pixelsdeep > currentypos) {
		uint8_t* mask_cur;
		trace2_EdgeHeader* row_edge;
		int8_t mask_first;
		int32_t cur_x;
		int done_flag;
		int row_done;

		maskptr = mask_cursor;

		if (videoypos >= vesa_page_size) {
			++vesa_page;
			videoypos -= vesa_page_size;
			rtsvga2_setcurrentpage(vesa_window, (uint16_t)vesa_page);
		}

		saved_tempptr = tempptr;
		saved_currptr = NULL; /* matches "currptr = row_edge" init */
		mask_cur = maskptr;

		/* --- 2. Age active-edge list. */
		{
			trace2_EdgeHeader* walk = (trace2_EdgeHeader*)headerlist;
			/* Drop dead head(s). */
			while (walk && --walk->numscanlines == 0) {
				walk = walk->next;
				headerlist = walk;
			}
			if (walk) {
				trace2_EdgeHeader* nxt;

				/* Advance head's info ptr by one. */
				++walk->info;
				headerlist = walk;
				lastptr = walk;
				nxt = walk->next;
				while (nxt) {
					if (--nxt->numscanlines) {
						++nxt->info;
						lastptr = nxt;
						nxt = nxt->next;
					} else {
						nxt = nxt->next;
						((trace2_EdgeHeader*)lastptr)->next = nxt;
					}
				}
			}
		}

		/* --- 3. Bubble-sort remaining edges by info->x. */
		if (headerlist && ((trace2_EdgeHeader*)headerlist)->next) {
			int swapped;
			do {
				trace2_EdgeHeader* sort_cur;

				swapped = 0;
				saved_tempptr = NULL;
				sort_cur = ((trace2_EdgeHeader*)headerlist)->next;
				lastptr = (trace2_EdgeHeader*)headerlist;
				while (sort_cur) {
					trace2_EdgeHeader* sort_last = (trace2_EdgeHeader*)lastptr;
					trace2_EdgeHeader* sort_save = sort_cur;

					if (sort_cur->info->x >= ((trace2_EdgeHeader*)lastptr)->info->x) {
						sort_cur = sort_cur->next;
						saved_tempptr = lastptr;
						lastptr = ((trace2_EdgeHeader*)lastptr)->next;
					} else {
						swapped = 1;
						if (saved_tempptr) {
							((trace2_EdgeHeader*)saved_tempptr)->next = sort_cur;
							sort_last->next = sort_cur->next;
							sort_cur->next = sort_last;
							saved_tempptr = sort_cur;
							sort_cur = sort_last->next;
						} else {
							trace2_EdgeHeader* nxt;

							headerlist = sort_cur;
							nxt = sort_cur->next;
							saved_tempptr = sort_cur;
							((trace2_EdgeHeader*)lastptr)->next = nxt;
							sort_cur = nxt;
							sort_save->next = sort_last;
						}
					}
				}
			} while (swapped);
		}

		/* --- 4. Merge rowheaders[y] in sorted order. */
		row_edge = trace2_rowheaders[currentypos];
		while (row_edge) {
			trace2_EdgeHeader* rcur = (trace2_EdgeHeader*)headerlist;
			lastptr = NULL;

			if (headerlist) {
				trace2_EdgeHeader* re_save = row_edge;
				trace2_EdgeHeader* after;

				while (rcur && rcur->info->x < row_edge->info->x) {
					lastptr = rcur;
					rcur = rcur->next;
				}

				if (rcur) {
					if (lastptr) {
						after = ((trace2_EdgeHeader*)lastptr)->next;
						((trace2_EdgeHeader*)lastptr)->next = row_edge;
						rcur = row_edge;
					} else {
						after = (trace2_EdgeHeader*)headerlist;
						headerlist = row_edge;
					}
					row_edge = row_edge->next;
					re_save->next = after;
					continue;
				}
			}
			if (!rcur) {
				trace2_EdgeHeader* tmp = row_edge;
				if (lastptr)
					((trace2_EdgeHeader*)lastptr)->next = row_edge;
				else
					headerlist = row_edge;
				row_edge = row_edge->next;
				tmp->next = NULL;
			}
		}

		/* --- 5. Read first mask run for this scanline. */
		mask_first = (int8_t)*mask_cur;
		lastheap = 0;
		curobjid = 0;
		popflag = 0;
		maskflag = mask_first;
		mask_cursor = mask_cur + 1;
		delta = *mask_cursor++;
		if (delta == 0) {
			delta = *mask_cursor++;
			if (delta == 0)
				delta = *mask_cursor++ + 511;
			else
				delta += 255;
		}
		cur_x = delta;
		done_flag = 0;
		row_done = 0;

		if (maskflag >= 0 || (startx_mod_54 = cur_x, (int32_t)pixelswide > cur_x)) {
			/* --- Left-edge skip: advance past runs entirely to the left
			 * of headerlist's first active edge's x. */
			if (headerlist) {
				int32_t left_val;

				runx = ((trace2_EdgeHeader*)headerlist)->info->x >> 8;
				left_val = leftside[currentypos];
				if (runx <= left_val || (int32_t)pixelswide <= left_val) {
					if ((int32_t)pixelswide < runx)
						runx = pixelswide;
					if (runx < 0)
						runx = 0;
				} else {
					startx_mod_54 = left_val;
					/* Skip runs ending before left_val. */
					if (left_val >= cur_x) {
						do {
							maskflag = (int8_t)-maskflag;
							delta = *mask_cursor++;
							if (delta == 0) {
								delta = *mask_cursor++;
								if (delta == 0)
									delta = *mask_cursor++ + 511;
								else
									delta += 255;
							}
							cur_x += delta;
						} while (startx_mod_54 >= cur_x);
					}
					if (maskflag < 0) {
						startx_mod_54 = cur_x;
						if ((int32_t)pixelswide <= cur_x)
							row_done = 1;
					}
					if (!row_done) {
						/* Emit runs up to runx. */
						while (runx >= cur_x) {
							if (cur_x >= (int32_t)pixelswide) {
								runx = pixelswide;
								break;
							}
							maskptr = mask_cursor;
							currptr = row_edge;
							tempptr = saved_tempptr;
							if (maskflag >= 0) {
								endx = cur_x;
								maskx = cur_x;
								xtrans2_outputxt();
								cur_x = maskx;
							} else {
								startx_mod_54 = cur_x;
							}
							saved_tempptr = tempptr;
							maskflag = (int8_t)-maskflag;
							row_edge = (trace2_EdgeHeader*)currptr;
							mask_cursor = maskptr;
							delta = *mask_cursor++;
							if (delta == 0) {
								delta = *mask_cursor++;
								if (delta == 0)
									delta = *mask_cursor++ + 511;
								else
									delta += 255;
							}
							cur_x += delta;
						}
						maskptr = mask_cursor;
						maskx = cur_x;
						currptr = row_edge;
						tempptr = saved_tempptr;
						if (maskflag >= 0) {
							endx = runx;
							xtrans2_outputxt();
						} else {
							startx_mod_54 = cur_x;
						}
						saved_tempptr = tempptr;
						row_edge = (trace2_EdgeHeader*)currptr;
						cur_x = maskx;
						mask_cursor = maskptr;
						if ((int32_t)pixelswide <= startx_mod_54)
							row_done = 1;
					}
				}
				if (!row_done)
					leftside[currentypos] = runx;
			}

			if (!row_done) {
				/* --- Walk past any runs <= leftside[currentypos] (bait-catch
				 * for the first headerless scan). */
				for (startx_mod_54 = leftside[currentypos]; startx_mod_54 >= cur_x;) {
					maskflag = (int8_t)-maskflag;
					delta = *mask_cursor++;
					if (delta == 0) {
						delta = *mask_cursor++;
						if (delta == 0)
							delta = *mask_cursor++ + 511;
						else
							delta += 255;
					}
					cur_x += delta;
				}

				if (maskflag >= 0 || (startx_mod_54 = cur_x, (int32_t)pixelswide > cur_x)) {
					int32_t cur_mx2;

					maskptr = mask_cursor;
					maskx = cur_x;
					currptr = row_edge;
					tempptr = saved_tempptr;
					done_flag = 0;

					/* --- 6a. Main per-edge loop. */
					for (currentedgeptr = (trace2_EdgeHeader*)headerlist; currentedgeptr;
						 currentedgeptr = ((trace2_EdgeHeader*)currentedgeptr)->next) {
						trace2_EdgeHeader* ce = (trace2_EdgeHeader*)currentedgeptr;
						int32_t left_x;
						int32_t left_lt;
						int32_t cur_mx;

						objid = ce->objectid;
						edgeid = ce->edgeid;
						face1 = ce->face1;
						face2 = ce->face2;

						left_x = ce->info->x;
						left_lt = ce->info->lt;
						endx = left_x >> 8;
						newx = endx;
						newlt = left_lt >> 1;
						cur_mx = maskx;
						runx = endx;

						/* Catch up mask stream to the edge's x. */
						while (runx >= cur_mx) {
							maskptr = mask_cursor;
							currptr = row_edge;
							tempptr = saved_tempptr;
							maskflag = (int8_t)-maskflag;
							if (maskflag >= 0) {
								maskx = cur_mx;
								if (curobjid == 0xFFFF)
									curobjid = xtrans2_findnearest();
								objflag[curobjid] = 0xFF;
								saved_tempptr = tempptr;
								delta = *mask_cursor++;
								if (delta == 0) {
									delta = *mask_cursor++;
									if (delta == 0)
										delta = *mask_cursor++ + 511;
									else
										delta += 255;
								}
								cur_mx = delta + maskx;
							} else {
								endx = cur_mx;
								maskx = cur_mx;
								xtrans2_outputxt();
								cur_mx = maskx;
								if (maskx >= (int32_t)pixelswide) {
									done_flag = 1;
									saved_tempptr = tempptr;
									mask_cursor = maskptr;
									break;
								}
								saved_tempptr = tempptr;
								delta = *mask_cursor++;
								if (delta == 0) {
									delta = *mask_cursor++;
									if (delta == 0)
										delta = *mask_cursor++ + 511;
									else
										delta += 255;
								}
								startx_mod_54 += delta;
								cur_mx = delta + maskx;
								if (cur_mx >= (int32_t)pixelswide) {
									done_flag = 1;
									break;
								}
							}
						}
						maskptr = mask_cursor;
						maskx = cur_mx;
						currptr = row_edge;
						tempptr = saved_tempptr;
						if (done_flag)
							break;
						endx = runx;
						xtrans2_processedge();
					}
					saved_tempptr = tempptr;
					saved_currptr = currptr;
					cur_mx2 = maskx;

					/* --- 6b. Tail: scanline end. */
					if (done_flag) {
						rightside[currentypos] = pixelswide;
					} else {
						objid = 0;
						while (1) {
							maskptr = mask_cursor;
							currptr = saved_currptr;
							tempptr = saved_tempptr;
							if (cur_mx2 >= (int32_t)pixelswide)
								break;
							if (maskflag >= 0) {
								maskx = cur_mx2;
								if (curobjid == 0xFFFF)
									curobjid = xtrans2_findnearest();
								cur_mx2 = maskx;
								if (!curobjid)
									break;
								endx = maskx;
								xtrans2_outputxt();
								cur_mx2 = maskx;
							} else {
								startx_mod_54 = cur_mx2;
							}
							saved_tempptr = tempptr;
							maskflag = (int8_t)-maskflag;
							saved_currptr = currptr;
							delta = *mask_cursor++;
							if (delta == 0) {
								delta = *mask_cursor++;
								if (delta == 0)
									delta = *mask_cursor++ + 511;
								else
									delta += 255;
							}
							cur_mx2 += delta;
						}

						if (maskflag < 0) {
							rightside[currentypos] = pixelswide;
							maskx = cur_mx2;
						} else {
							maskx = cur_mx2;
							if (curobjid == 0xFFFF)
								curobjid = xtrans2_findnearest();
							if (curobjid) {
								endx = pixelswide;
								xtrans2_outputxt();
								rightside[currentypos] = pixelswide;
							} else {
								int32_t* rs_p = &rightside[currentypos];
								int32_t runx_snap = *rs_p;
								*rs_p = startx_mod_54;
								while (cur_mx2 < (int32_t)pixelswide) {
									if (maskflag >= 0) {
										maskptr = mask_cursor;
										currptr = saved_currptr;
										tempptr = saved_tempptr;
										if (cur_mx2 >= runx_snap) {
											maskx = cur_mx2;
											if (startx_mod_54 < runx_snap) {
												endx = runx_snap;
												xtrans2_outputxt();
											}
										} else {
											endx = cur_mx2;
											maskx = cur_mx2;
											xtrans2_outputxt();
										}
									} else {
										startx_mod_54 = cur_mx2;
										maskptr = mask_cursor;
										maskx = cur_mx2;
										currptr = saved_currptr;
										tempptr = saved_tempptr;
									}
									saved_tempptr = tempptr;
									saved_currptr = currptr;
									maskflag = (int8_t)-maskflag;
									delta = *mask_cursor++;
									if (delta == 0) {
										delta = *mask_cursor++;
										if (delta == 0)
											delta = *mask_cursor++ + 511;
										else
											delta += 255;
									}
									cur_mx2 = delta + maskx;
								}
								maskptr = mask_cursor;
								maskx = cur_mx2;
								currptr = saved_currptr;
								tempptr = saved_tempptr;
								if (maskflag > 0) {
									if (cur_mx2 >= runx_snap) {
										if (runx_snap > startx_mod_54) {
											endx = runx_snap;
											xtrans2_outputxt();
										}
									} else {
										endx = cur_mx2;
										xtrans2_outputxt();
									}
								}
							}
						}
					}

					saved_tempptr = tempptr;
					row_edge = (trace2_EdgeHeader*)currptr;
					cur_x = maskx;
					mask_cursor = maskptr;

					/* --- 7. Clear marking records dirtied this scanline. */
					if (markcnt) {
						int mark_i;

						markcnt = 0;
						mark_i = nummarks;
						if (nummarks) {
							int mark_i2 = nummarks;
							do {
								if (markingnumber[mark_i]) {
									uint8_t* mp;
									uint8_t cb;
									int j;
									uint8_t* rec;
									uint8_t* swap_src;
									uint8_t tmp;

									markingnumber[mark_i] = 0;
									mp = (uint8_t*)xtransdataptr + markingptr[mark_i2];
									cb = mp[18];
									/* Clear eight words at mp+18..mp+32. */
									for (j = 9; j <= 16; ++j)
										((uint16_t*)mp)[j] = 0;
									rec = (uint8_t*)xtransdataptr + 2 * mp[1] + objectptrs[mp[0]];
									swap_src = &mp[cb];
									tmp = swap_src[1];
									swap_src[1] = rec[534];
									rec[534] = tmp;
								}
								--mark_i2;
								--mark_i;
							} while (mark_i);
						}
					}

					/* Reset curobjid state. */
					if (curobjid != 0xFFFF) {
						objflag[curobjid] = 0;
						if (curobjid < 0x80)
							objectcount[curobjid] = 0;
					}
					/* Drain heap clearing any residual flags. */
					while (lastheap) {
						uint16_t h = objheap[lastheap];
						objflag[h] = 0;
						if (h < 128)
							objectcount[h] = 0;
						--lastheap;
					}
				}
			}
		}

		videoypos += (uint32_t)screenMemWidth;
		logbufypos += bytesPerPixel * pixelswide;
		maskx = cur_x;
		currptr = row_edge;
		tempptr = saved_tempptr;
		++currentypos;
	}

	maskptr = mask_cursor;
}
