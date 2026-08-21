#ifndef LANDRU_PAINT_H
#define LANDRU_PAINT_H

#include <stdbool.h>
#include <stdint.h>

#include <landru/rect.h>

/* Toggle paint-command render capture without affecting actual
 * raster into draw_buff_gbl. Text capture is unaffected. */
void lpaint_Set_Emit_Suppressed(bool on);

/* Suppress render capture for raster-only thickness padding. */
void lpaint_Set_Thickness_Duplicate(bool on);

int16_t lpaint_Plot_Clipped_Pixel(int16_t x, int16_t y, int16_t color);
int16_t lpaint_Horiz_Clipped_Line(int16_t x, int16_t y, int16_t length, int16_t color);
int16_t lpaint_Vert_Clipped_Line(int16_t x, int16_t y, int16_t length, int16_t color);
int16_t lpaint_Paint_Clipped_Rect(Rect* r, int16_t color);
int16_t lpaint_Frame_Clipped_Rect(Rect* r, int16_t color);
int16_t lpaint_Paint_Clipped_Bevel(Rect* r, int16_t shadow, int16_t highlight, int16_t fill, int16_t pressed);
int16_t lpaint_Frame_Clipped_Bevel(Rect* r, int16_t shadow, int16_t highlight, int16_t fill, int16_t pressed);
int16_t lpaint_Paint_Clipped_DBevel(Rect* r, int16_t outer_shadow, int16_t inner_shadow,
									int16_t outer_highlight, int16_t inner_highlight, int16_t fill,
									int16_t pressed);
int16_t lpaint_Frame_Clipped_DBevel(Rect* r, int16_t outer_shadow, int16_t inner_shadow,
									int16_t outer_highlight, int16_t inner_highlight, int16_t fill,
									int16_t pressed);
int16_t lpaint_XOR_Clipped_Rect(Rect* r, int16_t color);

#endif
