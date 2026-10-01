#include "tie/logbuf2.h"
#include "landru/vesa.h"
#include "tie/math2.h"
#include "tie/render_texture_tie98.h"
#include "tie/transfm2.h"
#include "tie/xtrans2.h"

#include <stdint.h>
#include <string.h>

/* --- Module-owned globals ----------------------------------------- */

// GLOBAL: TIE95 0xD4C1E
// GLOBAL: TIE98 0x5FD290
uint16_t pixelswide;
// GLOBAL: TIE95 0xD4C1C
uint16_t pixelswidemin1;
// GLOBAL: TIE95 0xD4C18
uint16_t halfpixelswide;
// GLOBAL: TIE95 0xD4C20
// GLOBAL: TIE98 0x5FD27E
uint16_t pixelsdeep;
// GLOBAL: TIE95 0xD4C1A
uint16_t pixelsdeepmin1;
// GLOBAL: TIE95 0xD4C16
uint16_t halfpixelsdeep;
// GLOBAL: TIE95 0xD4C08
uint32_t displaycorner;
// GLOBAL: TIE95 0xD4C00
uint32_t displaycorner_lines;
// GLOBAL: TIE95 0xD4C04
uint32_t displaycorner_columns;
// GLOBAL: TIE95 0xD4C0C
void* buffer_ptr;
/* Retail Z_TIE__.EXE ships this initialized to 0xFB (== (uint8_t)-5) at
 * 0xC5504. tie_updatescreen only assigns -5 AFTER the world render, so
 * the very first flight frame's logbuf2_clearbuffer() uses whatever value
 * is here at startup. Without the retail's initializer, buffer_ptr gets
 * memset to 0 on the first fullupdateflag pass, leaving xtrans2's
 * "color==0 => copy from logbuf" branch emitting zeros and stars'
 * `*dst < deepspacecolor` guard skipping every draw -- flight viewport
 * stays black until some non-render path happens to set deepspacecolor. */
// GLOBAL: TIE95 0xC5504
uint8_t deepspacecolor = 0xFB;

// GLOBAL: TIE98 0x5926D8
uint32_t g_surfacePitch;
// GLOBAL: TIE98 0x4F2ACC
uint32_t g_flight16bppBytesPerPixel;

/* PIP state (saved by startPIP, restored by finishPIP). */
// GLOBAL: TIE95 0xD4C12
// GLOBAL: TIE98 0x584C0C
static uint16_t temppw;
// GLOBAL: TIE95 0xD4C10
// GLOBAL: TIE98 0x584C08
static uint16_t temppd;
// GLOBAL: TIE95 0xD4C14
// GLOBAL: TIE98 0x584C04
static uint16_t tempdc;
// GLOBAL: TIE95 0xD4BF4
// GLOBAL: TIE98 0x584BE0
static int32_t tempA1;
// GLOBAL: TIE95 0xD4BFC
// GLOBAL: TIE98 0x584BE4
static int32_t tempA2;
// GLOBAL: TIE95 0xD4BF8
// GLOBAL: TIE98 0x584BF0
static int32_t tempA3;
// GLOBAL: TIE95 0xD4BDC
// GLOBAL: TIE98 0x584BE8
static int32_t tempB1;
// GLOBAL: TIE95 0xD4BE4
// GLOBAL: TIE98 0x584BEC
static int32_t tempB2;
// GLOBAL: TIE95 0xD4BE0
// GLOBAL: TIE98 0x584BFC
static int32_t tempB3;
// GLOBAL: TIE95 0xD4BE8
// GLOBAL: TIE98 0x584BF4
static int32_t tempC1;
// GLOBAL: TIE95 0xD4BF0
// GLOBAL: TIE98 0x584BF8
static int32_t tempC2;
// GLOBAL: TIE95 0xD4BEC
// GLOBAL: TIE98 0x584C00
static int32_t tempC3;

/* --- API ---------------------------------------------------------- */

// FUNCTION: TIE95 0x2E7F0
void logbuf2_graphsetup(void) { /* Empty stub in the shipped binary. */ }

// FUNCTION: TIE95 0x2E7F4
void logbuf2_selectbuffer(void* buffer) { buffer_ptr = buffer; }

// FUNCTION: TIE95 0x2E7FC
void logbuf2_setbufferdimensions(uint16_t width, uint16_t depth, uint32_t dc) {
	const uint32_t smw = (uint32_t)vesa_bpsl_gbl;

	pixelswide = width;
	pixelswidemin1 = (uint16_t)(width - 1);
	halfpixelswide = (uint16_t)(width / 2);
	pixelsdeep = depth;
	pixelsdeepmin1 = (uint16_t)(depth - 1);
	halfpixelsdeep = (uint16_t)(depth / 2);
	displaycorner = dc;
	displaycorner_lines = smw ? (dc / smw) : 0;
	displaycorner_columns = smw ? (dc % smw) : dc;
}

// FUNCTION: TIE98 0x44C2C0
// LOGBUF2_setbufferdimensions
void logbuf2_setbufferdimensions_tie98(uint16_t width, uint16_t depth, int unused, uint32_t dc) {
	(void)unused;
	pixelswide = width;
	halfpixelswide = width >> 1;
	pixelsdeep = depth;
	pixelswidemin1 = width - 1;
	pixelsdeepmin1 = depth - 1;
	halfpixelsdeep = depth >> 1;
	displaycorner = dc;
	displaycorner_lines = dc / g_surfacePitch;
	displaycorner_columns = dc % g_surfacePitch / g_flight16bppBytesPerPixel;
}

// FUNCTION: TIE95 0x2E870
void logbuf2_clearbuffer(void) {
	if (!buffer_ptr)
		return;
	memset(buffer_ptr, deepspacecolor, (size_t)pixelswide * pixelsdeep);
}

// FUNCTION: TIE98 0x44C330
void logbuf2_clearbuffer_tie98(void) {
	uint16_t* dst;
	uint16_t color;
	size_t count, i;
	if (!buffer_ptr)
		return;
	if (g_flight16bppBytesPerPixel != 2) {
		logbuf2_clearbuffer();
		return;
	}

	dst = buffer_ptr;
	color = g_flightTextPalette[deepspacecolor];
	count = (size_t)pixelswide * pixelsdeep;
	for (i = 0; i < count; ++i)
		dst[i] = color;
}

// FUNCTION: TIE95 0x2E89C
void logbuf2_outbuffer(const void* src) {
	uint16_t line;
	uint8_t* dst = vesa_buff_gbl + displaycorner;
	const uint8_t* s = src;
	const uint16_t w = pixelswide;
	const uint16_t h = pixelsdeep;
	const int32_t pitch = vesa_bpsl_gbl;

	for (line = 0; line < h; ++line) {
		memcpy(dst, s, w);
		s += w;
		dst += pitch;
	}
}

// FUNCTION: TIE98 0x44C3B0
void logbuf2_outbuffer_tie98(const void* src) {
	uint16_t line;
	uint8_t* dst = vesa_buff_gbl + displaycorner;
	const uint8_t* s = src;
	const size_t row_bytes = (size_t)g_flight16bppBytesPerPixel * pixelswide;

	for (line = 0; line < pixelsdeep; ++line) {
		memcpy(dst, s, row_bytes);
		s += row_bytes;
		dst += g_surfacePitch;
	}
}

// FUNCTION: TIE95 0x2E978
void logbuf2_outdiffbuffer(const void* oldbuf, const void* newbuf) {
	uint16_t line;
	uint8_t* dst;
	const uint8_t* s;
	uint16_t w, h;
	int32_t pitch;
	/* Callers mirror newbuf into oldbuf after this copy. */
	(void)oldbuf;

	dst = vesa_buff_gbl + displaycorner;
	s = newbuf;
	w = pixelswide;
	h = pixelsdeep;
	pitch = vesa_bpsl_gbl;

	for (line = 0; line < h; ++line) {
		memcpy(dst, s, w);
		s += w;
		dst += pitch;
	}
}

// FUNCTION: TIE98 0x44C460
// LOGBUF2_outdiffbuffer
void logbuf2_outdiffbuffer_tie98(const void* oldbuf, const void* newbuf) {
	(void)oldbuf;
	logbuf2_outbuffer_tie98(newbuf);
}

/* ------------------------------------------------------------------
 * logbuf2_drawclippedline
 *
 * Clipped 2D line into buffer_ptr (pixelswide x pixelsdeep, pitch =
 * pixelswide). Follows the shipped binary exactly, including the
 * endpoint-exclusive convention (the line spans P1 .. P2 but the pixel
 * at P2 itself is not written — it's `max(|dx|,|dy|)` pixels).
 *
 * Canonicalises x1 <= x2 by swapping endpoints, then branches:
 *   - x1 == x2    : vertical fast path
 *   - y1 == y2    : horizontal fast path
 *   - |dx| < |dy| : steep Bresenham (step Y, accumulate X)
 *   - else        : shallow Bresenham (step X, accumulate Y)
 * Off-screen endpoints are clipped with math2_ABoverC32.
 * ------------------------------------------------------------------ */
// FUNCTION: TIE95 0x2EAF8
void logbuf2_drawclippedline(int32_t x1, int32_t y1, int32_t x2, int32_t y2, uint8_t color) {
	int32_t dx, dy, t, len, err, steps, i;
	uint8_t* p;

#ifdef TIE_MODERN
	if (!buffer_ptr)
		return;
#endif

	dx = x2 - x1;
	if (dx < 0) {
		t = x1;
		x1 = x2;
		x2 = t;
		t = y1;
		y1 = y2;
		y2 = t;
		dx = -dx;
	} else if (dx == 0) {
		/* Vertical line. */
		if (x1 < 0 || x1 >= pixelswide)
			return;
		if (y1 > y2) {
			t = y1;
			y1 = y2;
			y2 = t;
		}
		if (y1 < 0)
			y1 = 0;
		if (y2 >= pixelsdeep)
			y2 = pixelsdeepmin1;
		len = y2 - y1;
		if (len > 0) {
			p = (uint8_t*)buffer_ptr + x1 + y1 * pixelswide;
			for (i = 0; i < len; ++i) {
				*p = color;
				p += pixelswide;
			}
		}
		return;
	}

	if (x1 >= pixelswide || x2 < 0)
		return;

	dy = y2 - y1;
	if (dy >= 0) {
		if (dy == 0) {
			/* Horizontal line. */
			if (y1 < 0 || y1 >= pixelsdeep)
				return;
			if (x1 < 0)
				x1 = 0;
			if (x2 >= pixelswide)
				x2 = pixelswidemin1;
			len = x2 - x1;
			if (len > 0) {
				p = (uint8_t*)buffer_ptr + x1 + y1 * pixelswide;
				for (i = 0; i < len; ++i)
					*p++ = color;
			}
			return;
		}

		/* Downward line. */
		if (y1 >= pixelsdeep || y2 < 0)
			return;
		if (y1 < 0) {
			x1 += math2_ABoverC32(-y1, dx, dy);
			if (x1 >= pixelswide)
				return;
			y1 = 0;
		}
		if (x1 < 0) {
			y1 += math2_ABoverC32(-x1, dy, dx);
			if (y1 >= pixelsdeep)
				return;
			x1 = 0;
		}
		if (x2 >= pixelswide)
			x2 = pixelswidemin1;
		if (y2 >= pixelsdeep)
			y2 = pixelsdeepmin1;
		p = (uint8_t*)buffer_ptr + x1 + y1 * pixelswide;
		if (dx < dy) {
			steps = x2 - x1 + 1;
			err = dy >> 1;
			len = y2 - y1;
			for (i = 0; i < len; ++i) {
				*p = color;
				p += pixelswide;
				err -= dx;
				if (err < 0) {
					err += dy;
					if (--steps == 0)
						return;
					++p;
				}
			}
		} else {
			steps = y2 - y1 + 1;
			err = dx >> 1;
			len = x2 - x1;
			for (i = 0; i < len; ++i) {
				*p++ = color;
				err -= dy;
				if (err < 0) {
					err += dx;
					if (--steps == 0)
						return;
					p += pixelswide;
				}
			}
		}
	} else {
		/* Upward line. */
		dy = -dy;
		if (y1 < 0 || y2 >= pixelsdeep)
			return;
		if (y1 >= pixelsdeep) {
			x1 += math2_ABoverC32(y1 - pixelsdeepmin1, dx, dy);
			if (x1 >= pixelswide)
				return;
			y1 = pixelsdeepmin1;
		}
		if (x1 < 0) {
			y1 -= math2_ABoverC32(-x1, dy, dx);
			if (y1 < 0)
				return;
			x1 = 0;
		}
		if (x2 >= pixelswide)
			x2 = pixelswidemin1;
		if (y2 < 0)
			y2 = 0;
		p = (uint8_t*)buffer_ptr + x1 + y1 * pixelswide;
		if (dx < dy) {
			steps = x2 - x1 + 1;
			err = dy >> 1;
			len = y1 - y2;
			for (i = 0; i < len; ++i) {
				*p = color;
				p -= pixelswide;
				err -= dx;
				if (err < 0) {
					err += dy;
					if (--steps == 0)
						return;
					++p;
				}
			}
		} else {
			steps = y1 - y2 + 1;
			err = dx >> 1;
			len = x2 - x1;
			for (i = 0; i < len; ++i) {
				*p++ = color;
				err -= dy;
				if (err < 0) {
					err += dx;
					if (--steps == 0)
						return;
					p -= pixelswide;
				}
			}
		}
	}
}

// FUNCTION: TIE98 0x44C8A0
// LOGBUF2_drawclippedline16
void logbuf2_drawclippedline16_tie98(int32_t x1, int32_t y1, int32_t x2, int32_t y2, uint8_t color) {
	int32_t dx, dy, t, len, err, steps, i;
	uint16_t* p;
	uint16_t value;

#ifdef TIE_MODERN
	if (!buffer_ptr)
		return;
#endif
	value = g_flightTextPalette[color];
	dx = x2 - x1;
	if (dx < 0) {
		t = x1;
		x1 = x2;
		x2 = t;
		t = y1;
		y1 = y2;
		y2 = t;
		dx = -dx;
	} else if (dx == 0) {
		/* Vertical line. */
		if (x1 < 0 || x1 >= pixelswide)
			return;
		if (y1 > y2) {
			t = y1;
			y1 = y2;
			y2 = t;
		}
		if (y1 < 0)
			y1 = 0;
		if (y2 >= pixelsdeep)
			y2 = pixelsdeepmin1;
		len = y2 - y1;
		if (len > 0) {
			p = (uint16_t*)buffer_ptr + x1 + y1 * pixelswide;
			for (i = 0; i < len; ++i) {
				*p = value;
				p += pixelswide;
			}
		}
		return;
	}

	if (x1 >= pixelswide || x2 < 0)
		return;

	dy = y2 - y1;
	if (dy >= 0) {
		if (dy == 0) {
			/* Horizontal line. */
			if (y1 < 0 || y1 >= pixelsdeep)
				return;
			if (x1 < 0)
				x1 = 0;
			if (x2 >= pixelswide)
				x2 = pixelswidemin1;
			len = x2 - x1;
			if (len > 0) {
				p = (uint16_t*)buffer_ptr + x1 + y1 * pixelswide;
				for (i = 0; i < len; ++i)
					*p++ = value;
			}
			return;
		}

		/* Downward line. */
		if (y1 >= pixelsdeep || y2 < 0)
			return;
		if (y1 < 0) {
			x1 += math2_ABoverC32(-y1, dx, dy);
			if (x1 >= pixelswide)
				return;
			y1 = 0;
		}
		if (x1 < 0) {
			y1 += math2_ABoverC32(-x1, dy, dx);
			if (y1 >= pixelsdeep)
				return;
			x1 = 0;
		}
		if (x2 >= pixelswide)
			x2 = pixelswidemin1;
		if (y2 >= pixelsdeep)
			y2 = pixelsdeepmin1;
		p = (uint16_t*)buffer_ptr + x1 + y1 * pixelswide;
		if (dx < dy) {
			steps = x2 - x1 + 1;
			err = dy >> 1;
			len = y2 - y1;
			for (i = 0; i < len; ++i) {
				*p = value;
				p += pixelswide;
				err -= dx;
				if (err < 0) {
					err += dy;
					if (--steps == 0)
						return;
					++p;
				}
			}
		} else {
			steps = y2 - y1 + 1;
			err = dx >> 1;
			len = x2 - x1;
			for (i = 0; i < len; ++i) {
				*p++ = value;
				err -= dy;
				if (err < 0) {
					err += dx;
					if (--steps == 0)
						return;
					p += pixelswide;
				}
			}
		}
	} else {
		/* Upward line. */
		dy = -dy;
		if (y1 < 0 || y2 >= pixelsdeep)
			return;
		if (y1 >= pixelsdeep) {
			x1 += math2_ABoverC32(y1 - pixelsdeepmin1, dx, dy);
			if (x1 >= pixelswide)
				return;
			y1 = pixelsdeepmin1;
		}
		if (x1 < 0) {
			y1 -= math2_ABoverC32(-x1, dy, dx);
			if (y1 < 0)
				return;
			x1 = 0;
		}
		if (x2 >= pixelswide)
			x2 = pixelswidemin1;
		if (y2 < 0)
			y2 = 0;
		p = (uint16_t*)buffer_ptr + x1 + y1 * pixelswide;
		if (dx < dy) {
			steps = x2 - x1 + 1;
			err = dy >> 1;
			len = y1 - y2;
			for (i = 0; i < len; ++i) {
				*p = value;
				p -= pixelswide;
				err -= dx;
				if (err < 0) {
					err += dy;
					if (--steps == 0)
						return;
					++p;
				}
			}
		} else {
			steps = y1 - y2 + 1;
			err = dx >> 1;
			len = x2 - x1;
			for (i = 0; i < len; ++i) {
				*p++ = value;
				err -= dy;
				if (err < 0) {
					err += dx;
					if (--steps == 0)
						return;
					p -= pixelswide;
				}
			}
		}
	}
}

// FUNCTION: TIE98 0x44C470
// LOGBUF2_drawclippedline
void logbuf2_drawclippedline_tie98(int32_t x1, int32_t y1, int32_t x2, int32_t y2, uint8_t color) {
	int32_t dx, dy, t, len, err, steps, i;
	uint8_t* p;

	if (g_flight16bppBytesPerPixel == 2) {
		logbuf2_drawclippedline16_tie98(x1, y1, x2, y2, color);
		return;
	}
#ifdef TIE_MODERN
	if (!buffer_ptr)
		return;
#endif

	dx = x2 - x1;
	if (dx < 0) {
		t = x1;
		x1 = x2;
		x2 = t;
		t = y1;
		y1 = y2;
		y2 = t;
		dx = -dx;
	} else if (dx == 0) {
		/* Vertical line. */
		if (x1 < 0 || x1 >= pixelswide)
			return;
		if (y1 > y2) {
			t = y1;
			y1 = y2;
			y2 = t;
		}
		if (y1 < 0)
			y1 = 0;
		if (y2 >= pixelsdeep)
			y2 = pixelsdeepmin1;
		len = y2 - y1;
		if (len > 0) {
			p = (uint8_t*)buffer_ptr + x1 + y1 * pixelswide;
			for (i = 0; i < len; ++i) {
				*p = color;
				p += pixelswide;
			}
		}
		return;
	}

	if (x1 >= pixelswide || x2 < 0)
		return;

	dy = y2 - y1;
	if (dy >= 0) {
		if (dy == 0) {
			/* Horizontal line. */
			if (y1 < 0 || y1 >= pixelsdeep)
				return;
			if (x1 < 0)
				x1 = 0;
			if (x2 >= pixelswide)
				x2 = pixelswidemin1;
			len = x2 - x1;
			if (len > 0) {
				p = (uint8_t*)buffer_ptr + x1 + y1 * pixelswide;
				memset(p, color, (size_t)len);
			}
			return;
		}

		/* Downward line. */
		if (y1 >= pixelsdeep || y2 < 0)
			return;
		if (y1 < 0) {
			x1 += math2_ABoverC32(-y1, dx, dy);
			if (x1 >= pixelswide)
				return;
			y1 = 0;
		}
		if (x1 < 0) {
			y1 += math2_ABoverC32(-x1, dy, dx);
			if (y1 >= pixelsdeep)
				return;
			x1 = 0;
		}
		if (x2 >= pixelswide)
			x2 = pixelswidemin1;
		if (y2 >= pixelsdeep)
			y2 = pixelsdeepmin1;
		p = (uint8_t*)buffer_ptr + x1 + y1 * pixelswide;
		if (dx < dy) {
			steps = x2 - x1 + 1;
			err = dy >> 1;
			len = y2 - y1;
			for (i = 0; i < len; ++i) {
				*p = color;
				p += pixelswide;
				err -= dx;
				if (err < 0) {
					err += dy;
					if (--steps == 0)
						return;
					++p;
				}
			}
		} else {
			steps = y2 - y1 + 1;
			err = dx >> 1;
			len = x2 - x1;
			for (i = 0; i < len; ++i) {
				*p++ = color;
				err -= dy;
				if (err < 0) {
					err += dx;
					if (--steps == 0)
						return;
					p += pixelswide;
				}
			}
		}
	} else {
		/* Upward line. */
		dy = -dy;
		if (y1 < 0 || y2 >= pixelsdeep)
			return;
		if (y1 >= pixelsdeep) {
			x1 += math2_ABoverC32(y1 - pixelsdeepmin1, dx, dy);
			if (x1 >= pixelswide)
				return;
			y1 = pixelsdeepmin1;
		}
		if (x1 < 0) {
			y1 -= math2_ABoverC32(-x1, dy, dx);
			if (y1 < 0)
				return;
			x1 = 0;
		}
		if (x2 >= pixelswide)
			x2 = pixelswidemin1;
		if (y2 < 0)
			y2 = 0;
		p = (uint8_t*)buffer_ptr + x1 + y1 * pixelswide;
		if (dx < dy) {
			steps = x2 - x1 + 1;
			err = dy >> 1;
			len = y1 - y2;
			for (i = 0; i < len; ++i) {
				*p = color;
				p -= pixelswide;
				err -= dx;
				if (err < 0) {
					err += dy;
					if (--steps == 0)
						return;
					++p;
				}
			}
		} else {
			steps = y1 - y2 + 1;
			err = dx >> 1;
			len = x2 - x1;
			for (i = 0; i < len; ++i) {
				*p++ = color;
				err -= dy;
				if (err < 0) {
					err += dx;
					if (--steps == 0)
						return;
					p -= pixelswide;
				}
			}
		}
	}
}

/* ------------------------------------------------------------------
 * startPIP / finishPIP — save/restore the drawing viewport and the
 * world-to-eye rotation matrix, and swap the XTRANS2 side-buffer
 * pointers. The binary uses -8192 / -16384 for maskbufptr depending on
 * which pair of side-buffers is active; we preserve those literals.
 * ------------------------------------------------------------------ */
// FUNCTION: TIE95 0x2EF3C
void logbuf2_startPIP(uint16_t width, uint16_t depth, int16_t clear_runs, uint32_t dc) {
	temppw = pixelswide;
	temppd = pixelsdeep;
	tempdc = (uint16_t)displaycorner; /* binary truncates to 16 bits */
	tempA1 = worldeyeA1;
	tempA2 = worldeyeA2;
	tempA3 = worldeyeA3;
	tempB1 = worldeyeB1;
	tempB2 = worldeyeB2;
	tempB3 = worldeyeB3;
	tempC1 = worldeyeC1;
	tempC2 = worldeyeC2;
	tempC3 = worldeyeC3;

	logbuf2_setbufferdimensions(width, depth, dc);

	maskbufptr = (int16_t)0xE000; /* -8192 */
	rightside = rightsidedata2;
	leftside = leftsidedata2;

	if (clear_runs)
		xtrans2_clearruntable();
}

// FUNCTION: TIE95 0x2F070
void logbuf2_finishPIP(void) {
	uint32_t smw;
	worldeyeA1 = tempA1;
	worldeyeA2 = tempA2;
	worldeyeA3 = tempA3;
	worldeyeB1 = tempB1;
	worldeyeB2 = tempB2;
	worldeyeB3 = tempB3;
	worldeyeC1 = tempC1;
	worldeyeC2 = tempC2;
	worldeyeC3 = tempC3;

	smw = (uint32_t)vesa_bpsl_gbl;
	pixelswide = temppw;
	pixelswidemin1 = (uint16_t)(temppw - 1);
	halfpixelswide = (uint16_t)(temppw / 2);
	pixelsdeep = temppd;
	pixelsdeepmin1 = (uint16_t)(temppd - 1);
	halfpixelsdeep = (uint16_t)(temppd / 2);
	displaycorner = tempdc;
	displaycorner_lines = smw ? (tempdc / smw) : 0;
	displaycorner_columns = smw ? (tempdc % smw) : tempdc;

	maskbufptr = (int16_t)0xC000; /* -16384 */
	leftside = leftsidedata1;
	rightside = rightsidedata1;
}

// FUNCTION: TIE98 0x44CCB0
// LOGBUF2_startPIP
void logbuf2_startPIP_tie98(uint16_t width, uint16_t depth, int clear_runs, uint32_t dc) {
	(void)clear_runs;
	temppw = pixelswide;
	temppd = pixelsdeep;
	tempdc = (uint16_t)displaycorner;
	tempA1 = worldeyeA1;
	tempA2 = worldeyeA2;
	tempA3 = worldeyeA3;
	tempB1 = worldeyeB1;
	tempB2 = worldeyeB2;
	tempB3 = worldeyeB3;
	tempC1 = worldeyeC1;
	tempC2 = worldeyeC2;
	tempC3 = worldeyeC3;

	pixelswide = width;
	pixelswidemin1 = (uint16_t)(width - 1);
	halfpixelswide = (uint16_t)(width >> 1);
	pixelsdeep = depth;
	pixelsdeepmin1 = (uint16_t)(depth - 1);
	halfpixelsdeep = (uint16_t)(depth >> 1);
	displaycorner = dc;
	maskbufptr = (int16_t)0xE000;
	displaycorner_lines = dc / g_surfacePitch;
	displaycorner_columns = dc % g_surfacePitch / g_flight16bppBytesPerPixel;
}

// FUNCTION: TIE98 0x44CDC0
// LOGBUF2_finishPIP
void logbuf2_finishPIP_tie98(void) {
	worldeyeA1 = tempA1;
	worldeyeA2 = tempA2;
	worldeyeA3 = tempA3;
	worldeyeB1 = tempB1;
	worldeyeB2 = tempB2;
	worldeyeB3 = tempB3;
	worldeyeC1 = tempC1;
	worldeyeC2 = tempC2;
	worldeyeC3 = tempC3;

	pixelswide = temppw;
	pixelswidemin1 = (uint16_t)(temppw - 1);
	halfpixelswide = (uint16_t)(temppw >> 1);
	pixelsdeep = temppd;
	pixelsdeepmin1 = (uint16_t)(temppd - 1);
	halfpixelsdeep = (uint16_t)(temppd >> 1);
	displaycorner = tempdc;
	maskbufptr = (int16_t)0xC000;
	displaycorner_lines = tempdc / g_surfacePitch;
	displaycorner_columns = tempdc % g_surfacePitch / g_flight16bppBytesPerPixel;
}
