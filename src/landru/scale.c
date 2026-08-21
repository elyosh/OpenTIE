#include <string.h>

#include <landru/canvas.h>
#include <landru/scale.h>

#define MAX_DIM 320

/* Per-axis scaling tables: for each source pixel, how many dest pixels
   it maps to (scale_table[]) and where it starts in source (offset_table[]) */
static int16_t scale_xtable[MAX_DIM];
static int16_t offset_xtable[MAX_DIM];
static int16_t scale_ytable[MAX_DIM];
static int16_t offset_ytable[MAX_DIM];

/* Build a scaling table for one axis.
   src_count = number of source pixels (src_dim + extent).
   scale = 8.8 fixed-point factor (256 = 1:1).
   Fills scale_table[0..src_count-1] and offset_table[0..src_count-1].
   Returns total destination pixels. */
static int16_t build_scale_table(int16_t* scale_table, int16_t* offset_table, int16_t src_count,
								 int16_t scale) {
	int frac_step = scale & 0xFF;
	int frac = 0;
	int src_offset = 0;
	int16_t total_dest = 0;
	int base = scale >> 8;

	for (int i = 0; i < src_count; i++) {
		frac += frac_step;
		int carry = 0;
		if (frac > 255) {
			carry = 1;
			frac &= 0xFF;
		}
		int dest_cols = base + carry;
		scale_table[i] = dest_cols;
		offset_table[i] = src_offset;
		total_dest += dest_cols;
		src_offset += dest_cols;
	}
	return total_dest;
}

/* Clip the left/top of a scale table by removing `count` destination pixels
   from the low end. Adjusts scale values and offsets. */
static void clip_table_low(int16_t* scale_table, int16_t* offset_table, int16_t src_count, int16_t count) {
	int16_t col = 0;
	while (count > 0 && col < src_count) {
		if (scale_table[col]) {
			count--;
			scale_table[col]--;
			offset_table[col]++;
		} else {
			offset_table[col] = -1;
			col++;
		}
	}
	/* Propagate the first valid offset backwards to any -1 entries */
	if (col < src_count) {
		int16_t valid_off = offset_table[col];
		for (int16_t j = 0; j < src_count; j++) {
			if (offset_table[j] != -1)
				break;
			offset_table[j] = valid_off;
		}
	}
}

/* Clip the right/bottom of a scale table by removing `count` destination pixels
   from the high end. */
static void clip_table_high(int16_t* scale_table, int16_t src_count, int16_t count) {
	int16_t col = src_count - 1;
	while (count > 0 && col >= 0) {
		if (scale_table[col]) {
			count--;
			scale_table[col]--;
		} else {
			col--;
		}
	}
}

/* Skip past pixel_count bytes of delta data in a stream.
   Handles RLE (bit 0 of pixel_count set) and raw formats. */
static void skip_data_bytes(uint8_t** data_ptr, uint16_t pixel_count) {
	if (pixel_count & 1) {
		uint16_t remaining = pixel_count >> 1;
		while (remaining) {
			uint8_t run_byte = **data_ptr;
			uint16_t run_len = run_byte >> 1;
			(*data_ptr)++;
			remaining -= run_len;
			if (run_byte & 1)
				(*data_ptr)++; /* fill: skip 1 value byte */
			else
				*data_ptr += run_len; /* literal: skip run_len bytes */
		}
	} else {
		*data_ptr += pixel_count >> 1;
	}
}

bool lscale_Scale_Delta_Clip(uint8_t* data, int16_t x_off, int16_t y_off, int16_t src_w, int16_t src_h,
							 int16_t x_extent, int16_t y_extent, int16_t x_scale, int16_t y_scale,
							 int16_t clip_left, int16_t clip_top, int16_t clip_right, int16_t clip_bottom) {
	if (draw_w_gbl > 320 || draw_h_gbl > 200)
		return false;
	if (x_scale <= 0 || y_scale <= 0)
		return false;

	/* Phase 1: Build X scaling table */
	int16_t x_src_count = src_w + x_extent;
	int16_t total_dest_w = build_scale_table(scale_xtable, offset_xtable, x_src_count, x_scale);

	/* Clip X left */
	int16_t left_clip = clip_left - x_off;
	if (left_clip > 0)
		clip_table_low(scale_xtable, offset_xtable, x_src_count, left_clip);

	/* Clip X right */
	int16_t right_clip = total_dest_w + x_off - clip_right - 1;
	if (right_clip > 0)
		clip_table_high(scale_xtable, x_src_count, right_clip);

	/* Phase 2: Build Y scaling table */
	int16_t y_src_count = src_h + y_extent;
	int16_t total_dest_h = build_scale_table(scale_ytable, offset_ytable, y_src_count, y_scale);

	/* Clip Y top */
	int16_t top_clip = clip_top - y_off;
	if (top_clip > 0)
		clip_table_low(scale_ytable, offset_ytable, y_src_count, top_clip);

	/* Clip Y bottom */
	int16_t bottom_clip = total_dest_h + y_off - clip_bottom - 1;
	if (bottom_clip > 0)
		clip_table_high(scale_ytable, y_src_count, bottom_clip);

	/* Phase 3: Render */
	uint8_t* dp = data;
	while (1) {
		uint16_t pixel_count = *(uint16_t*)dp;
		dp += 2;
		if (!pixel_count)
			break;

		int16_t src_col = *(int16_t*)dp;
		dp += 2;
		int16_t src_row = *(int16_t*)dp;
		dp += 2;

		int16_t row_repeat = scale_ytable[src_row];
		int16_t y_dest_off = offset_ytable[src_row];
		int16_t x_dest_off = offset_xtable[src_col];

		uint8_t* dst = (uint8_t*)draw_buff_gbl + (y_off + y_dest_off) * 320 + x_off + x_dest_off;

		if (row_repeat) {
			uint8_t* saved_data = dp;
			for (int16_t r = 0; r < row_repeat; r++) {
				int16_t col = src_col;
				dp = saved_data;
				uint8_t* row_dst = dst;

				if (pixel_count & 1) {
					/* RLE delta data */
					int16_t remaining = pixel_count >> 1;
					while (remaining > 0) {
						uint8_t run_byte = *dp++;
						int16_t run_len = run_byte >> 1;
						if (run_byte & 1) {
							/* Fill run */
							uint8_t fill = *dp++;
							remaining -= run_len;
							for (int16_t p = 0; p < run_len; p++) {
								int16_t reps = scale_xtable[col++];
								while (reps-- > 0)
									*row_dst++ = fill;
							}
						} else {
							/* Literal run */
							remaining -= run_len;
							for (int16_t p = 0; p < run_len; p++) {
								int16_t reps = scale_xtable[col++];
								while (reps-- > 0)
									*row_dst++ = *dp;
								dp++;
							}
						}
					}
				} else {
					/* Raw delta data */
					int16_t count = pixel_count >> 1;
					for (int16_t p = 0; p < count; p++) {
						int16_t reps = scale_xtable[col++];
						while (reps-- > 0)
							*row_dst++ = *dp;
						dp++;
					}
				}

				dst += 320;
			}
		} else {
			/* Row is clipped — skip the data */
			skip_data_bytes(&dp, pixel_count);
		}
	}
	return true;
}
