#ifndef __BPFLIGHT_H__
#define __BPFLIGHT_H__

#include <stdbool.h>
#include <stdint.h>

#include "landru/actor.h"
#include "landru/fourcc.h"
#include "landru/rect.h"
#include "landru/res.h"
#include "tie/matrix.h"

/*
 * BPFLIGHT — 3D ship viewer for blueprint, training, and combat rooms.
 *
 * Hosts the custom-actor viewports whose draw callback runs the full 3D
 * pipeline (FVIEW → TRANSFM2 → DRAW/DRAWPOL → XTRANS2) on a locally
 * loaded SHIP resource. Supports three scenes: training (2 viewports +
 * "trnfly1" orbit matrix), combat (2 viewports + "cmbtfly1"), blueprint
 * (single large viewport, manually rotated).
 */

/* Nonzero while a flight engine is open. Checked by DRAW / DRAWPOL to
 * short-circuit some per-frame work (e.g. polydepthsort). */
extern uint32_t bpflightflag;

/* Primary TIE95 flight-object buffer (ship model data). BLUEPRNT dereferences
 * this to read ship dimensions for the info overlay.
 *
 * Layout at the head: [u16 size][ShipModelData...]. The binary stores a
 * 16-bit HANDLE; we keep a malloc'd pointer here. */
extern void* bpflight_fltobj_data;

/* Currently-highlighted mesh_type for the component-blink effect.
 * Toggles between bp_active_component and -1 every 8/16 frames. */
extern int16_t bpflight_cur_component;

/* Latched user-selected component mesh_type (-1 = none). Input comes from
 * outside modules that set which component to highlight. */
extern int16_t bpflight_active_component;

/* Per-viewport camera pivot angles, indexed by Actor->id (0=primary,
 * 1=secondary thumbnail, 2=blueprint full-screen). External modules
 * (e.g. BLUEPRNT) adjust these for manual camera control. */
extern int16_t bpflight_pivotpitch[3];
extern int16_t bpflight_pivotheading[3];
extern int16_t bpflight_pivotyaw[3];
extern int16_t bpflight_pivotroll[3];

/* -- Public API -- */

/* Initialize the 3D viewer for one of three scenes.
 *   scene == 1 : training room (primary 62,4,256,116 + thumbnail, "trnfly1")
 *   scene == 2 : combat room   (primary 59,2,260,115 + thumbnail, "cmbtfly1")
 *   scene == 3 : blueprint     (single viewport 131,30,278,200, no matrix)
 *
 * Allocates the viewport actor(s), 65000-byte xtransdata scratch, the
 * object heap (15000 or 33000 bytes), optional 9000-byte obstacle heap
 * (training only), loads a camera orbit matrix where applicable, seeds
 * 256 random stars, and sets the per-frame render flags. Returns the
 * primary viewport's Actor*. */
Actor* bpflight_Open_Flight_Engine(int16_t scene);

/* Shut down the viewer. Frees xtransdata, fltobj_data, optional obstacle
 * heap, the orbit Matrix, restores flightResolution from tempRes, and
 * resets the VESA video base pointer. Clears bpflightflag. */
void bpflight_Close_Flight_Engine(void);

/* Swap the current orbit Matrix. Called by TRAIN / COMBAT sub-screen
 * transitions (e.g. training ship -> training course uses a different
 * named frame set in matrix.lfd). */
void bpflight_Open_New_Matrix(const char* name);

/* Resume ship drawing in all three viewports and reset the primary
 * viewport's matrix-frame cursor. */
void bpflight_Start_Movie_Engine(void);

/* Pause ship drawing in the primary viewport. No-op for scene 3 (the
 * blueprint viewer keeps the ship visible even when the movie is
 * stopped, since its camera orbits instead of using matrix frames). */
void bpflight_Stop_Movie_Engine(void);

/* Load a FOURCC_SHIP resource from an LFD file into fltobj_data[mode].
 *   mode == 0 : primary ship  (fltobj_data)
 *   mode == 1 : training obstacle (fltobj_data_obstacle)
 * The resource name is uppercased in local scratch storage.
 * Always returns 1; on failure objectloadsize stays 0 and the viewer
 * paints a black rect. */
int bpflight_Load_Flight_Craft(const char* lfd_name, const char* shp_name, int16_t mode);

/* Maximum axis span of the primary TIE98 OPT preview model. */
int tie98_preview_primary_model_max_extent(void);

/* -- Dead-code helpers (inlined by Watcom in the shipped binary, no
 *    public callers). Declared for completeness / retail parity. -- */

/* Standalone version of the inline world->eye projection block.
 * Returns objectblockptr->model_scale_shift for convenience. */
uint8_t bpflight_getrelativexyz(void);

/* BSP walker for a single object.
 *   pass_gated == 0        : draw all meshes
 *   pass_gated != 0, mh==0 : draw non-MainHull (MiscHull+Antenna hidden)
 *   pass_gated != 0, mh!=0 : draw only MainHull (MiscHull+Antenna hidden)
 */
void bpflight_drawtreeobject(void* node, int16_t pass_gated, int16_t pass_mainhull);

/* Two-pass inline BSP walk: accessories (non-MainHull) then MainHull.
 * Unrolled form of two consecutive drawtreeobject calls. */
void bpflight_drawtrainobject(void* node);

/* Read a FOURCC_SHIP resource and store its payload size in objectloadsize. */
int bpflight_Res_Ship(ResFile* rf, uint8_t* buffer, const char* name);

/* Extract per-joint world pos + rotation from a MatrixFrame into the
 * render globals (worldx/y/z, calc{f,S,U}{1..3}), negate fwd into
 * craftf{1,2,3}, copy craft{S,U}, call fview_calcrotworldeye. */
void bpflight_Position_Craft(MatrixFrame* frame, int16_t joint_idx);

/* Apply (+) or undo (-) the training/combat per-material color offset
 * to materialcolors[0 .. 39*16-1] using the *roommapping[] table. */
void bpflight_settraincolors(int16_t apply_forward);
void bpflight_setcombatcolors(int16_t apply_forward);

/* Swap materialcolors[] with the 720-byte bp_materialcolors[] backup. */
void bpflight_swapbpmaterials(void);

#endif /* __BPFLIGHT_H__ */
