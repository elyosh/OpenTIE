/*
 * MOUSE2 — flight-sim mouse input wrappers.
 * Thin wrappers around the Landru XMOUSE/XIO mouse driver.
 */

#include "tie/mouse2.h"
#include "landru/io.h"
#include "landru/mouse.h"

// FUNCTION: TIE 0x323B0
int16_t mouse2_checkformouse(void) { return lio_Is_Mouse_Input(); }

// FUNCTION: TIE 0x323B8
int16_t mouse2_readmouse(int16_t* x, int16_t* y) {
	int16_t buttons;
	lmouse_MS_Get_Mouse_Pos(&buttons, x, y);
	return buttons;
}

void mouse2_deltamouse(int16_t* dx, int16_t* dy) { lmouse_MS_Mouse_Movement(dx, dy); }
