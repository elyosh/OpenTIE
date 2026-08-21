#ifndef __BACKDRP2_H__
#define __BACKDRP2_H__

#include <stdint.h>

/*
 * BACKDRP2 — 3D skybox backdrop renderer.
 *
 * The skybox is a cube of up to 22 sprite tiles (planets, stars, nebulae)
 * distributed across six walls: front, back, left, right, top, bottom.
 * Each frame only three walls are drawn (the ones facing the camera).
 *
 * Tile layout is fully data-driven: position[] is a packed 8-bit direction
 * descriptor and species[] is the per-tile bitmap id (an index into the
 * global species_table). CREATE_createbackdrop populates them for a new
 * mission; CREATE_loadmission can override from the .TIE file.
 */

/* --- backdrp2-owned globals (per watdbg) --- */

/* Per-tile direction descriptor, packed into one byte:
 *   bits 0-2 : primary axis index (0..7), indexes shift*mul[] tables
 *   bit  3   : primary negate (-primary instead of +primary)
 *   bits 4-6 : secondary axis index (0..7)
 *   bit  7   : secondary subtract (otherwise add)
 *
 * Tiles are laid out in the array as:
 *   [front backdropfrontcnt][back backdropbackcnt]
 *   [left  backdropleftcnt ][right backdroprightcnt]
 *   [top   backdroptopcnt  ][bottom backdropbottomcnt] */
extern uint8_t backdropposition[64];

/* Per-tile bitmap id (index into the global species_table[]). */
extern uint8_t backdropspecies[64];

/* Tile counts per wall. Totals typically add up to 22 (the default in
 * CREATE_createbackdrop: 4+4+4+4+3+3). */
extern uint16_t backdropfrontcnt;
extern uint16_t backdropbackcnt;
extern uint16_t backdroptopcnt;
extern uint16_t backdropbottomcnt;
extern uint16_t backdropleftcnt;
extern uint16_t backdroprightcnt;

/* --- API --- */

/* Render one frame of the backdrop:
 *   1. Refresh scaled rotation-matrix lookup tables (shift*mul) used by
 *      the per-tile position solver.
 *   2. Refresh the 5x5x5 parallax-star eye-space grid (stareyex/y/z).
 *   3. If drawbackdropflag is set, dispatch each visible wall's tiles
 *      through backdrp2_backdrawbitmap.
 *
 * Called from MAPROOM_maproom, TIE_updatescreen, BPFLIGHT_draw_Engine. */
void backdrp2_backdrop(void);

/* Project a single tile from eye space to screen and blit the rotated
 * sprite. Frustum-culls when |x| > z or |y| > z. angle is 16-bit
 * fixed-point (0..0xFFFF covering 360°). tile_idx is a 0-based index
 * into backdropposition[]/backdropspecies[]. */
void backdrp2_backdrawbitmap(int32_t x, int32_t y, int32_t z, uint16_t angle, int tile_idx);

#endif
