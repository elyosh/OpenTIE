#ifndef LANDRU_FLIP_H
#define LANDRU_FLIP_H

#include <stdint.h>

void lflip_Delta_Flip(uint8_t* data, int16_t x_off, int16_t y_off, int16_t x_flip_base, int16_t y_flip_base,
					  int16_t flip_x, int16_t flip_y);
void lflip_Delta_Clip_Flip(uint8_t* data, int16_t x_off, int16_t y_off, int16_t x_flip_base,
						   int16_t y_flip_base, int16_t flip_x, int16_t flip_y, int16_t clip_left,
						   int16_t clip_top, int16_t clip_right, int16_t clip_bottom);
void lflip_Copy_Transparent(uint8_t* dst, const uint8_t* src, int count);

#endif
