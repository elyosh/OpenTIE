#ifndef TIE_FORMATS_COCKPIT_H
#define TIE_FORMATS_COCKPIT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "tie_formats/common.h"
#include "tie_formats/shape.h"

typedef struct TieCockpitCoverageRect {
	int16_t x, y, w, h;
} TieCockpitCoverageRect;

typedef struct TieCockpitCoverage {
	TieCockpitCoverageRect* rects;
	uint32_t count;
	uint16_t image_width, image_height;
} TieCockpitCoverage;

typedef struct TieCockpitPartInstrument {
	int32_t x, y;
	uint16_t param1, param2;
} TieCockpitPartInstrument;

bool TieCockpitPalette_Build(const void* vga_pac, size_t vga_pac_size, const void* pltt, size_t pltt_size,
							 uint8_t out_palette[768], TieFormatError* error);
bool TieCockpitMask_Apply(const void* mask, size_t mask_size, int viewport_x, int viewport_y,
						  int viewport_width, int viewport_height, int image_width, int image_height,
						  uint8_t* rgba, int* out_complete_rows, TieFormatError* error);
bool TieCockpitBase_Build(const void* panl, size_t panl_size, const void* mask, size_t mask_size,
						  const void* pltt, size_t pltt_size, const void* vga_pac, size_t vga_pac_size,
						  int viewport_x, int viewport_y, int viewport_width, int viewport_height,
						  TieRgbaFrame* out, uint8_t out_palette[768], int* out_complete_rows,
						  TieFormatError* error);
bool TieCockpitCoverage_Build(const uint8_t* rgba, int width, int height, TieCockpitCoverage* out,
							  TieFormatError* error);
void TieCockpitCoverage_Free(TieCockpitCoverage* coverage);
bool TieCockpitPartTransparency_Build(const TieCockpitPartInstrument* instruments, uint32_t instrument_count,
									  uint32_t frame_count, uint16_t* out_transparent_indices,
									  TieFormatError* error);
bool TieCockpitShapeFrames_Build(const TieShapeList* shapes, const uint8_t palette[768],
								 const uint16_t* transparent_indices, uint16_t transparent_index_alt,
								 TieRgbaFrames* out, TieFormatError* error);
bool TieCockpitCrtMask_Decode(const void* bytes, size_t size, uint8_t** out_rgba, int* out_width,
							  int* out_height, TieFormatError* error);

#endif
