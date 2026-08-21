#ifndef TIE_FLIGHT_TIMING_H
#define TIE_FLIGHT_TIMING_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "tie_runtime/runtime/profile_types.h"

typedef struct TieFlightCadence {
	uint16_t elapsed_ticks;
	bool due;
} TieFlightCadence;

typedef struct TieFlightTimingCheckpoint {
	uint8_t requested_rate;
	uint8_t step_ticks;
	uint8_t compatibility_ticks;
	uint8_t legacy_due;
	uint16_t pending_legacy_ticks;
	uint16_t pending_ai_ticks;
	uint16_t pending_animation_ticks;
	uint32_t dropped_legacy_periods;
	uint32_t dropped_ai_periods;
	uint32_t dropped_animation_periods;
} TieFlightTimingCheckpoint;

void TieFlightTiming_BeginSession(const TieFlightProfile* profile);
void TieFlightTiming_EndSession(void);

uint16_t TieFlightTiming_StepTicks(void);
uint16_t TieFlightTiming_CompatibilityTicks(void);
bool TieFlightTiming_IsHighRate(void);
uint32_t TieFlightTiming_RecordFrameLimit(void);
uint64_t TieFlightTiming_RecordDurationLimitUs(void);
int32_t TieFlightTiming_ScaleWithRemainder(int32_t value, uint16_t elapsed_ticks, int32_t divisor,
										   int32_t* remainder);

void TieFlightTiming_BeginAdvance(uint16_t elapsed_ticks);
bool TieFlightTiming_LegacyDue(void);
TieFlightCadence TieFlightTiming_AdvanceAi(uint16_t elapsed_ticks);
TieFlightCadence TieFlightTiming_AdvanceAnimation(uint16_t elapsed_ticks);
uint32_t TieFlightTiming_DroppedLegacyPeriods(void);
uint32_t TieFlightTiming_DroppedAiPeriods(void);
uint32_t TieFlightTiming_DroppedAnimationPeriods(void);

void TieFlightTiming_Save(TieFlightTimingCheckpoint* out);
bool TieFlightTiming_Restore(const TieFlightTimingCheckpoint* checkpoint);

#endif
