#ifndef __XTIMER_H__
#define __XTIMER_H__

#include <stdint.h>

/*
 * XTIMER — PIT-tick delta accumulator backed by sim_clock.
 *
 * Mirrors retail's XTIMER_Time_Elapsed (0x8D46C): returns the number
 * of PIT ticks (4 ms each) that have elapsed since the previous call,
 * and resets the per-cursor accumulator. The implementation pulls
 * ticks from the synthetic clock via a TieSimClockCursor, so HOST_DRIVEN
 * mode advances ticks deterministically when TieRuntime_Tick(delta_us) feeds
 * the clock.
 *
 * Used by tie_doframe's per-frame floor, the fade pacing loop, the
 * training-bonus countdown task, replay viewer pacing, and a handful
 * of init paths (create.c) that just want to seed the cursor.
 */
uint32_t xtimer_time_elapsed(void);
void xtimer_rebase(void);

/* Delay until an accumulator reaches target_ticks without consuming the
 * XTIMER cursor. The active flight task uses this to publish its wake time. */
uint64_t xtimer_delay_until_ticks_us(uint32_t accumulated_ticks, uint32_t target_ticks);

#endif
