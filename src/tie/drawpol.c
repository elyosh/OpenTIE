/*
 * DRAWPOL — polygon-rendering front-end (8 functions).
 *
 * Top of the 3D object pipeline for craft / static objects. Parses the
 * raw .LFD poly_data stream, transforms vertices via TRANSFM2, culls
 * against the view frustum, walks the face list (BSP or flat), computes
 * per-face lighting, and emits faces/edges/markings via TRACE2 and
 * DRAWLN2. All 8 watdbg-listed functions are implemented here:
 *     drawpol_setmarkingcolors   drawpol_checknormal
 *     drawpol_drawpolyobject     drawpol_dobsptree
 *     drawpol_getlightvalue      drawpol_drawmarkings
 *     drawpol_drawlineface       drawpol_drawsurfacepoly
 *
 * Globals below are all DRAWPOL-owned per docs/watdbg-prototypes.txt.
 * Cross-module globals (objecteyex/y/z, bpflightflag, pixelsdeep, etc.)
 * are #included from their owning module's header.
 */

#include <stdint.h>
#include <string.h>

#include "anim.h"         /* anim_add_bitmap_draw (unused here but harmless) */
#include "tie/bpflight.h" /* bpflightflag */
#include "tie/collide.h"  /* collide_roughdistance3d (stub) */
#include "tie/draw.h"     /* PolyFace struct */
#include "tie/drawln2.h"  /* drawln2_tracelineedges */
#include "tie/drawpol.h"
#include "tie/dynamix.h" /* pspecnum (dynamix-owned) */
#include "tie/logbuf2.h"
#include "tie/spec.h"     /* spec_getspecnum */
#include "tie/tie.h"      /* world-camera, objecteye*, worldz, currenttarget, spec_data, etc. */
#include "tie/trace2.h"   /* trace2_drawface, trace2_drawscreencoords */
#include "tie/transfm2.h" /* TRANSFM2 transforms */
#include "tie/xtrans2.h"  /* flatx/y/z, edgept1/2, eyexyzdata, edgeflags (xtrans2-owned) */

/* DRAWPOL_EyeVertex and DRAWPOL_MarkingEyeData structs are defined in
 * drawpol.h (exported for callers that touch these buffers). */

/* ======================================================================
 * Globals (DRAWPOL-owned per watdbg)
 * ================================================================== */

/* Per-vertex / per-edge setup arrays. Watcom binary emits 1-based aliases
 * (_array - stride) as a load-base optimization; the C source uses plain
 * 0-based access. */
int32_t* calcflag[128];
// GLOBAL: TIE 0xD3E4C
uint16_t vertexlight[128];

/* Per-frame drawpol diagnostic counters (flushed by tie_updatescreen). */
int dbg_dp_total, dbg_dp_polycnt0, dbg_dp_polycnt_nz;
int dbg_dp_first_min_z, dbg_dp_first_max_z;
// GLOBAL: TIE 0xD3FE0
uint8_t* firstvertptr;
/* edgeflags[], edgept1[], edgept2[] defined in xtrans2.c per watdbg. */

/* Screen-xy ring */
// GLOBAL: TIE 0xD4010
int32_t* firstscreenxy;
// GLOBAL: TIE 0xD3FF4
int32_t* lastscreenxy;
// GLOBAL: TIE 0xD3FE4
int32_t* newscreenxy;

/* Shared scratch buffer for newscreenxy. Retail does `mov newscreenxy,
 * esp` and writes into uncommitted DOS stack memory below SP, so the
 * effective size is bounded only by the stack segment. We need a real
 * fixed-size allocation. numpoints/numedges/numfaces are single-byte
 * (≤255); the worst-case advance across one drawpolyobject call (line
 * branch + polygon branch via dobsptree) stays well under this. */
static int32_t newscreenxy_buf[4096];
// GLOBAL: TIE 0xD4008
// GLOBAL: TIE 0xD400C
int32_t *minscreenx, *maxscreenx;
// GLOBAL: TIE 0xD3FD4
// GLOBAL: TIE 0xD3FDC
int32_t *minscreeny, *maxscreeny;

/* Current polygon context */
// GLOBAL: TIE 0xD4026
int16_t numpoints;
int16_t numedges;
// GLOBAL: TIE 0xD4034
int16_t samexcnt;
// GLOBAL: TIE 0xD4036
int16_t sameycnt;
// GLOBAL: TIE 0xD4032
int16_t counter;
// GLOBAL: TIE 0xD4054
uint8_t color;
uint16_t facenumber;
// GLOBAL: TIE 0xD4030
uint16_t objectnum;
// GLOBAL: TIE 0xD4048
uint16_t parentobject;
uint8_t gauraudflag;
uint16_t layervalue;
int16_t numfaces;
int16_t polycnt;
int16_t threedflag;
uint8_t closerthansizeflag;
uint8_t doublesideflag;
uint8_t numeyezpos;
uint8_t lightval;
/* polyidbyte / edgeidbyte / objectedgeword are trace2.c-owned per watdbg.
 * thickness is drawln2.c-owned per watdbg. */

/* Object position (worldz is tie.c-owned per watdbg). */
int32_t objectx, objecty, objectz;
int16_t objectxhi, objectyhi, objectzhi;
int16_t objectxlo, objectylo, objectzlo;
/* worldx, worldy moved to tie.c per watdbg ownership; declared in tie.h. */

/* Light rig (rotlight{X,Y,Z} are tie.c-owned per watdbg). */
// GLOBAL: TIE 0xD4014
// GLOBAL: TIE 0xD4018
// GLOBAL: TIE 0xD401C
int32_t lightX, lightY, lightZ;
// GLOBAL: TIE 0xC1918
int32_t localLightCnt;
DRAWPOL_LocalLight localLights[8];

/* Poly-data layout pointers for the current mesh */
uint8_t* firstcoloroff;
PolyVert* firstpointoff;
PolyVert* firstvertnorm;
// GLOBAL: TIE 0xD3FE8
PolyFace* firstfaceoff;
// GLOBAL: TIE 0xD4004
DRAWPOL_EyeVertex* firsteyexyz;
/* eyexyzdata[] defined in xtrans2.c per watdbg. */
uint8_t facevisflag[256];

/* Object-record write cursor */
uint16_t newobjectdef;
uint16_t* objectdef;
uint8_t* curobjptr;

/* Gameplay shade palette: 45 materials × 16 shades. The rasterizer copies
 * a ramp locally when it needs the duplicated boundary shade at index 16. */
// GLOBAL: TIE 0xC191C
uint8_t materialcolors[720] = {
	0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4A, 0x4B, 0x4C, 0x4D, 0x4E, 0x4F, 0x54,
	0x55, 0x56, 0x57, 0x58, 0x59, 0x5A, 0x5B, 0x5C, 0x5D, 0x5E, 0x5F, 0x60, 0x61, 0x62, 0x63, 0x68, 0x69,
	0x6A, 0x6B, 0x6C, 0x6D, 0x6E, 0x6F, 0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77, 0x7C, 0x7D, 0x7E,
	0x7F, 0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8A, 0x8B, 0x44, 0x45, 0x46, 0x47,
	0x48, 0x49, 0x4A, 0x4B, 0x4C, 0x4D, 0x4E, 0x4F, 0x50, 0x51, 0x52, 0x53, 0x58, 0x59, 0x5A, 0x5B, 0x5C,
	0x5D, 0x5E, 0x5F, 0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x6C, 0x6D, 0x6E, 0x6F, 0x70, 0x71,
	0x72, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7A, 0x7B, 0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86,
	0x87, 0x88, 0x89, 0x8A, 0x8B, 0x8C, 0x8D, 0x8E, 0x8F, 0x48, 0x48, 0x49, 0x4A, 0x4A, 0x4B, 0x4C, 0x4D,
	0x4D, 0x4E, 0x4F, 0x50, 0x50, 0x51, 0x52, 0x53, 0x5C, 0x5C, 0x5D, 0x5E, 0x5E, 0x5F, 0x60, 0x61, 0x61,
	0x62, 0x63, 0x64, 0x64, 0x65, 0x66, 0x67, 0x70, 0x70, 0x71, 0x72, 0x72, 0x73, 0x74, 0x75, 0x75, 0x76,
	0x77, 0x78, 0x78, 0x79, 0x7A, 0x7B, 0x84, 0x84, 0x85, 0x86, 0x86, 0x87, 0x88, 0x89, 0x89, 0x8A, 0x8B,
	0x8C, 0x8C, 0x8D, 0x8E, 0x8F, 0x90, 0x91, 0x92, 0x92, 0x93, 0x94, 0x95, 0x95, 0x96, 0x97, 0x98, 0x98,
	0x99, 0x9A, 0x9B, 0x9B, 0x9C, 0x9D, 0x9E, 0x9E, 0x9F, 0xA0, 0xA1, 0xA1, 0xA2, 0xA3, 0xA4, 0xA4, 0xA5,
	0xA6, 0xA7, 0xA7, 0xA8, 0xA9, 0xAA, 0xAA, 0xAB, 0xAC, 0xAD, 0xAD, 0xAE, 0xAF, 0xB0, 0xB0, 0xB1, 0xB2,
	0xB3, 0xB3, 0xB4, 0xB4, 0xB5, 0xB5, 0xB6, 0xB6, 0xB7, 0xB7, 0xB8, 0xB8, 0xB9, 0xB9, 0xB9, 0xB9, 0xB9,
	0xB9, 0xB4, 0xB4, 0xB4, 0xB5, 0xB5, 0xB5, 0xB6, 0xB6, 0xB7, 0xB7, 0xB8, 0xB8, 0xB9, 0xB9, 0xB9, 0xB9,
	0x7C, 0x7D, 0x7E, 0x7F, 0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8A, 0x8B, 0x80,
	0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8A, 0x8B, 0x8C, 0x8D, 0x8E, 0x8F, 0x84, 0x84,
	0x85, 0x86, 0x86, 0x87, 0x88, 0x89, 0x89, 0x8A, 0x8B, 0x8C, 0x8C, 0x8D, 0x8E, 0x8F, 0xF8, 0xF8, 0xF8,
	0xF8, 0xF8, 0xF8, 0xF8, 0xF8, 0xF8, 0xF8, 0xF8, 0xF8, 0xF8, 0xF8, 0xF8, 0xF8, 0xF9, 0xF9, 0xF9, 0xF9,
	0xF9, 0xF9, 0xF9, 0xF9, 0xF9, 0xF9, 0xF9, 0xF9, 0xF9, 0xF9, 0xF9, 0xF9, 0xFA, 0xFA, 0xFA, 0xFA, 0xFA,
	0xFA, 0xFA, 0xFA, 0xFA, 0xFA, 0xFA, 0xFA, 0xFA, 0xFA, 0xFA, 0xFA, 0x9C, 0x9C, 0x9D, 0x9D, 0x9E, 0x9E,
	0x9F, 0x9F, 0xA0, 0xA0, 0xA1, 0xA1, 0xA2, 0xA2, 0xA3, 0xA3, 0x9F, 0x9F, 0xA0, 0xA0, 0xA0, 0xA1, 0xA1,
	0xA1, 0xA2, 0xA2, 0xA2, 0xA3, 0xA3, 0xA3, 0xA4, 0xA4, 0xA2, 0xA2, 0xA3, 0xA3, 0xA3, 0xA4, 0xA4, 0xA4,
	0xA5, 0xA5, 0xA5, 0xA6, 0xA6, 0xA6, 0xA7, 0xA7, 0x8B, 0x8B, 0x8C, 0x8C, 0x8C, 0x8D, 0x8D, 0x8D, 0x8E,
	0x8E, 0x8E, 0x8E, 0x8F, 0x8F, 0x8F, 0x8F, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4A,
	0x4B, 0x4C, 0x4D, 0x4E, 0x4F, 0x50, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5A, 0x5B, 0x5C, 0x5D, 0x5E, 0x5F,
	0x60, 0x61, 0x62, 0x63, 0x64, 0x69, 0x6A, 0x6B, 0x6C, 0x6D, 0x6E, 0x6F, 0x70, 0x71, 0x72, 0x73, 0x74,
	0x75, 0x76, 0x77, 0x78, 0x7D, 0x7E, 0x7F, 0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89,
	0x8A, 0x8B, 0x8C, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4A, 0x4B, 0x4C, 0x4D, 0x4E, 0x4F,
	0x50, 0x51, 0x56, 0x57, 0x58, 0x59, 0x5A, 0x5B, 0x5C, 0x5D, 0x5E, 0x5F, 0x60, 0x61, 0x62, 0x63, 0x64,
	0x65, 0x6A, 0x6B, 0x6C, 0x6D, 0x6E, 0x6F, 0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79,
	0x7E, 0x7F, 0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8A, 0x8B, 0x8C, 0x8D, 0x43,
	0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4A, 0x4B, 0x4C, 0x4D, 0x4E, 0x4F, 0x50, 0x51, 0x52, 0x57, 0x58,
	0x59, 0x5A, 0x5B, 0x5C, 0x5D, 0x5E, 0x5F, 0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x6B, 0x6C, 0x6D,
	0x6E, 0x6F, 0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7A, 0x7F, 0x80, 0x81, 0x82,
	0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8A, 0x8B, 0x8C, 0x8D, 0x8E, 0xA8, 0xA8, 0xA9, 0xA9, 0xAA,
	0xAA, 0xAB, 0xAB, 0xAC, 0xAC, 0xAD, 0xAD, 0xAE, 0xAE, 0xAF, 0xAF, 0xAB, 0xAB, 0xAC, 0xAC, 0xAC, 0xAD,
	0xAD, 0xAD, 0xAE, 0xAE, 0xAE, 0xAF, 0xAF, 0xAF, 0xB0, 0xB0, 0xAE, 0xAE, 0xAF, 0xAF, 0xAF, 0xB0, 0xB0,
	0xB0, 0xB1, 0xB1, 0xB1, 0xB2, 0xB2, 0xB2, 0xB3, 0xB3, 0x90, 0x90, 0x91, 0x91, 0x92, 0x92, 0x93, 0x93,
	0x94, 0x94, 0x95, 0x95, 0x96, 0x96, 0x97, 0x97, 0x93, 0x93, 0x94, 0x94, 0x94, 0x95, 0x95, 0x95, 0x96,
	0x96, 0x96, 0x97, 0x97, 0x97, 0x98, 0x98, 0x96, 0x96, 0x97, 0x97, 0x97, 0x98, 0x98, 0x98, 0x99, 0x99,
	0x99, 0x9A, 0x9A, 0x9A, 0x9B, 0x9B
};
/* markingdefs[3][16]: three 16-byte shade ramps for the "marking" material.
 * Mode 0 (OFF) is the default ramp at 0x9c..0xa7; mode 1 (NORMAL, written
 * with markingstate=-1) is 0x90..0x9b; mode 2 (ALT, markingstate=+1) is
 * 0xa8..0xb3. setmarkingcolors copies the chosen row into
 * materialcolors[208..223] (= the marking-material shade ramp). Bytes
 * dumped from retail binary at 0xC1C20 (debug symbol _markingdefs[48]). */
// GLOBAL: TIE 0xC1C20
uint8_t markingdefs[3][16] = {
	/* mode 0 (OFF):    */
	{ 0x9c, 0x9d, 0x9e, 0x9e, 0x9f, 0xa0, 0xa1, 0xa1, 0xa2, 0xa3, 0xa4, 0xa4, 0xa5, 0xa6, 0xa7, 0xa7 },
	/* mode 1 (NORMAL): */
	{ 0x90, 0x91, 0x92, 0x92, 0x93, 0x94, 0x95, 0x95, 0x96, 0x97, 0x98, 0x98, 0x99, 0x9a, 0x9b, 0x9b },
	/* mode 2 (ALT):    */
	{ 0xa8, 0xa9, 0xaa, 0xaa, 0xab, 0xac, 0xad, 0xad, 0xae, 0xaf, 0xb0, 0xb0, 0xb1, 0xb2, 0xb3, 0xb3 },
};
/* Color-remap tables, mirrored byte-for-byte from the retail binary's
 * .data segment.
 *
 * IDA labels: materialcolors @0xC191C, targetmapping_base1 @0xC1BEB,
 * highlightmapping @0xC1C13, traininggatecolors @0xC1C1C,
 * markcoloroffset @0xC1C50.
 *
 * targetmapping uses the Watcom base-1 access pattern: the binary
 * emits `byte_C1BEB[c & 0x3F]` with the compiled array base at
 * 0xC1BEB, while the real data sits one byte later at 0xC1BEC.
 * Effectively the binary reads `real_data[(c & 0x3F) - 1]`. We keep
 * the array sized to its real 39 bytes here and write the base-1
 * indexing explicitly at the call sites. The c == 0 case (mapped
 * face color zero) is degenerate -- the binary reads whatever sits
 * at 0xC1BEB (a 0x9b spill from shieldcolor's tail) which then
 * indexes way past highlightmapping; we treat it as an invalid
 * input and return 0 deterministically instead. */
// GLOBAL: TIE 0xC1BEC
const uint8_t targetmapping[39] = {
	0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x02, 0x02, 0x02, 0x02, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x02, 0x00, 0x01, 0x02,
	0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
};

/* highlightmapping (binary 0xC1C13, 9 bytes: 3 highlightcolors x 3 idx
 * values. DRAW_drawcraft only ever sets highlightcolor in {0, 1, 2}
 * and targetmapping_base1 returns idx in {0, 1, 2} for legitimate
 * inputs, so 9 entries suffice). */
// GLOBAL: TIE 0xC1C13
const uint8_t highlightmapping[9] = {
	/* highlightcolor=0 (whole-craft target): */ 0x18,
	0x19,
	0x1a,
	/* highlightcolor=1 (blue/secondary):     */ 0x28,
	0x29,
	0x2a,
	/* highlightcolor=2 (component highlight):*/ 0x2b,
	0x2c,
	0x2d,
};

/* traininggatecolors (binary 0xC1C1C, 4 bytes). Indexed by gatecolor in
 * 0..3 when parentobject HIBYTE == 0x40 (training-gate overlay path). */
// GLOBAL: TIE 0xC1C1C
const uint8_t traininggatecolors[4] = {
	0x00,
	0x0d,
	0x0c,
	0x0e,
};

/* Signed per-face color offsets. Index 14 is the mutable three-frame
 * marking animation state; masked reads can address indices 0..63. */
// GLOBAL: TIE 0xC1C50
int8_t markcoloroffset[72] = { 0 };

/* linelight1, linelight2, point1ptr: drawln2.c-owned per watdbg. */

/* Marking book-keeping */
uint8_t* farmarkingptr[128];
uint16_t markingnumber[128];
uint16_t markingptr[128];
uint16_t objectptrs[128];
DRAWPOL_MarkingEyeData markingeyedata;
uint8_t markcnt;
uint16_t nummarks;
/* gatecolor is tie.c-owned per watdbg. */

/* flatx/y/z, flatcolors, flatcomponentnum, flatparentobj, flatobjnum all
 * defined in xtrans2.c per watdbg. */

// GLOBAL: TIE 0xD404A
uint16_t solidindex;

/* ======================================================================
 * Cross-module externs (referenced but owned elsewhere)
 * ================================================================== */

/* Owned by TRANSFM2 / FVIEW. See transfm2.h / tie.h for declarations. */
/* transfm2-owned world->eye matrix; see transfm2.h. */

/* Pixel geometry (owned by tie.c / vesa.c). */

/* Engine tick / globals. */

/* ======================================================================
 * Helpers
 * ================================================================== */

/* Clamp a 30-bit accumulator to +/-0x40000000, then callers >> 15 to get
 * a signed Q15 result. Matches the binary's clamp-then-shift idiom. */
static inline int32_t clamp_q30(int32_t v) {
	if (v >= 0x40000000)
		return 0x3FFF0000;
	if (v <= -0x40000000)
		return -0x3FFF0000;
	return v;
}

/* ======================================================================
 * drawpol_setmarkingcolors
 *
 * Install the 16-entry marking/decal palette for this draw pass and
 * record the marking-state offset that will modulate face base color 14.
 *
 * Three source rows in markingdefs (OFF / NORMAL / ALT) — copies the
 * chosen row into materialcolors[208..223] (= row 13 of the material
 * LUT, reserved for the "marking" shade ramp).
 *
 * The marking state is stored in markcoloroffset[14], NOT in a separate
 * variable: the binary aliases the two by writing single bytes at the
 * 0xC1C5E address that is also markcoloroffset+14. DRAWPOL_getlightvalue
 * reads markcoloroffset[c & 0x3F] AS SIGNED (movsx) on every face, so
 * face base color 14 picks up a -1 / 0 / +1 offset and renders as
 * material 13 / 14 / 15 (= materialcolors row 12 / 13 / 14) for the
 * three modes. That's the "marking material" three-frame animation.
 * ================================================================== */
// FUNCTION: TIE 0x1D480
void drawpol_setmarkingcolors(MarkingMode mode) {
	uint8_t* dest = &materialcolors[208]; /* row 13, col 0 */
	switch (mode) {
		case MARKING_NORMAL:
			markcoloroffset[14] = -1;
			memcpy(dest, markingdefs[1], 16);
			break;
		case MARKING_ALT:
			markcoloroffset[14] = 1;
			memcpy(dest, markingdefs[2], 16);
			break;
		case MARKING_OFF:
		default:
			markcoloroffset[14] = 0;
			memcpy(dest, markingdefs[0], 16);
			break;
	}
}

/* ======================================================================
 * drawpol_checknormal
 *
 * Backface test. Rotates the face normal (firstfaceoff[face_idx].normal_*)
 * by the full world->eye matrix (rotworldeyeA/B/C), clamping each
 * component to +/-0x40000000 before the Q15 shift. Then fetches the
 * first vertex of the face via vlist_offset + 3 (vertex-list header at
 * bytes 0..2, first index at +3) and looks it up in firsteyexyz[].
 *
 * Returns:
 *   0 = dot < 0  (front-facing; normal points away from +Z eye axis,
 *                 so back toward camera at origin)
 *   2 = dot >= 0 (back-facing; cull candidate)
 *
 * Caller (drawpolyobject) stores (ret >> 1) into facevisflag[face_idx]
 * -- 0 means draw, 1 means back-facing (handled separately or culled).
 * dobsptree's jz after checknormal skips the face when 0 is returned.
 * ================================================================== */
// FUNCTION: TIE 0x1E674
uint16_t drawpol_checknormal(uint16_t face_idx) {
	PolyFace* face = &firstfaceoff[face_idx];

	int32_t nx = clamp_q30(face->normal_x * rotworldeyeA1 + face->normal_y * rotworldeyeB1 +
						   face->normal_z * rotworldeyeC1) >>
				 15;
	int32_t ny = clamp_q30(face->normal_x * rotworldeyeA2 + face->normal_y * rotworldeyeB2 +
						   face->normal_z * rotworldeyeC2) >>
				 15;
	int32_t nz_pre = clamp_q30(face->normal_x * rotworldeyeA3 + face->normal_y * rotworldeyeB3 +
							   face->normal_z * rotworldeyeC3);

	/* First vertex of the face: byte at face + vlist_offset + 3 */
	uint8_t first_vtx_idx = ((uint8_t*)face)[face->vlist_offset + 3];
	DRAWPOL_EyeVertex* v = &firsteyexyz[first_vtx_idx];

	int64_t dot = ((int64_t)v->x * nx) >> 15;
	dot += ((int64_t)v->y * ny) >> 15;
	dot += ((int64_t)v->z * (nz_pre >> 15)) >> 15;
	return (dot < 0) ? 0 : 2;
}

/* ======================================================================
 * drawpol_drawsurfacepoly
 *
 * ORPHANED in the demo binary (no callers). Fills a 20-dword scratch
 * buffer representing a 4-vertex quad and rasterizes it via
 * TRACE2_drawscreencoords. Kept as a direct port of the binary for
 * faithfulness; color_code -= 80 is the documented adjustment.
 * ================================================================== */
// FUNCTION: TIE 0x1F454
void drawpol_drawsurfacepoly(int32_t* scratch, char color_code) {
	/* Duplicate vertex data into the slots expected by getscreencoords. */
	scratch[17] = scratch[5];
	scratch[18] = scratch[6];
	scratch[19] = scratch[7];
	scratch[2] = scratch[14];
	scratch[3] = scratch[15];
	scratch[4] = scratch[16];

	firstscreenxy = scratch;
	minscreenx = scratch;
	minscreeny = scratch;
	maxscreenx = scratch;
	maxscreeny = scratch;
	color = (uint8_t)(color_code - 80);
	sameycnt = 0;
	samexcnt = 0;
	numpoints = 4;
	counter = 4;

	lastscreenxy = transfm2_getscreencoords(&scratch[5], scratch);
	trace2_drawscreencoords();
}

/* ======================================================================
 * drawpol_drawlineface
 *
 * Render a single wireframe edge from the current polygon. Reads the
 * 5-byte edge record via firstvertptr:
 *   +0 (u16) thickness base
 *   +2 (u8)  vertex 1 index
 *   +3 (u8)  vertex 2 index
 *   +4 (u8)  edge index into edgept1/edgept2
 *
 * Computes perspective-adjusted thickness from average eye-z of the
 * two endpoints, copies the precomputed screen-xy pairs into the
 * scratch buffer, and calls drawln2_tracelineedges.
 * ================================================================== */
// FUNCTION: TIE 0x1F328
void drawpol_drawlineface(void) {
	uint8_t edge_idx = firstvertptr[4];
	uint8_t vtx1_idx = firstvertptr[2];
	uint8_t vtx2_idx = firstvertptr[3];
	uint16_t base = *(uint16_t*)firstvertptr;

	/* Reset the first endpoint before copying this edge's screen coordinates. */
	point1ptr = edgept1[edge_idx];
	int32_t* point2_start = edgept2[edge_idx];

	linelight1 = vertexlight[vtx1_idx];
	linelight2 = vertexlight[vtx2_idx];

	int32_t avg_z = (firsteyexyz[vtx1_idx].z / 2) + (firsteyexyz[vtx2_idx].z / 2);

	uint16_t thick_val = (uint16_t)(thicknessMultiple * base);
	int32_t scale_z = avg_z >> 8;
	if (scale_z > 0)
		thick_val = thick_val / (uint16_t)scale_z;

	/* Polyobject lines take color from the object's face flags. */
	polyidbyte = objectnum;
	edgeidbyte = edge_idx;
	objectedgeword = (uint16_t)(edge_idx | ((uint8_t)objectnum << 8));

	int32_t* dst = newscreenxy;
	dst[0] = point2_start[0];
	dst[1] = point2_start[1];
	int32_t p1a = point1ptr[0];
	int32_t p1b = point1ptr[1];
	point1ptr = dst + 2;
	dst[2] = p1a;
	dst[3] = p1b;

	thickness = (uint16_t)(thick_val + 1);

	int16_t saved_flatobj = (int16_t)flatobjnum;
	drawln2_tracelineedges(dst);
	flatobjnum = saved_flatobj;
}

/* Resolve material remapping, training-gate and target overlays, then either
 * cache Gouraud vertex lighting or return a flat-shaded palette color. */
// FUNCTION: TIE 0x1E7E4
int16_t drawpol_getlightvalue(int16_t color_byte, uint16_t face_idx) {
	uint16_t mapped_color = (uint16_t)((int16_t)markcoloroffset[color_byte & 0x3F] + color_byte);

	if (!threedflag)
		return (int16_t)mapped_color;

	uint16_t color_with_flags = mapped_color;

	/* Training-gate overlay (parentobject HIBYTE == 0x40). */
	if (((parentobject >> 8) & 0xFF) == 0x40)
		color_with_flags = (uint16_t)((uint8_t)traininggatecolors[gatecolor] + mapped_color);

	/* Target highlight. Watcom base-1 access on targetmapping: the
	 * binary uses `byte_C1BEB[c & 0x3F]` (= real_data[N-1]). We
	 * express that as N-1 here; mapped == 0 is degenerate (binary
	 * reads garbage one byte before the table and then runs off the
	 * end of highlightmapping) -- pin idx to 0 for determinism. */
	if (parentobject == currenttarget) {
		uint8_t mapped = (uint8_t)(color_with_flags & 0x3F);
		uint8_t idx = (mapped >= 1 && mapped <= 39) ? targetmapping[mapped - 1] : 0;
		color_with_flags = (uint16_t)(highlightmapping[3 * highlightcolor + idx] | (color_with_flags & 0x80));
	}

	PolyFace* face = &firstfaceoff[face_idx];
	uint8_t* vlist = (uint8_t*)face + face->vlist_offset;
	uint8_t flag_byte = vlist[0];
	uint8_t unlit_bit = flag_byte & 0x80;
	uint8_t vcount = flag_byte & 0x3F;

	/* Reset DRAWPOL's per-call Gouraud output flag (binary: mov _gauraudflag, dl
	 * with dl=0 unconditional at entry to this path). */
	gauraudflag = 0;

	/* Per-vertex Gouraud path. Gated by EXTERNAL input gouraudflag (tie.c)
	 * AND the face's flag_byte bit 0x40. gauraudflag (DRAWPOL's OUTPUT flag,
	 * misspelled as in the binary) is SET here when the path is taken;
	 * drawmarkings reads it later to pick its color path. */
	if ((gouraudflag & flag_byte) & 0x40) {
		gauraudflag = 0x40;
		uint8_t* walker = vlist;
		if (vcount == 2) {
			walker = vlist + 3;
			vlist[6] = vlist[3]; /* duplicate last vertex ref */
		}

		for (int i = 0; i < vcount; ++i) {
			uint8_t vtx_idx = walker[1];
			walker += 2;

			/* Skip if already computed (unless unlit_bit forces recompute). */
			if (!unlit_bit && vertexlight[vtx_idx] != 0xFFFF)
				continue;

			PolyVert* vn = &firstvertnorm[vtx_idx];
			int32_t dot = clamp_q30(rotlightX * vn->x + rotlightY * vn->y + rotlightZ * vn->z) >> 15;
			vertexlight[vtx_idx] = (uint16_t)dot;
			if ((dot & 0x8000) && flag_byte != 194)
				vertexlight[vtx_idx] = 0;

			/* Local-light accumulation. */
			for (int li = 0; li < localLightCnt; ++li) {
				PolyVert* vp = &firstpointoff[vtx_idx];
				int32_t dx = vp->x - localLights[li].x;
				int32_t dy = vp->y - localLights[li].y;
				int32_t dz = vp->z - localLights[li].z;
				int32_t d = collide_roughdistance3d(dx, dy, dz);
				int32_t range = localLights[li].range;
				if (d > (range << 7))
					continue;
				if (!d) {
					vertexlight[vtx_idx] = 0x7FFF;
					break;
				}
				/* Watcom emits `shl reg, 15` then `idiv`; perform the
				 * shift in uint32_t (well-defined for negative dx/dy/dz)
				 * and cast back for the signed divide. */
				int32_t n_dot = clamp_q30(((int32_t)((uint32_t)dx << 15) / d) * vn->x +
										  ((int32_t)((uint32_t)dy << 15) / d) * vn->y +
										  ((int32_t)((uint32_t)dz << 15) / d) * vn->z) >>
								15;
				int32_t gain = n_dot + 0x8000;
				if (gain <= 0)
					continue;
				int32_t contrib = range * gain / (((d * d) >> 12) + 1);
				if (contrib + vertexlight[vtx_idx] > 0x7FFF) {
					vertexlight[vtx_idx] = 0x7FFF;
					break;
				}
				vertexlight[vtx_idx] = (uint16_t)(vertexlight[vtx_idx] + contrib);
			}
		}
		if (!(color_with_flags & 0x80))
			return (int16_t)color_with_flags;
		color_with_flags &= 0x7F;
		/* Fall through to flat-lighting for the 'unlit' bit-stripped variant. */
	}

	/* Flat-face lighting path. */
	int32_t face_dot =
		clamp_q30(face->normal_x * rotlightX + face->normal_y * rotlightY + face->normal_z * rotlightZ) >> 15;
	uint16_t shade_off = (uint16_t)(16 * (color_with_flags & 0x7F));
	uint16_t mat_idx;
	if (face_dot >= 0) {
		lightval = (uint8_t)(face_dot >> 11);
		mat_idx = (uint16_t)(shade_off - lightval);
	} else {
		lightval = 0;
		mat_idx = shade_off;
	}
	/* Binary indexes via `materialcolors_base1` (= materialcolors - 1),
	 * a classic Watcom pre-decremented-pointer trick. mat_idx is always
	 * >= 1 in practice since color >= 1 and lightval <= 15. */
	return (int16_t)materialcolors[mat_idx - 1];
}

/* Markings are barycentric sub-polygons stored as:
 *   +0 (u8) marktot          (count of markings on this face)
 *   (then marktot copies of:)
 *     +0 (u8) vcount | flags  (0xFF = depth-cull record follows)
 *     if (depth-cull marker): +1 (i32) min_z threshold, then advance 5 bytes
 *     for each vertex:
 *       +0 (u8) base vertex index (relative to parent face)
 *       +1 (u8) barycentric weight toward edge-1 vertex (0..32)
 *       +2 (u8) barycentric weight toward diagonal vertex
 * Weights use a denominator of 32. Two-vertex markings use the line path;
 * other markings use screen-coordinate tracing. */
// FUNCTION: TIE 0x1EC68
void drawpol_drawmarkings(uint16_t face_idx) {
	uint16_t saved_flatobj = flatobjnum;
	uint16_t saved_layerv = layervalue;
	uint16_t marking_idx = markingnumber[face_idx];

	layervalue = (uint8_t)marking_idx;
	uint8_t* mwalk = farmarkingptr[marking_idx];
	uint8_t marktot = mwalk[0];
	markcnt = marktot;
	mwalk += marktot + 1;

	if (newobjectdef + 32u * marktot > 0xC000u) {
		flatobjnum = saved_flatobj;
		layervalue = saved_layerv;
		return;
	}

	flatobjnum = 127;
	while (markcnt != 0) {
		/* Depth-cull record: 0xFF marker + i32 z-threshold. */
		if (*mwalk == 0xFF) {
			int32_t z_cut = *(int32_t*)(mwalk + 1);
			mwalk += 5;
			if (firsteyexyz[firstvertptr[0]].z > z_cut)
				break;
		}

		uint8_t raw_vcount = mwalk[0];
		numpoints = raw_vcount;
		mwalk += 1;
		if (raw_vcount > 16) {
			thickness = (uint16_t)(raw_vcount - 16);
			numpoints = 2;
			counter = 2;
			thickness = (uint16_t)(thickness * (uint16_t)thicknessMultiple);
		} else {
			counter = numpoints;
		}

		firstscreenxy = newscreenxy;
		minscreenx = newscreenxy;
		minscreeny = newscreenxy;
		maxscreenx = newscreenxy;
		maxscreeny = newscreenxy;
		sameycnt = 0;
		samexcnt = 0;

		if (closerthansizeflag || numpoints == 2) {
			/* Eye-space barycentric interpolation into markingeyedata.verts[].
			 * firstvertptr holds a 2-byte-per-vertex face body; base_idx points
			 * to the anchor vertex slot, and (base_idx-2)/(base_idx+2) are
			 * the previous/next vertex entries in the parent face. w_edge/
			 * w_diag are /32 barycentric weights toward those neighbours. */
			for (int i = 0; i < numpoints; ++i) {
				uint8_t base_idx = mwalk[0];
				uint8_t w_edge = mwalk[1];
				uint8_t w_diag = mwalk[2];
				DRAWPOL_EyeVertex* v0 = &firsteyexyz[firstvertptr[base_idx]];
				DRAWPOL_EyeVertex* slot = &markingeyedata.verts[i];
				slot->x = v0->x;
				slot->y = v0->y;
				slot->z = v0->z;
				if (w_edge) {
					DRAWPOL_EyeVertex* ve = &firsteyexyz[firstvertptr[base_idx - 2]];
					slot->x += ((ve->x - v0->x) * w_edge) >> 5;
					slot->y += ((ve->y - v0->y) * w_edge) >> 5;
					slot->z += ((ve->z - v0->z) * w_edge) >> 5;
				}
				if (w_diag) {
					DRAWPOL_EyeVertex* vd = &firsteyexyz[firstvertptr[base_idx + 2]];
					slot->x += ((vd->x - v0->x) * w_diag) >> 5;
					slot->y += ((vd->y - v0->y) * w_diag) >> 5;
					slot->z += ((vd->z - v0->z) * w_diag) >> 5;
				}
				mwalk += 3;
			}
			if (numpoints == 2) {
				/* Line marking: sentinel written to scratch.z and to
				 * verts[numpoints].z (the polygon-close slot). Rendering
				 * code treats these as 'no further endpoint'. */
				markingeyedata.scratch.z = -1;
				markingeyedata.verts[2].z = -1;
			} else {
				/* Close the polygon: append verts[0] as verts[numpoints]
				 * so the wrap edge renders; stash last vertex into scratch
				 * for the line-endpoint register used downstream. */
				markingeyedata.verts[numpoints] = markingeyedata.verts[0];
				markingeyedata.scratch = markingeyedata.verts[numpoints - 1];
			}
			lastscreenxy = transfm2_getscreencoords(&markingeyedata.verts[0].x, firstscreenxy);
		} else {
			/* Screen-space barycentric (calcflag[] holds projected pairs). */
			int32_t* out = newscreenxy;
			for (int j = 0; j < numpoints; ++j) {
				uint8_t base_idx = mwalk[0];
				uint8_t w_edge = mwalk[1];
				uint8_t w_diag = mwalk[2];
				int32_t* s0 = calcflag[firstvertptr[base_idx]];
				out[0] = s0[0];
				out[1] = s0[1];
				if (w_edge) {
					int32_t* se = calcflag[firstvertptr[base_idx - 2]];
					out[0] += ((se[0] - s0[0]) * w_edge) >> 5;
					out[1] += ((se[1] - s0[1]) * w_edge) >> 5;
				}
				if (w_diag) {
					int32_t* sd = calcflag[firstvertptr[base_idx + 2]];
					out[0] += (w_diag * (sd[0] - s0[0])) >> 5;
					out[1] += (w_diag * (sd[1] - s0[1])) >> 5;
				}
				transfm2_doxminmax(out[0], out);
				mwalk += 3;
				transfm2_doyminmax(out[1], out);
				out += 2;
			}
			lastscreenxy = out;
		}

		if (numpoints == 2) {
			polyidbyte = (uint16_t)(flatobjnum + 0x80);
			edgeidbyte = layervalue;
			objectedgeword = (uint16_t)((uint8_t)layervalue | (polyidbyte << 8));
			/* Average z of the two marking endpoints: binary reads
			 * dword_E24D8 (byte 32 = verts[1].z) and dword_E24CC
			 * (byte 20 = verts[0].z) -- perspective-scales thickness. */
			int32_t mid_z = (markingeyedata.verts[1].z + markingeyedata.verts[0].z) / 2;
			if (mid_z > 0 && (mid_z >> 8) > 0)
				thickness = thickness / (uint16_t)(mid_z >> 8);
			point1ptr = firstscreenxy;
			thickness++;
			drawln2_tracelineedges(firstscreenxy + 2);
		} else {
			trace2_drawscreencoords();
		}
		/* Update flatobjnum only after the termination check, matching the
		 * loop's increment-clause ordering. */
		if (!--markcnt)
			break;
		flatobjnum = (uint16_t)(markcnt + 127u - marktot);
	}

	/* Build per-marking color table at xtransdataptr + newobjectdef. */
	uint8_t* base_src = farmarkingptr[marking_idx] + 1;
	uint16_t obj_off = newobjectdef;
	markingptr[marking_idx] = obj_off;
	uint8_t* out_ptr = (uint8_t*)xtransdataptr + obj_off;
	out_ptr[0] = (uint8_t)objectnum;
	out_ptr[1] = (uint8_t)facenumber;
	out_ptr += 2;
	int n_colors = (int)base_src[-1];
	uint8_t* src = base_src;

	if (currenttarget == parentobject) {
		while (--n_colors >= 0) {
			uint8_t c = *src;
			uint8_t mapped = (uint8_t)(c & 0x3F);
			/* targetmapping is Watcom base-1 indexed; bypass with idx=0
			 * for the degenerate mapped==0 input (see drawpol_getlightvalue). */
			uint8_t idx = (mapped >= 1 && mapped <= 39) ? targetmapping[mapped - 1] : 0;
			uint8_t remap = highlightmapping[3 * highlightcolor + idx];
			/* materialcolors_base1 indexing (- 1). */
			uint8_t shaded = (uint8_t)(materialcolors[16 * remap - lightval - 1] - ((c >> 6) & 3));
			*out_ptr++ = shaded;
			++src;
		}
	} else if (gauraudflag) {
		while (--n_colors >= 0) {
			uint8_t c = *src;
			*out_ptr++ = (uint8_t)(markcoloroffset[c & 0x3F] + c);
			++src;
		}
	} else {
		while (--n_colors >= 0) {
			uint8_t c = *src;
			/* materialcolors_base1 indexing (- 1). */
			uint8_t shaded = (uint8_t)(materialcolors[16 * (c & 0x3F) - lightval - 1] - ((c >> 6) & 3));
			*out_ptr++ = shaded;
			++src;
		}
	}

	/* 8 pairs of zero padding. */
	uint16_t newdef = newobjectdef + 18;
	newobjectdef = newdef;
	uint8_t* pad = (uint8_t*)xtransdataptr + newdef;
	for (int i = 0; i < 8; ++i) {
		pad[0] = 0;
		pad[1] = 0;
		pad += 2;
		newdef += 2;
	}
	newobjectdef = newdef;

	flatobjnum = saved_flatobj;
	layervalue = saved_layerv;
}

/* Recursive back-to-front BSP walk. Nodes contain a face index and signed
 * self-relative child/sibling offset. Visibility bits select normal testing,
 * two-sided lighting, face emission, and marking emission. */
// FUNCTION: TIE 0x1E388
uint8_t* drawpol_dobsptree(uint8_t* node_ptr) {
	BSPFaceNode* node = (BSPFaceNode*)node_ptr;
	uint8_t* result = node_ptr;
	uint8_t face_idx = 0;

	/* 4-level nested structure mirroring the binary's control flow.
	 * Each inner loop handles one walk mode; break falls through to the
	 * next-outer mode's body. face_idx is carried across levels (it's
	 * re-read at the top of mode-1). */
	while (1) {         /* outer: mode-4 */
		while (1) {     /* inner-3: mode-3 */
			while (1) { /* inner-2: mode-2 */

				/* -------- inner-1: mode-1 (facevis & 0x01) -------- */
				while (1) {
					face_idx = node->face_idx;
					int16_t offset_m1 = node->next_off;

					if (!(facevisflag[face_idx] & 0x01))
						break;

					if (offset_m1 > 3 || offset_m1 < 0)
						drawpol_dobsptree((uint8_t*)(node + 1));

					if (facevisflag[face_idx] & 0x04) {
						PolyFace* face = &firstfaceoff[face_idx];
						uint8_t* vlist = (uint8_t*)face + face->vlist_offset;
						uint8_t vcount = vlist[0] & 0x3F;
						firstvertptr = vlist + 1;

						uint8_t color_val = (uint8_t)drawpol_getlightvalue(firstcoloroff[face_idx], face_idx);
						uint8_t* slot = (uint8_t*)&objectdef[facenumber + 267];
						slot[0] = color_val;
						slot[1] = face_idx;

						if (vcount == 2)
							drawpol_drawlineface();
						else
							trace2_drawface(vcount);
						if (markingnumber[face_idx])
							drawpol_drawmarkings(face_idx);
						++facenumber;
					}
					result = (uint8_t*)&node->next_off;
					if (node->next_off <= 0)
						return result;
					node = (BSPFaceNode*)((uint8_t*)node + node->next_off);
				}

				/* -------- inner-2 body: mode-2 (facevis & 0x10) -------- */
				if (!(facevisflag[face_idx] & 0x10))
					break;

				int16_t offset_m2 = node->next_off;
				if (offset_m2 > 0) {
					drawpol_dobsptree((uint8_t*)node + offset_m2);
					/* Binary does node = back - offset (no-op); preserved as such. */
				}

				if (facevisflag[face_idx] & 0x04) {
					PolyFace* face = &firstfaceoff[face_idx];
					uint8_t* vlist = (uint8_t*)face + face->vlist_offset;
					if (vlist[0] & 0x80) {
						/* Two-sided: recompute lighting with negated rotlight. */
						rotlightZ = -rotlightZ;
						rotlightX = -rotlightX;
						rotlightY = -rotlightY;
						uint8_t color_val = (uint8_t)drawpol_getlightvalue(firstcoloroff[face_idx], face_idx);
						rotlightX = -rotlightX;
						rotlightY = -rotlightY;
						rotlightZ = -rotlightZ;

						uint8_t* slot2 = (uint8_t*)(objectdef + 268);
						int v_off = 2 * facenumber - 2;
						uint8_t vcount = vlist[0] & 0x3F;
						slot2[v_off + 1] = face_idx;
						firstvertptr = vlist + 1;
						slot2[v_off] = color_val;

						if (vcount == 2)
							drawpol_drawlineface();
						else
							trace2_drawface(vcount);
						if (markingnumber[face_idx])
							drawpol_drawmarkings(face_idx);
						++facenumber;
					}
				}
				result = (uint8_t*)&node->next_off;
				if (node->next_off <= 3 && node->next_off >= 0)
					return result;
				node = (BSPFaceNode*)((uint8_t*)node + 3);
				/* loop back to top of inner-2 (which re-enters inner-1). */
			}

			/* -------- inner-3 body: mode-3 (checknormal) -------- */
			if (!drawpol_checknormal(node->face_idx))
				break;

			int16_t offset_m3 = node->next_off;
			/* Binary: result = (uint8_t *)offset_m3 (bogus pointer when
			 * offset_m3 is small). Overwritten by recursion if we take it.
			 * Callers discard the return value, so the bogus value is harmless. */
			result = (uint8_t*)(uintptr_t)(uint16_t)offset_m3;
			if (offset_m3 > 3 || offset_m3 < 0)
				result = drawpol_dobsptree((uint8_t*)(node + 1));
			if (node->next_off <= 0)
				return result;
			node = (BSPFaceNode*)((uint8_t*)node + offset_m3);
			/* loop back to top of inner-3 (which re-enters inner-2). */
		}

		/* -------- outer body: mode-4 (tail fallback) -------- */
		int16_t offset_m4 = node->next_off;
		if (offset_m4 > 0) {
			drawpol_dobsptree((uint8_t*)node + offset_m4);
			/* node stays (binary's back - offset dance is a no-op). */
		}
		result = (uint8_t*)(uintptr_t)(uint16_t)offset_m4;
		if (offset_m4 <= 3 && offset_m4 >= 0)
			break; /* exit outer loop */
		node = (BSPFaceNode*)((uint8_t*)node + 3);
	}
	return result;
}

/* Polyobject types are 0xFF billboards, 0x40/0x41 line objects, and
 * 0x80..0x83 meshes. Mesh data layout:
 *   +0              PolyMeshHeader              (5 bytes)
 *   +5              face_colors[numfaces]       (1 B each, firstcoloroff)
 *   +5+nf           bbox[2] (min XYZ, max XYZ)  (2 * PolyVert = 12 B)
 *   +17+nf          points[numpoints]           (PolyVert, firstpointoff
 *                                                advanced past the bbox)
 *   +17+nf+6*np     normals[numpoints]          (PolyVert, firstvertnorm)
 *   +17+nf+12*np    faces[numfaces]             (PolyFace, firstfaceoff)
 *   +after          variable-length face bodies (firstvertptr)
 * The dedup flag enables 0x7F00 continuation markers in face bodies. */
// FUNCTION: TIE 0x1D4F0
void drawpol_drawpolyobject(const uint16_t* poly_data, int32_t obj_x, int32_t obj_y, int32_t obj_z) {
	const uint8_t* data = (const uint8_t*)poly_data;

	objectx = obj_x;
	objecty = obj_y;
	objectz = obj_z;
	objectxlo = (int16_t)obj_x;
	objectylo = (int16_t)obj_y;
	objectzlo = (int16_t)obj_z;
	objectxhi = (int16_t)(obj_x >> 16);
	objectyhi = (int16_t)(obj_y >> 16);
	objectzhi = (int16_t)(obj_z >> 16);

	uint8_t header_byte = data[0];

	/* --- Billboard/sprite path --- */
	if (header_byte == 0xFF) {
		uint8_t scale = data[3];
		numpoints = data[2];
		numeyezpos = 0;
		int32_t eye_buf[16];
		int32_t* eye_end;
		int16_t* src = (int16_t*)(data + 4);

		switch (scale) {
			case 0:
				eye_end = transfm2_geteyecoordsZ0(src, eye_buf);
				break;
			case 8:
				eye_end = transfm2_geteyecoordsZ0s8(src, eye_buf);
				break;
			case 16:
				eye_end = transfm2_geteyecoordsZ0s16(src, eye_buf);
				break;
			default:
				return;
		}
		if (!numeyezpos)
			return;

		int32_t screenxy[8] = { 0 };
		firstscreenxy = screenxy;
		minscreenx = firstscreenxy;
		maxscreenx = firstscreenxy;
		minscreeny = firstscreenxy;
		maxscreeny = firstscreenxy;
		sameycnt = 0;
		samexcnt = 0;
		counter = numpoints;
		lastscreenxy = transfm2_getscreencoords(eye_buf, firstscreenxy);
		(void)eye_end;
		trace2_drawscreencoords();
		return;
	}

	/* threedflag = type & 1; if clear, copy world->eye matrix for the
	 * precomputed-local path (rotworldeye* used directly). */
	if (header_byte & 1) {
		threedflag = 1;
	} else {
		rotworldeyeA1 = worldeyeA1;
		rotworldeyeA2 = worldeyeA2;
		rotworldeyeA3 = worldeyeA3;
		rotworldeyeB1 = worldeyeB1;
		rotworldeyeB2 = worldeyeB2;
		rotworldeyeB3 = worldeyeB3;
		rotworldeyeC1 = worldeyeC1;
		rotworldeyeC2 = worldeyeC2;
		rotworldeyeC3 = worldeyeC3;
		threedflag = 0;
	}

	/* --- Line-object path --- */
	if (header_byte < 0x80) {
		if (header_byte != 0x40 && header_byte != 0x41)
			return;
		numedges = data[3];
		numpoints = data[2];
		for (int i = 0; i < numpoints; ++i) {
			calcflag[i] = NULL;
			vertexlight[i] = 0xFFFF;
		}
		firstpointoff = (PolyVert*)(data + 4);
		edgeflags[0] = 0;
		edgeflags[1] = 0;
		edgeflags[2] = 0;
		edgeflags[3] = 0;
		numeyezpos = 0;
		firsteyexyz = (DRAWPOL_EyeVertex*)eyexyzdata;
		transfm2_geteyecoords((int16_t*)firstpointoff, eyexyzdata);
		if (!numeyezpos)
			return;

		newscreenxy = newscreenxy_buf;
		/* Edges sit immediately after numpoints PolyVerts. */
		uint8_t* edge = (uint8_t*)&firstpointoff[numpoints];
		/* Binary 0x1d8de: `firstvertptr = edge + 1` before walking edges.
		 * transfm2_facezintersect dereferences `firstvertptr - 1` to test
		 * the lighting flag (0x40 bit). For hyperstars edge[0]==0x40
		 * (thickness lo byte) so that bit IS set, and the lighting branch
		 * reads firstvertnorm[v]. The binary leaves firstvertnorm at its
		 * BSS-initialised NULL when no 3D mesh has rendered yet — on DOS
		 * that reads the interrupt vector table (garbage but valid), on
		 * a modern OS it SIGSEGVs. Pin firstvertnorm at firstpointoff so
		 * the lighting-branch read lands inside the points array (valid
		 * memory; the resulting lightVal is unused for line draws). */
		firstvertptr = edge + 1;
		firstvertnorm = firstpointoff;
		facenumber = numedges;
		do {
			int32_t* pts = transfm2_calclinepts(edge);
			if (pts) {
				uint16_t slot = flatobjnum;
				flatcolors[slot] = edge[4];
				flatx[slot] = worldx >> 5;
				flatcomponentnum[slot] = objectnum;
				flaty[slot] = worldy >> 5;
				flatparentobj[slot] = parentobject;
				flatz[slot] = worldz >> 5;
				polyidbyte = (uint16_t)(flatobjnum + 0x80);
				edgeidbyte = layervalue;
				objectedgeword = (uint16_t)((uint8_t)layervalue | (polyidbyte << 8));
				uint16_t thick = (uint16_t)(thicknessMultiple * *(uint16_t*)edge);
				int32_t avgz = ((firsteyexyz[edge[3]].z >> 1) + (firsteyexyz[edge[2]].z >> 1)) >> 8;
				if (avgz > 0)
					thick = thick / (uint16_t)avgz;
				int32_t* dst = newscreenxy;
				dst[0] = pts[0];
				dst[1] = pts[1];
				int32_t p1a = point1ptr[0];
				int32_t p1b = point1ptr[1];
				point1ptr = dst + 2;
				dst[2] = p1a;
				thickness = (uint16_t)(thick + 1);
				dst[3] = p1b;
				drawln2_tracelineedges(dst);
			}
			edge += 5;
			--facenumber;
		} while (facenumber);
		return;
	}

	/* --- 3D mesh path (0x80..0x83) --- */
	if (header_byte > 0x83)
		return;
	int use_bsp_tree = (header_byte >= 0x82);
	if (objectnum >= 0x7F)
		return;

	PolyMeshHeader* header = (PolyMeshHeader*)data;
	polyidbyte = objectnum;
	objectedgeword = (uint16_t)((uint8_t)edgeidbyte | ((uint8_t)objectnum << 8));
	polycnt = 0;
	numfaces = header->numfaces;
	numedges = header->numedges;
	numpoints = header->numpoints;

	if (newobjectdef + 2 * header->numfaces + 4 * header->numedges + 536 > 0xC000)
		return;

	uint16_t* odef = (uint16_t*)((uint8_t*)xtransdataptr + newobjectdef);
	/* Clear bytes 18..22 (6 flag bytes), then stash metadata. */
	uint8_t* obytes = (uint8_t*)odef;
	obytes[18] = 0;
	obytes[19] = 0;
	obytes[20] = 0;
	obytes[21] = 0;
	obytes[22] = 0;
	objectdef = odef;
	curobjptr = (uint8_t*)odef;
	odef[6] = parentobject;
	odef[7] = solidindex;
	odef[8] = (uint16_t)numfaces;
	/* Face indices are one-based, so initialize the inclusive sentinel range. */
	for (int i = 0; i <= numfaces; ++i)
		*(uint32_t*)&odef[2 * i + 12] = 0xFFFFFFFF;
	/* Per-vertex init. */
	for (int i = 0; i < numpoints; ++i) {
		calcflag[i] = NULL;
		vertexlight[i] = 0xFFFF;
	}
	for (int j = 0; j < numedges; ++j)
		edgeflags[j] = 0;

	/* One-shot dedup: resolve 0x7F00 continuation markers in the vertex
	 * stream by looking back into the first-N vertex records. The vstream
	 * starts at header + numfaces + 17 (verified via disasm at 0x1d3e1:
	 * `add ecx, 11h`). The 12 bytes between face_colors[numfaces] and the
	 * vstream hold a 2-point bbox (min XYZ + max XYZ as int16 triples)
	 * used by transfm2_geteyeminmax before the vertex transform. */
	if (header->dedup_flag) {
		header->dedup_flag = 0;
		uint16_t* vstream = (uint16_t*)((uint8_t*)header + numfaces + 17);
		uint16_t* w = vstream;
		for (int v = 0; v < numpoints; ++v) {
			for (int k = 0; k < 3; ++k) {
				if ((w[k] & 0xFF00) == 0x7F00) {
					/* Binary: xor ah,ah; sar ax,1 -- zeros high byte then
					 * arithmetic-shifts. Net effect on the low byte is
					 * unsigned div-by-2, back in [0..127]. */
					int back = (w[k] & 0xFF) >> 1;
					w[k] = vstream[3 * v + k - 3 * back];
				}
			}
			w += 3;
		}
	}

	firstcoloroff = (uint8_t*)header + sizeof(*header);
	/* pts_ptr points at the 2-PolyVert bbox (min XYZ, max XYZ) used for
	 * the frustum cull. Actual vertex array starts at pts_ptr + 2. */
	PolyVert* pts_ptr = (PolyVert*)(firstcoloroff + numfaces);
	closerthansizeflag = 0;
	someznegflag = 0;
	firstpointoff = pts_ptr;

	if (parentobject < 0x5000)
		transfm2_geteyeminmax((int16_t*)pts_ptr, eyexyzdata);
	else
		transfm2_geteyeminmaxS2((int16_t*)pts_ptr, eyexyzdata);

	/* eyexyzdata[0..5] = {min_x, max_x, min_y, max_y, min_z, max_z}.
	 * The buffer will be overwritten with vertex data by geteyecoords below. */
	int32_t min_x = eyexyzdata[0];
	int32_t max_x = eyexyzdata[1];
	int32_t min_y = eyexyzdata[2];
	int32_t max_y = eyexyzdata[3];
	int32_t min_z = eyexyzdata[4];
	int32_t max_z = eyexyzdata[5];

	if ((min_z >> 2) >= 0) {
		if ((min_z >> 2) < (int)(uint16_t)objectsize)
			closerthansizeflag++;
	} else {
		closerthansizeflag++;
		someznegflag++;
	}

	if (max_z >= 0) {
		/* Frustum cull: FOV=45 slope 1:1 -- |x| must be <= z inside. */
		if (max_x >= 0) {
			if (max_z <= min_x)
				return;
		} else if (-max_x >= max_z) {
			return;
		}
		if (max_y >= 0) {
			if (max_z <= min_y)
				return;
		} else if (-max_y >= max_z) {
			return;
		}

		/* World-bbox at objectdef[0..5]. */
		int16_t* odf = (int16_t*)objectdef;
		odf[0] = odf[3] = (int16_t)(worldx >> 5);
		odf[1] = odf[4] = (int16_t)(worldy >> 5);
		odf[2] = odf[5] = (int16_t)(worldz >> 5);
		if (threedflag) {
			if (parentobject < 0x5000)
				transfm2_getworldminmax((int16_t*)firstpointoff, odf);
			else
				transfm2_getworldminmaxS2((int16_t*)firstpointoff, odf);
		} else {
			/* Point-wise world-bbox: translate the 2-PolyVert poly-local
			 * bbox (firstpointoff[0] = min corner, firstpointoff[1] = max)
			 * into world coords by shifting each component right by 6
			 * (parent<0x5000) or 4 (parent>=0x5000) and adding to the
			 * world-position-shifted odf slots. All 6 components use the
			 * same shift -- the binary's apparent shift_lo/shift_hi mix
			 * was just a Watcom int32-load trick for high-word extraction. */
			int shift = (parentobject < 0x5000) ? 6 : 4;
			odf[0] += firstpointoff[0].x >> shift;
			odf[1] += firstpointoff[0].y >> shift;
			odf[2] += firstpointoff[0].z >> shift;
			odf[3] += firstpointoff[1].x >> shift;
			odf[4] += firstpointoff[1].y >> shift;
			odf[5] += firstpointoff[1].z >> shift;
		}

		/* Skip the 2-PolyVert bbox; actual vertices start at firstpointoff[2]. */
		PolyVert* verts = firstpointoff + 2;
		firsteyexyz = (DRAWPOL_EyeVertex*)eyexyzdata;
		firstpointoff = verts;
		if (parentobject < 0x5000)
			transfm2_geteyecoords((int16_t*)verts, eyexyzdata);
		else
			transfm2_geteyecoordsS2((int16_t*)verts, eyexyzdata);

		newscreenxy = newscreenxy_buf;
		/* Normals sit immediately after the vertex array; faces after normals. */
		firstvertnorm = firstpointoff + numpoints;
		firstfaceoff = (PolyFace*)(firstpointoff + 2 * numpoints);
		facenumber = numfaces;
		firstvertptr = (uint8_t*)&firstfaceoff[numfaces];

		for (int k = 0; k < numfaces; ++k) {
			doublesideflag = *firstvertptr++;
			uint8_t vis = (uint8_t)(drawpol_checknormal((uint16_t)k) >> 1);
			if (vis) {
				facevisflag[k] = vis;
			} else {
				vis = (uint8_t)(doublesideflag & 0x80);
				facevisflag[k] = 0x10;
			}
			uint8_t face_onscreen;
			if (vis) {
				if ((doublesideflag & 0x3F) == 2) {
					int32_t* lp = transfm2_calclinepts(firstvertptr);
					if (lp) {
						edgept2[firstvertptr[4]] = lp;
						edgept1[firstvertptr[4]] = point1ptr;
						face_onscreen = 4;
					} else {
						face_onscreen = 0;
					}
				} else {
					if (facevisflag[k] & 0x10) {
						rotlightX = -rotlightX;
						rotlightY = -rotlightY;
						rotlightZ = -rotlightZ;
					}
					face_onscreen = (uint8_t)transfm2_getfacescreenxy((uint16_t)(doublesideflag & 0x3F));
					if (facevisflag[k] & 0x10) {
						rotlightX = -rotlightX;
						rotlightY = -rotlightY;
						rotlightZ = -rotlightZ;
					}
				}
				if (face_onscreen == 4) {
					facevisflag[k] |= 4;
					++polycnt;
				}
			}
			--facenumber;
			firstvertptr += 2 * (doublesideflag & 0x3F) + 3;
		}

		{
			/* Accumulate stats; tie_updatescreen's end-of-frame hook
			 * flushes + resets. */

			dbg_dp_total++;
			if (polycnt == 0)
				dbg_dp_polycnt0++;
			else
				dbg_dp_polycnt_nz++;
			if (dbg_dp_total == 1) {
				dbg_dp_first_min_z = (int)min_z;
				dbg_dp_first_max_z = (int)max_z;
			}
		}
		if (polycnt) {
			objectptrs[objectnum] = newobjectdef;
			newobjectdef = (uint16_t)(newobjectdef + 2 * numfaces + 4 * numedges + 536);
			farmarkingptr[0] = firstvertptr;
			if (use_bsp_tree)
				farmarkingptr[0] += 3 * (uint16_t)numfaces;

			if (drawmarkingsflag) {
				int mcount = *(int16_t*)farmarkingptr[0];
				if (mcount && mcount + (int)nummarks < 0x80) {
					int16_t* link = (int16_t*)(farmarkingptr[0] + 3);
					uint8_t* ent = farmarkingptr[0] + 2;
					while (--mcount != -1) {
						uint16_t nm = ++nummarks;
						farmarkingptr[nm] = ent;
						markingnumber[*ent] = nm;
						farmarkingptr[nm] = &ent[*link];
						link = (int16_t*)((uint8_t*)link + 3);
						ent += 3;
					}
				}
			}

			facenumber = 1;
			if (use_bsp_tree) {
				drawpol_dobsptree(firstvertptr);
			} else {
				for (int face_i = 0; face_i < numfaces; ++face_i) {
					if (!(facevisflag[face_i] & 4))
						continue;
					firstvertptr = (uint8_t*)&firstfaceoff[face_i] + firstfaceoff[face_i].vlist_offset;
					uint8_t color_val;
					if (!(facevisflag[face_i] & 1)) {
						if (facevisflag[face_i] & 0x10) {
							if (!(firstvertptr[0] & 0x80))
								continue;
							rotlightY = -rotlightY;
							rotlightZ = -rotlightZ;
							rotlightX = -rotlightX;
							color_val = (uint8_t)drawpol_getlightvalue(firstcoloroff[face_i], face_i);
							rotlightX = -rotlightX;
							rotlightY = -rotlightY;
							rotlightZ = -rotlightZ;
						} else if (!drawpol_checknormal(face_i)) {
							if (!(firstvertptr[0] & 0x80))
								continue;
							uint8_t base_c = firstcoloroff[face_i];
							rotlightX = -rotlightX;
							rotlightY = -rotlightY;
							rotlightZ = -rotlightZ;
							color_val = (uint8_t)drawpol_getlightvalue(base_c, face_i);
							rotlightX = -rotlightX;
							rotlightY = -rotlightY;
							rotlightZ = -rotlightZ;
						} else {
							color_val = (uint8_t)drawpol_getlightvalue(firstcoloroff[face_i], face_i);
						}
					} else {
						color_val = (uint8_t)drawpol_getlightvalue(firstcoloroff[face_i], face_i);
					}
					uint8_t vcount = firstvertptr[0] & 0x3F;
					uint8_t* vlist_body = firstvertptr + 1;
					uint16_t* slot = objectdef + 268;
					int vi = facenumber - 1;
					((uint8_t*)slot)[2 * vi] = color_val;
					((uint8_t*)slot)[2 * vi + 1] = (uint8_t)face_i;
					firstvertptr = vlist_body;
					if (vcount == 2)
						drawpol_drawlineface();
					else
						trace2_drawface(vcount);
					if (markingnumber[face_i])
						drawpol_drawmarkings((uint16_t)face_i);
					++facenumber;
				}
			}

			/* Clear staged markingnumber[face] entries. */
			if (drawmarkingsflag) {
				int mcount = *(int16_t*)farmarkingptr[0];
				if (mcount) {
					uint8_t* ent = farmarkingptr[0] + 2;
					while (--mcount != -1) {
						markingnumber[*ent] = 0;
						ent += 3;
					}
				}
			}

			uint16_t saved_optr = objectptrs[objectnum];
			++objectnum;
			if ((uint16_t)numedges >= 0x80) {
				objectptrs[objectnum] = saved_optr;
				++objectnum;
			}
		}
	}
}
