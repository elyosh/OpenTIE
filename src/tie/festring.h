#ifndef TIE_FESTRING_H
#define TIE_FESTRING_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* TIE98 widened the cursor coordinates to int. */
#if defined(TIE98) && !defined(TIE_MODERN)
typedef int FestringCoord;
#else
typedef uint16_t FestringCoord;
#endif

void festring_setcursor(FestringCoord x, FestringCoord y);
void festring_setbound(uint16_t left, uint16_t top, uint16_t right, uint16_t bottom);
void festring_settextcolor(uint16_t color);
void festring_setbackcolor(uint16_t color);
void festring_setdropcolor(uint16_t color);
void festring_setlinewrap(int16_t enable);
void festring_setautofill(int16_t enable);
void festring_setfontsize(int size);
void festring_farstrcpy(const char* src);
void festring_farstrcat(const char* src);
void festring_farstradd(char c);
void festring_outstring(const uint8_t* s);
void festring_outstringcenter(const uint8_t* s);
void festring_outstringright(const uint8_t* s);
void festring_clearscreen(void);
void festring_hidescreen(void);
void festring_showscreen(void);

#ifdef __cplusplus
}
#endif

#endif
