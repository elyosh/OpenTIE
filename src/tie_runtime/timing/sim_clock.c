#include "tie_runtime/timing/sim_clock.h"

/*
 * Synthetic engine clock — host-driven counter. TieRuntime_Tick(delta_us)
 * advances `s_now_us`; every tie_core consumer reads it via
 * TieSimClock_Now*. There is no wall-clock fallback: replay
 * determinism requires that all engine timing follows the host's
 * delta sequence, and the only producer of that delta is TieRuntime_Tick.
 *
 * The catch-up cap mirrors libimuse's: a single advance call moves
 * the clock by at most CATCHUP_CAP_US. Time beyond the cap is
 * dropped, never deferred — bounded burst rather than an avalanche.
 */

#define PIT_PERIOD_US 4000         /* 250 Hz: 4 ms / tick */
#define CATCHUP_CAP_US (64 * 1000) /* 64 ms hard ceiling per advance */

static uint64_t s_now_us;         /* counter value */
static int32_t s_last_advance_us; /* most recent TieSimClock_Advance value */

void TieSimClock_Init(void) {
	s_now_us = 0;
	s_last_advance_us = 0;
}

void TieSimClock_Advance(int32_t delta_us) {
	if (delta_us <= 0)
		return;
	if (delta_us > CATCHUP_CAP_US)
		delta_us = CATCHUP_CAP_US;
	s_last_advance_us = delta_us;
	s_now_us += (uint64_t)delta_us;
}

int32_t TieSimClock_LastAdvanceUs(void) { return s_last_advance_us; }

uint64_t TieSimClock_NowUs(void) { return s_now_us; }

uint64_t TieSimClock_NowNs(void) { return s_now_us * 1000ull; }

uint32_t TieSimClock_NowMs(void) { return (uint32_t)(s_now_us / 1000ull); }

void TieSimClock_CursorInit(TieSimClockCursor* c) {
	c->last_us = s_now_us;
	c->residual_us = 0;
}

int32_t TieSimClock_CursorConsumePitTicks(TieSimClockCursor* c) {
	uint64_t delta = s_now_us - c->last_us;
	c->last_us = s_now_us;

	/* Add residual sub-tick fragment from the previous call. The
	 * residual is in [0, PIT_PERIOD_US) so the int32 widening is
	 * safe; the delta itself can be larger but is fed straight into
	 * the divide. */
	int64_t total = (int64_t)delta + (int64_t)c->residual_us;
	int64_t ticks = total / PIT_PERIOD_US;
	c->residual_us = (int32_t)(total - ticks * PIT_PERIOD_US);
	return (int32_t)ticks;
}
