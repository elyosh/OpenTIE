#ifndef __PANELRTS_H__
#define __PANELRTS_H__

#include <stdint.h>

/*
 * PANELRTS -- panel real-time helpers.
 *
 * Contains two small routines hot in the flight loop: the panel-view switch
 * used by USER / ANIM / REPLAY to move the cockpit between the ~28 slots in
 * panelviewdefs[], and the zero/space-padded decimal printer used by PANEL,
 * GATE, MAPROOM, MSG and MSGROOM to format HUD numbers.
 *
 * The module also owns the placevalue table and four runtime-loaded string
 * pointers (buoystr, warheadstrings, unknownstring, statusstrings) that are
 * populated by fediskio_loadstringdata from strings.dat.
 */

/* Place-value table consumed by panelrts_outnum and msg '&N' formatting.
 * 6 entries matching the 12-byte binary layout at 0xD6518:
 *   [0]=1, [1]=1 (ones), [2]=10, [3]=100, [4]=1000, [5]=10000.
 * Index 0 is unused; index is 1-based (one-digit position = 1).
 * Max representable width is 5 digits; larger widths walk past the table. */
extern uint16_t placevalue[6];

/* String pointers populated at startup by fediskio_loadstringdata. Each is a
 * pointer into the strings.dat relocation buffer; the runtime shape behind
 * the void* is a flat array of char* (char **) for buoystr / warheadstrings /
 * statusstrings, and a single char* for unknownstring. */
extern void* buoystr;        /* char *[]: species 70..84 buoy names   */
extern void* warheadstrings; /* char *[]: warhead label by ship_idx   */
extern void* unknownstring;  /* char *   : "unknown" fill string     */
extern void* statusstrings;  /* char *[]: target status labels       */

/*
 * panelrts_setnewpilotview -- try to switch the cockpit to panelviewdefs[view_idx].
 *
 * Returns 0 if the slot is empty (panelviewdefs[view_idx].flags == 0),
 * 1 otherwise. Writes pilotview + pilotview_save when the slot is different
 * from the current view, and calls panel_dosetnewpilotview to load/paint the
 * new panel bitmap -- unless replayviewmode is set, in which case the repaint
 * is skipped (the replay subsystem owns the on-screen view).
 */
uint16_t panelrts_setnewpilotview(uint16_t view_idx);

/*
 * panelrts_outnum -- emit 'ndigits' decimal characters for 'value' via outchar.
 *
 * Layout: right-aligned within ndigits columns. Positions with index <= minpad
 * always print a digit (forcing leading zeros there); positions > minpad print
 * a digit only once a non-zero digit has been emitted, otherwise a space.
 * Digits are capped at 9 before the ASCII adjust.
 *
 * Sentinel: value == 0xFFFF draws 'ndigits' literal '0' characters in color
 * 0x40 with dropflag temporarily cleared, then restores the previous drop and
 * text colors. Used by HUD fields to render an "unknown" placeholder.
 */
void panelrts_outnum(int32_t value, uint16_t ndigits, uint16_t minpad);

#endif /* __PANELRTS_H__ */
