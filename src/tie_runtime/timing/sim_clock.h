#ifndef TIE_SIM_CLOCK_H
#define TIE_SIM_CLOCK_H

#include <stdint.h>

/*
 * Synthetic clock — the single source of engine time inside tie_core.
 *
 * Host-driven counter. TieRuntime_Tick(delta_us) calls TieSimClock_Advance
 * once per host tick; every tie_core consumer (timer.c, xtimer.c,
 * fade.c, etc.) reads the counter via the now_*() queries below.
 * No wall-clock fallback — replay determinism requires that engine
 * time follows the host's delta sequence exactly.
 */

/* Reset to t=0. Idempotent; called from tie_init. */
void TieSimClock_Init(void);

/* Bump the synthetic clock by delta_us. delta_us <= 0 is a no-op;
 * large deltas are clamped to a sane cap (~64 ms) so a host stall
 * produces a bounded burst rather than an avalanche of catch-up
 * work. */
void TieSimClock_Advance(int32_t delta_us);

/* The most recent value passed to `TieSimClock_Advance` (post-clamp,
 * post sign filter). Used by the per-tick replay recorder to capture
 * the host's TieRuntime_Tick delta into each ReplayInputFrame; replay
 * playback feeds the value back through `TieSimClock_Advance` so the
 * synthetic clock advances bit-stably across record/playback. Returns
 * 0 before the first TieSimClock_Advance call. */
int32_t TieSimClock_LastAdvanceUs(void);

/* Current synthetic time. Both queries return monotonic values; ns is
 * µs * 1000. Every tie_core consumer reads through these — there is no
 * other source of "now" in tie_core. */
uint64_t TieSimClock_NowUs(void);
uint64_t TieSimClock_NowNs(void);

/* Convenience: ms-precision time. Wraps every ~49 days, matching the
 * 32-bit counter shape used by `landru/timer.c`'s frame-budget gate. */
uint32_t TieSimClock_NowMs(void);

/* Read-and-zero the elapsed-PIT-tick accumulator. Returns the number
 * of 4000-µs (250 Hz) ticks that have elapsed since the previous
 * consume call for this consumer. The accumulator is a per-call cursor
 * — each consumer holds its own TieSimClockCursor.
 *
 * The two existing consumers — flight (xtimer) and front-end
 * (timer.c) — each track an independent cursor. */
typedef struct TieSimClockCursor {
	uint64_t last_us;
	int32_t residual_us; /* sub-tick remainder carried forward */
} TieSimClockCursor;

void TieSimClock_CursorInit(TieSimClockCursor* c);
int32_t TieSimClock_CursorConsumePitTicks(TieSimClockCursor* c);

#endif /* TIE_SIM_CLOCK_H */
