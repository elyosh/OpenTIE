#include <stdint.h>

#include "tie/drawln2.h"
#include "tie/logbuf2.h"
#include "tie/trace2.h" /* vertlight1/2, lightincy/x, xdiffsign, ydiffsign,
                        * TRACE2_{x,y}domedge, TRACE2_entervertedge */
#include "tie/drawpol.h"
#include "tie/math2.h"   /* math2_ABoverC32 */
#include "tie/xtrans2.h" /* flatobjnum */

/* ======================================================================
 * Globals owned by DRAWLN2 (per watdbg)
 * ==================================================================== */

/* Extern (shared with drawpol callers). */
// GLOBAL: TIE 0xD35EC
int32_t* point1ptr;
int16_t linelightincy;
int16_t linelightincx;
// GLOBAL: TIE 0xD35FC
uint16_t thickness;
// GLOBAL: TIE 0xD35F4
int16_t linelight1;
// GLOBAL: TIE 0xD35F6
int16_t linelight2;

/* Static (file-private per watdbg 'static OPAQUE'). */
static int16_t templight1;
static int16_t templight2;
static uint8_t ydomflag;
static int8_t linexsign;
static int8_t lineysign;

/* ======================================================================
 * Cross-module externs (not re-declared in any included header but used here)
 * ==================================================================== */

/* ======================================================================
 * Helpers
 * ==================================================================== */

/* Q16 fractional remainder from a Bresenham-style slope division:
 * returns high 16 bits of ((num << 32) / den). Mirrors Watcom's
 * 64-bit-shift-and-divide idiom used throughout this function. */
static inline uint16_t q16_frac(uint32_t num, uint32_t den) {
	return (uint16_t)(((uint64_t)num << 32) / den >> 16);
}

/* Binary idiom: `if (BYTE1(x)) LOBYTE(x) = x & 0xFE` (asm: test ah,FFh
 * / and bl,FEh). Clears bit 0 of byte 0 when any of bits 8..15 is set —
 * i.e. round the divide quotient down to even when its magnitude
 * exceeds 8 bits OR it is negative (sign-extension makes byte 1 = 0xFF).
 * This ensures the subsequent `2 * x` doubling preserves the rounded
 * value's parity. */
static inline int clear_b1_lsb(int x) {
	if ((x >> 8) & 0xFF)
		x &= ~1;
	return x;
}

/* ======================================================================
 * drawln2_tracelineedges
 * ==================================================================== */
// FUNCTION: TIE 0x1C6D0
void drawln2_tracelineedges(int32_t* pt2) {
	int32_t* pt1ptr = point1ptr;

	/* --- Compute signed dx/dy and the sign-flag globals.
	 * Subtraction is done in uint32_t to match the binary's
	 * 32-bit two's-complement wraparound (asm: sub/neg). Upstream
	 * clip/transform paths can emit near-INT32_MAX coordinates,
	 * which would overflow a signed `pt2 - pt1`; signed overflow
	 * is UB in C, but the asm tolerates it via wraparound and
	 * downstream consumers handle the bogus delta. */
	lineysign = 1;
	ydiffsign = 1;
	int32_t pt2_y = pt2[1];
	int32_t pt1_y = pt1ptr[1];
	uint32_t dy_u = (uint32_t)pt2_y - (uint32_t)pt1_y;
	if ((int32_t)dy_u < 0) {
		lineysign = -1;
		ydiffsign = -1;
		dy_u = -dy_u;
	}
	int32_t dy_abs = (int32_t)dy_u;

	int32_t pt2_x = pt2[0];
	int32_t pt1_x = pt1ptr[0];
	linexsign = 1;
	xdiffsign = 1;
	uint32_t dx_u = (uint32_t)pt2_x - (uint32_t)pt1_x;
	if ((int32_t)dx_u < 0) {
		linexsign = -1;
		xdiffsign = -1;
		dx_u = -dx_u;
	}
	int32_t dx_abs = (int32_t)dx_u;

	/* --- Early-exit: off-screen in Y even with thickness padding. --- */
	int y_offscreen;
	if (ydiffsign >= 0) {
		if (-(int)thickness > pt2_y)
			return;
		y_offscreen = pixelsdeep + thickness <= pt1_y;
	} else {
		if (-(int)thickness > pt1_y)
			return;
		y_offscreen = pixelsdeep + thickness <= pt2_y;
	}
	if (y_offscreen)
		return;

	/* --- Early-exit: off-screen in X. --- */
	int x_offscreen;
	if (xdiffsign >= 0) {
		if (-(int)thickness >= pt2_x)
			return;
		x_offscreen = pixelswide + thickness <= pt1_x;
	} else {
		if (-(int)thickness >= pt1_x)
			return;
		x_offscreen = pixelswide + thickness <= pt2_x;
	}
	if (x_offscreen)
		return;

	/* --- Cap the flat-line queue at 111 entries. --- */
	if (flatobjnum == 111)
		return;
	++flatobjnum;

	/* --- Saturate per-endpoint light (negative -> 0). --- */
	int16_t init_light1 = (linelight1 < 0) ? 0 : linelight1;
	vertlight1 = init_light1;
	templight1 = init_light1;

	int16_t init_light2 = (linelight2 < 0) ? 0 : linelight2;
	vertlight2 = init_light2;
	templight2 = init_light2;

	/* When both endpoints have the same signed light value, normalize both
	 * to 0 (if originally >= 0) or to -vertlight1 (if originally < 0). */
	if (linelight1 == linelight2) {
		int16_t zero_or_neg = (linelight1 >= 0) ? 0 : (int16_t)-vertlight1;
		linelight1 = zero_or_neg;
		linelight2 = zero_or_neg;
	}

	ydomflag = 0;

	/* =================================================================
	 * Dispatch: Y-dominant vs X-dominant
	 * ================================================================= */

	/* Y-major / X-major dispatch is a signed compare in the binary
	 * (asm 0x1c8c6: `jl`). With wrapped-negative dx_abs from
	 * near-clip saturation, signed vs unsigned flips the branch. */
	if (dx_abs < dy_abs) {
		/* ------------------------------------------------------------- *
		 * Y-DOMINANT PATH (|dy| > |dx|)                                 *
		 * ------------------------------------------------------------- */
		linelightincy = 0;
		lightincy = 0;
		lightincx = 0;

		if (linelight1 != linelight2) {
			int16_t light_delta = vertlight2 - vertlight1;
			int light_neg_flag = 0;
			if (light_delta < 0) {
				light_neg_flag = 1;
				light_delta = -light_delta;
			}
			int light_step = (uint16_t)light_delta >> 1;
			if ((int16_t)light_step > (int)dy_abs) {
				if (dy_abs)
					light_step = (int16_t)light_step / (int16_t)dy_abs;
				light_step = clear_b1_lsb(light_step);
				/* Sign-reconcile: ydiffsign and light_neg_flag together
				 * pick the sign of the gradient. */
				if (((ydiffsign ^ (uint16_t)-light_neg_flag) & 0x8000u) != 0)
					light_step = -light_step;
			}
			lightincy = (int16_t)(2 * light_step);
			linelightincy = (int16_t)(2 * light_step);
		}

		/* Vertical-bounds check: at least one endpoint inside vertical range. */
		if ((pt2_y >= 0 || pt1_y >= 0) && (pt1_y < pixelsdeep || pt2_y < pixelsdeep)) {
			uint16_t half_thickness = (thickness + 1) >> 1;
			int32_t x1_shifted = pt1_x - half_thickness;
			int32_t x2_shifted = pt2_x - half_thickness;
			pt1ptr[0] = x1_shifted;

			pt2[0] = x2_shifted;
			if (x1_shifted >= 0) {
				if (x1_shifted >= pixelswide && x2_shifted >= pixelswide)
					return;
			} else if (x2_shifted < 0) {
				int32_t neg_thickness = -(int32_t)thickness;
				if (x2_shifted <= neg_thickness && neg_thickness >= x1_shifted)
					return;
			}

			/* Emit first edge (left side of the thick line). */
			int edge_slope;
			uint16_t edge_fraction;
			if ((uint32_t)dy_abs / 2 >= (uint32_t)dx_abs) {
				if ((uint32_t)dy_abs / 2 <= (uint32_t)dx_abs) {
					/* Close to 45° */
					edge_slope = 2;
					edge_fraction = 0;
				} else if (dx_abs) {
					/* Asm: div ebx — unsigned 32-bit divide.
					 * Match the bit pattern even when an input
					 * is wrapped-negative from saturation. */
					edge_slope = (int)((uint32_t)dy_abs / (uint32_t)dx_abs);
					edge_fraction = q16_frac((uint32_t)dy_abs % (uint32_t)dx_abs, (uint32_t)dx_abs);
				} else {
					edge_slope = 0x7FFFFFFF;
					edge_fraction = 0;
				}
				++ydomflag;
				trace2_ydomedge(edge_slope, edge_fraction, pt1ptr, pt2);
			} else if (dy_abs) {
				edge_slope = (int)((uint32_t)dx_abs / (uint32_t)dy_abs);
				edge_fraction = q16_frac((uint32_t)dx_abs % (uint32_t)dy_abs, (uint32_t)dy_abs);
				trace2_xdomedge(edge_slope, edge_fraction, pt1ptr, pt2);
			} else {
				edge_slope = 0x7FFFFFFF;
				edge_fraction = 0;
				trace2_xdomedge(0x7FFFFFFF, 0, pt1ptr, pt2);
			}

			/* --- Prepare for the right-side edge. --- */
			xdiffsign = linexsign;
			ydiffsign = lineysign;
			vertlight1 = half_thickness; /* reuse as edge offset */
			vertlight2 = (linelight2 < 0) ? 0 : linelight2;

			pt1ptr[0] = thickness + x1_shifted;
			pt2[0] = thickness + x2_shifted;

			/* Emit second edge (right side). ydomflag tracks which kind. */
			uint8_t ydomflag_saved = ydomflag;
			int second_is_xdom = 0;
			if (ydomflag) {
				--ydomflag;
				if (ydomflag_saved != 1)
					second_is_xdom = 1; /* fall to the xdomedge tail */
			} else {
				/* Recompute slope from the shifted/clipped coords.
				 * Asm: div ebx — unsigned 32-bit divide. */
				if ((uint32_t)dy_abs / 2 < (uint32_t)dx_abs) {
					if (dy_abs)
						trace2_xdomedge((int)((uint32_t)dx_abs / (uint32_t)dy_abs),
										q16_frac((uint32_t)dx_abs % (uint32_t)dy_abs, (uint32_t)dy_abs),
										pt1ptr, pt2);
					else
						trace2_xdomedge(0x7FFFFFFF, 0, pt1ptr, pt2);
					return;
				}
				if ((uint32_t)dy_abs / 2 > (uint32_t)dx_abs) {
					if (dx_abs)
						trace2_ydomedge((int)((uint32_t)dy_abs / (uint32_t)dx_abs),
										q16_frac((uint32_t)dy_abs % (uint32_t)dx_abs, (uint32_t)dx_abs),
										pt1ptr, pt2);
					else
						trace2_ydomedge(0x7FFFFFFF, 0, pt1ptr, pt2);
					return;
				}
				edge_slope = 2;
				edge_fraction = 0;
			}
			if (second_is_xdom)
				trace2_xdomedge(edge_slope, edge_fraction, pt1ptr, pt2);
			else
				trace2_ydomedge(edge_slope, edge_fraction, pt1ptr, pt2);
		}
		return;
	}

	/* ------------------------------------------------------------- *
	 * X-DOMINANT PATH (|dx| >= |dy|)                                 *
	 * ------------------------------------------------------------- */

	/* Additional horizontal-bounds gate. */
	if (pt1_x < 0 && pt2_x < 0)
		return;
	if (pt1_x >= pixelswide && pt2_x >= pixelswide)
		return;

	/* --- Compute per-column light gradient. --- */
	if (linelight1 == linelight2) {
		/* Same-light endpoints: divide by thickness for the thick-line
		 * cross-gradient (linelightincy perpendicular to the line). */
		int16_t delta = linelight1 - vertlight1;
		int neg = 0;
		if (delta < 0) {
			neg = 1;
			delta = -delta;
		}
		int step = (uint16_t)delta >> 1;
		if ((int16_t)step > (int)thickness) {
			if (thickness)
				step = (uint16_t)step / thickness;
			step = clear_b1_lsb(step);
			if (neg)
				step = -step;
		}
		linelightincy = (int16_t)(2 * step);
		lightincy = 0;
	} else {
		/* Different-light endpoints: divide by dy_abs. */
		int16_t delta = vertlight2 - vertlight1;
		int neg = 0;
		if (delta < 0) {
			neg = 1;
			delta = -delta;
		}
		int step = (uint16_t)delta >> 1;
		if ((int16_t)step > (int)dy_abs) {
			if (dy_abs)
				step = (int16_t)step / (int16_t)dy_abs;
			step = clear_b1_lsb(step);
			if (((ydiffsign ^ (uint16_t)-neg) & 0x8000u) != 0)
				step = -step;
		}
		lightincy = (int16_t)(2 * step);
		linelightincy = 0;
	}

	/* --- Left-clip (pt1). --- */
	if (pt1_x >= 0) {
		if (pt1_x > pixelswide) {
			int clip = math2_ABoverC32(pt1_x - pixelswide, dy_abs, dx_abs);
			if (lineysign < 0)
				clip = -clip;
			pt1_y += clip;
			pt1ptr[1] = pt1_y;
			int16_t light_adj = (int16_t)(lightincy * clip);
			vertlight1 += light_adj;
			linelight1 += light_adj;
			templight1 += light_adj;
			pt1_x = pixelswide;
			pt1ptr[0] = pixelswide;
		}
	} else {
		int clip = math2_ABoverC32(-pt1_x, dy_abs, dx_abs);
		if (lineysign < 0)
			clip = -clip;
		pt1_y += clip;
		pt1ptr[1] = pt1_y;
		int16_t light_adj = (int16_t)(lightincy * clip);
		vertlight1 += light_adj;
		linelight1 += light_adj;
		templight1 += light_adj;
		pt1_x = 0;
		pt1ptr[0] = 0;
	}

	/* --- Right-clip (pt2). --- */
	if (pt2_x >= 0) {
		if (pt2_x > pixelswide) {
			int clip = math2_ABoverC32(pt2_x - pixelswide, dy_abs, dx_abs);
			if (lineysign >= 0)
				clip = -clip;
			pt2_y += clip;
			pt2[1] = pt2_y;
			int16_t light_adj = (int16_t)(lightincy * clip);
			vertlight1 += light_adj;
			linelight1 += light_adj;
			templight1 += light_adj;
			pt2_x = pixelswide;
			pt2[0] = pixelswide;
		}
	} else {
		int clip = math2_ABoverC32(-pt2_x, dy_abs, dx_abs);
		if (lineysign >= 0)
			clip = -clip;
		int16_t light_adj = (int16_t)(lightincy * clip);
		int16_t old_vertlight1 = vertlight1;
		int16_t old_templight1 = templight1;
		pt2[0] = 0;
		pt2_y += clip;
		pt2[1] = pt2_y;
		pt2_x = 0;
		vertlight1 = light_adj + old_vertlight1;
		linelight1 += light_adj;
		templight1 = light_adj + old_templight1;
	}

	/* --- Vertical caps via TRACE2_entervertedge and final emit. --- */
	uint16_t half_thick_plus1 = (thickness + 1) >> 1;
	int32_t y1_shifted = pt1_y - half_thick_plus1;
	int32_t y2_shifted = pt2_y - half_thick_plus1;
	int32_t y1_clipped = y1_shifted;
	int32_t pt2_y_clip = y2_shifted;
	pt1ptr[1] = y1_shifted;
	pt2[1] = y2_shifted;

	int edge_slope = 0;
	uint16_t edge_fraction = 0;

	/* Top-edge classification: defer the xdomedge emit (leaving ydomflag=0)
	 * only when BOTH endpoints of the shifted line land on the same side
	 * of the screen vertically. Otherwise set ydomflag=2 and, if dy_abs,
	 * emit an xdomedge with pre-computed slope/fraction. Factored from the
	 * binary's three-way nested if/else since all emit branches were
	 * identical once pt2[1] = y2_shifted is hoisted. */
	int both_above_top = (y1_shifted < 0) && (y2_shifted < 0);
	int both_past_bottom = (y1_shifted >= pixelsdeep) && (y2_shifted >= pixelsdeep);
	if (!both_above_top && !both_past_bottom) {
		ydomflag = 2;
		if (dy_abs) {
			/* Asm: div ebx — unsigned 32-bit divide. */
			edge_slope = (int)((uint32_t)dx_abs / (uint32_t)dy_abs);
			edge_fraction = q16_frac((uint32_t)dx_abs % (uint32_t)dy_abs, (uint32_t)dy_abs);
			ydomflag = (uint8_t)-1;
			trace2_xdomedge(edge_slope, edge_fraction, pt1ptr, pt2);
		}
	}

	ydiffsign = lineysign;
	xdiffsign = linexsign;

	/* --- Top cap via TRACE2_entervertedge. --- */
	if (y1_shifted >= 0) {
		if (y1_shifted < pixelsdeep) {
			int16_t len = pixelsdeep - y1_shifted;
			if (len > (int)thickness)
				len = thickness;
			trace2_entervertedge(y1_shifted, len, (int16_t)pt1_x, templight1);
		}
	} else {
		int32_t y1_plus_thick = y1_shifted + thickness;
		if (y1_plus_thick >= 0) {
			templight1 += (int16_t)(linelightincy * -(int16_t)y1_shifted);
			int16_t cap_len = (y1_plus_thick <= pixelsdeep) ? (int16_t)y1_plus_thick : (int16_t)pixelsdeep;
			trace2_entervertedge(0, cap_len, (int16_t)pt1_x, templight1);
		}
		y1_clipped = y1_plus_thick - thickness; /* == y1_shifted; preserves binary semantics */
	}

	/* --- Bottom cap. --- */
	if (pt2_y_clip >= 0) {
		if (pt2_y_clip < pixelsdeep) {
			int16_t len = pixelsdeep - pt2_y_clip;
			if (len > (int)thickness)
				len = thickness;
			trace2_entervertedge(pt2_y_clip, len, (int16_t)pt2_x, templight1);
		}
	} else {
		int32_t y2_plus_thick = pt2_y_clip + thickness;
		if (y2_plus_thick >= 0) {
			templight1 += (int16_t)(linelightincy * (thickness - y2_plus_thick));
			int16_t cap_len = (y2_plus_thick <= pixelsdeep) ? (int16_t)y2_plus_thick : (int16_t)pixelsdeep;
			trace2_entervertedge(0, cap_len, (int16_t)pt2_x, templight1);
		}
		pt2_y_clip = y2_plus_thick - thickness;
	}

	/* --- Final sweep emit. --- */
	int32_t final_y1 = thickness + y1_clipped;
	int32_t final_y2 = thickness + pt2_y_clip;
	pt1ptr[1] = final_y1;
	pt2[1] = final_y2;
	if (final_y1 >= 0) {
		if (final_y1 >= pixelsdeep && final_y2 >= pixelsdeep)
			return;
	} else if (final_y2 < 0) {
		return;
	}

	/* Reset vertlight1/2 from linelight1/2 (saturated). */
	vertlight1 = (linelight1 < 0) ? 0 : linelight1;
	vertlight2 = (linelight2 < 0) ? 0 : linelight2;

	if (!ydomflag) {
		if (dy_abs)
			trace2_xdomedge((int)((uint32_t)dx_abs / (uint32_t)dy_abs),
							q16_frac((uint32_t)dx_abs % (uint32_t)dy_abs, (uint32_t)dy_abs), pt1ptr, pt2);
		return;
	}
	/* ydomflag == 2: caller did a pre-emit; skip the final xdomedge. */
	if (ydomflag == 2)
		return;

	/* ydomflag == 1 or -1 (0xFF): emit with the saved slope/fraction. */
	trace2_xdomedge(edge_slope, edge_fraction, pt1ptr, pt2);
}
