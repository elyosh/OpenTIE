#ifndef LANDRU_RECT_H
#define LANDRU_RECT_H

#include <stdbool.h>
#include <stdint.h>

typedef struct Rect Rect;
typedef struct Poly Poly;

struct Rect {
	int16_t top;
	int16_t left;
	int16_t bottom;
	int16_t right;
};

struct Poly {
	int16_t x[4];
	int16_t y[4];
};

void lrect_Clear_Rect(Rect* rect);
void lrect_Set_Rect(Rect* rect, int16_t left, int16_t top, int16_t right, int16_t bottom);
void lrect_Max_Rect(Rect* rect);
void lrect_Copy_Rect(Rect* dst, Rect* src);
bool lrect_Equal_Rect(Rect* rect1, Rect* rect2);
void lrect_Offset_Rect(Rect* rect, int16_t xOffset, int16_t yOffset);
void lrect_Origin_Rect(Rect* rect);
void lrect_Allign_Rect(Rect* rect, Rect* parent, uint8_t xAlign, uint8_t yAlign);
void lrect_Flip_Rect(Rect* rect, Rect* frame, int16_t hflip, int16_t vflip);
void lrect_Inset_Rect(Rect* rect, int16_t offsetX, int16_t offsetY);
bool lrect_Empty_Rect(Rect* rect);
bool lrect_Sect_Rect(Rect* a, Rect* b);
void lrect_Enclose_Rect(Rect* dstRect, Rect* srcRect);
bool lrect_Clip_Rect(Rect* dstRect, Rect* clipRect);
void lrect_Clip_Point_To_Rect(Rect* clipRect, int16_t* pointX, int16_t* pointY);
bool lrect_Point_In_Rect(Rect* rect, int16_t pointX, int16_t pointY);
void lrect_Set_Poly(Poly* p, int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t x2, int16_t y2,
					int16_t x3, int16_t y3);
void lrect_Copy_Poly(Poly* dst, Poly* src);

#endif
