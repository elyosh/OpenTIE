#ifndef TIE_TRANSFM2_H
#define TIE_TRANSFM2_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Eye-space vertex (12 bytes: 3 int32). Output of TRANSFM2_geteyecoords;
 * stored contiguously in eyexyzdata (xtrans2.c owned) and read through
 * firsteyexyz (drawpol.c owned). */
typedef struct DRAWPOL_EyeVertex {
	int32_t x;
	int32_t y;
	int32_t z;
} DRAWPOL_EyeVertex;

/* Projected face point carved from the newscreenxy pool by
 * TRANSFM2_classifyedges / TRANSFM2_calclinepts. calcflag[], edgept1[],
 * edgept2[] and trace2_lastpointPtr hold these records and TRACE2 compares
 * them by identity. xy is the {x, y} pair consumed by the pair-based
 * DRAWLN2/TRACE2 edge tracers. light is the Gouraud value interpolated by
 * TRANSFM2_facezintersect for a near-plane clip point; it is not written for
 * vertices in front of the eye (TRACE2 only reads it for clip points).
 * Retail stored clip points as a 10-byte {light, x, y} record addressed at
 * x and in-front vertices as a bare 8-byte pair. */
typedef struct TRANSFM2_ScreenPoint {
	int32_t xy[2];
	int16_t light;
} TRANSFM2_ScreenPoint;

/* World-to-eye rotation matrix (set by FVIEW) */
extern int32_t worldeyeA1, worldeyeA2, worldeyeA3;
extern int32_t worldeyeB1, worldeyeB2, worldeyeB3;
extern int32_t worldeyeC1, worldeyeC2, worldeyeC3;
extern int32_t transfm2_screenyoffset;

/* Single-point eye coordinate transforms */
int32_t transfm2_geteyex(int32_t x, int32_t y, int32_t z);
int32_t transfm2_geteyey(int32_t x, int32_t y, int32_t z);
int32_t transfm2_geteyez(int32_t x, int32_t y, int32_t z);

/* Batch transforms */
int32_t* transfm2_geteyecoords(const int16_t* source, int32_t* dest);
int32_t* transfm2_geteyecoordsS2(const int16_t* source, int32_t* dest);
/* Transform a stream of int16 XYZ triples to eye-space and store the
 * axis-aligned bounding box as dest[0..5] = {min_x, max_x, min_y, max_y,
 * min_z, max_z} -- interleaved min/max per axis, not all-min then all-max.
 * S2 variant uses a coarser right-shift (14 vs 16) for parent-object types
 * >= 0x5000. Callers (DRAWPOL_drawpolyobject) reuse the same buffer for
 * bbox then overwrite with per-vertex eye coords via geteyecoords. */
void transfm2_geteyeminmax(const int16_t* source, int32_t* dest);
void transfm2_geteyeminmaxS2(const int16_t* source, int32_t* dest);

/* Same min/max layout as geteyeminmax but in object-local (non-rotated)
 * world coords scaled by the object position. */
void transfm2_getworldminmax(const int16_t* source, int16_t* dest);
void transfm2_getworldminmaxS2(const int16_t* source, int16_t* dest);
int32_t* transfm2_geteyecoordsZ0(const int16_t* source, int32_t* dest);
int32_t* transfm2_geteyecoordsZ0s16(const int16_t* source, int32_t* dest);
int32_t* transfm2_geteyecoordsZ0s8(const int16_t* source, int32_t* dest);

/* Screen projection */
int32_t* transfm2_getscreencoords(const DRAWPOL_EyeVertex* source, int32_t* dest);
int32_t transfm2_getscreenx(int32_t eyex, int32_t eyez);
int32_t transfm2_getscreeny(int32_t eyey, int32_t eyez);
void transfm2_doxminmax(int32_t eyex, int32_t* newScreenX);
void transfm2_doyminmax(int32_t eyey, int32_t* newScreenY);

/* Z-clipping */
void transfm2_clipobjecteyez(int32_t x, int32_t y, int32_t z);
/* source[vertex] is behind the eye; its ring neighbours are
 * source[vertex - 1] and source[vertex + 1]. Closed-polygon callers
 * (drawmarkings, drawsurfacepoly) duplicate the last vertex before
 * source[0] and the first vertex after the last one. */
int32_t* transfm2_clipeyez(const DRAWPOL_EyeVertex* source, int vertex, int32_t* dest);
int32_t* transfm2_calczintersect(const DRAWPOL_EyeVertex* source1, const DRAWPOL_EyeVertex* source2,
								 int32_t* dest);
TRANSFM2_ScreenPoint* transfm2_facezintersect(int16_t negV, int16_t posV, const DRAWPOL_EyeVertex* source1,
											  const DRAWPOL_EyeVertex* source2, TRANSFM2_ScreenPoint* dest);

/* Edge/face processing */
TRANSFM2_ScreenPoint* transfm2_calclinepts(const uint8_t* source);
int16_t transfm2_getfacescreenxy(uint16_t ptCnt);
int16_t transfm2_classifyedges(void);

#ifdef __cplusplus
}
#endif

#endif
