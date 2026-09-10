#include "tie_formats/shape.h"
#include "aeron/asset/pnl.h"
#include "tie_formats/internal.h"

#include <stdlib.h>
#include <string.h>

void TieShapeList_Free(TieShapeList* list) {
	if (!list)
		return;
	AeronPnlList decoded = { list->shapes, list->count };
	AeronPnl_Free(&decoded);
	memset(list, 0, sizeof *list);
}

bool TieShapeList_Parse(const void* bytes, size_t size, uint32_t declared_count, TieShapeList* out,
						TieFormatError* error) {
	AeronPnlList decoded = { 0 };
	if (out)
		memset(out, 0, sizeof *out);
	if (!AeronPnl_Parse(bytes, size, declared_count, out ? &decoded : NULL, error))
		return false;
	out->shapes = decoded.bitmaps;
	out->count = decoded.count;
	return true;
}

bool TieShape_Measure(const void* bytes, size_t size, int* width, int* height, size_t* consumed,
					  TieFormatError* error) {
	return AeronPnl_Measure(bytes, size, width, height, consumed, error);
}

bool TieShape_RasterizeRgba8(const void* bytes, size_t size, int width, int height,
							 const uint8_t palette[768], uint16_t skip, uint16_t skip_alt, uint8_t* rgba,
							 TieFormatError* error) {
	if (!palette || !rgba || width <= 0 || height <= 0 || width > 4096 || height > 4096)
		return TieFormat_SetError(error, 44, "invalid shape raster arguments");
	AeronPnlBitmap decoded = { 0 };
	if (!AeronPnl_Decode(bytes, size, &decoded, error))
		return false;
	memset(rgba, 0, (size_t)width * height * 4);
	for (int y = 0; y < height && y < decoded.height; ++y) {
		for (int x = 0; x < width && x < decoded.width; ++x) {
			const size_t source = (size_t)y * decoded.width + x;
			const uint8_t index = decoded.indices[source];
			if (!decoded.coverage[source] || index == skip || index == skip_alt)
				continue;
			uint8_t* pixel = rgba + ((size_t)y * width + x) * 4;
			memcpy(pixel, palette + (size_t)index * 3, 3);
			pixel[3] = decoded.coverage[source];
		}
	}
	AeronIndexedFrame_Free(&decoded);
	return true;
}
