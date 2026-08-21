#ifndef __SHADE_H__
#define __SHADE_H__

#include "landru/rect.h"
#include <stdint.h>

void shade_Build_Shaded_Palette(void);
void shade_Set_Shaded_Palette(uint8_t* pal_data, int16_t intensity, int16_t target_r, int16_t target_g,
							  uint16_t target_b);
void shade_Find_Shade_Cycles(uint8_t* mask);
void shade_Draw_Talk_Shade_Rect(Rect* r);
void shade_Shadow_Line_List(const uint8_t* palette, int16_t x, int16_t y, int16_t width, int16_t height);

/* SHADE global */
extern uint8_t shade_palette[256];

#endif
