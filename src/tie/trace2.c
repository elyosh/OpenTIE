/* Retail polygon-edge rasterizer. */

#include "tie/trace2.h"
#include "tie/drawpol.h"
#include "tie/logbuf2.h"
#include "tie/math2.h"
#include "tie/xtrans2.h"
#include "tie_runtime/runtime/wide_arithmetic.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* --- External state not owned by TRACE2 -------------------------- */

/* logbuf2.h */

/* Pool storage is allocated by FEDISKIO; xtrans2_initxtrans locks it into
 * the running cursors each frame and derives the overflow clamps. */

// GLOBAL: TIE95 0xEB778
trace2_EdgeHeader* trace2_rowheaders[480];
// GLOBAL: TIE95 0xEBEF8
TRANSFM2_ScreenPoint* trace2_lastpointPtr;
// GLOBAL: TIE95 0xEBEFC
int32_t trace2_startx;
// GLOBAL: TIE95 0xEBF00
int32_t trace2_starty;
// GLOBAL: TIE95 0xEBF04
int32_t trace2_endy;
// GLOBAL: TIE95 0xEBF08
trace2_EdgeHeader* trace2_newedgeheader;
// GLOBAL: TIE95 0xEBF0C
trace2_EdgeInfo* trace2_newedgeinfo;
// GLOBAL: TIE95 0xEBF10
trace2_EdgeInfo* trace2_lastedgeinfo;
// GLOBAL: TIE95 0xEBF14
trace2_EdgeHeader* trace2_lastedgeheader;

/* --- Module globals --------------------------------------------- */

// GLOBAL: TIE95 0xEBF18
int16_t vertlight1;
// GLOBAL: TIE95 0xEBF1A
int16_t vertlight2;
// GLOBAL: TIE95 0xEBF1C
uint16_t objectedgeword;
// GLOBAL: TIE95 0xEBF1E
int16_t trace2_lastedge;
// GLOBAL: TIE95 0xEBF20
uint16_t trace2_znegflag;
// GLOBAL: TIE95 0xEBF22
int16_t lightincy;
// GLOBAL: TIE95 0xEBF24
int16_t lightincx;
// GLOBAL: TIE95 0xEBF26
int16_t someznegflag;
// GLOBAL: TIE95 0xEBF2A
uint8_t polyidbyte;
// GLOBAL: TIE95 0xEBF2B
int8_t xdiffsign;
// GLOBAL: TIE95 0xEBF2C
uint8_t edgeidbyte;
// GLOBAL: TIE95 0xEBF2D
int8_t ydiffsign;

/* --- Constants --------------------------------------------------- */

/* Retail safety clamp: max |trace2_startx| before handing off to
 * the directional tracers. 0x7F0000 = 32512 pixels in 24.8 fixed point. */
enum {
	TRACE2_STARTX_CLAMP = 0x7F0000,
};

/* Slope cap used by both demo and retail. */
enum {
	TRACE2_SLOPE_MAX = 0x7FFFFF,
};

/* Degenerate-slope sentinels. */
enum {
	TRACE2_SLOPE_EQ = 2,
	TRACE2_SLOPE_INF = 0x7FFFFFFF,
};

/* Edge slope as a 16.16 quotient: *slope = dividend / divisor and *frac the
 * high 16 bits of the remainder's 32-bit fraction. A zero divisor yields
 * TRACE2_SLOPE_INF with a zero fraction. drawface inlines this at every use;
 * other toolchains compute the same values with math2_mul_div_u32. */
#ifdef __WATCOMC__
void trace2_slopediv(uint32_t dividend, uint32_t divisor, int32_t* slope, uint16_t* frac);
#pragma aux trace2_slopediv = "or ebx, ebx"                                                                  \
							  "jnz sd_divide"                                                                \
							  "mov dword ptr [esi], 7FFFFFFFh"                                               \
							  "mov [edi], bx"                                                                \
							  "jmp sd_done"                                                                  \
							  "sd_divide: xor edx, edx"                                                      \
							  "div ebx"                                                                      \
							  "mov [esi], eax"                                                               \
							  "xor eax, eax"                                                                 \
							  "div ebx"                                                                      \
							  "shr eax, 16"                                                                  \
							  "mov [edi], ax"                                                                \
							  "sd_done:" parm[eax][ebx][esi][edi] modify exact[eax edx];
#endif

/* ================================================================ */
/*  Flat-polygon entry points                                        */
/* ================================================================ */

// FUNCTION: TIE95 0x58140
void trace2_enterflatvertical(int16_t xCoord, int16_t topY, int16_t lineCnt) {
	polyidbyte = flatobjnum + 0x80;
	edgeidbyte = layervalue;
	objectedgeword = (uint16_t)((edgeidbyte & 0xFF) | ((polyidbyte & 0xFF) << 8));
	trace2_entervertedge(topY, lineCnt, xCoord, 0);
}

// FUNCTION: TIE95 0x5818C
void trace2_entervertedge(int16_t topY, int16_t lineCnt, int16_t xCoord, int16_t lightVal) {
	trace2_EdgeInfo* info;
	trace2_EdgeHeader* h;
	int32_t x;
	int32_t lt;

	info = trace2_newedgeinfo;
	if (lineCnt == 0)
		return;

	/* Link a new EdgeHeader at rowheaders[topY]. */
	h = trace2_newedgeheader;
	h->next = trace2_rowheaders[topY];
	trace2_rowheaders[topY] = h;
	h->numscanlines = lineCnt;
	h->objectid = polyidbyte;
	h->face1 = facenumber;
	h->face2 = 0;
	h->rightedge = NULL;
	h->edgeid = edgeidbyte;
	h->info = info;
	if (++trace2_newedgeheader > trace2_lastedgeheader)
		trace2_newedgeheader = trace2_lastedgeheader;

	/* Fill lineCnt EdgeInfo entries with constant (x = xCoord<<8, lt = lightVal). */
	x = (int32_t)xCoord << 8;
	lt = lightVal;
	while (lineCnt-- != 0) {
		info->x = x;
		info->lt = lt;
		info++;
	}

	if (info > trace2_lastedgeinfo)
		info = trace2_lastedgeinfo;
	trace2_newedgeinfo = info;
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

// FUNCTION: TIE95 0x58238
void trace2_ydownleft(int32_t ytop, int32_t ytotal, int32_t xval, int32_t slope) {
	trace2_EdgeHeader* h;
	int32_t ltval;
	int32_t ycnt;
	int32_t frac;

	if (ytop + ytotal > pixelsdeep)
		ytotal = pixelsdeep - ytop;
	if (ytotal == 0)
		return;

	/* Allocate header at rowheaders[ytop]. */
	h = trace2_newedgeheader;
	h->next = trace2_rowheaders[ytop];
	trace2_rowheaders[ytop] = h;
	h->numscanlines = ytotal;
	h->objectid = polyidbyte;
	h->face1 = facenumber;
	h->face2 = 0;
	h->rightedge = NULL;
	h->edgeid = edgeidbyte;
	h->info = trace2_newedgeinfo;
	if (++trace2_newedgeheader > trace2_lastedgeheader)
		trace2_newedgeheader = trace2_lastedgeheader;

	ltval = vertlight1;
	xval = (int32_t)((uint32_t)xval << 8);
	frac = slope >> 1;
	ycnt = frac;
	ycnt >>= 8;
	frac &= 0xFF;
	if (ycnt > ytotal)
		ycnt = ytotal;
	ytotal -= ycnt;

	/* First run: ycnt scanlines at the starting x. */
	while (--ycnt != -1) {
		trace2_newedgeinfo->x = xval;
		trace2_newedgeinfo->lt = ltval;
		ltval += lightincy;
		++trace2_newedgeinfo;
	}

	/* Subsequent runs step x left one pixel each time the fractional
	 * accumulator overflows. */
	xval -= 256;
	while (ytotal != 0) {
		frac += slope;
		ycnt = frac >> 8;
		frac &= 0xFF;
		if (ycnt > ytotal)
			ycnt = ytotal;
		ytotal -= ycnt;
		while (--ycnt != -1) {
			trace2_newedgeinfo->x = xval;
			trace2_newedgeinfo->lt = ltval;
			ltval += lightincy;
			++trace2_newedgeinfo;
		}
		xval -= 256;
	}

	if (trace2_newedgeinfo > trace2_lastedgeinfo)
		trace2_newedgeinfo = trace2_lastedgeinfo;
}

// FUNCTION: TIE95 0x58374
void trace2_ydownright(uint32_t ytop, uint32_t ytotal, uint32_t xval, uint32_t slope) {
	int32_t ytotala = (int32_t)ytotal;
	trace2_EdgeHeader* h;
	int32_t ltval;
	int32_t xvala;
	int32_t ycnt;
	uint32_t yval;
	int32_t ytotalb;
	int32_t xvalb;

	if (ytop + ytotal > pixelsdeep)
		ytotala = (int32_t)pixelsdeep - (int32_t)ytop;
	if (ytotala == 0)
		return;

	h = trace2_newedgeheader;
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

	ltval = vertlight1;
	xvala = (int32_t)(xval << 8);
	ycnt = (int32_t)(slope >> 9);
	yval = (slope >> 1) & 0xFF;
	if (ycnt > ytotala)
		ycnt = ytotala;
	ytotalb = ytotala - ycnt;

	while (--ycnt != -1) {
		trace2_newedgeinfo->x = xvala;
		trace2_newedgeinfo->lt = ltval;
		ltval += lightincy;
		++trace2_newedgeinfo;
	}

	xvalb = xvala + 256;
	while (ytotalb > 0) {
		int32_t ycnta;

		yval += slope;
		ycnta = (int32_t)(yval >> 8);
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

// FUNCTION: TIE95 0x584B0
void trace2_yupleft(int32_t ytop, int32_t ytotal, int32_t xval, int32_t slope) {
	trace2_EdgeInfo* info;
	int32_t ltval;
	int32_t ycnt;
	int32_t frac;
	int32_t ystart;

	ystart = ytop - ytotal;
	if (ystart < 0) {
		ytotal += ystart; /* equals ytop */
		ystart = 0;
	}
	if (ytotal == 0)
		return;

	/* Allocate header at rowheaders[ystart]. */
	trace2_newedgeheader->next = trace2_rowheaders[ystart];
	trace2_rowheaders[ystart] = trace2_newedgeheader;
	trace2_newedgeheader->numscanlines = ytotal;
	trace2_newedgeheader->objectid = polyidbyte;
	trace2_newedgeheader->face1 = facenumber;
	trace2_newedgeheader->face2 = 0;
	trace2_newedgeheader->rightedge = NULL;
	trace2_newedgeheader->edgeid = edgeidbyte;
	trace2_newedgeheader->info = trace2_newedgeinfo;
	if (++trace2_newedgeheader > trace2_lastedgeheader)
		trace2_newedgeheader = trace2_lastedgeheader;

	/* Allocate ytotal slots forward, then fill backwards. */
	ltval = vertlight1;
	xval = (int32_t)((uint32_t)xval << 8);
	frac = slope >> 1;
	info = trace2_newedgeinfo + ytotal - 1;
	trace2_newedgeinfo += ytotal;
	ycnt = frac;
	ycnt >>= 8;
	frac &= 0xFF;
	if (ycnt > ytotal)
		ycnt = ytotal;
	ytotal -= ycnt;

	/* First run: ycnt scanlines at the starting x. */
	while (--ycnt != -1) {
		info->x = xval;
		info->lt = ltval;
		ltval -= lightincy;
		--info;
	}

	/* Subsequent runs step x left one pixel each time the fractional
	 * accumulator overflows. */
	xval -= 256;
	while (ytotal != 0) {
		frac += slope;
		ycnt = frac >> 8;
		frac &= 0xFF;
		if (ycnt > ytotal)
			ycnt = ytotal;
		ytotal -= ycnt;
		while (--ycnt != -1) {
			info->x = xval;
			info->lt = ltval;
			ltval -= lightincy;
			--info;
		}
		xval -= 256;
	}

	if (trace2_newedgeinfo > trace2_lastedgeinfo)
		trace2_newedgeinfo = trace2_lastedgeinfo;
}

// FUNCTION: TIE95 0x58614
void trace2_yupright(int32_t ytop, int32_t ytotal, int32_t xval, int32_t slope) {
	trace2_EdgeInfo* info;
	int32_t ltval;
	int32_t ycnt;
	int32_t frac;
	int32_t ystart;

	ystart = ytop - ytotal;
	if (ystart < 0) {
		ytotal += ystart; /* equals ytop */
		ystart = 0;
	}
	if (ytotal == 0)
		return;

	/* Allocate header at rowheaders[ystart]. */
	trace2_newedgeheader->next = trace2_rowheaders[ystart];
	trace2_rowheaders[ystart] = trace2_newedgeheader;
	trace2_newedgeheader->numscanlines = ytotal;
	trace2_newedgeheader->objectid = polyidbyte;
	trace2_newedgeheader->face1 = facenumber;
	trace2_newedgeheader->face2 = 0;
	trace2_newedgeheader->rightedge = NULL;
	trace2_newedgeheader->edgeid = edgeidbyte;
	trace2_newedgeheader->info = trace2_newedgeinfo;
	if (++trace2_newedgeheader > trace2_lastedgeheader)
		trace2_newedgeheader = trace2_lastedgeheader;

	/* Allocate ytotal slots forward, then fill backwards. */
	ltval = vertlight1;
	xval = (int32_t)((uint32_t)xval << 8);
	frac = slope >> 1;
	info = trace2_newedgeinfo + ytotal - 1;
	trace2_newedgeinfo += ytotal;
	ycnt = frac;
	ycnt >>= 8;
	frac &= 0xFF;
	if (ycnt > ytotal)
		ycnt = ytotal;
	ytotal -= ycnt;

	/* First run: ycnt scanlines at the starting x. */
	while (--ycnt != -1) {
		info->x = xval;
		info->lt = ltval;
		ltval -= lightincy;
		--info;
	}

	/* Subsequent runs step x right one pixel each time the fractional
	 * accumulator overflows. */
	xval += 256;
	while (ytotal != 0) {
		frac += slope;
		ycnt = frac >> 8;
		frac &= 0xFF;
		if (ycnt > ytotal)
			ycnt = ytotal;
		ytotal -= ycnt;
		while (--ycnt != -1) {
			info->x = xval;
			info->lt = ltval;
			ltval -= lightincy;
			--info;
		}
		xval += 256;
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

// FUNCTION: TIE95 0x58778
void trace2_xdownleft(uint32_t ytop, uint32_t ytotal, uint32_t xval, uint32_t slope) {
	int32_t ytotala = (int32_t)ytotal;
	trace2_EdgeHeader* h;
	int32_t xvala;
	int32_t ltval;

	if (ytop + ytotal > pixelsdeep)
		ytotala = (int32_t)pixelsdeep - (int32_t)ytop;
	if (ytotala == 0)
		return;

	h = trace2_newedgeheader;
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

	xvala = (int32_t)(xval << 8) - ((int32_t)slope >> 1);
	ltval = vertlight1 + (lightincy >> 1);
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

// FUNCTION: TIE95 0x5885C
void trace2_xdownright(uint32_t ytop, uint32_t ytotal, uint32_t xval, uint32_t slope) {
	int32_t ytotala = (int32_t)ytotal;
	trace2_EdgeHeader* h;
	int32_t xvala;
	int32_t ltval;

	if (ytop + ytotal > pixelsdeep)
		ytotala = (int32_t)pixelsdeep - (int32_t)ytop;
	if (ytotala == 0)
		return;

	h = trace2_newedgeheader;
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

	xvala = ((int32_t)slope >> 1) + (int32_t)(xval << 8);
	ltval = vertlight1 + (lightincy >> 1);
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

// FUNCTION: TIE95 0x58940
void trace2_xupleft(uint32_t ytop, uint32_t ytotal, uint32_t xval, uint32_t slope) {
	int32_t ytopa = (int32_t)(ytop - ytotal);
	int32_t ytotala = (int32_t)ytotal;
	trace2_EdgeHeader* h;
	int32_t row;
	int32_t xvala;
	int32_t ltval;

	if (ytopa < 0) {
		ytotala = ytotala + ytopa;
		ytopa = 0;
	}
	if (ytotala == 0)
		return;

	h = trace2_newedgeheader;
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
	row = ytotala;

	xvala = (int32_t)(xval << 8) - ((int32_t)slope >> 1);
	ltval = vertlight1 - (lightincy >> 1);
	while (--ytotala != -1) {
		--row;
		h->info[row].x = xvala;
		h->info[row].lt = ltval;
		ltval -= lightincy;
		xvala -= (int32_t)slope;
	}

	if (trace2_newedgeinfo > trace2_lastedgeinfo)
		trace2_newedgeinfo = trace2_lastedgeinfo;
}

// FUNCTION: TIE95 0x58A24
void trace2_xupright(uint32_t ytop, uint32_t ytotal, uint32_t xval, uint32_t slope) {
	trace2_EdgeInfo* info = trace2_newedgeinfo;
	int32_t ytopa = (int32_t)(ytop - ytotal);
	int32_t ytotala = (int32_t)ytotal;
	trace2_EdgeHeader* h;
	trace2_EdgeInfo* last;
	int32_t row;
	int32_t xvala;
	int32_t ltval;

	if (ytopa < 0) {
		ytotala = ytotala + ytopa;
		ytopa = 0;
	}
	if (ytotala == 0)
		return;

	h = trace2_newedgeheader;
	h->next = trace2_rowheaders[ytopa];
	trace2_rowheaders[ytopa] = h;
	h->numscanlines = ytotala;
	h->objectid = polyidbyte;
	h->face1 = facenumber;
	h->face2 = 0;
	h->rightedge = NULL;
	h->edgeid = edgeidbyte;
	h->info = info;
	if (++trace2_newedgeheader > trace2_lastedgeheader)
		trace2_newedgeheader = trace2_lastedgeheader;

	xvala = ((int32_t)slope >> 1) + (int32_t)(xval << 8);
	ltval = vertlight1 - (lightincy >> 1);
	last = info + ytotala;
	row = ytotala;
	while (--ytotala != -1) {
		--row;
		info[row].x = xvala;
		info[row].lt = ltval;
		ltval -= lightincy;
		xvala += (int32_t)slope;
	}

	if (last > trace2_lastedgeinfo)
		last = trace2_lastedgeinfo;
	trace2_newedgeinfo = last;
}

/* ================================================================ */
/*  Clippers                                                         */
/* ================================================================ */

// FUNCTION: TIE95 0x58B08
void trace2_ydomclipy(int32_t x1, int32_t y1, int32_t x2, int32_t y2, int32_t slope, uint16_t fraction) {
	int16_t t;

	if (y1 >= 0 && y1 < pixelsdeep) {
		if (y2 >= 0 && y2 < pixelsdeep) {
			if (y2 < y1) {
				trace2_endy = y1;
				trace2_startx = x2;
				t = vertlight2;
				vertlight2 = vertlight1;
				vertlight1 = t;
				trace2_starty = y2;
				xdiffsign = (int8_t)(-xdiffsign);
			} else {
				trace2_starty = y1;
				trace2_startx = x1;
				trace2_endy = y2;
			}
		} else {
			trace2_starty = y1;
			trace2_startx = x1;
			if (y2 < 0)
				trace2_endy = 0;
			else if (y2 > pixelsdeep)
				trace2_endy = pixelsdeep;
			else
				trace2_endy = y2;
		}
	} else if (y2 >= 0 && y2 < pixelsdeep) {
		t = vertlight1;
		xdiffsign = (int8_t)(-xdiffsign);
		vertlight1 = vertlight2;
		trace2_startx = x2;
		trace2_starty = y2;
		vertlight2 = t;
		if (y1 < 0)
			trace2_endy = 0;
		else if (y1 <= pixelsdeep)
			trace2_endy = y1;
		else
			trace2_endy = pixelsdeep;
	} else {
		int32_t dx;

		/* The light delta wraps in 16 bits like the original imul; the
		 * multiply goes through uint32_t to avoid signed-overflow UB. */
		if (y1 < 0) {
			y1 = -y1;
			vertlight1 += (int16_t)((uint32_t)y1 * (uint32_t)lightincy);
			if (slope > 0xFFFF)
				dx = y1 / slope;
			else
				dx = math2_ABoverC32(y1, 0x10000, (int32_t)(fraction + ((uint32_t)slope << 16)));
			if (xdiffsign < 0)
				x1 -= dx;
			else
				x1 += dx;
			trace2_starty = 0;
		} else {
			y1 -= pixelsdeep;
			vertlight1 -= (int16_t)((uint32_t)y1 * (uint32_t)lightincy);
			if (slope > 0xFFFF)
				dx = y1 / slope;
			else
				dx = math2_ABoverC32(y1, 0x10000, (int32_t)(fraction + ((uint32_t)slope << 16)));
			if (xdiffsign < 0)
				x1 -= dx;
			else
				x1 += dx;
			trace2_starty = pixelsdeep;
		}
		trace2_startx = x1;
		if (y2 < 0)
			trace2_endy = 0;
		else if (y2 > pixelsdeep)
			trace2_endy = pixelsdeep;
		else
			trace2_endy = y2;
	}
}

// FUNCTION: TIE95 0x58D4C
void trace2_xdomclipy(int32_t x1, int32_t y1, int32_t x2, int32_t y2, int32_t slope, uint16_t fraction) {
	if (y1 >= 0 && y1 < pixelsdeep) {
		if (y2 >= 0 && y2 < pixelsdeep) {
			if (y2 < y1) {
				int16_t t;

				trace2_endy = y1;
				trace2_starty = y2;
				trace2_startx = x2;
				t = vertlight1;
				vertlight1 = vertlight2;
				vertlight2 = t;
				xdiffsign = (int8_t)(-xdiffsign);
			} else {
				trace2_starty = y1;
				trace2_startx = x1;
				trace2_endy = y2;
			}
		} else {
			trace2_starty = y1;
			trace2_startx = x1;
			if (y2 < 0)
				trace2_endy = 0;
			else if (y2 > pixelsdeep)
				trace2_endy = pixelsdeep;
			else
				trace2_endy = y2;
		}
	} else if (y2 >= 0 && y2 < pixelsdeep) {
		int16_t t;

		trace2_startx = x2;
		t = vertlight1;
		vertlight1 = vertlight2;
		trace2_starty = y2;
		vertlight2 = t;
		xdiffsign = (int8_t)(-xdiffsign);
		if (y1 < 0)
			trace2_endy = 0;
		else if (y1 > pixelsdeep)
			trace2_endy = pixelsdeep;
		else
			trace2_endy = y1;
	} else {
		int32_t dy;
		int32_t dx;

		/* The slope product wraps like the original imul; it goes
		 * through uint32_t to avoid signed-overflow UB. */
		if (y1 < 0) {
			dy = -y1;
			vertlight1 += dy * lightincy;
			slope = (int32_t)((uint32_t)slope * (uint32_t)dy);
			dx = slope + math2_longfraction(dy, fraction);
			if (xdiffsign < 0)
				x1 -= dx;
			else
				x1 += dx;
			trace2_starty = 0;
		} else {
			dy = y1 - pixelsdeep;
			slope = (int32_t)((uint32_t)slope * (uint32_t)dy);
			vertlight1 -= dy * lightincy;
			dx = slope + math2_longfraction(dy, fraction);
			if (xdiffsign < 0)
				x1 -= dx;
			else
				x1 += dx;
			trace2_starty = pixelsdeep;
		}
		trace2_startx = x1;
		if (y2 < 0)
			trace2_endy = 0;
		else if (y2 > pixelsdeep)
			trace2_endy = pixelsdeep;
		else
			trace2_endy = y2;
	}
}

/* ================================================================ */
/*  drawscreencoords — flat-polygon tracer                           */
/* ================================================================ */

// FUNCTION: TIE95 0x58F4C
void trace2_drawscreencoords(void) {
	int32_t* first;
	int32_t slope;
	int16_t slot;
	uint16_t frac;
	int32_t* last;

	/* Early-exit on full-screen AABB miss. */
	if (maxscreeny[1] < 0)
		return;
	if (maxscreenx[0] < 0)
		return;
	if ((int32_t)pixelsdeep <= minscreeny[1])
		return;
	if ((int32_t)pixelswide <= minscreenx[0])
		return;

	slot = (int16_t)flatobjnum;
	if (slot == 111)
		return;

	flatcolors[slot] = color;
	flatcomponentnum[slot] = (uint8_t)objectnum;
	flatparentobj[slot] = parentobject;
	flatz[slot] = -32767;
	flatobjnum = (uint16_t)(slot + 1);

	slot += 0x80;
	polyidbyte = (uint8_t)slot;
	edgeidbyte = layervalue;
	objectedgeword = (uint16_t)(((uint8_t)slot << 8) + edgeidbyte);

	if (numpoints == samexcnt) {
		/* Vertical-line degenerate: two 1-px vertical edges spanning y-range. */
		int16_t y1;
		int16_t ydelta;

		frac = 0;
		if (minscreeny[1] >= 0)
			frac = (uint16_t)minscreeny[1];
		y1 = (int16_t)pixelsdeep;
		if (maxscreeny[1] >= 0 && (int32_t)pixelsdeep > maxscreeny[1])
			y1 = (int16_t)maxscreeny[1];
		slot = (int16_t)maxscreeny[1];
		ydelta = (int16_t)(y1 - frac);
		trace2_entervertedge(frac, ydelta, (uint8_t)slot, slot);
		slot++;
		trace2_entervertedge(frac, ydelta, (uint8_t)slot, slot);
		return;
	}

	if (numpoints == sameycnt) {
		/* Horizontal-line degenerate. */
		trace2_entervertedge((int16_t)minscreeny[1], 1, (int16_t)minscreenx[0], 0);
		trace2_entervertedge((int16_t)minscreeny[1], 1, (int16_t)maxscreenx[0], 0);
		return;
	}

	/* General: walk the vertex ring, classify each edge, dispatch. */
	last = minscreeny;
	do {
		/* Retail uses wrapping 32-bit sub/neg here. Projected endpoints can
		 * sit near opposite INT32 limits, so express those operations unsigned. */
		uint32_t ydiff;
		uint32_t xdiff;

		first = last;
		last = first + 2;
		if (last == lastscreenxy)
			last = firstscreenxy;

		ydiffsign = 1;
		ydiff = (uint32_t)last[1] - (uint32_t)first[1];
		if ((int32_t)ydiff < 0) {
			ydiffsign = -1;
			ydiff = -ydiff;
			if (first[1] < 0)
				continue;
			if ((int32_t)pixelsdeep <= last[1])
				continue;
		} else {
			if (ydiff == 0)
				continue;
			if (last[1] < 0)
				continue;
			if ((int32_t)pixelsdeep <= first[1])
				continue;
		}

		xdiffsign = 1;
		xdiff = (uint32_t)last[0] - (uint32_t)first[0];
		if ((int32_t)xdiff < 0) {
			xdiffsign = -1;
			xdiff = -xdiff;
		}

		slope = (int32_t)ydiff >> 1;
		if ((int32_t)xdiff < slope) {
			/* y-dominant: slope = ydiff/xdiff */
#ifdef __WATCOMC__
			trace2_slopediv(ydiff, xdiff, &slope, &frac);
#else
			if (xdiff != 0) {
				slope = (int32_t)(ydiff / xdiff);
				frac = (uint16_t)math2_mul_div_u32(ydiff % xdiff, 0x10000u, xdiff);
			} else {
				slope = TRACE2_SLOPE_INF;
				frac = 0;
			}
#endif
			trace2_ydomedge(slope, frac, first, last);
		} else if ((int32_t)xdiff > slope) {
			/* x-dominant: slope = xdiff/ydiff */
#ifdef __WATCOMC__
			trace2_slopediv(xdiff, ydiff, &slope, &frac);
#else
			if (ydiff != 0) {
				slope = (int32_t)(xdiff / ydiff);
				frac = (uint16_t)math2_mul_div_u32(xdiff % ydiff, 0x10000u, ydiff);
			} else {
				slope = TRACE2_SLOPE_INF;
				frac = 0;
			}
#endif
			trace2_xdomedge(slope, frac, first, last);
		} else {
			trace2_ydomedge(TRACE2_SLOPE_EQ, 0, first, last);
		}
	} while (last != minscreeny);
}

/* ================================================================ */
/*  drawface — lit-polygon tracer                                    */
/* ================================================================ */

// FUNCTION: TIE95 0x59278
void trace2_drawface(uint16_t numberOfVertices) {
	int16_t edge;
	uint16_t frac;
	int16_t vertexIndex;
	int32_t xdiff;
	int32_t slope;
	int16_t vi;
	TRANSFM2_ScreenPoint* edgePtc;
	int32_t ydiff;

	trace2_znegflag = 0;

	vertexIndex = 0;
	for (counter = (uint8_t)numberOfVertices; counter > 0; --counter) {
		TRANSFM2_ScreenPoint* edgePt;
		int16_t flags;

		edge = firstvertptr[vertexIndex + 1];
		vertlight2 = (int16_t)vertexlight[firstvertptr[vertexIndex + 2]];
		vertlight1 = (int16_t)vertexlight[firstvertptr[vertexIndex]];

		/* Swap lights if this face walks the edge in the reverse direction. */
		edgePt = calcflag[firstvertptr[vertexIndex]];
		if (edgePt == edgept2[firstvertptr[vertexIndex + 1]]) {
			int16_t t = vertlight1;
			vertlight1 = vertlight2;
			vertlight2 = t;
		}
		if (edgePt == NULL)
			++trace2_znegflag;

		polyidbyte = (uint8_t)objectnum;
		edgeidbyte = (uint8_t)edge;
		objectedgeword = (uint16_t)((polyidbyte << 8) + (uint8_t)edge);

		flags = edgeflags[edge];
		if (flags == XTRANS2_EDGEFLAG_YDOM ||
			flags == (XTRANS2_EDGEFLAG_HAS_HEADER | XTRANS2_EDGEFLAG_YDOM) ||
			(flags & XTRANS2_EDGEFLAG_DEGEN) != 0) {
			/* Off-right sentinel (0x80 alone), already-headered, or degenerate.
			 * Just remember as lastedge candidate for z-clip synthesis. */
			if ((flags & XTRANS2_EDGEFLAG_HAS_HEADER) == 0)
				trace2_lastedge = edge;
		} else if ((flags & XTRANS2_EDGEFLAG_HAS_HEADER) != 0) {
			/* Second face touching this edge — fill face2 on cached header. */
			trace2_EdgeHeader* h = (trace2_EdgeHeader*)edgeflagptr[edge];
			if ((uint8_t)edge == h->edgeid)
				h->face2 = facenumber;
		} else {
			/* New edge: install header + cache slope, then dispatch. */
			edgeflags[edge] = (uint8_t)(flags | XTRANS2_EDGEFLAG_HAS_HEADER);
			edgeflagptr[edge] = trace2_newedgeheader;
			++edgeindex;

			if ((flags & XTRANS2_EDGEFLAG_UNUSED_10) != 0) {
				/* DEAD CODE: bit 0x10 is never set by TRANSFM2_classifyedges
				 * or anywhere else in demo or retail. The adjacent-face
				 * orientation flip that this branch was presumably intended
				 * for is actually performed inside classifyedges itself
				 * (flips signs + swaps edgept1/edgept2 in place when a
				 * cached flag is seen). Retail complements the low word of
				 * each stored pointer in place. */
				uint16_t* lo1 = (uint16_t*)&edgept1[edge];
				uint16_t* lo2 = (uint16_t*)&edgept2[edge];

				*lo1 = (uint16_t)~*lo1;
				*lo2 = (uint16_t)~*lo2;
			}

			if (gauraudflag) {
				/* Gouraud gradient: lightincy = 2 * |dlight| / ydiff, signed
				 * by (edgeysign XOR -sign_flag). */
				int16_t dlt = vertlight2 - vertlight1;
				int sign_flag;
				int32_t div = 0;

				if (dlt < 0) {
					sign_flag = 1;
					dlt = -dlt;
				} else {
					sign_flag = 0;
				}
				dlt >>= 1;
				if (dlt > edgeydiff[edge]) {
					if (edgeydiff[edge] != 0)
						div = dlt / edgeydiff[edge];
					if (div > 255)
						div &= 0xFFFE;
					if ((int16_t)(-sign_flag ^ edgeysign[edge]) < 0)
						div = -div;
				}
				lightincy = (int16_t)(div + div);
			}
			xdiffsign = edgexsign[edge];
			ydiffsign = edgeysign[edge];

			if ((edgeflags[edge] & XTRANS2_EDGEFLAG_SLOPE_CACHED) != 0) {
				frac = edgeslopefrac[edge];
				slope = (uint32_t)(uint16_t)edgeslopehi[edge] << 16;
				slope |= (uint16_t)edgeslopelo[edge];
			} else {
				uint32_t dy;
				uint32_t dx;

				edgeflags[edge] |= XTRANS2_EDGEFLAG_SLOPE_CACHED;
				dy = (uint32_t)edgeydiff[edge];
				dx = (uint32_t)edgexdiff[edge];
				if ((dy >> 1) > dx) {
#ifdef __WATCOMC__
					trace2_slopediv(dy, dx, &slope, &frac);
#else
					if (dx != 0) {
						slope = (int32_t)((uint32_t)dy / (uint32_t)dx);
						frac =
							(uint16_t)math2_mul_div_u32((uint32_t)dy % (uint32_t)dx, 0x10000u, (uint32_t)dx);
					} else {
						slope = TRACE2_SLOPE_INF;
						frac = 0;
					}
#endif
					edgeslopefrac[edge] = frac;
					edgeslopelo[edge] = (int16_t)slope;
					edgeflags[edge] |= XTRANS2_EDGEFLAG_YDOM;
					edgeslopehi[edge] = (int16_t)((uint32_t)slope >> 16);
				} else if ((dy >> 1) < dx) {
#ifdef __WATCOMC__
					trace2_slopediv(dx, dy, &slope, &frac);
#else
					if (dy != 0) {
						slope = (int32_t)((uint32_t)dx / (uint32_t)dy);
						frac =
							(uint16_t)math2_mul_div_u32((uint32_t)dx % (uint32_t)dy, 0x10000u, (uint32_t)dy);
					} else {
						slope = TRACE2_SLOPE_INF;
						frac = 0;
					}
#endif
					edgeslopefrac[edge] = frac;
					edgeslopelo[edge] = (int16_t)slope;
					edgeslopehi[edge] = (int16_t)((uint32_t)slope >> 16);
				} else {
					slope = TRACE2_SLOPE_EQ;
					frac = 0;
					edgeslopefrac[edge] = 0;
					edgeslopelo[edge] = TRACE2_SLOPE_EQ;
					edgeslopehi[edge] = 0;
					edgeflags[edge] |= XTRANS2_EDGEFLAG_YDOM;
				}

				if ((edgeflags[edge] & XTRANS2_EDGEFLAG_YDOM) != 0)
					trace2_ydomedge(slope, frac, edgept1[edge]->xy, edgept2[edge]->xy);
				else
					trace2_xdomedge(slope, frac, edgept1[edge]->xy, edgept2[edge]->xy);
			}
		}

		vertexIndex += 2;
	}

	/* --- Z-clip repair. When 0 < znegflag < numverts, synthesise a
	 * boundary edge between the first/last visible vertices. --- */
	if (trace2_znegflag == 0 || trace2_znegflag == numberOfVertices)
		return;

	vi = 0;
	if (calcflag[firstvertptr[0]]) {
		/* First vertex visible. Walk forward until we lose visibility. */
		TRANSFM2_ScreenPoint* cf;
		int16_t e;

		do {
			vi += 2;
		} while (calcflag[firstvertptr[vi]]);
		cf = calcflag[firstvertptr[vi - 2]];
		e = firstvertptr[vi - 1];
		if (cf == edgept1[e])
			trace2_lastpointPtr = edgept2[e];
		else
			trace2_lastpointPtr = edgept1[e];

		do {
			vi += 2;
			cf = calcflag[firstvertptr[vi]];
		} while (!cf);
		e = firstvertptr[vi - 1];
		if (cf == edgept1[e])
			edgePtc = edgept2[e];
		else
			edgePtc = edgept1[e];
	} else {
		/* First vertex invisible. Skip to first visible, then lose-and-regain. */
		TRANSFM2_ScreenPoint* cf;
		int16_t e;

		do {
			vi += 2;
			cf = calcflag[firstvertptr[vi]];
		} while (!cf);
		e = firstvertptr[vi - 1];
		if (cf == edgept1[e])
			trace2_lastpointPtr = edgept2[e];
		else
			trace2_lastpointPtr = edgept1[e];

		do {
			vi += 2;
		} while (calcflag[firstvertptr[vi]]);
		cf = calcflag[firstvertptr[vi - 2]];
		e = firstvertptr[vi - 1];
		if (cf == edgept1[e])
			edgePtc = edgept2[e];
		else
			edgePtc = edgept1[e];
	}

	/* Both repair endpoints are near-plane clip points: each is the endpoint
	 * of a straddling edge that is not the visible vertex's calcflag[] record,
	 * so TRANSFM2_facezintersect produced it and filled its light slot. */
	vertlight1 = trace2_lastpointPtr->light;
	vertlight2 = edgePtc->light;

	ydiffsign = 1;
	ydiff = edgePtc->xy[1] - trace2_lastpointPtr->xy[1];
	if (ydiff < 0) {
		ydiffsign = -1;
		ydiff = -ydiff;
		if (trace2_lastpointPtr->xy[1] < 0 || (int32_t)pixelsdeep <= edgePtc->xy[1])
			return;
	} else {
		if (ydiff == 0 || edgePtc->xy[1] < 0 || (int32_t)pixelsdeep <= trace2_lastpointPtr->xy[1])
			return;
	}

	xdiffsign = 1;
	xdiff = edgePtc->xy[0] - trace2_lastpointPtr->xy[0];
	if (xdiff < 0) {
		xdiffsign = -1;
		xdiff = -xdiff;
	}

	if (trace2_lastedge == 0)
		trace2_findlastedge();

	edgeidbyte = (uint8_t)trace2_lastedge;
	objectedgeword = (uint16_t)((polyidbyte << 8) + (uint8_t)trace2_lastedge);

	if (gauraudflag) {
		/* Z-clip Gouraud: same formula as main loop but uses the locally
		 * computed ydiff/ydiffsign instead of edgeydiff[]/edgeysign[]. */
		int16_t dlt = vertlight2 - vertlight1;
		int sign_flag;
		int32_t div = 0;

		if (dlt < 0) {
			sign_flag = 1;
			dlt = -dlt;
		} else {
			sign_flag = 0;
		}
		dlt >>= 1;
		if (dlt > ydiff) {
			if (ydiff != 0)
				div = dlt / ydiff;
			if ((uint8_t)(div >> 8) != 0)
				div &= 0xFFFE;
			if ((int16_t)(-sign_flag ^ ydiffsign) < 0)
				div = -div;
		}
		lightincy = (int16_t)(div + div);
	}

	if (((uint32_t)ydiff >> 1) > (uint32_t)xdiff) {
#ifdef __WATCOMC__
		trace2_slopediv(ydiff, xdiff, &slope, &frac);
#else
		if (xdiff != 0) {
			slope = (int32_t)((uint32_t)ydiff / (uint32_t)xdiff);
			frac = (uint16_t)math2_mul_div_u32((uint32_t)ydiff % (uint32_t)xdiff, 0x10000u, (uint32_t)xdiff);
		} else {
			slope = TRACE2_SLOPE_INF;
			frac = 0;
		}
#endif
		edgeflags[trace2_lastedge] |= XTRANS2_EDGEFLAG_HAS_HEADER;
		edgeflagptr[trace2_lastedge] = trace2_newedgeheader;
		trace2_ydomedge(slope, frac, trace2_lastpointPtr->xy, edgePtc->xy);
	} else if (((uint32_t)ydiff >> 1) < (uint32_t)xdiff) {
#ifdef __WATCOMC__
		trace2_slopediv(xdiff, ydiff, &slope, &frac);
#else
		if (ydiff != 0) {
			slope = (int32_t)((uint32_t)xdiff / (uint32_t)ydiff);
			frac = (uint16_t)math2_mul_div_u32((uint32_t)xdiff % (uint32_t)ydiff, 0x10000u, (uint32_t)ydiff);
		} else {
			slope = TRACE2_SLOPE_INF;
			frac = 0;
		}
#endif
		edgeflags[trace2_lastedge] |= XTRANS2_EDGEFLAG_HAS_HEADER;
		edgeflagptr[trace2_lastedge] = trace2_newedgeheader;
		trace2_xdomedge(slope, frac, trace2_lastpointPtr->xy, edgePtc->xy);
	} else {
		slope = TRACE2_SLOPE_EQ;
		frac = 0;
		edgeflags[trace2_lastedge] |= XTRANS2_EDGEFLAG_HAS_HEADER;
		edgeflagptr[trace2_lastedge] = trace2_newedgeheader;
		trace2_ydomedge(slope, frac, trace2_lastpointPtr->xy, edgePtc->xy);
	}
	trace2_lastedge = 0;
}

/* ================================================================ */
/*  findlastedge                                                     */
/* ================================================================ */

// FUNCTION: TIE95 0x59A78
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
/*  Domain-specific dispatchers (retail variant with clamp)          */
/* ================================================================ */

// FUNCTION: TIE95 0x59AC0
void trace2_ydomedge(int32_t slope, uint16_t fraction, int32_t* pt1, int32_t* pt2) {
	int32_t slopea = slope;
	int32_t slopeb;

	trace2_ydomclipy(pt1[0], pt1[1], pt2[0], pt2[1], slope, fraction);
	if (slopea > TRACE2_SLOPE_MAX)
		slopea = TRACE2_SLOPE_MAX;
	/* slopea << 8 is done through u32 to avoid signed-shift UB; slopea is
	 * clamped positive above but a negative `slope` arg would still hit UB
	 * under signed semantics. */
	slopeb = ((int32_t)fraction >> 8) + (int32_t)((uint32_t)slopea << 8);

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

// FUNCTION: TIE95 0x59BB0
void trace2_xdomedge(int32_t slope, uint16_t fraction, int32_t* pt1, int32_t* pt2) {
	int32_t slopea = slope;
	int32_t slopeb;

	trace2_xdomclipy(pt1[0], pt1[1], pt2[0], pt2[1], slope, fraction);
	if (slopea > TRACE2_SLOPE_MAX)
		slopea = TRACE2_SLOPE_MAX;
	/* slopea << 8 is done through u32 to avoid signed-shift UB; slopea is
	 * clamped positive above but a negative `slope` arg would still hit UB
	 * under signed semantics. */
	slopeb = ((int32_t)fraction >> 8) + (int32_t)((uint32_t)slopea << 8);

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
