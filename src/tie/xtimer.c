// FLAGS: TIE95 -s
#include "tie/xtimer.h"

#ifdef TIE_MODERN
#include "tie_runtime/timing/flight_clock.h"

uint32_t xtimer_time_elapsed(void) { return TieFlightClock_TimeElapsed(); }
#else
extern int32_t timer_ticks_gbl;

// FUNCTION: TIE95 0x8D46C
uint16_t xtimer_time_elapsed(void) {
	uint16_t elapsed = (uint16_t)timer_ticks_gbl;
	timer_ticks_gbl = 0;
	return elapsed;
}
#endif
