#ifndef LANDRU_SCALE_H
#define LANDRU_SCALE_H

#include <stdbool.h>
#include <stdint.h>

bool lscale_Scale_Delta_Clip(uint8_t* data, int16_t x_off, int16_t y_off, int16_t src_w, int16_t src_h,
							 int16_t x_extent, int16_t y_extent, int16_t x_scale, int16_t y_scale,
							 int16_t clip_left, int16_t clip_top, int16_t clip_right, int16_t clip_bottom);

#endif
