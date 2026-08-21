#include <string.h>

#include <landru/canvas.h>
#include <landru/color.h>

/*
 * Delta image record format:
 *   u16 pixel_count  (0 = end-of-image; bit 0 = is_compressed flag)
 *   u16 x_offset     (column relative to actor origin)
 *   u16 y_offset     (row relative to actor origin)
 *   [pixel data]     (RLE if compressed, raw if not)
 *
 * RLE control byte: bit 0 = run(1) or literal(0), bits 1-7 = count.
 * Actual pixel count = pixel_count >> 1.
 * In color mode all decoded pixels are replaced with the solid fill color.
 */

void lcolor_Delta_Color_Image(uint8_t* data, int16_t x_off, int16_t y_off, uint8_t color) {
	uint8_t* ptr = data + 2;
	uint16_t pixel_count = *(uint16_t*)data;

	while (pixel_count) {
		int16_t sx = x_off + *(int16_t*)ptr;
		int16_t sy = y_off + *(int16_t*)(ptr + 2);
		uint8_t* rle = ptr + 4;
		uint8_t* dst = (uint8_t*)draw_buff_gbl + sy * draw_w_gbl + sx;

		int16_t count = pixel_count >> 1;
		int is_compressed = pixel_count & 1;

		if (is_compressed) {
			int16_t rem = count;
			while (rem > 0) {
				uint8_t ctrl = *rle++;
				int len = ctrl >> 1;
				if (ctrl & 1) {
					rle++; /* skip source byte — we fill with color */
				} else {
					rle += len;
				}
				memset(dst, color, len);
				dst += len;
				rem -= len;
			}
		} else {
			memset(dst, color, count);
			rle += count;
		}

		pixel_count = *(uint16_t*)rle;
		ptr = rle + 2;
	}
}

void lcolor_Delta_Color_Clip(uint8_t* data, int16_t x_off, int16_t y_off, int16_t clip_left, int16_t clip_top,
							 int16_t clip_right, int16_t clip_bottom, uint8_t color) {
	uint8_t* line_buf = (uint8_t*)buff_gbl;
	uint8_t* ptr = data + 2;
	uint16_t pixel_count = *(uint16_t*)data;

	while (pixel_count) {
		int16_t line_x = x_off + *(int16_t*)ptr;
		int16_t data_y = *(int16_t*)(ptr + 2);
		int16_t screen_y = y_off + data_y;
		uint8_t* rle = ptr + 4;
		uint8_t* line_dst = &line_buf[line_x];

		int16_t count = pixel_count >> 1;
		int is_compressed = pixel_count & 1;

		if (is_compressed) {
			int16_t rem = count;
			while (rem > 0) {
				uint8_t ctrl = *rle++;
				int len = ctrl >> 1;
				if (ctrl & 1) {
					rle++;
				} else {
					rle += len;
				}
				memset(line_dst, color, len);
				line_dst += len;
				rem -= len;
			}
		} else {
			memset(&line_buf[line_x], color, count);
			rle += count;
		}

		/* ORIGINAL_BUG: clip range [line_x - count + 1, line_x] does not match
		   fill range [line_x, line_x + count - 1]. Latent — clipped path is
		   never reached in normal gameplay. Preserved to match binary. */
		if (screen_y >= clip_top && screen_y <= clip_bottom) {
			int16_t start_x = line_x - count + 1;
			int16_t end_x = line_x + 1;
			int16_t copy_w = count;

			if (start_x < clip_left) {
				copy_w = line_x + 1 - clip_left;
				start_x = clip_left;
			}
			if (end_x > clip_right)
				copy_w -= end_x - clip_right;

			if (copy_w > 0) {
				uint8_t* screen_dst = (uint8_t*)draw_buff_gbl + screen_y * draw_w_gbl + start_x;
				uint8_t* src = &line_buf[start_x];
				for (int16_t i = 0; i < copy_w; i++) {
					if (src[i])
						screen_dst[i] = src[i];
				}
			}
		}

		pixel_count = *(uint16_t*)rle;
		ptr = rle + 2;
	}
}

/* Dead code — 0 xrefs in the binary */
void lcolor_Copy_Transparent(uint8_t* dst, const uint8_t* src, int count) {
	for (int i = 0; i < count; i++) {
		if (src[i])
			dst[i] = src[i];
	}
}
