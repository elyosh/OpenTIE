#include "tie_runtime/runtime/flight_screen.h"

static TieFlightScreen s_active_screen = TIE_FLIGHT_SCREEN_NORMAL;

void TieFlightScreen_Reset(void) { s_active_screen = TIE_FLIGHT_SCREEN_NORMAL; }

TieFlightScreen TieFlightScreen_Active(void) { return s_active_screen; }

TieFlightScreen TieFlightScreen_SetActive(TieFlightScreen screen) {
	const TieFlightScreen previous = s_active_screen;
	s_active_screen = screen;
	return previous;
}
