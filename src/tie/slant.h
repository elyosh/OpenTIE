#ifndef __SLANT_H__
#define __SLANT_H__

#include <stdint.h>

/* Bresenham-style horizontal scale-line renderer for the title crawl.
 * Reads source pixels from bitmap_data at (src_x, src_y), writes to the
 * current canvas at (dst_x, dst_y). Source advances by (skip+1) per
 * output pixel with fractional accumulator skipf. Non-zero source pixels
 * are written as 'color'; transparent (0) pixels are skipped. */
void slant_Scale_Line(void* bitmap_data, int16_t src_x, int16_t src_y, int16_t skip, int16_t skipf,
					  int16_t dst_x, int16_t dst_y, int16_t width, uint8_t color);

#endif
