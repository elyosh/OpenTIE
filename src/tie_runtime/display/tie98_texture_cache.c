#include "tie_runtime/display/tie98_texture_cache.h"
#include "tie/render_scene_tie98.h"
#include "tie/render_texture_tie98.h"
#include "tie/rtsvga2.h"

#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

enum {
	SOFTWARE_SHADE_TABLE_CACHE_SIZE = 1024,
	SOFTWARE_SHADE_TABLE_SIZE = 4096,
	HARDWARE_SHADE_TABLE_CACHE_SIZE = 1024,
	HARDWARE_SHADE_TABLE_ENTRIES = (16 * 256),
};

typedef struct SoftwareShadeTableCacheEntry {
	const uint16_t* rgb565_shades;
	uint8_t palette_indices[SOFTWARE_SHADE_TABLE_SIZE];
} SoftwareShadeTableCacheEntry;

typedef struct HardwareShadeTableCacheEntry {
	const uint16_t* rgb565_shades;
	uint16_t shades[HARDWARE_SHADE_TABLE_ENTRIES];
} HardwareShadeTableCacheEntry;

/* PORT: the original runtime OPT builder stores one converted 8-bit shade
 * table inside each mutable model handle. Native host OPT images remain
 * immutable, so the equivalent tables are retained by source address here. */
static SoftwareShadeTableCacheEntry s_softwareShadeTableCache[SOFTWARE_SHADE_TABLE_CACHE_SIZE];

/* PORT: hardware-mode counterpart of the software cache above. The original
 * builder copies each texture's 16 RGB565 shade palettes into the mutable
 * handle, runs the illumination analysis, and bakes the brightness option
 * into the copy; the rebuilt tables are retained by source address here. */
static HardwareShadeTableCacheEntry s_hardwareShadeTableCache[HARDWARE_SHADE_TABLE_CACHE_SIZE];

// PORT: TIE98 maintains the 16-bit table when its DirectDraw palette changes. The
// portable host exposes the active flight palette as 6-bit RGB instead.
void RenderTexture_SyncFlightPalette(void) {
	unsigned int index;

	unsigned int best_distance = UINT32_MAX;
	for (index = 0; index < 256; ++index) {
		const uint8_t* rgb = &rtsvga2_vgapalette[3 * index];
		const unsigned int red8 = rgb[0] * 255u / 63u;
		const unsigned int green8 = rgb[1] * 255u / 63u;
		const unsigned int blue8 = rgb[2] * 255u / 63u;
		int blue_delta;
		unsigned int distance;
		g_flightTextPalette[index] = (uint16_t)(((red8 >> 3) << 11) | ((green8 >> 2) << 5) | (blue8 >> 3));
		blue_delta = (int)rgb[2] - 2;
		distance = rgb[0] * rgb[0] + rgb[1] * rgb[1] + (unsigned int)(blue_delta * blue_delta);
		if (distance < best_distance) {
			best_distance = distance;
			g_flightColorKeyIndex = (uint8_t)index;
		}
	}
}

/* PORT: supplies the representation produced by the original
 * OptModel_BuildRuntimeHandle software branch without modifying the host-owned
 * serialized OPT image. */
const uint8_t* RenderTexture_GetSoftwareShadeTable(const uint16_t* rgb565_shades) {
	unsigned int count;
	int index;
	SoftwareShadeTableCacheEntry* entry;

	unsigned int slot = ((uintptr_t)rgb565_shades >> 4) & (SOFTWARE_SHADE_TABLE_CACHE_SIZE - 1);
	for (count = 0; count < SOFTWARE_SHADE_TABLE_CACHE_SIZE; ++count) {
		entry = &s_softwareShadeTableCache[slot];
		if (entry->rgb565_shades == rgb565_shades)
			return entry->palette_indices;
		if (entry->rgb565_shades == NULL) {
			entry->rgb565_shades = rgb565_shades;
			for (index = 0; index < SOFTWARE_SHADE_TABLE_SIZE; ++index)
				entry->palette_indices[index] = g_inversePaletteTable[rgb565_shades[index]];
			return entry->palette_indices;
		}
		slot = (slot + 1) & (SOFTWARE_SHADE_TABLE_CACHE_SIZE - 1);
	}

	entry = &s_softwareShadeTableCache[slot];
	entry->rgb565_shades = rgb565_shades;
	for (index = 0; index < SOFTWARE_SHADE_TABLE_SIZE; ++index)
		entry->palette_indices[index] = g_inversePaletteTable[rgb565_shades[index]];
	return entry->palette_indices;
}

/* PORT: changing the active indexed destination palette invalidates the
 * converted tables embedded in the original runtime OPT handles. */
void RenderTexture_ResetSoftwareShadeTableCache(void) {
	int index;

	for (index = 0; index < SOFTWARE_SHADE_TABLE_CACHE_SIZE; ++index)
		s_softwareShadeTableCache[index].rgb565_shades = NULL;
}

void RenderTexture_ReleaseMissionCaches(void) {
	int index;

	if (g_useHardware3D && g_pStd3DCurDevice)
		std3D_FlushTextureCache();
	memset(g_renderTextureCacheKeys, 0, sizeof g_renderTextureCacheKeys);
	g_renderTextureCacheCursor = -1;
	RenderTexture_ResetSoftwareShadeTableCache();
	for (index = 0; index < HARDWARE_SHADE_TABLE_CACHE_SIZE; ++index)
		s_hardwareShadeTableCache[index].rgb565_shades = NULL;
}

/* PORT: supplies the representation produced by the original
 * OptModel_BuildRuntimeHandle hardware branch without modifying the
 * host-owned serialized OPT image. The returned tables are mutable: the
 * renderer clears the overlay flag for projectile models and
 * RenderTexture_GetOrCreateColorKey saves palette[0] through the remap slot,
 * exactly as the original mutated its handle. */
uint16_t* RenderTexture_GetHardwareShadeTables(const uint16_t* rgb565_shades) {
	unsigned int count;
	HardwareShadeTableCacheEntry* entry;

	unsigned int slot = ((uintptr_t)rgb565_shades >> 4) & (HARDWARE_SHADE_TABLE_CACHE_SIZE - 1);
	for (count = 0; count < HARDWARE_SHADE_TABLE_CACHE_SIZE; ++count) {
		entry = &s_hardwareShadeTableCache[slot];
		if (entry->rgb565_shades == rgb565_shades)
			return entry->shades;
		if (entry->rgb565_shades == NULL)
			break;
		slot = (slot + 1) & (HARDWARE_SHADE_TABLE_CACHE_SIZE - 1);
	}

	entry = &s_hardwareShadeTableCache[slot];
	entry->rgb565_shades = rgb565_shades;
	memcpy(entry->shades, rgb565_shades, sizeof entry->shades);
	RenderTexture_BuildHardwareShadeTables(entry->shades);
	return entry->shades;
}
