#include "imgbake/delt.h"

#include "imgbake/byteio.h"

#include <stdlib.h>
#include <string.h>

void image_free(Image8* img) {
	free(img->pixels);
	memset(img, 0, sizeof(*img));
}

static bool TieFilmDelt_DecodeRleLine(const uint8_t* src, size_t src_len, uint8_t* dst, int dst_len,
									  size_t* consumed) {
	size_t s = 0;
	int d = 0;
	while (d < dst_len) {
		if (s >= src_len)
			return false;
		uint8_t ctrl = src[s++];
		int len = ctrl >> 1;
		if (len == 0)
			return false; /* malformed */
		if (d + len > dst_len)
			return false;
		if (ctrl & 1) {
			if (s >= src_len)
				return false;
			memset(dst + d, src[s++], (size_t)len);
		} else {
			if (s + (size_t)len > src_len)
				return false;
			memcpy(dst + d, src + s, (size_t)len);
			s += (size_t)len;
		}
		d += len;
	}
	*consumed = s;
	return true;
}

bool decode_delt(Image8* out, const uint8_t* data, uint32_t size) {
	memset(out, 0, sizeof(*out));
	if (size < 8)
		return false;

	int left = rd_i16(data + 0);
	int top = rd_i16(data + 2);
	int right = rd_i16(data + 4) + 1;
	int bottom = rd_i16(data + 6) + 1;
	int w = right - left;
	int h = bottom - top;
	if (w <= 0 || h <= 0 || w > 8192 || h > 8192)
		return false;

	out->left = left;
	out->top = top;
	out->width = w;
	out->height = h;
	out->pixels = (uint8_t*)calloc((size_t)w * (size_t)h, 1);
	if (!out->pixels)
		return false;

	size_t off = 8;
	while (off + 6 <= size) {
		uint16_t ctrl = rd_u16(data + off);
		off += 2;
		if (ctrl == 0)
			return true;
		int rec_x = rd_i16(data + off);
		off += 2;
		int rec_y = rd_i16(data + off);
		off += 2;

		int count = ctrl >> 1;
		bool rle = (ctrl & 1) != 0;
		int col = rec_x - left;
		int row = rec_y - top;

		if (col < 0 || row < 0 || col + count > w || row >= h) {
			image_free(out);
			return false;
		}
		uint8_t* dst = out->pixels + (size_t)row * (size_t)w + col;

		if (rle) {
			size_t consumed = 0;
			if (off > size)
				goto fail;
			if (!TieFilmDelt_DecodeRleLine(data + off, size - off, dst, count, &consumed))
				goto fail;
			off += consumed;
		} else {
			if (off + (size_t)count > size)
				goto fail;
			memcpy(dst, data + off, (size_t)count);
			off += (size_t)count;
		}
	}
	/* No terminator — accept truncated stream (some retail data has it). */
	return true;
fail:
	image_free(out);
	return false;
}
