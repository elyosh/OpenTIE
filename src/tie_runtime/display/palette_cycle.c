#include "tie_runtime/display/palette_cycle.h"

#include "tie/tie.h"
#include "tie_runtime/display/classic_framebuffer.h"
#include "tie_runtime/timing/sim_clock.h"

/* Five-phase glow animation rewrites palette slots 0xF8..0xFA every 18 or
 * 36 PIT ticks when color cycling is enabled. */

static const uint8_t s_glow_table[45] = {
	/* slot 0xF8 — green pulse */
	0x00,
	0x3F,
	0x00,
	0x00,
	0x30,
	0x00,
	0x00,
	0x22,
	0x00,
	0x00,
	0x14,
	0x00,
	0x00,
	0x06,
	0x00,
	/* slot 0xF9 — pink pulse */
	0x3F,
	0x20,
	0x1F,
	0x3F,
	0x1A,
	0x15,
	0x3F,
	0x14,
	0x0E,
	0x3F,
	0x1A,
	0x15,
	0x3F,
	0x20,
	0x1F,
	/* slot 0xFA — blue pulse */
	0x1A,
	0x39,
	0x3F,
	0x11,
	0x32,
	0x3F,
	0x08,
	0x29,
	0x3F,
	0x11,
	0x32,
	0x3F,
	0x1A,
	0x39,
	0x3F,
};

static TieSimClockCursor s_glow_cursor;
static int s_glow_cursor_init;
static int s_glow_countdown; /* PIT ticks remaining */
static int s_glow_phase;     /* 0..4 */

void TiePaletteCycle_Tick(void) {
	if (!s_glow_cursor_init) {
		TieSimClock_CursorInit(&s_glow_cursor);
		s_glow_cursor_init = 1;
		/* countdown = 0 + phase = 0 from BSS; the first non-empty call
		 * will fall straight into the reload+fire branch, matching the
		 * retail ISR's behavior right after init when D2B15 starts at 0. */
		return;
	}

	int32_t pit_ticks = TieSimClock_CursorConsumePitTicks(&s_glow_cursor);
	while (pit_ticks > 0) {
		if (s_glow_countdown > 0) {
			int step = pit_ticks < s_glow_countdown ? pit_ticks : s_glow_countdown;
			s_glow_countdown -= step;
			pit_ticks -= step;
			if (s_glow_countdown > 0)
				break;
		}

		/* Reload runs unconditionally so cadence is preserved even
		 * when the gate is closed (matches retail 88af0..88b0f). */
		s_glow_countdown = colorcycleuserflag ? 36 : 18;

		int gate = ((palette_cycle_user & colorcycleflag) != 0) || (colorcycleuserflag != 0);
		if (!gate)
			continue;

		s_glow_phase = (s_glow_phase + 1) % 5;
		int off = s_glow_phase * 3;
		TieClassicFramebuffer_SetPalette(&s_glow_table[0 + off], 0xF8, 1);
		TieClassicFramebuffer_SetPalette(&s_glow_table[15 + off], 0xF9, 1);
		TieClassicFramebuffer_SetPalette(&s_glow_table[30 + off], 0xFA, 1);
	}
}
