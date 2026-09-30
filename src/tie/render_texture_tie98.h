#ifndef TIE_RENDER_TEXTURE_TIE98_H
#define TIE_RENDER_TEXTURE_TIE98_H

#include "tie/std3d_tie98.h"
#include "tie_runtime/display/tie98_texture_cache.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

extern uint16_t g_flightTextPalette[256];
extern uint8_t g_flightColorKeyIndex;
enum { RENDER_TEXTURE_CACHE_SIZE = 1024 };

extern const void* g_renderTextureCacheKeys[RENDER_TEXTURE_CACHE_SIZE];
extern int g_renderTextureCacheCursor;
extern uint8_t* g_inversePaletteTable;

Std3DTextureSurface* RenderTexture_FindOrAllocateCacheEntry(const void* cache_key);
Std3DTextureSurface* RenderTexture_GetOrCreateOpaque(int width, int height, const uint16_t* palette,
													 const uint8_t* pixels);
Std3DTextureSurface* RenderTexture_GetOrCreateColorKey(int width, int height, uint16_t* palette,
													   const uint8_t* pixels);
Std3DTextureSurface* RenderTexture_GetOrCreateBitmap(int width, int height, uint16_t* palette,
													 const uint8_t* pixels, int rle_format);
void Color_BuildRgb565ToPaletteIndexTable(uint8_t* dst, unsigned int first_index, unsigned int end_index);
void RenderTexture_BuildHardwareShadeTables(uint16_t* shades);

#ifdef __cplusplus
}
#endif

#endif
