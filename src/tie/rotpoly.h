#ifndef TIE_ROTPOLY_H
#define TIE_ROTPOLY_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Fill dest[0..count-1] with Bresenham-interpolated values from start to end. */
void rotpoly_Build_Ratio(int16_t* dest, int16_t count, int16_t start, int16_t end);

/* Affine texture-map src_data onto the canvas using edge ratio tables.
 * Each table holds 3 arrays of int16_t at byte offsets 0, 400, 800
 * (x coords, u coords, v coords), one entry per scanline.
 * Non-transparent (nonzero) source pixels are copied. */
void rotpoly_Map_Image(void* src_data, const int16_t* left_table, const int16_t* right_table,
					   int16_t src_stride, int16_t map_mode, int16_t num_scanlines, int16_t start_y);

#ifdef __cplusplus
}
#endif

#endif
