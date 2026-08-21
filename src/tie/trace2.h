#ifndef __TRACE2_H__
#define __TRACE2_H__

#include <stdint.h>

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

/* --- Pool bases + cursors (module-owned) -------------------------
 *
 * trace2_edgeinfos / trace2_edgeheaders are bound to fediskio's
 * flightbuf_small / flightbuf_big once xtrans2_initxtrans runs. They
 * are NULL between FEDISKIO_FreeFlightHandles and the next
 * xtrans2_initxtrans; never read them before the first init. */

#define TRACE2_EDGEINFO_CAP 30720u   /* retail word_D4188 capacity */
#define TRACE2_EDGEHEADER_CAP 26016u /* retail word_D4186 capacity */

extern trace2_EdgeInfo* trace2_edgeinfos;
extern trace2_EdgeHeader* trace2_edgeheaders;
extern trace2_EdgeHeader* trace2_rowheaders[480];

extern trace2_EdgeInfo* trace2_newedgeinfo;
extern trace2_EdgeHeader* trace2_newedgeheader;
extern trace2_EdgeInfo* trace2_lastedgeinfo;
extern trace2_EdgeHeader* trace2_lastedgeheader;

/* --- Module globals written by TRACE2, read by XTRANS2 / drawxtrans --- */

extern uint16_t polyidbyte; /* u8 in practice; Watcom pads to u16 */
extern uint16_t edgeidbyte;
extern uint16_t objectedgeword; /* (polyidbyte << 8) | edgeidbyte */

extern int16_t vertlight1;
extern int16_t vertlight2;
extern int16_t lightincy; /* light gradient per scanline */
extern int16_t lightincx; /* cached adjacent to lightincy (Watcom dword load trick) */

extern int16_t someznegflag;
extern uint16_t newxblock;

extern int8_t xdiffsign;
extern int8_t ydiffsign;

/* Module state (file-static in source, but exposed through accessors in the
 * binary's register pressure tricks). Named with trace2_ prefix to avoid
 * collision with MAP/PLAYER/REGISTER modules' startx/starty/endy etc. */
extern int32_t trace2_startx;
extern int32_t trace2_starty;
extern int32_t trace2_endy;
extern uint16_t trace2_lastedge;
extern uint16_t trace2_znegflag;
extern uint16_t trace2_nummarks;
extern uint16_t trace2_edgeindex;

extern int32_t* trace2_lastpointPtr;

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
void trace2_ydownleft(uint32_t ytop, uint32_t ytotal, uint32_t xval, uint32_t slope);
void trace2_ydownright(uint32_t ytop, uint32_t ytotal, uint32_t xval, uint32_t slope);
void trace2_yupleft(uint32_t ytop, uint32_t ytotal, uint32_t xval, uint32_t slope);
void trace2_yupright(uint32_t ytop, uint32_t ytotal, uint32_t xval, uint32_t slope);
void trace2_xdownleft(uint32_t ytop, uint32_t ytotal, uint32_t xval, uint32_t slope);
void trace2_xdownright(uint32_t ytop, uint32_t ytotal, uint32_t xval, uint32_t slope);
void trace2_xupleft(uint32_t ytop, uint32_t ytotal, uint32_t xval, uint32_t slope);
void trace2_xupright(uint32_t ytop, uint32_t ytotal, uint32_t xval, uint32_t slope);

#endif
