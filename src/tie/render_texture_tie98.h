#ifndef TIE_RENDER_TEXTURE_TIE98_H
#define TIE_RENDER_TEXTURE_TIE98_H

#include "tie/std3d_tie98.h"

#include <stdint.h>

extern uint16_t g_flightTextPalette[256];
extern uint8_t g_flightColorKeyIndex;
extern int g_renderTextureCacheCursor;
extern uint8_t* g_inversePaletteTable;

Std3DTextureSurface* RenderTexture_FindOrAllocateCacheEntry(const void* cache_key);
Std3DTextureSurface* RenderTexture_GetOrCreateOpaque(int width, int height, const uint16_t* palette,
													 const uint8_t* pixels);
Std3DTextureSurface* RenderTexture_GetOrCreateColorKey(int width, int height, uint16_t* palette,
													   const uint8_t* pixels);
Std3DTextureSurface* RenderTexture_GetOrCreateBitmap(int width, int height, uint16_t* palette,
													 const uint8_t* pixels, int rle_format);
void RenderTexture_SyncFlightPalette(void);
void RenderTexture_ReleaseMissionCaches(void);
void Color_BuildRgb565ToPaletteIndexTable(uint8_t* dst, unsigned int first_index, unsigned int end_index);
const uint8_t* RenderTexture_GetSoftwareShadeTable(const uint16_t* rgb565_shades);
void RenderTexture_ResetSoftwareShadeTableCache(void);
uint16_t* RenderTexture_GetHardwareShadeTables(const uint16_t* rgb565_shades);

#endif
