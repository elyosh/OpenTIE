#ifndef TIE_COCKPIT_TEXT_H
#define TIE_COCKPIT_TEXT_H

/* Builds TieUIText records consumed by the cockpit scene2d renderer. */

#include <stdbool.h>
#include <stdint.h>

#include "tie_remaster/flight/cockpit/layout.h"
#include "tie_remaster/scene2d/text.h"
#include "tie_runtime/snapshot/snapshot_types.h"

/* festring's `remap_color`: inputs ≥ 0x40 indirect through the engine
 * remap table (256-byte logical → physical palette indices); below
 * stays raw. */
uint8_t TieCockpitText_RemapColor(uint8_t logical_color);

/* TieScene2dTextRenderer font IDs for the two cockpit typefaces. The engine's
 * festring_setfontsize selects between them:
 *   setfontsize(2) → MICRO64 (smaller, default for HUD digits, target name)
 *   setfontsize(1) → TINY64  (larger, used by threat readout + training CRT) */
#define FONT_MICRO64 3
#define FONT_TINY64 4

/* Engine setbackcolor() values selected by each panel_update* function. */
#define COCKPIT_BG_CMD 0x30    /* panel_updatecmd */
#define COCKPIT_BG_HUD 0x40    /* panel_updatespeed / throttle / replay */
#define COCKPIT_BG_THREAT 0x2C /* panel_updatethreatname */
#define COCKPIT_BG_AMMO 0x00   /* panel_updatehardpoint */

/* Append a TieUIText record at (x, y), MICRO64 default; returns false
 * when the output buffer is full or the string is empty. */
bool TieCockpitText_Push(TieUIText* buf, int* cnt, int cap, int16_t x, int16_t y, const char* s,
						 uint8_t color);

/* Compose the message bar's text records (body line, time-warp "T:Nx"
 * readout, and active training timer/bonus values). Returns the count
 * of records appended.
 *
 * `line_top` / `line_left` / `line_right` are the bar's top edge,
 * body-text x indent, and time-warp right edge — all in the cockpit
 * pass's coord frame. Caller has already rescaled from the engine's
 * 4:3 classic frame onto the layout's reference frame. */
int TieCockpitText_BuildmsgBarText(const TieSnapshot* snap, int line_top, int line_left, int line_right,
								   TieUIText* out, int cap);

/* Mission clock (idx 30, panel_updateclock). MM + SS as two records;
 * the colon between them is part of the cockpit bitmap. View 0 only.
 * Returns the record count written.
 *
 * `instruments` is the HUD instrument array the builder reads x/y from.
 * Pass `snap->hud.instruments` for 4:3 cockpits; widescreen layouts use a
 * remapped array so anchors land in the layout's
 * reference frame. */
int TieCockpitText_BuildclockText(const TieSnapshot* snap, const TieHudInstrument* instruments,
								  TieUIText* out, int cap);

/* Per-tick HUD text dispatch, mirroring panel_updatepanel:
 *   view 0  → CMD readout + speed/throttle + missile ammo + recording %
 *   panel source 17 → requested alternate-camera title strip
 *   view 19 → no text
 *   view 20 → threat readout
 *   other   → no text
 * `text_renderer` is the host text measurer (right-align/centre math); may
 * be NULL — then those layouts fall through to left-aligned at ins.x.
 * `instruments` per TieCockpitText_BuildclockText.
 * `layout` (nullable) supplies authored CMD/threat column widths;
 * NULL → classic-px constants used raw (works for 4:3 only). */
int TieCockpitText_BuildHudText(struct TieScene2dTextRenderer* text_renderer, const TieSnapshot* snap,
								const TieHudInstrument* instruments, const TieCockpitLayout* layout,
								const TieScene2dTextSpace* space, TieUIText* out, int cap);

#endif
