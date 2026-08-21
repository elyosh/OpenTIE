#ifndef LANDRU_VIDEO_H
#define LANDRU_VIDEO_H

#include <stdint.h>

void VIDEO_Blit_Indexed_Rect(uint8_t* dst, int dst_stride, int dst_height, const uint8_t* src, int width,
							 int height, int src_stride, int dst_x, int dst_y, int flags);

#endif
