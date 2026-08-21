#include "tie/textext.h"
#include "tie/shellext.h"
#include "tie_runtime/snapshot/capture_views.h"
#include "tie_runtime/snapshot/snapshot.h"
#include "tie_runtime/snapshot/snapshot_internal.h" /* TieSnapshotBuilder_AllocUIText, TIE_EMIT_TARGET_* */

#include "landru/actcust.h"
#include "landru/actor.h"
#include "landru/canvas.h"
#include "landru/dirty.h"
#include "landru/font.h"
#include "landru/paragrp.h"
#include "landru/rect.h"
#include "landru/res.h"
#include "landru/view.h"

#include <stdlib.h>
#include <string.h>

#include "tie/textext_data.h"

#include "tie/shell.h"

#include "tie/stub.h"

/* --- Static data tables --- */

static char text_res_names[3][16] = { "tietext0.lfd", "tietext1.lfd", "tietext2.lfd" };
static char text_file_names[3][16] = { "tietext0", "tietext1", "tietext2" };

/* Maps text ID (0..288) to resource index (0, 1, or 2). Entry 288 = 99 (sentinel). */
static int16_t text_resource_table[289];

/* 7-word entries: [scene, resource, start_time, stop_time, x, y, fade_type], sentinel=-1 */
static int16_t text_scene_list_gbl[1099];

/* --- Runtime state --- */

static int16_t text_start_gbl;
static int16_t text_stop_gbl;
static Rect prev_text_bounds;
static char text_ext_string[256];
static int16_t text_string_table[288];
static Rect text_bounds;
static Actor* display_text_actor;
static Actor* restore_text_actor;
static int16_t text_color[3];
static int16_t text_h[3];
static int16_t text_w[3];
static int16_t text_y[3];
static int16_t text_x[3];
static int16_t text_string[3];
static int16_t text_res[3];
/* Original TextFade type per active line — kept so the snapshot
 * emitter can map to a TIE_SUBTITLE_STYLE_* enum without
 * heuristically inverting the color computation. */
static TextFade text_fade_type[3];
// GLOBAL: TIE 0xF5D5A
static void* text_para[3];
static int16_t num_text_lines;
static void* text_buffer;

/* --- Internal helpers --- */

static void Find_Text_Range(int16_t scene, int16_t* pstart, int16_t* pstop);
static void user_Text_Actor(Actor* the_actor, int32_t time);
static int draw_Text_Actor(Actor* the_actor, Rect* r, Rect* clip_r, int16_t x, int16_t y, int16_t refresh);

/* --- Functions --- */

// FUNCTION: TIE 0x6F4B0
void textext_Open_Text_Ext(void) {
	ResFile* res_file;
	int16_t size[3] = { 0, 0, 0 };
	int16_t i, res_idx;

	/* Initialize data tables from compiled-in binary data */
	memcpy(text_resource_table, text_resource_table_init, sizeof(text_resource_table));
	memcpy(text_scene_list_gbl, text_scene_list_init, sizeof(text_scene_list_init));

	for (i = 0; i < 3; i++) {
		res_file = shellext_Open_Empire_Resource(text_res_names[i]);
		if (res_file) {
			text_para[i] = lparagrp_Res_Paragraph(res_file, text_file_names[i]);
			lres_Close_Resource(res_file);
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

// FUNCTION: TIE 0x6F544
void textext_Close_Text_Ext(void) {
	int16_t i;

	for (i = 0; i < 3; i++) {
		if (text_para[i]) {
			lparagrp_Free_Paragraph(text_para[i]);
			text_para[i] = NULL;
		}
	}
}

// FUNCTION: TIE 0x6F588
void textext_Open_Text_Ext_Scene(int16_t scene) {
	Rect r;
	int16_t start, stop;

	Find_Text_Range(scene, &start, &stop);
	text_start_gbl = start;
	text_stop_gbl = stop;

	if (start == -1 || stop == -1)
		return;

	lrect_Set_Rect(&r, 0, 0, 320, 200);

	ViewStruct* view = lview_Get_Current_View();
	if (view->clear)
		text_buffer = NULL;
	else
		text_buffer = malloc(12800);

	restore_text_actor = lactcust_Alloc_Custom_Actor(NULL, &r, 0, 0, 10000);
	display_text_actor = lactcust_Alloc_Custom_Actor(NULL, &r, 0, 0, -10000);
	restore_text_actor->id = 0;
	display_text_actor->id = 1;

	lactor_Set_Actor_User_Function(display_text_actor, user_Text_Actor);
	lactor_Set_Actor_Draw_Function(restore_text_actor, (lactorDrawFunc)draw_Text_Actor);
	lactor_Set_Actor_Draw_Function(display_text_actor, (lactorDrawFunc)draw_Text_Actor);
}

// FUNCTION: TIE 0x6F690
void textext_Close_Text_Ext_Scene(int16_t scene) {
	(void)scene;
	if (text_buffer) {
		free(text_buffer);
		text_buffer = NULL;
	}
	/* user_Text_Actor stops running once display_text_actor is gone;
	 * zero num_text_lines so TieRecoveredText_CaptureSnapshot doesn't keep
	 * emitting the last frame's data into subsequent snapshots. */
	num_text_lines = 0;
}

/*
 * Per-frame text scheduling callback.
 * Walks text_scene_list_gbl for the current scene range, finds lines whose
 * time window includes the current frame, computes position and fade color.
 */
static void user_Text_Actor(Actor* the_actor, int32_t time) {
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
	lrect_Copy_Rect(&prev_text_bounds, &text_bounds);
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

		if (start > (int16_t)time || stop <= (int16_t)time)
			continue;
		if (type >= fadeTitle1 && !options_gbl.text_active && digital_exists)
			continue;
		if (!text_para[resource])
			continue;
		if (num_text_lines >= 3)
			continue;

		lparagrp_Get_Paragraph_String(text_para[resource], str, 1, i - base_id);

		old_font = lfont_Set_Font(0);
		width = lfont_Get_String_Width(str);
		lfont_Set_Font(old_font);

		text_res[num_text_lines] = resource;
		text_string[num_text_lines] = i - base_id;
		text_fade_type[num_text_lines] = type;
		text_w[num_text_lines] = width;
		text_h[num_text_lines] = lfont_Get_FontID_Height(0);

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
		if (lactor_Is_Actor_Visible(display_text_actor))
			lactor_Show_Actor(restore_text_actor);
		else
			lactor_Show_Actor(display_text_actor);

		lrect_Set_Rect(&text_bounds, text_x[0], text_y[0], text_w[0] + text_x[0], text_h[0] + text_y[0]);

		for (i = 1; i < num_text_lines; i++) {
			lrect_Set_Rect(&r, text_x[i], text_y[i], text_w[i] + text_x[i], text_h[i] + text_y[i]);
			lrect_Enclose_Rect(&text_bounds, &r);
		}
	} else {
		if (lactor_Is_Actor_Visible(display_text_actor))
			lactor_Hide_Actor(display_text_actor);
		else
			lactor_Hide_Actor(restore_text_actor);
	}

	/* Per-line state is captured by the lfont snapshot hook when the
	 * text actor's draw callback runs lfont_Print_Clipped_Text — no
	 * separate textext snapshot channel is needed. */
}

/*
 * Draw callback for text actors.
 * id=1 (display): save background, draw text lines with shadow.
 * id=0 (restore): restore previously saved background.
 */
static int draw_Text_Actor(Actor* the_actor, Rect* r, Rect* clip_r, int16_t x, int16_t y, int16_t refresh) {
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
			lrect_Copy_Rect(&br, &text_bounds);
			lrect_Origin_Rect(&br);
			stub_Copy_To_Clipped_Buffer(text_buffer, &br, text_bounds.left, text_bounds.top,
										br.right - br.left, br.bottom - br.top);
		}

		lcanvas_Set_Suppress_Text_Render(true);
		lfont_Enable_FontID_Shadow(0);
		for (i = 0; i < num_text_lines; i++) {
			lparagrp_Get_Paragraph_String(text_para[text_res[i]], string, 1, text_string[i]);
			lfont_Print_Clipped_Text(string, text_x[i], text_y[i], 0, text_color[i]);
		}
		lfont_Disable_FontID_Shadow(0);
		lcanvas_Set_Suppress_Text_Render(false);

		ldirty_Dirty_Rect(&text_bounds);
	} else {
		/* Restore actor: put saved background back */
		if (text_buffer) {
			lrect_Copy_Rect(&br, &prev_text_bounds);
			lrect_Origin_Rect(&br);
			stub_Copy_From_Clipped_Buffer(text_buffer, &br, prev_text_bounds.left, prev_text_bounds.top,
										  br.right - br.left, br.bottom - br.top);
			ldirty_Dirty_Rect(&prev_text_bounds);
		}
	}

	return 1;
}

/*
 * Per-tick snapshot emitter — writes the current frame's per-line
 * subtitle state into the unified TieUIText channel. Called from
 * TieRuntime_Tick alongside the other emit_* helpers.
 *
 * Required because the actor system's dirty-rect machinery only
 * fires draw_Text_Actor on redraw frames (the engine's classic FB
 * persists between draws), so the lfont snapshot hook misses ticks
 * where the text is on screen but not being repainted. Re-emitting
 * here every tick (using the fade-machine state populated by
 * user_Text_Actor) gives the renderer per-tick coverage; the lfont
 * hook is suppressed inside draw_Text_Actor's display branch to
 * keep this from being doubled on the redraw frames.
 */
int TieRecoveredText_SnapshotLineCount(void) { return num_text_lines > 0 ? num_text_lines : 0; }

bool TieRecoveredText_ReadSnapshotLine(int index, TieRecoveredTextSnapshotLine* out) {
	if (!out || index < 0 || index >= num_text_lines)
		return false;
	memset(out, 0, sizeof *out);
	lparagrp_Get_Paragraph_String(text_para[text_res[index]], out->text, 1, text_string[index]);
	out->text[sizeof out->text - 1] = '\0';
	out->x = text_x[index];
	out->y = text_y[index];
	out->color = (uint8_t)text_color[index];
	out->bold_color = (uint8_t)lfont_Get_FontID_Bold_Color(0);
	out->shadow_color = (uint8_t)lfont_Get_FontID_Shadow_Color(0);
	return true;
}

// FUNCTION: TIE 0x6FC24
const char* textext_Get_Text(int16_t id) {
	int16_t res_id, str_idx;

	strcpy(text_ext_string, "*");
	res_id = text_resource_table[id];
	str_idx = text_string_table[id];

	if (text_para[res_id])
		lparagrp_Get_Paragraph_String(text_para[res_id], text_ext_string, 0, str_idx);

	return text_ext_string;
}

// FUNCTION: TIE 0x6FC7C
void textext_Copy_Text(char* string, int16_t id) { strcpy(string, textext_Get_Text(id)); }

// FUNCTION: TIE 0x6FCAC
void textext_Cat_Text(char* string, int16_t id) { strcat(string, textext_Get_Text(id)); }

// FUNCTION: TIE 0x6FCE4
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

// FUNCTION: TIE 0x6FD38
void textext_Get_Ship_Text(char* string, int16_t ship_id) {
	lparagrp_Get_Paragraph_String(text_para[0], string, 2, ship_id);
}

// FUNCTION: TIE 0x6FD5C
void textext_Get_Train_Text(char* string, int16_t line) {
	lparagrp_Get_Paragraph_String(text_para[0], string, 3, line);
}

// FUNCTION: TIE 0x6FD80
int16_t textext_Count_Train_Text_Lines(void) { return lparagrp_Count_Paragraph_Strings(text_para[0], 3); }

// FUNCTION: TIE 0x6FD98
void textext_Get_Weapon_Select_Text(char* string, int16_t line) {
	lparagrp_Get_Paragraph_String(text_para[0], string, 4, line);
}

static void Find_Text_Range(int16_t scene, int16_t* pstart, int16_t* pstop) {
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

Rect* textext_Get_Prev_Text_Bounds_Rect(void) { return &prev_text_bounds; }

void textext_Clear_Prev_Text_Bounds_Rect(void) { lrect_Clear_Rect(&prev_text_bounds); }

Rect* textext_Get_Text_Bounds_Rect(void) { return &text_bounds; }

void textext_Clear_Text_Bounds_Rect(void) { lrect_Clear_Rect(&text_bounds); }
