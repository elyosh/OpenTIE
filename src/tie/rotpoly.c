#include "tie/rotpoly.h"
#include "landru/bitmap.h"
#include "landru/canvas.h"

enum {
	EDGE_TABLE_U_OFFSET = 200, /* 400 bytes / sizeof(int16_t) */
	EDGE_TABLE_V_OFFSET = 400, /* 800 bytes / sizeof(int16_t) */
};

// FUNCTION: TIE95 0x87890
void rotpoly_Build_Ratio(int16_t* dest, int16_t count, int16_t start, int16_t end) {
	int16_t remaining, accum, delta;

	if (start > end) {
		delta = start - end;
		remaining = count;
		accum = delta >> 1;
		for (;;) {
			*dest++ = start;
			accum += delta;
			while (accum >= remaining) {
				accum -= remaining;
				start--;
			}
			if (--count <= 0)
				break;
		}
	} else if (start == end) {
		for (remaining = count; remaining > 0; remaining--)
			*dest++ = start;
	} else {
		delta = end - start;
		remaining = count;
		accum = delta >> 1;
		for (;;) {
			*dest++ = start;
			accum += delta;
			while (accum >= remaining) {
				accum -= remaining;
				start++;
			}
			if (--count <= 0)
				break;
		}
	}
}

// FUNCTION: TIE95 0x87934
void rotpoly_Map_Image(void* src_data, const int16_t* left_table, const int16_t* right_table,
					   int16_t src_stride, int16_t map_mode, int16_t num_scanlines, int16_t start_y) {
	BitmapStruct* canvas_bm;
	uint8_t* canvas_pixels;
	int16_t row;

	(void)map_mode;

	canvas_bm = xcanvas_Get_Current_Canvas_Bitmap();
	canvas_pixels = (uint8_t*)xbm_Lock_Bitmap(canvas_bm);

	for (row = 0; row < num_scanlines; row++) {
		uint8_t* dest;
		uint8_t* src;
		int16_t right_x, right_u, right_v;
		int16_t left_x, left_u, left_v;
		int16_t span;
		int16_t du_delta, du_step, du_step_round, du_frac;
		int16_t dv_delta, dv_step, dv_step_round, dv_frac;
		int16_t step, frac, dir;
		int16_t u_accum, v_accum;
		int16_t count;
		int16_t y;

		/* Read edge coordinates for this scanline */
		right_x = right_table[row];
		right_u = right_table[row + EDGE_TABLE_U_OFFSET];
		right_v = right_table[row + EDGE_TABLE_V_OFFSET];
		left_x = left_table[row];
		left_u = left_table[row + EDGE_TABLE_U_OFFSET];
		left_v = left_table[row + EDGE_TABLE_V_OFFSET];

		span = right_x + 1 - left_x;
		y = start_y + row;

#ifdef TIE_MODERN
		/* PORT: the shared mapper targets either the VGA or SVGA canvas. */
		dest = canvas_pixels + (int)y * canvas_bm->w + left_x;
#else
		dest = canvas_pixels + (y << 8) + (y << 6) + left_x;
#endif
		src = (uint8_t*)src_data + left_u + left_v * src_stride;

		/* Compute Bresenham-style du stepping */
		du_delta = right_u - left_u;
		if (du_delta != 0) {
			if (du_delta > 0) {
				du_delta++;
				frac = du_delta % span;
				step = du_delta / span;
				dir = 1;
			} else {
				du_delta = -(du_delta - 1);
				frac = du_delta % span;
				step = -(du_delta / span);
				dir = -1;
			}
			du_step = step;
			du_step_round = step + dir;
			du_frac = frac;
		} else {
			du_step_round = 0;
			du_frac = 0;
			du_step = 0;
		}

		/* Compute Bresenham-style dv stepping (scaled by src_stride) */
		dir = src_stride;
		dv_delta = right_v - left_v;
		if (dv_delta != 0) {
			if (dv_delta > 0) {
				dv_delta++;
				frac = dv_delta % span;
				step = dv_delta / span * src_stride;
			} else {
				dv_delta = -(dv_delta - 1);
				frac = dv_delta % span;
				step = -(dv_delta / span * src_stride);
				dir = -src_stride;
			}
			dv_frac = frac;
			dv_step = step;
			dv_step_round = step + dir;
		} else {
			dv_step_round = 0;
			dv_step = 0;
			dv_frac = 0;
		}

		/* Walk the span, sampling source texture */
		u_accum = 0;
		v_accum = 0;
		for (count = span; count > 0; count--) {
			if (*src)
				*dest = *src;

			u_accum += du_frac;
			dest++;
			if (u_accum < span) {
				src += du_step;
			} else {
				src += du_step_round;
				u_accum -= span;
			}

			v_accum += dv_frac;
			if (v_accum < span) {
				src += dv_step;
			} else {
				src += dv_step_round;
				v_accum -= span;
			}
		}
	}

	xbm_Unlock_Bitmap(canvas_bm);
}
