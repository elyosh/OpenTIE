#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "landru/vesa.h"
#include "tie/backdrp2.h"
#include "tie/bpflight.h"
#include "tie/draw.h"
#include "tie/drawpol.h"
#include "tie/fediskio.h" /* flightbuf_small / flightbuf_big ownership */
#include "tie/flight_surface_tie98.h"
#include "tie/fview.h"
#include "tie/logbuf2.h"
#include "tie/matrix.h"
#include "tie/modelbounds.h"
#include "tie/rand.h"
#include "tie/render_scene_tie98.h"
#include "tie/render_texture_tie98.h"
#include "tie/rtsvga2.h"
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

/* ----- Types ----- */

/* 4-slot, 3-byte RGB palette programmed into VGA slots 252..255 at t=1
 * of the primary viewport. Laid out tight (no pack directive). */
typedef struct BPStarColor {
	uint8_t r;
	uint8_t g;
	uint8_t b;
} BPStarColor;

/* ----- BPFLIGHT-owned exports ----- */

uint32_t bpflightflag;

/* Exposed piece of the camera-pivot block (see internal storage below). */
int16_t bpflight_pivotpitch[3];
int16_t bpflight_pivotheading[3];
int16_t bpflight_pivotyaw[3];
int16_t bpflight_pivotroll[3];

/* Re-typed fltobj_data (was HANDLE → void*). Second slot is
 * fltobj_data_obstacle, kept adjacent so the index trick
 * `fltobj_bufs[use_obstacle_buf]` works. */
void* bpflight_fltobj_data;
static void* bpflight_fltobj_data_obstacle;
static const Tie98OptimizedPolyObject* bpflight_opt_models[2];
static uint16_t bpflight_opt_model_types[2];

int16_t bpflight_cur_component;
int16_t bpflight_active_component;

/* 4-slot star palette cross-faded into VGA colors 252..255 once per
 * primary-viewport activation. */
static BPStarColor starpal[4];

/* 45 materials × 16 colors each. Swapped with global materialcolors[]
 * around each draw so the viewer uses its own palette without clobbering
 * the gameplay palette. Values copied verbatim from the retail binary's
 * static .data at 0xD0E1C (word_D0E1C); the 45×16 layout is a palette-
 * index per material-shade. Without this initialiser the blueprint/
 * training/combat viewers render ship polys in color 0 (black), visible
 * as a rotating silhouette on the tech-room viewport. */
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
static uint8_t trainroommapping[39];
static uint8_t combatroommapping[39];

/* Viewport actors + orbit Matrix + primary object buffer. */
// GLOBAL: TIE 0xF64B8
static Actor* engine;
static Actor* engine_secondary;
// GLOBAL: TIE 0xF64C4
static Matrix* matrix;

/* Scanline transform scratch (65000 bytes). HANDLE in the binary. */
// GLOBAL: TIE 0xD0E18
static void* xtransdata;

/* True when BPFLIGHT owns the shared edge-pool allocations. */
static int flightbufs_owned_by_bpflight;

/* Per-viewport camera state arrays (3 slots). Originally 16-bit Q16.0
 * for angles (0x10000 ≈ 360°) and Q16.16 signed for positions. */
static int32_t bpcamerax[3], bpcameray[3], bpcameraz[3];
static int16_t bpcamerapitch[3], bpcameraheading[3];
static int16_t bpcameraroll[3], bpcamerayaw[3];
static int16_t bpcameralookpitch[3], bpcameralookclock[3];
static int16_t bpcameraxv[3], bpcamerayv[3], bpcamerazv[3], bpcameravel[3];

/* Per-viewport runtime state. */
// GLOBAL: TIE 0xF6500
static int16_t bpshipstate[3]; /* 1 = draw the ship this frame */
static int16_t bpused[3];      /* 1 = viewport is active */
static int16_t bpid[3];        /* mode tag copied into actor->id */

/* Save / restore + per-frame scratch. */
static int fullstarupdate;
// GLOBAL: TIE 0xF6564
static int16_t tempRes;
static int16_t cur_flight_scene;
static int16_t objectloadsize; /* nonzero while the active preview model is loaded */

/* Save slot for the flight resolution restored by Close_Flight_Engine.
 * cameraX/Y/Z here are the *scene* camera (distinct from the per-viewport
 * bpcameraX/Y/Z and from the game's shared worldX/Y/Z in tie.c). */
static int32_t scene_camerax, scene_cameray, scene_cameraz;
static int16_t scene_cameraroll, scene_cameraheading;
static int16_t scene_cameralookclock, scene_camerapitch;
static int16_t scene_cameralookpitch, scene_camerayaw;

/* ----- Forward decls for callbacks registered with the actor system. ----- */

static void bpflight_user_Engine(Actor* actor, int32_t time);
static int bpflight_draw_Engine(Actor* actor, Rect* clip, Rect* dest, int16_t xoff, int16_t yoff,
								int16_t refresh);
static int bpflight_draw_Engine_tie98(Actor* actor, Rect* clip, Rect* dest, int16_t xoff, int16_t yoff,
									  int16_t refresh);
static void bpflight_Position_Craft_tie98(const MatrixFrame* frame, int16_t joint_idx);
static void bpflight_Load_Flight_Craft_tie98(const char* lfd_name, const char* opt_name, int16_t model_slot,
											 int scene);

/* ----- Small helpers ----- */

/* Select the object buffer for the current draw iteration. Matches the
 * binary's `*(&fltobj_data + use_obstacle_buf)` pointer-adjacency
 * trick — fltobj_data at +0 and fltobj_data_obstacle at +1 word. */
static void* select_fltobj_buf(int use_obstacle) {
	return use_obstacle ? bpflight_fltobj_data_obstacle : bpflight_fltobj_data;
}

/* Uppercase into bounded scratch storage without modifying the caller. */
static void uppercase_copy(char* dst, size_t dst_sz, const char* src) {
	size_t i = 0;
	if (dst_sz == 0)
		return;
	while (i + 1 < dst_sz && src[i]) {
		dst[i] = (char)toupper((unsigned char)src[i]);
		++i;
	}
	dst[i] = '\0';
}

/* Q30 clamp + arithmetic-shift-right-15 → Q15. Matches the binary's
 * `if (x >= 0x40000000) x = 0x3FFF0000; if (x <= -0x40000000) x = -0x3FFF0000;
 * return x >> 15;` idiom used everywhere in the 3D pipeline. */
static int32_t q15_clamp_shift15(int32_t v) {
	if (v >= 0x40000000)
		v = 0x3FFF0000;
	if (v <= -0x40000000)
		v = -0x3FFF0000;
	return v >> 15;
}

/* ----- BPFLIGHT_Open_Flight_Engine (0x7A3A0) ----- */

// FUNCTION: TIE 0x78E70, TIE98 0x404D60
Actor* bpflight_Open_Flight_Engine(int16_t scene) {
	Rect r;

	/* Save + overwrite the per-screen resolution selector, then reinit
	 * the perspective/projection constants for flight mode. */
	tempRes = flightResolution;
	flightResolution = frontResolution;
	tie_initflightresolution();
	matrix = NULL;
	cur_flight_scene = scene;

	if (scene == 1) {
		/* Training room: primary + small thumbnail viewport, loads the
		 * orbit matrix from matrix.lfd entry "trnfly1". */
		ResFile* rf = shellext_Open_Empire_Resource("matrix.lfd");
		matrix = matrix_Res_Matrix(rf, "trnfly1");
		lres_Close_Resource(rf);

		if (TieProfile_FrontendId() == TIE_FRONTEND_PROFILE_TIE98)
			lrect_Set_Rect(&r, 144, 60, 500, 300);
		else
			lrect_Set_Rect(&r, 62, 4, 256, 116);
		engine = lactcust_Alloc_Custom_Actor(NULL, &r, 0, 0, 10);
		lactor_Set_Actor_User_Function(engine, bpflight_user_Engine);
		lactor_Set_Actor_Draw_Function(engine, bpflight_draw_Engine);
		engine->id = 0;
		bpused[0] = 1;
		bpid[0] = 0;

		if (TieProfile_FrontendId() == TIE_FRONTEND_PROFILE_TIE98)
			lrect_Set_Rect(&r, 176, 340, 358, 449);
		else
			lrect_Set_Rect(&r, 85, 131, 182, 178);
		engine_secondary = lactcust_Alloc_Custom_Actor(NULL, &r, 0, 0, 10);
		lactor_Set_Actor_User_Function(engine_secondary, bpflight_user_Engine);
		lactor_Set_Actor_Draw_Function(engine_secondary, bpflight_draw_Engine);
		engine_secondary->id = 1;
		bpid[1] = 1;
		bpused[1] = 1;
		bpused[2] = 0;

		shipext_Init_Train_Ship_Name();
		bpflight_cur_component = -1;
		bpflight_active_component = -1;
	} else if (scene == 2) {
		/* Combat room: primary + thumbnail, "cmbtfly1" orbit. */
		ResFile* rf = shellext_Open_Empire_Resource("matrix.lfd");
		matrix = matrix_Res_Matrix(rf, "cmbtfly1");
		lres_Close_Resource(rf);

		if (TieProfile_FrontendId() == TIE_FRONTEND_PROFILE_TIE98)
			lrect_Set_Rect(&r, 124, 7, 516, 272);
		else
			lrect_Set_Rect(&r, 59, 2, 260, 115);
		engine = lactcust_Alloc_Custom_Actor(NULL, &r, 0, 0, 10);
		lactor_Set_Actor_User_Function(engine, bpflight_user_Engine);
		lactor_Set_Actor_Draw_Function(engine, bpflight_draw_Engine);
		engine->id = 0;
		bpid[0] = 0;
		bpused[0] = 1;

		if (TieProfile_FrontendId() == TIE_FRONTEND_PROFILE_TIE98)
			lrect_Set_Rect(&r, 297, 313, 485, 440);
		else
			lrect_Set_Rect(&r, 146, 130, 247, 179);
		engine_secondary = lactcust_Alloc_Custom_Actor(NULL, &r, 0, 0, 10);
		lactor_Set_Actor_User_Function(engine_secondary, bpflight_user_Engine);
		lactor_Set_Actor_Draw_Function(engine_secondary, bpflight_draw_Engine);
		engine_secondary->id = 1;
		bpid[1] = 1;
		bpused[1] = 1;
		bpused[2] = 0;

		shipext_Init_Combat_Ship_Name();
		bpflight_cur_component = -1;
		bpflight_active_component = -1;
	} else if (scene == 3) {
		/* Blueprint viewer: single full-area viewport, no orbit matrix.
		 * Z plane 20 places it above the UI chrome. */
		if (TieProfile_FrontendId() == TIE_FRONTEND_PROFILE_TIE98)
			lrect_Set_Rect(&r, 222, 75, 570, 310);
		else
			lrect_Set_Rect(&r, 131, 30, 278, 200);
		engine = lactcust_Alloc_Custom_Actor(NULL, &r, 0, 0, 20);
		lactor_Set_Actor_User_Function(engine, bpflight_user_Engine);
		lactor_Set_Actor_Draw_Function(engine, bpflight_draw_Engine);
		engine->id = 2;
		bpid[2] = 2;
		bpused[2] = 1;
		bpused[1] = 0;
		bpused[0] = 0;
		bpflight_cur_component = -1;
		bpflight_active_component = -1;
	}

	/* XTRANS2 scanline scratch. Big enough for a full-screen viewport. */
	xtransdata = malloc(65000);

	/* Blueprint, training, and combat previews may run before FEDISKIO. */
	flightbufs_owned_by_bpflight = 0;
	if (!flightbuf_small) {
		flightbuf_small = malloc(TRACE2_EDGEINFO_CAP * sizeof(trace2_EdgeInfo));
		flightbufs_owned_by_bpflight = 1;
	}
	if (!flightbuf_big) {
		flightbuf_big = malloc(TRACE2_EDGEHEADER_CAP * sizeof(trace2_EdgeHeader));
		flightbufs_owned_by_bpflight = 1;
	}

	/* Object-buffer heaps differ per scene:
	 *   scene 3        : 33000 bytes (blueprint has the largest models)
	 *   scene 1        : 15000 bytes + 9000-byte obstacle heap
	 *   scene 2        : 15000 bytes (no obstacle) */
	if (scene == 3) {
		bpflight_fltobj_data = malloc(33000);
		bpflight_fltobj_data_obstacle = NULL;
	} else {
		bpflight_fltobj_data = malloc(15000);
		if (scene == 1)
			bpflight_fltobj_data_obstacle = malloc(9000);
		else
			bpflight_fltobj_data_obstacle = NULL;
	}

	/* Kick a SHIP resource into fltobj_data via SHIPEXT; training adds
	 * the course obstacle. */
	if (scene == 1) {
		shipext_Get_Train_Ship_SHP();
		shipext_Get_Train_Course_SHP();
	} else if (scene == 2) {
		shipext_Get_Combat_Ship_SHP();
	} else if (scene == 3) {
		shipext_Get_Blueprint_Ship_SHP();
	}

	/* Pre-fill XTRANS2's per-scanline mask. For each row of the
	 * (rightmost viewport's) vertical span, emit one of:
	 *   [1, w]        (width fits in one byte, w <= 0xFF)
	 *   [1, 0, w_lo]  (width > 0xFF, split into a 0 marker + low byte)
	 * The writer advances by 2 or 3 bytes per row accordingly. */
	{
		uint8_t* mask = (uint8_t*)xtransdata + (uint16_t)maskbufptr;
		int16_t width = (int16_t)(r.right - r.left);
		int16_t height = (int16_t)(r.bottom - r.top);
		uint8_t width_lo = (uint8_t)width;
		int16_t y;
		int16_t remaining = width;

		for (y = 0; y < height; ++y) {
			mask[0] = 1;
			if (remaining > 0xFF) {
				/* Watcom fills only the first overflow row this way
				 * then keeps writing the reduced remaining width for
				 * the rest — matches the binary literally. */
				remaining -= 0x100;
				mask[1] = 0;
				mask[2] = width_lo;
				mask += 3;
			} else {
				mask[1] = (uint8_t)remaining;
				mask += 2;
			}
		}
	}

	/* Light direction: symmetric, pointing into +X+Y+Z. 0x49E6 ≈ 0.577
	 * in Q15 (roughly 1/sqrt(3) for a uniform diagonal light). */
	lightX = 0x49E6;
	lightY = 0x49E6;
	lightZ = 0x49E6;

	/* Seed 256 (pos, brightness) pairs at stride 2 into the tie.c-owned
	 * stars[512] buffer. pos is 6-bit (0..63), brightness is 3-bit (0..7). */
	for (int i = 0; i < 511; i += 2) {
		stars[i] = (uint8_t)(rand_rand() & 0x3F);
		stars[i + 1] = (uint8_t)(rand_rand() & 0x07);
	}

	/* Default camera pose per active viewport:
	 *   pitch=0x4000 (≈ horizon), heading/roll/yaw/look*=0
	 *   position: x=0, y=-4096 (elevated), z=0
	 *   id==1/2 override: y=-2048, pitch=0x4000; id==2 uses pitch=19456
	 *   (≈107°), id==1 starts with heading 0x6000 (≈135°). */
	for (int j = 0; j < 3; ++j) {
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

		int16_t mode = bpid[j];
		if (mode != 0) {
			bpcameray[j] = -2048;
			bpflight_pivotpitch[j] = 0x4000;
			if (mode == 2)
				bpflight_pivotpitch[j] = 19456;
			else
				bpflight_pivotheading[j] = 24576;
		}
	}

	/* Per-frame render flags. */
	if (TieProfile_FrontendId() == TIE_FRONTEND_PROFILE_TIE98)
		g_flightInitialTextureCacheFlushPending = 1;
	stardetaillevel = 1;
	drawmarkingsflag = 1;
	fullupdateflag = 1;
	fullstarupdate = 1;
	bpflightflag = 1;
	starshipdetail = 4;
	shipdetailvalue = 0;
	shipdetailpolycnt = 16;
	bpshipstate[0] = 1;
	gouraudflag = 64;
	bpshipstate[2] = 1;
	bpshipstate[1] = 1;
	lightflag = 1;

	/* Reset matrix-frame cursor on the primary viewport. */
	if (engine && engine->id == 0)
		engine->var1 = 0;

	return engine;
}

/* ----- BPFLIGHT_Close_Flight_Engine (0x7A948) ----- */

// FUNCTION: TIE 0x79414
void bpflight_Close_Flight_Engine(void) {
	bpflight_opt_models[0] = NULL;
	bpflight_opt_models[1] = NULL;
	/* The host cache owns TIE98 preview models beyond the active pointers. */
	if (TieProfile_FrontendId() == TIE_FRONTEND_PROFILE_TIE98)
		TieFlightRuntime_ReleaseRecoveredResources();
	free(xtransdata);
	xtransdata = NULL;
	free(bpflight_fltobj_data);
	bpflight_fltobj_data = NULL;
	if (bpflight_fltobj_data_obstacle) {
		free(bpflight_fltobj_data_obstacle);
		bpflight_fltobj_data_obstacle = NULL;
	}
	if (flightbufs_owned_by_bpflight) {
		free(flightbuf_small);
		flightbuf_small = NULL;
		free(flightbuf_big);
		flightbuf_big = NULL;
		flightbufs_owned_by_bpflight = 0;
	}
	if (matrix) {
		matrix_Free_Matrix(matrix);
		matrix = NULL;
	}
	bpflightflag = 0;
	flightResolution = tempRes;
	/* Restore the framebuffer alias retail rebinds to 0xA0000 here.
	 * In the SDL port that's vesa_buff_gbl (the Landru framebuffer that
	 * the presenter scans out). Leaving it NULL would segfault every
	 * post-flight rtsvga2/xtrans2 write into the HUD or 2D screens. */
	xtrans2_videobaseptr = vesa_buff_gbl;
}

/* ----- BPFLIGHT_Open_New_Matrix (0x7A9B0) ----- */

// FUNCTION: TIE 0x794B4
void bpflight_Open_New_Matrix(const char* name) {
	if (matrix) {
		matrix_Free_Matrix(matrix);
		matrix = NULL;
	}
	ResFile* rf = shellext_Open_Empire_Resource("matrix.lfd");
	matrix = matrix_Res_Matrix(rf, name);
	lres_Close_Resource(rf);
}

/* ----- BPFLIGHT_Start_Movie_Engine (0x7A9F0) ----- */

// FUNCTION: TIE 0x794F4
void bpflight_Start_Movie_Engine(void) {
	bpshipstate[1] = 1;
	bpshipstate[2] = 1;
	bpshipstate[0] = 1;
	if (engine && engine->id == 0)
		engine->var1 = 0;
}

/* ----- BPFLIGHT_Stop_Movie_Engine (0x7AA28) ----- */

// FUNCTION: TIE 0x7952C
void bpflight_Stop_Movie_Engine(void) {
	/* Blueprint scene never stops: the ship keeps rendering even when
	 * the orbit is paused because BLUEPRNT drives the camera manually. */
	if (cur_flight_scene != 3)
		bpshipstate[0] = 0;
	if (engine && engine->id == 0)
		engine->var1 = 0;
}

/* ----- BPFLIGHT_user_Engine (0x7AA5C) ----- */

// FUNCTION: TIE 0x79560
static void bpflight_user_Engine(Actor* actor, int32_t time) {
	/* Primary viewport frame 1 only: program 4 star palette slots
	 * (VGA colors 252..255). Used to cross-fade the star colors. */
	if (actor->id == 0 && time == 1) {
		int16_t color_slot = 252;
		for (int i = 0; i < 4; ++i) {
			uint8_t r = starpal[i].r, g = starpal[i].g, b = starpal[i].b;
			lpal_Set_Screen_RGB(color_slot, color_slot, r, g, b);
			lpal_Set_Src_Pal_Color(color_slot, color_slot, r, g, b);
			lpal_Set_Dest_Pal_Color(color_slot, color_slot, r, g, b);
			++color_slot;
		}
	}

	/* Animate the active viewport(s). */
	if (bpshipstate[actor->id]) {
		int16_t id = actor->id;
		if (id == 0) {
			/* Primary: play the orbit matrix, advance the frame cursor. */
			fview_newcalcview(bpcameraroll[0], bpcamerapitch[0], bpcameraheading[0], bpcamerayaw[0],
							  bpcameralookpitch[0], bpcameralookclock[0], NULL);
			actor->var1 = (int16_t)((actor->var1 + 1) % matrix->frame_count);
		} else if (id == 1) {
			/* Secondary thumbnail: slow heading drift. The binary
			 * bumps the high byte (+=4 every 16 of 64 frames), which
			 * is +0x400 on the full 16-bit angle. */
			if ((time & 0x3F) < 0x10) {
				uint16_t h = (uint16_t)bpflight_pivotheading[id];
				h += 0x0400;
				bpflight_pivotheading[id] = (int16_t)h;
			}
		} else if (id == 2) {
			/* Blueprint full-screen: fast heading spin (+0x1F0 / frame)
			 * and pitch oscillation (+/-64 each 64-frame phase). */
			bpflight_pivotheading[2] += 496;
			if (time & 0x40)
				bpflight_pivotpitch[actor->id] += 64;
			else
				bpflight_pivotpitch[actor->id] -= 64;
		}
	}

	/* Component-highlight blink: show the active component for 8 ticks,
	 * hide for 8. Drives DRAWPOL's component-colour override. */
	if ((time & 0x0F) >= 8)
		bpflight_cur_component = -1;
	else
		bpflight_cur_component = bpflight_active_component;
}

/* ----- BPFLIGHT_draw_Engine (0x7AC50) -----
 *
 * Main render. Conforms to the lactorDrawFunc ABI even though only actor
 * and clip are used. */
// FUNCTION: TIE98 0x405640 BPFLIGHT_draw_Engine
static int bpflight_draw_Engine_tie98(Actor* actor, Rect* clip, Rect* dest, int16_t xoff, int16_t yoff,
									  int16_t refresh) {
	(void)dest;
	(void)xoff;
	(void)yoff;
	(void)refresh;

	if (!objectloadsize) {
		lpaint_Paint_Clipped_Rect(clip, 0);
		return 0;
	}

	ldirty_Dirty_Rect(clip);
	MatrixFrame frame;
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

	xtransdataptr = xtransdata;
	BitmapStruct* bitmap = lcanvas_Get_Current_Canvas_Bitmap();
	xtrans2_videobaseptr = (uint8_t*)lbitmap_Lock_Bitmap(bitmap);
	buffer_ptr = xtrans2_videobaseptr;
	const uint16_t width = (uint16_t)(clip->right - clip->left);
	const uint16_t height = (uint16_t)(clip->bottom - clip->top);
	uint8_t* mask = (uint8_t*)xtransdataptr + (uint16_t)maskbufptr;
	for (uint16_t row = 0; row < height; ++row) {
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

	const int16_t object_count = actor->id != 0 ? 1 : matrix->matrix_count;
	transfm2_screenyoffset = 0;
	objectsize = 0x7FFF;
	if (bpshipstate[actor->id] && object_count > 0) {
		for (int16_t object_index = 0; object_index < object_count; ++object_index) {
			int model_slot;
			if (actor->id == 0) {
				bpflight_Position_Craft_tie98(&frame, object_index);
				model_slot = bpflight_opt_models[1] != NULL;
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
						TieFlightModelApi original_models = TieFlightAssets_Tie98OriginalModelApi();
						worldy =
							scene_cameray +
							(modelbounds_getmaxextent_from_api(&original_models, bpflight_opt_model_types[0])
							 << 9) /
								200;
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
			const Tie98OptimizedPolyObject* saved_model_override = g_flightModelOverride;
			g_flightModelOverride = bpflight_opt_models[model_slot];
			FlightModel_Draw_Object(&objects[0]);
			g_flightModelOverride = saved_model_override;
		}
	}

	const uint8_t saved_deepspace_color = deepspacecolor;
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
	deepspacecolor = saved_deepspace_color;
	lbitmap_Unlock_Bitmap(bitmap);
	return 0;
}

// FUNCTION: TIE 0x7975C
static int bpflight_draw_Engine(Actor* actor, Rect* clip, Rect* dest, int16_t xoff, int16_t yoff,
								int16_t refresh) {
	if (TieProfile_FrontendId() == TIE_FRONTEND_PROFILE_TIE98)
		return bpflight_draw_Engine_tie98(actor, clip, dest, xoff, yoff, refresh);
	(void)dest;
	(void)xoff;
	(void)yoff;
	(void)refresh;

	/* No ship loaded yet → paint the viewport black and exit. */
	if (!objectloadsize) {
		lpaint_Paint_Clipped_Rect(clip, 0);
		return 0;
	}

	/* Mark the viewport dirty so XDIRTY refreshes this region. */
	ldirty_Dirty_Rect(clip);

	/* -- Camera setup -- */
	MatrixFrame frame;
	if (actor->id != 0) {
		/* Secondary/blueprint viewport: copy from per-viewport arrays. */
		int16_t i = actor->id;
		scene_camerax = bpcamerax[i];
		scene_cameray = bpcameray[i];
		scene_cameraz = bpcameraz[i];
		scene_camerapitch = bpcamerapitch[i];
		scene_cameraheading = bpcameraheading[i];
		scene_cameraroll = bpcameraroll[i];
		scene_camerayaw = bpcamerayaw[i];
		scene_cameralookpitch = bpcameralookpitch[i];
		scene_cameralookclock = bpcameralookclock[i];
		fview_newcalcview(bpcameraroll[i], bpcamerapitch[i], bpcameraheading[i], bpcamerayaw[i],
						  bpcameralookpitch[i], bpcameralookclock[i], NULL);
	} else {
		/* Primary viewport: pull the current frame from the orbit
		 * matrix. actor->var1 is the frame cursor (Watcom reads this
		 * with HIWORD(*(_DWORD*)&actor->yscale) in the binary — the
		 * unaligned-dword idiom for a +2 byte offset). */
		matrix_Get_Matrix_Frame(matrix, &frame, actor->var1);
		scene_camerayaw = 0;
		scene_cameralookpitch = 0;
		scene_camerax = frame.cam_x;
		scene_cameralookclock = 0;
		scene_cameray = frame.cam_y;
		scene_cameraz = frame.cam_z;
		scene_camerapitch = frame.cam_pitch;
		scene_cameraheading = frame.cam_heading;
		scene_cameraroll = frame.cam_roll;
		fview_newcalcview(frame.cam_roll, frame.cam_pitch, frame.cam_heading, 0, 0, 0, NULL);
	}

	/* Lock the XTRANS2 scratch + canvas; logbuf picks up videobaseptr. */
	xtransdataptr = xtransdata;
	BitmapStruct* bm = lcanvas_Get_Current_Canvas_Bitmap();
	xtrans2_videobaseptr = (uint8_t*)lbitmap_Lock_Bitmap(bm);
	buffer_ptr = xtrans2_videobaseptr;

	/* Re-fill the XTRANS2 mask buffer for this viewport. */
	uint16_t width = (uint16_t)(clip->right - clip->left);
	uint16_t height = (uint16_t)(clip->bottom - clip->top);
	{
		uint8_t* mask = (uint8_t*)xtransdataptr + (uint16_t)maskbufptr;
		for (uint16_t y = 0; y < height; ++y) {
			mask[0] = 1;
			if (width > 0xFF) {
				/* TIE98 BPFLIGHT_draw_Engine 0x405868. */
				mask[1] = 0;
				mask[2] = (uint8_t)(width + 1);
				mask += 3;
			} else {
				mask[1] = (uint8_t)width;
				mask += 2;
			}
		}
	}

	/* Configure LOGBUF2 for this viewport + reset XTRANS2 state. */
	/* TIE98 BPFLIGHT_draw_Engine 0x405895 selects the canvas stride. */
	screenMemWidth = bm->w;
	logbuf2_setbufferdimensions(width, height, (uint32_t)clip->left + (uint32_t)bm->w * clip->top);
	xtrans2_clearruntable();
	fullupdateflag = 0;
	xtrans2_initxtrans();

	/* -- Pre-render material swap --
	 *
	 * 1) Swap 45×16 bytes between the live materialcolors[] and the
	 *    720-byte bp_materialcolors[] backup so the viewer temporarily
	 *    installs its own palette. (In the binary these two arrays are
	 *    adjacent, so the swap is written as `&bp_active_component +
	 *    i + 1` base-minus-1 trick — unfolded here to direct indexing.) */
	for (int i = 0; i < 45; ++i) {
		uint8_t* mc = materialcolors + i * 16;
		uint8_t* bk = bp_materialcolors + i * 16;
		for (int k = 0; k < 16; ++k) {
			uint8_t tmp = mc[k];
			mc[k] = bk[k];
			bk[k] = tmp;
		}
	}
	/* 2) Scene-specific offset: for each of 39 room materials, shift
	 *    all 16 colour bytes by -trainroommapping[j] (training) or
	 *    -combatroommapping[j] (combat). Same algorithm as settraincolors
	 *    with apply_forward==0 (the post-render call negates this). */
	if (cur_flight_scene == 1) {
		for (int j = 0; j < 39; ++j) {
			uint8_t delta = (uint8_t)(-(int)trainroommapping[j]);
			uint8_t* mc = materialcolors + j * 16;
			for (int k = 0; k < 16; ++k)
				mc[k] = (uint8_t)(delta + mc[k]);
		}
	} else if (cur_flight_scene == 2) {
		for (int j = 0; j < 39; ++j) {
			uint8_t delta = (uint8_t)(-(int)combatroommapping[j]);
			uint8_t* mc = materialcolors + j * 16;
			for (int k = 0; k < 16; ++k)
				mc[k] = (uint8_t)(delta + mc[k]);
		}
	}

	/* -- Object draw loop --
	 *
	 * Primary viewport animates matrix->matrix_count objects (one per
	 * joint slot); secondary/blueprint draws a single ship. */
	int16_t object_count = (actor->id != 0) ? 1 : matrix->matrix_count;
	objectsize = 0x7FFF;
	transfm2_screenyoffset = 0;

	if (bpshipstate[actor->id] && object_count > 0) {
		int16_t obj_idx;
		for (obj_idx = 0; obj_idx < object_count; ++obj_idx) {
			int use_obstacle = 0;

			if (actor->id != 0) {
				/* Single-object viewports read the ship world pos from
				 * SHIPEXT (training / combat) or synthesize it from the
				 * fltobj_data header (blueprint). */
				if (cur_flight_scene == 1) {
					shipext_Get_Train_Ship_Pos(&worldx, &worldy, &worldz);
				} else if (cur_flight_scene == 2) {
					shipext_Get_Combat_Ship_Pos(&worldx, &worldy, &worldz);
				} else if (cur_flight_scene == 3) {
					/* Centre the model on screen, add +30 vertical offset.
					 * The fltobj header layout is
					 *   u16 size; ShipModelData data;
					 * so data starts at +2. Ship height is in data.height
					 * (offset +0x0A inside ShipModelData → +0x0C from
					 * buffer start). num_lods is at data+0x1F → buf+0x21.
					 * mesh table = &data.lod_records[num_lods]. */
					uint8_t* buf = bpflight_fltobj_data;
					ShipModelData* smd = (ShipModelData*)(buf + 2);
					uint16_t bp_height = smd->height;
					worldx = 0;
					worldz = 0;
					transfm2_screenyoffset = -30;
					worldy = scene_cameray + (bp_height << 8) / 200;
					objectblockptr = smd;
					componentblockptr = (ShipModelMesh*)(&smd->lod_records[smd->num_lods]);
				}
				fview_newcalcrotate(bpflight_pivotroll[actor->id], bpflight_pivotpitch[actor->id],
									bpflight_pivotheading[actor->id], bpflight_pivotyaw[actor->id], NULL);
			} else {
				/* Primary viewport: read this object's joint from the
				 * matrix frame. joint_pos[obj_idx].xyz and
				 * joint_rot[obj_idx][0..8] (9-short rotation matrix). */
				worldx = frame.joint_pos[obj_idx][0];
				worldy = frame.joint_pos[obj_idx][1];
				worldz = frame.joint_pos[obj_idx][2];

				const int16_t* rot = frame.joint_rot[obj_idx];
				calcf1 = rot[0];
				calcf2 = rot[1];
				calcf3 = rot[2];
				calcS1 = rot[3];
				calcS2 = rot[4];
				calcS3 = rot[5];
				calcU1 = rot[6];
				calcU2 = rot[7];
				calcU3 = rot[8];

				/* Forward axis negates because the ship model's +fwd is
				 * the renderer's -eye-z. Side and up copy verbatim. */
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
				use_obstacle = (bpflight_fltobj_data_obstacle != NULL);
			}

			/* World-relative eye-space position (per-object). */
			worldx -= scene_camerax;
			worldy -= scene_cameray;
			worldz -= scene_cameraz;
			objecteyex = transfm2_geteyex(worldx, worldy, worldz);
			objecteyey = transfm2_geteyey(worldx, worldy, worldz);
			objecteyez = transfm2_geteyez(worldx, worldy, worldz);

			uint8_t* obj_buf = (uint8_t*)select_fltobj_buf(use_obstacle);
			ShipModelData* smd = (ShipModelData*)(obj_buf + 2);
			objectblockptr = smd;

			/* -- Compute relativeshift + relative{x,y,z} --
			 *
			 * Find the minimum right-shift so the largest |camera-world|
			 * delta fits in 15 bits, then project the eye-delta onto
			 * the craft's local (S, f, U) basis. Matches the binary's
			 * scan-dy/dz/dx double loop literally. */
			int32_t dx_raw = 2 * (scene_camerax - worldx);
			int32_t dy_raw = 2 * (scene_cameray - worldy);
			int32_t dz_raw = 2 * (scene_cameraz - worldz);

			int32_t dx_hi = (int32_t)(int16_t)((scene_camerax - worldx) >> 15);
			int32_t dy_hi = (int32_t)(int16_t)((scene_cameray - worldy) >> 15);
			int32_t dz_hi = (int32_t)(int16_t)((scene_cameraz - worldz) >> 15);
			if (dx_hi < 0)
				dx_hi = -dx_hi;
			if (dy_hi < 0)
				dy_hi = -dy_hi;
			if (dz_hi < 0)
				dz_hi = -dz_hi;

			uint16_t scan_dx = (uint16_t)(2 * dx_hi);
			uint16_t scan_dy = (uint16_t)(2 * dy_hi);
			uint16_t scan_dz = (uint16_t)(2 * dz_hi);
			relativeshift = -1;
			do {
				do {
					scan_dy >>= 1;
					scan_dz >>= 1;
					dx_raw >>= 1;
					dy_raw >>= 1;
					dz_raw >>= 1;
					scan_dx >>= 1;
					++relativeshift;
				} while (scan_dx);
			} while (scan_dy || scan_dz);

			int16_t dx_s = (int16_t)dx_raw;
			int16_t dy_s = (int16_t)dy_raw;
			int16_t dz_s = (int16_t)dz_raw;

			int32_t dot_side = (int32_t)dz_s * craftS3 + (int32_t)dy_s * craftS2 + (int32_t)dx_s * craftS1;
			relativex = (int16_t)q15_clamp_shift15(dot_side);

			int32_t dot_fwd = (int32_t)dz_s * craftf3 + (int32_t)dy_s * craftf2 + (int32_t)dx_s * craftf1;
			relativey = (int16_t)(-q15_clamp_shift15(dot_fwd));

			int32_t dot_up = (int32_t)dz_s * craftU3 + (int32_t)dy_s * craftU2 + (int32_t)dx_s * craftU1;
			relativez = (int16_t)q15_clamp_shift15(dot_up);

			/* Adjust shift by the model's own LOD shift, compute the
			 * mesh table start, and locate the BSP tree root. */
			relativeshift = (int16_t)(relativeshift - smd->model_scale_shift);
			componentblockptr = (ShipModelMesh*)(&smd->lod_records[smd->num_lods]);

			/* Layout: just past the mesh table sits a u16 self-relative
			 * offset to the BSP root (+2-byte size prefix). The binary
			 * computes it as
			 *     p  = &smd->speed_default + 3 * num_lods + 1   (int16*)
			 *     p2 = (char*)p + *p                            (base)
			 *     bsp = p2 + 2                                  (root)
			 * which expressed in typed form is: */
			int16_t* render_start = (int16_t*)((uint8_t*)&smd->speed_default + 6 * smd->num_lods + 2);
			uint8_t* bsp_base = (uint8_t*)render_start + *render_start;
			BSPNode* bsp_root = (BSPNode*)(bsp_base + 2);
			parentobject = (uint16_t)(obj_idx + 1);

			int16_t cur_scene = shellext_Get_Cur_Scene();
			if (cur_scene == SCENE_TRAIN_A || cur_scene == SCENE_TRAIN_B) {
				/* Training: two BSP passes — accessories then MainHull. */
				bpflight_drawtreeobject(bsp_root, 1, 0);
				bpflight_drawtreeobject(bsp_root, 1, 1);
			} else {
				/* Non-training: inline the BSP walk with pass_gated=0
				 * (all-meshes filter). Same painter's-order logic as
				 * bpflight_drawtreeobject: sign of the plane equation
				 * picks which child to recurse into first. */
				int16_t has_orbit_pass = 0;
				int16_t mainhull_pass = 0;
				BSPNode* n = bsp_root;
				/* PORT: Watcom emits variable x86 SAR instructions here.
				 * Masking the count preserves their behavior when
				 * relativeshift is negative without invoking C UB. */
				uint16_t bsp_shift = (uint16_t)relativeshift & 31u;
				while (n->left_off != 0) {
					int32_t delta_x = relativex - (n->center_x >> bsp_shift);
					int32_t delta_y = relativey - (n->center_y >> bsp_shift);
					int32_t delta_z = relativez - (n->center_z >> bsp_shift);
					/* Plane dot-product. The binary clamps to ±0x3FFF0000
					 * and tests bit 30 via `(v>>15)&0x8000`; since the
					 * clamp preserves sign, testing < 0 on the raw sum is
					 * equivalent (we only need the sign here). */
					int32_t plane_eq = (int32_t)(int16_t)delta_z * (n->normal_z >> bsp_shift) +
									   (int32_t)(int16_t)delta_y * (n->normal_y >> bsp_shift) +
									   (int32_t)(int16_t)delta_x * (n->normal_x >> bsp_shift);
					if (plane_eq < 0) {
						bpflight_drawtreeobject((uint8_t*)n + n->right_off, has_orbit_pass, mainhull_pass);
						n = (BSPNode*)((uint8_t*)n + n->left_off);
					} else {
						bpflight_drawtreeobject((uint8_t*)n + n->left_off, has_orbit_pass, mainhull_pass);
						n = (BSPNode*)((uint8_t*)n + n->right_off);
					}
				}

				/* Leaf: render the root mesh picked out by right_off. */
				int16_t root_mesh_idx = n->right_off;
				ShipModelMesh* mesh = &componentblockptr[root_mesh_idx];
				uint16_t mesh_off = mesh->render_offset;
				bluetarget = (uint16_t)-1;
				ShipMeshLOD* lods = (ShipMeshLOD*)((uint8_t*)mesh + mesh_off);
				if (mesh->mesh_type == (uint16_t)bpflight_cur_component) {
					currenttargetcomp = root_mesh_idx;
					highlightcolor = 0;
					currenttarget = parentobject;
				} else {
					currenttarget = 1024;
				}
				/* pass_gated==0 statically here → always draw. */
				const uint16_t* poly = draw_getdetailptr(lods, objecteyez);
				drawpol_drawpolyobject(poly, objecteyex, objecteyey, objecteyez);
			}
			/* Obstacle buffer is "unlocked" by... nothing; malloc stays
			 * live until Close_Flight_Engine. */
		}
	}

	/* Temporarily suppress deep-space colour on secondary viewports so
	 * the background renders as clean zeros. */
	uint8_t saved_deepspace = deepspacecolor;
	if (actor->id != 0)
		deepspacecolor = 0;
	xtrans2_drawxtrans();

	/* Primary viewport adds the skybox + star-field over the edge list. */
	if (actor->id == 0) {
		uint16_t star_width = 0x140;
		uint16_t star_height = 0xC8;
		if (TieProfile_FrontendId() == TIE_FRONTEND_PROFILE_TIE98) {
			/* TIE98 BPFLIGHT_draw_Engine 0x405B36. */
			star_width = 0x280;
			star_height = 0x1E0;
		}
		drawbackdropflag = 0;
		backdrp2_backdrop();
		rtsvga2_setvgapointers(xtrans2_videobaseptr, star_width, star_height);
		uint16_t fullupdate_save = fullupdateflag;
		if (fullstarupdate) {
			fullstarupdate = 0;
			fullupdateflag = 1;
		}
		rtsvga2_drawstars();
		fullupdateflag = fullupdate_save;
		rtsvga2_setvgapointers(NULL, star_width, star_height);
	}

	/* -- Post-render material restore (mirror of the pre-render swap) -- */
	if (cur_flight_scene == 1) {
		for (int j = 0; j < 39; ++j) {
			uint8_t delta = trainroommapping[j];
			uint8_t* mc = materialcolors + j * 16;
			for (int k = 0; k < 16; ++k)
				mc[k] = (uint8_t)(delta + mc[k]);
		}
	} else if (cur_flight_scene == 2) {
		for (int j = 0; j < 39; ++j) {
			uint8_t delta = combatroommapping[j];
			uint8_t* mc = materialcolors + j * 16;
			for (int k = 0; k < 16; ++k)
				mc[k] = (uint8_t)(delta + mc[k]);
		}
	}
	for (int i = 0; i < 45; ++i) {
		uint8_t* mc = materialcolors + i * 16;
		uint8_t* bk = bp_materialcolors + i * 16;
		for (int k = 0; k < 16; ++k) {
			uint8_t tmp = mc[k];
			mc[k] = bk[k];
			bk[k] = tmp;
		}
	}
	deepspacecolor = saved_deepspace;

	lbitmap_Unlock_Bitmap(bm);
	return 0;
}

// FUNCTION: TIE 0x79DEC
uint8_t bpflight_getrelativexyz(void) {
	int32_t dx_raw = 2 * (scene_camerax - worldx);
	int32_t dy_raw = 2 * (scene_cameray - worldy);
	int32_t dz_raw = 2 * (scene_cameraz - worldz);

	int32_t dx_hi = (int32_t)(int16_t)((scene_camerax - worldx) >> 15);
	int32_t dy_hi = (int32_t)(int16_t)((scene_cameray - worldy) >> 15);
	int32_t dz_hi = (int32_t)(int16_t)((scene_cameraz - worldz) >> 15);
	if (dx_hi < 0)
		dx_hi = -dx_hi;
	if (dy_hi < 0)
		dy_hi = -dy_hi;
	if (dz_hi < 0)
		dz_hi = -dz_hi;

	uint16_t scan_dx = (uint16_t)(2 * dx_hi);
	uint16_t scan_dy = (uint16_t)(2 * dy_hi);
	uint16_t scan_dz = (uint16_t)(2 * dz_hi);
	relativeshift = -1;
	do {
		do {
			scan_dy >>= 1;
			scan_dz >>= 1;
			dx_raw >>= 1;
			dy_raw >>= 1;
			dz_raw >>= 1;
			scan_dx >>= 1;
			++relativeshift;
		} while (scan_dx);
	} while (scan_dy || scan_dz);

	int16_t dx = (int16_t)dx_raw, dy = (int16_t)dy_raw, dz = (int16_t)dz_raw;
	relativex =
		(int16_t)q15_clamp_shift15((int32_t)dz * craftS3 + (int32_t)dy * craftS2 + (int32_t)dx * craftS1);
	relativey =
		(int16_t)(-q15_clamp_shift15((int32_t)dz * craftf3 + (int32_t)dy * craftf2 + (int32_t)dx * craftf1));
	relativez =
		(int16_t)q15_clamp_shift15((int32_t)dz * craftU3 + (int32_t)dy * craftU2 + (int32_t)dx * craftU1);
	uint8_t shift = objectblockptr->model_scale_shift;
	relativeshift = (int16_t)(relativeshift - shift);
	return shift;
}

/* ----- BPFLIGHT_drawtreeobject (0x7BBA8) ----- */

// FUNCTION: TIE 0x79FD4
void bpflight_drawtreeobject(void* node, int16_t pass_gated, int16_t pass_mainhull) {
	BSPNode* n = (BSPNode*)node;
	/* PORT: Watcom emits variable x86 SAR instructions here. Masking
	 * the count preserves their behavior without invoking C UB. */
	uint16_t bsp_shift = (uint16_t)relativeshift & 31u;

	/* Walk internal nodes (left_off != 0). */
	while (n->left_off != 0) {
		int32_t dx = relativex - (n->center_x >> bsp_shift);
		int32_t dy = relativey - (n->center_y >> bsp_shift);
		int32_t dz = relativez - (n->center_z >> bsp_shift);
		/* Plane dot-product sign selects which child is "far" (draw
		 * first) and which is "near" (recurse into next). See the inline
		 * version in draw_Engine for the clamp-equivalence note. */
		int32_t plane_eq = (int32_t)(int16_t)dz * (n->normal_z >> bsp_shift) +
						   (int32_t)(int16_t)dy * (n->normal_y >> bsp_shift) +
						   (int32_t)(int16_t)dx * (n->normal_x >> bsp_shift);
		if (plane_eq < 0) {
			bpflight_drawtreeobject((uint8_t*)n + n->right_off, pass_gated, pass_mainhull);
			n = (BSPNode*)((uint8_t*)n + n->left_off);
		} else {
			bpflight_drawtreeobject((uint8_t*)n + n->left_off, pass_gated, pass_mainhull);
			n = (BSPNode*)((uint8_t*)n + n->right_off);
		}
	}

	/* Leaf: right_off is the mesh index into componentblockptr[]. */
	int16_t leaf = n->right_off;
	ShipModelMesh* mesh = &componentblockptr[leaf];
	uint16_t mesh_off = mesh->render_offset;
	bluetarget = (uint16_t)-1;
	ShipMeshLOD* lods = (ShipMeshLOD*)((uint8_t*)mesh + mesh_off);

	if (mesh->mesh_type == (uint16_t)bpflight_cur_component) {
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
	int draw;
	if (pass_gated) {
		if (pass_mainhull)
			draw = (mesh->mesh_type == 1 /* MESH_MainHull */);
		else
			draw = (mesh->mesh_type != 1 /* MESH_MainHull */);
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
}

/* ----- BPFLIGHT_drawtrainobject (0x7BDAC) -----
 *
 * Inlined two-pass BSP walker (accessories then MainHull). Equivalent to:
 *   bpflight_drawtreeobject(node, 1, 0);
 *   bpflight_drawtreeobject(node, 1, 1);
 * The binary unrolled both passes to save the recursive call in each
 * traversal step. No xrefs in the demo. */
// FUNCTION: TIE 0x7A1D8
void bpflight_drawtrainobject(void* node) {
	bpflight_drawtreeobject(node, 1, 0);
	bpflight_drawtreeobject(node, 1, 1);
}

/* ----- BPFLIGHT_Load_Flight_Craft (0x7C1BC) ----- */

// FUNCTION: TIE98 0x405BE0 BPFLIGHT_Load_Flight_Craft
static void bpflight_Load_Flight_Craft_tie98(const char* lfd_name, const char* opt_name, int16_t model_slot,
											 int scene) {
	(void)lfd_name;
	/* PORT: the original frees and reloads a movable OPT handle in
	 * g_loaded_model_handles[model_slot]. The host native-OPT cache owns an
	 * immutable equivalent for the same IVFILES name. */
	g_inversePaletteTable = bpflight_inverse_palette;
	Palette* palette = lpal_Get_Dest_Palette();
	int palette_changed = 0;
	for (int index = 0; index < 256; ++index) {
		int16_t red = 0;
		int16_t green = 0;
		int16_t blue = 0;
		lpal_Get_Palette_Index_RGB(palette, &red, &green, &blue, (int16_t)index);
		uint8_t* previous = &bpflight_palette_rgb[3 * index];
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
	bpflight_opt_models[model_slot] =
		TieNativeOpt_AcquireNamed(opt_name, &bpflight_opt_model_types[model_slot]);
	objectloadsize = bpflight_opt_models[model_slot] != NULL;
}

int tie98_preview_primary_model_max_extent(void) {
	if (!bpflight_opt_models[0])
		return 0;
	TieFlightModelApi original_models = TieFlightAssets_Tie98OriginalModelApi();
	return modelbounds_getmaxextent_from_api(&original_models, bpflight_opt_model_types[0]);
}

// FUNCTION: TIE 0x7A1FC
int bpflight_Load_Flight_Craft(const char* lfd_name, const char* shp_name, int16_t mode) {
	if (TieProfile_FrontendId() == TIE_FRONTEND_PROFILE_TIE98) {
		bpflight_Load_Flight_Craft_tie98(lfd_name, shp_name, mode, cur_flight_scene);
		return 1;
	}

	ResFile* rf = shellext_Open_Empire_Resource(lfd_name);
	if (!rf)
		return 1;

	void* target = (mode == 0) ? bpflight_fltobj_data : bpflight_fltobj_data_obstacle;
	uint8_t* obj_data = (uint8_t*)target;

	char name[16];
	uppercase_copy(name, sizeof(name), shp_name);

	int out_offset = 0;
	uint32_t out_size = 0;
	if (lres_Get_Resource_Offset(rf, FOURCC_SHIP, name, &out_offset, &out_size)) {
		if (lres_Open_Resource_Data(FOURCC_SHIP, name)) {
			/* Leading u16 in the resource is the object data size; the
			 * rest is raw ShipModelData. */
			uint16_t size = lres_Read_Resource_Word(rf);
			objectloadsize = (int16_t)size;
			lres_Read_Resource_Buffer_Data(rf, obj_data, size);
			lres_Close_Resource_Data(rf);
		}
	}

	lres_Close_Resource(rf);
	return 1;
}

/* ----- BPFLIGHT_Res_Ship (0x7C294) -----
 *
 * Thinner variant of Load_Flight_Craft: caller supplies the ResFile and
 * the buffer. No xrefs in the demo. */
// FUNCTION: TIE 0x7A244
int bpflight_Res_Ship(ResFile* rf, uint8_t* buffer, const char* name) {
	char up[16];
	uppercase_copy(up, sizeof(up), name);

	int out_offset = 0;
	uint32_t out_size = 0;
	int ok = 0;
	if (lres_Get_Resource_Offset(rf, FOURCC_SHIP, up, &out_offset, &out_size)) {
		if (lres_Open_Resource_Data(FOURCC_SHIP, up)) {
			uint16_t size = lres_Read_Resource_Word(rf);
			objectloadsize = (int16_t)size;
			lres_Read_Resource_Buffer_Data(rf, buffer, size);
			lres_Close_Resource_Data(rf);
			ok = 1;
		}
	}
	return ok;
}

/* ----- BPFLIGHT_Position_Craft (0x7C314) -----
 *
 * Extracts a joint pose + rotation from a MatrixFrame and folds it into
 * the shared craft{f,S,U}{1,2,3} basis. No xrefs in the demo (inlined). */
// FUNCTION: TIE98 0x405F00 BPFLIGHT_Position_Craft
static void bpflight_Position_Craft_tie98(const MatrixFrame* frame, int16_t joint_idx) {
	worldx = frame->joint_pos[joint_idx][0];
	worldy = frame->joint_pos[joint_idx][1];
	worldz = frame->joint_pos[joint_idx][2];
	const int16_t* rotation = frame->joint_rot[joint_idx];
	calcf1 = rotation[0];
	calcf2 = rotation[1];
	calcf3 = rotation[2];
	calcS1 = rotation[3];
	calcS2 = rotation[4];
	calcS3 = rotation[5];
	calcU1 = rotation[6];
	calcU2 = rotation[7];
	calcU3 = rotation[8];
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

// FUNCTION: TIE 0x7A2C4
void bpflight_Position_Craft(MatrixFrame* frame, int16_t joint_idx) {
	worldx = frame->joint_pos[joint_idx][0];
	worldy = frame->joint_pos[joint_idx][1];
	worldz = frame->joint_pos[joint_idx][2];

	const int16_t* rot = frame->joint_rot[joint_idx];
	calcf1 = rot[0];
	calcf2 = rot[1];
	calcf3 = rot[2];
	calcS1 = rot[3];
	calcS2 = rot[4];
	calcS3 = rot[5];
	calcU1 = rot[6];
	calcU2 = rot[7];
	calcU3 = rot[8];

	craftS1 = calcS1;
	craftf1 = -calcf1;
	craftS2 = calcS2;
	craftS3 = calcS3;
	craftf2 = -calcf2;
	craftU1 = calcU1;
	craftU2 = calcU2;
	craftf3 = -calcf3;
	craftU3 = calcU3;

	fview_calcrotworldeye();
}

/* ----- BPFLIGHT_settraincolors (0x7C444) -----
 *
 * Apply (forward) or undo (inverse) the 39×16-byte training-room material
 * offset. apply_forward != 0 adds trainroommapping[j], == 0 subtracts.
 * No xrefs in the demo (inlined in draw_Engine). */
// FUNCTION: TIE 0x7A3F4
void bpflight_settraincolors(int16_t apply_forward) {
	for (int j = 0; j < 39; ++j) {
		uint8_t d = trainroommapping[j];
		if (!apply_forward)
			d = (uint8_t)(-(int)d);
		uint8_t* mc = materialcolors + j * 16;
		for (int k = 0; k < 16; ++k)
			mc[k] = (uint8_t)(d + mc[k]);
	}
}

/* ----- BPFLIGHT_setcombatcolors (0x7C4C8) ----- */

// FUNCTION: TIE 0x7A478
void bpflight_setcombatcolors(int16_t apply_forward) {
	for (int j = 0; j < 39; ++j) {
		uint8_t d = combatroommapping[j];
		if (!apply_forward)
			d = (uint8_t)(-(int)d);
		uint8_t* mc = materialcolors + j * 16;
		for (int k = 0; k < 16; ++k)
			mc[k] = (uint8_t)(d + mc[k]);
	}
}

/* ----- BPFLIGHT_swapbpmaterials (0x7C488) ----- */

// FUNCTION: TIE 0x7A438
void bpflight_swapbpmaterials(void) {
	for (int i = 0; i < 45; ++i) {
		uint8_t* mc = materialcolors + i * 16;
		uint8_t* bk = bp_materialcolors + i * 16;
		for (int k = 0; k < 16; ++k) {
			uint8_t tmp = mc[k];
			mc[k] = bk[k];
			bk[k] = tmp;
		}
	}
}
