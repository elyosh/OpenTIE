/* PIT-tick accumulator backed by a synthetic-clock cursor. The cursor advances
 * only with runtime ticks, preserving deterministic playback. */

#include "tie/xtimer.h"
#include "tie_runtime/timing/sim_clock.h"
#include <stdint.h>

static TieSimClockCursor s_cursor;
static int s_initialized;

enum { XTIMER_PIT_PERIOD_US = 4000 };

// FUNCTION: TIE 0x8D46C
uint32_t xtimer_time_elapsed(void) {
	if (!s_initialized) {
		TieSimClock_CursorInit(&s_cursor);
		s_initialized = 1;
		return 0;
	}

	int32_t ticks = TieSimClock_CursorConsumePitTicks(&s_cursor);
	return ticks > 0 ? (uint32_t)ticks : 0u;
}

void xtimer_rebase(void) { TieSimClock_CursorInit(&s_cursor); }

uint64_t xtimer_delay_until_ticks_us(uint32_t accumulated_ticks, uint32_t target_ticks) {
	if (accumulated_ticks >= target_ticks || !s_initialized)
		return 0;

	const uint64_t elapsed_us = TieSimClock_NowUs() - s_cursor.last_us;
	const uint64_t total_us = elapsed_us + (uint32_t)s_cursor.residual_us;
	const uint32_t pending_ticks = (uint32_t)(total_us / XTIMER_PIT_PERIOD_US);
	if (accumulated_ticks + pending_ticks >= target_ticks)
		return 0;

	const uint32_t missing_ticks = target_ticks - accumulated_ticks - pending_ticks;
	return (uint64_t)missing_ticks * XTIMER_PIT_PERIOD_US - total_us % XTIMER_PIT_PERIOD_US;
}
