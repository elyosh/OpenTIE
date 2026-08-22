/* Retail polygon-edge rasterizer. */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "tie/drawpol.h"
#include "tie/logbuf2.h"
#include "tie/math2.h"
#include "tie/trace2.h"
#include "tie/xtrans2.h"

/* --- External state not owned by TRACE2 -------------------------- */

/* logbuf2.h */

/* Pool storage is allocated by FEDISKIO and bound by xtrans2_initxtrans. */

trace2_EdgeInfo* trace2_edgeinfos;
trace2_EdgeHeader* trace2_edgeheaders;
trace2_EdgeHeader* trace2_rowheaders[480];

trace2_EdgeInfo* trace2_newedgeinfo;
trace2_EdgeHeader* trace2_newedgeheader;
trace2_EdgeInfo* trace2_lastedgeinfo; /* rebound per-frame by xtrans2_initxtrans */
trace2_EdgeHeader* trace2_lastedgeheader;

/* --- Module globals --------------------------------------------- */

// GLOBAL: TIE 0xEBF2A
uint16_t polyidbyte;
// GLOBAL: TIE 0xEBF2C
uint16_t edgeidbyte;
// GLOBAL: TIE 0xEBF1C
uint16_t objectedgeword;

int16_t vertlight1;
int16_t vertlight2;
int16_t lightincy;
int16_t lightincx;

int16_t someznegflag;
uint16_t newxblock;

int8_t xdiffsign;
int8_t ydiffsign;

int32_t trace2_startx;
int32_t trace2_starty;
int32_t trace2_endy;
uint16_t trace2_lastedge;
uint16_t trace2_znegflag;
uint16_t trace2_nummarks;
uint16_t trace2_edgeindex;

int32_t* trace2_lastpointPtr;

/* --- Constants --------------------------------------------------- */

/* Retail safety clamp: max |trace2_startx| before handing off to
 * the directional tracers. 0x7F0000 = 32512 pixels in 24.8 fixed point. */
#define TRACE2_STARTX_CLAMP 0x7F0000

/* Slope cap used by both demo and retail. */
#define TRACE2_SLOPE_MAX 0x7FFFFF

/* Degenerate-slope sentinels. */
#define TRACE2_SLOPE_EQ 2
#define TRACE2_SLOPE_INF 0x7FFFFFFF

/* ================================================================ */
/*  Flat-polygon entry points                                        */
/* ================================================================ */

// FUNCTION: TIE 0x58140
void trace2_enterflatvertical(int16_t xCoord, int16_t topY, int16_t lineCnt) {
	polyidbyte = flatobjnum + 0x80;
	edgeidbyte = layervalue;
	objectedgeword = (uint16_t)((edgeidbyte & 0xFF) | ((polyidbyte & 0xFF) << 8));
	trace2_entervertedge(topY, lineCnt, xCoord, 0);
}

// FUNCTION: TIE 0x5818C
void trace2_entervertedge(int16_t topY, int16_t lineCnt, int16_t xCoord, int16_t lightVal) {
	if (lineCnt == 0)
		return;

	/* Link a new EdgeHeader at rowheaders[topY]. */
	trace2_EdgeHeader* h = trace2_newedgeheader;
	h->next = trace2_rowheaders[(uint16_t)topY];
	trace2_rowheaders[(uint16_t)topY] = h;
	h->numscanlines = lineCnt;
	h->objectid = polyidbyte;
	h->face1 = facenumber;
	h->face2 = 0;
	h->rightedge = NULL;
	h->edgeid = edgeidbyte;
	h->info = trace2_newedgeinfo;
	if (++trace2_newedgeheader > trace2_lastedgeheader)
		trace2_newedgeheader = trace2_lastedgeheader;

	/* Fill lineCnt EdgeInfo entries with constant (x = xCoord<<8, lt = lightVal). */
	const int32_t xpacked = (int32_t)((uint32_t)(int32_t)xCoord << 8);
	int16_t i = lineCnt;
	while (--i != -1) {
		trace2_newedgeinfo->x = xpacked;
		trace2_newedgeinfo->lt = lightVal;
		++trace2_newedgeinfo;
	}

	if (trace2_newedgeinfo > trace2_lastedgeinfo)
		trace2_newedgeinfo = trace2_lastedgeinfo;
}

/* ================================================================ */
/*  y-dominant directional tracers                                   */
/*                                                                   */
/*  |dy| >= |dx|. Primary loop walks one scanline per iteration with */
/*  a Bresenham-style x-step accumulator. slope is in 24.8 fixed;    */
/*  slope>>9 = integer y-per-x-step, slope>>1 & 0xFF = initial frac. */
/*                                                                   */
/*  "down" variants (y increases): fill forward.                     */
/*  "up"   variants (y decreases): allocate ytotal info slots then   */
/*                                 fill backwards so records stay in */
/*                                 top-to-bottom scanline order.     */
/* ================================================================ */

// FUNCTION: TIE 0x58238
void trace2_ydownleft(uint32_t ytop, uint32_t ytotal, uint32_t xval, uint32_t slope) {
	int32_t ytotala = (int32_t)ytotal;
	if (ytop + ytotal > pixelsdeep)
		ytotala = (int32_t)pixelsdeep - (int32_t)ytop;
	if (ytotala == 0)
		return;

	/* Allocate header at rowheaders[ytop]. */
	trace2_EdgeHeader* h = trace2_newedgeheader;
	h->next = trace2_rowheaders[ytop];
	trace2_rowheaders[ytop] = h;
	h->numscanlines = ytotala;
	h->objectid = polyidbyte;
	h->face1 = facenumber;
	h->face2 = 0;
	h->rightedge = NULL;
	h->edgeid = edgeidbyte;
	h->info = trace2_newedgeinfo;
	if (++trace2_newedgeheader > trace2_lastedgeheader)
		trace2_newedgeheader = trace2_lastedgeheader;

	int32_t ltval = vertlight1;
	int32_t xvala = (int32_t)(xval << 8);
	int32_t ycnt = (int32_t)(slope >> 9);
	uint32_t yval = (slope >> 1) & 0xFF;
	if (ycnt > ytotala)
		ycnt = ytotala;
	int32_t ytotalb = ytotala - ycnt;

	/* First run: ycnt scanlines of constant xvala. */
	while (--ycnt != -1) {
		trace2_newedgeinfo->x = xvala;
		trace2_newedgeinfo->lt = ltval;
		ltval += lightincy;
		++trace2_newedgeinfo;
	}

	/* Subsequent runs: xvalb = xvala - 256, keep stepping -256 each
	 * time the fractional accumulator overflows. */
	int32_t xvalb = xvala - 256;
	while (ytotalb > 0) {
		yval += slope;
		int32_t ycnta = (int32_t)(yval >> 8);
		yval &= 0xFF;
		if (ycnta > ytotalb)
			ycnta = ytotalb;
		ytotalb -= ycnta;
		while (--ycnta != -1) {
			trace2_newedgeinfo->x = xvalb;
			trace2_newedgeinfo->lt = ltval;
			ltval += lightincy;
			++trace2_newedgeinfo;
		}
		xvalb -= 256;
	}

	if (trace2_newedgeinfo > trace2_lastedgeinfo)
		trace2_newedgeinfo = trace2_lastedgeinfo;
}

// FUNCTION: TIE 0x58374
void trace2_ydownright(uint32_t ytop, uint32_t ytotal, uint32_t xval, uint32_t slope) {
	int32_t ytotala = (int32_t)ytotal;
	if (ytop + ytotal > pixelsdeep)
		ytotala = (int32_t)pixelsdeep - (int32_t)ytop;
	if (ytotala == 0)
		return;

	trace2_EdgeHeader* h = trace2_newedgeheader;
	h->next = trace2_rowheaders[ytop];
	trace2_rowheaders[ytop] = h;
	h->numscanlines = ytotala;
	h->objectid = polyidbyte;
	h->face1 = facenumber;
	h->face2 = 0;
	h->rightedge = NULL;
	h->edgeid = edgeidbyte;
	h->info = trace2_newedgeinfo;
	if (++trace2_newedgeheader > trace2_lastedgeheader)
		trace2_newedgeheader = trace2_lastedgeheader;

	int32_t ltval = vertlight1;
	int32_t xvala = (int32_t)(xval << 8);
	int32_t ycnt = (int32_t)(slope >> 9);
	uint32_t yval = (slope >> 1) & 0xFF;
	if (ycnt > ytotala)
		ycnt = ytotala;
	int32_t ytotalb = ytotala - ycnt;

	while (--ycnt != -1) {
		trace2_newedgeinfo->x = xvala;
		trace2_newedgeinfo->lt = ltval;
		ltval += lightincy;
		++trace2_newedgeinfo;
	}

	int32_t xvalb = xvala + 256;
	while (ytotalb > 0) {
		yval += slope;
		int32_t ycnta = (int32_t)(yval >> 8);
		yval &= 0xFF;
		if (ycnta > ytotalb)
			ycnta = ytotalb;
		ytotalb -= ycnta;
		while (--ycnta != -1) {
			trace2_newedgeinfo->x = xvalb;
			trace2_newedgeinfo->lt = ltval;
			ltval += lightincy;
			++trace2_newedgeinfo;
		}
		xvalb += 256;
	}

	if (trace2_newedgeinfo > trace2_lastedgeinfo)
		trace2_newedgeinfo = trace2_lastedgeinfo;
}

// FUNCTION: TIE 0x584B0
void trace2_yupleft(uint32_t ytop, uint32_t ytotal, uint32_t xval, uint32_t slope) {
	int32_t ytopa = (int32_t)(ytop - ytotal);
	int32_t ytotala = (int32_t)ytotal;
	if (ytopa < 0) {
		ytotala = ytotala + ytopa; /* equals ytop */
		ytopa = 0;
	}
	if (ytotala == 0)
		return;

	trace2_EdgeHeader* h = trace2_newedgeheader;
	h->next = trace2_rowheaders[ytopa];
	trace2_rowheaders[ytopa] = h;
	h->numscanlines = ytotala;
	h->objectid = polyidbyte;
	h->face1 = facenumber;
	h->face2 = 0;
	h->rightedge = NULL;
	h->edgeid = edgeidbyte;
	h->info = trace2_newedgeinfo;
	if (++trace2_newedgeheader > trace2_lastedgeheader)
		trace2_newedgeheader = trace2_lastedgeheader;

	/* Allocate ytotala slots forward, then fill backwards. */
	trace2_newedgeinfo += ytotala;
	trace2_EdgeInfo* infoptr = trace2_newedgeinfo - 1;

	int32_t ltval = vertlight1;
	int32_t xvala = (int32_t)(xval << 8);
	int32_t ycnt = (int32_t)(slope >> 9);
	uint32_t yval = (slope >> 1) & 0xFF;
	if (ycnt > ytotala)
		ycnt = ytotala;
	int32_t ytotalb = ytotala - ycnt;

	while (--ycnt != -1) {
		infoptr->x = xvala;
		infoptr->lt = ltval;
		ltval -= lightincy;
		--infoptr;
	}

	int32_t xvalb = xvala - 256;
	while (ytotalb > 0) {
		yval += slope;
		int32_t ycnta = (int32_t)(yval >> 8);
		yval &= 0xFF;
		if (ycnta > ytotalb)
			ycnta = ytotalb;
		ytotalb -= ycnta;
		while (--ycnta != -1) {
			infoptr->x = xvalb;
			infoptr->lt = ltval;
			ltval -= lightincy;
			--infoptr;
		}
		xvalb -= 256;
	}

	if (trace2_newedgeinfo > trace2_lastedgeinfo)
		trace2_newedgeinfo = trace2_lastedgeinfo;
}

// FUNCTION: TIE 0x58614
void trace2_yupright(uint32_t ytop, uint32_t ytotal, uint32_t xval, uint32_t slope) {
	int32_t ytopa = (int32_t)(ytop - ytotal);
	int32_t ytotala = (int32_t)ytotal;
	if (ytopa < 0) {
		ytotala = ytotala + ytopa;
		ytopa = 0;
	}
	if (ytotala == 0)
		return;

	trace2_EdgeHeader* h = trace2_newedgeheader;
	h->next = trace2_rowheaders[ytopa];
	trace2_rowheaders[ytopa] = h;
	h->numscanlines = ytotala;
	h->objectid = polyidbyte;
	h->face1 = facenumber;
	h->face2 = 0;
	h->rightedge = NULL;
	h->edgeid = edgeidbyte;
	h->info = trace2_newedgeinfo;
	if (++trace2_newedgeheader > trace2_lastedgeheader)
		trace2_newedgeheader = trace2_lastedgeheader;

	trace2_newedgeinfo += ytotala;
	trace2_EdgeInfo* infoptr = trace2_newedgeinfo - 1;

	int32_t ltval = vertlight1;
	int32_t xvala = (int32_t)(xval << 8);
	int32_t ycnt = (int32_t)(slope >> 9);
	uint32_t yval = (slope >> 1) & 0xFF;
	if (ycnt > ytotala)
		ycnt = ytotala;
	int32_t ytotalb = ytotala - ycnt;

	while (--ycnt != -1) {
		infoptr->x = xvala;
		infoptr->lt = ltval;
		ltval -= lightincy;
		--infoptr;
	}

	int32_t xvalb = xvala + 256;
	while (ytotalb > 0) {
		yval += slope;
		int32_t ycnta = (int32_t)(yval >> 8);
		yval &= 0xFF;
		if (ycnta > ytotalb)
			ycnta = ytotalb;
		ytotalb -= ycnta;
		while (--ycnta != -1) {
			infoptr->x = xvalb;
			infoptr->lt = ltval;
			ltval -= lightincy;
			--infoptr;
		}
		xvalb += 256;
	}

	if (trace2_newedgeinfo > trace2_lastedgeinfo)
		trace2_newedgeinfo = trace2_lastedgeinfo;
}

/* ================================================================ */
/*  x-dominant directional tracers                                   */
/*                                                                   */
/*  |dx| > |dy|. One EdgeInfo per scanline; x advances by ±slope     */
/*  (slope = x-per-1y in 24.8 fixed). No fractional-y accumulator.   */
/*  Pre-biased by ±slope/2 for mid-pixel sampling.                   */
/* ================================================================ */

// FUNCTION: TIE 0x58778
void trace2_xdownleft(uint32_t ytop, uint32_t ytotal, uint32_t xval, uint32_t slope) {
	int32_t ytotala = (int32_t)ytotal;
	if (ytop + ytotal > pixelsdeep)
		ytotala = (int32_t)pixelsdeep - (int32_t)ytop;
	if (ytotala == 0)
		return;

	trace2_EdgeHeader* h = trace2_newedgeheader;
	h->next = trace2_rowheaders[ytop];
	trace2_rowheaders[ytop] = h;
	h->numscanlines = ytotala;
	h->objectid = polyidbyte;
	h->face1 = facenumber;
	h->face2 = 0;
	h->rightedge = NULL;
	h->edgeid = edgeidbyte;
	h->info = trace2_newedgeinfo;
	if (++trace2_newedgeheader > trace2_lastedgeheader)
		trace2_newedgeheader = trace2_lastedgeheader;

	int32_t xvala = (int32_t)(xval << 8) - ((int32_t)slope >> 1);
	int32_t ltval = vertlight1 + (lightincy >> 1);
	while (--ytotala != -1) {
		trace2_newedgeinfo->x = xvala;
		trace2_newedgeinfo->lt = ltval;
		ltval += lightincy;
		xvala -= (int32_t)slope;
		++trace2_newedgeinfo;
	}

	if (trace2_newedgeinfo > trace2_lastedgeinfo)
		trace2_newedgeinfo = trace2_lastedgeinfo;
}

// FUNCTION: TIE 0x5885C
void trace2_xdownright(uint32_t ytop, uint32_t ytotal, uint32_t xval, uint32_t slope) {
	int32_t ytotala = (int32_t)ytotal;
	if (ytop + ytotal > pixelsdeep)
		ytotala = (int32_t)pixelsdeep - (int32_t)ytop;
	if (ytotala == 0)
		return;

	trace2_EdgeHeader* h = trace2_newedgeheader;
	h->next = trace2_rowheaders[ytop];
	trace2_rowheaders[ytop] = h;
	h->numscanlines = ytotala;
	h->objectid = polyidbyte;
	h->face1 = facenumber;
	h->face2 = 0;
	h->rightedge = NULL;
	h->edgeid = edgeidbyte;
	h->info = trace2_newedgeinfo;
	if (++trace2_newedgeheader > trace2_lastedgeheader)
		trace2_newedgeheader = trace2_lastedgeheader;

	int32_t xvala = ((int32_t)slope >> 1) + (int32_t)(xval << 8);
	int32_t ltval = vertlight1 + (lightincy >> 1);
	while (--ytotala != -1) {
		trace2_newedgeinfo->x = xvala;
		trace2_newedgeinfo->lt = ltval;
		ltval += lightincy;
		xvala += (int32_t)slope;
		++trace2_newedgeinfo;
	}

	if (trace2_newedgeinfo > trace2_lastedgeinfo)
		trace2_newedgeinfo = trace2_lastedgeinfo;
}

// FUNCTION: TIE 0x58940
void trace2_xupleft(uint32_t ytop, uint32_t ytotal, uint32_t xval, uint32_t slope) {
	int32_t ytopa = (int32_t)(ytop - ytotal);
	int32_t ytotala = (int32_t)ytotal;
	if (ytopa < 0) {
		ytotala = ytotala + ytopa;
		ytopa = 0;
	}
	if (ytotala == 0)
		return;

	trace2_EdgeHeader* h = trace2_newedgeheader;
	h->next = trace2_rowheaders[ytopa];
	trace2_rowheaders[ytopa] = h;
	h->numscanlines = ytotala;
	h->objectid = polyidbyte;
	h->face1 = facenumber;
	h->face2 = 0;
	h->rightedge = NULL;
	h->edgeid = edgeidbyte;
	h->info = trace2_newedgeinfo;
	if (++trace2_newedgeheader > trace2_lastedgeheader)
		trace2_newedgeheader = trace2_lastedgeheader;

	trace2_newedgeinfo += ytotala;
	trace2_EdgeInfo* infoptr = trace2_newedgeinfo - 1;

	int32_t xvala = (int32_t)(xval << 8) - ((int32_t)slope >> 1);
	int32_t ltval = vertlight1 - (lightincy >> 1);
	while (--ytotala != -1) {
		infoptr->x = xvala;
		infoptr->lt = ltval;
		ltval -= lightincy;
		xvala -= (int32_t)slope;
		--infoptr;
	}

	if (trace2_newedgeinfo > trace2_lastedgeinfo)
		trace2_newedgeinfo = trace2_lastedgeinfo;
}

// FUNCTION: TIE 0x58A24
void trace2_xupright(uint32_t ytop, uint32_t ytotal, uint32_t xval, uint32_t slope) {
	int32_t ytopa = (int32_t)(ytop - ytotal);
	int32_t ytotala = (int32_t)ytotal;
	if (ytopa < 0) {
		ytotala = ytotala + ytopa;
		ytopa = 0;
	}
	if (ytotala == 0)
		return;

	trace2_EdgeHeader* h = trace2_newedgeheader;
	h->next = trace2_rowheaders[ytopa];
	trace2_rowheaders[ytopa] = h;
	h->numscanlines = ytotala;
	h->objectid = polyidbyte;
	h->face1 = facenumber;
	h->face2 = 0;
	h->rightedge = NULL;
	h->edgeid = edgeidbyte;
	h->info = trace2_newedgeinfo;
	if (++trace2_newedgeheader > trace2_lastedgeheader)
		trace2_newedgeheader = trace2_lastedgeheader;

	trace2_newedgeinfo += ytotala;
	trace2_EdgeInfo* infoptr = trace2_newedgeinfo - 1;

	int32_t xvala = ((int32_t)slope >> 1) + (int32_t)(xval << 8);
	int32_t ltval = vertlight1 - (lightincy >> 1);
	while (--ytotala != -1) {
		infoptr->x = xvala;
		infoptr->lt = ltval;
		ltval -= lightincy;
		xvala += (int32_t)slope;
		--infoptr;
	}

	if (trace2_newedgeinfo > trace2_lastedgeinfo)
		trace2_newedgeinfo = trace2_lastedgeinfo;
}

/* ================================================================ */
/*  Clippers                                                         */
/* ================================================================ */

// FUNCTION: TIE 0x58B08
void trace2_ydomclipy(int32_t x1, int32_t y1, int32_t x2, int32_t y2, int32_t slope, uint16_t fraction) {
	const int32_t pd = (int32_t)pixelsdeep;

	if (y1 >= 0 && y1 < pd) {
		if (y2 >= 0 && y2 < pd) {
			if (y2 >= y1) {
				trace2_starty = y1;
				trace2_startx = x1;
				trace2_endy = y2;
			} else {
				trace2_endy = y1;
				trace2_starty = y2;
				trace2_startx = x2;
				int16_t t = vertlight1;
				vertlight1 = vertlight2;
				vertlight2 = t;
				xdiffsign = (int8_t)(-xdiffsign);
			}
		} else {
			trace2_starty = y1;
			trace2_startx = x1;
			if (y2 < 0)
				trace2_endy = 0;
			else if (y2 <= pd)
				trace2_endy = y2;
			else
				trace2_endy = pd;
		}
	} else if (y2 >= 0 && y2 < pd) {
		trace2_starty = y2;
		trace2_startx = x2;
		int16_t t = vertlight1;
		vertlight1 = vertlight2;
		vertlight2 = t;
		xdiffsign = (int8_t)(-xdiffsign);
		if (y1 < 0)
			trace2_endy = 0;
		else if (y1 <= pd)
			trace2_endy = y1;
		else
			trace2_endy = pd;
	} else {
		/* Both endpoints out of range.
		 * Note: y1c/y1a * lightincy uses unsigned u32 multiplication to match
		 * the binary's 32-bit imul wrap semantics without tripping C's signed-
		 * overflow UB. Both operands are cast to u32; the product is cast back
		 * to i32 and truncated to i16. */
		int32_t dx_at_clip;
		int32_t clipped_x;
		if (y1 >= 0) {
			/* y1 >= pixelsdeep: clip to bottom. */
			int32_t y1c = y1 - pd;
			int32_t lt_delta = (int32_t)((uint32_t)y1c * (uint32_t)(int32_t)lightincy);
			vertlight1 -= (int16_t)lt_delta;
			if ((uint32_t)slope <= 0xFFFFu)
				dx_at_clip =
					math2_ABoverC32(y1c, 0x10000, (int32_t)((uint32_t)fraction + ((uint32_t)slope << 16)));
			else
				dx_at_clip = y1c / slope;
			clipped_x = (xdiffsign >= 0) ? (dx_at_clip + x1) : (x1 - dx_at_clip);
			trace2_starty = pd;
		} else {
			/* y1 < 0: clip to top. */
			int32_t y1a = -y1;
			int32_t lt_delta = (int32_t)((uint32_t)y1a * (uint32_t)(int32_t)lightincy);
			vertlight1 += (int16_t)lt_delta;
			if ((uint32_t)slope <= 0xFFFFu)
				dx_at_clip = math2_ABoverC32(y1a, 0x10000, (int32_t)(fraction + (slope << 16)));
			else
				dx_at_clip = y1a / slope;
			clipped_x = (xdiffsign >= 0) ? (dx_at_clip + x1) : (x1 - dx_at_clip);
			trace2_starty = 0;
		}
		trace2_startx = clipped_x;
		if (y2 < 0)
			trace2_endy = 0;
		else if (y2 <= pd)
			trace2_endy = y2;
		else
			trace2_endy = pd;
	}
}

// FUNCTION: TIE 0x58D4C
void trace2_xdomclipy(int32_t x1, int32_t y1, int32_t x2, int32_t y2, int32_t slope, uint16_t fraction) {
	const int32_t pd = (int32_t)pixelsdeep;

	if (y1 >= 0 && y1 < pd) {
		if (y2 >= 0 && y2 < pd) {
			if (y2 >= y1) {
				trace2_starty = y1;
				trace2_startx = x1;
				trace2_endy = y2;
			} else {
				trace2_endy = y1;
				trace2_starty = y2;
				trace2_startx = x2;
				int16_t t = vertlight1;
				vertlight1 = vertlight2;
				vertlight2 = t;
				xdiffsign = (int8_t)(-xdiffsign);
			}
		} else {
			trace2_starty = y1;
			trace2_startx = x1;
			if (y2 < 0)
				trace2_endy = 0;
			else if (y2 <= pd)
				trace2_endy = y2;
			else
				trace2_endy = pd;
		}
	} else if (y2 >= 0 && y2 < pd) {
		trace2_starty = y2;
		trace2_startx = x2;
		int16_t t = vertlight1;
		vertlight1 = vertlight2;
		vertlight2 = t;
		xdiffsign = (int8_t)(-xdiffsign);
		if (y1 < 0)
			trace2_endy = 0;
		else if (y1 <= pd)
			trace2_endy = y1;
		else
			trace2_endy = pd;
	} else {
		/* Note: slope * y1c and y1c * lightincy both use unsigned u32
		 * multiplication to match the binary's 32-bit imul wrap semantics
		 * without tripping C's signed-overflow UB. slope can be as large as
		 * 0x7FFFFF and y1c up to several hundred, so the product easily
		 * exceeds INT32_MAX. */
		int32_t dx_at_clip;
		int32_t clipped_x;
		if (y1 >= 0) {
			int32_t y1c = y1 - pd;
			int32_t lt_delta = (int32_t)((uint32_t)y1c * (uint32_t)(int32_t)lightincy);
			int32_t slope_delta = (int32_t)((uint32_t)slope * (uint32_t)y1c);
			vertlight1 -= (int16_t)lt_delta;
			dx_at_clip = math2_longfraction(y1c, fraction) + slope_delta;
			clipped_x = (xdiffsign >= 0) ? (dx_at_clip + x1) : (x1 - dx_at_clip);
			trace2_starty = pd;
		} else {
			int32_t y1a = -y1;
			int32_t lt_delta = (int32_t)((uint32_t)y1a * (uint32_t)(int32_t)lightincy);
			int32_t slope_delta = (int32_t)((uint32_t)slope * (uint32_t)y1a);
			vertlight1 += (int16_t)lt_delta;
			dx_at_clip = math2_longfraction(y1a, fraction) + slope_delta;
			clipped_x = (xdiffsign >= 0) ? (dx_at_clip + x1) : (x1 - dx_at_clip);
			trace2_starty = 0;
		}
		trace2_startx = clipped_x;
		if (y2 < 0)
			trace2_endy = 0;
		else if (y2 <= pd)
			trace2_endy = y2;
		else
			trace2_endy = pd;
	}
}

/* ================================================================ */
/*  Domain-specific dispatchers (retail variant with clamp)          */
/* ================================================================ */

// FUNCTION: TIE 0x59AC0
void trace2_ydomedge(int32_t slope, uint16_t fraction, int32_t* pt1, int32_t* pt2) {
	int32_t slopea = slope;
	trace2_ydomclipy(pt1[0], pt1[1], pt2[0], pt2[1], slope, fraction);
	if (slopea > TRACE2_SLOPE_MAX)
		slopea = TRACE2_SLOPE_MAX;
	/* slopea << 8 is done through u32 to avoid signed-shift UB; slopea is
	 * clamped positive above but a negative `slope` arg would still hit UB
	 * under signed semantics. */
	int32_t slopeb = ((int32_t)fraction >> 8) + (int32_t)((uint32_t)slopea << 8);

	/* Retail-only: clamp trace2_startx to ±0x7F0000 before dispatch. */
	if (trace2_startx > TRACE2_STARTX_CLAMP)
		trace2_startx = TRACE2_STARTX_CLAMP;
	if (trace2_startx < -TRACE2_STARTX_CLAMP)
		trace2_startx = -TRACE2_STARTX_CLAMP;

	if (trace2_starty >= trace2_endy) {
		uint32_t linesa = (uint32_t)(trace2_starty - trace2_endy);
		if (xdiffsign >= 0)
			trace2_yupright((uint32_t)trace2_starty, linesa, (uint32_t)trace2_startx, slopeb);
		else
			trace2_yupleft((uint32_t)trace2_starty, linesa, (uint32_t)trace2_startx, slopeb);
	} else {
		uint32_t lines = (uint32_t)(trace2_endy - trace2_starty);
		if (xdiffsign >= 0)
			trace2_ydownright((uint32_t)trace2_starty, lines, (uint32_t)trace2_startx, slopeb);
		else
			trace2_ydownleft((uint32_t)trace2_starty, lines, (uint32_t)trace2_startx, slopeb);
	}
}

// FUNCTION: TIE 0x59BB0
void trace2_xdomedge(int32_t slope, uint16_t fraction, int32_t* pt1, int32_t* pt2) {
	int32_t slopea = slope;
	trace2_xdomclipy(pt1[0], pt1[1], pt2[0], pt2[1], slope, fraction);
	if (slopea > TRACE2_SLOPE_MAX)
		slopea = TRACE2_SLOPE_MAX;
	/* slopea << 8 is done through u32 to avoid signed-shift UB; slopea is
	 * clamped positive above but a negative `slope` arg would still hit UB
	 * under signed semantics. */
	int32_t slopeb = ((int32_t)fraction >> 8) + (int32_t)((uint32_t)slopea << 8);

	if (trace2_startx > TRACE2_STARTX_CLAMP)
		trace2_startx = TRACE2_STARTX_CLAMP;
	if (trace2_startx < -TRACE2_STARTX_CLAMP)
		trace2_startx = -TRACE2_STARTX_CLAMP;

	if (trace2_starty >= trace2_endy) {
		uint32_t linesa = (uint32_t)(trace2_starty - trace2_endy);
		if (xdiffsign >= 0)
			trace2_xupright((uint32_t)trace2_starty, linesa, (uint32_t)trace2_startx, slopeb);
		else
			trace2_xupleft((uint32_t)trace2_starty, linesa, (uint32_t)trace2_startx, slopeb);
	} else {
		uint32_t lines = (uint32_t)(trace2_endy - trace2_starty);
		if (xdiffsign >= 0)
			trace2_xdownright((uint32_t)trace2_starty, lines, (uint32_t)trace2_startx, slopeb);
		else
			trace2_xdownleft((uint32_t)trace2_starty, lines, (uint32_t)trace2_startx, slopeb);
	}
}

/* ================================================================ */
/*  findlastedge                                                     */
/* ================================================================ */

// FUNCTION: TIE 0x59A78
void trace2_findlastedge(void) {
	uint16_t edge = (uint16_t)numedges;
	uint16_t index = 0;
	while (edge > 0 && (edgeflags[index] & 0x2A) != 0) {
		--edge;
		++index;
	}
	if (edge == 0)
		edge = 1;
	trace2_lastedge = (uint16_t)((uint16_t)numedges - edge);
}

/* ================================================================ */
/*  drawscreencoords — flat-polygon tracer                           */
/* ================================================================ */

// FUNCTION: TIE 0x58F4C
void trace2_drawscreencoords(void) {
	const int32_t pd = (int32_t)pixelsdeep;
	const int32_t pw = (int32_t)pixelswide;

	/* Early-exit on full-screen AABB miss. */
	if (maxscreeny[1] < 0)
		return;
	if (maxscreenx[0] < 0)
		return;
	if (pd <= minscreeny[1])
		return;
	if (pw <= minscreenx[0])
		return;

	if (flatobjnum == 111)
		return;

	const uint16_t slot = flatobjnum;
	flatcolors[slot] = color;
	flatcomponentnum[slot] = (uint8_t)objectnum;
	flatparentobj[slot] = parentobject;
	flatz[slot] = -32767;
	flatobjnum += 1;

	polyidbyte = slot + 0x80;
	edgeidbyte = layervalue;
	objectedgeword = (uint16_t)((edgeidbyte & 0xFF) | ((polyidbyte & 0xFF) << 8));

	if (numpoints == samexcnt) {
		/* Vertical-line degenerate: two 1-px vertical edges spanning y-range. */
		int16_t y0 = 0;
		if (minscreeny[1] >= 0)
			y0 = (int16_t)minscreeny[1];
		int16_t y1 = (int16_t)pd;
		if (maxscreeny[1] >= 0 && pd > maxscreeny[1])
			y1 = (int16_t)maxscreeny[1];
		const int16_t ydelta = (int16_t)(y1 - y0);
		const int16_t x0 = (int16_t)maxscreeny[1];
		trace2_entervertedge(y0, ydelta, (int16_t)(uint8_t)x0, x0);
		trace2_entervertedge(y0, ydelta, (int16_t)(uint8_t)(x0 + 1), (int16_t)(x0 + 1));
		return;
	}

	if (numpoints == sameycnt) {
		/* Horizontal-line degenerate. */
		trace2_entervertedge((int16_t)(minscreeny[1]), 1, (int16_t)minscreenx[0], 0);
		trace2_entervertedge((int16_t)(minscreeny[1]), 1, (int16_t)maxscreenx[0], 0);
		return;
	}

	/* General: walk the vertex ring, classify each edge, dispatch. */
	int32_t* last = minscreeny;
	for (;;) {
		int32_t* first = last;
		last = first + 2;
		if (last == lastscreenxy)
			last = firstscreenxy;

		/* Retail uses wrapping 32-bit sub/neg here. Projected endpoints can
		 * sit near opposite INT32 limits, so express those operations unsigned. */
		ydiffsign = 1;
		uint32_t ydiff = (uint32_t)last[1] - (uint32_t)first[1];
		int skip = 0;
		if ((int32_t)ydiff < 0) {
			ydiffsign = -1;
			ydiff = -ydiff;
			if (first[1] < 0 || pd <= last[1])
				skip = 1;
		} else {
			if (ydiff == 0 || last[1] < 0 || pd <= first[1])
				skip = 1;
		}

		if (!skip) {
			xdiffsign = 1;
			uint32_t xdiff = (uint32_t)last[0] - (uint32_t)first[0];
			if ((int32_t)xdiff < 0) {
				xdiffsign = -1;
				xdiff = -xdiff;
			}

			const int32_t half_ydiff = (int32_t)ydiff >> 1;
			if (half_ydiff > (int32_t)xdiff) {
				/* y-dominant: slope = ydiff/xdiff */
				int32_t slope;
				uint16_t frac;
				if (xdiff != 0) {
					slope = (int32_t)(ydiff / xdiff);
					uint32_t rem = ydiff % xdiff;
					frac = (uint16_t)(((uint64_t)rem << 32) / xdiff >> 16);
				} else {
					slope = TRACE2_SLOPE_INF;
					frac = 0;
				}
				trace2_ydomedge(slope, frac, first, last);
			} else if (half_ydiff == (int32_t)xdiff) {
				trace2_ydomedge(TRACE2_SLOPE_EQ, 0, first, last);
			} else {
				/* x-dominant: slope = xdiff/ydiff */
				int32_t slope;
				uint16_t frac;
				if (ydiff != 0) {
					slope = (int32_t)(xdiff / ydiff);
					uint32_t rem = xdiff % ydiff;
					frac = (uint16_t)(((uint64_t)rem << 32) / ydiff >> 16);
				} else {
					slope = TRACE2_SLOPE_INF;
					frac = 0;
				}
				trace2_xdomedge(slope, frac, first, last);
			}
		}

		if (last == minscreeny)
			return;
	}
}

/* ================================================================ */
/*  drawface — lit-polygon tracer                                    */
/* ================================================================ */

// FUNCTION: TIE 0x59278
void trace2_drawface(uint16_t numberOfVertices) {
	trace2_znegflag = 0;

	uint16_t vertexIndex = 0;
	counter = (int16_t)(uint8_t)numberOfVertices;
	while (counter > 0) {
		const uint8_t vtx1 = firstvertptr[vertexIndex];
		const uint8_t edgeIdx = firstvertptr[vertexIndex + 1];
		const uint8_t vtx2 = firstvertptr[vertexIndex + 2];
		vertlight1 = (int16_t)vertexlight[vtx1];
		vertlight2 = (int16_t)vertexlight[vtx2];

		/* Swap lights if this face walks the edge in the reverse direction. */
		int32_t* edgePt = calcflag[vtx1];
		if (edgePt == edgept2[edgeIdx]) {
			int16_t t = vertlight1;
			vertlight1 = vertlight2;
			vertlight2 = t;
		}
		if (edgePt == NULL)
			++trace2_znegflag;

		polyidbyte = objectnum;
		edgeidbyte = edgeIdx;
		objectedgeword = (uint16_t)(edgeIdx | ((objectnum & 0xFF) << 8));

		const uint8_t pt = edgeflags[edgeIdx];
		if (pt == XTRANS2_EDGEFLAG_YDOM || pt == (XTRANS2_EDGEFLAG_HAS_HEADER | XTRANS2_EDGEFLAG_YDOM) ||
			(pt & XTRANS2_EDGEFLAG_DEGEN) != 0) {
			/* Off-right sentinel (0x80 alone), already-headered, or degenerate.
			 * Just remember as lastedge candidate for z-clip synthesis. */
			if ((pt & XTRANS2_EDGEFLAG_HAS_HEADER) == 0)
				trace2_lastedge = edgeIdx;
		} else if ((pt & XTRANS2_EDGEFLAG_HAS_HEADER) != 0) {
			/* Second face touching this edge — fill face2 on cached header. */
			trace2_EdgeHeader* h = (trace2_EdgeHeader*)edgeflagptr[edgeIdx];
			if (edgeidbyte == h->edgeid)
				h->face2 = facenumber;
		} else {
			/* New edge: install header + cache slope, then dispatch. */
			edgeflags[edgeIdx] |= XTRANS2_EDGEFLAG_HAS_HEADER;
			edgeflagptr[edgeIdx] = trace2_newedgeheader;
			++trace2_edgeindex;

			if ((pt & XTRANS2_EDGEFLAG_UNUSED_10) != 0) {
				/* DEAD CODE: bit 0x10 is never set by TRANSFM2_classifyedges
				 * or anywhere else in demo or retail. The adjacent-face
				 * orientation flip that this branch was presumably intended
				 * for is actually performed inside classifyedges itself
				 * (flips signs + swaps edgept1/edgept2 in place when a
				 * cached flag is seen). Preserved bit-literal to match
				 * the binary exactly. */
				uintptr_t v1 = (uintptr_t)edgept1[edgeIdx];
				uintptr_t v2 = (uintptr_t)edgept2[edgeIdx];
				v1 = (v1 & ~(uintptr_t)0xFFFF) | (~v1 & 0xFFFF);
				v2 = (v2 & ~(uintptr_t)0xFFFF) | (~v2 & 0xFFFF);
				edgept1[edgeIdx] = (int32_t*)v1;
				edgept2[edgeIdx] = (int32_t*)v2;
			}

			if (gauraudflag) {
				/* Gouraud gradient: lightincy = 2 * |dlight| / ydiff, signed
				 * by (edgeysign XOR -sign_flag) high bit. */
				int16_t dlt_raw = vertlight2 - vertlight1;
				int sign_flag = 0;
				if (dlt_raw < 0) {
					sign_flag = 1;
					dlt_raw = -dlt_raw;
				}
				int16_t dlt_half = dlt_raw >> 1;
				int32_t div = 0;
				if (dlt_half > edgeydiff[edgeIdx]) {
					if (edgeydiff[edgeIdx] != 0)
						div = dlt_half / edgeydiff[edgeIdx];
					if (div > 255)
						div &= 0xFFFE;
					if (((edgeysign[edgeIdx] ^ (uint16_t)(-sign_flag)) & 0x8000u) != 0)
						div = -div;
				}
				lightincy = (int16_t)(2 * div);
			}
			xdiffsign = edgexsign[edgeIdx];
			ydiffsign = edgeysign[edgeIdx];

			if ((edgeflags[edgeIdx] & XTRANS2_EDGEFLAG_SLOPE_CACHED) == 0) {
				edgeflags[edgeIdx] |= XTRANS2_EDGEFLAG_SLOPE_CACHED;

				const uint32_t dy = (uint32_t)edgeydiff[edgeIdx];
				const uint32_t dx = (uint32_t)edgexdiff[edgeIdx];
				int32_t slope;
				uint16_t frac;

				if ((dy / 2) > dx) {
					/* y-dominant */
					if (dx != 0) {
						slope = (int32_t)(dy / dx);
						uint32_t rem = dy % dx;
						frac = (uint16_t)(((uint64_t)rem << 32) / dx >> 16);
					} else {
						slope = TRACE2_SLOPE_INF;
						frac = 0;
					}
					edgeslopefrac[edgeIdx] = (int16_t)frac;
					edgeslopelo[edgeIdx] = (int16_t)slope;
					edgeslopehi[edgeIdx] = (int16_t)(slope >> 16);
					edgeflags[edgeIdx] |= XTRANS2_EDGEFLAG_YDOM;
				} else if ((dy / 2) == dx) {
					slope = TRACE2_SLOPE_EQ;
					frac = 0;
					edgeslopefrac[edgeIdx] = 0;
					edgeslopelo[edgeIdx] = TRACE2_SLOPE_EQ;
					edgeslopehi[edgeIdx] = 0;
					edgeflags[edgeIdx] |= XTRANS2_EDGEFLAG_YDOM;
				} else {
					/* x-dominant */
					if (dy != 0) {
						slope = (int32_t)(dx / dy);
						uint32_t rem = dx % dy;
						frac = (uint16_t)(((uint64_t)rem << 32) / dy >> 16);
					} else {
						slope = TRACE2_SLOPE_INF;
						frac = 0;
					}
					edgeslopefrac[edgeIdx] = (int16_t)frac;
					edgeslopelo[edgeIdx] = (int16_t)slope;
					edgeslopehi[edgeIdx] = (int16_t)(slope >> 16);
				}

				if ((edgeflags[edgeIdx] & XTRANS2_EDGEFLAG_YDOM) != 0)
					trace2_ydomedge(slope, frac, edgept1[edgeIdx], edgept2[edgeIdx]);
				else
					trace2_xdomedge(slope, frac, edgept1[edgeIdx], edgept2[edgeIdx]);
			}
		}

		vertexIndex += 2;
		--counter;
	}

	/* --- Z-clip repair. When 0 < znegflag < numverts, synthesise a
	 * boundary edge between the first/last visible vertices. --- */
	if (trace2_znegflag == 0 || trace2_znegflag == numberOfVertices)
		return;

	uint16_t vi = 0;
	int32_t* edgePtc = NULL;

	if (calcflag[firstvertptr[0]]) {
		/* First vertex visible. Walk forward until we lose visibility. */
		do {
			vi += 2;
		} while (calcflag[firstvertptr[vi]]);
		int32_t* cf_prev = calcflag[firstvertptr[vi - 2]];
		trace2_lastpointPtr = (cf_prev == edgept1[firstvertptr[vi - 1]]) ? edgept2[firstvertptr[vi - 1]]
																		 : edgept1[firstvertptr[vi - 1]];
		int32_t* cf_here;
		do {
			vi += 2;
			cf_here = calcflag[firstvertptr[vi]];
		} while (!cf_here);
		edgePtc = (cf_here == edgept1[firstvertptr[vi - 1]]) ? edgept2[firstvertptr[vi - 1]]
															 : edgept1[firstvertptr[vi - 1]];
	} else {
		/* First vertex invisible. Skip to first visible, then lose-and-regain. */
		int32_t* cf_here;
		do {
			vi += 2;
			cf_here = calcflag[firstvertptr[vi]];
		} while (!cf_here);
		int32_t* edgePta = (cf_here == edgept1[firstvertptr[vi - 1]]) ? edgept2[firstvertptr[vi - 1]]
																	  : edgept1[firstvertptr[vi - 1]];
		trace2_lastpointPtr = edgePta;
		do {
			vi += 2;
		} while (calcflag[firstvertptr[vi]]);
		int32_t* cf_prev = calcflag[firstvertptr[vi - 2]];
		edgePtc = (cf_prev == edgept1[firstvertptr[vi - 1]]) ? edgept2[firstvertptr[vi - 1]]
															 : edgept1[firstvertptr[vi - 1]];
	}

	/* Read u16 light from 2 bytes BEFORE the screen-point pointer. This is
	 * safe for edges reached from the z-clip repair branch because:
	 *
	 * - The repair branch fires only when trace2_znegflag > 0, i.e. at
	 *   least one vertex has calcflag[v] == NULL (behind near plane).
	 * - For such vertices, classifyedges went through the else branch of
	 *   its "eyez >= 0" check and called TRANSFM2_facezintersect for the
	 *   containing edge.
	 * - facezintersect allocates a 10-byte record [u16 light][i32 x][i32 y]
	 *   and returns a pointer to the x field — stored in edgept1/edgept2.
	 * - Therefore (u16)(edgept - 2) = light, always, for these edges.
	 *
	 * (The 8-byte {x,y} records produced when eyez >= 0 don't have a
	 * light at -2, but the repair branch never reads those as pointers.) */
	vertlight1 = *(((int16_t*)trace2_lastpointPtr) - 1);
	vertlight2 = *(((int16_t*)edgePtc) - 1);

	/* Same wrapping sub/neg sequence as the original x86 near-plane repair. */
	ydiffsign = 1;
	uint32_t ydiff = (uint32_t)edgePtc[1] - (uint32_t)trace2_lastpointPtr[1];
	if ((int32_t)ydiff < 0) {
		ydiffsign = -1;
		ydiff = -ydiff;
		if (trace2_lastpointPtr[1] < 0 || (int32_t)pixelsdeep <= edgePtc[1])
			return;
	} else {
		if (ydiff == 0 || edgePtc[1] < 0 || (int32_t)pixelsdeep <= trace2_lastpointPtr[1])
			return;
	}

	xdiffsign = 1;
	uint32_t xdiff = (uint32_t)edgePtc[0] - (uint32_t)trace2_lastpointPtr[0];
	if ((int32_t)xdiff < 0) {
		xdiffsign = -1;
		xdiff = -xdiff;
	}

	if (trace2_lastedge == 0)
		trace2_findlastedge();

	edgeidbyte = trace2_lastedge;
	objectedgeword = (uint16_t)((trace2_lastedge & 0xFF) | (polyidbyte << 8));

	if (gauraudflag) {
		/* Z-clip Gouraud: same formula as main loop but uses the locally
		 * computed ydiff/ydiffsign instead of edgeydiff[]/edgeysign[]. */
		int16_t dlt_raw = vertlight2 - vertlight1;
		int sign_flag = 0;
		if (dlt_raw < 0) {
			sign_flag = 1;
			dlt_raw = -dlt_raw;
		}
		int16_t dlt_half = dlt_raw >> 1;
		int32_t div = 0;
		if (dlt_half > (int32_t)ydiff) {
			if (ydiff != 0)
				div = dlt_half / (int32_t)ydiff;
			if ((div & 0xFF00) != 0)
				div &= 0xFFFE; /* BYTE1(div) != 0 */
			if (((ydiffsign ^ (uint16_t)(-sign_flag)) & 0x8000u) != 0)
				div = -div;
		}
		lightincy = (int16_t)(2 * div);
	}

	int32_t slope;
	uint16_t frac;
	if ((ydiff >> 1) > xdiff) {
		if (xdiff != 0) {
			slope = (int32_t)(ydiff / xdiff);
			uint32_t rem = ydiff % xdiff;
			frac = (uint16_t)(((uint64_t)rem << 32) / xdiff >> 16);
		} else {
			slope = TRACE2_SLOPE_INF;
			frac = 0;
		}
		edgeflags[trace2_lastedge] |= XTRANS2_EDGEFLAG_HAS_HEADER;
		edgeflagptr[trace2_lastedge] = trace2_newedgeheader;
		trace2_ydomedge(slope, frac, trace2_lastpointPtr, edgePtc);
	} else if ((ydiff >> 1) == xdiff) {
		edgeflags[trace2_lastedge] |= XTRANS2_EDGEFLAG_HAS_HEADER;
		edgeflagptr[trace2_lastedge] = trace2_newedgeheader;
		trace2_ydomedge(TRACE2_SLOPE_EQ, 0, trace2_lastpointPtr, edgePtc);
	} else {
		if (ydiff != 0) {
			slope = (int32_t)(xdiff / ydiff);
			uint32_t rem = xdiff % ydiff;
			frac = (uint16_t)(((uint64_t)rem << 32) / ydiff >> 16);
		} else {
			slope = TRACE2_SLOPE_INF;
			frac = 0;
		}
		edgeflags[trace2_lastedge] |= XTRANS2_EDGEFLAG_HAS_HEADER;
		edgeflagptr[trace2_lastedge] = trace2_newedgeheader;
		trace2_xdomedge(slope, frac, trace2_lastpointPtr, edgePtc);
	}
	trace2_lastedge = 0;
}
