#ifndef __FESTRING_H__
#define __FESTRING_H__

#include <stdint.h>

void festring_setcursor(int16_t x, int16_t y);
void festring_setbound(int16_t left, int16_t top, int16_t right, int16_t bottom);
void festring_settextcolor(uint16_t color);
void festring_setbackcolor(uint16_t color);
void festring_setdropcolor(uint16_t color);
void festring_setlinewrap(int16_t enable);
void festring_setautofill(int16_t enable);
void festring_setfontsize(int16_t size);
void festring_farstrcpy(const char* src);
void festring_farstrcat(const char* src);
void festring_farstradd(char c);
void festring_outstring(const uint8_t* s);
void festring_outstringcenter(const uint8_t* s);
void festring_outstringright(const uint8_t* s);
void festring_clearscreen(void);
void festring_hidescreen(void);
void festring_showscreen(void);

#endif
