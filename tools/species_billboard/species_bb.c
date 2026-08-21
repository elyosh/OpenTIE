#include "species_bb.h"

#include "tie_formats/xact.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void TieSpeciesBillboard_SetError(char* error, size_t capacity, const char* message) {
	if (error && capacity)
		snprintf(error, capacity, "%s", message);
}

static int TieSpeciesBillboard_PaletteIndex(TieSpeciesBillboardPalette* palette, const uint8_t rgba[4]) {
	for (int index = 0; index < palette->count; ++index)
		if (memcmp(palette->rgb[index], rgba, 3) == 0)
			return index;
	if (palette->count >= 64)
		return -1;
	const int index = palette->count++;
	memcpy(palette->rgb[index], rgba, 3);
	return index;
}

void TieSpeciesBillboard_Free(AnimImage* frames) {
	if (!frames)
		return;
	for (int index = 0; index < frames->count; ++index)
		free(frames->frames[index].pixels);
	free(frames->frames);
	memset(frames, 0, sizeof *frames);
}

bool TieSpeciesBillboard_Decode(AnimImage* out, TieSpeciesBillboardPalette** palettes_out,
								const uint8_t* data, uint32_t size, char* error, size_t capacity) {
	if (!out || !palettes_out)
		return false;
	memset(out, 0, sizeof *out);
	*palettes_out = NULL;
	TieRgbaFrames decoded = { 0 };
	TieFormatError codec_error = { 0 };
	if (!TieXact_DecodeRgba8(data, size, &decoded, &codec_error)) {
		TieSpeciesBillboard_SetError(error, capacity, codec_error.message);
		return false;
	}
	out->frames = calloc(decoded.count, sizeof *out->frames);
	TieSpeciesBillboardPalette* palettes = calloc(decoded.count, sizeof *palettes);
	if (!out->frames || !palettes) {
		free(out->frames);
		free(palettes);
		TieRgbaFrames_Free(&decoded);
		memset(out, 0, sizeof *out);
		TieSpeciesBillboard_SetError(error, capacity, "out of memory");
		return false;
	}
	out->count = decoded.count;
	for (uint16_t frame_index = 0; frame_index < decoded.count; ++frame_index) {
		const TieRgbaFrame* source = &decoded.frames[frame_index];
		Image8* image = &out->frames[frame_index];
		image->width = source->width;
		image->height = source->height;
		image->left = source->anchor_x;
		image->top = source->anchor_y;
		const size_t pixel_count = (size_t)source->width * source->height;
		image->pixels = malloc(pixel_count);
		if (!image->pixels)
			goto failed;
		for (size_t pixel = 0; pixel < pixel_count; ++pixel) {
			const uint8_t* rgba = source->rgba + pixel * 4;
			if (rgba[3] == 0) {
				image->pixels[pixel] = SPECIES_BB_TRANSPARENT;
				continue;
			}
			const int index = TieSpeciesBillboard_PaletteIndex(&palettes[frame_index], rgba);
			if (index < 0) {
				TieSpeciesBillboard_SetError(error, capacity, "decoded XACT frame uses more than 64 colors");
				goto failed;
			}
			image->pixels[pixel] = (uint8_t)index;
		}
	}
	TieRgbaFrames_Free(&decoded);
	*palettes_out = palettes;
	return true;

failed:
	TieRgbaFrames_Free(&decoded);
	TieSpeciesBillboard_Free(out);
	free(palettes);
	return false;
}
