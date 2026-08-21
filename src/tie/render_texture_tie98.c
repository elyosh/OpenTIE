#include "tie/render_texture_tie98.h"

#include "tie/render_scene_tie98.h"
#include "tie/rtsvga2.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define RENDER_TEXTURE_CACHE_SIZE 1024
#define RENDER_TEXTURE_MAX_PIXELS 65536
#define SOFTWARE_SHADE_TABLE_CACHE_SIZE 1024
#define SOFTWARE_SHADE_TABLE_SIZE 4096
#define HARDWARE_SHADE_TABLE_CACHE_SIZE 1024
#define HARDWARE_SHADE_TABLE_ENTRIES (16 * 256)

typedef struct SoftwareShadeTableCacheEntry {
	const uint16_t* rgb565_shades;
	uint8_t palette_indices[SOFTWARE_SHADE_TABLE_SIZE];
} SoftwareShadeTableCacheEntry;

typedef struct HardwareShadeTableCacheEntry {
	const uint16_t* rgb565_shades;
	uint16_t shades[HARDWARE_SHADE_TABLE_ENTRIES];
} HardwareShadeTableCacheEntry;

// GLOBAL: TIE98 0x5FE8A0
static uintptr_t g_renderTextureCacheKeys[RENDER_TEXTURE_CACHE_SIZE];
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

/* PORT: the original runtime OPT builder stores one converted 8-bit shade
 * table inside each mutable model handle. Native host OPT images remain
 * immutable, so the equivalent tables are retained by source address here. */
static SoftwareShadeTableCacheEntry g_softwareShadeTableCache[SOFTWARE_SHADE_TABLE_CACHE_SIZE];

/* PORT: hardware-mode counterpart of the software cache above. The original
 * builder copies each texture's 16 RGB565 shade palettes into the mutable
 * handle, runs the illumination analysis, and bakes the brightness option
 * into the copy; the rebuilt tables are retained by source address here. */
static HardwareShadeTableCacheEntry g_hardwareShadeTableCache[HARDWARE_SHADE_TABLE_CACHE_SIZE];

// GLOBAL: TIE98 0x5971A0
uint16_t g_flightTextPalette[256];
// GLOBAL: TIE98 0x4F2A80
uint8_t g_flightColorKeyIndex = 0xFB;

// PORT: TIE98 maintains the 16-bit table when its DirectDraw palette changes. The
// portable host exposes the active flight palette as 6-bit RGB instead.
void RenderTexture_SyncFlightPalette(void) {
	unsigned int best_distance = UINT32_MAX;
	for (unsigned int index = 0; index < 256; ++index) {
		const uint8_t* rgb = &rtsvga2_vgapalette[3 * index];
		const unsigned int red8 = rgb[0] * 255u / 63u;
		const unsigned int green8 = rgb[1] * 255u / 63u;
		const unsigned int blue8 = rgb[2] * 255u / 63u;
		g_flightTextPalette[index] = (uint16_t)(((red8 >> 3) << 11) | ((green8 >> 2) << 5) | (blue8 >> 3));
		const int blue_delta = (int)rgb[2] - 2;
		const unsigned int distance =
			rgb[0] * rgb[0] + rgb[1] * rgb[1] + (unsigned int)(blue_delta * blue_delta);
		if (distance < best_distance) {
			best_distance = distance;
			g_flightColorKeyIndex = (uint8_t)index;
		}
	}
}

// FUNCTION: TIE98 0x47AEC0
void Color_BuildRgb565ToPaletteIndexTable(uint8_t* dst, unsigned int first_index, unsigned int end_index) {
	for (unsigned int value = 0; value < 0x10000; ++value) {
		const uint8_t rgb6[3] = {
			(uint8_t)(2 * ((value >> 11) & 0x1F)),
			(uint8_t)((value >> 5) & 0x3F),
			(uint8_t)(2 * (value & 0x1F)),
		};
		dst[value] = (uint8_t)rtsvga2_findNearestColor(rgb6, rtsvga2_vgapalette, first_index, end_index);
	}
}

/* PORT: supplies the representation produced by the original
 * OptModel_BuildRuntimeHandle software branch without modifying the host-owned
 * serialized OPT image. */
const uint8_t* RenderTexture_GetSoftwareShadeTable(const uint16_t* rgb565_shades) {
	unsigned int slot = ((uintptr_t)rgb565_shades >> 4) & (SOFTWARE_SHADE_TABLE_CACHE_SIZE - 1);
	for (unsigned int count = 0; count < SOFTWARE_SHADE_TABLE_CACHE_SIZE; ++count) {
		SoftwareShadeTableCacheEntry* entry = &g_softwareShadeTableCache[slot];
		if (entry->rgb565_shades == rgb565_shades)
			return entry->palette_indices;
		if (entry->rgb565_shades == NULL) {
			entry->rgb565_shades = rgb565_shades;
			for (int index = 0; index < SOFTWARE_SHADE_TABLE_SIZE; ++index)
				entry->palette_indices[index] = g_inversePaletteTable[rgb565_shades[index]];
			return entry->palette_indices;
		}
		slot = (slot + 1) & (SOFTWARE_SHADE_TABLE_CACHE_SIZE - 1);
	}

	SoftwareShadeTableCacheEntry* entry = &g_softwareShadeTableCache[slot];
	entry->rgb565_shades = rgb565_shades;
	for (int index = 0; index < SOFTWARE_SHADE_TABLE_SIZE; ++index)
		entry->palette_indices[index] = g_inversePaletteTable[rgb565_shades[index]];
	return entry->palette_indices;
}

/* PORT: changing the active indexed destination palette invalidates the
 * converted tables embedded in the original runtime OPT handles. */
void RenderTexture_ResetSoftwareShadeTableCache(void) {
	for (int index = 0; index < SOFTWARE_SHADE_TABLE_CACHE_SIZE; ++index)
		g_softwareShadeTableCache[index].rgb565_shades = NULL;
}

void RenderTexture_ReleaseMissionCaches(void) {
	if (g_useHardware3D && g_pStd3DCurDevice)
		std3D_FlushTextureCache();
	memset(g_renderTextureCacheKeys, 0, sizeof g_renderTextureCacheKeys);
	g_renderTextureCacheCursor = -1;
	RenderTexture_ResetSoftwareShadeTableCache();
	for (int index = 0; index < HARDWARE_SHADE_TABLE_CACHE_SIZE; ++index)
		g_hardwareShadeTableCache[index].rgb565_shades = NULL;
}

// FUNCTION: TIE98 0x42DAE0
static void RenderTexture_AnalyzeIlluminationShades(uint16_t* shades) {
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
	for (int color = 0; color < 256; ++color) {
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
static void RenderTexture_BuildHardwareShadeTables(uint16_t* shades) {
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
	for (int chunk = 0; chunk < HARDWARE_SHADE_TABLE_ENTRIES; chunk += 1024) {
		for (int i = 0; i < 1024; ++i) {
			const uint16_t value = shades[chunk + i];
			rgb6[3 * i] = (uint8_t)(2 * (value >> 11));
			rgb6[3 * i + 1] = (uint8_t)((value >> 5) & 0x3F);
			rgb6[3 * i + 2] = (uint8_t)(2 * (value & 0x1F));
		}
		rtsvga2_applyBrightness16_tie98(rgb6, shades + chunk, 0, 1024);
	}
}

/* PORT: supplies the representation produced by the original
 * OptModel_BuildRuntimeHandle hardware branch without modifying the
 * host-owned serialized OPT image. The returned tables are mutable: the
 * renderer clears the overlay flag for projectile models and
 * RenderTexture_GetOrCreateColorKey saves palette[0] through the remap slot,
 * exactly as the original mutated its handle. */
uint16_t* RenderTexture_GetHardwareShadeTables(const uint16_t* rgb565_shades) {
	unsigned int slot = ((uintptr_t)rgb565_shades >> 4) & (HARDWARE_SHADE_TABLE_CACHE_SIZE - 1);
	for (unsigned int count = 0; count < HARDWARE_SHADE_TABLE_CACHE_SIZE; ++count) {
		HardwareShadeTableCacheEntry* entry = &g_hardwareShadeTableCache[slot];
		if (entry->rgb565_shades == rgb565_shades)
			return entry->shades;
		if (entry->rgb565_shades == NULL)
			break;
		slot = (slot + 1) & (HARDWARE_SHADE_TABLE_CACHE_SIZE - 1);
	}

	HardwareShadeTableCacheEntry* entry = &g_hardwareShadeTableCache[slot];
	entry->rgb565_shades = rgb565_shades;
	memcpy(entry->shades, rgb565_shades, sizeof entry->shades);
	RenderTexture_BuildHardwareShadeTables(entry->shades);
	return entry->shades;
}

// FUNCTION: TIE98 0x427250
Std3DTextureSurface* RenderTexture_FindOrAllocateCacheEntry(const void* cache_key) {
	if (g_renderTextureCacheCursor == -1) {
		memset(g_renderTextureCacheKeys, 0, sizeof g_renderTextureCacheKeys);
		for (int index = 0; index < RENDER_TEXTURE_CACHE_SIZE; ++index)
			g_renderTextureCache[index].bCached = 0;
	}

	const uintptr_t key = (uintptr_t)cache_key;
	int slot = (int)(key & (RENDER_TEXTURE_CACHE_SIZE - 1));
	g_renderTextureCacheCursor = slot;
	int count = 0;
	while (count < RENDER_TEXTURE_CACHE_SIZE) {
		if (g_renderTextureCacheKeys[slot] == key)
			break;
		slot = (slot + 1) & (RENDER_TEXTURE_CACHE_SIZE - 1);
		++count;
	}
	g_renderTextureCacheCursor = slot;
	if (count != RENDER_TEXTURE_CACHE_SIZE) {
		g_renderTextureCacheKeys[slot] = key;
		return &g_renderTextureCache[slot];
	}

	slot = (int)(key & (RENDER_TEXTURE_CACHE_SIZE - 1));
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
	g_renderTextureCacheKeys[slot] = key;
	return &g_renderTextureCache[slot];
}

// FUNCTION: TIE98 0x4276D0
Std3DTextureSurface* RenderTexture_GetOrCreateOpaque(int width, int height, const uint16_t* palette,
													 const uint8_t* pixels) {
	Std3DTextureSurface* surface = RenderTexture_FindOrAllocateCacheEntry(pixels);
	if (surface->bCached) {
		std3D_CacheTextureSurface(surface);
		return surface;
	}

	Std3DVBuffer source;
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
	static const uint8_t run_length_masks[9] = { 0, 1, 3, 7, 15, 31, 63, 127, 255 };
	static const uint8_t color_shifts[9] = { 0, 1, 2, 3, 4, 5, 6, 7, 8 };
	if (width * height > RENDER_TEXTURE_MAX_PIXELS)
		return NULL;

	Std3DTextureSurface* surface = RenderTexture_FindOrAllocateCacheEntry(pixels);
	if (surface->bCached) {
		std3D_CacheTextureSurface(surface);
		return surface;
	}

	const uint8_t* input = pixels;
	uint8_t* output = g_renderTextureDecodeScratch;
	uint8_t base_color = 0;
	unsigned int max_color = 0;
	int row = 0;
	while (row < height && *input != 0xff) {
		uint8_t* row_end = output + width;
		int x = 0;
		while (*input != 0xfe) {
			const uint8_t opcode = *input;
			if (opcode == 0xfb) {
				base_color = input[1];
				input += 3;
				continue;
			}

			uint8_t run_length;
			uint8_t color;
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

	Std3DVBuffer source;
	memset(&source, 0, sizeof source);
	source.storageType = 0;
	source.pixels = g_renderTextureDecodeScratch;
	source.raster.width = (uint32_t)width;
	source.raster.height = (uint32_t)height;
	source.raster.rowPitch = (uint32_t)width;
	source.raster.sourceType = 0;
	source.raster.bitsPerPixel = 8;

	const int saved_alpha_texture = g_pStd3DCurDevice->caps.bAlphaTexture;
	if (g_pStd3DCurDevice->caps.bColorKeyTexture)
		g_pStd3DCurDevice->caps.bAlphaTexture = 0;
	palette[0] = g_flightTextPalette[g_flightColorKeyIndex];
	if (g_pStd3DCurDevice->caps.bAlphaTexture)
		std3D_ConvertTexTo1555(palette, (int)max_color + 1);
	else
		std3D_CopyPaletteToScratch16(palette, (int)max_color + 1);
	const int created = std3D_CreateMipSurface(&source, surface, 1, 0);
	if (g_pStd3DCurDevice->caps.bColorKeyTexture)
		g_pStd3DCurDevice->caps.bAlphaTexture = saved_alpha_texture;
	return created ? surface : NULL;
}

// FUNCTION: TIE98 0x4277A0
Std3DTextureSurface* RenderTexture_GetOrCreateColorKey(int width, int height, uint16_t* palette,
													   const uint8_t* pixels) {
	Std3DTextureSurface* surface = RenderTexture_FindOrAllocateCacheEntry(pixels + 1);
	if (surface->bCached) {
		std3D_CacheTextureSurface(surface);
		return surface;
	}

	const int replacement = palette[256];
	int has_color_key = 0;
	const int pixel_count = width * height;
	for (int i = 0; i < pixel_count; ++i) {
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

	Std3DVBuffer source;
	memset(&source, 0, sizeof source);
	source.storageType = 0;
	source.pixels = g_renderTextureColorKeyScratch;
	source.raster.width = (uint32_t)width;
	source.raster.height = (uint32_t)height;
	source.raster.rowPitch = (uint32_t)width;
	source.raster.sourceType = 0;
	source.raster.bitsPerPixel = 8;

	const int saved_alpha_texture = g_pStd3DCurDevice->caps.bAlphaTexture;
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
	const int created = std3D_CreateMipSurface(&source, surface, 1, 0);
	if (g_pStd3DCurDevice->caps.bColorKeyTexture)
		g_pStd3DCurDevice->caps.bAlphaTexture = saved_alpha_texture;
	return created ? surface : NULL;
}
