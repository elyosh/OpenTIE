#include "tie/textext.h"
#include "tie/shellext.h"
#ifdef TIE_MODERN
#include "tie_runtime/snapshot/capture_views.h"
#endif

#include "landru/actcust.h"
#include "landru/actor.h"
#include "landru/canvas.h"
#include "landru/dirty.h"
#include "landru/font.h"
#include "landru/paragrp.h"
#include "landru/rect.h"
#include "landru/res.h"
#include "landru/view.h"

#include "tie/shell.h"

#include "tie/stub.h"

#include <string.h>

/* Text ID → resource index (0 = tietext0, 1 = tietext1, 2 = tietext2) */
/* IDs 0..266 use resource 0, 267..273 resource 1, 274..287 resource 2.
 * Entry 288 is the original 99 sentinel. */
// GLOBAL: TIE95 0xCE7B4
static int16_t text_resource_table[289] = {
	/* 0-15 */ 0,    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	/* 16-31 */ 0,   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	/* 32-47 */ 0,   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	/* 48-63 */ 0,   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	/* 64-79 */ 0,   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	/* 80-95 */ 0,   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	/* 96-111 */ 0,  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	/* 112-127 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	/* 128-143 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	/* 144-159 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	/* 160-175 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	/* 176-191 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	/* 192-207 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	/* 208-223 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	/* 224-239 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	/* 240-255 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	/* 256-271 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1,
	/* 272-287 */ 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
	/* 288 */ 99,
};

/* Retail scene subtitle timing: 216 entries plus a seven-word sentinel row.
 * Fields: scene, resource, start_time, stop_time, x, y, fade_type
 * fade_type: 0=fadeTitle, 1=fadeFastTitle, 2=fadeTitle1, 3=fadeTitle2,
 *            4=fadePerson1, 5=fadePerson2 */
// GLOBAL: TIE95 0xCE9F6
// GLOBAL: TIE98 0x4F1E78
static int16_t text_scene_list_gbl[1519] = {
	10,  0,  4,   100, 0,   4,   0,  10,  0, 4,   100, 0,   14,  0, 10,  0, 130, 190, 0,   180, 1,
	10,  0,  130, 190, 0,   190, 1,  20,  0, 4,   70,  0,   4,   1, 20,  0, 4,   70,  0,   14,  1,
	30,  0,  4,   50,  0,   170, 2,  30,  0, 60,  116, 0,   24,  2, 30,  0, 60,  116, 0,   34,  2,
	30,  0,  124, 210, 0,   170, 2,  30,  0, 124, 210, 0,   180, 2, 40,  0, 4,   60,  0,   4,   1,
	40,  0,  4,   60,  0,   14,  1,  50,  0, 4,   80,  0,   4,   2, 50,  0, 4,   80,  0,   14,  2,
	50,  0,  4,   80,  0,   24,  2,  50,  0, 140, 200, 0,   160, 3, 50,  0, 140, 200, 0,   170, 3,
	50,  0,  202, 210, 0,   40,  2,  170, 0, 4,   32,  0,   10,  1, 280, 0, 4,   50,  0,   10,  1,
	281, 0,  4,   50,  0,   10,  1,  282, 0, 4,   50,  0,   10,  1, 283, 0, 4,   50,  0,   10,  1,
	284, 0,  4,   50,  0,   10,  1,  285, 0, 4,   50,  0,   10,  1, 390, 0, 4,   40,  0,   190, 1,
	400, 0,  4,   40,  0,   12,  2,  401, 0, 4,   60,  0,   10,  2, 402, 0, 4,   60,  0,   10,  2,
	403, 0,  4,   60,  0,   10,  2,  404, 0, 50,  90,  0,   60,  2, 405, 0, 50,  90,  0,   60,  2,
	210, 0,  4,   40,  0,   10,  1,  210, 0, 210, 250, 0,   100, 2, 500, 0, 4,   60,  0,   20,  1,
	500, 0,  4,   60,  0,   30,  1,  500, 0, 200, 230, 0,   170, 2, 500, 0, 234, 280, 0,   24,  3,
	500, 0,  234, 280, 0,   34,  3,  500, 0, 300, 358, 0,   170, 2, 500, 0, 300, 358, 0,   180, 2,
	500, 0,  360, 430, 0,   24,  3,  500, 0, 360, 430, 0,   34,  3, 510, 0, 4,   60,  0,   20,  1,
	510, 0,  4,   60,  0,   30,  1,  510, 0, 164, 230, 0,   50,  2, 510, 0, 164, 230, 0,   60,  2,
	510, 0,  234, 294, 0,   50,  2,  510, 0, 234, 294, 0,   60,  2, 510, 0, 296, 350, 0,   50,  2,
	510, 0,  296, 350, 0,   60,  2,  520, 0, 4,   100, 0,   20,  1, 520, 0, 4,   100, 0,   30,  1,
	520, 0,  165, 190, -10, 140, 3,  520, 0, 192, 220, 50,  40,  2, 520, 0, 192, 220, 50,  50,  2,
	520, 0,  222, 270, -10, 140, 3,  520, 0, 222, 270, -10, 150, 3, 520, 0, 272, 300, 50,  40,  2,
	520, 0,  272, 300, 50,  50,  2,  530, 0, 40,  98,  -20, 20,  2, 530, 0, 40,  98,  -20, 30,  2,
	530, 0,  40,  98,  -20, 40,  2,  530, 0, 100, 126, 40,  20,  3, 530, 0, 100, 126, 40,  30,  3,
	531, 0,  90,  120, 40,  20,  3,  531, 0, 120, 190, -20, 20,  2, 531, 0, 120, 190, -20, 30,  2,
	531, 0,  120, 190, -20, 40,  2,  531, 0, 192, 250, 40,  20,  3, 531, 0, 192, 250, 40,  30,  3,
	540, 0,  24,  70,  0,   20,  2,  540, 0, 90,  130, 0,   20,  2, 540, 0, 165, 200, 0,   20,  2,
	550, 0,  4,   22,  -40, 180, 2,  550, 0, 24,  40,  40,  180, 3, 550, 0, 24,  40,  40,  190, 3,
	550, 0,  78,  104, -40, 180, 2,  550, 0, 78,  104, -40, 190, 2, 550, 0, 106, 120, 40,  180, 3,
	560, 0,  4,   40,  0,   190, 1,  560, 0, 44,  100, 0,   20,  2, 700, 0, 70,  90,  0,   180, 2,
	700, 0,  98,  150, 0,   180, 3,  700, 0, 98,  150, 0,   190, 3, 700, 0, 160, 240, 0,   180, 2,
	700, 0,  160, 240, 0,   190, 2,  700, 0, 260, 290, 0,   180, 3, 700, 0, 320, 380, 0,   180, 2,
	710, 0,  10,  90,  0,   180, 3,  710, 0, 10,  90,  0,   190, 3, 710, 0, 96,  184, 0,   180, 2,
	710, 0,  96,  184, 0,   190, 2,  710, 0, 194, 270, 0,   180, 3, 710, 0, 194, 270, 0,   190, 3,
	710, 0,  280, 315, 0,   180, 3,  710, 0, 280, 315, 0,   190, 3, 710, 0, 320, 370, 0,   180, 2,
	710, 0,  320, 370, 0,   190, 2,  720, 0, 10,  45,  0,   180, 2, 720, 0, 60,  130, 0,   170, 3,
	720, 0,  60,  130, 0,   180, 3,  720, 0, 140, 210, 0,   170, 3, 720, 0, 140, 210, 0,   180, 3,
	720, 0,  220, 260, 0,   170, 2,  720, 0, 272, 360, 0,   170, 3, 720, 0, 272, 360, 0,   180, 3,
	580, 1,  4,   60,  0,   190, 0,  573, 1, 4,   45,  0,   190, 1, 573, 1, 32,  60,  0,   10,  2,
	573, 1,  64,  126, 0,   10,  3,  573, 1, 64,  126, 0,   20,  3, 573, 1, 130, 189, 0,   10,  2,
	573, 1,  130, 189, 0,   20,  2,  573, 1, 193, 233, 0,   10,  2, 573, 1, 193, 233, 0,   20,  2,
	573, 1,  237, 270, 0,   10,  2,  573, 1, 237, 270, 0,   20,  2, 573, 1, 274, 303, 0,   10,  2,
	573, 1,  307, 340, 0,   10,  2,  573, 1, 307, 340, 0,   20,  2, 573, 1, 344, 360, 0,   10,  3,
	581, 1,  1,   44,  0,   180, 3,  581, 1, 1,   44,  0,   190, 3, 581, 1, 48,  64,  0,   190, 2,
	581, 1,  68,  111, 0,   180, 2,  581, 1, 68,  111, 0,   190, 2, 581, 1, 115, 169, 0,   180, 2,
	581, 1,  115, 169, 0,   190, 2,  581, 1, 173, 237, 0,   180, 2, 581, 1, 173, 237, 0,   190, 2,
	590, 1,  80,  167, 0,   180, 2,  590, 1, 80,  167, 0,   190, 2, 590, 1, 171, 255, 0,   180, 2,
	590, 1,  171, 255, 0,   190, 2,  590, 1, 259, 283, 0,   190, 2, 591, 1, 5,   50,  0,   180, 3,
	591, 1,  5,   50,  0,   190, 3,  591, 1, 54,  109, 0,   180, 2, 591, 1, 54,  109, 0,   190, 2,
	591, 1,  118, 161, 0,   190, 2,  591, 1, 165, 191, 0,   190, 3, 591, 1, 195, 233, 0,   190, 3,
	730, 1,  80,  138, 0,   180, 2,  730, 1, 80,  138, 0,   190, 2, 730, 1, 142, 190, 0,   180, 3,
	730, 1,  142, 190, 0,   190, 3,  730, 1, 196, 239, 0,   190, 3, 730, 1, 245, 318, 0,   180, 2,
	730, 1,  245, 318, 0,   190, 2,  730, 1, 320, 385, 0,   180, 2, 730, 1, 320, 385, 0,   190, 2,
	406, 1,  1,   40,  0,   190, 2,  407, 1, 1,   40,  0,   190, 2, 408, 1, 1,   40,  0,   190, 2,
	600, 2,  85,  112, 0,   10,  3,  600, 2, 117, 131, 0,   10,  2, 600, 2, 134, 186, 0,   10,  3,
	600, 2,  134, 186, 0,   20,  3,  600, 2, 191, 267, 0,   10,  3, 600, 2, 191, 267, 0,   20,  3,
	600, 2,  191, 267, 0,   30,  3,  600, 2, 272, 325, 0,   10,  3, 600, 2, 272, 325, 0,   20,  3,
	601, 2,  5,   90,  0,   10,  0,  603, 2, 170, 184, 0,   10,  2, 603, 2, 189, 214, 0,   10,  2,
	603, 2,  219, 252, 0,   10,  3,  603, 2, 257, 349, 0,   10,  3, 603, 2, 257, 349, 0,   20,  3,
	603, 2,  257, 349, 0,   30,  3,  603, 2, 354, 405, 0,   10,  2, 603, 2, 354, 405, 0,   20,  2,
	603, 2,  410, 454, 0,   10,  2,  610, 2, 3,   43,  0,   190, 3, 610, 2, 48,  107, 0,   180, 3,
	610, 2,  48,  107, 0,   190, 3,  610, 2, 117, 189, 0,   180, 3, 610, 2, 117, 189, 0,   190, 3,
	610, 2,  194, 230, 0,   190, 3,  610, 2, 235, 334, 0,   10,  2, 610, 2, 235, 334, 0,   20,  2,
	610, 2,  339, 365, 0,   10,  2,  610, 2, 370, 400, 0,   10,  2, 620, 2, 50,  95,  0,   10,  2,
	620, 2,  50,  95,  0,   20,  2,  620, 2, 100, 170, 0,   10,  3, 620, 2, 100, 170, 0,   20,  3,
	621, 2,  1,   46,  0,   180, 2,  621, 2, 1,   46,  0,   190, 2, 621, 2, 55,  70,  0,   100, 3,
	622, 2,  1,   50,  0,   190, 2,  622, 2, 54,  85,  0,   190, 2, 622, 2, 85,  125, 0,   190, 2,
	623, 2,  35,  95,  0,   10,  2,  740, 2, 4,   66,  0,   10,  2, 740, 2, 4,   66,  0,   20,  2,
	740, 2,  70,  142, 0,   10,  2,  740, 2, 70,  142, 0,   20,  2, 740, 2, 146, 233, 0,   10,  2,
	740, 2,  146, 233, 0,   20,  2,  740, 2, 237, 275, 0,   10,  2, 740, 2, 282, 336, 0,   180, 3,
	740, 2,  282, 336, 0,   190, 3,  740, 2, 340, 376, 0,   180, 3, 740, 2, 340, 376, 0,   190, 3,
	740, 2,  380, 440, 0,   190, 3,  740, 2, 446, 542, 0,   180, 2, 740, 2, 446, 542, 0,   190, 2,
	406, 2,  1,   40,  0,   190, 2,  407, 2, 1,   40,  0,   190, 2, 408, 2, 1,   40,  0,   190, 2,
	409, 2,  1,   40,  0,   190, 2,  410, 2, 1,   40,  0,   190, 2, 411, 2, 1,   40,  0,   190, 2,
	-1,  -1, -1,  -1,  -1,  -1,  -1,
};

/* --- Static data tables --- */

// GLOBAL: TIE95 0xCE754
static char text_res_names[3][16] = { "tietext0.lfd", "tietext1.lfd", "tietext2.lfd" };
// GLOBAL: TIE95 0xCE784
static char text_file_names[3][16] = { "tietext0", "tietext1", "tietext2" };

/* --- Runtime state --- */

// GLOBAL: TIE95 0xCF5D4
// GLOBAL: TIE98 0x4F2A58
static int16_t text_start_gbl;
// GLOBAL: TIE95 0xCF5D6
// GLOBAL: TIE98 0x4F2A5C
static int16_t text_stop_gbl;
// GLOBAL: TIE95 0xF59D8
// GLOBAL: TIE98 0x589E48
static Rect prev_text_bounds;
// GLOBAL: TIE95 0xF59E8
// GLOBAL: TIE98 0x58A148
static char text_ext_string[256];
// GLOBAL: TIE95 0xF5AE8
static int16_t text_string_table[288];
// GLOBAL: TIE95 0xF59E0
// GLOBAL: TIE98 0x58A250
Rect textext_bounds;
// GLOBAL: TIE95 0xF5D28
// GLOBAL: TIE98 0x589E58
static Actor* display_text_actor;
// GLOBAL: TIE95 0xF5D2C
// GLOBAL: TIE98 0x589E5C
static Actor* restore_text_actor;
// GLOBAL: TIE95 0xF5D30
// GLOBAL: TIE98 0x58A248
static int16_t text_color[3];
// GLOBAL: TIE95 0xF5D3C
// GLOBAL: TIE98 0x58A118
static int16_t text_h[3];
// GLOBAL: TIE95 0xF5D42
// GLOBAL: TIE98 0x58A128
static int16_t text_w[3];
// GLOBAL: TIE95 0xF5D48
// GLOBAL: TIE98 0x58A138
static int16_t text_y[3];
// GLOBAL: TIE95 0xF5D4E
// GLOBAL: TIE98 0x58A130
static int16_t text_x[3];
// GLOBAL: TIE95 0xF5D54
// GLOBAL: TIE98 0x589E40
static int16_t text_string[3];
// GLOBAL: TIE95 0xF5D36
// GLOBAL: TIE98 0x589E50
static int16_t text_res[3];
// GLOBAL: TIE95 0xF5D5A
static LandruHandle text_para[3];
// GLOBAL: TIE95 0xF5D62
// GLOBAL: TIE98 0x58A120
static int16_t num_text_lines;
// GLOBAL: TIE95 0xF5D60
// GLOBAL: TIE98 0x58A140
static LandruHandle text_buffer;

/* --- Internal helpers --- */

static void textext_Find_Text_Range(int16_t scene, int16_t* pstart, int16_t* pstop);
static void textext_user_Text_Actor(Actor* the_actor, int32_t time);
static int16_t textext_draw_Text_Actor(Actor* the_actor, Rect* r, Rect* clip_r, int16_t x, int16_t y,
									   int16_t refresh);

/* --- Functions --- */

// FUNCTION: TIE95 0x6F4B0
void textext_Open_Text_Ext(void) {
	ResFile* res_file;
	int16_t size[3] = { 0, 0, 0 };
	int16_t i, res_idx;

	for (i = 0; i < 3; i++) {
		res_file = shellext_Open_Empire_Resource(text_res_names[i]);
		if (res_file) {
			text_para[i] = xparagrp_Res_Paragraph(res_file, text_file_names[i]);
			xres_Close_Resource(res_file);
		}
		size[i] = 0;
	}

	/* Build text_string_table: sequential per-resource string index */
	for (i = 0; i < 288; i++) {
		res_idx = text_resource_table[i];
		text_string_table[i] = size[res_idx];
		size[res_idx]++;
	}
}

// FUNCTION: TIE95 0x6F544
void textext_Close_Text_Ext(void) {
	int16_t i;

	for (i = 0; i < 3; i++) {
		if (text_para[i]) {
			xparagrp_Free_Paragraph(text_para[i]);
			text_para[i] = LANDRU_NULL_HANDLE;
		}
	}
}

// FUNCTION: TIE95 0x6F588
void textext_Open_Text_Ext_Scene(int16_t scene) {
	Rect r;
	int16_t start, stop;

	ViewStruct* view;

	textext_Find_Text_Range(scene, &start, &stop);
	text_start_gbl = start;
	text_stop_gbl = stop;

	if (start == -1 || stop == -1)
		return;

	xrect_Set_Rect(&r, 0, 0, 320, 200);

	view = xview_Get_Current_View();
	if (view->clear)
		text_buffer = LANDRU_NULL_HANDLE;
	else
		text_buffer = xmemhdl_Alloc_Handle(12800, LANDRU_MEMORY_DEFAULT);

	restore_text_actor = xactcust_Alloc_Custom_Actor(LANDRU_NULL_HANDLE, &r, 0, 0, 10000);
	display_text_actor = xactcust_Alloc_Custom_Actor(LANDRU_NULL_HANDLE, &r, 0, 0, -10000);
	restore_text_actor->id = 0;
	display_text_actor->id = 1;

	xactor_Set_Actor_User_Function(display_text_actor, textext_user_Text_Actor);
	xactor_Set_Actor_Draw_Function(restore_text_actor, textext_draw_Text_Actor);
	xactor_Set_Actor_Draw_Function(display_text_actor, textext_draw_Text_Actor);
}

// FUNCTION: TIE95 0x6F690
void textext_Close_Text_Ext_Scene(int16_t scene) {
	(void)scene;
	if (text_buffer) {
		xmemhdl_Free_Handle(text_buffer);
		text_buffer = LANDRU_NULL_HANDLE;
	}
	/* textext_user_Text_Actor stops running once display_text_actor is gone;
	 * zero num_text_lines so TieRecoveredText_CaptureSnapshot doesn't keep
	 * emitting the last frame's data into subsequent snapshots. */
#ifdef TIE_MODERN
	num_text_lines = 0;
#endif
}

/*
 * Per-frame text scheduling callback.
 * Walks text_scene_list_gbl for the current scene range, finds lines whose
 * time window includes the current frame, computes position and fade color.
 */
// FUNCTION: TIE95 0x6F6B4
static void textext_user_Text_Actor(Actor* the_actor, int32_t time) {
	char str[80];
	Rect r;
	int16_t resource, start, stop, x, y;
	int16_t width;
	int16_t index, i;
	int16_t color_index;
	int16_t base_id;
	int16_t old_font;
	TextFade type;

	(void)the_actor;
	xrect_Copy_Rect(&prev_text_bounds, &textext_bounds);
	num_text_lines = 0;
	base_id = 0;

	for (i = text_start_gbl, index = 7 * text_start_gbl; i < text_stop_gbl; i++, index += 7) {
		resource = text_scene_list_gbl[index + 1];
		start = text_scene_list_gbl[index + 2];
		stop = text_scene_list_gbl[index + 3];
		x = text_scene_list_gbl[index + 4];
		y = text_scene_list_gbl[index + 5];
		type = text_scene_list_gbl[index + 6];

		if (resource == 1)
			base_id = 108;
		else if (resource == 2)
			base_id = 156;

		if (start > time || stop <= time)
			continue;
		if (type >= fadeTitle1 && !options_gbl.text_active && digital_exists)
			continue;
		if (!text_para[resource])
			continue;
		if (num_text_lines >= 3)
			continue;

		xparagrp_Get_Paragraph_String(text_para[resource], str, 1, i - base_id);

		old_font = xfont_Get_Font();
		xfont_Set_Font(0);
		width = xfont_Get_String_Width(str);
		xfont_Set_Font(old_font);

		text_res[num_text_lines] = resource;
		text_string[num_text_lines] = i - base_id;
		text_w[num_text_lines] = width;
		text_h[num_text_lines] = xfont_Get_FontID_Height(0);

		if (type < fadeTitle1) {
			/* Fade in/out: color ramp 16..31 */
			color_index = (int16_t)time - start;
			if (type == fadeFastTitle)
				color_index *= 2;
			if (color_index > 15) {
				color_index = stop - (int16_t)time;
				if (type == fadeFastTitle)
					color_index *= 2;
				if (color_index > 15)
					color_index = 15;
			}
			text_x[num_text_lines] = 160 - (width >> 1) + x;
			text_y[num_text_lines] = y;
			text_color[num_text_lines] = color_index + 16;
		} else if (type <= fadeTitle2) {
			/* Centered title text */
			text_x[num_text_lines] = 160 - (width >> 1) + x;
			text_y[num_text_lines] = y;
			text_color[num_text_lines] = (type == fadeTitle1) ? 13 : 3;
		} else if (type <= fadePerson2) {
			/* Left-aligned person text */
			text_x[num_text_lines] = x;
			text_y[num_text_lines] = y;
			text_color[num_text_lines] = (type == fadePerson1) ? 13 : 3;
		}

		num_text_lines++;
	}

	if (num_text_lines) {
		if (xactor_Is_Actor_Visible(display_text_actor))
			xactor_Show_Actor(restore_text_actor);
		else
			xactor_Show_Actor(display_text_actor);

		xrect_Set_Rect(&textext_bounds, text_x[0], text_y[0], text_w[0] + text_x[0], text_h[0] + text_y[0]);

		for (i = 1; i < num_text_lines; i++) {
			xrect_Set_Rect(&r, text_x[i], text_y[i], text_w[i] + text_x[i], text_h[i] + text_y[i]);
			xrect_Enclose_Rect(&textext_bounds, &r);
		}
	} else {
		if (xactor_Is_Actor_Visible(display_text_actor))
			xactor_Hide_Actor(display_text_actor);
		else
			xactor_Hide_Actor(restore_text_actor);
	}
}

/*
 * Draw callback for text actors.
 * id=1 (display): save background, draw text lines with shadow.
 * id=0 (restore): restore previously saved background.
 */
// FUNCTION: TIE95 0x6FAA0
// FUNCTION: TIE98 0x48CAF0
static int16_t textext_draw_Text_Actor(Actor* the_actor, Rect* r, Rect* clip_r, int16_t x, int16_t y,
									   int16_t refresh) {
	char string[80];
	Rect br;
	int16_t i;

	(void)r;
	(void)clip_r;
	(void)x;
	(void)y;

	if (!refresh)
		return 0;

	if (the_actor->id) {
		/* Display actor: save background then draw text. The lfont
		 * snapshot emit is suppressed for the duration: the actor
		 * system's dirty-rect machinery fires this callback on a
		 * non-deterministic cadence, so TieRecoveredText_CaptureSnapshot owns
		 * the per-tick subtitle records instead. Without this gate
		 * the lfont hook would double-emit on redraw frames. */
		if (text_buffer) {
			xrect_Copy_Rect(&br, &textext_bounds);
			xrect_Origin_Rect(&br);
			stub_Copy_To_Clipped_Buffer(text_buffer, &br, textext_bounds.left, textext_bounds.top,
										br.right - br.left, br.bottom - br.top);
		}

#ifdef TIE_MODERN
		xcanvas_Set_Suppress_Text_Render(true);
#endif
		xfont_Enable_FontID_Shadow(0);
		for (i = 0; i < num_text_lines; i++) {
			xparagrp_Get_Paragraph_String(text_para[text_res[i]], string, 1, text_string[i]);
			xfont_Print_Clipped_Text(string, text_x[i], text_y[i], 0, text_color[i]);
		}
		xfont_Disable_FontID_Shadow(0);
#ifdef TIE_MODERN
		xcanvas_Set_Suppress_Text_Render(false);
#endif

		xdirty_Dirty_Rect(&textext_bounds);
	} else {
		/* Restore actor: put saved background back */
		if (text_buffer) {
			xrect_Copy_Rect(&br, &prev_text_bounds);
			xrect_Origin_Rect(&br);
			stub_Copy_From_Clipped_Buffer(text_buffer, &br, prev_text_bounds.left, prev_text_bounds.top,
										  br.right - br.left, br.bottom - br.top);
			xdirty_Dirty_Rect(&prev_text_bounds);
		}
	}

	return 1;
}

#ifdef TIE_MODERN
/*
 * Per-tick snapshot emitter — writes the current frame's per-line
 * subtitle state into the unified TieUIText channel. Called from
 * TieRuntime_Tick alongside the other emit_* helpers.
 *
 * Required because the actor system's dirty-rect machinery only
 * fires textext_draw_Text_Actor on redraw frames (the engine's classic FB
 * persists between draws), so the lfont snapshot hook misses ticks
 * where the text is on screen but not being repainted. Re-emitting
 * here every tick (using the fade-machine state populated by
 * textext_user_Text_Actor) gives the renderer per-tick coverage; the lfont
 * hook is suppressed inside textext_draw_Text_Actor's display branch to
 * keep this from being doubled on the redraw frames.
 */
int TieRecoveredText_SnapshotLineCount(void) { return num_text_lines > 0 ? num_text_lines : 0; }

bool TieRecoveredText_ReadSnapshotLine(int index, TieRecoveredTextSnapshotLine* out) {
	if (!out || index < 0 || index >= num_text_lines)
		return false;
	memset(out, 0, sizeof *out);
	xparagrp_Get_Paragraph_String(text_para[text_res[index]], out->text, 1, text_string[index]);
	out->text[sizeof out->text - 1] = '\0';
	out->x = text_x[index];
	out->y = text_y[index];
	out->color = (uint8_t)text_color[index];
	out->bold_color = (uint8_t)xfont_Get_FontID_Bold_Color(0);
	out->shadow_color = (uint8_t)xfont_Get_FontID_Shadow_Color(0);
	return true;
}

#endif

// FUNCTION: TIE95 0x6FC24
const char* textext_Get_Text(uint16_t id) {
	int16_t res_id, str_idx;

	strcpy(text_ext_string, "*");
	res_id = text_resource_table[id];
	str_idx = text_string_table[id];

	if (text_para[res_id])
		xparagrp_Get_Paragraph_String(text_para[res_id], text_ext_string, 0, str_idx);

	return text_ext_string;
}

// FUNCTION: TIE95 0x6FC7C
void textext_Copy_Text(char* string, uint16_t id) { strcpy(string, textext_Get_Text(id)); }

// FUNCTION: TIE95 0x6FCAC
void textext_Cat_Text(char* string, uint16_t id) { strcat(string, textext_Get_Text(id)); }

// FUNCTION: TIE95 0x6FCE4
void textext_Copy_Joy_Text(char* string, int16_t id) {
	const char* text;

	switch (id) {
		case 0:
			text = textext_Get_Text(txtJoyCenter);
			strcpy(string, text);
			break;
		case 1:
			text = textext_Get_Text(txtJoyTopLeft);
			strcpy(string, text);
			break;
		case 2:
			text = textext_Get_Text(txtJoyBtmRight);
			strcpy(string, text);
			break;
	}
}

// FUNCTION: TIE95 0x6FD38
void textext_Get_Ship_Text(char* string, int16_t ship_id) {
	xparagrp_Get_Paragraph_String(text_para[0], string, 2, ship_id);
}

// FUNCTION: TIE95 0x6FD5C
void textext_Get_Train_Text(char* string, int16_t line) {
	xparagrp_Get_Paragraph_String(text_para[0], string, 3, line);
}

// FUNCTION: TIE95 0x6FD80
int16_t textext_Count_Train_Text_Lines(void) { return xparagrp_Count_Paragraph_Strings(text_para[0], 3); }

// FUNCTION: TIE95 0x6FD98
void textext_Get_Weapon_Select_Text(char* string, int16_t line) {
	xparagrp_Get_Paragraph_String(text_para[0], string, 4, line);
}

// FUNCTION: TIE95 0x6FDBC
// FUNCTION: TIE98 0x48CE60
static void textext_Find_Text_Range(int16_t scene, int16_t* pstart, int16_t* pstop) {
	int16_t index, start, stop;

	index = 0;
	start = -1;
	stop = -1;

	/* Search for first entry matching scene (sentinel = scene word -1) */
	while (text_scene_list_gbl[index] != -1) {
		if (text_scene_list_gbl[index] == scene) {
			start = index / 7;
			index += 7;
			break;
		}
		index += 7;
	}

	if (start != -1) {
		/* Extend to cover all consecutive entries for this scene */
		while (text_scene_list_gbl[index] != -1 && text_scene_list_gbl[index] == scene)
			index += 7;
		stop = index / 7;
	}

	*pstart = start;
	*pstop = stop;
}
