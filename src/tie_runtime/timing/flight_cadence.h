#ifndef TIE_RUNTIME_TIMING_FLIGHT_CADENCE_H
#define TIE_RUNTIME_TIMING_FLIGHT_CADENCE_H

#include <stdint.h>

#include "tie_runtime/timing/flight_timing.h"

/* Port-owned high-rate flight cadence adapters around recovered per-frame
 * updates. At the native rate they call the recovered update directly. */

/* Latch the AI cadence for this frame; tie_updatetime decrements per-craft
 * AI timers by the latched ticks instead of frameticks. */
void TieFlightCadence_SetAiTimerTicks(TieFlightCadence ai_cadence);
uint16_t TieFlightCadence_AiTimerTicks(void);

void TieFlightCadence_RunPlaneAi(TieFlightCadence cadence);
void TieFlightCadence_RunAnimation(TieFlightCadence cadence);

#endif
