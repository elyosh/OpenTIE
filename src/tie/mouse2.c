/*
 * MOUSE2 — flight-sim mouse input wrappers.
 * Thin wrappers around the Landru XMOUSE/XIO mouse driver.
 */

#include "tie/mouse2.h"
#include "landru/io.h"
#include "landru/mouse.h"

// FUNCTION: TIE95 0x323B0
int mouse2_checkformouse(void) { return xio_Is_Mouse_Input(); }

// FUNCTION: TIE95 0x323B8
int16_t mouse2_readmouse(int16_t* x, int16_t* y) {
	int16_t buttons;
	xmouse_MS_Get_Mouse_Pos(&buttons, x, y);
	return buttons;
}

// FUNCTION: TIE95 0x323D4
void mouse2_deltamouse(int16_t* dx, int16_t* dy) { xmouse_MS_Mouse_Movement(dx, dy); }
