/*
 * MAPROOM — in-flight tactical map screen (USER_inflightinfo page 1).
 *
 * Port of maproom.c from the LucasArts Watcom binary. The room renders a
 * top-down/side view 3D wireframe map of the player's surroundings: every
 * FlightObject and StaticObject is z-sorted, projected to screen-space,
 * and drawn as a side-coded icon with optional name + distance text plus
 * three helper lines (ground projection, velocity vector, and missile-
 * lock indicator). A grid of 33-tick world axes is drawn around the
 * focus object as orientation reference.
 *
 * Five public functions (the entry point + four internal helpers that
 * watdbg also flags as module-scope). Returns a navigation code (-1/0/+1)
 * matching the help / goals / damage / message rooms.
 */

#ifndef TIE_MAPROOM_H
#define TIE_MAPROOM_H

#include <stdint.h>

/* --- Public entry point --------------------------------------------------- */

/*
 * Push the tactical map as a tie_core task. The task latches a
 * USER_inflightinfo navigation code into `user_submodal_result`
 * before pop:
 *   -1 = page backward (previous tab)
 *    0 = exit / pick (Esc / 'm' / 'M' / Q)
 *   +1 = page forward (next tab)
 *
 * The task temporarily replaces the global farbufferptrs[] table with
 * the maproom icon-shape pointers and restores it on the FINISH
 * branch; sets up its own camera (centered on object_idx, distance
 * 0x40000), runs RENDER + POLL phases on the task stack at a 4-PIT-
 * tick frame budget, and double-buffers between newbuf and
 * xtransdataptr each frame.
 */
void maproom_Push_MapRoom_Task(void);

/* --- Internal helpers (watdbg module-scope) ------------------------------ */

/*
 * Place the camera at the world position of `obj_or_kind` (resolved via
 * create_getworldposition: object slot, static slot, or fg waypoint),
 * then translate the camera back along the eye-to-world Z basis vector
 * (worldeyeA3/B3/C3) by `distance` units.
 *
 * Distance > 0x7FFF is broken into integer-unit steps (subtracting one
 * full basis vector per step) plus a fractional `(int16)remainder *
 * basis >> 15` tail. Used to set up the orbit camera at room entry and
 * after a target-switch.
 */
void maproom_setcamerafocus(uint16_t obj_or_kind, int32_t distance);

/*
 * Test whether `obj_idx` is the first slot in objects[] that belongs to
 * its flight group (used to decide which member of an FG carries the
 * full text label).  Returns 1 for the leader / sentinel cases, 0 for
 * a non-leader follower.
 *
 * Special-cases that all return 1: 0xFFFF (no-target sentinel), the
 * player's own object slot, indices >= NUM_CRAFTS (28 in the demo), and
 * idx==0 (always first).
 *
 * NOTE: the demo binary inlines every call site of this function and
 * the orphaned definition is unreferenced; we keep it for parity.
 */
int32_t maproom_firstingroup(uint16_t idx);

/*
 * Draw one map item: its side-coded icon, optional name above, optional
 * distance ("XX.YY MGLT") below, plus 2-3 helper lines (ground vertical,
 * velocity vector, missile-target indicator).
 *
 * `obj_idx` is the LOCAL index in [0, NUM_CRAFTS+48): 0..0x4B is a
 * FlightObject slot and >= 0x4C is a static-table slot at index
 * (obj_idx - 76).
 *
 * `selected_obj_ref` is the cursor selection in CREATE_getworldposition's
 * obj_or_kind namespace (0x3800+i for static slot i); the function adds
 * 14260 (= 0x3800 - 76) when comparing static `obj_idx` against
 * `selected_obj_ref` or `target_obj_idx`.
 *
 * `num_label`: 0 = no extra label, otherwise printed as a 1-based digit
 * suffix " (N)" on the name (the digit emitted is num_label + '/').
 *
 * `z` is the projection-plane scaling passed to TRANSFM2_getscreencoord*
 * (smaller z = closer to the eye in the orthographic-ish projection).
 */
void maproom_drawmapitem(uint16_t obj_idx, uint16_t selected_obj_ref, char num_label, int32_t z);

/*
 * Paint the five status text panels around the tactical map:
 *   bottom-left  "Hostile"  (color 0x52, red)
 *   bottom-center "Imperial" (color 0x4A, yellow)
 *   bottom-right "Neutral"  (color 0x46, cyan)
 *   top-left help string from maproomhelpstrings[page_idx]
 *   top-right context help (always maproomhelpstrings[2])
 *
 * Each NHI label is followed (right-aligned) by the count from
 * NHIstatusstrings[xxxflag] for that side.  Background palette index
 * 0x40 fills the panels.
 */
void maproom_drawNHIstatus(uint16_t page_idx);

/* --- Module globals (watdbg owner: maproom.c) ----------------------------- */

/*
 * NHI / warhead filter status (cycle 0 -> 1 -> 2 -> 0 by 'h'/'i'/'n'/'w'
 * keys). Indices into NHIstatusstrings[]:
 *   0 = "All"   (full label + distance per icon)
 *   1 = "Cnt"   (icon-only, no per-icon text)
 *   2 = "Off"   (don't draw items of this side at all)
 * `warheadflag` only suppresses warhead/missile sprites (genus 6/7).
 */
extern uint8_t imperialflag;
extern uint8_t neutralflag;
extern uint8_t hostileflag;
extern uint8_t warheadflag;

/*
 * Lookup tables for the 320x200 / 640x480 icon variants.  At room
 * entry, maproom selects one of the two pairs based on flightResolution
 * and points species2icon / iconxsize / iconysize / iconfilename at it.
 *
 * species2icon[ship_idx]      -> base icon index
 * iconxsize[base_icon_idx]    -> icon pixel width
 * iconysize[base_icon_idx]    -> icon pixel height
 * iconfilename                -> "icons640.ico" or "mapicons.ico"
 *                                (basename only — caller prepends resourcedir)
 */
extern const uint8_t species2icon640[106];
extern const uint8_t iconxsize640[64];
extern const uint8_t iconysize640[64];
extern const uint8_t species2icon320[106];
extern const uint8_t iconxsize320[66];
extern const uint8_t iconysize320[66];
extern const char iconfilename640[22]; /* "icons640.ico\0" */
extern const char iconfilename320[22]; /* "mapicons.ico\0" */

/* Active resolution-dependent pointers (point at one of the *320 / *640
 * tables).  Set by maproom_maproom at room entry. */
extern const uint8_t* species2icon;
extern const uint8_t* iconxsize;
extern const uint8_t* iconysize;
extern const char* iconfilename;

/* Map render rectangle (set by maproom_maproom: 0,640,41,442 hi-res /
 * 0,320,17,184 lo-res). Width/Height are the derived dimensions. */
extern int32_t mapScreenLeft;
extern int32_t mapScreenRight;
extern int32_t mapScreenTop;
extern int32_t mapScreenBottom;
extern int32_t mapScreenWidth;
extern int32_t mapScreenHeight;

/* Number of icons per side (61 hi-res, 66 lo-res); the per-side icon
 * offset is N*maxMapIcons (Hostile=0, Imperial=2*N, Neutral=3*N, other=N). */
extern int32_t maxMapIcons;

/*
 * String-table pointer arrays (filled by fediskio_loadtext / equivalent).
 * NHIstatusstrings[xxxflag]   -> count display ("All", "Cnt", "Off", ...)
 * maproomhelpstrings[page_idx]-> top-left help text per view mode
 * maproomhelpstrings[2]       -> always the right-side fixed-help string
 * hostilestr/imperialstr/neutralstr -> the three labels at the bottom
 */
extern const char* hostilestr;
extern const char* imperialstr;
extern const char* neutralstr;
extern const char** NHIstatusstrings;
extern const char** maproomhelpstrings;

/*
 * Saved farbufferptrs[] entries: 265-pointer table used to swap between
 * "panel parts" mode (in-flight) and "map icons" mode (this room).
 * Backing storage = maproomicons_buf (owned by fediskio.c); first 1060
 * bytes hold the 265-entry pointer array and the rest holds the icon
 * shape data.
 */
extern void** mapfarbufferptrs;

/*
 * Tracks whether map icons have been loaded yet (cleared at game start,
 * set the first time maproom_maproom enters and calls fediskio_loadbufferdata).
 */
extern uint8_t mapiconsloaded;

#endif /* TIE_MAPROOM_H */
