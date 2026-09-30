#ifndef TIE_RUNTIME_TIMING_FLIGHT_CLOCK_H
#define TIE_RUNTIME_TIMING_FLIGHT_CLOCK_H

#include <stdint.h>

uint32_t TieFlightClock_TimeElapsed(void);
void TieFlightClock_Rebase(void);
/* Remaining delay without consuming the flight clock's PIT accumulator. */
uint64_t TieFlightClock_DelayUntilTicksUs(uint32_t accumulated_ticks, uint32_t target_ticks);

#endif
