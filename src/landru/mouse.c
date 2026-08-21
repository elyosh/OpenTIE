#include <landru/mouse.h>

#include "host_internal.h"

bool lmouse_MS_Initialize_Mouse(void) { return true; }

void lmouse_MS_Show_Mouse(void) { landru_host_mouse_show(true); }

void lmouse_MS_Hide_Mouse(void) { landru_host_mouse_show(false); }

void lmouse_MS_Get_Mouse_Pos(int16_t* buttons, int16_t* x, int16_t* y) {
	landru_host_mouse_position(buttons, x, y);
}

void lmouse_MS_Mouse_Movement(int16_t* x, int16_t* y) { landru_host_mouse_movement(x, y); }

void lmouse_MS_Set_Mouse_Pos(int16_t x, int16_t y) { landru_host_mouse_set_position(x, y); }

void lmouse_MS_Set_Mouse_X_Limits(int16_t min, int16_t max) {
	(void)min;
	(void)max;
}

void lmouse_MS_Set_Mouse_Y_Limits(int16_t min, int16_t max) {
	(void)min;
	(void)max;
}

void lmouse_MS_Set_Mouse_Sensitivity(int16_t hMick, int16_t vMick) {
	(void)hMick;
	(void)vMick;
}

int16_t lmouse_MS_Reset_Mouse(void) { return 1; }

int16_t lmouse_MS_Get_Number_Of_Buttons(void) { return 2; }
