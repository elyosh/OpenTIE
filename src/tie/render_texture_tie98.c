#include "tie/render_texture_tie98.h"
#include "tie/render_scene_tie98.h"
#include "tie/rtsvga2.h"
#include "tie_runtime/runtime/pointer_key.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

enum {
	RENDER_TEXTURE_MAX_PIXELS = 65536,
	HARDWARE_SHADE_TABLE_ENTRIES = (16 * 256),
};

// GLOBAL: TIE98 0x5FE8A0
const void* g_renderTextureCacheKeys[RENDER_TEXTURE_CACHE_SIZE];
// GLOBAL: TIE98 0x5FF8A0
static Std3DTextureSurface g_renderTextureCache[RENDER_TEXTURE_CACHE_SIZE];
// GLOBAL: TIE98 0x5601C4
int g_renderTextureCacheCursor = -1;
// GLOBAL: TIE98 0x5702D8
static uint8_t g_renderTextureColorKeyScratch[RENDER_TEXTURE_MAX_PIXELS];
// GLOBAL: TIE98 0x560250
static uint8_t g_renderTextureDecodeScratch[RENDER_TEXTURE_MAX_PIXELS];

// GLOBAL: TIE98 0x5FD35C
uint8_t* g_inversePaletteTable;

// GLOBAL: TIE98 0x5971A0
uint16_t g_flightTextPalette[256];
// GLOBAL: TIE98 0x4F2A80
uint8_t g_flightColorKeyIndex = 0xFB;

// FUNCTION: TIE98 0x47AEC0
void Color_BuildRgb565ToPaletteIndexTable(uint8_t* dst, unsigned int first_index, unsigned int end_index) {
	unsigned int value;

	for (value = 0; value < 0x10000; ++value) {
		const uint8_t rgb6[3] = {
			(uint8_t)(2 * ((value >> 11) & 0x1F)),
			(uint8_t)((value >> 5) & 0x3F),
			(uint8_t)(2 * (value & 0x1F)),
		};
		dst[value] = (uint8_t)rtsvga2_findNearestColor(rgb6, rtsvga2_vgapalette, first_index, end_index);
	}
}

// FUNCTION: TIE98 0x42DAE0
static void RenderTexture_AnalyzeIlluminationShades(uint16_t* shades) {
	int color;

	/* Rewrites shade level 0 into the self-illumination overlay palette:
	 * entries that are dark at the darkest level or that shade normally
	 * across levels become 0 (transparent in the overlay); entries that stay
	 * constant through levels 0-6 (lit windows, engine glows) take their
	 * level-10 color. shades[256] receives the first zeroed index (the
	 * texel-0 remap slot RenderTexture_GetOrCreateColorKey saves through)
	 * and shades[2304] the overlay-enable flag RenderScene_DrawMeshFaces
	 * tests as palette[256].
	 * PORT: the original runs the analysis only when the foption.cfg
	 * texture-detail option is 2 and stores flag 0 otherwise; the host
	 * always uses the high-detail path. */
	int first_zeroed = -1;
	int zeroed_count = 0;
	for (color = 0; color < 256; ++color) {
		uint16_t* entry = &shades[color];
		const int red = (*entry >> 11) & 0x1F;
		const int green = (*entry >> 6) & 0x1F;
		const int blue = *entry & 0x1F;
		int constant_levels = 1;
		if (blue * blue + green * green + red * red >= 0x20) {
			const uint16_t* other = entry + 256;
			while (constant_levels < 7) {
				const int delta_blue = (*other & 0x1F) - blue;
				const int delta_green = ((*other >> 6) & 0x1F) - green;
				const int delta_red = ((*other >> 11) & 0x1F) - red;
				if (delta_blue * delta_blue + delta_green * delta_green + delta_red * delta_red > 16)
					break;
				other += 256;
				++constant_levels;
			}
			if (constant_levels == 7) {
				*entry = entry[2560];
				continue;
			}
		}
		*entry = 0;
		++zeroed_count;
		if (first_zeroed == -1)
			first_zeroed = color;
	}
	shades[256] = (uint16_t)first_zeroed;
	shades[2304] = (uint16_t)(zeroed_count >= 256 ? 0 : zeroed_count);
}

// FUNCTION: TIE98 0x437EF0
void RenderTexture_BuildHardwareShadeTables(uint16_t* shades) {
	int chunk, i;

	/* Illumination analysis first, then the brightness option is baked into
	 * all 16 levels (including the analyzed level 0 and the metadata words,
	 * matching the original's transform order). At the default brightness
	 * the unpack/repack round-trip is exact.
	 * PORT: only hardware-mode callers reach this cache, so the original's
	 * g_useHardware3D gate around the analysis is implicit. The original
	 * converts all 4096 entries in one call; the recovered
	 * rtsvga2_applyBrightness16_tie98 holds 1024, so convert per level pair. */
	uint8_t rgb6[3 * 1024];

	RenderTexture_AnalyzeIlluminationShades(shades);
	for (chunk = 0; chunk < HARDWARE_SHADE_TABLE_ENTRIES; chunk += 1024) {
		for (i = 0; i < 1024; ++i) {
			const uint16_t value = shades[chunk + i];
			rgb6[3 * i] = (uint8_t)(2 * (value >> 11));
			rgb6[3 * i + 1] = (uint8_t)((value >> 5) & 0x3F);
			rgb6[3 * i + 2] = (uint8_t)(2 * (value & 0x1F));
		}
		rtsvga2_applyBrightness16_tie98(rgb6, shades + chunk, 0, 1024);
	}
}

// FUNCTION: TIE98 0x427250
Std3DTextureSurface* RenderTexture_FindOrAllocateCacheEntry(const void* cache_key) {
	int slot, count, index;

	if (g_renderTextureCacheCursor == -1) {
		memset(g_renderTextureCacheKeys, 0, sizeof g_renderTextureCacheKeys);
		for (index = 0; index < RENDER_TEXTURE_CACHE_SIZE; ++index)
			g_renderTextureCache[index].bCached = 0;
	}

	slot = TiePointerKey_LowBits(cache_key) & (RENDER_TEXTURE_CACHE_SIZE - 1);
	g_renderTextureCacheCursor = slot;
	count = 0;
	while (count < RENDER_TEXTURE_CACHE_SIZE) {
		if (g_renderTextureCacheKeys[slot] == cache_key)
			break;
		slot = (slot + 1) & (RENDER_TEXTURE_CACHE_SIZE - 1);
		++count;
	}
	g_renderTextureCacheCursor = slot;
	if (count != RENDER_TEXTURE_CACHE_SIZE) {
		g_renderTextureCacheKeys[slot] = cache_key;
		return &g_renderTextureCache[slot];
	}

	slot = TiePointerKey_LowBits(cache_key) & (RENDER_TEXTURE_CACHE_SIZE - 1);
	g_renderTextureCacheCursor = slot;
	count = 0;
	while (count < RENDER_TEXTURE_CACHE_SIZE) {
		if (!g_renderTextureCache[slot].bCached)
			break;
		slot = (slot + 1) & (RENDER_TEXTURE_CACHE_SIZE - 1);
		++count;
	}
	g_renderTextureCacheCursor = slot;
	if (count == RENDER_TEXTURE_CACHE_SIZE)
		return &g_renderTextureCache[g_renderTextureCacheCursor];
	g_renderTextureCacheKeys[slot] = cache_key;
	return &g_renderTextureCache[slot];
}

// FUNCTION: TIE98 0x4276D0
Std3DTextureSurface* RenderTexture_GetOrCreateOpaque(int width, int height, const uint16_t* palette,
													 const uint8_t* pixels) {
	Std3DVBuffer source;

	Std3DTextureSurface* surface = RenderTexture_FindOrAllocateCacheEntry(pixels);
	if (surface->bCached) {
		std3D_CacheTextureSurface(surface);
		return surface;
	}

	memset(&source, 0, sizeof source);
	source.storageType = 0;
	source.raster.sourceType = 0;
	source.pixels = (void*)pixels;
	source.raster.width = (uint32_t)width;
	source.raster.height = (uint32_t)height;
	source.raster.rowPitch = (uint32_t)width;
	source.raster.bitsPerPixel = 8;
	std3D_CopyPaletteToScratch16(palette, 256);
	return std3D_CreateMipSurface(&source, surface, 0, 0) ? surface : NULL;
}

// FUNCTION: TIE98 0x427340
Std3DTextureSurface* RenderTexture_GetOrCreateBitmap(int width, int height, uint16_t* palette,
													 const uint8_t* pixels, int rle_format) {
	Std3DTextureSurface* surface;
	const uint8_t* input;
	uint8_t* output;
	uint8_t base_color;
	unsigned int max_color;
	int row;
	Std3DVBuffer source;
	int saved_alpha_texture, created;

	static const uint8_t run_length_masks[9] = { 0, 1, 3, 7, 15, 31, 63, 127, 255 };
	static const uint8_t color_shifts[9] = { 0, 1, 2, 3, 4, 5, 6, 7, 8 };
	if (width * height > RENDER_TEXTURE_MAX_PIXELS)
		return NULL;

	surface = RenderTexture_FindOrAllocateCacheEntry(pixels);
	if (surface->bCached) {
		std3D_CacheTextureSurface(surface);
		return surface;
	}

	input = pixels;
	output = g_renderTextureDecodeScratch;
	base_color = 0;
	max_color = 0;
	row = 0;
	while (row < height && *input != 0xff) {
		uint8_t* row_end = output + width;
		int x = 0;
		while (*input != 0xfe) {
			const uint8_t opcode = *input;
			uint8_t run_length, color;
			if (opcode == 0xfb) {
				base_color = input[1];
				input += 3;
				continue;
			}

			if (opcode == 0xfc) {
				run_length = input[1] + 1;
				color = 0;
				input += 2;
			} else if (opcode == 0xfd) {
				run_length = input[1] + 1;
				color = input[2];
				input += 3;
			} else {
				run_length = (opcode & run_length_masks[rle_format]) + 1;
				color = base_color + (opcode >> color_shifts[rle_format]);
				++input;
			}

			if (color > max_color)
				max_color = color;
			if (x < width) {
				if (x + run_length > width)
					run_length = (uint8_t)(width - x);
				memset(output, color, run_length);
				output += run_length;
				x += run_length;
			}
		}
		++input;
		if (output < row_end) {
			memset(output, 0, (size_t)(row_end - output));
			output = row_end;
		}
		++row;
	}
	if (row < height)
		memset(output, 0, (size_t)width * (height - row));

	memset(&source, 0, sizeof source);
	source.storageType = 0;
	source.pixels = g_renderTextureDecodeScratch;
	source.raster.width = (uint32_t)width;
	source.raster.height = (uint32_t)height;
	source.raster.rowPitch = (uint32_t)width;
	source.raster.sourceType = 0;
	source.raster.bitsPerPixel = 8;

	saved_alpha_texture = g_pStd3DCurDevice->caps.bAlphaTexture;
	if (g_pStd3DCurDevice->caps.bColorKeyTexture)
		g_pStd3DCurDevice->caps.bAlphaTexture = 0;
	palette[0] = g_flightTextPalette[g_flightColorKeyIndex];
	if (g_pStd3DCurDevice->caps.bAlphaTexture)
		std3D_ConvertTexTo1555(palette, (int)max_color + 1);
	else
		std3D_CopyPaletteToScratch16(palette, (int)max_color + 1);
	created = std3D_CreateMipSurface(&source, surface, 1, 0);
	if (g_pStd3DCurDevice->caps.bColorKeyTexture)
		g_pStd3DCurDevice->caps.bAlphaTexture = saved_alpha_texture;
	return created ? surface : NULL;
}

// FUNCTION: TIE98 0x4277A0
Std3DTextureSurface* RenderTexture_GetOrCreateColorKey(int width, int height, uint16_t* palette,
													   const uint8_t* pixels) {
	int replacement, has_color_key, pixel_count, i;
	Std3DVBuffer source;
	int saved_alpha_texture, created;

	Std3DTextureSurface* surface = RenderTexture_FindOrAllocateCacheEntry(pixels + 1);
	if (surface->bCached) {
		std3D_CacheTextureSurface(surface);
		return surface;
	}

	replacement = palette[256];
	has_color_key = 0;
	pixel_count = width * height;
	for (i = 0; i < pixel_count; ++i) {
		const uint8_t source = pixels[i];
		if (palette[source] != 0) {
			has_color_key = 1;
			g_renderTextureColorKeyScratch[i] = source ? source : (uint8_t)replacement;
		} else {
			g_renderTextureColorKeyScratch[i] = 0;
		}
	}
	if (!has_color_key)
		return NULL;

	memset(&source, 0, sizeof source);
	source.storageType = 0;
	source.pixels = g_renderTextureColorKeyScratch;
	source.raster.width = (uint32_t)width;
	source.raster.height = (uint32_t)height;
	source.raster.rowPitch = (uint32_t)width;
	source.raster.sourceType = 0;
	source.raster.bitsPerPixel = 8;

	saved_alpha_texture = g_pStd3DCurDevice->caps.bAlphaTexture;
	if (g_pStd3DCurDevice->caps.bColorKeyTexture)
		g_pStd3DCurDevice->caps.bAlphaTexture = 0;
	palette[replacement] = palette[0];
	palette[0] = g_flightTextPalette[g_flightColorKeyIndex];
	if (g_pStd3DCurDevice->caps.bAlphaTexture)
		std3D_ConvertTexTo1555(palette, 256);
	else
		std3D_CopyPaletteToScratch16(palette, 256);
	palette[0] = palette[replacement];
	palette[replacement] = 0;
	created = std3D_CreateMipSurface(&source, surface, 1, 0);
	if (g_pStd3DCurDevice->caps.bColorKeyTexture)
		g_pStd3DCurDevice->caps.bAlphaTexture = saved_alpha_texture;
	return created ? surface : NULL;
}
