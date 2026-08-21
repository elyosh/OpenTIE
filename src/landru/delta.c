#include <string.h>

#include <landru/canvas.h>
#include <landru/delta.h>

/* Resource bytes are packed and not naturally aligned: a raw line with odd
 * pixel count leaves the next header on an odd byte. Read 16-bit fields via
 * memcpy to avoid UB; the compiler folds it to a single load. */
static inline uint16_t read_u16_le(const uint8_t* p) {
	uint16_t v;
	memcpy(&v, p, sizeof(v));
	return v;
}

static inline int16_t read_i16_le(const uint8_t* p) {
	int16_t v;
	memcpy(&v, p, sizeof(v));
	return v;
}

/*
 * Delta image record format (shared with XCOLOR, XFLIP):
 *   u16 pixel_count  (0 = end; bit 0 = RLE flag)
 *   u16 x_offset     (column relative to actor origin)
 *   u16 y_offset     (row relative to actor origin)
 *   [pixel data]     (RLE or raw, pixel_count >> 1 bytes)
 *
 * RLE control byte: bit 0 = run(1) or literal(0), bits 1-7 = count.
 */

// GLOBAL: TIE 0xD3370
static int16_t delta_clip_x_base;
// GLOBAL: TIE 0xD3372
static int16_t delta_clip_y_base;

/* Decode one RLE line, copying pixel data to dst. Returns updated src pointer. */
static uint8_t* decode_rle_to(uint8_t* src, uint8_t* dst, int count) {
	while (count > 0) {
		uint8_t ctrl = *src++;
		int len = ctrl >> 1;
		if (ctrl & 1) {
			uint8_t fill = *src++;
			memset(dst, fill, len);
		} else {
			memcpy(dst, src, len);
			src += len;
		}
		dst += len;
		count -= len;
	}
	return src;
}

/* Skip one RLE line without writing. Returns updated src pointer. */
static uint8_t* skip_rle(uint8_t* src, int count) {
	while (count > 0) {
		uint8_t ctrl = *src++;
		int len = ctrl >> 1;
		if (ctrl & 1)
			src++; /* run: skip 1 fill byte */
		else
			src += len; /* literal: skip len bytes */
		count -= len;
	}
	return src;
}

void ldelta_Delta_Image(int16_t x_off, int16_t y_off, uint8_t* data) {
	uint8_t* base = (uint8_t*)draw_buff_gbl + x_off + y_off * draw_w_gbl;

	while (read_u16_le(data)) {
		uint16_t pixel_count = read_u16_le(data);
		int16_t rec_x = read_i16_le(data + 2);
		int16_t rec_y = read_i16_le(data + 4);
		data += 6;

		uint8_t* dst = base + rec_x + rec_y * draw_w_gbl;
		int count = pixel_count >> 1;

		if (pixel_count & 1) {
			data = decode_rle_to(data, dst, count);
		} else {
			memcpy(dst, data, count);
			data += count;
		}
	}
}

/*
 * Skip lines above clip_top. Advances *pp_data past skipped headers and
 * pixel data. Returns screen_y of the first visible line. Sets *out_count
 * and *out_x for that line. Returns 0 with *out_count=0 if data exhausted.
 */
static int16_t skip_to_clip_top(uint8_t** pp_data, uint16_t* out_count, int16_t* out_x) {
	uint8_t* data = *pp_data;

	while (1) {
		uint16_t pixel_count = read_u16_le(data);
		data += 2;
		if (!pixel_count) {
			*out_count = 0;
			*pp_data = data - 2;
			return 0;
		}

		int16_t x_off = read_i16_le(data);
		data += 2;
		int16_t y_off = read_i16_le(data);
		data += 2;

		int16_t screen_y = delta_clip_y_base + y_off;
		int16_t x_start = delta_clip_x_base + x_off;

		if (screen_y >= clip_top_gbl) {
			*out_count = pixel_count;
			*out_x = x_start;
			*pp_data = data;
			return screen_y;
		}

		/* Skip pixel data for this line */
		int count = pixel_count >> 1;
		if (pixel_count & 1)
			data = skip_rle(data, count);
		else
			data += count;
	}
}

void ldelta_Delta_Clip(int16_t x_base, int16_t y_base, uint8_t* data) {
	delta_clip_x_base = x_base;
	delta_clip_y_base = y_base;

	uint16_t pixel_count;
	int16_t x_start;
	int16_t screen_y = skip_to_clip_top(&data, &pixel_count, &x_start);
	if (!pixel_count)
		return;

	while (screen_y < clip_bottom_gbl) {
		int count = pixel_count >> 1;
		int skip_left = 0;

		if (x_start < clip_left_gbl) {
			skip_left = clip_left_gbl - x_start;
			x_start = clip_left_gbl;
		}

		uint8_t* dst = (uint8_t*)draw_buff_gbl + draw_w_gbl * screen_y + x_start;

		if (pixel_count & 1) {
			/* Compressed: decode to buff_gbl, then clip-copy to screen */
			if (clip_right_gbl <= x_start || count <= skip_left) {
				data = skip_rle(data, count);
			} else {
				int visible = count - skip_left;
				if (visible > clip_right_gbl - x_start)
					visible = clip_right_gbl - x_start;

				data = decode_rle_to(data, (uint8_t*)buff_gbl, count);
				memcpy(dst, (uint8_t*)buff_gbl + skip_left, visible);
			}
		} else {
			/* Uncompressed: direct copy with left/right clipping */
			if (clip_right_gbl <= x_start || count <= skip_left) {
				data += count;
			} else {
				uint8_t* src = data + skip_left;
				int visible = count - skip_left;
				int right_skip = 0;
				if (visible > clip_right_gbl - x_start) {
					right_skip = visible - (clip_right_gbl - x_start);
					visible = clip_right_gbl - x_start;
				}
				memcpy(dst, src, visible);
				data = src + visible + right_skip;
			}
		}

		/* Read next line header */
		pixel_count = read_u16_le(data);
		if (!pixel_count)
			break;
		x_start = delta_clip_x_base + read_i16_le(data + 2);
		screen_y = delta_clip_y_base + read_i16_le(data + 4);
		data += 6;
	}
}
