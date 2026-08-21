#include "tie_formats/shape.h"
#include "tie_formats/internal.h"

#include <stdlib.h>
#include <string.h>

void TieShapeList_Free(TieShapeList* list) {
	if (!list)
		return;
	free(list->shapes);
	memset(list, 0, sizeof *list);
}

bool TieShapeList_Parse(const void* data, size_t size, uint32_t declared_count, TieShapeList* out,
						TieFormatError* error) {
	const uint8_t* bytes = data;
	if (out)
		memset(out, 0, sizeof *out);
	if (!bytes || !out || !declared_count || declared_count > 4096 || size > TIE_FORMAT_MAX_ENTRY_SIZE)
		return TieFormat_SetError(error, 30, "invalid PNL shape list");
	TieShape* shapes = calloc(declared_count, sizeof *shapes);
	if (!shapes)
		return TieFormat_SetError(error, 31, "PNL allocation failed");
	size_t offset = 0;
	uint32_t count = 0;
	for (uint32_t index = 0; index < declared_count; ++index) {
		const size_t start = offset;
		while (offset < size && bytes[offset] != 0xff)
			++offset;
		if (offset == size) {
			/* Retail turns clean EOF into empty trailing entries. Keeping only
			 * the complete shapes has the same no-draw result on the GPU path. */
			if (start == size)
				break;
			free(shapes);
			return TieFormat_SetError(error, 32, "PNL shape %u has no terminator", index);
		}
		++offset;
		shapes[count].data = bytes + start;
		shapes[count].size = offset - start;
		++count;
	}
	if (!count) {
		free(shapes);
		return TieFormat_SetError(error, 32, "PNL contains no complete shapes");
	}
	out->shapes = shapes;
	out->count = count;
	return true;
}

static bool TieShape_WalkRow(const uint8_t** cursor, const uint8_t* end, int* out_width, bool* truncated) {
	const uint8_t* p = *cursor;
	int width = 0;
	*truncated = false;
	while (p < end) {
		const uint8_t op = *p++;
		if (op <= 0xfa) {
			width += (op & 3) + 1;
			continue;
		}
		if (op == 0xfb) {
			if (p == end) {
				*truncated = true;
				return false;
			}
			++p;
			continue;
		}
		if (op == 0xfc || op == 0xfd) {
			if ((size_t)(end - p) < 2) {
				*truncated = true;
				return false;
			}
			width += (op == 0xfc ? p[1] : p[0]) + 1;
			p += 2;
			continue;
		}
		*out_width = width;
		*cursor = p;
		return op == 0xfe;
	}
	*truncated = true;
	return false;
}

bool TieShape_Measure(const void* data, size_t size, int* out_width, int* out_height, size_t* out_consumed,
					  TieFormatError* error) {
	const uint8_t *bytes = data, *cursor = bytes, *end = bytes + size;
	if (!bytes || !size)
		return TieFormat_SetError(error, 40, "empty shape stream");
	int max_width = 0, rows = 0;
	for (;;) {
		const uint8_t* row_start = cursor;
		int width = 0;
		bool truncated = false;
		const bool more = TieShape_WalkRow(&cursor, end, &width, &truncated);
		if (truncated)
			return TieFormat_SetError(error, 41, "truncated shape at byte %zu", (size_t)(cursor - bytes));
		if (width > max_width)
			max_width = width;
		if (more || cursor - row_start != 1 || width != 0)
			++rows;
		if (!more)
			break;
		if (rows > 4096 || max_width > 4096)
			return TieFormat_SetError(error, 42, "shape dimensions exceed 4096");
	}
	if (max_width <= 0 || rows <= 0)
		return TieFormat_SetError(error, 43, "shape has no pixels");
	if (out_width)
		*out_width = max_width;
	if (out_height)
		*out_height = rows;
	if (out_consumed)
		*out_consumed = (size_t)(cursor - bytes);
	return true;
}

static void TieShape_WritePixel(uint8_t* rgba, int width, int height, int x, int y, const uint8_t* palette,
								uint8_t color, uint16_t skip, uint16_t skip_alt) {
	if (x < 0 || y < 0 || x >= width || y >= height || color == skip || color == skip_alt)
		return;
	uint8_t* pixel = rgba + ((size_t)y * width + x) * 4;
	pixel[0] = palette[(size_t)color * 3];
	pixel[1] = palette[(size_t)color * 3 + 1];
	pixel[2] = palette[(size_t)color * 3 + 2];
	pixel[3] = 255;
}

bool TieShape_RasterizeRgba8(const void* data, size_t size, int width, int height, const uint8_t palette[768],
							 uint16_t skip, uint16_t skip_alt, uint8_t* rgba, TieFormatError* error) {
	const uint8_t *p = data, *end = p + size;
	if (!p || !palette || !rgba || width <= 0 || height <= 0 || width > 4096 || height > 4096)
		return TieFormat_SetError(error, 44, "invalid shape raster arguments");
	memset(rgba, 0, (size_t)width * height * 4);
	int x = 0, y = 0;
	uint8_t base = 0;
	while (p < end) {
		const uint8_t op = *p++;
		if (op <= 0xfa) {
			const uint8_t color = (uint8_t)((op >> 2) + base);
			for (int count = (op & 3) + 1; count--; ++x)
				TieShape_WritePixel(rgba, width, height, x, y, palette, color, skip, skip_alt);
			continue;
		}
		if (op == 0xfb) {
			if (p == end)
				return TieFormat_SetError(error, 45, "truncated shape 0xFB");
			base = *p++;
			continue;
		}
		if (op == 0xfc) {
			if ((size_t)(end - p) < 2)
				return TieFormat_SetError(error, 46, "truncated shape 0xFC");
			const uint8_t color = *p++;
			int remaining = *p++ + 1;
			while (remaining > 0) {
				TieShape_WritePixel(rgba, width, height, x++, y, palette, color, skip, skip_alt);
				--remaining;
				if (remaining > 0) {
					TieShape_WritePixel(rgba, width, height, x++, y, palette, (uint8_t)(color + 1), skip,
										skip_alt);
					--remaining;
				}
			}
			continue;
		}
		if (op == 0xfd) {
			if ((size_t)(end - p) < 2)
				return TieFormat_SetError(error, 47, "truncated shape 0xFD");
			int count = *p++ + 1;
			const uint8_t color = *p++;
			while (count--)
				TieShape_WritePixel(rgba, width, height, x++, y, palette, color, skip, skip_alt);
			continue;
		}
		if (op == 0xff)
			return true;
		if (++y > height)
			return TieFormat_SetError(error, 48, "shape has too many rows");
		x = 0;
	}
	return TieFormat_SetError(error, 49, "shape stream has no terminator");
}
