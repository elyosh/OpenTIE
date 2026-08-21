#ifndef TIE_FORMATS_SHAPE_H
#define TIE_FORMATS_SHAPE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "tie_formats/common.h"

typedef struct TieShape {
	const uint8_t* data;
	size_t size;
} TieShape;

typedef struct TieShapeList {
	TieShape* shapes;
	uint32_t count;
} TieShapeList;

bool TieShapeList_Parse(const void* bytes, size_t size, uint32_t declared_count, TieShapeList* out,
						TieFormatError* error);
void TieShapeList_Free(TieShapeList* list);
bool TieShape_Measure(const void* bytes, size_t size, int* out_width, int* out_height, size_t* out_consumed,
					  TieFormatError* error);
bool TieShape_RasterizeRgba8(const void* bytes, size_t size, int width, int height,
							 const uint8_t palette[768], uint16_t transparent_index,
							 uint16_t transparent_index_alt, uint8_t* out_rgba, TieFormatError* error);

#endif
