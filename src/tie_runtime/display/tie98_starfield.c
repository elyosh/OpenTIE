#include "tie_runtime/display/tie98_starfield.h"

#include "tie/rtsvga2.h"

uint8_t g_tie98StarPositionIndex[TIE98_STARFIELD_STAR_COUNT];
uint8_t g_tie98StarColor8[TIE98_STARFIELD_STAR_COUNT];
uint16_t g_tie98StarColor16[TIE98_STARFIELD_STAR_COUNT];

void Tie98StarColors_Invalidate(void) {
	g_starColor8Initialized = 0;
	g_starColor16Initialized = 0;
}
