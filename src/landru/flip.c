#include <string.h>

#include <landru/canvas.h>
#include <landru/flip.h>

void lflip_Delta_Flip(uint8_t* data, int16_t x_off, int16_t y_off, int16_t x_flip_base, int16_t y_flip_base,
					  int16_t flip_x, int16_t flip_y) {
	uint16_t pixel_count = *(uint16_t*)data;
	uint8_t* ptr = data + 2;
	if (!pixel_count)
		return;

	int16_t x_base = x_off + x_flip_base;
	int16_t y_base = y_off + y_flip_base;

	do {
		int16_t data_x = *(int16_t*)ptr;
		int16_t data_y = *(int16_t*)(ptr + 2);
		uint8_t* rle = ptr + 4;

		int16_t screen_x = flip_x ? (x_base - data_x) : (x_off + data_x);
		int16_t screen_y = flip_y ? (y_base - data_y) : (y_off + data_y);

		uint8_t* dst = (uint8_t*)draw_buff_gbl + draw_w_gbl * screen_y + screen_x;
		int16_t count = pixel_count >> 1;
		int is_rle = pixel_count & 1;

		if (is_rle) {
			int16_t rem = count;
			while (rem > 0) {
				uint8_t ctrl = *rle++;
				int len = ctrl >> 1;
				if (ctrl & 1) {
					/* Run */
					uint8_t fill = *rle++;
					rem -= len;
					if (flip_x) {
						memset(dst - len + 1, fill, len);
						dst -= len;
					} else {
						memset(dst, fill, len);
						dst += len;
					}
				} else {
					/* Literal */
					rem -= len;
					if (flip_x) {
						for (int i = 0; i < len; i++)
							*dst-- = *rle++;
					} else {
						memcpy(dst, rle, len);
						dst += len;
						rle += len;
					}
				}
			}
		} else {
			/* Uncompressed */
			if (flip_x) {
				for (int16_t i = 0; i < count; i++)
					*dst-- = *rle++;
			} else {
				memcpy(dst, rle, count);
				rle += count;
			}
		}

		pixel_count = *(uint16_t*)rle;
		ptr = rle + 2;
	} while (pixel_count);
}

void lflip_Delta_Clip_Flip(uint8_t* data, int16_t x_off, int16_t y_off, int16_t x_flip_base,
						   int16_t y_flip_base, int16_t flip_x, int16_t flip_y, int16_t clip_left,
						   int16_t clip_top, int16_t clip_right, int16_t clip_bottom) {
	uint8_t* line_buf = (uint8_t*)buff_gbl;
	/* Strip writes use screen_x as a line-buffer index. For a flipped actor
	 * partially clipped on the left edge, screen_x can be negative; the
	 * subsequent Copy_Transparent step only reads from line_buf[clip_left..]
	 * (≥ 0), so writes outside [line_buf, line_buf + buff_size_gbl) are
	 * wasted and must be skipped to avoid underflow. The retail Watcom build
	 * skipped this clamp and relied on heap padding from XMEMHDL_Alloc_Handle. */
	uint8_t* buf_lo = line_buf;
	uint8_t* buf_hi = line_buf + buff_size_gbl;
	uint16_t pixel_count = *(uint16_t*)data;
	uint8_t* ptr = data + 2;
	if (!pixel_count)
		return;

	int16_t x_base = x_off + x_flip_base;
	int16_t y_base = y_off + y_flip_base;

	do {
		int16_t data_x = *(int16_t*)ptr;
		int16_t data_y = *(int16_t*)(ptr + 2);
		uint8_t* rle = ptr + 4;

		int16_t screen_x = flip_x ? (x_base - data_x) : (x_off + data_x);
		int16_t draw_y = flip_y ? (y_base - data_y) : (y_off + data_y);

		uint8_t* buf_dst = &line_buf[screen_x];
		int16_t count = pixel_count >> 1;
		int16_t copy_w = count;
		int is_rle = pixel_count & 1;

		if (is_rle) {
			int16_t rem = count;
			while (rem > 0) {
				uint8_t ctrl = *rle++;
				int len = ctrl >> 1;
				if (ctrl & 1) {
					uint8_t fill = *rle++;
					rem -= len;
					if (flip_x) {
						uint8_t* lo = buf_dst - len + 1;
						uint8_t* hi = buf_dst + 1;
						if (lo < buf_lo)
							lo = buf_lo;
						if (hi > buf_hi)
							hi = buf_hi;
						if (lo < hi)
							memset(lo, fill, hi - lo);
						buf_dst -= len;
					} else {
						memset(buf_dst, fill, len);
						buf_dst += len;
					}
				} else {
					rem -= len;
					if (flip_x) {
						for (int i = 0; i < len; i++) {
							if (buf_dst >= buf_lo && buf_dst < buf_hi)
								*buf_dst = *rle;
							buf_dst--;
							rle++;
						}
					} else {
						memcpy(buf_dst, rle, len);
						buf_dst += len;
						rle += len;
					}
				}
			}
		} else {
			if (flip_x) {
				for (int16_t i = 0; i < count; i++) {
					if (buf_dst >= buf_lo && buf_dst < buf_hi)
						*buf_dst = *rle;
					buf_dst--;
					rle++;
				}
			} else {
				memcpy(buf_dst, rle, count);
				rle += count;
			}
		}

		/* Clip and copy visible pixels to screen */
		if (draw_y >= clip_top && draw_y <= clip_bottom) {
			int16_t visible_left, visible_right;
			if (flip_x) {
				visible_right = screen_x + 1;
				visible_left = screen_x - copy_w + 1;
			} else {
				visible_left = screen_x;
				visible_right = screen_x + copy_w;
			}

			if (visible_left < clip_left) {
				copy_w -= clip_left - visible_left;
				visible_left = clip_left;
			}
			if (visible_right > clip_right)
				copy_w -= visible_right - clip_right;

			if (copy_w > 0) {
				uint8_t* screen_dst = (uint8_t*)draw_buff_gbl + draw_w_gbl * draw_y + visible_left;
				uint8_t* src = &line_buf[visible_left];
				for (int16_t i = 0; i < copy_w; i++) {
					if (src[i])
						screen_dst[i] = src[i];
				}
			}
		}

		pixel_count = *(uint16_t*)rle;
		ptr = rle + 2;
	} while (pixel_count);
}

/* Dead code — 0 xrefs in the binary */
void lflip_Copy_Transparent(uint8_t* dst, const uint8_t* src, int count) {
	for (int i = 0; i < count; i++) {
		if (src[i])
			dst[i] = src[i];
	}
}
