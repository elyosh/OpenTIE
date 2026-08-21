#ifndef LANDRU_MOUSE_H
#define LANDRU_MOUSE_H

#include <stdbool.h>
#include <stdint.h>

bool lmouse_MS_Initialize_Mouse(void);
void lmouse_MS_Show_Mouse(void);
void lmouse_MS_Hide_Mouse(void);
void lmouse_MS_Get_Mouse_Pos(int16_t* buttons, int16_t* x, int16_t* y);
void lmouse_MS_Mouse_Movement(int16_t* x, int16_t* y);
void lmouse_MS_Set_Mouse_Pos(int16_t x, int16_t y);

#endif
