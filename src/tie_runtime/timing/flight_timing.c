#include "tie_runtime/timing/flight_timing.h"

#include <limits.h>

typedef struct TieFlightTimingState {
	TieFlightUpdateRate requested_rate;
	uint16_t step_ticks;
	uint16_t compatibility_ticks;
	uint16_t pending_legacy_ticks;
	uint16_t pending_ai_ticks;
	uint16_t pending_animation_ticks;
	uint32_t dropped_legacy_periods;
	uint32_t dropped_ai_periods;
	uint32_t dropped_animation_periods;
	bool legacy_due;
	bool session_active;
} TieFlightTimingState;

static TieFlightTimingState s_timing = {
	.requested_rate = TIE_FLIGHT_UPDATE_RATE_NATIVE,
	.step_ticks = 4,
	.compatibility_ticks = 4,
};

static void TieTiming_AddDroppedPeriods(uint32_t* total, uint32_t dropped) {
	*total = dropped > UINT32_MAX - *total ? UINT32_MAX : *total + dropped;
}

static TieFlightCadence TieTiming_AdvanceCadence(uint16_t* pending, uint32_t* dropped_periods,
												 uint16_t elapsed_ticks) {
	if (!TieFlightTiming_IsHighRate()) {
		*pending = 0;
		return (TieFlightCadence) { .elapsed_ticks = elapsed_ticks, .due = true };
	}

	uint32_t accumulated = (uint32_t)*pending + elapsed_ticks;
	if (accumulated < s_timing.compatibility_ticks) {
		*pending = (uint16_t)accumulated;
		return (TieFlightCadence) { 0 };
	}
	accumulated -= s_timing.compatibility_ticks;
	if (accumulated >= s_timing.compatibility_ticks) {
		TieTiming_AddDroppedPeriods(dropped_periods, accumulated / s_timing.compatibility_ticks);
		accumulated %= s_timing.compatibility_ticks;
	}
	*pending = (uint16_t)accumulated;
	return (TieFlightCadence) { .elapsed_ticks = s_timing.compatibility_ticks, .due = true };
}

void TieFlightTiming_BeginSession(const TieFlightProfile* profile) {
	const TieGameVersion version = profile ? profile->version : TIE_GAME_VERSION_TIE95;
	const TieFlightUpdateRate requested = profile ? profile->update_rate : TIE_FLIGHT_UPDATE_RATE_NATIVE;
	const uint16_t native_ticks = version == TIE_GAME_VERSION_TIE98 ? 7u : 4u;

	s_timing.requested_rate = requested;
	s_timing.compatibility_ticks = requested == TIE_FLIGHT_UPDATE_RATE_TIE95 ? 4u : native_ticks;
	s_timing.step_ticks = requested == TIE_FLIGHT_UPDATE_RATE_UNLOCKED ? 1u : s_timing.compatibility_ticks;
	s_timing.pending_legacy_ticks = 0;
	s_timing.pending_ai_ticks = 0;
	s_timing.pending_animation_ticks = 0;
	s_timing.dropped_legacy_periods = 0;
	s_timing.dropped_ai_periods = 0;
	s_timing.dropped_animation_periods = 0;
	s_timing.legacy_due = false;
	s_timing.session_active = true;
}

void TieFlightTiming_EndSession(void) {
	s_timing = (TieFlightTimingState) {
		.requested_rate = TIE_FLIGHT_UPDATE_RATE_NATIVE,
		.step_ticks = 4,
		.compatibility_ticks = 4,
	};
}

uint16_t TieFlightTiming_StepTicks(void) { return s_timing.step_ticks; }

uint16_t TieFlightTiming_CompatibilityTicks(void) { return s_timing.compatibility_ticks; }

bool TieFlightTiming_IsHighRate(void) {
	return s_timing.session_active && s_timing.step_ticks < s_timing.compatibility_ticks;
}

uint32_t TieFlightTiming_RecordFrameLimit(void) {
	return 0x20000u * s_timing.compatibility_ticks / s_timing.step_ticks;
}

uint64_t TieFlightTiming_RecordDurationLimitUs(void) {
	return (uint64_t)0x20000u * s_timing.compatibility_ticks * 4000u;
}

int32_t TieFlightTiming_ScaleWithRemainder(int32_t value, uint16_t elapsed_ticks, int32_t divisor,
										   int32_t* remainder) {
	const int64_t numerator = (int64_t)value * elapsed_ticks + *remainder;
	const int32_t whole = (int32_t)(numerator / divisor);
	*remainder = (int32_t)(numerator % divisor);
	return whole;
}

void TieFlightTiming_BeginAdvance(uint16_t elapsed_ticks) {
	if (!TieFlightTiming_IsHighRate()) {
		s_timing.pending_legacy_ticks = 0;
		s_timing.legacy_due = true;
		return;
	}

	uint32_t accumulated = (uint32_t)s_timing.pending_legacy_ticks + elapsed_ticks;
	s_timing.legacy_due = accumulated >= s_timing.compatibility_ticks;
	if (s_timing.legacy_due) {
		accumulated -= s_timing.compatibility_ticks;
		if (accumulated >= s_timing.compatibility_ticks) {
			TieTiming_AddDroppedPeriods(&s_timing.dropped_legacy_periods,
										accumulated / s_timing.compatibility_ticks);
			accumulated %= s_timing.compatibility_ticks;
		}
	}
	s_timing.pending_legacy_ticks = (uint16_t)accumulated;
}

bool TieFlightTiming_LegacyDue(void) { return s_timing.legacy_due; }

TieFlightCadence TieFlightTiming_AdvanceAi(uint16_t elapsed_ticks) {
	return TieTiming_AdvanceCadence(&s_timing.pending_ai_ticks, &s_timing.dropped_ai_periods, elapsed_ticks);
}

TieFlightCadence TieFlightTiming_AdvanceAnimation(uint16_t elapsed_ticks) {
	return TieTiming_AdvanceCadence(&s_timing.pending_animation_ticks, &s_timing.dropped_animation_periods,
									elapsed_ticks);
}

uint32_t TieFlightTiming_DroppedLegacyPeriods(void) { return s_timing.dropped_legacy_periods; }

uint32_t TieFlightTiming_DroppedAiPeriods(void) { return s_timing.dropped_ai_periods; }

uint32_t TieFlightTiming_DroppedAnimationPeriods(void) { return s_timing.dropped_animation_periods; }

void TieFlightTiming_Save(TieFlightTimingCheckpoint* out) {
	if (!out)
		return;
	*out = (TieFlightTimingCheckpoint) {
		.requested_rate = (uint8_t)s_timing.requested_rate,
		.step_ticks = (uint8_t)s_timing.step_ticks,
		.compatibility_ticks = (uint8_t)s_timing.compatibility_ticks,
		.legacy_due = s_timing.legacy_due,
		.pending_legacy_ticks = s_timing.pending_legacy_ticks,
		.pending_ai_ticks = s_timing.pending_ai_ticks,
		.pending_animation_ticks = s_timing.pending_animation_ticks,
		.dropped_legacy_periods = s_timing.dropped_legacy_periods,
		.dropped_ai_periods = s_timing.dropped_ai_periods,
		.dropped_animation_periods = s_timing.dropped_animation_periods,
	};
}

bool TieFlightTiming_Restore(const TieFlightTimingCheckpoint* checkpoint) {
	if (!checkpoint || checkpoint->requested_rate > TIE_FLIGHT_UPDATE_RATE_UNLOCKED ||
		(checkpoint->step_ticks != 1u && checkpoint->step_ticks != 4u && checkpoint->step_ticks != 7u) ||
		(checkpoint->compatibility_ticks != 4u && checkpoint->compatibility_ticks != 7u) ||
		checkpoint->step_ticks > checkpoint->compatibility_ticks)
		return false;
	if ((checkpoint->requested_rate == TIE_FLIGHT_UPDATE_RATE_UNLOCKED && checkpoint->step_ticks != 1u) ||
		(checkpoint->requested_rate != TIE_FLIGHT_UPDATE_RATE_UNLOCKED &&
		 checkpoint->step_ticks != checkpoint->compatibility_ticks) ||
		(checkpoint->requested_rate == TIE_FLIGHT_UPDATE_RATE_TIE95 && checkpoint->compatibility_ticks != 4u))
		return false;

	s_timing = (TieFlightTimingState) {
		.requested_rate = (TieFlightUpdateRate)checkpoint->requested_rate,
		.step_ticks = checkpoint->step_ticks,
		.compatibility_ticks = checkpoint->compatibility_ticks,
		.pending_legacy_ticks = checkpoint->pending_legacy_ticks,
		.pending_ai_ticks = checkpoint->pending_ai_ticks,
		.pending_animation_ticks = checkpoint->pending_animation_ticks,
		.legacy_due = checkpoint->legacy_due != 0,
		.dropped_legacy_periods = checkpoint->dropped_legacy_periods,
		.dropped_ai_periods = checkpoint->dropped_ai_periods,
		.dropped_animation_periods = checkpoint->dropped_animation_periods,
		.session_active = true,
	};
	return true;
}
