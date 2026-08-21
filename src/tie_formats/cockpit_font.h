#ifndef TIE_FORMATS_COCKPIT_FONT_H
#define TIE_FORMATS_COCKPIT_FONT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "tie_formats/common.h"

typedef struct TieCockpitFontGlyph {
	uint16_t atlas_x, atlas_y, atlas_w, atlas_h, advance;
} TieCockpitFontGlyph;

typedef struct TieCockpitFontAtlas {
	uint8_t* rgba;
	TieCockpitFontGlyph* glyphs;
	uint16_t width, height;
	uint16_t first_char, glyph_count;
	uint16_t cell_w, cell_h, baseline;
} TieCockpitFontAtlas;

bool TieCockpitFont_Decode(const void* bytes, size_t size, uint8_t row_bytes, TieCockpitFontAtlas* out,
						   TieFormatError* error);
void TieCockpitFont_Free(TieCockpitFontAtlas* atlas);

#endif
