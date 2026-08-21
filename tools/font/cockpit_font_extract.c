#include "imgbake/png_write.h"
#include "imgbake/upscale.h"
#include "tie_formats/cockpit_font.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FNT_MAGIC 0x544E4654u
#define FNT_VERSION 2

typedef enum TieCockpitFontExtractVariant { FONT_SVGA, FONT_VGA } TieCockpitFontExtractVariant;

static void TieCockpitFontExtract_Die(const char* message) {
	fprintf(stderr, "cockpit_font_extract: %s\n", message);
	exit(1);
}

static uint8_t* TieCockpitFontExtract_ReadFile(const char* path, size_t* out_size) {
	FILE* file = fopen(path, "rb");
	if (!file)
		return NULL;
	if (fseek(file, 0, SEEK_END) != 0) {
		fclose(file);
		return NULL;
	}
	const long length = ftell(file);
	if (length < 0 || fseek(file, 0, SEEK_SET) != 0) {
		fclose(file);
		return NULL;
	}
	uint8_t* bytes = malloc((size_t)length ? (size_t)length : 1);
	if (!bytes || fread(bytes, 1, (size_t)length, file) != (size_t)length) {
		free(bytes);
		fclose(file);
		return NULL;
	}
	fclose(file);
	*out_size = (size_t)length;
	return bytes;
}

static int TieCockpitFontExtract_ScaleX(int value, TieCockpitFontExtractVariant variant) {
	return variant == FONT_VGA ? value * SCALE_X_4K : scale_svga_xy_to_4k(value);
}

static int TieCockpitFontExtract_ScaleY(int value, TieCockpitFontExtractVariant variant) {
	return variant == FONT_VGA ? scale_y_4k(value) : scale_svga_xy_to_4k(value);
}

static bool TieCockpitFontExtract_Upscale(uint8_t** rgba, int* width, int* height,
										  TieCockpitFontExtractVariant variant) {
	return variant == FONT_VGA ? atlas_vga_to_4k(rgba, width, height) : atlas_svga_to_4k(rgba, width, height);
}

static void TieCockpitFontExtract_WriteU16(FILE* file, uint16_t value) {
	if (fwrite(&value, sizeof value, 1, file) != 1)
		TieCockpitFontExtract_Die("metrics write failed");
}

static void TieCockpitFontExtract_WriteMetrics(const char* path, const TieCockpitFontAtlas* font,
											   TieCockpitFontExtractVariant variant, int atlas_width,
											   int atlas_height) {
	if (atlas_width > UINT16_MAX || atlas_height > UINT16_MAX)
		TieCockpitFontExtract_Die("atlas exceeds 16-bit metrics");
	FILE* file = fopen(path, "wb");
	if (!file)
		TieCockpitFontExtract_Die("cannot create metrics file");
	uint8_t header[24] = { 0 };
	const uint32_t magic = FNT_MAGIC;
	memcpy(header, &magic, sizeof magic);
	uint16_t value = FNT_VERSION;
	memcpy(header + 4, &value, 2);
	value = font->first_char;
	memcpy(header + 6, &value, 2);
	value = font->glyph_count;
	memcpy(header + 8, &value, 2);
	value = (uint16_t)atlas_width;
	memcpy(header + 10, &value, 2);
	value = (uint16_t)atlas_height;
	memcpy(header + 12, &value, 2);
	value = (uint16_t)TieCockpitFontExtract_ScaleX(font->cell_w, variant);
	memcpy(header + 14, &value, 2);
	value = (uint16_t)TieCockpitFontExtract_ScaleY(font->cell_h, variant);
	memcpy(header + 16, &value, 2);
	value = (uint16_t)TieCockpitFontExtract_ScaleY(font->baseline, variant);
	memcpy(header + 18, &value, 2);
	if (fwrite(header, 1, sizeof header, file) != sizeof header)
		TieCockpitFontExtract_Die("metrics header write failed");
	for (uint16_t index = 0; index < font->glyph_count; ++index) {
		const TieCockpitFontGlyph* glyph = &font->glyphs[index];
		const int bottom = TieCockpitFontExtract_ScaleY(glyph->atlas_y + glyph->atlas_h, variant);
		const int top = TieCockpitFontExtract_ScaleY(glyph->atlas_y, variant);
		TieCockpitFontExtract_WriteU16(file, (uint16_t)TieCockpitFontExtract_ScaleX(glyph->atlas_x, variant));
		TieCockpitFontExtract_WriteU16(file, (uint16_t)top);
		TieCockpitFontExtract_WriteU16(file, (uint16_t)TieCockpitFontExtract_ScaleX(glyph->atlas_w, variant));
		TieCockpitFontExtract_WriteU16(file, (uint16_t)(bottom - top));
		TieCockpitFontExtract_WriteU16(file, (uint16_t)TieCockpitFontExtract_ScaleX(glyph->advance, variant));
	}
	fclose(file);
}

int main(int argc, char** argv) {
	if (argc != 4) {
		fprintf(stderr, "usage: %s <font.fnt> <out_basename> {vga|svga}\n", argv[0]);
		return 2;
	}
	TieCockpitFontExtractVariant variant = FONT_SVGA;
	if (strcmp(argv[3], "vga") == 0)
		variant = FONT_VGA;
	else if (strcmp(argv[3], "svga") == 0)
		variant = FONT_SVGA;
	else
		TieCockpitFontExtract_Die("variant must be 'vga' or 'svga'");

	size_t size = 0;
	uint8_t* bytes = TieCockpitFontExtract_ReadFile(argv[1], &size);
	if (!bytes)
		TieCockpitFontExtract_Die("cannot read input font");
	TieCockpitFontAtlas decoded = { 0 };
	TieFormatError codec_error = { 0 };
	if (!TieCockpitFont_Decode(bytes, size, variant == FONT_VGA ? 1 : 4, &decoded, &codec_error)) {
		free(bytes);
		TieCockpitFontExtract_Die(codec_error.message);
	}
	free(bytes);

	uint8_t* rgba = decoded.rgba;
	decoded.rgba = NULL;
	int atlas_width = decoded.width;
	int atlas_height = decoded.height;
	if (!TieCockpitFontExtract_Upscale(&rgba, &atlas_width, &atlas_height, variant)) {
		TieCockpitFont_Free(&decoded);
		TieCockpitFontExtract_Die("upscale failed");
	}
	for (size_t index = 0; index < (size_t)atlas_width * atlas_height; ++index) {
		uint8_t* pixel = rgba + index * 4;
		pixel[0] = pixel[1] = pixel[2] = pixel[3];
	}
	char path[1024];
	if (snprintf(path, sizeof path, "%s.png", argv[2]) >= (int)sizeof path ||
		!write_png_rgba(path, atlas_width, atlas_height, rgba)) {
		free(rgba);
		TieCockpitFont_Free(&decoded);
		TieCockpitFontExtract_Die("PNG write failed");
	}
	free(rgba);
	if (snprintf(path, sizeof path, "%s.fnt", argv[2]) >= (int)sizeof path) {
		TieCockpitFont_Free(&decoded);
		TieCockpitFontExtract_Die("output path is too long");
	}
	TieCockpitFontExtract_WriteMetrics(path, &decoded, variant, atlas_width, atlas_height);
	fprintf(stderr, "cockpit_font_extract: decoded %u glyphs with shared codec\n", decoded.glyph_count);
	TieCockpitFont_Free(&decoded);
	return 0;
}
