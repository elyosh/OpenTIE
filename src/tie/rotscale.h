#ifndef __ROTSCALE_H__
#define __ROTSCALE_H__

#include <stdint.h>

/* Rotated and scaled RLE sprite renderer used by bitmaps and reticles. */

/* Module-public globals */
extern uint16_t reverseflag;   /* 1 = horizontal-flip the sprite */
extern uint16_t bSquarePixels; /* derived from yAspect == 0 */

/* --- public API --------------------------------------------------- */

int16_t rotscale_calcscale(int32_t depth, uint16_t bound_hwidth, uint16_t factor);

void rotscale_prepare_fastdraw(uint16_t angle);

/* Force a line_data rebuild on the next preparefastdraw. tie_simulator
 * calls this on mission start to drop any stale cache from a previous
 * flight. Mirrors the binary's `dword_C787C = 0` write. */
void rotscale_invalidate_linedata(void);

extern int rotscale_linedata_built;

void rotscale_prepare_color(const char* palette_entries);

int16_t rotscale_rotate_scale_image(int16_t screen_x, int16_t screen_y, uint16_t scale,
									const uint8_t* image_hdr);

#endif
