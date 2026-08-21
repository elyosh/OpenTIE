#include <stdint.h>
#include <string.h>

#include "landru/vesa.h"
#include "tie/logbuf2.h"
#include "tie/math2.h"
#include "tie/render_texture_tie98.h"
#include "tie/transfm2.h"
#include "tie/xtrans2.h"

/* --- Module-owned globals ----------------------------------------- */

// GLOBAL: TIE 0xD4C1E
uint16_t pixelswide;
// GLOBAL: TIE 0xD4C1C
uint16_t pixelswidemin1;
// GLOBAL: TIE 0xD4C18
uint16_t halfpixelswide;
// GLOBAL: TIE 0xD4C20
uint16_t pixelsdeep;
// GLOBAL: TIE 0xD4C1A
uint16_t pixelsdeepmin1;
// GLOBAL: TIE 0xD4C16
uint16_t halfpixelsdeep;
// GLOBAL: TIE 0xD4C08
uint32_t displaycorner;
// GLOBAL: TIE 0xD4C00
uint32_t displaycorner_lines;
// GLOBAL: TIE 0xD4C04
uint32_t displaycorner_columns;
// GLOBAL: TIE 0xD4C0C
void* buffer_ptr;
/* Retail Z_TIE__.EXE ships this initialized to 0xFB (== (uint8_t)-5) at
 * 0xC5504. tie_updatescreen only assigns -5 AFTER the world render, so
 * the very first flight frame's logbuf2_clearbuffer() uses whatever value
 * is here at startup. Without the retail's initializer, buffer_ptr gets
 * memset to 0 on the first fullupdateflag pass, leaving xtrans2's
 * "color==0 => copy from logbuf" branch emitting zeros and stars'
 * `*dst < deepspacecolor` guard skipping every draw -- flight viewport
 * stays black until some non-render path happens to set deepspacecolor. */
// GLOBAL: TIE 0xC5504
uint8_t deepspacecolor = 0xFB;

// GLOBAL: TIE98 0x5926D8
uint32_t g_surfacePitch;
// GLOBAL: TIE98 0x4F2ACC
uint32_t g_flight16bppBytesPerPixel;

/* PIP state (saved by startPIP, restored by finishPIP). */
static uint16_t s_temp_pw, s_temp_pd, s_temp_dc;
static int32_t s_tempA1, s_tempA2, s_tempA3;
static int32_t s_tempB1, s_tempB2, s_tempB3;
static int32_t s_tempC1, s_tempC2, s_tempC3;

// GLOBAL: TIE98 0x584C0C
static uint16_t s_tie98_temp_pw;
// GLOBAL: TIE98 0x584C08
static uint16_t s_tie98_temp_pd;
// GLOBAL: TIE98 0x584C04
static uint16_t s_tie98_temp_dc;
// GLOBAL: TIE98 0x584BE0
static int32_t s_tie98_tempA1;
// GLOBAL: TIE98 0x584BE4
static int32_t s_tie98_tempA2;
// GLOBAL: TIE98 0x584BF0
static int32_t s_tie98_tempA3;
// GLOBAL: TIE98 0x584BE8
static int32_t s_tie98_tempB1;
// GLOBAL: TIE98 0x584BEC
static int32_t s_tie98_tempB2;
// GLOBAL: TIE98 0x584BFC
static int32_t s_tie98_tempB3;
// GLOBAL: TIE98 0x584BF4
static int32_t s_tie98_tempC1;
// GLOBAL: TIE98 0x584BF8
static int32_t s_tie98_tempC2;
// GLOBAL: TIE98 0x584C00
static int32_t s_tie98_tempC3;

/* --- helpers ------------------------------------------------------ */

static inline uint8_t* video_base(void) { return vesa_buff_gbl; }

static inline int32_t screen_mem_width(void) { return vesa_bpsl_gbl; }

/* --- API ---------------------------------------------------------- */

// FUNCTION: TIE 0x2E7F0
void logbuf2_graphsetup(void) { /* Empty stub in the shipped binary. */ }

// FUNCTION: TIE 0x2E7F4
void logbuf2_selectbuffer(void* buffer) { buffer_ptr = buffer; }

// FUNCTION: TIE 0x2E7FC
void logbuf2_setbufferdimensions(uint16_t width, uint16_t depth, uint32_t dc) {
	const uint32_t smw = (uint32_t)screen_mem_width();

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

// FUNCTION: TIE98 0x44C2C0 LOGBUF2_setbufferdimensions
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

// FUNCTION: TIE 0x2E870
void logbuf2_clearbuffer(void) {
	if (!buffer_ptr)
		return;
	memset(buffer_ptr, deepspacecolor, (size_t)pixelswide * pixelsdeep);
}

// FUNCTION: TIE98 0x44C330
void logbuf2_clearbuffer_tie98(void) {
	if (!buffer_ptr)
		return;
	if (g_flight16bppBytesPerPixel != 2) {
		logbuf2_clearbuffer();
		return;
	}

	uint16_t* dst = buffer_ptr;
	const uint16_t color = g_flightTextPalette[deepspacecolor];
	const size_t count = (size_t)pixelswide * pixelsdeep;
	for (size_t i = 0; i < count; ++i)
		dst[i] = color;
}

// FUNCTION: TIE 0x2E89C
void logbuf2_outbuffer(const void* src) {
	uint8_t* dst = video_base() + displaycorner;
	const uint8_t* s = src;
	const uint16_t w = pixelswide;
	const uint16_t h = pixelsdeep;
	const int32_t pitch = screen_mem_width();

	for (uint16_t line = 0; line < h; ++line) {
		memcpy(dst, s, w);
		s += w;
		dst += pitch;
	}
}

// FUNCTION: TIE98 0x44C3B0
void logbuf2_outbuffer_tie98(const void* src) {
	uint8_t* dst = video_base() + displaycorner;
	const uint8_t* s = src;
	const size_t row_bytes = (size_t)g_flight16bppBytesPerPixel * pixelswide;

	for (uint16_t line = 0; line < pixelsdeep; ++line) {
		memcpy(dst, s, row_bytes);
		s += row_bytes;
		dst += g_surfacePitch;
	}
}

// FUNCTION: TIE 0x2E978
void logbuf2_outdiffbuffer(const void* oldbuf, const void* newbuf) {
	/* Callers mirror newbuf into oldbuf after this copy. */
	(void)oldbuf;

	uint8_t* dst = video_base() + displaycorner;
	const uint8_t* s = newbuf;
	const uint16_t w = pixelswide;
	const uint16_t h = pixelsdeep;
	const int32_t pitch = screen_mem_width();

	for (uint16_t line = 0; line < h; ++line) {
		memcpy(dst, s, w);
		s += w;
		dst += pitch;
	}
}

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
static inline void logbuf2_write_pixel(uint8_t* dst, uint16_t color, uint8_t pixel_bytes) {
	if (pixel_bytes == 2)
		*(uint16_t*)dst = color;
	else
		*dst = (uint8_t)color;
}

static void logbuf2_drawclippedline_impl(int32_t x1, int32_t y1, int32_t x2, int32_t y2, uint8_t color_index,
										 uint8_t pixel_bytes) {
	if (!buffer_ptr)
		return;

	const int32_t pw = (int32_t)pixelswide;
	const int32_t pd = (int32_t)pixelsdeep;
	const int32_t pwm1 = (int32_t)pixelswidemin1;
	const int32_t pdm1 = (int32_t)pixelsdeepmin1;
	uint8_t* buf = (uint8_t*)buffer_ptr;
	const uint16_t color = pixel_bytes == 2 ? g_flightTextPalette[color_index] : color_index;

	/* --- Canonicalise so x1 <= x2 --- */
	int32_t dx = x2 - x1;
	if (dx < 0) {
		int32_t tx = x1, ty = y1;
		x1 = x2;
		y1 = y2;
		x2 = tx;
		y2 = ty;
		dx = -dx;
	}

	/* --- Vertical line fast path (x1 == x2) --- */
	if (dx == 0) {
		if (x1 < 0 || x1 >= pw)
			return;
		if (y2 < y1) {
			int32_t t = y1;
			y1 = y2;
			y2 = t;
		}
		if (y1 < 0)
			y1 = 0;
		if (y2 >= pd)
			y2 = pdm1;
		int32_t len = y2 - y1;
		if (len <= 0)
			return;
		uint8_t* p = buf + pixel_bytes * (x1 + y1 * pw);
		for (int32_t i = 0; i < len; ++i) {
			logbuf2_write_pixel(p, color, pixel_bytes);
			p += pixel_bytes * pw;
		}
		return;
	}

	/* Reject if the x-range cannot intersect the buffer. */
	if (x1 >= pw || x2 < 0)
		return;

	/* --- Horizontal line fast path (y1 == y2) --- */
	if (y1 == y2) {
		if (y1 < 0 || y1 >= pd)
			return;
		int32_t xa = x1 < 0 ? 0 : x1;
		int32_t xb = x2 >= pw ? pwm1 : x2;
		int32_t len = xb - xa;
		if (len <= 0)
			return;
		uint8_t* p = buf + pixel_bytes * (xa + y1 * pw);
		for (int32_t i = 0; i < len; ++i) {
			logbuf2_write_pixel(p, color, pixel_bytes);
			p += pixel_bytes;
		}
		return;
	}

	/* --- General Bresenham --- */
	const int32_t dy_signed = y2 - y1;
	const int32_t dy = dy_signed < 0 ? -dy_signed : dy_signed;
	const int going_up = dy_signed < 0;

	if (going_up) {
		if (y1 < 0 || y2 >= pd)
			return;
		if (y1 >= pd) {
			x1 += math2_ABoverC32(y1 - pdm1, dx, dy);
			if (x1 >= pw)
				return;
			y1 = pdm1;
		}
		if (x1 < 0) {
			y1 -= math2_ABoverC32(-x1, dy, dx);
			if (y1 < 0)
				return;
			x1 = 0;
		}
		if (x2 >= pw)
			x2 = pwm1;
		if (y2 < 0)
			y2 = 0;
	} else {
		if (y1 >= pd || y2 < 0)
			return;
		if (y1 < 0) {
			x1 += math2_ABoverC32(-y1, dx, dy);
			if (x1 >= pw)
				return;
			y1 = 0;
		}
		if (x1 < 0) {
			y1 += math2_ABoverC32(-x1, dy, dx);
			if (y1 >= pd)
				return;
			x1 = 0;
		}
		if (x2 >= pw)
			x2 = pwm1;
		if (y2 >= pd)
			y2 = pdm1;
	}

	uint8_t* p = buf + pixel_bytes * (x1 + y1 * pw);
	const int32_t ystep = going_up ? -pixel_bytes * pw : pixel_bytes * pw;
	const int32_t xspan = x2 - x1;
	const int32_t yspan = going_up ? (y1 - y2) : (y2 - y1);

	if (dx < dy) {
		/* Steep: one pixel per Y step, X accumulates. */
		int32_t cols_left = xspan + 1;
		int32_t err = dy >> 1;
		for (int32_t i = 0; i < yspan; ++i) {
			logbuf2_write_pixel(p, color, pixel_bytes);
			err -= dx;
			p += ystep;
			if (err < 0) {
				err += dy;
				if (--cols_left == 0)
					return;
				p += pixel_bytes;
			}
		}
	} else {
		/* Shallow: one pixel per X step, Y accumulates. */
		int32_t rows_left = yspan + 1;
		int32_t err = dx >> 1;
		for (int32_t i = 0; i < xspan; ++i) {
			logbuf2_write_pixel(p, color, pixel_bytes);
			p += pixel_bytes;
			err -= dy;
			if (err < 0) {
				err += dx;
				if (--rows_left == 0)
					return;
				p += ystep;
			}
		}
	}
}

// FUNCTION: TIE 0x2EAF8
void logbuf2_drawclippedline(int32_t x1, int32_t y1, int32_t x2, int32_t y2, uint8_t color) {
	logbuf2_drawclippedline_impl(x1, y1, x2, y2, color, 1);
}

// FUNCTION: TIE98 0x44C470, 0x44C8A0
void logbuf2_drawclippedline_tie98(int32_t x1, int32_t y1, int32_t x2, int32_t y2, uint8_t color) {
	logbuf2_drawclippedline_impl(x1, y1, x2, y2, color, (uint8_t)g_flight16bppBytesPerPixel);
}

/* ------------------------------------------------------------------
 * startPIP / finishPIP — save/restore the drawing viewport and the
 * world-to-eye rotation matrix, and swap the XTRANS2 side-buffer
 * pointers. The binary uses -8192 / -16384 for maskbufptr depending on
 * which pair of side-buffers is active; we preserve those literals.
 * ------------------------------------------------------------------ */
// FUNCTION: TIE 0x2EF3C
void logbuf2_startPIP(uint16_t width, uint16_t depth, int16_t clear_runs, uint32_t dc) {
	s_temp_pw = pixelswide;
	s_temp_pd = pixelsdeep;
	s_temp_dc = (uint16_t)displaycorner; /* binary truncates to 16 bits */
	s_tempA1 = worldeyeA1;
	s_tempA2 = worldeyeA2;
	s_tempA3 = worldeyeA3;
	s_tempB1 = worldeyeB1;
	s_tempB2 = worldeyeB2;
	s_tempB3 = worldeyeB3;
	s_tempC1 = worldeyeC1;
	s_tempC2 = worldeyeC2;
	s_tempC3 = worldeyeC3;

	logbuf2_setbufferdimensions(width, depth, dc);

	maskbufptr = (int16_t)0xE000; /* -8192 */
	rightside = rightsidedata2;
	leftside = leftsidedata2;

	if (clear_runs)
		xtrans2_clearruntable();
}

// FUNCTION: TIE 0x2F070
void logbuf2_finishPIP(void) {
	worldeyeA1 = s_tempA1;
	worldeyeA2 = s_tempA2;
	worldeyeA3 = s_tempA3;
	worldeyeB1 = s_tempB1;
	worldeyeB2 = s_tempB2;
	worldeyeB3 = s_tempB3;
	worldeyeC1 = s_tempC1;
	worldeyeC2 = s_tempC2;
	worldeyeC3 = s_tempC3;

	const uint32_t smw = (uint32_t)screen_mem_width();
	pixelswide = s_temp_pw;
	pixelswidemin1 = (uint16_t)(s_temp_pw - 1);
	halfpixelswide = (uint16_t)(s_temp_pw / 2);
	pixelsdeep = s_temp_pd;
	pixelsdeepmin1 = (uint16_t)(s_temp_pd - 1);
	halfpixelsdeep = (uint16_t)(s_temp_pd / 2);
	displaycorner = s_temp_dc;
	displaycorner_lines = smw ? (s_temp_dc / smw) : 0;
	displaycorner_columns = smw ? (s_temp_dc % smw) : s_temp_dc;

	maskbufptr = (int16_t)0xC000; /* -16384 */
	leftside = leftsidedata1;
	rightside = rightsidedata1;
}

// FUNCTION: TIE98 0x44CCB0 LOGBUF2_startPIP
void logbuf2_startPIP_tie98(uint16_t width, uint16_t depth, int clear_runs, uint32_t dc) {
	(void)clear_runs;
	s_tie98_temp_pw = pixelswide;
	s_tie98_temp_pd = pixelsdeep;
	s_tie98_temp_dc = (uint16_t)displaycorner;
	s_tie98_tempA1 = worldeyeA1;
	s_tie98_tempA2 = worldeyeA2;
	s_tie98_tempA3 = worldeyeA3;
	s_tie98_tempB1 = worldeyeB1;
	s_tie98_tempB2 = worldeyeB2;
	s_tie98_tempB3 = worldeyeB3;
	s_tie98_tempC1 = worldeyeC1;
	s_tie98_tempC2 = worldeyeC2;
	s_tie98_tempC3 = worldeyeC3;

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

// FUNCTION: TIE98 0x44CDC0 LOGBUF2_finishPIP
void logbuf2_finishPIP_tie98(void) {
	worldeyeA1 = s_tie98_tempA1;
	worldeyeA2 = s_tie98_tempA2;
	worldeyeA3 = s_tie98_tempA3;
	worldeyeB1 = s_tie98_tempB1;
	worldeyeB2 = s_tie98_tempB2;
	worldeyeB3 = s_tie98_tempB3;
	worldeyeC1 = s_tie98_tempC1;
	worldeyeC2 = s_tie98_tempC2;
	worldeyeC3 = s_tie98_tempC3;

	pixelswide = s_tie98_temp_pw;
	pixelswidemin1 = (uint16_t)(s_tie98_temp_pw - 1);
	halfpixelswide = (uint16_t)(s_tie98_temp_pw >> 1);
	pixelsdeep = s_tie98_temp_pd;
	pixelsdeepmin1 = (uint16_t)(s_tie98_temp_pd - 1);
	halfpixelsdeep = (uint16_t)(s_tie98_temp_pd >> 1);
	displaycorner = s_tie98_temp_dc;
	maskbufptr = (int16_t)0xC000;
	displaycorner_lines = s_tie98_temp_dc / g_surfacePitch;
	displaycorner_columns = s_tie98_temp_dc % g_surfacePitch / g_flight16bppBytesPerPixel;
}
