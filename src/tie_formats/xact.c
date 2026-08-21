#include "tie_formats/xact.h"
#include "tie_formats/internal.h"

#include <stdlib.h>
#include <string.h>

static const uint8_t TieXact_RunMask[16] = {
	0x00, 0x01, 0x03, 0x07, 0x0f, 0x1f, 0x3f, 0x7f, 0xff, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
};
static const uint8_t TieXact_RunShift[16] = {
	0, 1, 2, 3, 4, 5, 6, 7, 8, 0, 1, 2, 3, 4, 5, 6,
};

static bool TieXact_RangeValid(size_t size, uint32_t offset, size_t bytes) {
	return offset <= size && bytes <= size - offset;
}

static bool TieXact_DecodeFrame(const uint8_t* blob, size_t size, uint32_t sub_offset, uint16_t stable_id,
								TieRgbaFrame* out, TieFormatError* error) {
	if (!TieXact_RangeValid(size, sub_offset, 44))
		return TieFormat_SetError(error, 60, "XACT frame %u header is truncated", stable_id);
	const uint32_t rgb_relative = TieFormat_ReadU32Le(blob + sub_offset + 4);
	const uint32_t rle_relative = TieFormat_ReadU32Le(blob + sub_offset + 8);
	const uint32_t width = TieFormat_ReadU32Le(blob + sub_offset + 16);
	const uint32_t height = TieFormat_ReadU32Le(blob + sub_offset + 20);
	const uint32_t split = TieFormat_ReadU32Le(blob + sub_offset + 32);
	const uint32_t palette_count = TieFormat_ReadU32Le(blob + sub_offset + 40);
	if (!width || !height || width > 4096 || height > 4096 || !palette_count || palette_count > 64)
		return TieFormat_SetError(error, 61, "XACT frame %u dimensions or palette are invalid", stable_id);
	if (rgb_relative > UINT32_MAX - sub_offset || rle_relative > UINT32_MAX - sub_offset)
		return TieFormat_SetError(error, 62, "XACT frame %u offset overflows", stable_id);
	const uint32_t rgb_offset = sub_offset + rgb_relative;
	const uint32_t rle_offset = sub_offset + rle_relative;
	if (!TieXact_RangeValid(size, rgb_offset, (size_t)palette_count * 4) ||
		!TieXact_RangeValid(size, rle_offset, 16))
		return TieFormat_SetError(error, 63, "XACT frame %u data is truncated", stable_id);
	if ((size_t)width > SIZE_MAX / height || (size_t)width * height > SIZE_MAX / 4)
		return TieFormat_SetError(error, 64, "XACT frame %u allocation overflows", stable_id);
	uint8_t* rgba = calloc((size_t)width * height, 4);
	if (!rgba)
		return TieFormat_SetError(error, 65, "XACT frame allocation failed");
	out->rgba = rgba;
	out->width = (uint16_t)width;
	out->height = (uint16_t)height;
	out->anchor_x = TieFormat_ReadI16Le(blob + rle_offset);
	out->anchor_y = (int16_t)-TieFormat_ReadI16Le(blob + rle_offset + 4);
	out->stable_id = stable_id;
	const uint8_t mask = TieXact_RunMask[split & 15];
	const uint8_t shift = TieXact_RunShift[split & 15];
	size_t cursor = (size_t)rle_offset + 16;
	for (uint32_t y = 0; y < height; ++y) {
		if (cursor >= size)
			goto truncated;
		if (blob[cursor] == 0xff)
			return true;
		uint8_t base = 0;
		uint32_t x = 0;
		bool terminated = false;
		while (cursor < size) {
			const uint8_t op = blob[cursor++];
			if (op == 0xfe) {
				terminated = true;
				break;
			}
			if (op == 0xfb) {
				if (size - cursor < 2)
					goto truncated;
				base = blob[cursor];
				cursor += 2;
				continue;
			}
			uint32_t count;
			uint8_t color;
			bool transparent = false;
			if (op == 0xfc) {
				if (cursor == size)
					goto truncated;
				count = (uint32_t)blob[cursor++] + 1;
				color = 0;
				transparent = true;
			} else if (op == 0xfd) {
				if (size - cursor < 2)
					goto truncated;
				count = (uint32_t)blob[cursor++] + 1;
				color = blob[cursor++];
			} else {
				count = (uint32_t)(op & mask) + 1;
				color = (uint8_t)(base + (op >> shift));
			}
			if (count > width - x)
				goto invalid_run;
			if (!transparent) {
				if (color >= palette_count)
					goto invalid_color;
				const uint8_t* rgb = blob + rgb_offset + (size_t)color * 4;
				for (uint32_t pixel = 0; pixel < count; ++pixel) {
					uint8_t* destination = rgba + ((size_t)y * width + x + pixel) * 4;
					destination[0] = rgb[0];
					destination[1] = rgb[1];
					destination[2] = rgb[2];
					destination[3] = 255;
				}
			}
			x += count;
		}
		if (!terminated)
			goto truncated;
	}
	return true;

truncated:
	free(out->rgba);
	memset(out, 0, sizeof *out);
	return TieFormat_SetError(error, 66, "XACT frame %u RLE is truncated", stable_id);
invalid_run:
	free(out->rgba);
	memset(out, 0, sizeof *out);
	return TieFormat_SetError(error, 67, "XACT frame %u run exceeds its row", stable_id);
invalid_color:
	free(out->rgba);
	memset(out, 0, sizeof *out);
	return TieFormat_SetError(error, 68, "XACT frame %u palette index is invalid", stable_id);
}

bool TieXact_DecodeRgba8(const void* data, size_t size, TieRgbaFrames* out, TieFormatError* error) {
	const uint8_t* blob = data;
	if (out)
		memset(out, 0, sizeof *out);
	if (!blob || !out || size < 32 || size > TIE_FORMAT_MAX_ENTRY_SIZE)
		return TieFormat_SetError(error, 69, "invalid XACT payload size");
	const uint32_t table = TieFormat_ReadU32Le(blob + 16);
	const uint32_t count = TieFormat_ReadU32Le(blob + 24);
	if (!count || count > 256 || !TieXact_RangeValid(size, table, (size_t)count * 4))
		return TieFormat_SetError(error, 70, "invalid XACT frame table");
	TieRgbaFrame* frames = calloc(count, sizeof *frames);
	if (!frames)
		return TieFormat_SetError(error, 71, "XACT frame allocation failed");
	out->frames = frames;
	out->count = (uint16_t)count;
	for (uint32_t index = 0; index < count; ++index) {
		const uint32_t sub_offset = TieFormat_ReadU32Le(blob + table + index * 4);
		if (!TieXact_DecodeFrame(blob, size, sub_offset, (uint16_t)index, &frames[index], error)) {
			TieRgbaFrames_Free(out);
			return false;
		}
	}
	return true;
}
