#include "tie/bpflight.h"
#ifdef TIE_MODERN
#include "tie_runtime/display/classic_display.h"
#include "tie_runtime/runtime/flight_task.h"
#endif

#include "landru/vesa.h"
#include "tie/backdrp2.h"
#include "tie/draw.h"
#include "tie/drawpol.h"
#include "tie/edition.h"
#include "tie/fediskio.h" /* flightbuf_small_handle / flightbuf_big_handle */
#include "tie/flight_surface_tie98.h"
#include "tie/fview.h"
#include "tie/logbuf2.h"
#include "tie/matrix.h"
#include "tie/render_scene_tie98.h"
#include "tie/render_texture_tie98.h"
#include "tie/rtsvga2.h"
#include "tie/shell.h"
#include "tie/shellext.h"
#include "tie/shipext.h"
#include "tie/tie.h"
#include "tie/tie_render_tie98.h"
#include "tie/trace2.h" /* TRACE2_{EDGEINFO,EDGEHEADER}_CAP for flight-pool sizing */
#include "tie/transfm2.h"
#include "tie/xtrans2.h"
#include "tie_runtime/flight_assets/native_opt.h"
#include "tie_runtime/flight_assets/service.h"
#include "tie_runtime/runtime/profile.h"
#include "tie_runtime/runtime/wide_arithmetic.h"

#include "landru/actcust.h"
#include "landru/actor.h"
#include "landru/bitmap.h"
#include "landru/canvas.h"
#include "landru/dirty.h"
#include "landru/fourcc.h"
#include "landru/paint.h"
#include "landru/pal.h"
#include "landru/rect.h"
#include "landru/res.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* ----- Types ----- */

/* 4-slot, 3-byte RGB palette programmed into VGA slots 252..255 at t=1
 * of the primary viewport. Laid out tight (no pack directive). */
typedef struct BPStarColor {
	uint8_t r;
	uint8_t g;
	uint8_t b;
} BPStarColor;

/* ----- BPFLIGHT-owned exports ----- */

// GLOBAL: TIE95 0xD0E08
// GLOBAL: TIE98 0x50A9CC
uint32_t bpflightflag;

/* Exposed piece of the camera-pivot block (see internal storage below). */
// GLOBAL: TIE95 0xF6530
// GLOBAL: TIE98 0x6269B0
int16_t bpflight_pivotpitch[3];
// GLOBAL: TIE95 0xF6542
// GLOBAL: TIE98 0x6269B8
int16_t bpflight_pivotheading[3];
// GLOBAL: TIE95 0xF6524
// GLOBAL: TIE98 0x6269A8
int16_t bpflight_pivotyaw[3];
// GLOBAL: TIE95 0xF652A
// GLOBAL: TIE98 0x6269C0
int16_t bpflight_pivotroll[3];

/* Object buffers: [0] holds the viewed ship, [1] the optional training
 * course obstacle (0 when the scene has none). */
// GLOBAL: TIE95 0xF64C8
// GLOBAL: TIE98 0x6269A0
LandruHandle bpflight_fltobj_data[2];

// GLOBAL: TIE95 0xD0E1A
// GLOBAL: TIE98 0x4DF1EC
int16_t bpflight_cur_component;
// GLOBAL: TIE95 0xD0E1C
// GLOBAL: TIE98 0x4DF1F0
int16_t bpflight_active_component;

/* 4-slot star palette cross-faded into VGA colors 252..255 once per
 * primary-viewport activation. */
// GLOBAL: TIE95 0xD0E0C
// GLOBAL: TIE98 0x4DF1E0
static BPStarColor starpal[4] = {
	{ 0x0b, 0x0c, 0x17 },
	{ 0x0f, 0x0f, 0x1f },
	{ 0x18, 0x18, 0x25 },
	{ 0x25, 0x25, 0x2c },
};

/* 45 materials × 16 colors each. Swapped with global materialcolors[]
 * around each draw so the viewer uses its own palette without clobbering
 * the gameplay palette. Values copied verbatim from the retail binary's
 * static .data at 0xD0E1E; the 45×16 layout is a palette-
 * index per material-shade. Without this initialiser the blueprint/
 * training/combat viewers render ship polys in color 0 (black), visible
 * as a rotating silhouette on the tech-room viewport. */
// GLOBAL: TIE95 0xD0E1E
static uint8_t bp_materialcolors[720] = {
	0x40, 0x40, 0x41, 0x42, 0x42, 0x43, 0x44, 0x45, 0x45, 0x46, 0x47, 0x48, 0x48, 0x49, 0x4a, 0x4b, 0x50,
	0x50, 0x51, 0x52, 0x52, 0x53, 0x54, 0x55, 0x55, 0x56, 0x57, 0x58, 0x58, 0x59, 0x5a, 0x5b, 0x60, 0x60,
	0x61, 0x62, 0x62, 0x63, 0x64, 0x65, 0x65, 0x66, 0x67, 0x68, 0x68, 0x69, 0x6a, 0x6b, 0x70, 0x70, 0x71,
	0x72, 0x72, 0x73, 0x74, 0x75, 0x75, 0x76, 0x77, 0x78, 0x78, 0x79, 0x7a, 0x7b, 0x44, 0x44, 0x45, 0x46,
	0x46, 0x47, 0x48, 0x49, 0x49, 0x4a, 0x4b, 0x4c, 0x4c, 0x4d, 0x4e, 0x4f, 0x54, 0x54, 0x55, 0x56, 0x56,
	0x57, 0x58, 0x59, 0x59, 0x5a, 0x5b, 0x5c, 0x5c, 0x5d, 0x5e, 0x5f, 0x64, 0x64, 0x65, 0x66, 0x66, 0x67,
	0x68, 0x69, 0x69, 0x6a, 0x6b, 0x6c, 0x6c, 0x6d, 0x6e, 0x6f, 0x74, 0x74, 0x75, 0x76, 0x76, 0x77, 0x78,
	0x79, 0x79, 0x7a, 0x7b, 0x7c, 0x7c, 0x7d, 0x7e, 0x7f, 0x48, 0x48, 0x49, 0x49, 0x4a, 0x4a, 0x4b, 0x4b,
	0x4c, 0x4c, 0x4d, 0x4d, 0x4e, 0x4e, 0x4f, 0x4f, 0x58, 0x58, 0x59, 0x59, 0x5a, 0x5a, 0x5b, 0x5b, 0x5c,
	0x5c, 0x5d, 0x5d, 0x5e, 0x5e, 0x5f, 0x5f, 0x68, 0x68, 0x69, 0x69, 0x6a, 0x6a, 0x6b, 0x6b, 0x6c, 0x6c,
	0x6d, 0x6d, 0x6e, 0x6e, 0x6f, 0x6f, 0x78, 0x78, 0x79, 0x79, 0x7a, 0x7a, 0x7b, 0x7b, 0x7c, 0x7c, 0x7d,
	0x7d, 0x7e, 0x7e, 0x7f, 0x7f, 0x80, 0x81, 0x82, 0x82, 0x83, 0x84, 0x85, 0x85, 0x86, 0x87, 0x88, 0x88,
	0x89, 0x8a, 0x8b, 0x8b, 0x8c, 0x8d, 0x8e, 0x8e, 0x8f, 0x90, 0x91, 0x91, 0x92, 0x93, 0x94, 0x94, 0x95,
	0x96, 0x97, 0x97, 0x98, 0x99, 0x9a, 0x9a, 0x9b, 0x9c, 0x9d, 0x9d, 0x9e, 0x9f, 0xa0, 0xa0, 0xa1, 0xa2,
	0xa3, 0xa3, 0xa4, 0xa5, 0xa6, 0xa6, 0xa7, 0xa8, 0xa9, 0xa9, 0xaa, 0xab, 0xac, 0xac, 0xad, 0xae, 0xaf,
	0xaf, 0xa4, 0xa6, 0xa8, 0xa9, 0xaa, 0xaa, 0xab, 0xab, 0xac, 0xac, 0xad, 0xad, 0xae, 0xae, 0xaf, 0xaf,
	0x70, 0x70, 0x71, 0x72, 0x72, 0x73, 0x74, 0x75, 0x75, 0x76, 0x77, 0x78, 0x78, 0x79, 0x7a, 0x7b, 0x74,
	0x74, 0x75, 0x76, 0x76, 0x77, 0x78, 0x79, 0x79, 0x7a, 0x7b, 0x7c, 0x7c, 0x7d, 0x7e, 0x7f, 0x78, 0x78,
	0x79, 0x79, 0x7a, 0x7a, 0x7b, 0x7b, 0x7c, 0x7c, 0x7d, 0x7d, 0x7e, 0x7e, 0x7f, 0x7f, 0xf8, 0xf8, 0xf8,
	0xf8, 0xf8, 0xf8, 0xf8, 0xf8, 0xf8, 0xf8, 0xf8, 0xf8, 0xf8, 0xf8, 0xf8, 0xf8, 0xf9, 0xf9, 0xf9, 0xf9,
	0xf9, 0xf9, 0xf9, 0xf9, 0xf9, 0xf9, 0xf9, 0xf9, 0xf9, 0xf9, 0xf9, 0xf9, 0xfa, 0xfa, 0xfa, 0xfa, 0xfa,
	0xfa, 0xfa, 0xfa, 0xfa, 0xfa, 0xfa, 0xfa, 0xfa, 0xfa, 0xfa, 0xfa, 0x8c, 0x8c, 0x8d, 0x8d, 0x8e, 0x8e,
	0x8f, 0x8f, 0x90, 0x90, 0x91, 0x91, 0x92, 0x92, 0x93, 0x93, 0x8f, 0x8f, 0x90, 0x90, 0x90, 0x91, 0x91,
	0x91, 0x92, 0x92, 0x92, 0x93, 0x93, 0x93, 0x94, 0x94, 0x92, 0x92, 0x93, 0x93, 0x93, 0x94, 0x94, 0x94,
	0x95, 0x95, 0x95, 0x96, 0x96, 0x96, 0x97, 0x97, 0x7b, 0x7b, 0x7c, 0x7c, 0x7c, 0x7d, 0x7d, 0x7d, 0x7e,
	0x7e, 0x7e, 0x7e, 0x7f, 0x7f, 0x7f, 0x7f, 0x41, 0x41, 0x42, 0x43, 0x43, 0x44, 0x45, 0x46, 0x46, 0x47,
	0x48, 0x49, 0x49, 0x4a, 0x4b, 0x4c, 0x51, 0x51, 0x52, 0x53, 0x53, 0x54, 0x55, 0x56, 0x56, 0x57, 0x58,
	0x59, 0x59, 0x5a, 0x5b, 0x5c, 0x61, 0x61, 0x62, 0x63, 0x63, 0x64, 0x65, 0x66, 0x66, 0x67, 0x68, 0x69,
	0x69, 0x6a, 0x6b, 0x6c, 0x71, 0x71, 0x72, 0x73, 0x73, 0x74, 0x75, 0x76, 0x76, 0x77, 0x78, 0x79, 0x79,
	0x7a, 0x7b, 0x7c, 0x42, 0x42, 0x43, 0x44, 0x44, 0x45, 0x46, 0x47, 0x47, 0x48, 0x49, 0x4a, 0x4a, 0x4b,
	0x4c, 0x4d, 0x52, 0x52, 0x53, 0x54, 0x54, 0x55, 0x56, 0x57, 0x57, 0x58, 0x59, 0x5a, 0x5a, 0x5b, 0x5c,
	0x5d, 0x62, 0x62, 0x63, 0x64, 0x64, 0x65, 0x66, 0x67, 0x67, 0x68, 0x69, 0x6a, 0x6a, 0x6b, 0x6c, 0x6d,
	0x72, 0x72, 0x73, 0x74, 0x74, 0x75, 0x76, 0x77, 0x77, 0x78, 0x79, 0x7a, 0x7a, 0x7b, 0x7c, 0x7d, 0x43,
	0x43, 0x44, 0x45, 0x45, 0x46, 0x47, 0x48, 0x48, 0x49, 0x4a, 0x4b, 0x4b, 0x4c, 0x4d, 0x4e, 0x53, 0x53,
	0x54, 0x55, 0x55, 0x56, 0x57, 0x58, 0x58, 0x59, 0x5a, 0x5b, 0x5b, 0x5c, 0x5d, 0x5e, 0x63, 0x63, 0x64,
	0x65, 0x65, 0x66, 0x67, 0x68, 0x68, 0x69, 0x6a, 0x6b, 0x6b, 0x6c, 0x6d, 0x6e, 0x73, 0x73, 0x74, 0x75,
	0x75, 0x76, 0x77, 0x78, 0x78, 0x79, 0x7a, 0x7b, 0x7b, 0x7c, 0x7d, 0x7e, 0x98, 0x98, 0x99, 0x99, 0x9a,
	0x9a, 0x9b, 0x9b, 0x9c, 0x9c, 0x9d, 0x9d, 0x9e, 0x9e, 0x9f, 0x9f, 0x9b, 0x9b, 0x9c, 0x9c, 0x9c, 0x9d,
	0x9d, 0x9d, 0x9e, 0x9e, 0x9e, 0x9f, 0x9f, 0x9f, 0xa0, 0xa0, 0x9e, 0x9e, 0x9f, 0x9f, 0x9f, 0xa0, 0xa0,
	0xa0, 0xa1, 0xa1, 0xa1, 0xa2, 0xa2, 0xa2, 0xa3, 0xa3, 0x80, 0x80, 0x81, 0x81, 0x82, 0x82, 0x83, 0x83,
	0x84, 0x84, 0x85, 0x85, 0x86, 0x86, 0x87, 0x87, 0x83, 0x83, 0x84, 0x84, 0x84, 0x85, 0x85, 0x85, 0x86,
	0x86, 0x86, 0x87, 0x87, 0x87, 0x88, 0x88, 0x86, 0x86, 0x87, 0x87, 0x87, 0x88, 0x88, 0x88, 0x89, 0x89,
	0x89, 0x8a, 0x8a, 0x8a, 0x8b, 0x8b,
};

// GLOBAL: TIE98 0x4FA680
static uint8_t bpflight_palette_rgb[256 * 3];
// GLOBAL: TIE98 0x4FA9C8
static uint8_t bpflight_inverse_palette[65536];

/* Per-material color offset for the training / combat rooms. */
// GLOBAL: TIE95 0xD10EE
static uint8_t trainroommapping[39] = {
	0x00, 0x10, 0x10, 0x20, 0x00, 0x10, 0x10, 0x20, 0x00, 0x10, 0x10, 0x20, 0x20,
	0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x00, 0x00, 0x00, 0x20, 0x20, 0x20,
	0x20, 0x00, 0x10, 0x10, 0x20, 0x00, 0x10, 0x10, 0x20, 0x00, 0x10, 0x10, 0x20,
};
// GLOBAL: TIE95 0xD1115
static uint8_t combatroommapping[39] = {
	0x00, 0x10, 0x20, 0x30, 0x00, 0x10, 0x20, 0x30, 0x00, 0x10, 0x20, 0x30, 0x30,
	0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x00, 0x00, 0x00, 0x30, 0x30, 0x30,
	0x30, 0x00, 0x10, 0x20, 0x30, 0x00, 0x10, 0x20, 0x30, 0x00, 0x10, 0x20, 0x30,
};

/* Viewport actors + orbit Matrix + primary object buffer. */
/* Viewport actors (primary, secondary thumbnail, blueprint) + orbit
 * Matrix + primary object buffer. */
// GLOBAL: TIE95 0xF64B8
// GLOBAL: TIE98 0x4FA648
static Actor* engine[3];
// GLOBAL: TIE95 0xF64C4
// GLOBAL: TIE98 0x4FA654
static Matrix* matrix;

/* Scanline transform scratch (65000 bytes). */
// GLOBAL: TIE95 0xD0E18
// GLOBAL: TIE98 0x50A9D0
static LandruHandle xtransdata;

/* Per-viewport camera state arrays (3 slots). Originally 16-bit Q16.0
 * for angles (0x10000 ≈ 360°) and Q16.16 signed for positions. */
// GLOBAL: TIE95 0xF64D8
// GLOBAL: TIE98 0x4FA618
static int32_t bpcamerax[3];
// GLOBAL: TIE95 0xF64E4
// GLOBAL: TIE98 0x4FA608
static int32_t bpcameray[3];
// GLOBAL: TIE95 0xF64CC
// GLOBAL: TIE98 0x4FA5F8
static int32_t bpcameraz[3];
// GLOBAL: TIE95 0xF653C
// GLOBAL: TIE98 0x4FA638
static uint16_t bpcamerapitch[3];
// GLOBAL: TIE95 0xF6506
// GLOBAL: TIE98 0x4FA9B0
static uint16_t bpcameraheading[3];
// GLOBAL: TIE95 0xF655A
// GLOBAL: TIE98 0x4FA640
static uint16_t bpcameraroll[3];
// GLOBAL: TIE95 0xF6560
// GLOBAL: TIE98 0x4FA628
static uint16_t bpcamerayaw[3];
// GLOBAL: TIE95 0xF654E
// GLOBAL: TIE98 0x4FA658
static uint16_t bpcameralookpitch[3];
// GLOBAL: TIE95 0xF650C
// GLOBAL: TIE98 0x4FA678
static uint16_t bpcameralookclock[3];

/* Per-viewport runtime state. */
// GLOBAL: TIE95 0xF6500
// GLOBAL: TIE98 0x4FA9B8
static int16_t bpshipstate[3]; /* 1 = draw the ship this frame */
// GLOBAL: TIE95 0xF6536
// GLOBAL: TIE98 0x4FA980
static int16_t bpused[3]; /* 1 = viewport is active */
// GLOBAL: TIE95 0xF6548
// GLOBAL: TIE98 0x4FA990
static uint16_t bpid[3]; /* mode tag copied into actor->id */

/* Save / restore + per-frame scratch. */
// GLOBAL: TIE95 0xF64F0
// GLOBAL: TIE98 0x50A9C8
static int fullstarupdate;
// GLOBAL: TIE95 0xF6566
// GLOBAL: TIE98 0x4FA668
static int16_t cur_flight_scene;
// GLOBAL: TIE95 0xF6568
// GLOBAL: TIE98 0x4FA5F4
static int16_t objectloadsize; /* nonzero while the active preview model is loaded */

/* cameraX/Y/Z here are the *scene* camera (distinct from the per-viewport
 * bpcameraX/Y/Z and from the game's shared worldX/Y/Z in tie.c). */
// GLOBAL: TIE95 0xF64F4
// GLOBAL: TIE98 0x4FA5E8
static int32_t scene_camerax;
// GLOBAL: TIE95 0xF64F8
// GLOBAL: TIE98 0x4FA5F0
static int32_t scene_cameray;
// GLOBAL: TIE95 0xF64FC
// GLOBAL: TIE98 0x4FA5EC
static int32_t scene_cameraz;
// GLOBAL: TIE95 0xF656A
// GLOBAL: TIE98 0x4FA66C
static int16_t scene_cameraroll;
// GLOBAL: TIE95 0xF656C
// GLOBAL: TIE98 0x4FA624
static int16_t scene_cameraheading;
// GLOBAL: TIE95 0xF656E
// GLOBAL: TIE98 0x4FA670
static int16_t scene_cameralookclock;
// GLOBAL: TIE95 0xF6570
// GLOBAL: TIE98 0x4FA660
static int16_t scene_camerapitch;
// GLOBAL: TIE95 0xF6572
// GLOBAL: TIE98 0x4FA9C0
static int16_t scene_cameralookpitch;
// GLOBAL: TIE95 0xF6574
// GLOBAL: TIE98 0x4FA664
static int16_t scene_camerayaw;

/* ----- Forward decls for callbacks registered with the actor system. ----- */

static int16_t bpflight_user_Engine(Actor* actor, int32_t time);
static int16_t bpflight_draw_Engine(Actor* actor, Rect* clip, Rect* dest, int16_t xoff, int16_t yoff,
									int16_t refresh);
static int16_t bpflight_draw_Engine_tie98(Actor* actor, Rect* clip, Rect* dest, int16_t xoff, int16_t yoff,
										  int16_t refresh);
static void bpflight_Load_Flight_Craft_tie98(const char* lfd_name, const char* opt_name, int16_t model_slot,
											 int scene);

/* ----- BPFLIGHT_Open_Flight_Engine (0x7A3A0) ----- */

// FUNCTION: TIE95 0x78E70
// FUNCTION: TIE98 0x404D60
void bpflight_Open_Flight_Engine(int16_t scene) {
	Rect r;
	int16_t i;
	int16_t j;

	/* Save + overwrite the per-screen resolution selector, then reinit
	 * the perspective/projection constants for flight mode. */
	flightResolution = frontResolution;
	tie_InitFlightResolution();
	matrix = NULL;
	cur_flight_scene = scene;

	switch (scene) {
		case 3:
			/* Blueprint viewer: single full-area viewport, no orbit matrix.
			 * Z plane 20 places it above the UI chrome. */
			xrect_Set_Rect(&r, TIE_FRONTEND_EDITION(131, 222), TIE_FRONTEND_EDITION(30, 75),
						   TIE_FRONTEND_EDITION(278, 570), TIE_FRONTEND_EDITION(200, 310));
			engine[0] = xactcust_Alloc_Custom_Actor(LANDRU_NULL_HANDLE, &r, 0, 0, 20);
			xactor_Set_Actor_User_Function(engine[0], (xactorCallback)bpflight_user_Engine);
			xactor_Set_Actor_Draw_Function(engine[0], bpflight_draw_Engine);
			engine[0]->id = 2;
			bpid[2] = 2;
			bpused[2] = 1;
			bpused[1] = 0;
			bpused[0] = 0;
			bpflight_cur_component = -1;
			bpflight_active_component = -1;
			break;
		case 1: {
			/* Training room: primary + small thumbnail viewport, loads the
			 * orbit matrix from matrix.lfd entry "trnfly1". */
			ResFile* rf = shellext_Open_Empire_Resource("matrix.lfd");
			matrix = matrix_Res_Matrix(rf, "trnfly1");
			xres_Close_Resource(rf);

			xrect_Set_Rect(&r, TIE_FRONTEND_EDITION(62, 144), TIE_FRONTEND_EDITION(4, 60),
						   TIE_FRONTEND_EDITION(256, 500), TIE_FRONTEND_EDITION(116, 300));
			engine[0] = xactcust_Alloc_Custom_Actor(LANDRU_NULL_HANDLE, &r, 0, 0, 10);
			xactor_Set_Actor_User_Function(engine[0], (xactorCallback)bpflight_user_Engine);
			xactor_Set_Actor_Draw_Function(engine[0], bpflight_draw_Engine);
			engine[0]->id = 0;
			bpid[0] = 0;
			bpused[0] = 1;

			xrect_Set_Rect(&r, TIE_FRONTEND_EDITION(85, 176), TIE_FRONTEND_EDITION(131, 340),
						   TIE_FRONTEND_EDITION(182, 358), TIE_FRONTEND_EDITION(178, 449));
			engine[1] = xactcust_Alloc_Custom_Actor(LANDRU_NULL_HANDLE, &r, 0, 0, 10);
			xactor_Set_Actor_User_Function(engine[1], (xactorCallback)bpflight_user_Engine);
			xactor_Set_Actor_Draw_Function(engine[1], bpflight_draw_Engine);
			engine[1]->id = 1;
			bpid[1] = 1;
			bpused[1] = 1;
			bpused[2] = 0;

			shipext_Init_Train_Ship_Name();
			bpflight_cur_component = -1;
			bpflight_active_component = -1;
			break;
		}
		case 2: {
			/* Combat room: primary + thumbnail, "cmbtfly1" orbit. */
			ResFile* rf = shellext_Open_Empire_Resource("matrix.lfd");
			matrix = matrix_Res_Matrix(rf, "cmbtfly1");
			xres_Close_Resource(rf);

			xrect_Set_Rect(&r, TIE_FRONTEND_EDITION(59, 124), TIE_FRONTEND_EDITION(2, 7),
						   TIE_FRONTEND_EDITION(260, 516), TIE_FRONTEND_EDITION(115, 272));
			engine[0] = xactcust_Alloc_Custom_Actor(LANDRU_NULL_HANDLE, &r, 0, 0, 10);
			xactor_Set_Actor_User_Function(engine[0], (xactorCallback)bpflight_user_Engine);
			xactor_Set_Actor_Draw_Function(engine[0], bpflight_draw_Engine);
			engine[0]->id = 0;
			bpid[0] = 0;
			bpused[0] = 1;

			xrect_Set_Rect(&r, TIE_FRONTEND_EDITION(146, 297), TIE_FRONTEND_EDITION(130, 313),
						   TIE_FRONTEND_EDITION(247, 485), TIE_FRONTEND_EDITION(179, 440));
			engine[1] = xactcust_Alloc_Custom_Actor(LANDRU_NULL_HANDLE, &r, 0, 0, 10);
			xactor_Set_Actor_User_Function(engine[1], (xactorCallback)bpflight_user_Engine);
			xactor_Set_Actor_Draw_Function(engine[1], bpflight_draw_Engine);
			engine[1]->id = 1;
			bpid[1] = 1;
			bpused[1] = 1;
			bpused[2] = 0;

			shipext_Init_Combat_Ship_Name();
			bpflight_cur_component = -1;
			bpflight_active_component = -1;
			break;
		}
	}

	/* XTRANS2 scanline scratch. Big enough for a full-screen viewport. */
	xtransdata = xmemhdl_Alloc_Handle(65000, LANDRU_MEMORY_RESOURCE);

	flightbuf_small_handle = xmemhdl_Alloc_Handle((uint32_t)(TRACE2_EDGEINFO_CAP * sizeof(trace2_EdgeInfo)),
												  LANDRU_MEMORY_RESOURCE);
	flightbuf_big_handle = xmemhdl_Alloc_Handle((uint32_t)(TRACE2_EDGEHEADER_CAP * sizeof(trace2_EdgeHeader)),
												LANDRU_MEMORY_RESOURCE);

	/* Object-buffer heaps differ per scene:
	 *   scene 3        : 33000 bytes (blueprint has the largest models)
	 *   scene 1        : 15000 bytes + 9000-byte obstacle heap
	 *   scene 2        : 15000 bytes (no obstacle) */
	if (scene == 3) {
		bpflight_fltobj_data[0] = xmemhdl_Alloc_Handle(33000, LANDRU_MEMORY_RESOURCE);
		bpflight_fltobj_data[1] = 0;
	} else {
		bpflight_fltobj_data[0] = xmemhdl_Alloc_Handle(15000, LANDRU_MEMORY_RESOURCE);
		if (scene == 1)
			bpflight_fltobj_data[1] = xmemhdl_Alloc_Handle(9000, LANDRU_MEMORY_RESOURCE);
		else
			bpflight_fltobj_data[1] = 0;
	}

	/* Kick a SHIP resource into fltobj_data via SHIPEXT; training adds
	 * the course obstacle. */
	switch (scene) {
		case 3:
			shipext_Get_Blueprint_Ship_SHP();
			break;
		case 1:
			shipext_Get_Train_Ship_SHP();
			shipext_Get_Train_Course_SHP();
			break;
		case 2:
			shipext_Get_Combat_Ship_SHP();
			break;
	}

	/* Pre-fill XTRANS2's span mask from the last viewport rect: one
	 * entry per row, [1, w] when the width fits in a byte, otherwise
	 * [1, 0, w - overflow]. */
	{
		uint8_t* mask;
		int16_t y;
		int16_t width;

		xtransdataptr = xmemhdl_Lock_Handle(xtransdata);
		width = r.right - r.left;
		mask = (uint8_t*)xtransdataptr + (uint16_t)maskbufptr;
		for (y = r.top; y < r.bottom; ++y) {
			*mask++ = 1;
			if (width > 0xFF) {
				*mask++ = 0;
				width -= TIE_FRONTEND_EDITION(0x100, 0xFF);
			}
			*mask++ = (uint8_t)width;
		}
		xmemhdl_Unlock_Handle(xtransdata);
	}

	/* Light direction: symmetric, pointing into +X+Y+Z. 0x49E6 ≈ 0.577
	 * in Q15 (roughly 1/sqrt(3) for a uniform diagonal light). */
	lightX = 0x49E6;
	lightY = 0x49E6;
	lightZ = 0x49E6;

	/* Seed 256 (pos, brightness) pairs at stride 2 into the tie.c-owned
	 * stars[512] buffer. pos is 6-bit (0..63), brightness is 3-bit (0..7). */
	for (i = 0; i < 511; i += 2) {
		stars[i] = (uint8_t)(rand() & 0x3F);
		stars[i + 1] = (uint8_t)(rand() & 0x07);
	}

	/* Default camera pose per active viewport:
	 *   pitch=0x4000 (≈ horizon), heading/roll/yaw/look*=0
	 *   position: x=0, y=-4096 (elevated), z=0
	 *   id==1/2 override: y=-2048, pitch=0x4000; id==2 uses pitch=19456
	 *   (≈107°), id==1 starts with heading 0x6000 (≈135°). */
	for (j = 0; j < 3; ++j) {
		if (!bpused[j])
			continue;
		bpcamerapitch[j] = 0x4000;
		bpcameraheading[j] = 0;
		bpcameraroll[j] = 0;
		bpcamerayaw[j] = 0;
		bpcameralookpitch[j] = 0;
		bpcameralookclock[j] = 0;
		bpcamerax[j] = 0;
		bpcameray[j] = -4096;
		bpcameraz[j] = 0;

		if (bpid[j] != 0) {
			bpcameray[j] = -2048;
			bpflight_pivotpitch[j] = 0x4000;
			if (bpid[j] == 2)
				bpflight_pivotpitch[j] = 19456;
			else
				bpflight_pivotheading[j] = 24576;
		}
	}

	/* Per-frame render flags. */
	if (!TIE_FRONTEND_TIE98)
		fullupdateflag = 1;
	starshipdetail = 4;
	if (TIE_FRONTEND_TIE98)
		g_flightInitialTextureCacheFlushPending = 1;
	fullstarupdate = 1;
	bpflightflag = 1;
	stardetaillevel = 1;
	shipdetailvalue = TIE_FRONTEND_EDITION(0, -1);
	shipdetailpolycnt = 16;
	drawmarkingsflag = 1;
	gouraudflag = 64;
	lightflag = 1;
	bpflight_Start_Movie_Engine();
}

/* ----- BPFLIGHT_Close_Flight_Engine (0x7A948) ----- */

// FUNCTION: TIE95 0x79414
void bpflight_Close_Flight_Engine(void) {
	TieFlightAssets_ClearPreviewModels();
#ifdef TIE_MODERN
	/* The host cache owns TIE98 preview models beyond the active pointers. */
	if (TIE_FRONTEND_TIE98)
		TieFlightRuntime_ReleaseRecoveredResources();
#endif
	xmemhdl_Free_Handle(xtransdata);
	xmemhdl_Free_Handle(flightbuf_small_handle);
	xmemhdl_Free_Handle(flightbuf_big_handle);
	xmemhdl_Free_Handle(bpflight_fltobj_data[0]);
	if (bpflight_fltobj_data[1])
		xmemhdl_Free_Handle(bpflight_fltobj_data[1]);
#ifdef TIE_MODERN
	/* PORT: clear the released handles so a later release is a no-op. */
	xtransdata = LANDRU_NULL_HANDLE;
	bpflight_fltobj_data[0] = LANDRU_NULL_HANDLE;
	bpflight_fltobj_data[1] = LANDRU_NULL_HANDLE;
	flightbuf_small_handle = LANDRU_NULL_HANDLE;
	flightbuf_big_handle = LANDRU_NULL_HANDLE;
#endif
	if (matrix) {
		matrix_Free_Matrix(matrix);
		matrix = NULL;
	}
	bpflightflag = 0;
#ifdef TIE_MODERN
	/* PORT: the flight profile replaces the f_res preference. */
	flightResolution = (int16_t)TieClassicDisplay_FlightMode();
#elif defined(TIE98)
	switch (f_res) {
		case 0:
			flightResolution = TIE_FLIGHT_RES_VGA;
			break;
		case 1:
			flightResolution = TIE_FLIGHT_RES_SVGA;
			break;
		case 2:
			flightResolution = TIE_FLIGHT_RES_SVGA_16;
			break;
		case 3:
			flightResolution = TIE_FLIGHT_RES_SVGA_D3D;
			break;
	}
#else
	if (f_res == 0)
		flightResolution = TIE_FLIGHT_RES_VGA;
	else if (f_res == 1)
		flightResolution = TIE_FLIGHT_RES_SVGA;
#endif
	/* Restore the framebuffer alias retail rebinds to 0xA0000 here.
	 * In the SDL port that's vesa_buff_gbl (the Landru framebuffer that
	 * the presenter scans out). Leaving it NULL would segfault every
	 * post-flight rtsvga2/xtrans2 write into the HUD or 2D screens. */
	xtrans2_videobaseptr = vesa_buff_gbl;
}

/* ----- BPFLIGHT_Open_New_Matrix (0x7A9B0) ----- */

// FUNCTION: TIE95 0x794B4
void bpflight_Open_New_Matrix(const char* name) {
	ResFile* rf;

	if (matrix) {
		matrix_Free_Matrix(matrix);
		matrix = NULL;
	}
	rf = shellext_Open_Empire_Resource("matrix.lfd");
	matrix = matrix_Res_Matrix(rf, name);
	xres_Close_Resource(rf);
}

/* ----- BPFLIGHT_Start_Movie_Engine (0x7A9F0) ----- */

// FUNCTION: TIE95 0x794F4
void bpflight_Start_Movie_Engine(void) {
	bpshipstate[1] = 1;
	bpshipstate[2] = 1;
	bpshipstate[0] = 1;
	if (engine[0] && engine[0]->id == 0)
		engine[0]->var1 = 0;
}

/* ----- BPFLIGHT_Stop_Movie_Engine (0x7AA28) ----- */

// FUNCTION: TIE95 0x7952C
void bpflight_Stop_Movie_Engine(void) {
	/* Blueprint scene never stops: the ship keeps rendering even when
	 * the orbit is paused because BLUEPRNT drives the camera manually. */
	if (cur_flight_scene != 3)
		bpshipstate[0] = 0;
	if (engine[0] && engine[0]->id == 0)
		engine[0]->var1 = 0;
}

/* ----- BPFLIGHT_user_Engine (0x7AA5C) ----- */

// FUNCTION: TIE95 0x79560
static int16_t bpflight_user_Engine(Actor* actor, int32_t time) {
	/* Primary viewport frame 1 only: program 4 star palette slots
	 * (VGA colors 252..255). Used to cross-fade the star colors. */
	if (actor->id == 0 && time == 1) {
		int16_t color_slot = 252;
		int16_t i;

		for (i = 0; i < 4; i++) {
			xpal_Set_Screen_RGB(color_slot, color_slot, starpal[i].r, starpal[i].g, starpal[i].b);
			xpal_Set_Src_Pal_Color(color_slot, color_slot, starpal[i].r, starpal[i].g, starpal[i].b);
			xpal_Set_Dest_Pal_Color(color_slot, color_slot, starpal[i].r, starpal[i].g, starpal[i].b);
			color_slot++;
		}
	}

	/* Animate the active viewport(s). */
	if (bpshipstate[actor->id]) {
		switch (actor->id) {
			case 0:
				/* Primary: play the orbit matrix, advance the frame cursor. */
				fview_newcalcview(bpcameraroll[actor->id], bpcamerapitch[actor->id],
								  bpcameraheading[actor->id], bpcamerayaw[actor->id],
								  bpcameralookpitch[actor->id], bpcameralookclock[actor->id], NULL);
				actor->var1 = (int16_t)((actor->var1 + 1) % matrix->frame_count);
				break;
			case 1:
				/* Secondary thumbnail: slow heading drift (+0x400 every
				 * 16 of 64 frames). */
				if ((time & 0x3F) < 0x10)
					bpflight_pivotheading[actor->id] += 0x400;
				break;
			case 2:
				/* Blueprint full-screen: fast heading spin (+0x1F0 / frame)
				 * and pitch oscillation (+/-64 each 64-frame phase). */
				bpflight_pivotheading[actor->id] = bpflight_pivotheading[actor->id] + 0x1F0;
				if (time & 0x40)
					bpflight_pivotpitch[actor->id] += 64;
				else
					bpflight_pivotpitch[actor->id] -= 64;
				break;
		}
	}

	/* Component-highlight blink: show the active component for 8 ticks,
	 * hide for 8. Drives DRAWPOL's component-colour override. */
	if ((time & 0x0F) < 8)
		bpflight_cur_component = bpflight_active_component;
	else
		bpflight_cur_component = -1;
	return 0;
}

/* ----- BPFLIGHT_draw_Engine (0x7AC50) -----
 *
 * Main render. Conforms to the xactorDrawFunc ABI even though only actor
 * and clip are used. */
// FUNCTION: TIE98 0x405640
// BPFLIGHT_draw_Engine
static int16_t bpflight_draw_Engine_tie98(Actor* actor, Rect* clip, Rect* dest, int16_t xoff, int16_t yoff,
										  int16_t refresh) {
	MatrixFrame frame;
	BitmapStruct* bitmap;
	uint16_t width;
	uint16_t height;
	uint8_t* mask;
	uint16_t row;
	int16_t object_count;
	uint8_t saved_deepspace_color;

	(void)dest;
	(void)xoff;
	(void)yoff;
	(void)refresh;

	if (!objectloadsize) {
		xpaint_Paint_Clipped_Rect(clip, 0);
		return 0;
	}

	xdirty_Dirty_Rect(clip);

	if (actor->id == 0) {
		matrix_Get_Matrix_Frame(matrix, &frame, actor->var1);
		scene_cameraz = frame.cam_z;
		scene_camerax = frame.cam_x;
		scene_cameray = frame.cam_y;
		scene_cameraheading = frame.cam_heading;
		scene_camerapitch = frame.cam_pitch;
		scene_cameraroll = frame.cam_roll;
		scene_camerayaw = 0;
		scene_cameralookpitch = 0;
		scene_cameralookclock = 0;
		fview_newcalcview(frame.cam_roll, frame.cam_pitch, frame.cam_heading, 0, 0, 0, NULL);
	} else {
		const int16_t id = actor->id;
		scene_camerax = bpcamerax[id];
		scene_cameray = bpcameray[id];
		scene_cameraz = bpcameraz[id];
		scene_camerapitch = bpcamerapitch[id];
		scene_cameraheading = bpcameraheading[id];
		scene_cameraroll = bpcameraroll[id];
		scene_camerayaw = bpcamerayaw[id];
		scene_cameralookpitch = bpcameralookpitch[id];
		scene_cameralookclock = bpcameralookclock[id];
		fview_newcalcview(bpcameraroll[id], bpcamerapitch[id], bpcameraheading[id], bpcamerayaw[id],
						  bpcameralookpitch[id], bpcameralookclock[id], NULL);
	}

	xtransdataptr = xmemhdl_Lock_Handle(xtransdata);
	bitmap = xcanvas_Get_Current_Canvas_Bitmap();
	xtrans2_videobaseptr = (uint8_t*)xbm_Lock_Bitmap(bitmap);
	buffer_ptr = xtrans2_videobaseptr;
	width = (uint16_t)(clip->right - clip->left);
	height = (uint16_t)(clip->bottom - clip->top);
	mask = (uint8_t*)xtransdataptr + (uint16_t)maskbufptr;
	for (row = 0; row < height; ++row) {
		*mask++ = 1;
		if (width > 0xFF) {
			*mask++ = 0;
			*mask++ = (uint8_t)(width + 1);
		} else {
			*mask++ = (uint8_t)width;
		}
	}
	g_surfacePitch = bitmap->w;
	/* PORT: the TIE98 frontend display mode owns the indexed pixel format;
	 * this preview only redirects the destination to its Landru canvas. */
	logbuf2_setbufferdimensions_tie98(width, height, 1,
									  (uint32_t)clip->left + (uint32_t)bitmap->w * clip->top);
	RenderScene_Initialize_tie98(1);

	object_count = actor->id != 0 ? 1 : matrix->matrix_count;
	transfm2_screenyoffset = 0;
	objectsize = 0x7FFF;
	if (bpshipstate[actor->id] && object_count > 0) {
		int16_t object_index;

		for (object_index = 0; object_index < object_count; ++object_index) {
			int model_slot;
			const Tie98OptimizedPolyObject* saved_model_override;

			if (actor->id == 0) {
				bpflight_Position_Craft(&frame, object_index);
				model_slot = TieFlightAssets_PreviewModel(1) != NULL;
			} else {
				switch (cur_flight_scene) {
					case 1:
						shipext_Get_Train_Ship_Pos(&worldx, &worldy, &worldz);
						break;
					case 2:
						shipext_Get_Combat_Ship_Pos(&worldx, &worldy, &worldz);
						break;
					case 3:
						worldx = 0;
						worldz = 0;
						worldy = scene_cameray + (TieFlightAssets_PreviewModelMaxExtent(0) << 9) / 200;
						break;
					default:
						break;
				}
				model_slot = 0;
				fview_newcalcrotate(bpflight_pivotroll[actor->id], bpflight_pivotpitch[actor->id],
									bpflight_pivotheading[actor->id], bpflight_pivotyaw[actor->id], NULL);
			}

			camera.x = scene_camerax;
			camera.y = scene_cameray;
			camera.z = scene_cameraz;
			objects[0].world_x = worldx;
			objects[0].world_y = worldy;
			objects[0].world_z = worldz;
			objects[0].ship_idx = (uint8_t)model_slot;
			objects[0].genus = 0;
			objects[0].craft_ptr = &crafts[0];
			/* PORT: the retained TIE95 CraftData has 40 component slots;
			 * TIE98 clears both of its 50-byte arrays here. Preview slots 0/1
			 * do not consume component state beyond the retained capacity. */
			memset(crafts[0].mesh_state, 0, sizeof crafts[0].mesh_state);
			memset(crafts[0].mesh_rotation, 0, sizeof crafts[0].mesh_rotation);

			worldx -= scene_camerax;
			worldy -= scene_cameray;
			worldz -= scene_cameraz;
			objecteyex = transfm2_geteyex(worldx, worldy, worldz);
			objecteyey = transfm2_geteyey(worldx, worldy, worldz);
			objecteyez = transfm2_geteyez(worldx, worldy, worldz);
			parentobject = (uint16_t)(object_index + 1);
			saved_model_override = g_flightModelOverride;
			g_flightModelOverride = TieFlightAssets_PreviewModel(model_slot);
			FlightModel_Draw_Object(&objects[0]);
			g_flightModelOverride = saved_model_override;
		}
	}

	saved_deepspace_color = deepspacecolor;
	if (actor->id != 0) {
		deepspacecolor = 0;
	} else {
		drawbackdropflag = 0;
		deepspacecolor = 0;
		backdrp2_backdrop();
		rtsvga2_setvgapointers(xtrans2_videobaseptr, 640, 480);
		if (hyperspaceflag != 3 && hyperspaceflag != 5)
			rtsvga2_drawstars_tie98();
		rtsvga2_setvgapointers(NULL, 640, 480);
	}
	g_flightSurfaceAlreadyLocked = 1;
	RenderScene_DrawVisibleFaces();
	g_flightSurfaceAlreadyLocked = 0;
	RenderScene_UnlockSceneBuffers_tie98();
	xmemhdl_Unlock_Handle(xtransdata);
	deepspacecolor = saved_deepspace_color;
	xbm_Unlock_Bitmap(bitmap);
	return 0;
}

// FUNCTION: TIE95 0x7975C
static int16_t bpflight_draw_Engine(Actor* actor, Rect* clip, Rect* dest, int16_t xoff, int16_t yoff,
									int16_t refresh) {
	MatrixFrame frame;
	BitmapStruct* bm;
	uint16_t height;
	uint16_t width;
	uint8_t* mask;
	int16_t row;
	int16_t object_count;
	int16_t obj_idx;
	int16_t use_obstacle;
	uint8_t saved_deepspace;

	if (TIE_FRONTEND_TIE98)
		return bpflight_draw_Engine_tie98(actor, clip, dest, xoff, yoff, refresh);
	(void)dest;
	(void)xoff;
	(void)yoff;
	(void)refresh;

	/* No ship loaded yet → paint the viewport black and exit. */
	if (!objectloadsize) {
		xpaint_Paint_Clipped_Rect(clip, 0);
		return 0;
	}

	/* Mark the viewport dirty so XDIRTY refreshes this region. */
	xdirty_Dirty_Rect(clip);

	/* -- Camera setup -- */
	if (actor->id == 0) {
		/* Primary viewport: pull the current frame from the orbit matrix. */
		matrix_Get_Matrix_Frame(matrix, &frame, actor->var1);
		scene_camerayaw = 0;
		scene_cameralookpitch = 0;
		scene_camerax = frame.cam_x;
		scene_cameralookclock = 0;
		scene_cameray = frame.cam_y;
		scene_cameraz = frame.cam_z;
		scene_cameraheading = frame.cam_heading;
		scene_camerapitch = frame.cam_pitch;
		scene_cameraroll = frame.cam_roll;
		fview_newcalcview(frame.cam_roll, frame.cam_pitch, frame.cam_heading, 0, 0, 0, NULL);
	} else {
		/* Secondary/blueprint viewport: copy from per-viewport arrays. */
		scene_camerax = bpcamerax[actor->id];
		scene_cameray = bpcameray[actor->id];
		scene_cameraz = bpcameraz[actor->id];
		scene_camerapitch = bpcamerapitch[actor->id];
		scene_cameraheading = bpcameraheading[actor->id];
		scene_cameraroll = bpcameraroll[actor->id];
		scene_camerayaw = bpcamerayaw[actor->id];
		scene_cameralookpitch = bpcameralookpitch[actor->id];
		scene_cameralookclock = bpcameralookclock[actor->id];
		fview_newcalcview(bpcameraroll[actor->id], bpcamerapitch[actor->id], bpcameraheading[actor->id],
						  bpcamerayaw[actor->id], bpcameralookpitch[actor->id], bpcameralookclock[actor->id],
						  NULL);
	}

	/* Lock the XTRANS2 scratch + canvas; logbuf picks up videobaseptr. */
	xtransdataptr = xmemhdl_Lock_Handle(xtransdata);
	bm = xcanvas_Get_Current_Canvas_Bitmap();
	xtrans2_videobaseptr = (uint8_t*)xbm_Lock_Bitmap(bm);
	buffer_ptr = xtrans2_videobaseptr;

	/* Re-fill the XTRANS2 mask buffer for this viewport. */
	height = (uint16_t)(clip->bottom - clip->top);
	width = (uint16_t)(clip->right - clip->left);
	mask = (uint8_t*)xtransdataptr + (uint16_t)maskbufptr;
	for (row = 0; row < height; ++row) {
		*mask++ = 1;
		if (width > 0xFF) {
			*mask++ = 0;
			*mask++ = (uint8_t)(width - 0x100);
		} else {
			*mask++ = (uint8_t)width;
		}
	}

	/* Configure LOGBUF2 for this viewport + reset XTRANS2 state. */
#ifdef TIE_MODERN
	/* The host canvas stride can differ from the retail 320-byte VGA page. */
	screenMemWidth = bm->w;
	logbuf2_setbufferdimensions(width, height, (uint32_t)clip->left + (uint32_t)bm->w * clip->top);
#else
	logbuf2_setbufferdimensions(width, height, (uint16_t)(clip->top * 320 + clip->left));
#endif
	xtrans2_clearruntable();
	fullupdateflag = 0;
	xtrans2_initxtrans();

	/* Install the viewer palette, then remove the room's material offset. */
	bpflight_swapbpmaterials();
	switch (cur_flight_scene) {
		case 1:
			bpflight_settraincolors(1);
			break;
		case 2:
			bpflight_setcombatcolors(1);
			break;
	}

	/* Primary viewport animates one object per matrix joint; the others
	 * draw a single ship. */
	if (actor->id == 0)
		object_count = matrix->matrix_count;
	else
		object_count = 1;
	objectsize = 0x7FFF;
	transfm2_screenyoffset = 0;

	if (bpshipstate[actor->id]) {
		for (obj_idx = 0; obj_idx < object_count; ++obj_idx) {
			ShipModelData* smd;
			int16_t* render_start;
			uint8_t* bsp_root;

			if (actor->id == 0) {
				bpflight_Position_Craft(&frame, obj_idx);
				if (bpflight_fltobj_data[1])
					use_obstacle = 1;
				else
					use_obstacle = 0;
			} else {
				switch (cur_flight_scene) {
					case 3:
						/* Centre the model on screen with a 30-line offset. */
						objectblockptr =
							(ShipModelData*)((uint8_t*)xmemhdl_Lock_Handle(bpflight_fltobj_data[0]) + 2);
						worldx = 0;
						worldy = scene_cameray + (objectblockptr->length << 8) / 200;
						worldz = 0;
						transfm2_screenyoffset = -30;
						componentblockptr =
							(ShipModelMesh*)&objectblockptr->lod_records[objectblockptr->num_lods];
						use_obstacle = 0;
						xmemhdl_Unlock_Handle(bpflight_fltobj_data[0]);
						break;
					case 1:
						shipext_Get_Train_Ship_Pos(&worldx, &worldy, &worldz);
						use_obstacle = 0;
						break;
					case 2:
						shipext_Get_Combat_Ship_Pos(&worldx, &worldy, &worldz);
						use_obstacle = 0;
						break;
				}
				fview_newcalcrotate(bpflight_pivotroll[actor->id], bpflight_pivotpitch[actor->id],
									bpflight_pivotheading[actor->id], bpflight_pivotyaw[actor->id], NULL);
			}

			/* World-relative eye-space position (per-object). */
			worldx -= scene_camerax;
			worldy -= scene_cameray;
			worldz -= scene_cameraz;
			objecteyex = transfm2_geteyex(worldx, worldy, worldz);
			objecteyey = transfm2_geteyey(worldx, worldy, worldz);
			objecteyez = transfm2_geteyez(worldx, worldy, worldz);

			smd = (ShipModelData*)((uint8_t*)xmemhdl_Lock_Handle(bpflight_fltobj_data[use_obstacle]) + 2);
			objectblockptr = smd;
			bpflight_getrelativexyz();
			componentblockptr = (ShipModelMesh*)&smd->lod_records[objectblockptr->num_lods];

			/* Past the mesh table sits a self-relative offset to the BSP
			 * root, which follows a 2-byte size prefix. */
			render_start = (int16_t*)&objectblockptr->lod_records[objectblockptr->num_lods - 1];
			bsp_root = (uint8_t*)render_start + *render_start + 2;
			parentobject = (uint16_t)(obj_idx + 1);
			if (shellext_Get_Cur_Scene() == SCENE_TRAIN_A || shellext_Get_Cur_Scene() == SCENE_TRAIN_B)
				bpflight_drawtrainobject(bsp_root);
			else
				bpflight_drawtreeobject(bsp_root, 0, 0);
			xmemhdl_Unlock_Handle(bpflight_fltobj_data[use_obstacle]);
		}
	}

	/* Suppress deep-space colour on secondary viewports so the
	 * background renders as clean zeros. */
	saved_deepspace = deepspacecolor;
	if (actor->id != 0)
		deepspacecolor = 0;
	xtrans2_drawxtrans();

	/* Primary viewport adds the skybox + star-field over the edge list. */
	if (actor->id == 0) {
		uint16_t saved_fullupdate;

		drawbackdropflag = 0;
		backdrp2_backdrop();
		rtsvga2_setvgapointers(xtrans2_videobaseptr, TIE_FRONTEND_EDITION(320, 640),
							   TIE_FRONTEND_EDITION(200, 480));
		saved_fullupdate = fullupdateflag;
		if (fullstarupdate) {
			fullstarupdate = 0;
			fullupdateflag = 1;
		}
		rtsvga2_drawstars();
		fullupdateflag = saved_fullupdate;
		rtsvga2_setvgapointers(NULL, TIE_FRONTEND_EDITION(320, 640), TIE_FRONTEND_EDITION(200, 480));
	}

	/* Restore the room's material offset and the gameplay palette. */
	switch (cur_flight_scene) {
		case 1:
			bpflight_settraincolors(0);
			break;
		case 2:
			bpflight_setcombatcolors(0);
			break;
	}
	bpflight_swapbpmaterials();
	deepspacecolor = saved_deepspace;
	xmemhdl_Unlock_Handle(xtransdata);
	xbm_Unlock_Bitmap(bm);
	return 0;
}

// FUNCTION: TIE95 0x79DEC
void bpflight_getrelativexyz(void) {
	int32_t dx, dy, dz;
	uint16_t hx, hy, hz;
	int32_t value;

	dx = (scene_camerax - worldx) * 2;
	dz = (scene_cameraz - worldz) * 2;
	dy = (scene_cameray - worldy) * 2;
	hx = (uint16_t)(dx >> 16);
	hz = (uint16_t)(dz >> 16);
	hy = (uint16_t)(dy >> 16);
	if (hx & 0x8000)
		hx = -hx;
	if (hy & 0x8000)
		hy = -hy;
	if (hz & 0x8000)
		hz = -hz;
	hy *= 2;
	hz *= 2;
	hx *= 2;
	relativeshift = -1;
	do {
		hy >>= 1;
		hz >>= 1;
		dx >>= 1;
		dy >>= 1;
		relativeshift++;
		dz >>= 1;
		hx >>= 1;
	} while (hx || hy || hz);

	value = math2_dot3_q15_clamped(craftS1, craftS2, craftS3, (int16_t)dx, (int16_t)dy, (int16_t)dz);
	relativex = (int16_t)value;
	value = math2_dot3_q15_clamped(craftf1, craftf2, craftf3, (int16_t)dx, (int16_t)dy, (int16_t)dz);
	relativey = (int16_t)value;
	relativey = -relativey;
	value = math2_dot3_q15_clamped(craftU1, craftU2, craftU3, (int16_t)dx, (int16_t)dy, (int16_t)dz);
	relativez = (int16_t)value;
	relativeshift -= objectblockptr->model_scale_shift;
}

/* ----- BPFLIGHT_drawtreeobject (0x7BBA8) ----- */

// FUNCTION: TIE95 0x79FD4
void bpflight_drawtreeobject(void* node, int16_t pass_gated, int16_t pass_mainhull) {
	BSPNode* n = (BSPNode*)node;

	if (n->left_off == 0) {
		/* Leaf: right_off is the mesh index into componentblockptr[]. */
		int16_t leaf = n->right_off;
		ShipModelMesh* mesh = &componentblockptr[leaf];
		ShipMeshLOD* lods;
		int16_t draw;

		bluetarget = (uint16_t)-1;
		lods = (ShipMeshLOD*)((uint8_t*)mesh + mesh->render_offset);
		if (mesh->mesh_type == bpflight_cur_component) {
			currenttargetcomp = leaf;
			highlightcolor = 0;
			currenttarget = parentobject;
		} else {
			currenttarget = 1024;
		}

		/* Filter:
		 *   pass_gated == 0           → draw everything
		 *   pass_gated, mh==0         → draw non-MainHull
		 *   pass_gated, mh!=0         → draw only MainHull
		 *   MiscHull / Antenna always hidden when pass_gated != 0 */
		if (pass_gated) {
			draw = 0;
			if (pass_mainhull && mesh->mesh_type == 1 /* MESH_MainHull */)
				draw = 1;
			if (!pass_mainhull && mesh->mesh_type != 1 /* MESH_MainHull */)
				draw = 1;
			if (mesh->mesh_type == 18 /* MESH_MiscHull */)
				draw = 0;
			if (mesh->mesh_type == 19 /* MESH_Antenna  */)
				draw = 0;
		} else {
			draw = 1;
		}

		if (draw) {
			const uint16_t* poly = draw_getdetailptr(lods, objecteyez);
			drawpol_drawpolyobject(poly, objecteyex, objecteyey, objecteyez);
		}
	} else {
		int16_t dx, dy, dz;
		int32_t plane_eq;

		dx = n->center_x;
		dy = n->center_y;
		dz = n->center_z;
		dx >>= relativeshift;
		dy >>= relativeshift;
		dz >>= relativeshift;
		dx = relativex - dx;
		dy = relativey - dy;
		dz = relativez - dz;
		/* Plane dot-product sign selects which child is "far" (draw
		 * first) and which is "near" (drawn last). */
		plane_eq = (n->normal_x >> relativeshift) * dx + (n->normal_y >> relativeshift) * dy +
				   (n->normal_z >> relativeshift) * dz;
		if (plane_eq >= 0x40000000)
			plane_eq = 0x3FFF0000;
		if (plane_eq <= -0x40000000)
			plane_eq = -0x3FFF0000;
		if ((int16_t)(plane_eq >> 15) >= 0) {
			bpflight_drawtreeobject((uint8_t*)n + n->left_off, pass_gated, pass_mainhull);
			bpflight_drawtreeobject((uint8_t*)n + n->right_off, pass_gated, pass_mainhull);
		} else {
			bpflight_drawtreeobject((uint8_t*)n + n->right_off, pass_gated, pass_mainhull);
			bpflight_drawtreeobject((uint8_t*)n + n->left_off, pass_gated, pass_mainhull);
		}
	}
}

/* ----- BPFLIGHT_drawtrainobject (0x7BDAC) -----
 *
 * Inlined two-pass BSP walker (accessories then MainHull). Equivalent to:
 *   bpflight_drawtreeobject(node, 1, 0);
 *   bpflight_drawtreeobject(node, 1, 1);
 * The binary unrolled both passes to save the recursive call in each
 * traversal step. No xrefs in the demo. */
// FUNCTION: TIE95 0x7A1D8
void bpflight_drawtrainobject(void* node) {
	bpflight_drawtreeobject(node, 1, 0);
	bpflight_drawtreeobject(node, 1, 1);
}

/* ----- BPFLIGHT_Load_Flight_Craft (0x7C1BC) ----- */

// FUNCTION: TIE98 0x405BE0
// BPFLIGHT_Load_Flight_Craft
static void bpflight_Load_Flight_Craft_tie98(const char* lfd_name, const char* opt_name, int16_t model_slot,
											 int scene) {
	Palette* palette;
	int palette_changed;
	int index;

	(void)lfd_name;
	/* PORT: the original frees and reloads a movable OPT handle in
	 * g_loaded_model_handles[model_slot]. The host native-OPT cache owns an
	 * immutable equivalent for the same IVFILES name. */
	g_inversePaletteTable = bpflight_inverse_palette;
	palette = xpal_Get_Dest_Palette();
	palette_changed = 0;
	for (index = 0; index < 256; ++index) {
		int16_t red = 0;
		int16_t green = 0;
		int16_t blue = 0;
		uint8_t* previous;

		xpal_Get_Palette_Index_RGB(palette, &red, &green, &blue, (int16_t)index);
		previous = &bpflight_palette_rgb[3 * index];
		if (previous[0] != red || previous[1] != green || previous[2] != blue)
			palette_changed = 1;
		previous[0] = (uint8_t)red;
		previous[1] = (uint8_t)green;
		previous[2] = (uint8_t)blue;
	}
	if (palette_changed) {
		const char* inverse_palette_file = NULL;
		switch (scene) {
			case 1:
				inverse_palette_file = "Mission/TrainingRoom.inv";
				break;
			case 2:
				inverse_palette_file = "Mission/CombatChamber.inv";
				break;
			case 3:
				inverse_palette_file = "Mission/TechRoom.inv";
				break;
			default:
				break;
		}
		if (inverse_palette_file) {
			fediskio_readfiletofarmemory(TIE_FILE_ROOT_FRONTEND_ASSET, inverse_palette_file,
										 bpflight_inverse_palette);
			RenderTexture_ResetSoftwareShadeTableCache();
		}
	}
	RenderTexture_SyncFlightPalette();
	objectloadsize = TieFlightAssets_LoadPreviewModel(model_slot, opt_name) != 0;
}

// FUNCTION: TIE95 0x7A1FC
int bpflight_Load_Flight_Craft(const char* lfd_name, const char* shp_name, int16_t mode) {
	ResFile* rf;

	if (TIE_FRONTEND_TIE98) {
		bpflight_Load_Flight_Craft_tie98(lfd_name, shp_name, mode, cur_flight_scene);
		return 1;
	}

	rf = shellext_Open_Empire_Resource(lfd_name);
	if (rf) {
		bpflight_Res_Ship(rf, (uint8_t*)xmemhdl_Lock_Handle(bpflight_fltobj_data[mode]), (char*)shp_name);
		xmemhdl_Unlock_Handle(bpflight_fltobj_data[mode]);
		xres_Close_Resource(rf);
	}
	return 1;
}

/* ----- BPFLIGHT_Res_Ship (0x7C294) -----
 *
 * Thinner variant of Load_Flight_Craft: caller supplies the ResFile and
 * the buffer. No xrefs in the demo. */
// FUNCTION: TIE95 0x7A244
void bpflight_Res_Ship(ResFile* rf, uint8_t* buffer, char* name) {
	int16_t i;
	int resource_offset;
	int resource_size;

	for (i = 0; name[i]; ++i)
		name[i] = (char)toupper((int8_t)name[i]);

	if (xres_Get_Resource_Offset(rf, FOURCC_SHIP, name, &resource_offset, &resource_size)) {
		if (xres_Open_Resource_Data(FOURCC_SHIP, name)) {
			resource_size = (int16_t)xres_Read_Resource_Word(rf);
			objectloadsize = resource_size;
			xres_Read_Resource_Buffer_Data(rf, buffer, resource_size);
			xres_Close_Resource_Data(rf);
		}
	}
}

/* ----- BPFLIGHT_Position_Craft (0x7C314) -----
 *
 * Extracts a joint pose + rotation from a MatrixFrame and folds it into
 * the shared craft{f,S,U}{1,2,3} basis. No xrefs in the demo (inlined). */
// FUNCTION: TIE95 0x7A2C4
// FUNCTION: TIE98 0x405F00
void bpflight_Position_Craft(const MatrixFrame* frame, int16_t joint_idx) {
	const int16_t* rot;

	worldx = frame->joint_pos[joint_idx][0];
	worldy = frame->joint_pos[joint_idx][1];
	worldz = frame->joint_pos[joint_idx][2];

	rot = frame->joint_rot[joint_idx];
	calcf1 = rot[0];
	calcf2 = rot[1];
	calcf3 = rot[2];
	calcS1 = rot[3];
	calcS2 = rot[4];
	calcS3 = rot[5];
	calcU1 = rot[6];
	calcU2 = rot[7];
	calcU3 = rot[8];

	craftf1 = -calcf1;
	craftf2 = -calcf2;
	craftf3 = -calcf3;
	craftS1 = calcS1;
	craftS2 = calcS2;
	craftS3 = calcS3;
	craftU1 = calcU1;
	craftU2 = calcU2;
	craftU3 = calcU3;

	fview_calcrotworldeye();
}

/* ----- BPFLIGHT_settraincolors (0x7C444) -----
 *
 * Remove or re-apply the 39×16-byte training-room material offset.
 * remove != 0 subtracts trainroommapping[j], == 0 adds it back.
 * No xrefs in the demo (inlined in draw_Engine). */
// FUNCTION: TIE95 0x7A3F4
void bpflight_settraincolors(int16_t remove) {
	int j;

	for (j = 0; j < 39; ++j) {
		uint8_t d = trainroommapping[j];
		uint8_t* mc;
		int k;

		if (remove)
			d = (uint8_t)(-(int)d);
		mc = materialcolors + j * 16;
		for (k = 0; k < 16; ++k)
			mc[k] = (uint8_t)(d + mc[k]);
	}
}

/* ----- BPFLIGHT_swapbpmaterials (0x7C488) ----- */

// FUNCTION: TIE95 0x7A438
void bpflight_swapbpmaterials(void) {
	int i;
	int k;
	uint8_t tmp;

	for (i = 0; i < 45; ++i) {
		for (k = 0; k < 16; ++k) {
			tmp = materialcolors[i * 16 + k];
			materialcolors[i * 16 + k] = bp_materialcolors[i * 16 + k];
			bp_materialcolors[i * 16 + k] = tmp;
		}
	}
}

/* ----- BPFLIGHT_setcombatcolors (0x7C4C8) ----- */

// FUNCTION: TIE95 0x7A478
void bpflight_setcombatcolors(int16_t remove) {
	int j;

	for (j = 0; j < 39; ++j) {
		uint8_t d = combatroommapping[j];
		uint8_t* mc;
		int k;

		if (remove)
			d = (uint8_t)(-(int)d);
		mc = materialcolors + j * 16;
		for (k = 0; k < 16; ++k)
			mc[k] = (uint8_t)(d + mc[k]);
	}
}
