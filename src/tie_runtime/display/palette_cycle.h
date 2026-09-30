#ifndef TIE_RUNTIME_DISPLAY_PALETTE_CYCLE_H
#define TIE_RUNTIME_DISPLAY_PALETTE_CYCLE_H

/* Engine-glow palette cycle driver. Reproduces the second half of
 * retail GAMESND_Host_Int (0x88adc..0x88bcf): on its own PIT-tick
 * cursor, decrements a countdown and on expiry advances a 5-phase
 * wheel that rewrites VGA DAC slots 0xF8/0xF9/0xFA. Cadence is 18
 * PIT ticks (~72 ms) by default, 36 (~144 ms) when colorcycleuserflag
 * is set. Gated by (palette_cycle_user & colorcycleflag) ||
 * colorcycleuserflag. Called once per TieRuntime_Tick from runtime.c. */
void TiePaletteCycle_Tick(void);

#endif
