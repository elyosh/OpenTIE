#ifndef TIE_TRACE2_H
#define TIE_TRACE2_H

#include "tie/transfm2.h" /* TRANSFM2_ScreenPoint */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Converts projected polygons into per-scanline edges consumed by XTRANS2.
 * The edge pools are bound to FEDISKIO flight buffers by xtrans2_initxtrans. */

/* --- Pool record types ------------------------------------------- */

typedef struct trace2_EdgeInfo {
	int32_t x;  /* 24.8 fixed point within the scanline */
	int32_t lt; /* light value along the edge (signed) */
} trace2_EdgeInfo;

typedef struct trace2_EdgeHeader {
	trace2_EdgeInfo* info; /* first EdgeInfo for this edge */
	int32_t numscanlines;  /* live scanline count, aged by drawxtrans */
	uint32_t objectid;     /* polyidbyte */
	uint32_t face1;
	uint32_t face2;
	uint32_t edgeid;                     /* edgeidbyte */
	struct trace2_EdgeHeader* rightedge; /* lazy-paired right edge (populated by XTRANS2) */
	struct trace2_EdgeHeader* next;      /* next in the active chain */
} trace2_EdgeHeader;

/* --- Pool cursors (module-owned) ---------------------------------
 *
 * xtrans2_initxtrans points the cursors at fediskio's flightbuf_small_handle /
 * flightbuf_big_handle and derives the last-slot clamps; they are invalid between
 * FEDISKIO_FreeFlightHandles and the next xtrans2_initxtrans. */

#define TRACE2_EDGEINFO_CAP 30720u   /* flightbuf_small_handle capacity */
#define TRACE2_EDGEHEADER_CAP 26000u /* flightbuf_big_handle capacity */

extern trace2_EdgeHeader* trace2_rowheaders[480];

extern trace2_EdgeInfo* trace2_newedgeinfo;
extern trace2_EdgeHeader* trace2_newedgeheader;
extern trace2_EdgeInfo* trace2_lastedgeinfo;
extern trace2_EdgeHeader* trace2_lastedgeheader;

/* --- Module globals written by TRACE2, read by XTRANS2 / drawxtrans --- */

extern uint8_t polyidbyte;
extern uint8_t edgeidbyte;
extern uint16_t objectedgeword; /* (polyidbyte << 8) | edgeidbyte */

extern int16_t vertlight1;
extern int16_t vertlight2;
extern int16_t lightincy; /* light gradient per scanline */
extern int16_t lightincx; /* cached adjacent to lightincy (Watcom dword load trick) */

extern int16_t someznegflag;

extern int8_t xdiffsign;
extern int8_t ydiffsign;

/* Module state (file-static in source, but exposed through accessors in the
 * binary's register pressure tricks). Named with trace2_ prefix to avoid
 * collision with MAP/PLAYER/REGISTER modules' startx/starty/endy etc. */
extern int32_t trace2_startx;
extern int32_t trace2_starty;
extern int32_t trace2_endy;
extern int16_t trace2_lastedge;
extern uint16_t trace2_znegflag;

extern TRANSFM2_ScreenPoint* trace2_lastpointPtr;

/* --- API -------------------------------------------------------- */

void trace2_drawface(uint16_t numberOfVertices);
void trace2_drawscreencoords(void);
void trace2_findlastedge(void);

void trace2_ydomedge(int32_t slope, uint16_t fraction, int32_t* pt1, int32_t* pt2);
void trace2_xdomedge(int32_t slope, uint16_t fraction, int32_t* pt1, int32_t* pt2);

void trace2_ydomclipy(int32_t x1, int32_t y1, int32_t x2, int32_t y2, int32_t slope, uint16_t fraction);
void trace2_xdomclipy(int32_t x1, int32_t y1, int32_t x2, int32_t y2, int32_t slope, uint16_t fraction);

void trace2_enterflatvertical(int16_t xCoord, int16_t topY, int16_t lineCnt);
void trace2_entervertedge(int16_t topY, int16_t lineCnt, int16_t xCoord, int16_t lightVal);

/* 8 directional Bresenham tracers (|dy|>=|dx| y-dom, |dx|>|dy| x-dom;
 * direction given by sign of dy in screen space and xdiffsign). */
void trace2_ydownleft(int32_t ytop, int32_t ytotal, int32_t xval, int32_t slope);
void trace2_ydownright(uint32_t ytop, uint32_t ytotal, uint32_t xval, uint32_t slope);
void trace2_yupleft(int32_t ytop, int32_t ytotal, int32_t xval, int32_t slope);
void trace2_yupright(int32_t ytop, int32_t ytotal, int32_t xval, int32_t slope);
void trace2_xdownleft(uint32_t ytop, uint32_t ytotal, uint32_t xval, uint32_t slope);
void trace2_xdownright(uint32_t ytop, uint32_t ytotal, uint32_t xval, uint32_t slope);
void trace2_xupleft(uint32_t ytop, uint32_t ytotal, uint32_t xval, uint32_t slope);
void trace2_xupright(uint32_t ytop, uint32_t ytotal, uint32_t xval, uint32_t slope);

#ifdef __cplusplus
}
#endif

#endif
