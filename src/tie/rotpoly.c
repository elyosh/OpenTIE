#include "tie/rotpoly.h"

#include "landru/bitmap.h"
#include "landru/canvas.h"

#define EDGE_TABLE_U_OFFSET 200 /* 400 bytes / sizeof(int16_t) */
#define EDGE_TABLE_V_OFFSET 400 /* 800 bytes / sizeof(int16_t) */

// FUNCTION: TIE 0x87890
void rotpoly_Build_Ratio(int16_t* dest, int16_t count, int16_t start, int16_t end) {
	int16_t i, accum, delta;

	if (start == end) {
		for (i = 0; i < count; i++)
			dest[i] = start;
		return;
	}

	if (start < end) {
		delta = end - start;
		accum = delta / 2;
		for (i = 0; i < count; i++) {
			dest[i] = start;
			accum += delta;
			while (accum >= count) {
				accum -= count;
				start++;
			}
		}
	} else {
		delta = start - end;
		accum = delta / 2;
		for (i = 0; i < count; i++) {
			dest[i] = start;
			accum += delta;
			while (accum >= count) {
				accum -= count;
				start--;
			}
		}
	}
}

// FUNCTION: TIE 0x87934
void rotpoly_Map_Image(void* src_data, const int16_t* left_table, int16_t src_stride,
					   const int16_t* right_table, int16_t num_scanlines, int16_t start_y) {
	BitmapStruct* canvas_bm;
	uint8_t* canvas_pixels;
	int16_t row;

	canvas_bm = lcanvas_Get_Current_Canvas_Bitmap();
	canvas_pixels = (uint8_t*)lbitmap_Lock_Bitmap(canvas_bm);

	for (row = 0; row < num_scanlines; row++) {
		/* Read edge coordinates for this scanline */
		int16_t right_x = right_table[row];
		int16_t right_u = right_table[row + EDGE_TABLE_U_OFFSET];
		int16_t right_v = right_table[row + EDGE_TABLE_V_OFFSET];
		int16_t left_x = left_table[row];
		int16_t left_u = left_table[row + EDGE_TABLE_U_OFFSET];
		int16_t left_v = left_table[row + EDGE_TABLE_V_OFFSET];

		int16_t span = right_x + 1 - left_x;
		if (span <= 0) {
			start_y++;
			continue;
		}

		/* PORT: the shared mapper targets either the VGA or SVGA canvas. */
		uint8_t* dest = canvas_pixels + (int)start_y * canvas_bm->w + left_x;
		uint8_t* src = (uint8_t*)src_data + left_v * src_stride + left_u;

		/* Compute Bresenham-style du stepping */
		int16_t du_delta = right_u - left_u;
		int16_t du_step, du_step_round, du_frac;
		int16_t du_dir;

		if (du_delta == 0) {
			du_step = 0;
			du_step_round = 0;
			du_frac = 0;
		} else if (du_delta > 0) {
			du_dir = 1;
			du_frac = (du_delta + 1) % span;
			du_step = (du_delta + 1) / span;
			du_step_round = du_step + du_dir;
		} else {
			du_dir = -1;
			int16_t abs_du = 1 - du_delta;
			du_frac = abs_du % span;
			du_step = -(abs_du / span);
			du_step_round = du_step + du_dir;
		}

		/* Compute Bresenham-style dv stepping (scaled by src_stride) */
		int16_t dv_delta = right_v - left_v;
		int16_t dv_step, dv_step_round, dv_frac;
		int16_t stride_dir;

		if (dv_delta == 0) {
			dv_step = 0;
			dv_step_round = 0;
			dv_frac = 0;
		} else if (dv_delta > 0) {
			stride_dir = src_stride;
			dv_frac = (dv_delta + 1) % span;
			dv_step = src_stride * ((dv_delta + 1) / span);
			dv_step_round = dv_step + stride_dir;
		} else {
			stride_dir = -src_stride;
			int16_t abs_dv = 1 - dv_delta;
			dv_frac = abs_dv % span;
			dv_step = -(src_stride * (abs_dv / span));
			dv_step_round = dv_step + stride_dir;
		}

		/* Walk the span, sampling source texture */
		int16_t u_accum = 0;
		int16_t v_accum = 0;
		int16_t px;

		for (px = 0; px < span; px++) {
			if (*src)
				*dest = *src;

			u_accum += du_frac;
			dest++;

			int16_t u_advance;
			if (u_accum >= span) {
				u_advance = du_step_round;
				u_accum -= span;
			} else {
				u_advance = du_step;
			}

			src += u_advance;

			v_accum += dv_frac;
			int16_t v_advance;
			if (v_accum >= span) {
				v_advance = dv_step_round;
				v_accum -= span;
			} else {
				v_advance = dv_step;
			}

			src += v_advance;
		}

		start_y++;
	}

	lbitmap_Unlock_Bitmap(canvas_bm);
}
