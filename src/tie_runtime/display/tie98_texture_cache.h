#ifndef TIE_RUNTIME_DISPLAY_TIE98_TEXTURE_CACHE_H
#define TIE_RUNTIME_DISPLAY_TIE98_TEXTURE_CACHE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Port-owned TIE98 renderer caches. The original OPT runtime-handle builder
 * converts shade tables inside each mutable model handle and DirectDraw keeps
 * the 16-bit text palette current; host OPT images are immutable, so the
 * equivalent representations are retained here by source address. */
void RenderTexture_SyncFlightPalette(void);
const uint8_t* RenderTexture_GetSoftwareShadeTable(const uint16_t* rgb565_shades);
void RenderTexture_ResetSoftwareShadeTableCache(void);
uint16_t* RenderTexture_GetHardwareShadeTables(const uint16_t* rgb565_shades);
void RenderTexture_ReleaseMissionCaches(void);

#ifdef __cplusplus
}
#endif

#endif
