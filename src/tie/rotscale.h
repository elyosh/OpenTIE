#ifndef TIE_ROTSCALE_H
#define TIE_ROTSCALE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Rotated and scaled RLE sprite renderer used by bitmaps and reticles. */

/* Module-public globals */
extern uint32_t reverseflag;   /* 1 = horizontal-flip the sprite */
extern uint16_t bSquarePixels; /* derived from yAspect == 0 */

/* --- public API --------------------------------------------------- */

int16_t rotscale_calcscale(int32_t depth, uint16_t bound_hwidth, uint16_t factor);

void rotscale_preparefastdraw(uint16_t angle, int mode); /* mode is ignored by retail */

extern int rotscale_linedata_built;

void rotscale_preparecolor(const char* palette_entries);

int16_t rotscale_rotatescaleimage(int16_t screen_x, int16_t screen_y, uint16_t scale,
								  const uint8_t* image_hdr);

#ifdef __cplusplus
}
#endif

#endif
