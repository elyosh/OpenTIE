#ifndef TIE_COCKPIT_COMMON_H
#define TIE_COCKPIT_COMMON_H

/*
 * Small cockpit predicates and coordinate rules shared by the renderer.
 * Inline-only — no .c file, no link-time symbol. Text-specific helpers
 * (TieCockpitText_RemapColor, font IDs) live in cockpit_text.h.
 */

#include <stdbool.h>
#include <stdint.h>

#include "tie_runtime/snapshot/snapshot_types.h"

/* "Is this instrument used at all on this view?" — layout marks unused
 * slots with (x, y) == (0, 0). */
static inline bool TieCockpitCommon_InstrumentActive(const TieHudInstrument* ins) {
	return (ins->x != 0) || (ins->y != 0);
}

/* panel.c branches layout widths on TIE_FLIGHT_RES_VGA vs SVGA. */
static inline bool TieCockpitCommon_IsSvga(const TieSnapshot* snap) { return snap->cockpit.classic_w >= 600; }

static inline bool TieCockpitCommon_IsClassic4x3(int coord_w, int coord_h) {
	return (coord_w == 320 && coord_h == 200) || (coord_w == 640 && coord_h == 480);
}

/* Classic VGA pixels are 1:1.2 on display, so 320x200 and 640x480
 * cockpit coordinate frames both occupy a 4:3 presentation region. */
static inline float TieCockpitCommon_DisplayAspect(int coord_w, int coord_h) {
	if (TieCockpitCommon_IsClassic4x3(coord_w, coord_h))
		return 4.0f / 3.0f;
	if (coord_w <= 0 || coord_h <= 0)
		return 4.0f / 3.0f;
	return (float)coord_w / (float)coord_h;
}

#endif
