#ifndef __STUB_H__
#define __STUB_H__

#include <stdint.h>

#include "landru/rect.h"

/* Copy a rectangular region FROM a flat buffer TO the drawing canvas.
 * The buffer is treated as a bitmap of (buf_w × buf_h) pixels.
 * src_rect defines which portion of the buffer to read.
 * (screen_x, screen_y) is the destination position on the canvas.
 * Clips against the current canvas clip rect. */
int stub_Copy_From_Clipped_Buffer(void* buffer, Rect* src_rect, int16_t screen_x, int16_t screen_y,
								  int16_t buf_w, int16_t buf_h);

/* Copy a rectangular region FROM the drawing canvas TO a flat buffer.
 * The buffer is treated as a bitmap of (buf_w × buf_h) pixels.
 * src_rect defines which portion of the buffer to write into.
 * (screen_x, screen_y) is the source position on the canvas.
 * Clips against the current canvas clip rect. */
int stub_Copy_To_Clipped_Buffer(void* buffer, Rect* src_rect, int16_t screen_x, int16_t screen_y,
								int16_t buf_w, int16_t buf_h);

/* Texture-map a source image onto a rotated quadrilateral.
 * Uses ROTPOLY edge interpolation and mapping.
 * src_stride is the source image width in bytes (passed through to Map_Image). */
void stub_Map_Clipped_Image(void* src_data, int16_t* dst_poly, Rect* src_rect, int16_t src_stride,
							int16_t map_mode);

#endif
