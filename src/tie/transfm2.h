#ifndef __TRANSFM2_H__
#define __TRANSFM2_H__

#include <stdint.h>

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
int32_t transfm2_getscreencoordx(int32_t eyex, int32_t eyez);
int32_t transfm2_getscreencoordy(int32_t eyey, int32_t eyez);
int32_t* transfm2_getscreencoords(int32_t* source, int32_t* dest);
int32_t transfm2_getscreenx(int32_t eyex, int32_t eyez);
int32_t transfm2_getscreeny(int32_t eyey, int32_t eyez);
void transfm2_doxminmax(int32_t eyex, int32_t* newScreenX);
void transfm2_doyminmax(int32_t eyey, int32_t* newScreenY);

/* Z-clipping */
void transfm2_clipobjecteyez(int32_t x, int32_t y, int32_t z);
int32_t* transfm2_clipeyez(int32_t* source, int32_t* dest);
int32_t* transfm2_calczintersect(int32_t* source1, int32_t* source2, int32_t* dest);
int32_t* transfm2_facezintersect(int16_t negV, int16_t posV, int32_t* source1, int32_t* source2,
								 int32_t* dest);

/* Edge/face processing */
int32_t* transfm2_calclinepts(const uint8_t* source);
int16_t transfm2_getfacescreenxy(uint16_t ptCnt);
int16_t transfm2_classifyedges(void);

#endif
