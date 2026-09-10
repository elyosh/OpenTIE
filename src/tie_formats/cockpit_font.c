#include "tie_formats/cockpit_font.h"
#include "aeron/asset/bitmap_font.h"
#include "tie_formats/internal.h"

#include <stdlib.h>
#include <string.h>

void TieCockpitFont_Free(TieCockpitFontAtlas* atlas) {
	if (!atlas)
		return;
	free(atlas->rgba);
	free(atlas->glyphs);
	memset(atlas, 0, sizeof *atlas);
}

bool TieCockpitFont_Decode(const void* bytes, size_t size, uint8_t row_bytes, TieCockpitFontAtlas* out,
						   TieFormatError* error) {
	if (!out)
		return TieFormat_SetError(error, 80, "invalid cockpit font output");
	memset(out, 0, sizeof *out);
	AeronDecodedFont font = { 0 };
	if (!AeronBitmapFont_Decode(bytes, size, row_bytes, 0x20, &font, error))
		return false;
	if (font.glyph_count > 224) {
		AeronDecodedFont_Free(&font);
		return TieFormat_SetError(error, 83, "invalid cockpit font glyph count");
	}
	const size_t pixels = (size_t)font.width * font.height;
	out->rgba = calloc(pixels, 4);
	out->glyphs = calloc(font.glyph_count, sizeof *out->glyphs);
	if (!out->rgba || !out->glyphs) {
		AeronDecodedFont_Free(&font);
		TieCockpitFont_Free(out);
		return TieFormat_SetError(error, 87, "cockpit font allocation failed");
	}
	/* TIE applies its existing shadow policy at draw time. */
	for (size_t i = 0; i < pixels; ++i)
		if (font.foreground[i])
			memset(out->rgba + i * 4, 255, 4);
	for (uint16_t i = 0; i < font.glyph_count; ++i) {
		const AeronDecodedGlyph* glyph = &font.glyphs[i];
		out->glyphs[i] =
			(TieCockpitFontGlyph) { glyph->x, glyph->y, glyph->width, glyph->height, glyph->advance };
	}
	out->width = font.width;
	out->height = font.height;
	out->first_char = font.first_char;
	out->glyph_count = font.glyph_count;
	out->cell_w = font.cell_width;
	out->cell_h = font.cell_height;
	out->baseline = font.baseline;
	AeronDecodedFont_Free(&font);
	return true;
}
