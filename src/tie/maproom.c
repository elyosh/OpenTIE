#include "tie/maproom.h"
#ifdef TIE_MODERN
#include "tie_runtime/runtime/maproom_task.h"
#endif
#include "tie/backdrp2.h"
#include "tie/create.h"
#include "tie/edition.h"
#include "tie/fediskio.h"
#include "tie/feinput.h"
#include "tie/festring.h"
#include "tie/flight_surface_tie98.h"
#include "tie/frontend_display_tie98.h"
#include "tie/fsfx.h"
#include "tie/fview.h"
#include "tie/logbuf2.h"
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
#include <stdbool.h>
#ifdef TIE_MODERN
#include <landru/task.h>
#endif

#include <stddef.h>
#include <stdint.h>

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
enum {
	MAP_FLIGHT_SLOT_COUNT = WARHEAD_SLOT_END,
	MAP_STATIC_BIAS = (OBJ_REF_STATIC_BASE - MAP_FLIGHT_SLOT_COUNT),
};

/* Per-frame insertion-sort capacity. The IDA frame holds 48 sort_indices
 * + 48 fg_render_count bytes. */
enum {
	MAP_SORT_CAP = 48,
};

/* Cluster pre-pass scans NUM_CRAFTS slots (32 retail / 28 demo). */
enum {
	MAP_CRAFT_SCAN = NUM_CRAFTS,
};

/* View-mode page count (top-down vs side-on). */
enum {
	MAP_VIEW_TRANSITION = 118, /* duration of side<->top animation in ticks */
};

/* Frame pacing target (1 tick = ~4ms of XTIMER, so 4 ticks ~= 16ms = 60fps). */

/* Initial camera orbit distance (= 4 megaunits). */
enum {
	MAP_CAMERA_DEFAULT = 0x40000,
};

/* Camera-distance clamps (mouse zoom). */
enum {
	MAP_CAMERA_NEAR = 2048,
	MAP_CAMERA_FAR = 0x200000,
};

/* Numpad pan speed (units per tick; user_framerateadjust scales by frametime). */
enum {
	MAP_PAN_DELTA = 10240,
};

/* Buffer fill colour (logbuf2_clearbuffer reads `backcolor`). */
enum {
	MAP_BG_COLOR = ((uint8_t)-5), /* 0xFB */
};

/* Palette indices used for backgrounds, text, and helper lines. */
enum {
	MAP_PANEL_BG = 0x40,    /* status-panel background */
	MAP_AXIS_LINE = 0x30,   /* world-axis tick colour */
	MAP_BG_HOSTILE = 0x51,  /* red */
	MAP_BG_IMPERIAL = 0x49, /* yellow */
	MAP_BG_NEUTRAL = 0x45,  /* cyan */
	MAP_BG_OTHER = 0x55,
	MAP_BG_SELECTED = 0x43, /* cyan highlight (matches MAP_TC_HOSTILE_DOT) */
	MAP_BG_TARGET = 0x4E,   /* pink (pstate.target_obj_idx highlight) */
	MAP_TC_HOSTILE = 0x52,  /* status-panel text colors */
	MAP_TC_IMPERIAL = 0x4A,
	MAP_TC_NEUTRAL = 0x46,
	MAP_TC_HELP = 0x56,
	MAP_TC_DEFAULT = 0x43,
};

/* --- Module globals (watdbg owner: maproom.c) ----------------------------- */

// GLOBAL: TIE95 0xC5508
// GLOBAL: TIE98 0x584CE8
uint8_t imperialflag;
// GLOBAL: TIE95 0xC5509
// GLOBAL: TIE98 0x584CEC
uint8_t neutralflag;
// GLOBAL: TIE95 0xC550A
// GLOBAL: TIE98 0x584CF0
uint8_t hostileflag;
// GLOBAL: TIE95 0xC550B
// GLOBAL: TIE98 0x4E4A68
uint8_t warheadflag = 2;
/* mapiconsloaded is owned by tie.c per watdbg; extern in maproom.h. */

// GLOBAL: TIE95 0xD4C64
// GLOBAL: TIE98 0x5FD238
int32_t mapScreenLeft;
// GLOBAL: TIE95 0xD4C60
// GLOBAL: TIE98 0x5FD240
int32_t mapScreenRight;
// GLOBAL: TIE95 0xD4C5C
// GLOBAL: TIE98 0x5FD25C
int32_t mapScreenTop;
// GLOBAL: TIE95 0xD4C50
// GLOBAL: TIE98 0x5FD24C
int32_t mapScreenBottom;
// GLOBAL: TIE95 0xD4C4C
// GLOBAL: TIE98 0x5FD270
int32_t mapScreenWidth;
// GLOBAL: TIE95 0xD4C54
// GLOBAL: TIE98 0x5FD248
int32_t mapScreenHeight;
// GLOBAL: TIE95 0xD4C3C
// GLOBAL: TIE98 0x5FD278
int32_t maxMapIcons;

// GLOBAL: TIE95 0xD4C34
// GLOBAL: TIE98 0x5FD264
const uint8_t* species2icon;
// GLOBAL: TIE95 0xD4C2C
// GLOBAL: TIE98 0x5FD268
const uint8_t* iconxsize;
// GLOBAL: TIE95 0xD4C28
// GLOBAL: TIE98 0x5FD23C
const uint8_t* iconysize;
// GLOBAL: TIE95 0xD4C30
// GLOBAL: TIE98 0x5FD260
const char* iconfilename;

// GLOBAL: TIE95 0xD4C58
// GLOBAL: TIE98 0x5FD258
const char* hostilestr;
// GLOBAL: TIE95 0xD4C40
// GLOBAL: TIE98 0x5FD244
const char* imperialstr;
// GLOBAL: TIE95 0xD4C24
// GLOBAL: TIE98 0x5FD254
const char* neutralstr;
// GLOBAL: TIE95 0xD4C48
// GLOBAL: TIE98 0x5FD26C
const char** NHIstatusstrings;
// GLOBAL: TIE95 0xD4C38
// GLOBAL: TIE98 0x5FD274
const char** maproomhelpstrings;

// GLOBAL: TIE95 0xD4C44
// GLOBAL: TIE98 0x5FD250
void** mapfarbufferptrs;

/* Per-resolution lookup tables (binary-extracted byte literals). */
// GLOBAL: TIE95 0xC56FA
// GLOBAL: TIE98 0x4E4C78
const char iconfilename640[22] = "RESOURCE\\icons640.ico";
// GLOBAL: TIE95 0xC56E4
// GLOBAL: TIE98 0x4E4C60
const char iconfilename320[22] = "RESOURCE\\mapicons.ico";

/* Verbatim bytes from retail Z_TIE__.EXE (identical in TIE95.EXE). Demo had
 * different (smaller) tables: 61 hi-res icons / 60 distinct ids vs retail's
 * 64 / 63. */
// GLOBAL: TIE95 0xC550C
// GLOBAL: TIE98 0x4E4A70
const uint8_t species2icon640[106] = {
	0,  0,  1,  2,  3,  4,  5,  6,  7,  8,  0,  0,  9,  10, 11, 12, 13, 14, 15, 16, 17, 18,
	19, 20, 21, 22, 23, 24, 25, 26, 27, 0,  28, 29, 30, 31, 0,  32, 33, 33, 34, 35, 36, 37,
	38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 0,  48, 49, 50, 51, 52, 53, 53, 53, 53, 53, 53,
	63, 61, 62, 54, 55, 55, 55, 55, 55, 56, 57, 58, 60, 60, 59, 59, 59, 60, 60, 58, 61, 61,
	0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  61, 61, 61, 61, 61, 61,
};

// GLOBAL: TIE95 0xC5576
// GLOBAL: TIE98 0x4E4AE0
const uint8_t iconxsize640[64] = {
	8, 8, 7, 11, 8,  8, 11, 7, 10, 11, 7,  7, 8, 11, 11, 11, 7, 7, 6, 8,  8, 4,
	5, 6, 9, 4,  9,  6, 10, 9, 9,  9,  10, 9, 7, 7,  5,  9,  7, 6, 7, 7,  8, 9,
	7, 7, 9, 11, 10, 6, 9,  7, 6,  14, 17, 9, 9, 9,  6,  6,  7, 7, 7, 14,
};

// GLOBAL: TIE95 0xC55B6
// GLOBAL: TIE98 0x4E4B20
const uint8_t iconysize640[64] = {
	12, 13, 10, 10, 9, 9, 10, 11, 10, 10, 13, 11, 10, 9,  9,  11, 14, 13, 11, 13, 13, 6,
	8,  11, 9,  9,  9, 8, 15, 12, 13, 16, 11, 12, 15, 15, 17, 17, 18, 16, 17, 17, 17, 20,
	17, 15, 17, 21, 7, 7, 6,  9,  9,  14, 15, 6,  8,  8,  3,  6,  10, 7,  7,  17,
};

// GLOBAL: TIE95 0xC55F6
// GLOBAL: TIE98 0x4E4B60
const uint8_t species2icon320[106] = {
	0,  0,  1,  2,  3,  4,  5,  6,  7,  8,  0,  0,  50, 51, 9,  52, 10, 11, 12, 13, 53, 14,
	15, 54, 16, 55, 17, 18, 19, 20, 21, 0,  22, 23, 24, 25, 0,  56, 26, 26, 27, 28, 29, 30,
	57, 58, 59, 31, 60, 32, 33, 34, 35, 36, 0,  37, 61, 62, 63, 64, 38, 38, 38, 38, 38, 38,
	38, 38, 38, 65, 39, 40, 40, 40, 40, 41, 42, 43, 43, 43, 44, 45, 46, 46, 46, 47, 48, 49,
	0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  48, 48, 48, 48, 48, 48,
};

// GLOBAL: TIE95 0xC5660
// GLOBAL: TIE98 0x4E4BD0
const uint8_t iconxsize320[66] = {
	5, 5, 5, 5, 5, 5, 7, 5, 5, 5, 9, 5, 5, 5, 3, 5, 5, 4,  6, 3, 5, 4, 4, 5, 5, 5, 7, 3, 3, 3, 7, 5, 5,
	5, 5, 5, 5, 6, 9, 5, 5, 7, 7, 4, 3, 4, 5, 3, 4, 4, 12, 9, 8, 5, 8, 7, 8, 7, 3, 3, 5, 8, 8, 7, 7, 5,
};

// GLOBAL: TIE95 0xC56A2
// GLOBAL: TIE98 0x4E4C18
const uint8_t iconysize320[66] = {
	5, 6, 4, 6, 5, 5, 5, 5, 5, 5, 5, 5, 5, 6, 6, 6, 5, 6, 4, 6, 5, 6, 7, 7, 7, 6, 6, 6,  6, 7, 7, 7, 8,
	9, 5, 7, 7, 4, 6, 5, 5, 7, 7, 8, 7, 6, 7, 4, 4, 4, 4, 5, 8, 6, 4, 5, 6, 5, 8, 8, 10, 4, 4, 4, 6, 7,
};

/* --- maproom_firstingroup ------------------------------------------------- */

// FUNCTION: TIE95 0x31B94
int32_t maproom_firstingroup(uint16_t idx) {
	/* Sentinel + boundary cases all return "is first". */
	uint8_t my_fg;
	uint16_t i;

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
	my_fg = objects[idx].fg_idx;
	for (i = 0; i < idx; i++) {
		if (objects[i].fg_idx == my_fg)
			return 0;
	}
	return 1;
}

/* --- maproom_setcamerafocus ----------------------------------------------- */

// FUNCTION: TIE95 0x31ABC
void maproom_setcamerafocus(uint16_t obj_or_kind, int32_t distance) {
	/* Resolve the focus point (writes worldlocx/y/z). fg_idx=0 is the
	 * caller's "no fg context" sentinel for create_getworldposition. */
	int32_t remaining;

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
	remaining = distance;
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

// FUNCTION: TIE95 0x31C04
void maproom_drawNHIstatus(uint16_t page_idx) {
	/* The status row sits along the bottom of the screen. mapScreenHeight
	 * is the world-area height (= mapScreenBottom - mapScreenTop) but the
	 * IDA decompile uses it as the absolute bottom Y; we mirror that. */
	const int16_t panel_w = (int16_t)(screenXRes >> 2);
	const int16_t bottom_y = (int16_t)mapScreenHeight;
	const int16_t bottom_top = (int16_t)(mapScreenHeight - (fontheight + 1));
	const int16_t bottom_curs = (int16_t)(mapScreenHeight - fontheight);

	/* --- Bottom-left panel: Hostile counter --- */
	int16_t mid_left;
	int16_t mid_right;
	int16_t right_left;
	int16_t topbar_h;
	int16_t topleft_str_w;
	int16_t topright_str_w;
	int16_t topright_left;

	festring_setbound(0, bottom_top, panel_w, bottom_y);
	festring_setbackcolor(MAP_PANEL_BG);
	clearwindow();
	festring_setcursor(2, bottom_curs);
	festring_settextcolor(MAP_TC_HOSTILE);
	festring_outstring((const uint8_t*)hostilestr);
	festring_outstringright((const uint8_t*)NHIstatusstrings[hostileflag]);

	/* --- Bottom-center panel: Imperial counter --- */
	mid_left = (int16_t)((screenXRes >> 1) - panel_w / 2);
	mid_right = (int16_t)(panel_w + mid_left);
	festring_setbound(mid_left, bottom_top, mid_right, bottom_y);
	festring_setbackcolor(MAP_PANEL_BG);
	clearwindow();
	festring_setcursor((int16_t)(mid_left + 2), bottom_curs);
	festring_settextcolor(MAP_TC_IMPERIAL);
	festring_outstring((const uint8_t*)imperialstr);
	festring_outstringright((const uint8_t*)NHIstatusstrings[imperialflag]);

	/* --- Bottom-right panel: Neutral counter --- */
	right_left = (int16_t)(screenXRes - panel_w);
	festring_setbound(right_left, bottom_top, (int16_t)screenXRes, bottom_y);
	festring_setbackcolor(MAP_PANEL_BG);
	clearwindow();
	festring_setcursor((int16_t)(right_left + 2), bottom_curs);
	festring_settextcolor(MAP_TC_NEUTRAL);
	festring_outstring((const uint8_t*)neutralstr);
	festring_outstringright((const uint8_t*)NHIstatusstrings[neutralflag]);

	/* --- Top-left help string for the active page --- */
	topbar_h = (int16_t)(fontheight + 2);
	topleft_str_w = sys2_calclength((const uint8_t*)maproomhelpstrings[0]);
	festring_setbound(0, 0, (int16_t)(fontheight + topleft_str_w), topbar_h);
	festring_setbackcolor(MAP_PANEL_BG);
	clearwindow();
	festring_setcursor(2, 1);
	festring_settextcolor(MAP_TC_HELP);
	festring_outstring((const uint8_t*)maproomhelpstrings[page_idx]);

	/* --- Top-right help string (always idx 2) --- */
	topright_str_w = sys2_calclength((const uint8_t*)maproomhelpstrings[2]);
	topright_left = (int16_t)(screenXRes - (fontheight + topright_str_w));
	festring_setbound(topright_left, 0, (int16_t)screenXRes, topbar_h);
	festring_setbackcolor(MAP_PANEL_BG);
	clearwindow();
	festring_setcursor((int16_t)(topright_left + 2), 1);
	festring_settextcolor(MAP_TC_HELP);
	festring_outstring((const uint8_t*)maproomhelpstrings[2]);
}

/* --- maproom_drawmapitem -------------------------------------------------- */

// FUNCTION: TIE95 0x31010
void maproom_drawmapitem(uint16_t obj_idx, uint16_t selected_obj_ref, char num_label, int32_t z) {
	/* --- Phase 1: world position lookup --- */
	int32_t eyex;
	int32_t eyey;
	int32_t screen_x;
	int32_t screen_y;
	int icon_only;
	uint16_t color;
	int16_t side_icon_offset_saved;
	uint8_t side;
	uint16_t ship_idx;
	int32_t ground_screen_x, ground_screen_y;
	uint16_t base_icon_idx;
	uint16_t final_icon_idx;
	int16_t icon_x;
	int16_t icon_y;
	int16_t icon_center_x;
	int16_t icon_bottom_y;
	uint16_t dist_obj_ref;
	int16_t two_digit_w;
	int32_t dist_scaled;
	uint16_t dist_int;
	uint16_t dist_int_part;

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

	eyex = transfm2_geteyex(worldx, worldy, worldz);
	eyey = transfm2_geteyey(worldx, worldy, worldz);
	screen_x = transfm2_getscreenx(eyex, z);
	screen_y = transfm2_getscreeny(eyey, z);

	festring_settextcolor(MAP_TC_DEFAULT);

	/* --- Phase 3: side dispatch -> background colour + icon offset --- */
	icon_only = 0;
	color = 0; /* 0 = no highlight */

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
		uint16_t static_obj_ref;

		ship_idx = staticobjects[obj_idx - MAP_FLIGHT_SLOT_COUNT].species;
		static_obj_ref = (uint16_t)(obj_idx + MAP_STATIC_BIAS);
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
		int32_t mx, my;

		if (obj_idx >= NUM_CRAFTS)
			mt = objects[obj_idx].craft_ptr->missile_target;
		else
			mt = (uint16_t)objects[obj_idx].craft_ptr->ai_target_ref;

		create_getworldposition(mt, 0);
		worldlocx -= camera.x;
		worldlocy -= camera.y;
		worldlocz -= camera.z;

		objecteyex = transfm2_geteyex(worldlocx, worldlocy, worldlocz);
		objecteyey = transfm2_geteyey(worldlocx, worldlocy, worldlocz);
		objecteyez = transfm2_geteyez(worldlocx, worldlocy, worldlocz);
		if (objecteyez <= 0)
			transfm2_clipobjecteyez(eyex, eyey, z);
		mx = transfm2_getscreenx(objecteyex, objecteyez);
		my = transfm2_getscreeny(objecteyey, objecteyez);
		if (TIE_DISPLAY_DX5)
			logbuf2_drawclippedline_tie98(mx, my, screen_x, screen_y, fontcolors[10]);
		else
			logbuf2_drawclippedline(mx, my, screen_x, screen_y, fontcolors[10]);
	}

	/* --- Phase 6: ground-projection vertical line --- */

	objecteyex = transfm2_geteyex(worldx, worldy, -65536 - camera.z);
	objecteyey = transfm2_geteyey(worldx, worldy, -65536 - camera.z);
	objecteyez = transfm2_geteyez(worldx, worldy, -65536 - camera.z);
	if (objecteyez <= 0)
		transfm2_clipobjecteyez(eyex, eyey, z);
	ground_screen_x = transfm2_getscreenx(objecteyex, objecteyez);
	ground_screen_y = transfm2_getscreeny(objecteyey, objecteyez);
	if (TIE_DISPLAY_DX5)
		logbuf2_drawclippedline_tie98(ground_screen_x, ground_screen_y, screen_x, screen_y, backcolor);
	else
		logbuf2_drawclippedline(ground_screen_x, ground_screen_y, screen_x, screen_y, backcolor);

	/* --- Phase 7: velocity-vector line (active flight objects only) --- */
	if (obj_idx < MAP_FLIGHT_SLOT_COUNT) {
		int32_t mvX;
		int32_t mvY;
		uint16_t cs;
		int32_t vx, vy;
		int32_t prev_eyex, prev_eyey, prev_eyez;

		if (objects[obj_idx].orient_dirty) {
			fview_calcrotatemove(objects[obj_idx].pitch, objects[obj_idx].heading, &objects[obj_idx]);
			fview_calcrotateorient(objects[obj_idx].roll, 0, &objects[obj_idx]);
		}

		/* Watcom unaligned-load resolution: `*(int*)&move_dirty >> 16`
		 * reads the moveX field at offset +0x3A, NOT move_dirty (+0x38).
		 * Likewise `*(int*)&moveX >> 16` reads moveY at +0x3C. */
		mvX = (int32_t)objects[obj_idx].moveX;
		mvY = (int32_t)objects[obj_idx].moveY;

		/* Initial nudge: the binary's `<< 8 >> 15` is mathematically
		 * `>> 7` for sar; rewrite that way to avoid the C UB of
		 * left-shifting a negative signed int. */
		worldx += mvX >> 7;
		worldy += mvY >> 7;

		cs = (uint16_t)objects[obj_idx].current_speed;
		if (cs < 0x400u) {
			/* Slow craft: extra step proportional to current_speed. */
			worldx += (mvX * 32 * (int32_t)cs) >> 15;
			worldy += (mvY * 32 * (int32_t)cs) >> 15;
		} else {
			/* Fast craft: add the raw move vector once. */
			worldx += mvX;
			worldy += mvY;
		}

		/* Clip against the ground point projected above. */
		prev_eyex = objecteyex;
		prev_eyey = objecteyey;
		prev_eyez = objecteyez;
		objecteyex = transfm2_geteyex(worldx, worldy, -65536 - camera.z);
		objecteyey = transfm2_geteyey(worldx, worldy, -65536 - camera.z);
		objecteyez = transfm2_geteyez(worldx, worldy, -65536 - camera.z);
		if (objecteyez <= 0)
			transfm2_clipobjecteyez(prev_eyex, prev_eyey, prev_eyez);
		vx = transfm2_getscreenx(objecteyex, objecteyez);
		vy = transfm2_getscreeny(objecteyey, objecteyez);
		if (TIE_DISPLAY_DX5)
			logbuf2_drawclippedline_tie98(vx, vy, ground_screen_x, ground_screen_y, backcolor);
		else
			logbuf2_drawclippedline(vx, vy, ground_screen_x, ground_screen_y, backcolor);
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
	base_icon_idx = (ship_idx > 0x69u) ? 19 : (uint16_t)species2icon[ship_idx];

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
			int16_t name_y;
			int16_t name_w;

			if (num_label) {
				festring_farstradd(' ');
				festring_farstradd('(');
				festring_farstradd((char)(num_label + '/'));
				festring_farstradd(')');
			}
			name_y = (int16_t)(screen_y - ((int32_t)iconysize[base_icon_idx] >> 1) - fontheight - 1);
			name_w = sys2_calclength((const uint8_t*)tempstring);
			festring_setcursor((int16_t)(screen_x - name_w / 2), name_y);
			festring_outstring((const uint8_t*)tempstring);
		}
	}

	/* Phase 8b: draw the icon (with optional fillbox highlight border). */
	final_icon_idx = (uint16_t)(base_icon_idx + side_icon_offset_saved);
	icon_x = (int16_t)((uint16_t)screen_x - ((int32_t)iconxsize[base_icon_idx] >> 1));
	icon_y = (int16_t)((uint16_t)screen_y - ((int32_t)iconysize[base_icon_idx] >> 1));

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

	icon_center_x = (int16_t)(((int32_t)iconxsize[base_icon_idx] >> 1) + (uint16_t)icon_x);
	icon_bottom_y = (int16_t)(iconysize[base_icon_idx] + (int16_t)icon_y);

	dist_obj_ref = (obj_idx >= MAP_FLIGHT_SLOT_COUNT) ? (uint16_t)(obj_idx + MAP_STATIC_BIAS) : obj_idx;
	pai_distancebetween(selected_obj_ref, dist_obj_ref);

	/* The "00" string is just a width template (= 2 character widths). */
	two_digit_w = sys2_calclength((const uint8_t*)"00");
	festring_setcursor((int16_t)(icon_center_x - two_digit_w), (int16_t)(icon_bottom_y + 1));

	/* trig2_polardistance is the polar distance in raw map units; *161/65536
	 * scales it into 0.00..99.99 MGLT units (capped at 9999). */
	trig2_polardistance *= 161;
	dist_scaled = trig2_polardistance >> 16;
	if (((uint32_t)trig2_polardistance >> 16) >= 0x2710u)
		dist_scaled = 9999;
	dist_int = (uint16_t)dist_scaled;
	dist_int_part = (uint16_t)(dist_int / 100);
	panelrts_outnum(dist_int_part, 2, 1);
	outchar('.');
	panelrts_outnum((uint16_t)(dist_int - 100 * dist_int_part), 2, 2);
}

/* --- maproom_maproom ------------------------------------------------------ */

// ORIGINAL_FUNCTION: TIE98 0x451470
// Initialization portion of MAPROOM_maproom; the original also contains the task loop.

// FUNCTION: TIE95 0x2F1BC
// FUNCTION: TIE98 0x451470
int32_t maproom_maproom(void) {
	uint16_t view_mode;
	uint16_t view_transition_progress;
	int view_transition_active;
	int16_t view_pitch;
	int16_t view_heading;
	int32_t camera_distance;
	int8_t page_delta;
	int buffer_toggle;
	uint16_t focus_obj_ref;
	int render_again = 1;
#ifdef TIE_MODERN
	MaproomState* continuation = landru_task_top();
	view_mode = continuation->view_mode;
	view_transition_progress = continuation->view_transition_progress;
	view_transition_active = continuation->view_transition_active;
	view_pitch = continuation->view_pitch;
	view_heading = continuation->view_heading;
	camera_distance = continuation->camera_distance;
	page_delta = continuation->page_delta;
	buffer_toggle = continuation->buffer_toggle;
	focus_obj_ref = continuation->focus_obj_ref;
	render_again = continuation->render;
	continuation->waiting = false;
	if (!continuation->started)
#endif
	{
		uint32_t buffer_line_offset;

		if (TIE_FLIGHT_TIE98) {
			uint8_t saved_mapflag = mapflag;
			mapflag = 1;
			fsfx_UpdatePlayerEngineSound();
			mapflag = saved_mapflag;
		}
		if (TIE_DISPLAY_DX5)
			FlightSurface_Lock();
		/* --- Stage 1: layout setup based on resolution --- */
		mapScreenLeft = 0;
		if (flightResolution == TIE_FLIGHT_RES_SVGA || flightResolution == TIE_FLIGHT_RES_SVGA_16 ||
			flightResolution == TIE_FLIGHT_RES_SVGA_D3D) {
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
		/* Layout: first 1060 bytes = 265-entry pointer table
		 * (mapfarbufferptrs); rest = shape data area. */
		farbufferptr = xmemhdl_Lock_Handle(maproomiconshandle);
		xmemhdl_Unlock_Handle(maproomiconshandle);
		mapfarbufferptrs = (void**)farbufferptr;
		farbufferptr += 1060;

		/* Swap each entry of farbufferptrs[] (current-mode shape table) with
		 * mapfarbufferptrs[] (saved). On first entry the icons have never
		 * been loaded, so just save the panel pointers and load the icons
		 * into farbufferptrs[]. */
		if (mapiconsloaded) {
			int i;

			for (i = 0; i < 265; i++) {
				void* tmp = mapfarbufferptrs[i];
				mapfarbufferptrs[i] = (void*)farbufferptrs[i];
				farbufferptrs[i] = (uint8_t*)tmp;
			}
		} else {
			int i;

			for (i = 0; i < 265; i++)
				mapfarbufferptrs[i] = (void*)farbufferptrs[i];
			fediskio_loadbufferdata(iconfilename, 0, (int16_t)(4 * maxMapIcons), 0);
			mapiconsloaded = 1;
		}

		/* --- Stage 3: reset display + initial camera --- */
		festring_setfontsize(2);
		dropflag = 0;

		buffer_line_offset = calcposition((uint16_t)mapScreenLeft, (uint16_t)mapScreenTop);
		if (TIE_DISPLAY_DX5)
			logbuf2_setbufferdimensions_tie98((uint16_t)mapScreenWidth, (uint16_t)mapScreenHeight, 1,
											  buffer_line_offset);
		else
			logbuf2_setbufferdimensions((uint16_t)mapScreenWidth, (uint16_t)mapScreenHeight,
										buffer_line_offset);

		fview_newcalcview(0, 0x7FFF, pstate.player->heading, 0, 0, 0, NULL);

		maproom_setcamerafocus(pstate.object_idx, MAP_CAMERA_DEFAULT);
		if (TIE_FLIGHT_TIE98)
			g_flightInitialTextureCacheFlushPending = 1;
		fullupdateflag = 1;
		logbuf2_selectbuffer(newbuf);
		if (TIE_DISPLAY_DX5)
			FlightSurface_Unlock();

		view_mode = 0; /* 0 = side, 1 = top-down */
		view_transition_active = 1;
		view_transition_progress = 236;
		view_pitch = 0x4800; /* initial side-on pitch, just below level */
		view_heading = pstate.player->heading;
		camera_distance = MAP_CAMERA_DEFAULT;
		page_delta = 0;
		buffer_toggle = 1;
		focus_obj_ref = pstate.object_idx;
#ifdef TIE_MODERN
		continuation->view_mode = view_mode;
		continuation->view_transition_progress = view_transition_progress;
		continuation->view_transition_active = view_transition_active;
		continuation->view_heading = view_heading;
		continuation->view_pitch = view_pitch;
		continuation->camera_distance = camera_distance;
		continuation->page_delta = page_delta;
		continuation->buffer_toggle = buffer_toggle;
		continuation->focus_obj_ref = focus_obj_ref;
		continuation->started = true;
		continuation->render = true;
		return 0;
#endif
	}
	for (;;) {
		if (render_again) {
			tickcounter += (uint16_t)xtimer_Time_Elapsed();
			if (tickcounter < MAP_FRAME_TICKS) {
#ifdef TIE_MODERN
				continuation->waiting = true;
				return 0;
#else
				continue;
#endif
			}
			frameticks = tickcounter;
			if (TIE_DISPLAY_DX5)
				FlightSurface_Lock();
			{
				uint16_t buffer_stride;
				uint8_t saved_backdrop;
				int32_t z_buf[140];
				uint8_t sort_indices[MAP_SORT_CAP];
				uint8_t fg_render_count[48];
				uint8_t sort_count;
				uint16_t i;
				uint16_t fg;
				uint8_t static_local_idx;
				int32_t origin_x;
				int32_t origin_y;

				/* View transition: animates camera pitch between 0x4800
				 * (side-on) and 0x7FFF (top-down) over 118 ticks. Mid-transition
				 * the camera is translated away from the focus by
				 * `camera_distance` along the eye-Z axis, the view matrix is
				 * recomputed at the interpolated pitch, then the camera is
				 * translated back so the focus point stays anchored on screen. */
				if (view_transition_active) {
					int32_t remaining = camera_distance;
					int stepback = 0;
					int16_t anim_pitch;
					int16_t anim_heading;

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

					/* `view_mode` is the TARGET orientation: 0 = top-down,
					 * 1 = side-on. The transition animates FROM the opposite
					 * orientation TO the one selected by `view_mode`. */
					if (view_mode) {
						/* Top -> Side transition. */
						if (view_transition_progress >= 0x76u) {
							fview_newcalcview(0, view_pitch, view_heading, 0, 0, 0, NULL);
							view_transition_active = 0;
						} else {
							anim_pitch = (int16_t)(((MAP_VIEW_TRANSITION - view_transition_progress) *
													(0x7FFF - (uint16_t)view_pitch)) /
													   MAP_VIEW_TRANSITION +
												   view_pitch);
							anim_heading = view_heading;
							fview_newcalcview(0, anim_pitch, anim_heading, 0, 0, 0, NULL);
						}
					} else {
						/* Side -> Top transition. */
						if (view_transition_progress >= 0x76u) {
							fview_newcalcview(0, 0x7FFF, view_heading, 0, 0, 0, NULL);
							view_transition_active = 0;
						} else {
							anim_heading = view_heading;
							anim_pitch =
								(int16_t)(0x7FFF - ((MAP_VIEW_TRANSITION - view_transition_progress) *
													(0x7FFF - (uint16_t)view_pitch)) /
													   MAP_VIEW_TRANSITION);
							fview_newcalcview(0, anim_pitch, anim_heading, 0, 0, 0, NULL);
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
				}

				festring_setbound(0, 0, (int16_t)mapScreenWidth, (int16_t)mapScreenHeight);
				backcolor = MAP_BG_COLOR;
				festring_setfontsize(2);
				festring_setlinewrap(0);
				festring_setautofill(0);

				/* Per-FG cluster pre-pass. For each fg group, find members
				 * and the max pairwise rough-distance. If the group is
				 * "tight" (3*pair_count*bound_hwidth > max_pairwise_dist),
				 * mark fg_render_count[fg] = 1 so the draw loop shows only
				 * the first members with full detail.
				 *
				 * An object's side is hidden when its NHI status flag is 2
				 * ("Off"): sides 1/4 gate on imperialflag, side 0 on
				 * hostileflag, sides 2/3/5+ on neutralflag. */
				for (fg = 0; fg < 48; fg++)
					fg_render_count[fg] = 0;

				for (fg = 0; fg < 48; fg++) {
					for (i = 0; i < MAP_CRAFT_SCAN; i++) {
						int32_t max_pair;
						uint16_t pair_cnt;
						uint16_t j;
						uint8_t side;

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
						side = objects[i].side;
						if (((side == 1 || side == 4) && imperialflag == 2) ||
							(side == 0 && hostileflag == 2) || (side >= 2 && side != 4 && neutralflag == 2))
							continue;

						max_pair = 0;
						pair_cnt = 1;
						for (j = 0; j < MAP_CRAFT_SCAN; j++) {
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
							side = objects[j].side;
							if (((side == 1 || side == 4) && imperialflag == 2) ||
								(side == 0 && hostileflag == 2) ||
								(side >= 2 && side != 4 && neutralflag == 2))
								continue;
							pai_roughdistancebetween(i, j);
							pair_cnt++;
							if (roughdistance > max_pair)
								max_pair = roughdistance;
						}
						if (pair_cnt != 1 &&
							(int16_t)(3 * pair_cnt) * species_table[objects[i].ship_idx].bound_hwidth >
								max_pair) {
							fg_render_count[fg] = 1;
						}
					}
				}

				/* Z-sort the active flight objects + 64 static objects
				 * (skipping empty / filtered slots) into sort_indices[]
				 * back-to-front. Only items with positive eye-Z are sorted;
				 * each is inserted before the first entry it is deeper than. */
				sort_count = 0;

				/* Pass A: active flight objects. */
				for (i = 0; i < MAP_FLIGHT_SLOT_COUNT; i++) {
					const uint8_t ship_idx = objects[i].ship_idx;
					uint8_t side;
					uint8_t genus;
					uint8_t fg_count;
					int32_t wx;
					int32_t wy;
					int32_t wz;
					int32_t ez;
					int k;
					int m;

					if (!ship_idx)
						continue;

					side = objects[i].side;
					if (i != pstate.target_obj_idx && i != focus_obj_ref &&
						(((side == 1 || side == 4) && imperialflag == 2) || (side == 0 && hostileflag == 2) ||
						 (side >= 2 && side != 4 && neutralflag == 2)))
						continue;

					genus = objects[i].genus;
					if (genus == GENUS_PROJECTILE_PLAYER || genus == GENUS_PROJECTILE_NPC) {
						/* Warhead/laser: only show if the craft has a player
						 * target AND warhead filter isn't off. */
						if (!objects[i].craft_ptr->species_idx)
							continue;
						if (warheadflag == 2)
							continue;
					} else if (genus > GENUS_PLATFORM) {
						/* Genus 8 and above skip in the active pass. */
						continue;
					}

					/* Cluster cap: for a "tight" fg only the FIRST member
					 * encountered here gets z-sorted; subsequent members
					 * bump the count used as the '(N)' digit suffix. */
					fg_count = fg_render_count[objects[i].fg_idx];
					if (fg_count) {
						fg_render_count[objects[i].fg_idx] = (uint8_t)(fg_count + 1);
						if (fg_count >= 2)
							continue;
					}

					wx = objects[i].world_x - camera.x;
					wy = objects[i].world_y - camera.y;
					wz = objects[i].world_z - camera.z;
					worldx = wx;
					worldy = wy;
					worldz = wz;
					ez = transfm2_geteyez(wx, wy, wz);
					objecteyez = ez;
					if (ez <= 0)
						continue;
					z_buf[i] = ez;
					/* DEVIATION FROM BINARY: bound sort_indices[] at
					 * MAP_SORT_CAP. The binary doesn't bounds-check and would
					 * corrupt adjacent stack memory past 48 entries. */
					if (sort_count >= MAP_SORT_CAP)
						continue;
					for (k = 0; k < sort_count; k++) {
						if (ez > z_buf[sort_indices[k]])
							break;
					}
					for (m = sort_count; m > k; m--)
						sort_indices[m] = sort_indices[m - 1];
					sort_indices[k] = (uint8_t)i;
					sort_count++;
				}

				/* Pass B: 64 static objects. */
				static_local_idx = MAP_FLIGHT_SLOT_COUNT;
				for (i = 0; i < 64; i++, static_local_idx++) {
					const uint8_t species_id = staticobjects[i].species;
					uint16_t obj_ref;
					uint8_t side;
					uint8_t ship_class;
					int32_t wx;
					int32_t wy;
					int32_t wz;
					int32_t ez;
					int k;
					int m;

					if (!species_id)
						continue;

					obj_ref = (uint16_t)(i + OBJ_REF_STATIC_BASE);
					side = fg_array[staticobjects[i].fg_idx].side;
					if (obj_ref != pstate.target_obj_idx && obj_ref != focus_obj_ref &&
						(((side == 1 || side == 4) && imperialflag == 2) || (side == 0 && hostileflag == 2) ||
						 (side >= 2 && side != 4 && neutralflag == 2)))
						continue;

					ship_class = staticobjects[i].ship_class;
					/* Only mines / planets (ship_class 8/9) participate. */
					if (ship_class < 8u || ship_class > 9u)
						continue;

					wx = (int32_t)staticobjects[i].world_x * 256 - camera.x;
					wy = (int32_t)staticobjects[i].world_y * 256 - camera.y;
					wz = (int32_t)staticobjects[i].world_z * 256 - camera.z;
					worldx = wx;
					worldy = wy;
					worldz = wz;
					ez = transfm2_geteyez(wx, wy, wz);
					objecteyez = ez;
					if (ez <= 0)
						continue;
					z_buf[MAP_FLIGHT_SLOT_COUNT + i] = ez;
					if (sort_count >= MAP_SORT_CAP)
						continue;
					for (k = 0; k < sort_count; k++) {
						if (ez > z_buf[sort_indices[k]])
							break;
					}
					for (m = sort_count; m > k; m--)
						sort_indices[m] = sort_indices[m - 1];
					sort_indices[k] = static_local_idx;
					sort_count++;
				}

				/* Render-buffer fill. */
				if (TIE_DISPLAY_DX5)
					logbuf2_clearbuffer_tie98();
				else
					logbuf2_clearbuffer();
				buffer_stride =
					(uint16_t)(mapScreenWidth * TIE_DISPLAY_EDITION(1u, g_flight16bppBytesPerPixel));
				rtsvga2_setvgapointers(buffer_toggle ? newbuf : xtransdataptr, buffer_stride,
									   (uint16_t)mapScreenHeight);

				/* Backward pass: paints items on the OPPOSITE side of the
				 * z=-65536 ground plane from the camera, so the world axes
				 * drawn next occlude them. */
				for (i = 0; i < sort_count; i++) {
					const uint8_t local_idx = sort_indices[i];
					int32_t item_z;

					if (local_idx < MAP_FLIGHT_SLOT_COUNT)
						item_z = objects[local_idx].world_z;
					else
						item_z = (int32_t)staticobjects[local_idx - MAP_FLIGHT_SLOT_COUNT].world_z * 256;
					if (camera.z < -65536) {
						if (item_z < -65536)
							continue;
					} else if (item_z >= -65536) {
						continue;
					}
					/* Static-object indices intentionally preserve the binary's
					 * out-of-range FG suffix lookup, whose displayed digit is
					 * indeterminate. */
					maproom_drawmapitem(local_idx, focus_obj_ref,
										(char)fg_render_count[objects[local_idx].fg_idx], z_buf[local_idx]);
				}

				/* World axes around the focus point: 33 vertical
				 * 1-megaunit-spaced tick lines along world X then Y. The base
				 * of each tick lies on the ground plane; the top is offset by
				 * one normalized unit along the eye basis vector. */
				create_getworldposition(focus_obj_ref, 0);
				origin_x = worldlocx - 0x100000;
				origin_y = worldlocy - 0x100000;
				for (i = 0; i < 33; i++) {
					const int32_t wx = origin_x - camera.x;
					const int32_t wy = origin_y - camera.y;
					const int32_t wz = -65536 - camera.z;
					int32_t base_ez;
					int32_t top_ez;
					int32_t base_ey;
					int32_t top_ex;
					int32_t top_ey;
					int32_t sy_top;
					int32_t sx_top;
					int32_t sy_base;
					int32_t sx_base;

					worldx = wx;
					worldy = wy;
					worldz = wz;
					origin_y = (int32_t)((uint32_t)origin_y + 0x10000u);

					base_ez = transfm2_geteyez(wx, wy, wz);
					top_ez = base_ez + worldeyeA3 * 64;
					objecteyez = base_ez;
					if (top_ez <= 0 && base_ez <= 0)
						continue;

					objecteyex = transfm2_geteyex(wx, wy, wz);
					base_ey = transfm2_geteyey(wx, wy, wz);
					objecteyey = base_ey;
					top_ex = objecteyex + worldeyeA1 * 64;
					top_ey = base_ey + worldeyeA2 * 64;

					/* If only the base eye-z is positive, swap the endpoints. */
					if (top_ez <= 0) {
						const int32_t tmp_z = top_ez;
						int32_t tmp_x;

						top_ez = objecteyez;
						objecteyez = tmp_z;
						top_ey = base_ey;
						objecteyey = base_ey + worldeyeA2 * 64;
						tmp_x = top_ex;
						top_ex = objecteyex;
						objecteyex = tmp_x;
					}

					if (objecteyez <= 0)
						transfm2_clipobjecteyez(top_ex, top_ey, top_ez);

					sy_top = transfm2_getscreeny(top_ey, top_ez);
					sx_top = transfm2_getscreenx(top_ex, top_ez);
					sy_base = transfm2_getscreeny(objecteyey, objecteyez);
					sx_base = transfm2_getscreenx(objecteyex, objecteyez);
					if (TIE_DISPLAY_DX5)
						logbuf2_drawclippedline_tie98(sx_base, sy_base, sx_top, sy_top, MAP_AXIS_LINE);
					else
						logbuf2_drawclippedline(sx_base, sy_base, sx_top, sy_top, MAP_AXIS_LINE);
				}
				origin_x = worldlocx - 0x100000;
				origin_y = worldlocy - 0x100000;
				for (i = 0; i < 33; i++) {
					const int32_t wx = origin_x - camera.x;
					const int32_t wy = origin_y - camera.y;
					const int32_t wz = -65536 - camera.z;
					int32_t base_ez;
					int32_t top_ez;
					int32_t base_ey;
					int32_t top_ex;
					int32_t top_ey;
					int32_t sy_top;
					int32_t sx_top;
					int32_t sy_base;
					int32_t sx_base;

					worldx = wx;
					worldy = wy;
					worldz = wz;
					origin_x = (int32_t)((uint32_t)origin_x + 0x10000u);

					base_ez = transfm2_geteyez(wx, wy, wz);
					top_ez = base_ez + worldeyeB3 * 64;
					objecteyez = base_ez;
					if (top_ez <= 0 && base_ez <= 0)
						continue;

					objecteyex = transfm2_geteyex(wx, wy, wz);
					base_ey = transfm2_geteyey(wx, wy, wz);
					objecteyey = base_ey;
					top_ex = objecteyex + worldeyeB1 * 64;
					top_ey = base_ey + worldeyeB2 * 64;

					if (top_ez <= 0) {
						const int32_t tmp_z = top_ez;
						int32_t tmp_x;

						top_ez = objecteyez;
						objecteyez = tmp_z;
						top_ey = base_ey;
						objecteyey = base_ey + worldeyeB2 * 64;
						tmp_x = top_ex;
						top_ex = objecteyex;
						objecteyex = tmp_x;
					}

					if (objecteyez <= 0)
						transfm2_clipobjecteyez(top_ex, top_ey, top_ez);

					sy_top = transfm2_getscreeny(top_ey, top_ez);
					sx_top = transfm2_getscreenx(top_ex, top_ez);
					sy_base = transfm2_getscreeny(objecteyey, objecteyez);
					sx_base = transfm2_getscreenx(objecteyex, objecteyez);
					if (TIE_DISPLAY_DX5)
						logbuf2_drawclippedline_tie98(sx_base, sy_base, sx_top, sy_top, MAP_AXIS_LINE);
					else
						logbuf2_drawclippedline(sx_base, sy_base, sx_top, sy_top, MAP_AXIS_LINE);
				}

				/* Forward pass: paints items on the SAME side of the ground
				 * plane as the camera, so they occlude the axes. */
				for (i = 0; i < sort_count; i++) {
					const uint8_t local_idx = sort_indices[i];
					int32_t item_z;

					if (local_idx < MAP_FLIGHT_SLOT_COUNT)
						item_z = objects[local_idx].world_z;
					else
						item_z = (int32_t)staticobjects[local_idx - MAP_FLIGHT_SLOT_COUNT].world_z * 256;
					if (camera.z < -65536) {
						if (item_z >= -65536)
							continue;
					} else if (item_z < -65536) {
						continue;
					}
					maproom_drawmapitem(local_idx, focus_obj_ref,
										(char)fg_render_count[objects[local_idx].fg_idx], z_buf[local_idx]);
				}

				/* Status panels + page flip. */
				maproom_drawNHIstatus(view_mode);
				rtsvga2_setvgapointers(NULL, 0x140u, 0xC8u);

				if (fullupdateflag) {
					if (buffer_toggle) {
						if (TIE_DISPLAY_DX5)
							logbuf2_outbuffer_tie98(newbuf);
						else
							logbuf2_outbuffer(newbuf);
						logbuf2_selectbuffer(xtransdataptr);
						buffer_toggle = 0;
					} else {
						if (TIE_DISPLAY_DX5)
							logbuf2_outbuffer_tie98(xtransdataptr);
						else
							logbuf2_outbuffer(xtransdataptr);
						logbuf2_selectbuffer(newbuf);
						buffer_toggle = 1;
					}
					fullupdateflag = 0;
				} else if (buffer_toggle) {
					if (TIE_DISPLAY_DX5)
						logbuf2_outdiffbuffer_tie98(xtransdataptr, newbuf);
					else
						logbuf2_outdiffbuffer(xtransdataptr, newbuf);
					logbuf2_selectbuffer(xtransdataptr);
					buffer_toggle = 0;
				} else {
					if (TIE_DISPLAY_DX5)
						logbuf2_outdiffbuffer_tie98(newbuf, xtransdataptr);
					else
						logbuf2_outdiffbuffer(newbuf, xtransdataptr);
					logbuf2_selectbuffer(newbuf);
					buffer_toggle = 1;
				}

				/* Backdrop+stars (drawbackdropflag temporarily forced off so
				 * BACKDRP2_backdrop only renders the parallax stars). TIE98 clears
				 * the pending cache flush immediately before and after this pair. */
				saved_backdrop = drawbackdropflag;
				if (TIE_FLIGHT_TIE98)
					g_flightInitialTextureCacheFlushPending = 0;
				drawbackdropflag = 0;
				backdrp2_backdrop();
				drawbackdropflag = saved_backdrop;
				rtsvga2_drawstars();
				if (TIE_FLIGHT_TIE98)
					g_flightInitialTextureCacheFlushPending = 0;
				fullupdateflag = 0;
			}
			if (TIE_DISPLAY_DX5) {
				FlightSurface_Unlock();
				FrontendDisplay_BlitOffscreenToRenderSurface();
				FrontendDisplay_PresentFrame();
			}
#ifdef TIE_MODERN
			continuation->view_mode = view_mode;
			continuation->view_transition_progress = view_transition_progress;
			continuation->view_transition_active = view_transition_active;
			continuation->view_heading = view_heading;
			continuation->view_pitch = view_pitch;
			continuation->camera_distance = camera_distance;
			continuation->page_delta = page_delta;
			continuation->buffer_toggle = buffer_toggle;
			continuation->focus_obj_ref = focus_obj_ref;
			continuation->render = false;
			return 0;
#endif
		}
		{
			int frame_dirty = 0;

			uint16_t key;
			uint16_t mb;

			tickcounter += xtimer_Time_Elapsed();
			if (tickcounter >= (uint16_t)MAP_FRAME_TICKS) {
				if (view_transition_progress < 0x76u) {
					view_transition_progress = (uint16_t)(view_transition_progress + tickcounter);
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
			key = (uint16_t)inputkey;
			switch (key) {
				/* Page nav. */
				case 1:
					page_delta = -1;
					inputkey = 109;
					frame_dirty = 1;
					break;
				case 2:
					page_delta = 1;
					inputkey = 109;
					frame_dirty = 1;
					break;

				/* Exit (Esc / 'M' / 'Q' / 'm' / 'q' / 0xBB). The binary
				 * routes all of these to the same LABEL_245 (page_delta=0). */
				case 27:
				case 0x4D:
				case 0x51:
				case 0x6D:
				case 0x71:
				case 0xBB:
					page_delta = 0;
					inputkey = 109;
					frame_dirty = 1;
					break;

				/* View toggle (Space / 0xBE = ',' KEY_PAGE-toggle). */
				case 32:
				case 190: {
					view_mode = (view_mode == 0) ? 1 : 0;
					view_transition_progress = 4;
					view_transition_active = 1;
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
					focus_obj_ref = pstate.target_obj_idx;
					maproom_setcamerafocus(pstate.target_obj_idx, camera_distance);
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
					uint16_t best_idx = 0xFFFF;
					uint32_t best_score = 0xFFFFFFFFu;
					uint16_t i;

					for (i = 0; i < MAP_CRAFT_SCAN; i++) {
						uint8_t g;
						uint8_t ff;

						if (!objects[i].ship_idx)
							continue;
						if (i == pstate.object_idx)
							continue;
						if (objects[i].side == objects[pstate.object_idx].side)
							continue;
						g = objects[i].genus;
						if (g == GENUS_FREIGHTER || g == GENUS_STARSHIP || g == GENUS_PLATFORM)
							continue;
						ff = objects[i].craft_ptr->flight_flag;
						if (ff != 0 && ff != 6)
							continue;
						pai_distancebetween(pstate.object_idx, i);
						if ((uint32_t)trig2_polardistance < best_score) {
							best_score = (uint32_t)trig2_polardistance;
							best_idx = i;
						}
					}
					if (best_idx != 0xFFFFu)
						pstate.target_obj_idx = best_idx;
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
					uint16_t best_idx = 0xFFFF;
					uint32_t best_age = 0xFFFFFFFFu;
					uint16_t i;

					for (i = 0; i < MAP_CRAFT_SCAN; i++) {
						if (!objects[i].ship_idx)
							continue;
						if (i == pstate.object_idx)
							continue;
						if (objects[i].craft_ptr->leader_obj_idx != 255)
							continue;
						if ((uint32_t)(uint16_t)objects[i].age_ticks < best_age) {
							best_age = (uint32_t)(uint16_t)objects[i].age_ticks;
							best_idx = i;
						}
					}
					if (best_idx != 0xFFFFu)
						pstate.target_obj_idx = best_idx;
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
			 * and not Ctrl, not numpad) they rotate the view heading (X) and pitch (Y). */
			if (inputdeltax || inputdeltay) {
				const int16_t dx_adj = user_framerateadjust(inputdeltax);
				const int16_t dy_adj = user_framerateadjust(inputdeltay);
				if (dx_adj || dy_adj) {
					const int is_numpad = (inputkey >= KEY_1 && inputkey <= KEY_9);
					if (!view_mode || is_numpad || sys2_checkctrlkey()) {
						int32_t scratch = camera_distance >> 14;
						if (!scratch)
							scratch = 1;
						camera.x += ((worldeyeA1 * (int16_t)dx_adj) >> 15) * scratch;
						camera.y += ((worldeyeB1 * (int16_t)dx_adj) >> 15) * scratch;
						camera.z += scratch * ((worldeyeC1 * (int16_t)dx_adj) >> 15);
						camera.x += ((worldeyeA2 * (int16_t)dy_adj) >> 15) * scratch;
						camera.y += ((worldeyeB2 * (int16_t)dy_adj) >> 15) * scratch;
						camera.z += scratch * ((worldeyeC2 * (int16_t)dy_adj) >> 15);
					} else {
						view_transition_active = 1;
						view_heading = (int16_t)(view_heading + (int16_t)dx_adj);
						view_pitch = (int16_t)(view_pitch - (int16_t)dy_adj);
					}
					frame_dirty = 1;
				}
			}

			/* Mouse button: 1 = zoom in, 2 = zoom out. */
			mb = (uint16_t)(inputbuttons & 0x0F);
			if (mb == 1 || mb == 2) {
				int32_t step = camera_distance >> 8;
				int16_t step_w;
				int32_t dx;
				int32_t dy;
				int32_t dz;

				if (step > 0x7FFF)
					step = 0x7FFF;
				if (step < 32)
					step = 32;
				step_w = (int16_t)step;
				step *= frameticks;
				dx = frameticks * ((worldeyeA3 * step_w) >> 15);
				dy = frameticks * ((worldeyeB3 * step_w) >> 15);
				dz = ((worldeyeC3 * step_w) >> 15) * frameticks;

				if (mb == 1) {
					/* Zoom in. */
					if (camera_distance < step) {
						camera_distance = MAP_CAMERA_NEAR;
					} else {
						camera_distance -= step;
						if (camera_distance < MAP_CAMERA_NEAR) {
							camera_distance += step;
						} else {
							camera.x += dx;
							camera.y += dy;
							camera.z += dz;
						}
					}
				} else {
					/* Zoom out. */
					camera_distance += step;
					if (camera_distance <= MAP_CAMERA_FAR) {
						camera.x -= dx;
						camera.y -= dy;
						camera.z -= dz;
					} else {
						camera_distance -= step;
					}
				}
				frame_dirty = 1;
			}

			/* Exit conditions: any key that maps to KEY_m (109) closes the
			 * room. The key dispatch above sets inputkey to 109 for ESC / 'M'
			 * / 'Q' / nav keys, so a single equality check covers them all. */
			if (inputkey == KEY_m) {

				logbuf2_selectbuffer(newbuf);
				{
					int i;

					for (i = 0; i < 265; i++) {
						void* tmp = mapfarbufferptrs[i];
						mapfarbufferptrs[i] = (void*)farbufferptrs[i];
						farbufferptrs[i] = (uint8_t*)tmp;
					}
				}
#ifdef TIE_MODERN
				continuation->finished = true;
#endif
				return page_delta;
			}

			render_again = frame_dirty;
		}
#ifdef TIE_MODERN
		continuation->view_mode = view_mode;
		continuation->view_transition_progress = view_transition_progress;
		continuation->view_transition_active = view_transition_active;
		continuation->view_heading = view_heading;
		continuation->view_pitch = view_pitch;
		continuation->camera_distance = camera_distance;
		continuation->page_delta = page_delta;
		continuation->buffer_toggle = buffer_toggle;
		continuation->focus_obj_ref = focus_obj_ref;
		continuation->render = render_again != 0;
		return 0;
#endif
	}
}
