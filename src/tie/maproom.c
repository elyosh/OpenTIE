#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "tie/backdrp2.h"
#include "tie/create.h"
#include "tie/fediskio.h"
#include "tie/feinput.h"
#include "tie/festring.h"
#include "tie/flight_surface_tie98.h"
#include "tie/frontend_display_tie98.h"
#include "tie/fsfx.h"
#include "tie/fview.h"
#include "tie/logbuf2.h"
#include "tie/maproom.h"
#include "tie/msg.h"
#include "tie/pai.h"
#include "tie/panel.h"
#include "tie/rtsvga2.h"
#include "tie/shipext.h"
#include "tie/sys2.h"
#include "tie/tie.h"
#include "tie/tie_render_tie98.h"
#include "tie/transfm2.h"
#include "tie/trig2.h"
#include "tie/user.h" /* user_submodal_result */
#include "tie/xtimer.h"
#include "tie_runtime/display/classic_display.h"
#include "tie_runtime/runtime/profile.h"
#include <landru/task.h>

/* --- Tunables ------------------------------------------------------------- */

/* Object-reference encoding boundaries used by the per-frame z-sort. The
 * "local" index space is [0, MAP_FLIGHT_SLOT_COUNT+NUM_STATIC_OBJECTS);
 * items with idx < MAP_FLIGHT_SLOT_COUNT reach into objects[], the rest
 * reach into staticobjects[idx - MAP_FLIGHT_SLOT_COUNT]. Retail = 80
 * (NUM_CRAFTS + NUM_WARHEADS, end of non-debris slots). Demo was 76.
 *
 * MAP_STATIC_TO_OBJREF translates a local static idx (>=MAP_FLIGHT_SLOT_COUNT)
 * into the obj_or_kind namespace by adding (OBJ_REF_STATIC_BASE -
 * MAP_FLIGHT_SLOT_COUNT) so the draw code can compare against
 * pstate.target_obj_idx / focus_obj_ref. Retail bias = 14256. */
#define MAP_FLIGHT_SLOT_COUNT WARHEAD_SLOT_END
#define MAP_STATIC_BIAS (OBJ_REF_STATIC_BASE - MAP_FLIGHT_SLOT_COUNT)

/* Per-frame insertion-sort capacity. The IDA frame holds 48 sort_indices
 * + 48 fg_render_count bytes. */
#define MAP_SORT_CAP 48

/* Cluster pre-pass scans NUM_CRAFTS slots (32 retail / 28 demo). */
#define MAP_CRAFT_SCAN NUM_CRAFTS

/* View-mode page count (top-down vs side-on). */
#define MAP_VIEW_TRANSITION 118 /* duration of side<->top animation in ticks */

/* Frame pacing target (1 tick = ~4ms of XTIMER, so 4 ticks ~= 16ms = 60fps). */
#define MAP_FRAME_TICKS 4

/* Initial camera orbit distance (= 4 megaunits). */
#define MAP_CAMERA_DEFAULT 0x40000

/* Camera-distance clamps (mouse zoom). */
#define MAP_CAMERA_NEAR 2048
#define MAP_CAMERA_FAR 0x200000

/* Numpad pan speed (units per tick; user_framerateadjust scales by frametime). */
#define MAP_PAN_DELTA 10240

/* Buffer fill colour (logbuf2_clearbuffer reads `backcolor`). */
#define MAP_BG_COLOR ((uint8_t)-5) /* 0xFB */

/* Palette indices used for backgrounds, text, and helper lines. */
#define MAP_PANEL_BG 0x40    /* status-panel background */
#define MAP_AXIS_LINE 0x30   /* world-axis tick colour */
#define MAP_BG_HOSTILE 0x51  /* red */
#define MAP_BG_IMPERIAL 0x49 /* yellow */
#define MAP_BG_NEUTRAL 0x45  /* cyan */
#define MAP_BG_OTHER 0x55
#define MAP_BG_SELECTED 0x43 /* cyan highlight (matches MAP_TC_HOSTILE_DOT) */
#define MAP_BG_TARGET 0x4E   /* pink (pstate.target_obj_idx highlight) */
#define MAP_TC_HOSTILE 0x52  /* status-panel text colors */
#define MAP_TC_IMPERIAL 0x4A
#define MAP_TC_NEUTRAL 0x46
#define MAP_TC_HELP 0x56
#define MAP_TC_DEFAULT 0x43

/* --- Module globals (watdbg owner: maproom.c) ----------------------------- */

// GLOBAL: TIE 0xC5508
uint8_t imperialflag;
// GLOBAL: TIE 0xC5509
uint8_t neutralflag;
// GLOBAL: TIE 0xC550A
uint8_t hostileflag;
uint8_t warheadflag;
/* mapiconsloaded is owned by tie.c per watdbg; extern in maproom.h. */

int32_t mapScreenLeft;
int32_t mapScreenRight;
int32_t mapScreenTop;
int32_t mapScreenBottom;
int32_t mapScreenWidth;
// GLOBAL: TIE 0xD4C54
int32_t mapScreenHeight;
int32_t maxMapIcons;

const uint8_t* species2icon;
const uint8_t* iconxsize;
const uint8_t* iconysize;
const char* iconfilename;

// GLOBAL: TIE 0xD4C58
const char* hostilestr;
// GLOBAL: TIE 0xD4C40
const char* imperialstr;
// GLOBAL: TIE 0xD4C24
const char* neutralstr;
// GLOBAL: TIE 0xD4C48
const char** NHIstatusstrings;
// GLOBAL: TIE 0xD4C38
const char** maproomhelpstrings;

void** mapfarbufferptrs;

/* Per-resolution lookup tables (binary-extracted byte literals). */
/* Basenames only — the original Watcom binary stored the literals as
 * "RESOURCE\\icons640.ico"; we prepend `resourcedir` at the call site so
 * the path separator is platform-correct (sfxblast.lfd/adlib.lfd use the
 * same convention). */
const char iconfilename640[22] = "icons640.ico";
const char iconfilename320[22] = "mapicons.ico";

/* Verbatim bytes from retail Z_TIE__.EXE (.data @ 0xC550C, 0xC5576,
 * 0xC55B6, 0xC55F6, 0xC5660, 0xC56A2). Demo had different (smaller)
 * tables: 61 hi-res icons / 60 distinct ids vs retail's 64 / 63. */
const uint8_t species2icon640[106] = {
	0,  0,  1,  2,  3,  4,  5,  6,  7,  8,  0,  0,  9,  10, 11, 12, 13, 14, 15, 16, 17, 18,
	19, 20, 21, 22, 23, 24, 25, 26, 27, 0,  28, 29, 30, 31, 0,  32, 33, 33, 34, 35, 36, 37,
	38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 0,  48, 49, 50, 51, 52, 53, 53, 53, 53, 53, 53,
	63, 61, 62, 54, 55, 55, 55, 55, 55, 56, 57, 58, 60, 60, 59, 59, 59, 60, 60, 58, 61, 61,
	0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  61, 61, 61, 61, 61, 61,
};

const uint8_t iconxsize640[64] = {
	8, 8, 7, 11, 8,  8, 11, 7, 10, 11, 7,  7, 8, 11, 11, 11, 7, 7, 6, 8,  8, 4,
	5, 6, 9, 4,  9,  6, 10, 9, 9,  9,  10, 9, 7, 7,  5,  9,  7, 6, 7, 7,  8, 9,
	7, 7, 9, 11, 10, 6, 9,  7, 6,  14, 17, 9, 9, 9,  6,  6,  7, 7, 7, 14,
};

const uint8_t iconysize640[64] = {
	12, 13, 10, 10, 9, 9, 10, 11, 10, 10, 13, 11, 10, 9,  9,  11, 14, 13, 11, 13, 13, 6,
	8,  11, 9,  9,  9, 8, 15, 12, 13, 16, 11, 12, 15, 15, 17, 17, 18, 16, 17, 17, 17, 20,
	17, 15, 17, 21, 7, 7, 6,  9,  9,  14, 15, 6,  8,  8,  3,  6,  10, 7,  7,  17,
};

const uint8_t species2icon320[106] = {
	0,  0,  1,  2,  3,  4,  5,  6,  7,  8,  0,  0,  50, 51, 9,  52, 10, 11, 12, 13, 53, 14,
	15, 54, 16, 55, 17, 18, 19, 20, 21, 0,  22, 23, 24, 25, 0,  56, 26, 26, 27, 28, 29, 30,
	57, 58, 59, 31, 60, 32, 33, 34, 35, 36, 0,  37, 61, 62, 63, 64, 38, 38, 38, 38, 38, 38,
	38, 38, 38, 65, 39, 40, 40, 40, 40, 41, 42, 43, 43, 43, 44, 45, 46, 46, 46, 47, 48, 49,
	0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  48, 48, 48, 48, 48, 48,
};

const uint8_t iconxsize320[66] = {
	5, 5, 5, 5, 5, 5, 7, 5, 5, 5, 9, 5, 5, 5, 3, 5, 5, 4,  6, 3, 5, 4, 4, 5, 5, 5, 7, 3, 3, 3, 7, 5, 5,
	5, 5, 5, 5, 6, 9, 5, 5, 7, 7, 4, 3, 4, 5, 3, 4, 4, 12, 9, 8, 5, 8, 7, 8, 7, 3, 3, 5, 8, 8, 7, 7, 5,
};

const uint8_t iconysize320[66] = {
	5, 6, 4, 6, 5, 5, 5, 5, 5, 5, 5, 5, 5, 6, 6, 6, 5, 6, 4, 6, 5, 6, 7, 7, 7, 6, 6, 6,  6, 7, 7, 7, 8,
	9, 5, 7, 7, 4, 6, 5, 5, 7, 7, 8, 7, 6, 7, 4, 4, 4, 4, 5, 8, 6, 4, 5, 6, 5, 8, 8, 10, 4, 4, 4, 6, 7,
};

/* --- Forward declarations ------------------------------------------------ */

static int maproom_side_visible(uint8_t side);
static int32_t maproom_local_world_z(uint16_t local_idx);
static void maproom_cluster_pass(uint8_t fg_render_count[48], uint16_t focus_obj_ref);
static void maproom_zsort(int32_t z_buf[140], uint8_t sort_indices[MAP_SORT_CAP], uint8_t* sort_count,
						  uint8_t fg_render_count[48], uint16_t focus_obj_ref);
static void maproom_draw_axis(int axis);
static void maproom_draw_line(int32_t x1, int32_t y1, int32_t x2, int32_t y2, uint8_t color);
static void maproom_clear_buffer(bool tie98_display);
static void maproom_output_buffer(bool tie98_display, const void* src);
static void maproom_output_diff_buffer(bool tie98_display, const void* oldbuf, const void* newbuf);
static void maproom_render_pass(const uint8_t sort_indices[MAP_SORT_CAP], uint8_t sort_count,
								const int32_t z_buf[140], const uint8_t fg_render_count[48],
								uint16_t focus_obj_ref, int side);
static int maproom_view_transition(int32_t camera_distance, uint16_t view_mode,
								   uint16_t view_transition_progress, int16_t view_heading,
								   int16_t view_pitch);

/* --- Helpers -------------------------------------------------------------- */

/*
 * NHI side filter test. Returns 0 if the object's side is currently hidden
 * by the player's NHI status flag (xxxflag == 2 = "Off"); 1 if the side is
 * visible (in either icon-only or full-detail mode).
 *
 * Side encoding mirrors the binary:
 *   side 1, 4   -> imperial (gates on imperialflag)
 *   side 0      -> hostile  (gates on hostileflag)
 *   side 2      -> neutral  (gates on neutralflag)
 *   side 3 etc. -> "other"  (also gates on neutralflag, mirroring the binary)
 */
static int maproom_side_visible(uint8_t side) {
	if (side == 1 || side == 4) {
		return imperialflag != 2;
	}
	if (side == 0) {
		return hostileflag != 2;
	}
	if (side >= 2 && side != 4 && neutralflag == 2) {
		return 0;
	}
	return 1;
}

/*
 * Read the world Z of the local-index item (active or static); used by
 * the back/front split test (camera_z<-65536 vs item_z<-65536).
 *
 * Watcom unaligned-load resolution
 * --------------------------------
 * The binary reads `*(int*)&staticobjects[i].world_y >> 16 << 8` to
 * extract `world_z << 8`. We just write the explicit field access.
 */
static int32_t maproom_local_world_z(uint16_t local_idx) {
	if (local_idx < MAP_FLIGHT_SLOT_COUNT) {
		return objects[local_idx].world_z;
	}
	return (int32_t)staticobjects[local_idx - MAP_FLIGHT_SLOT_COUNT].world_z * 256;
}

/* --- maproom_firstingroup ------------------------------------------------- */

// FUNCTION: TIE 0x31B94
int32_t maproom_firstingroup(uint16_t idx) {
	/* Sentinel + boundary cases all return "is first". */
	if (idx == 0xFFFF)
		return 1;
	if (idx == pstate.object_idx)
		return 1;
	if (idx >= NUM_CRAFTS)
		return 1; /* retail uses 0x20 (32), demo had 28 */
	if (idx == 0)
		return 1;

	/* Scan earlier slots; if any earlier object shares this fg_idx, the
	 * current slot isn't the first member. */
	const uint8_t my_fg = objects[idx].fg_idx;
	for (uint16_t i = 0; i < idx; i++) {
		if (objects[i].fg_idx == my_fg)
			return 0;
	}
	return 1;
}

/* --- maproom_setcamerafocus ----------------------------------------------- */

// FUNCTION: TIE 0x31ABC
void maproom_setcamerafocus(uint16_t obj_or_kind, int32_t distance) {
	/* Resolve the focus point (writes worldlocx/y/z). fg_idx=0 is the
	 * caller's "no fg context" sentinel for create_getworldposition. */
	create_getworldposition(obj_or_kind, 0);

	camera.x = worldlocx;
	camera.y = worldlocy;
	camera.z = worldlocz;

	/* Walk back along eye->world Z basis vector (worldeyeA3/B3/C3) by
	 * `distance` units. The binary does this in two phases to avoid
	 * 16-bit overflow: full-unit chunks (one basis vector subtracted
	 * each loop) until the remainder fits in a 16-bit signed multiply,
	 * then a single fractional `(int16)remainder * basis >> 15` step.
	 *
	 * Note: the binary uses worldeye* (the camera basis), NOT rotworldeye*
	 * (which is worldeye composed with the per-craft rotation). */
	int32_t remaining = distance;
	while (remaining > 0x7FFF) {
		remaining -= 0x7FFF;
		camera.x -= worldeyeA3;
		camera.y -= worldeyeB3;
		camera.z -= worldeyeC3;
	}
	camera.x -= (worldeyeA3 * (int16_t)remaining) >> 15;
	camera.y -= (worldeyeB3 * (int16_t)remaining) >> 15;
	camera.z -= (worldeyeC3 * (int16_t)remaining) >> 15;
}

/* --- maproom_drawNHIstatus ------------------------------------------------ */

/*
 * Internal helper: paint one status panel (background-fill + label +
 * right-aligned counter / arbitrary text) at the given pixel rect.
 *   text_color = palette index for the label
 *   label      = left-aligned label text (e.g. "Hostile")
 *   value      = right-aligned text (e.g. NHIstatusstrings[hostileflag])
 *
 * The caller guarantees `right > left+2` and `bottom > top+1`.
 */
static void maproom_draw_status_panel(int16_t left, int16_t top, int16_t right, int16_t bottom,
									  int16_t cursor_x, int16_t cursor_y, uint8_t text_color,
									  const char* label, const char* value) {
	festring_setbound(left, top, right, bottom);
	festring_setbackcolor(MAP_PANEL_BG);
	clearwindow();

	festring_setcursor(cursor_x, cursor_y);
	festring_settextcolor(text_color);
	festring_outstring((const uint8_t*)label);
	if (value)
		festring_outstringright((const uint8_t*)value);
}

// FUNCTION: TIE 0x31C04
void maproom_drawNHIstatus(uint16_t page_idx) {
	/* The status row sits along the bottom of the screen. mapScreenHeight
	 * is the world-area height (= mapScreenBottom - mapScreenTop) but the
	 * IDA decompile uses it as the absolute bottom Y; we mirror that. */
	const int16_t panel_w = (int16_t)(screenXRes >> 2);
	const int16_t bottom_y = (int16_t)mapScreenHeight;
	const int16_t bottom_top = (int16_t)(mapScreenHeight - (fontheight + 1));
	const int16_t bottom_curs = (int16_t)(mapScreenHeight - fontheight);

	/* --- Bottom-left panel: Hostile counter --- */
	maproom_draw_status_panel(0, bottom_top, panel_w, bottom_y, 2, bottom_curs, MAP_TC_HOSTILE, hostilestr,
							  NHIstatusstrings[hostileflag]);

	/* --- Bottom-center panel: Imperial counter --- */
	const int16_t mid_left = (int16_t)((screenXRes >> 1) - panel_w / 2);
	const int16_t mid_right = (int16_t)(panel_w + mid_left);
	maproom_draw_status_panel(mid_left, bottom_top, mid_right, bottom_y, (int16_t)(mid_left + 2), bottom_curs,
							  MAP_TC_IMPERIAL, imperialstr, NHIstatusstrings[imperialflag]);

	/* --- Bottom-right panel: Neutral counter --- */
	const int16_t right_left = (int16_t)(screenXRes - panel_w);
	maproom_draw_status_panel(right_left, bottom_top, (int16_t)screenXRes, bottom_y,
							  (int16_t)(right_left + 2), bottom_curs, MAP_TC_NEUTRAL, neutralstr,
							  NHIstatusstrings[neutralflag]);

	/* --- Top-left help string for the active page --- */
	const int16_t topbar_h = (int16_t)(fontheight + 2);
	const int16_t topleft_str_w = sys2_calclength((const uint8_t*)maproomhelpstrings[0]);
	maproom_draw_status_panel(0, 0, (int16_t)(fontheight + topleft_str_w), topbar_h, 2, 1, MAP_TC_HELP,
							  maproomhelpstrings[page_idx], NULL);

	/* --- Top-right help string (always idx 2) --- */
	const int16_t topright_str_w = sys2_calclength((const uint8_t*)maproomhelpstrings[2]);
	const int16_t topright_left = (int16_t)(screenXRes - (fontheight + topright_str_w));
	maproom_draw_status_panel(topright_left, 0, (int16_t)screenXRes, topbar_h, (int16_t)(topright_left + 2),
							  1, MAP_TC_HELP, maproomhelpstrings[2], NULL);
}

/* --- maproom_drawmapitem -------------------------------------------------- */

/*
 * Project (worldx, worldy, worldz) - camera into eye-space, then to
 * screen-space using `z` as the projection scale. `clip_zero` is the
 * fallback eye-z used when the object is at/behind the camera plane.
 *
 * Writes objecteyex/y/z and returns the screen-space x via *out_sx,
 * y via *out_sy.
 */
static void maproom_project(int32_t wx, int32_t wy, int32_t wz, int32_t clip_x, int32_t clip_y,
							int32_t clip_z, int32_t* out_sx, int32_t* out_sy) {
	objecteyex = transfm2_geteyex(wx, wy, wz);
	objecteyey = transfm2_geteyey(wx, wy, wz);
	objecteyez = transfm2_geteyez(wx, wy, wz);
	if (objecteyez <= 0)
		transfm2_clipobjecteyez(clip_x, clip_y, clip_z);
	*out_sx = transfm2_getscreencoordx(objecteyex, objecteyez);
	*out_sy = transfm2_getscreencoordy(objecteyey, objecteyez);
}

// FUNCTION: TIE 0x31010
void maproom_drawmapitem(uint16_t obj_idx, uint16_t selected_obj_ref, char num_label, int32_t z) {
	/* --- Phase 1: world position lookup --- */
	if (obj_idx >= MAP_FLIGHT_SLOT_COUNT) {
		/* Static slot: world coord is int16 / 256, scale up by <<8.
		 * The binary uses unaligned-dword loads + sar 16 here; we use
		 * the explicit field accesses since the StaticObject struct is
		 * pack(1) in C as well. */
		const uint16_t static_idx = (uint16_t)(obj_idx - MAP_FLIGHT_SLOT_COUNT);
		worldx = (int32_t)staticobjects[static_idx].world_x * 256;
		worldy = (int32_t)staticobjects[static_idx].world_y * 256;
		worldz = (int32_t)staticobjects[static_idx].world_z * 256;
	} else {
		worldx = objects[obj_idx].world_x;
		worldy = objects[obj_idx].world_y;
		worldz = objects[obj_idx].world_z;
	}

	/* --- Phase 2: camera-relative + project --- */
	worldx -= camera.x;
	worldy -= camera.y;
	worldz -= camera.z;

	const int32_t eyex = transfm2_geteyex(worldx, worldy, worldz);
	const int32_t eyey = transfm2_geteyey(worldx, worldy, worldz);
	const int32_t screen_x = transfm2_getscreencoordx(eyex, z);
	const int32_t screen_y = transfm2_getscreencoordy(eyey, z);

	festring_settextcolor(MAP_TC_DEFAULT);

	/* --- Phase 3: side dispatch -> background colour + icon offset --- */
	int icon_only = 0;
	uint16_t color = 0; /* 0 = no highlight */
	int16_t side_icon_offset_saved;
	uint8_t side;

	if (obj_idx < MAP_FLIGHT_SLOT_COUNT) {
		side = objects[obj_idx].side;
	} else {
		side = fg_array[staticobjects[obj_idx - MAP_FLIGHT_SLOT_COUNT].fg_idx].side;
	}

	if (side == 0) {
		if (hostileflag == 1)
			icon_only = 1;
		festring_setbackcolor(MAP_BG_HOSTILE);
		side_icon_offset_saved = 0;
	} else if (side == 1 || side == 4) {
		if (imperialflag == 1)
			icon_only = 1;
		festring_setbackcolor(MAP_BG_IMPERIAL);
		side_icon_offset_saved = (int16_t)(2 * maxMapIcons);
	} else if (side == 2) {
		if (neutralflag == 1)
			icon_only = 1;
		festring_setbackcolor(MAP_BG_NEUTRAL);
		side_icon_offset_saved = (int16_t)(3 * maxMapIcons);
	} else {
		if (neutralflag == 1)
			icon_only = 1;
		festring_setbackcolor(MAP_BG_OTHER);
		side_icon_offset_saved = (int16_t)maxMapIcons;
	}

	/* --- Phase 4: ship_idx + selected/target highlight --- */
	uint16_t ship_idx;
	if (obj_idx < MAP_FLIGHT_SLOT_COUNT) {
		ship_idx = objects[obj_idx].ship_idx;
		if (obj_idx == selected_obj_ref) {
			festring_setbackcolor(MAP_BG_SELECTED);
			color = MAP_BG_SELECTED;
			icon_only = 0;
		}
		if (obj_idx == pstate.target_obj_idx) {
			color = MAP_BG_TARGET;
			festring_setbackcolor(MAP_BG_TARGET);
			icon_only = 0;
		}
	} else {
		ship_idx = staticobjects[obj_idx - MAP_FLIGHT_SLOT_COUNT].species;
		const uint16_t static_obj_ref = (uint16_t)(obj_idx + MAP_STATIC_BIAS);
		if (static_obj_ref == selected_obj_ref) {
			festring_setbackcolor(MAP_BG_SELECTED);
			icon_only = 0;
			color = MAP_BG_SELECTED;
		}
		if (static_obj_ref == pstate.target_obj_idx) {
			festring_setbackcolor(MAP_BG_TARGET);
			color = MAP_BG_TARGET;
			icon_only = 0;
		}
	}

	/* --- Phase 5: missile-target indicator (locked target only) --- */
	if (obj_idx < MAP_FLIGHT_SLOT_COUNT && obj_idx == pstate.target_obj_idx) {
		/* AI craft (idx>=NUM_CRAFTS, i.e. warhead slots) read missile_target;
		 * player+wingmen (idx<NUM_CRAFTS) read the ai_target_ref linked-target
		 * field (per the binary's split). */
		uint16_t mt;
		if (obj_idx >= NUM_CRAFTS)
			mt = objects[obj_idx].craft_ptr->missile_target;
		else
			mt = (uint16_t)objects[obj_idx].craft_ptr->ai_target_ref;

		create_getworldposition(mt, 0);
		worldlocx -= camera.x;
		worldlocy -= camera.y;
		worldlocz -= camera.z;

		int32_t mx, my;
		maproom_project(worldlocx, worldlocy, worldlocz, eyex, eyey, z, &mx, &my);
		maproom_draw_line(mx, my, screen_x, screen_y, fontcolors[10]);
	}

	/* --- Phase 6: ground-projection vertical line --- */
	int32_t ground_screen_x, ground_screen_y;
	maproom_project(worldx, worldy, -65536 - camera.z, eyex, eyey, z, &ground_screen_x, &ground_screen_y);
	maproom_draw_line(ground_screen_x, ground_screen_y, screen_x, screen_y, backcolor);

	/* --- Phase 7: velocity-vector line (active flight objects only) --- */
	if (obj_idx < MAP_FLIGHT_SLOT_COUNT) {
		if (objects[obj_idx].orient_dirty) {
			fview_calcrotatemove(objects[obj_idx].heading, objects[obj_idx].pitch, &objects[obj_idx]);
			fview_calcrotateorient(objects[obj_idx].roll, 0, &objects[obj_idx]);
		}

		/* Watcom unaligned-load resolution: `*(int*)&move_dirty >> 16`
		 * reads the moveX field at offset +0x3A, NOT move_dirty (+0x38).
		 * Likewise `*(int*)&moveX >> 16` reads moveY at +0x3C. */
		const int32_t mvX = (int32_t)objects[obj_idx].moveX;
		const int32_t mvY = (int32_t)objects[obj_idx].moveY;

		/* Initial nudge: the binary's `<< 8 >> 15` is mathematically
		 * `>> 7` for sar; rewrite that way to avoid the C UB of
		 * left-shifting a negative signed int. */
		worldx += mvX >> 7;
		worldy += mvY >> 7;

		const uint16_t cs = (uint16_t)objects[obj_idx].current_speed;
		if (cs < 0x400u) {
			/* Slow craft: extra step proportional to current_speed. */
			worldx += (mvX * 32 * (int32_t)cs) >> 15;
			worldy += (mvY * 32 * (int32_t)cs) >> 15;
		} else {
			/* Fast craft: add the raw move vector once. */
			worldx += mvX;
			worldy += mvY;
		}

		int32_t vx, vy;
		maproom_project(worldx, worldy, -65536 - camera.z, objecteyex, objecteyey, objecteyez, &vx, &vy);
		maproom_draw_line(vx, vy, ground_screen_x, ground_screen_y, backcolor);
	}

	/* --- Phase 8: viewport-cull + icon + label + distance --- */
	festring_setbackcolor(MAP_PANEL_BG);

	if (mapScreenLeft - 32 > screen_x)
		return;
	if (mapScreenRight + 32 < screen_x)
		return;
	if (mapScreenTop - 32 > screen_y)
		return;
	if (mapScreenBottom + 32 < screen_y)
		return;

	/* Pick the base icon variant; species index > 0x69 falls back to icon 19 (generic). */
	const uint16_t base_icon_idx = (ship_idx > 0x69u) ? 19 : (uint16_t)species2icon[ship_idx];

	/* Phase 8a: optional name label above the icon (not in icon-only mode). */
	if (!icon_only) {
		uint16_t name_obj_ref;
		int build_name = 1;

		if (obj_idx >= MAP_FLIGHT_SLOT_COUNT) {
			/* ship_class == 8 = mines: don't print a name (binary clears
			 * tempstring[0] to skip the label entirely). */
			if (staticobjects[obj_idx - MAP_FLIGHT_SLOT_COUNT].ship_class == 8) {
				tempstring[0] = 0;
				build_name = 0;
			}
			name_obj_ref = (uint16_t)(obj_idx + MAP_STATIC_BIAS);
		} else {
			name_obj_ref = obj_idx;
		}

		if (build_name)
			panel_buildobjectname(name_obj_ref, 2);

		if (tempstring[0]) {
			/* Optional 1-based digit suffix " (N)": '/' + num_label maps
			 * num_label=1 -> '0', num_label=2 -> '1', ... matching the
			 * binary's farstradd(num_label + 47). */
			if (num_label) {
				festring_farstradd(' ');
				festring_farstradd('(');
				festring_farstradd((char)(num_label + '/'));
				festring_farstradd(')');
			}
			const int16_t name_y =
				(int16_t)(screen_y - ((int32_t)iconysize[base_icon_idx] >> 1) - fontheight - 1);
			const int16_t name_w = sys2_calclength((const uint8_t*)tempstring);
			festring_setcursor((int16_t)(screen_x - name_w / 2), name_y);
			festring_outstring((const uint8_t*)tempstring);
		}
	}

	/* Phase 8b: draw the icon (with optional fillbox highlight border). */
	const uint16_t final_icon_idx = (uint16_t)(base_icon_idx + side_icon_offset_saved);
	const int16_t icon_x = (int16_t)((uint16_t)screen_x - ((int32_t)iconxsize[base_icon_idx] >> 1));
	const int16_t icon_y = (int16_t)((uint16_t)screen_y - ((int32_t)iconysize[base_icon_idx] >> 1));

	if (color) {
		festring_setbackcolor((uint8_t)color);
		fillbox((uint16_t)(icon_x - 1), (uint16_t)(icon_y - 1),
				(uint16_t)(icon_x + iconxsize[base_icon_idx] + 1),
				(uint16_t)(icon_y + iconysize[base_icon_idx] + 1));
	}

	/* Margin clip: don't draw if the icon would cross any margin. */
	if ((int16_t)icon_x >= leftmargin &&
		(int16_t)(icon_x + iconxsize[base_icon_idx]) < (int)(uint16_t)rightmargin &&
		(int16_t)icon_y >= topmargin &&
		(int16_t)(icon_y + iconysize[base_icon_idx]) < (int)(uint16_t)bottommargin) {
		drawshape(farbufferptrs[final_icon_idx], (uint16_t)icon_x, (uint16_t)icon_y, 0, 0);
	}

	/* Phase 8c: distance text below the icon (skipped in icon-only mode). */
	festring_setbackcolor(MAP_PANEL_BG);
	if (icon_only)
		return;

	const int16_t icon_center_x = (int16_t)(((int32_t)iconxsize[base_icon_idx] >> 1) + (uint16_t)icon_x);
	const int16_t icon_bottom_y = (int16_t)(iconysize[base_icon_idx] + (int16_t)icon_y);

	const uint16_t dist_obj_ref =
		(obj_idx >= MAP_FLIGHT_SLOT_COUNT) ? (uint16_t)(obj_idx + MAP_STATIC_BIAS) : obj_idx;
	pai_distancebetween(selected_obj_ref, dist_obj_ref);

	/* The "00" string is just a width template (= 2 character widths). */
	const int16_t two_digit_w = sys2_calclength((const uint8_t*)"00");
	festring_setcursor((int16_t)(icon_center_x - two_digit_w), (int16_t)(icon_bottom_y + 1));

	/* trig2_polardistance is the polar distance in raw map units; *161/65536
	 * scales it into 0.00..99.99 MGLT units (capped at 9999). */
	trig2_polardistance *= 161;
	int32_t dist_scaled = trig2_polardistance >> 16;
	if (((uint32_t)trig2_polardistance >> 16) >= 0x2710u)
		dist_scaled = 9999;
	const uint16_t dist_int = (uint16_t)dist_scaled;
	const uint16_t dist_int_part = (uint16_t)(dist_int / 100);
	panelrts_outnum(dist_int_part, 2, 1);
	outchar('.');
	panelrts_outnum((uint16_t)(dist_int - 100 * dist_int_part), 2, 2);
}

/* --- maproom_maproom: per-frame helpers ------------------------------------ */

/*
 * Per-FG cluster pre-pass. For each fg group, walk the 28 craft slots
 * twice: first to find a slot in this fg, then to find pairs and the
 * max pairwise rough-distance. If the group is "tight" (3*pair_count *
 * bound_hwidth > max_pairwise_dist), mark fg_render_count[fg] = 1 so
 * the draw loop will show only the first 2 members with full detail.
 *
 * fg_render_count[] is the 48-byte stack array embedded in the IDA z[]
 * frame at z[175..]. Cleared to 0 at the top of each frame.
 */
static void maproom_cluster_pass(uint8_t fg_render_count[48], uint16_t focus_obj_ref) {
	for (int fg = 0; fg < 48; fg++)
		fg_render_count[fg] = 0;

	for (uint16_t fg = 0; fg < 48; fg++) {
		for (uint16_t i = 0; i < MAP_CRAFT_SCAN; i++) {
			if (!objects[i].ship_idx)
				continue;
			if (objects[i].fg_idx != fg)
				continue;
			if (fg_render_count[objects[i].fg_idx])
				continue;
			if (i == pstate.target_obj_idx)
				continue;
			if (i == focus_obj_ref)
				continue;
			if (!maproom_side_visible(objects[i].side))
				continue;

			int32_t max_pair = 0;
			uint16_t pair_cnt = 1;
			for (uint16_t j = 0; j < MAP_CRAFT_SCAN; j++) {
				if (!objects[j].ship_idx)
					continue;
				if (objects[j].fg_idx != fg)
					continue;
				if (j == pstate.target_obj_idx || j == focus_obj_ref) {
					pair_cnt = 1;
					break;
				}
				if (j == i)
					continue;
				if (!maproom_side_visible(objects[j].side))
					continue;
				pai_roughdistancebetween(i, j);
				pair_cnt++;
				if (roughdistance > max_pair)
					max_pair = roughdistance;
			}

			if (pair_cnt != 1 &&
				(int16_t)(3 * pair_cnt) * species_table[objects[i].ship_idx].bound_hwidth > max_pair) {
				fg_render_count[fg] = 1;
			}
		}
	}
}

/*
 * Insert local index `idx` into a sorted-by-deepest-Z list. Walks the
 * existing list, finds the slot where the new item's eye-Z is larger
 * than the current entry (= deeper), then memmove-shifts the tail down.
 *
 * sort_indices[] is a parallel byte array storing local indices; the
 * eye-Z values live in z[] indexed by the same local idx (so at
 * z[sort_indices[k]]).
 */
static void maproom_sort_insert(const int32_t z_buf[140], uint8_t sort_indices[MAP_SORT_CAP], uint8_t* count,
								uint8_t local_idx, int32_t eye_z) {
	int k;
	for (k = 0; k < *count; k++) {
		if (eye_z > z_buf[sort_indices[k]])
			break;
	}
	/* Shift down (right). */
	for (int m = *count; m > k; m--)
		sort_indices[m] = sort_indices[m - 1];
	sort_indices[k] = local_idx;
	(*count)++;
}

/*
 * Z-sort the 76 active flight objects + 64 static objects (skipping
 * empty / filtered slots) into sort_indices[] back-to-front. Only items
 * with positive eye-Z (in front of the eye plane) are sorted.
 */
static void maproom_zsort(int32_t z_buf[140], uint8_t sort_indices[MAP_SORT_CAP], uint8_t* sort_count,
						  uint8_t fg_render_count[48], uint16_t focus_obj_ref) {
	*sort_count = 0;

	/* --- Pass A: 76 active flight objects --- */
	for (uint16_t i = 0; i < MAP_FLIGHT_SLOT_COUNT; i++) {
		const uint8_t ship_idx = objects[i].ship_idx;
		if (!ship_idx)
			continue;

		const int is_target = (i == pstate.target_obj_idx);
		const int is_focus = (i == focus_obj_ref);
		if (!is_target && !is_focus && !maproom_side_visible(objects[i].side))
			continue;

		const uint8_t genus = objects[i].genus;
		if (genus == GENUS_PROJECTILE_PLAYER || genus == GENUS_PROJECTILE_NPC) {
			/* Warhead/laser: only show if the craft has a player target
			 * AND warhead filter isn't off. */
			if (!objects[i].craft_ptr->species_idx)
				continue;
			if (warheadflag == 2)
				continue;
		} else if (genus > GENUS_PLATFORM) {
			/* Genus 8 and above (mines, debris, etc.) skip in the active
			 * pass; the static pass handles them. */
			continue;
		}

		/* Cluster cap: for a "tight" fg (fg_render_count[]==1 set by the
		 * pre-pass), only the FIRST member encountered here gets z-sorted
		 * (the binary's `(byte<2)` test increments to 2 on the first hit
		 * and to 3 on the second; 2<2 is false so the second one skips).
		 * Subsequent members fall through to drawmapitem with num_label
		 * = the post-increment count, which the renderer prints as the
		 * '(N)' digit suffix. */
		const uint8_t fg_count = fg_render_count[objects[i].fg_idx];
		if (fg_count) {
			fg_render_count[objects[i].fg_idx] = (uint8_t)(fg_count + 1);
			if (fg_count >= 2)
				continue;
		}

		const int32_t wx = objects[i].world_x - camera.x;
		const int32_t wy = objects[i].world_y - camera.y;
		const int32_t wz = objects[i].world_z - camera.z;
		worldx = wx;
		worldy = wy;
		worldz = wz;
		const int32_t ez = transfm2_geteyez(wx, wy, wz);
		objecteyez = ez;
		if (ez <= 0)
			continue;
		z_buf[i] = ez;
		/* DEVIATION FROM BINARY: bound the sort_indices[] array at
		 * MAP_SORT_CAP. The binary doesn't bounds-check; it would corrupt
		 * adjacent stack memory (the fg_render_count[] table sits ~92
		 * bytes after sort_indices end) when sort_count exceeds 48. In
		 * practice the binary rarely hits the cap because the side / NHI
		 * filters trim hard. */
		if (*sort_count >= MAP_SORT_CAP)
			continue;
		maproom_sort_insert(z_buf, sort_indices, sort_count, (uint8_t)i, ez);
	}

	/* --- Pass B: 64 static objects --- */
	uint8_t static_local_idx = MAP_FLIGHT_SLOT_COUNT;
	for (uint16_t i = 0; i < 64; i++, static_local_idx++) {
		const uint8_t species_id = staticobjects[i].species;
		if (!species_id)
			continue;

		const uint16_t obj_ref = (uint16_t)(i + OBJ_REF_STATIC_BASE);
		const int is_target = (obj_ref == pstate.target_obj_idx);
		const int is_focus = (obj_ref == focus_obj_ref);
		if (!is_target && !is_focus && !maproom_side_visible(fg_array[staticobjects[i].fg_idx].side))
			continue;

		const uint8_t ship_class = staticobjects[i].ship_class;
		/* Only mines / planets (ship_class 8/9) participate in the
		 * static z-sort pass; other classes skip per the binary. */
		if (ship_class < 8u || ship_class > 9u)
			continue;

		const int32_t wx = (int32_t)staticobjects[i].world_x * 256 - camera.x;
		const int32_t wy = (int32_t)staticobjects[i].world_y * 256 - camera.y;
		const int32_t wz = (int32_t)staticobjects[i].world_z * 256 - camera.z;
		worldx = wx;
		worldy = wy;
		worldz = wz;
		const int32_t ez = transfm2_geteyez(wx, wy, wz);
		objecteyez = ez;
		if (ez <= 0)
			continue;
		z_buf[MAP_FLIGHT_SLOT_COUNT + i] = ez;
		if (*sort_count >= MAP_SORT_CAP)
			continue;
		maproom_sort_insert(z_buf, sort_indices, sort_count, static_local_idx, ez);
	}
}

/*
 * Draw 33 vertical 1-megaunit-spaced tick lines along world axis `axis`
 * (0 = X, 1 = Y), centered on the focus point's world coordinates.
 * The base of each tick lies on the ground plane (worldlocz=-65536); the
 * top is offset by one normalized unit along the corresponding eye basis
 * vector (worldeyeA{1,2,3} for X-axis, worldeyeB{1,2,3} for Y-axis).
 */
static void maproom_draw_axis(int axis) {
	int32_t origin_x = worldlocx - 0x100000;
	int32_t origin_y = worldlocy - 0x100000;

	/* Per-axis basis-vector selectors. */
	const int32_t b1 = (axis == 0) ? worldeyeA1 : worldeyeB1;
	const int32_t b2 = (axis == 0) ? worldeyeA2 : worldeyeB2;
	const int32_t b3 = (axis == 0) ? worldeyeA3 : worldeyeB3;

	for (int i = 0; i < 33; i++) {
		const int32_t wx = origin_x - camera.x;
		const int32_t wy = origin_y - camera.y;
		const int32_t wz = -65536 - camera.z;
		worldx = wx;
		worldy = wy;
		worldz = wz;

		/* Step the active axis by 1 megaunit (= +1 in HIWORD). */
		if (axis == 0)
			origin_y = (int32_t)((uint32_t)origin_y + 0x10000u);
		else
			origin_x = (int32_t)((uint32_t)origin_x + 0x10000u);

		const int32_t base_ez = transfm2_geteyez(wx, wy, wz);
		int32_t top_ez = base_ez + b3 * 64;
		objecteyez = base_ez;
		if (top_ez <= 0 && base_ez <= 0)
			continue;

		objecteyex = transfm2_geteyex(wx, wy, wz);
		const int32_t base_ey = transfm2_geteyey(wx, wy, wz);
		objecteyey = base_ey;
		int32_t top_ex = objecteyex + b1 * 64;
		int32_t top_ey = base_ey + b2 * 64;

		/* If only the BASE eye-z is positive (behind the eye line), swap
		 * the line endpoints so the base point becomes the projected top
		 * and vice versa; mirrors the binary's swap dance. */
		if (top_ez <= 0) {
			const int32_t tmp_z = top_ez;
			top_ez = objecteyez;
			objecteyez = tmp_z;
			const int32_t tmp_y = top_ey;
			top_ey = base_ey;
			objecteyey = base_ey + b2 * 64;
			const int32_t tmp_x = top_ex;
			top_ex = objecteyex;
			objecteyex = tmp_x;
			(void)tmp_y;
		}

		if (objecteyez <= 0)
			transfm2_clipobjecteyez(top_ex, top_ey, top_ez);

		const int32_t sy_top = transfm2_getscreencoordy(top_ey, top_ez);
		const int32_t sx_top = transfm2_getscreencoordx(top_ex, top_ez);
		const int32_t sy_base = transfm2_getscreencoordy(objecteyey, objecteyez);
		const int32_t sx_base = transfm2_getscreencoordx(objecteyex, objecteyez);
		maproom_draw_line(sx_base, sy_base, sx_top, sy_top, MAP_AXIS_LINE);
	}
}

/*
 * Render-loop helper: walk sort_indices[] and draw items that are on
 * the indicated side of the camera's z=-65536 plane.
 *   side > 0 : draw items in FRONT of the camera (after world axes)
 *   side < 0 : draw items BEHIND the camera (before world axes)
 */
static void maproom_render_pass(const uint8_t sort_indices[MAP_SORT_CAP], uint8_t sort_count,
								const int32_t z_buf[140], const uint8_t fg_render_count[48],
								uint16_t focus_obj_ref, int side) {
	for (uint8_t i = 0; i < sort_count; i++) {
		const uint8_t local_idx = sort_indices[i];
		const int32_t item_z = maproom_local_world_z(local_idx);

		/* Painter's-algorithm cull. The world-axes grid is drawn between
		 * the two passes (at z=ground plane); the backward pass therefore
		 * paints items on the OPPOSITE side of the ground plane from the
		 * camera (so the axes occlude them) and the forward pass paints
		 * items on the SAME side (so the items occlude the axes). */
		if (camera.z < -65536) {
			/* Camera below ground plane. */
			if (side > 0 && item_z >= -65536)
				continue; /* fwd: skip if above */
			if (side < 0 && item_z < -65536)
				continue; /* bwd: skip if below */
		} else {
			/* Camera at/above ground plane. */
			if (side > 0 && item_z < -65536)
				continue; /* fwd: skip if below */
			if (side < 0 && item_z >= -65536)
				continue; /* bwd: skip if above */
		}

		/* Static-object indices intentionally preserve the binary's out-of-range
		 * FG suffix lookup, whose displayed digit is indeterminate. */
		const uint8_t num_label = fg_render_count[objects[local_idx].fg_idx];
		maproom_drawmapitem(local_idx, focus_obj_ref, (char)num_label, z_buf[local_idx]);
	}
}

/*
 * View-transition step: animates camera heading between 0x4800 (side-on)
 * and 0x7FFF (top-down) over 118 ticks. Mid-transition we translate the
 * camera away from the focus by `camera_distance` along the eye-Z axis,
 * recompute the view matrix at the interpolated heading, then translate
 * back so the focus point stays anchored on screen.
 *
 * Returns the new view_transition_active flag (0 once the transition
 * finishes).
 */
static int maproom_view_transition(int32_t camera_distance, uint16_t view_mode,
								   uint16_t view_transition_progress, int16_t view_heading,
								   int16_t view_pitch) {
	/* Translate camera back along eye-Z so the focus is the rotation centre. */
	int32_t remaining = camera_distance;
	int stepback = 0;
	while (remaining > 0x7FFF) {
		stepback++;
		remaining -= 0x7FFF;
		camera.x += worldeyeA3;
		camera.y += worldeyeB3;
		camera.z += worldeyeC3;
	}
	camera.x += (worldeyeA3 * (int16_t)remaining) >> 15;
	camera.y += (worldeyeB3 * (int16_t)remaining) >> 15;
	camera.z += (worldeyeC3 * (int16_t)remaining) >> 15;

	int16_t anim_heading;
	int16_t anim_pitch;
	int active = 1;

	/* `view_mode` is the TARGET orientation: 0 = top-down, 1 = side-on.
	 * (Initial state is 0, snap-to-top via the >=0x76 branch.) The
	 * transition therefore animates FROM the OPPOSITE orientation
	 * TO the one selected by `view_mode`. */
	if (view_mode) {
		/* Top -> Side transition. progress=4 -> heading near 0x7FFF (top);
		 * progress>=0x76 -> snap to view_heading (side). */
		if (view_transition_progress >= 0x76u) {
			fview_newcalcview(0, view_heading, view_pitch, 0, 0, 0, NULL);
			active = 0;
		} else {
			anim_heading = (int16_t)(((MAP_VIEW_TRANSITION - view_transition_progress) *
									  (0x7FFF - (uint16_t)view_heading)) /
										 MAP_VIEW_TRANSITION +
									 view_heading);
			anim_pitch = view_pitch;
			fview_newcalcview(0, anim_heading, anim_pitch, 0, 0, 0, NULL);
		}
	} else {
		/* Side -> Top transition. progress=4 -> heading near view_heading
		 * (side); progress>=0x76 -> snap to 0x7FFF (top). */
		if (view_transition_progress >= 0x76u) {
			fview_newcalcview(0, 0x7FFF, view_pitch, 0, 0, 0, NULL);
			active = 0;
		} else {
			anim_pitch = view_pitch;
			anim_heading = (int16_t)(0x7FFF - ((MAP_VIEW_TRANSITION - view_transition_progress) *
											   (0x7FFF - (uint16_t)view_heading)) /
												  MAP_VIEW_TRANSITION);
			fview_newcalcview(0, anim_heading, anim_pitch, 0, 0, 0, NULL);
		}
	}

	/* Translate the camera back to its original position. */
	camera.x -= (worldeyeA3 * (int16_t)remaining) >> 15;
	camera.y -= (worldeyeB3 * (int16_t)remaining) >> 15;
	camera.z -= (worldeyeC3 * (int16_t)remaining) >> 15;
	while (stepback-- > 0) {
		camera.x -= worldeyeA3;
		camera.y -= worldeyeB3;
		camera.z -= worldeyeC3;
	}

	return active;
}

/* --- maproom_maproom ------------------------------------------------------ */

/*
 * Closest-target search shared by 'r' and 'u' keys: walk objects[0..27],
 * keep the entry whose `score` (computed by `cmp`) is lowest. Returns
 * the local index, or 0xFFFF if no match.
 */
static uint16_t maproom_find_min(uint32_t (*score)(uint16_t i, void* ud), int (*filter)(uint16_t i, void* ud),
								 void* ud) {
	uint16_t best_idx = 0xFFFF;
	uint32_t best_score = 0xFFFFFFFFu;
	for (uint16_t i = 0; i < MAP_CRAFT_SCAN; i++) {
		if (!objects[i].ship_idx)
			continue;
		if (!filter(i, ud))
			continue;
		const uint32_t s = score(i, ud);
		if (s < best_score) {
			best_score = s;
			best_idx = i;
		}
	}
	return best_idx;
}

static int filter_enemy_disabled_or_alive(uint16_t i, void* ud) {
	(void)ud;
	if (i == pstate.object_idx)
		return 0;
	if (objects[i].side == objects[pstate.object_idx].side)
		return 0;
	const uint8_t g = objects[i].genus;
	if (g == GENUS_FREIGHTER || g == GENUS_STARSHIP || g == GENUS_PLATFORM)
		return 0;
	const uint8_t ff = objects[i].craft_ptr->flight_flag;
	return (ff == 0 || ff == 6);
}

static uint32_t score_trig2_polardistance_to_player(uint16_t i, void* ud) {
	(void)ud;
	pai_distancebetween(pstate.object_idx, i);
	return (uint32_t)trig2_polardistance;
}

static int filter_unattended_other(uint16_t i, void* ud) {
	(void)ud;
	if (i == pstate.object_idx)
		return 0;
	return objects[i].craft_ptr->leader_obj_idx == 255;
}

static uint32_t score_age_ticks(uint16_t i, void* ud) {
	(void)ud;
	return (uint32_t)(uint16_t)objects[i].age_ticks;
}

/*
 * Swap each entry of farbufferptrs[i] (current-mode shape table) and
 * mapfarbufferptrs[i] (saved). After this call, farbufferptrs[] holds
 * whichever pointer set we want active for the next render path.
 *
 * `mapiconsloaded == 0` means we've never loaded the icons before, so
 * just save the existing panel pointers (no swap) and let the caller
 * issue fediskio_loadbufferdata to populate farbufferptrs[] with icons.
 */
static void maproom_swap_buffer_ptrs(int do_swap) {
	if (do_swap) {
		for (int i = 0; i < 265; i++) {
			void* tmp = mapfarbufferptrs[i];
			mapfarbufferptrs[i] = (void*)farbufferptrs[i];
			farbufferptrs[i] = (uint8_t*)tmp;
		}
	} else {
		for (int i = 0; i < 265; i++)
			mapfarbufferptrs[i] = (void*)farbufferptrs[i];
	}
}

typedef enum {
	MAPROOM_PHASE_FRAME = 0,
} MaproomPhase;

typedef struct MaproomTask {
	uint16_t view_mode;
	uint16_t view_transition_progress;
	int view_transition_active;
	int16_t view_heading;
	int16_t view_pitch;
	int32_t camera_distance;
	int8_t page_delta;
	int buffer_toggle;
	uint16_t focus_obj_ref;
	MaproomPhase phase;
} MaproomTask;

/* Per-frame scratch kept outside the size-limited task state. */
static int32_t s_map_z_buf[140];
static uint8_t s_map_sort_indices[MAP_SORT_CAP];
static uint8_t s_map_fg_render_count[48];
static uint8_t s_map_sort_count;

static void maproom_draw_line(int32_t x1, int32_t y1, int32_t x2, int32_t y2, uint8_t color) {
	if (TieClassicDisplay_UsesDx5())
		logbuf2_drawclippedline_tie98(x1, y1, x2, y2, color);
	else
		logbuf2_drawclippedline(x1, y1, x2, y2, color);
}

static void maproom_clear_buffer(bool tie98_display) {
	if (tie98_display)
		logbuf2_clearbuffer_tie98();
	else
		logbuf2_clearbuffer();
}

static void maproom_output_buffer(bool tie98_display, const void* src) {
	if (tie98_display)
		logbuf2_outbuffer_tie98(src);
	else
		logbuf2_outbuffer(src);
}

static void maproom_output_diff_buffer(bool tie98_display, const void* oldbuf, const void* newbuf) {
	if (tie98_display)
		logbuf2_outdiffbuffer_tie98(oldbuf, newbuf);
	else
		logbuf2_outdiffbuffer(oldbuf, newbuf);
}

static LandruTaskStepResult maproom_task_step(void* self);

static const LandruTaskVtable maproom_task_vt = {
	.step = maproom_task_step,
};

// FUNCTION: TIE98 0x451470 (task-split recovery)
void maproom_Push_MapRoom_Task(void) {
	if (TieProfile_UsesTie98Logic()) {
		uint8_t saved_mapflag = mapflag;
		mapflag = 1;
		FSFX_UpdatePlayerEngineSound();
		mapflag = saved_mapflag;
	}
	const bool tie98_display = TieClassicDisplay_UsesDx5();
	if (tie98_display)
		FlightSurface_Lock();
	/* --- Stage 1: layout setup based on resolution --- */
	mapScreenLeft = 0;
	if (tie_is_high_resolution_flight()) {
		mapScreenRight = 640;
		mapScreenTop = 41;
		mapScreenBottom = 442;
		maxMapIcons = 64;
		species2icon = species2icon640;
		iconxsize = iconxsize640;
		iconysize = iconysize640;
		iconfilename = iconfilename640;
	} else {
		mapScreenRight = 320;
		mapScreenTop = 17;
		mapScreenBottom = 184;
		maxMapIcons = 66;
		species2icon = species2icon320;
		iconxsize = iconxsize320;
		iconysize = iconysize320;
		iconfilename = iconfilename320;
	}
	mapScreenWidth = mapScreenRight - mapScreenLeft;
	mapScreenHeight = mapScreenBottom - mapScreenTop;

	/* --- Stage 2: take ownership of the icon buffer --- */
	/* The binary locks/unlocks the same handle here; with malloc the
	 * pointer is always valid. Layout: first 1060 bytes = 265-entry
	 * pointer table (mapfarbufferptrs); rest = shape data area. */
	mapfarbufferptrs = (void**)maproomicons_buf;
	farbufferptr = (uint8_t*)maproomicons_buf + 1060;

	if (mapiconsloaded) {
		/* Subsequent entries: swap saved-icons<->panel-mode pointers. */
		maproom_swap_buffer_ptrs(/*do_swap=*/1);
	} else {
		/* First entry: save current panel pointers, then load icons. */
		char iconpath[32];
		snprintf(iconpath, sizeof(iconpath), "%s%s", resourcedir, iconfilename);
		maproom_swap_buffer_ptrs(/*do_swap=*/0);
		fediskio_loadbufferdata(iconpath, 0, (int16_t)(4 * maxMapIcons), 0);
		mapiconsloaded = 1;
	}

	/* --- Stage 3: reset display + initial camera --- */
	festring_setfontsize(2);
	dropflag = 0;

	const uint32_t buffer_line_offset = calcposition((uint16_t)mapScreenLeft, (uint16_t)mapScreenTop);
	if (tie98_display)
		logbuf2_setbufferdimensions_tie98((uint16_t)mapScreenWidth, (uint16_t)mapScreenHeight, 1,
										  buffer_line_offset);
	else
		logbuf2_setbufferdimensions((uint16_t)mapScreenWidth, (uint16_t)mapScreenHeight, buffer_line_offset);

	fview_newcalcview(0, 0x7FFF, pstate.player->pitch, 0, 0, 0, NULL);

	maproom_setcamerafocus(pstate.object_idx, MAP_CAMERA_DEFAULT);
	if (TieProfile_UsesTie98Logic())
		g_flightInitialTextureCacheFlushPending = 1;
	fullupdateflag = 1;
	logbuf2_selectbuffer(newbuf);
	if (tie98_display)
		FlightSurface_Unlock();

	MaproomTask* t = (MaproomTask*)landru_task_push(&maproom_task_vt);
	if (!t)
		return;
	t->view_mode = 0; /* 0 = side, 1 = top-down */
	t->view_transition_active = 1;
	t->view_transition_progress = 236;
	t->view_heading = 0x4800; /* initial side-on heading */
	t->view_pitch = pstate.player->pitch;
	t->camera_distance = MAP_CAMERA_DEFAULT;
	t->page_delta = 0;
	t->buffer_toggle = 1;
	t->focus_obj_ref = pstate.object_idx;
	t->phase = MAPROOM_PHASE_FRAME;
}

/* One frame of maproom work: view-transition step + render. Called
 * by maproom_task_step once the 4-PIT-tick frame budget has elapsed
 * (frameticks pre-loaded). Input polling is split out into
 * maproom_poll_once so the host loop can interleave between frames. */
static void maproom_step_frame(MaproomTask* t) {
	if (t->view_transition_active) {
		t->view_transition_active = maproom_view_transition(
			t->camera_distance, t->view_mode, t->view_transition_progress, t->view_heading, t->view_pitch);
	}

	festring_setbound(0, 0, (int16_t)mapScreenWidth, (int16_t)mapScreenHeight);
	backcolor = MAP_BG_COLOR;
	festring_setfontsize(2);
	festring_setlinewrap(0);
	festring_setautofill(0);

	/* Cluster pre-pass + z-sort. */
	maproom_cluster_pass(s_map_fg_render_count, t->focus_obj_ref);
	maproom_zsort(s_map_z_buf, s_map_sort_indices, &s_map_sort_count, s_map_fg_render_count,
				  t->focus_obj_ref);

	/* Render-buffer fill. */
	const bool tie98_display = TieClassicDisplay_UsesDx5();
	maproom_clear_buffer(tie98_display);
	const uint16_t buffer_stride =
		(uint16_t)(mapScreenWidth * (tie98_display ? g_flight16bppBytesPerPixel : 1u));
	rtsvga2_setvgapointers(t->buffer_toggle ? newbuf : xtransdataptr, buffer_stride,
						   (uint16_t)mapScreenHeight);

	/* Backward pass: items behind the camera. */
	maproom_render_pass(s_map_sort_indices, s_map_sort_count, s_map_z_buf, s_map_fg_render_count,
						t->focus_obj_ref, /*side=*/-1);

	/* World axes around the focus point. */
	create_getworldposition(t->focus_obj_ref, 0);
	maproom_draw_axis(0); /* X-axis ticks */
	maproom_draw_axis(1); /* Y-axis ticks */

	/* Forward pass: items in front of the camera. */
	maproom_render_pass(s_map_sort_indices, s_map_sort_count, s_map_z_buf, s_map_fg_render_count,
						t->focus_obj_ref, /*side=*/+1);

	/* Status panels + page flip. */
	maproom_drawNHIstatus(t->view_mode);
	rtsvga2_setvgapointers(NULL, 0x140u, 0xC8u);

	if (fullupdateflag) {
		if (t->buffer_toggle) {
			maproom_output_buffer(tie98_display, newbuf);
			logbuf2_selectbuffer(xtransdataptr);
			t->buffer_toggle = 0;
		} else {
			maproom_output_buffer(tie98_display, xtransdataptr);
			logbuf2_selectbuffer(newbuf);
			t->buffer_toggle = 1;
		}
		fullupdateflag = 0;
	} else if (t->buffer_toggle) {
		maproom_output_diff_buffer(tie98_display, xtransdataptr, newbuf);
		logbuf2_selectbuffer(xtransdataptr);
		t->buffer_toggle = 0;
	} else {
		maproom_output_diff_buffer(tie98_display, newbuf, xtransdataptr);
		logbuf2_selectbuffer(newbuf);
		t->buffer_toggle = 1;
	}

	/* Backdrop+stars (drawbackdropflag temporarily forced off so
	 * BACKDRP2_backdrop only renders the parallax stars). TIE98 clears
	 * the pending cache flush immediately before and after this pair. */
	const uint8_t saved_backdrop = drawbackdropflag;
	if (TieProfile_UsesTie98Logic())
		g_flightInitialTextureCacheFlushPending = 0;
	drawbackdropflag = 0;
	backdrp2_backdrop();
	drawbackdropflag = saved_backdrop;
	rtsvga2_drawstars();
	if (TieProfile_UsesTie98Logic())
		g_flightInitialTextureCacheFlushPending = 0;
	fullupdateflag = 0;
}

/* Single input-poll iteration. Returns 1 if exit fires (KEY_m or
 * exit-class key), 2 if frame_dirty (caller should re-render), 0 if
 * nothing happened. */
static int maproom_poll_once(MaproomTask* t) {
	int frame_dirty = 0;

	tickcounter += xtimer_time_elapsed();
	if (tickcounter >= (uint16_t)MAP_FRAME_TICKS) {
		if (t->view_transition_progress < 0x76u) {
			t->view_transition_progress = (uint16_t)(t->view_transition_progress + tickcounter);
			frame_dirty = 1;
		}
		tickcounter = 0;
	}
	feinput_getrawinput();
	feinput_checkinput();
	feinput_degitterinput();
	inputdeltay *= 2;

	/* Key dispatch. The numeric ranges below match the IDA decompile
	 * exactly; do NOT collapse without re-checking the asm. */
	const uint16_t key = (uint16_t)inputkey;
	switch (key) {
		/* Page nav. */
		case 1:
			t->page_delta = -1;
			inputkey = 109;
			frame_dirty = 1;
			break;
		case 2:
			t->page_delta = 1;
			inputkey = 109;
			frame_dirty = 1;
			break;

		/* Exit (Esc / 'M' / 'Q' / 'm' / 'q' / 0xBB). The binary
		 * routes all of these to the same LABEL_245 (t->page_delta=0). */
		case 27:
		case 0x4D:
		case 0x51:
		case 0x6D:
		case 0x71:
		case 0xBB:
			t->page_delta = 0;
			inputkey = 109;
			frame_dirty = 1;
			break;

		/* View toggle (Space / 0xBE = ',' KEY_PAGE-toggle). */
		case 32:
		case 190: {
			t->view_mode = (t->view_mode == 0) ? 1 : 0;
			t->view_transition_progress = 4;
			t->view_transition_active = 1;
			frame_dirty = 1;
			break;
		}

		/* Numpad pan: 1..9 set inputdeltax/y. */
		case '1':
			inputdeltax = -MAP_PAN_DELTA;
			inputdeltay = MAP_PAN_DELTA;
			break;
		case '2':
			inputdeltax = 0;
			inputdeltay = MAP_PAN_DELTA;
			break;
		case '3':
			inputdeltax = MAP_PAN_DELTA;
			inputdeltay = MAP_PAN_DELTA;
			break;
		case '4':
			inputdeltax = -MAP_PAN_DELTA;
			inputdeltay = 0;
			break;
		case '6':
			inputdeltax = MAP_PAN_DELTA;
			inputdeltay = 0;
			break;
		case '7':
			inputdeltax = -MAP_PAN_DELTA;
			inputdeltay = -MAP_PAN_DELTA;
			break;
		case '8':
			inputdeltax = 0;
			inputdeltay = -MAP_PAN_DELTA;
			break;
		case '9':
			inputdeltax = MAP_PAN_DELTA;
			inputdeltay = -MAP_PAN_DELTA;
			break;

		/* 'a': closest attacker of the current target. */
		case 'a': {
			const uint16_t v = user_findclosestattacker(pstate.target_obj_idx);
			if (v != 0xFFFFu)
				pstate.target_obj_idx = v;
			frame_dirty = 1;
			break;
		}

		/* 'c': center camera on target. */
		case 'c': {
			t->focus_obj_ref = pstate.target_obj_idx;
			maproom_setcamerafocus(pstate.target_obj_idx, t->camera_distance);
			frame_dirty = 1;
			break;
		}

		/* 'e': closest attacker of the player. */
		case 'e': {
			const uint16_t v = user_findclosestattacker(pstate.object_idx);
			if (v != 0xFFFFu)
				pstate.target_obj_idx = v;
			frame_dirty = 1;
			break;
		}

		/* NHI / warhead filter cycles (0->1->2->0). */
		case 'h':
			hostileflag = (hostileflag == 2) ? 0 : (uint8_t)(hostileflag + 1);
			frame_dirty = 1;
			break;
		case 'i':
			imperialflag = (imperialflag == 2) ? 0 : (uint8_t)(imperialflag + 1);
			frame_dirty = 1;
			break;
		case 'n':
			neutralflag = (neutralflag == 2) ? 0 : (uint8_t)(neutralflag + 1);
			frame_dirty = 1;
			break;
		case 'w':
			warheadflag = (warheadflag == 2) ? 0 : (uint8_t)(warheadflag + 1);
			frame_dirty = 1;
			break;

		/* 'r': closest live enemy at any distance. */
		case 'r': {
			const uint16_t v =
				maproom_find_min(score_trig2_polardistance_to_player, filter_enemy_disabled_or_alive, NULL);
			if (v != 0xFFFFu)
				pstate.target_obj_idx = v;
			frame_dirty = 1;
			break;
		}

		/* 't': next radar target. */
		case 't': {
			if (pstate.target_obj_idx == 0xFFFFu)
				pstate.target_obj_idx = (uint16_t)pstate.radar_target0;
			pstate.target_obj_idx = user_picknexttarget(pstate.target_obj_idx, 1);
			frame_dirty = 1;
			break;
		}

		/* 'u': oldest unattended craft (no leader). */
		case 'u': {
			const uint16_t v = maproom_find_min(score_age_ticks, filter_unattended_other, NULL);
			if (v != 0xFFFFu)
				pstate.target_obj_idx = v;
			frame_dirty = 1;
			break;
		}

		/* 'y': previous radar target. */
		case 'y': {
			if (pstate.target_obj_idx == 0xFFFFu)
				pstate.target_obj_idx = (uint16_t)pstate.radar_target0;
			pstate.target_obj_idx = user_picknexttarget(pstate.target_obj_idx, -1);
			frame_dirty = 1;
			break;
		}

		default: {
			/* Quick-recall (F5-F7 ext, engine key codes 0xBF..0xC1). */
			if (key >= KEY_F5 && key <= KEY_F7) {
				uint16_t target = pstate.target_presets[key - KEY_F5];
				if (target != 0xFFFFu)
					pstate.target_obj_idx = target;
				frame_dirty = 1;
				/* Quick-save (Shift-F5..F7, engine key codes 0xD8..0xDA). */
			} else if (key >= KEY_SHIFT_F5 && key <= KEY_SHIFT_F7) {
				if (pstate.target_obj_idx != 0xFFFFu)
					pstate.target_presets[key - KEY_SHIFT_F5] = pstate.target_obj_idx;
			}
			break;
		}
	}

	/* Pan / rotate handling: in side-mode (or with Ctrl held / numpad
	 * keys) the deltas pan the camera in world XY; otherwise (top-mode
	 * and not Ctrl, not numpad) they rotate heading/pitch. */
	if (inputdeltax || inputdeltay) {
		const int16_t dx_adj = user_framerateadjust(inputdeltax);
		const int16_t dy_adj = user_framerateadjust(inputdeltay);
		if (dx_adj || dy_adj) {
			const int is_numpad = (inputkey >= KEY_1 && inputkey <= KEY_9);
			if (!t->view_mode || is_numpad || sys2_checkctrlkey()) {
				int32_t scratch = t->camera_distance >> 14;
				if (!scratch)
					scratch = 1;
				camera.x += ((worldeyeA1 * (int16_t)dx_adj) >> 15) * scratch;
				camera.y += ((worldeyeB1 * (int16_t)dx_adj) >> 15) * scratch;
				camera.z += scratch * ((worldeyeC1 * (int16_t)dx_adj) >> 15);
				camera.x += ((worldeyeA2 * (int16_t)dy_adj) >> 15) * scratch;
				camera.y += ((worldeyeB2 * (int16_t)dy_adj) >> 15) * scratch;
				camera.z += scratch * ((worldeyeC2 * (int16_t)dy_adj) >> 15);
			} else {
				t->view_transition_active = 1;
				t->view_pitch = (int16_t)(t->view_pitch + (int16_t)dx_adj);
				t->view_heading = (int16_t)(t->view_heading - (int16_t)dy_adj);
			}
			frame_dirty = 1;
		}
	}

	/* Mouse button: 1 = zoom in, 2 = zoom out. */
	const uint16_t mb = (uint16_t)(inputbuttons & 0x0F);
	if (mb == 1 || mb == 2) {
		int32_t step = t->camera_distance >> 8;
		if (step > 0x7FFF)
			step = 0x7FFF;
		if (step < 32)
			step = 32;
		const int16_t step_w = (int16_t)step;
		step *= frameticks;
		const int32_t dx = frameticks * ((worldeyeA3 * step_w) >> 15);
		const int32_t dy = frameticks * ((worldeyeB3 * step_w) >> 15);
		const int32_t dz = ((worldeyeC3 * step_w) >> 15) * frameticks;

		if (mb == 1) {
			/* Zoom in. */
			if (t->camera_distance < step) {
				t->camera_distance = MAP_CAMERA_NEAR;
			} else {
				t->camera_distance -= step;
				if (t->camera_distance < MAP_CAMERA_NEAR) {
					t->camera_distance += step;
				} else {
					camera.x += dx;
					camera.y += dy;
					camera.z += dz;
				}
			}
		} else {
			/* Zoom out. */
			t->camera_distance += step;
			if (t->camera_distance <= MAP_CAMERA_FAR) {
				camera.x -= dx;
				camera.y -= dy;
				camera.z -= dz;
			} else {
				t->camera_distance -= step;
			}
		}
		frame_dirty = 1;
	}

	/* Exit conditions: any key that maps to KEY_m (109) closes the
	 * room. The key dispatch above sets inputkey to 109 for ESC / 'M'
	 * / 'Q' / nav keys, so a single equality check covers them all. */
	if (inputkey == KEY_m)
		return 1;

	return frame_dirty ? 2 : 0;
}

typedef enum {
	MAPROOM_STEP_RENDER = 0,
	MAPROOM_STEP_POLL,
} MaproomStepPhase;

// FUNCTION: TIE 0x2F1BC, TIE98 0x451470 (task-split recovery)
static LandruTaskStepResult maproom_task_step(void* self) {
	MaproomTask* t = (MaproomTask*)self;

	if (t->phase == MAPROOM_PHASE_FRAME) {
		/* RENDER: pace until the 4-PIT-tick frame budget elapses,
		 * then run the per-frame transition + render block. The
		 * accumulator persists across yields. */
		tickcounter += (uint16_t)xtimer_time_elapsed();
		if (tickcounter < (uint16_t)MAP_FRAME_TICKS)
			return LANDRU_TASK_STEP_YIELD; /* xtimer cursor advances between tie_ticks */
		/* The original poll also consumes this interval for transition progress. */
		frameticks = tickcounter;

		const bool tie98_display = TieClassicDisplay_UsesDx5();
		if (tie98_display)
			FlightSurface_Lock();
		maproom_step_frame(t);
		if (tie98_display) {
			FlightSurface_Unlock();
			FrontendDisplay_BlitOffscreenToRenderSurface();
			FrontendDisplay_PresentFrame();
		}
		t->phase = (MaproomPhase)MAPROOM_STEP_POLL;
		return LANDRU_TASK_STEP_CONTINUE;
	}

	/* POLL: one input iteration. frame_dirty reverts to RENDER. KEY_m
	 * pops the task with the navigation hint latched into
	 * `user_submodal_result`. */
	int r = maproom_poll_once(t);
	if (r == 1) {
		/* --- Stage 4: restore farbufferptrs, return navigation. --- */
		logbuf2_selectbuffer(newbuf);
		maproom_swap_buffer_ptrs(/*do_swap=*/1);
		user_submodal_result = (int32_t)t->page_delta;
		return LANDRU_TASK_STEP_DONE;
	}
	if (r == 2)
		t->phase = (MaproomPhase)MAPROOM_STEP_RENDER;
	return LANDRU_TASK_STEP_CONTINUE;
}
