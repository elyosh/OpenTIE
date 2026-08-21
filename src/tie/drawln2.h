#ifndef __DRAWLN2_H__
#define __DRAWLN2_H__

#include <stdint.h>

/*
 * DRAWLN2 — line-edge rasterization (1 function per watdbg).
 *
 * Sole function `drawln2_tracelineedges` classifies, clips and emits
 * edge records for a single 2-vertex line polygon into TRACE2's edge
 * list. Called from three DRAWPOL sites:
 *   - drawpol_drawlineface           (wireframe face edges)
 *   - drawpol_drawpolyobject line path (type 0x40 / 0x41 emit)
 *   - drawpol_drawmarkings            (line-marking decals)
 *
 * Input: pt2 = pointer to the end-XY dword pair {x2, y2}; pt1 is read
 * from the global point1ptr. The function updates pt1/pt2 in-place
 * during screen-bounds clipping.
 */

/* DRAWLN2-owned globals per docs/watdbg-prototypes.txt:
 *   point1ptr       @0xE2220  DWORD*  start XY-pair pointer
 *   linelightincy   @0xE2224  i16     per-scanline light gradient (y-dominant)
 *   linelightincx   @0xE2226  i16     per-column light gradient (x-dominant)
 *   thickness       @0xE2228  u16     line thickness in pixels
 *   linelight1/2    @0xE222A/C i16    per-endpoint input light values
 * Static (file-scope in the original C source):
 *   templight1/2    @0xE222E/30 i16   scratch light state
 *   ydomflag        @0xE2232  u8      0 = x-dominant, 1 = y-dominant, 2 = pre-loaded
 *   linexsign       @0xE2233  i8      sign of (pt2.x - pt1.x)
 *   lineysign[4]    @0xE2234  i8      sign of (pt2.y - pt1.y) (4 bytes reserved) */
extern int32_t* point1ptr;
extern int16_t linelightincy;
extern int16_t linelightincx;
extern uint16_t thickness;
extern int16_t linelight1;
extern int16_t linelight2;

/* Entry point. Reads/writes many globals including TRACE2 state
 * (linexsign, lineysign, xdiffsign, ydiffsign, ydomflag, lightincy,
 * lightincx, vertlight1/2, templight1/2, flatobjnum) -- see drawln2.c
 * for the full list and side-effect description. */
void drawln2_tracelineedges(int32_t* pt2);

#endif
