#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "tie/player.h"
#include "tie/shellext.h"
#include "tie/soundext.h"
#include "tie/stub.h"
#include "tie/talk.h"
#include "tie/tie.h"
#include "tie_runtime/audio/config.h"
#include "tie_runtime/diagnostics/diagnostics.h"
#include "tie_runtime/display/classic_display.h"
#include "tie_runtime/display/classic_framebuffer.h"
#include "tie_runtime/flight_assets/model_types.h"
#include "tie_runtime/input/input.h"
#include "tie_runtime/runtime/exports.h"
#include "tie_runtime/runtime/profile.h"
#include "tie_runtime/snapshot/capture_views.h"
#include "tie_runtime/snapshot/snapshot.h"
#include "tie_runtime/snapshot/snapshot_internal.h"
#include "tie_runtime/snapshot/snapshot_map.h"
#include "tie_runtime/storage/storage.h"

#include "landru/actanim.h"
#include "landru/actdelt.h"
#include "landru/actor.h"
#include "landru/bitmap.h"
#include "landru/canvas.h"
#include "landru/dirty.h"
#include "landru/font.h"
#include "landru/inpattr.h"
#include "landru/input.h"
#include "landru/memptr.h"
#include "landru/paint.h"
#include "landru/rect.h"
#include "landru/res.h"
#include "landru/view.h"

#include "../util/binio.h"

/* ---- EBriefPage on-disk codec ---- */

/* Decode an 810-byte little-endian page record into the natively
 * aligned runtime struct. Layout: 5 leading int16 scalars at +0..+8,
 * followed by commands[400] at +0x0A. Read-only at runtime so no
 * encoder is provided. */
void TieRecoveredBrief_DecodePage(EBriefPage* dst, const uint8_t* src) {
	dst->len = br_i16le(src + 0x00);
	dst->time = br_i16le(src + 0x02);
	dst->index = br_i16le(src + 0x04);
	dst->size = br_i16le(src + 0x06);
	dst->tile = br_i16le(src + 0x08);
	for (int i = 0; i < 400; ++i)
		dst->commands[i] = br_i16le(src + 0x0A + i * 2);
}

/* ---- Page command opcodes and parameter counts ---- */

/* Parameter count per opcode (indexed by BriefCmd) */
static const int16_t map_cmd_size[BCMD_END_PAGE + 2] = {
	/*  0 */ 0,
	/*  1 SEEK          */ 0,
	/*  2 (unused)      */ 1,
	/*  3 CLEAR_PARA    */ 0,
	/*  4 SHOW_PARA0    */ 1,
	/*  5 SHOW_PARA1    */ 1,
	/*  6 MOVE          */ 2,
	/*  7 ZOOM          */ 2,
	/*  8 CLEAR_TARGET  */ 0,
	/*  9 SHOW_TARGET0  */ 1,
	1,
	1,
	1,
	1,
	1,
	1,
	/* 16 SHOW_TARGET7  */ 1,
	/* 17 CLEAR_TEXT    */ 0,
	/* 18 SHOW_TEXT0    */ 4,
	4,
	4,
	4,
	4,
	4,
	4,
	/* 25 SHOW_TEXT7    */ 4,
	/* 26-33 (unused)   */ 0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	/* 34 END_PAGE      */ 0,
	/*    sentinel       */ 0,
};

/* ---- Static globals (all owned by PLAYER.C per watdbg) ---- */

/* Briefing polygon for talk-screen projection */
static Poly brief_poly;
// GLOBAL: TIE 0xFABC2
static int16_t brief_poly_used;

/* Icon actors: [0]=green(enemy), [1]=red, [2]=blue(friendly), [3]=purple(neutral) */
static Actor* icon_actors[4];

/* Brief buffer bitmap for polygon projection */
static BitmapStruct brief_buffer;

/* Flight group data + briefing state */
// GLOBAL: TIE 0xF6EC0
static EFArrayStruct fgroup;
static EBriefStruct brief;

/* Map display state */
static int16_t map_playing; /* 1 = animation running */
// GLOBAL: TIE 0xF6EA2
static int16_t selected_fg_idx;
static int16_t map_center_x, map_center_y;
static int16_t map_target_x, map_target_y;
static int16_t map_scale_x, map_scale_y;
static int16_t map_scale_target_x, map_scale_target_y;
static Rect map_src_rect; /* map area rect (0,0,292,147) */

/* Star background */
static Actor* stars_actor;
static void* star_buffer_data; /* 320x150 pixel cache */

static Input* map_emit_widget;

/* Weapon state */
// GLOBAL: TIE 0xFABC0
static int16_t beam_level;
// GLOBAL: TIE 0xFABC6
static int16_t weapon_level;

/* ---- Helpers ---- */

static int16_t abs16(int16_t v) { return v < 0 ? -v : v; }

/* Signed division toward zero (matches Watcom codegen for >> 8 with sign adjust) */
static int16_t sdiv256(int32_t v) { return (int16_t)(v / 256); }

/* Side → icon index mapping */
static int16_t side_to_icon(int16_t side) {
	switch (side) {
		case 1:
		case 4:
			return 1;
		case 2:
			return 2;
		case 3:
		case 5:
			return 3;
		default:
			return 0;
	}
}

/* Side → target highlight base color */
static int16_t side_to_color(int16_t side) {
	switch (side) {
		case 1:
		case 4:
			return 232;
		case 2:
			return 248;
		case 3:
		case 5:
			return 240;
		default:
			return 224;
	}
}

/* Clear the para_on / target_on / text_on arrays */
static void clear_brief_state(void) {
	int16_t i;
	for (i = 0; i < 2; i++)
		brief.para_on[i] = 0;
	for (i = 0; i < 8; i++)
		brief.target_on[i] = 0;
	for (i = 0; i < 8; i++)
		brief.text_on[i] = 0;
}

/* ---- Forward declarations for XINPUT callbacks ---- */
static int16_t iupdate_Map(Input* inp, Rect* bounds, Rect* clip, int16_t key, uint8_t left, uint8_t right,
						   int16_t mouse_x, int16_t mouse_y);
static void iuser_Map(Input* inp, int32_t time);
static void idraw_Map(Input* inp, Rect* bounds, Rect* clip, int16_t refresh);

/* ================================================================
 * Weapon selectors
 * ================================================================ */

// FUNCTION: TIE 0x7D8EC
int16_t player_Get_Beam_Used(void) { return mission.beam_used; }

// FUNCTION: TIE 0x7D8F4
void player_Next_Beam(void) {
	if (++mission.beam_used > beam_level)
		mission.beam_used = 1;
}

// FUNCTION: TIE 0x7D918
void player_Last_Beam(void) {
	if (--mission.beam_used == 0)
		mission.beam_used = beam_level;
}

// FUNCTION: TIE 0x7D938
int16_t player_Get_Torp_Used(void) { return mission.torp_used; }

// FUNCTION: TIE 0x7D940
void player_Next_Torp(void) {
	if (++mission.torp_used > weapon_level)
		mission.torp_used = 1;
}

// FUNCTION: TIE 0x7D964
void player_Last_Torp(void) {
	if (--mission.torp_used == 0)
		mission.torp_used = weapon_level;
}

/* ================================================================
 * Data accessors
 * ================================================================ */

EBriefStruct* player_Fetch_Brief(void) { return &brief; }

// FUNCTION: TIE 0x7D8B8
EFArrayStruct* player_Fetch_FGroup(void) { return &fgroup; }

// FUNCTION: TIE 0x7D99C
int16_t player_Is_Side_Enemy(int16_t side) {
	switch (side) {
		case 0:
		case 4:
			return 1;
		case 1:
			return 0;
		case 2:
		case 3:
		case 5:
			return fgroup.mission.neutral_name[side - 2][0] == '1';
		default:
			return 0;
	}
}

// FUNCTION: TIE 0x7E838
int16_t player_Is_Map_Playing(void) { return map_playing; }

// FUNCTION: TIE 0x7E840
int player_Toggle_Map_Play(void) {
	map_playing = (map_playing == 0) ? 1 : 0;
	return 1;
}

/* ================================================================
 * Move_To_Value — step current toward target
 * ================================================================ */

// FUNCTION: TIE 0x7EAA4
int16_t player_Move_To_Value(int16_t current, int16_t target, int16_t step) {
	if (current > target) {
		current -= step;
		if (current < target)
			current = target;
	}
	if (current < target) {
		current += step;
		if (current > target)
			current = target;
	}
	return current;
}

/* ================================================================
 * Map coordinate transforms
 * ================================================================ */

// FUNCTION: TIE 0x7E990
void player_Map_To_Screen_Pos(Rect* view_rect, int16_t map_x, int16_t map_y, int16_t* out_x, int16_t* out_y) {
	int32_t sx = (int32_t)(map_x - map_center_x) * map_scale_x;
	*out_x = sdiv256(sx) + view_rect->left + ((view_rect->right - view_rect->left) >> 1);

	int32_t sy = (int32_t)(map_y - map_center_y) * map_scale_y;
	*out_y = sdiv256(sy) + view_rect->top + ((view_rect->bottom - view_rect->top) >> 1);
}

void player_Screen_To_Map_Pos(Rect* view_rect, int16_t screen_x, int16_t screen_y, int16_t* out_x,
							  int16_t* out_y) {
	int16_t view_cx = view_rect->left + ((view_rect->right - view_rect->left) >> 1);
	int16_t view_cy = view_rect->top + ((view_rect->bottom - view_rect->top) >> 1);

	*out_x = map_center_x + (int16_t)((int16_t)(screen_x - view_cx) << 8) / map_scale_x;
	*out_y = map_center_y + (int16_t)((int16_t)(screen_y - view_cy) << 8) / map_scale_y;
}

int16_t player_Find_Ship_On_Screen(Rect* bounds, int16_t screen_x, int16_t screen_y) {
	int16_t min_dist = 999;
	int16_t found_fg = 0;
	int16_t i;
	int16_t scr_x, scr_y;

	for (i = 0; i < fgroup.num_fgs; i++) {
		if (!fgroup.fg[i].way_used[14])
			continue;

		int32_t sx = (int32_t)(fgroup.fg[i].way_x[14] - map_center_x) * map_scale_x;
		scr_x = sdiv256(sx) + bounds->left + ((bounds->right - bounds->left) >> 1);

		int32_t sy = (int32_t)(fgroup.fg[i].way_y[14] - map_center_y) * map_scale_y;
		scr_y = sdiv256(sy) + bounds->top + ((bounds->bottom - bounds->top) >> 1);

		int16_t dx = screen_x - scr_x;
		if (abs16(dx) >= min_dist)
			continue;

		int16_t dy = screen_y - scr_y;
		if (abs16(dy) >= min_dist)
			continue;

		int16_t chebyshev;
		if (abs16(dx) >= abs16(dy))
			chebyshev = dx;
		else
			chebyshev = dy;

		min_dist = abs16(chebyshev);
		found_fg = i;
	}

	if (min_dist == 999)
		return 0;

	selected_fg_idx = found_fg;
	return 1;
}

/* ================================================================
 * Actor / star buffer helpers
 * ================================================================ */

// FUNCTION: TIE 0x7F55C
void player_Actor_To_Buffer(Actor* actor, void* buffer) {
	Rect r;
	lrect_Set_Rect(&r, 0, 0, 320, 150);
	if (actor->draw) {
		lpaint_Paint_Clipped_Rect(&r, actor->var1);
		actor->draw(actor, &r, &r, actor->x, actor->y, 1);
	}
	stub_Copy_To_Clipped_Buffer(buffer, &r, 0, 0, 320, 150);
}

// FUNCTION: TIE 0x7F5D0
void player_Stars_To_Back(int16_t screen_y) {
	if (star_buffer_data) {
		Rect r;
		lrect_Set_Rect(&r, 0, 0, 320, 150);
		stub_Copy_From_Clipped_Buffer(star_buffer_data, &r, 0, screen_y, 320, 150);
	}
}

/* ================================================================
 * Page command processing
 * ================================================================ */

/* Process a single page command step. `flag` nonzero means skip SFX and
 * snap move/zoom instantly. Used for initial seek and rewind. */
// FUNCTION: TIE 0x7E460
void player_Step_Page(int16_t flag) {
	int16_t cmd_index = brief.page.index;
	int16_t cmd_time = brief.page.commands[cmd_index];
	int16_t saved_index = cmd_index;
	int16_t params[4];
	int16_t sfx_volume = brief_poly_used ? 48 : 70;

	brief.para_off = 0;
	brief.target_off = 0;
	brief.text_off = 0;
	brief.move_on = 0;
	brief.scale_on = 0;
	brief.seek_on = 0;

	while (cmd_time <= brief.page.time) {
		saved_index = cmd_index;
		cmd_time = brief.page.commands[cmd_index];
		int16_t opcode = brief.page.commands[cmd_index + 1];
		cmd_index += 2;

		int16_t j;
		for (j = 0; j < map_cmd_size[opcode]; j++)
			params[j] = brief.page.commands[cmd_index++];

		if (cmd_time != brief.page.time)
			continue;

		switch (opcode) {
			case BCMD_SEEK:
				brief.seek_on = 1;
				break;
			case BCMD_CLEAR_PARA: {
				int16_t k;
				for (k = 0; k < 2; k++)
					brief.para_on[k] = 0;
				brief.para_off = 1;
				break;
			}
			case BCMD_SHOW_PARA0:
			case BCMD_SHOW_PARA1: {
				int16_t slot = opcode - BCMD_SHOW_PARA0;
				brief.para_on[slot] = 1;
				brief.para_id[slot] = params[0];
				/* Retail PLAYER_Step_Page case 5 stamps the slot-1
				 * paragraph id into talk_voice_question. The briefing
				 * map's end-view callback watches for that to change
				 * and fires voice/<sp>m<m>/...i<id>.voc each time. */
				if (slot == 1)
					talk_voice_question = params[0];
				break;
			}
			case BCMD_MOVE:
				if (cmd_time && !flag) {
					map_target_x = params[0];
					map_target_y = params[1];
				} else {
					map_center_x = params[0];
					map_center_y = params[1];
					map_target_x = params[0];
					map_target_y = params[1];
				}
				brief.move_on = 1;
				break;
			case BCMD_ZOOM:
				if (cmd_time && !flag) {
					map_scale_target_x = params[0];
					map_scale_target_y = params[1];
				} else {
					map_scale_x = params[0];
					map_scale_y = params[1];
					map_scale_target_x = params[0];
					map_scale_target_y = params[1];
				}
				brief.scale_on = 1;
				break;
			case BCMD_CLEAR_TARGET: {
				int16_t k;
				for (k = 0; k < 8; k++)
					brief.target_on[k] = 0;
				brief.target_off = 1;
				break;
			}
			case BCMD_SHOW_TARGET0:
			case BCMD_SHOW_TARGET0 + 1:
			case BCMD_SHOW_TARGET0 + 2:
			case BCMD_SHOW_TARGET0 + 3:
			case BCMD_SHOW_TARGET0 + 4:
			case BCMD_SHOW_TARGET0 + 5:
			case BCMD_SHOW_TARGET0 + 6:
			case BCMD_SHOW_TARGET7: {
				if (!flag) {
					int16_t side = fgroup.fg[params[0]].side;
					if (side > 2)
						side = 2;
					soundext_Play_SFX(side == 1 ? sfxTarget2 : sfxTarget1, sfx_volume);
				}
				int16_t slot = opcode - BCMD_SHOW_TARGET0;
				brief.target_on[slot] = 1;
				brief.target_state[slot] = flag ? 80 : 0;
				brief.target_id[slot] = params[0];
				break;
			}
			case BCMD_CLEAR_TEXT: {
				int16_t k;
				for (k = 0; k < 8; k++)
					brief.text_on[k] = 0;
				brief.text_off = 1;
				break;
			}
			case BCMD_SHOW_TEXT0:
			case BCMD_SHOW_TEXT0 + 1:
			case BCMD_SHOW_TEXT0 + 2:
			case BCMD_SHOW_TEXT0 + 3:
			case BCMD_SHOW_TEXT0 + 4:
			case BCMD_SHOW_TEXT0 + 5:
			case BCMD_SHOW_TEXT0 + 6:
			case BCMD_SHOW_TEXT7: {
				if (!flag) {
					char text_buf[40];
					char* src = (char*)brief.text_data[params[0]];
					if (src) {
						strcpy(text_buf, src);
						int16_t text_len = (int16_t)strlen(text_buf);
						if (text_len) {
							soundext_Play_SFX(sfxText, 0);
							soundext_Fade_SFX(sfxText, 0, 4 * text_len);
						}
					}
				}
				int16_t slot = opcode - BCMD_SHOW_TEXT0;
				brief.text_on[slot] = 1;
				brief.text_state[slot] = flag ? 80 : 0;
				brief.text_id[slot] = params[0];
				brief.text_x[slot] = params[1];
				brief.text_y[slot] = params[2];
				brief.text_color[slot] = params[3];
				break;
			}
			default:
				break;
		}
	}

	brief.page.index = saved_index;
	brief.page.time++;
}

// FUNCTION: TIE 0x7E1E0
void player_Clear_Page_Commands(void) {
	brief.page.len = 200;
	brief.page.time = 0;
	brief.page.index = 0;
	brief.page.size = 2;
	brief.page.tile = 0;
	brief.page.commands[0] = 9999;
	brief.page.commands[1] = BCMD_END_PAGE;

	map_center_x = 0;
	map_center_y = 0;
	map_target_x = 0;
	map_target_y = 0;
	map_scale_x = 16;
	map_scale_y = 16;
	map_scale_target_x = 16;
	map_scale_target_y = 16;

	clear_brief_state();

	brief.page.time = 0;
	brief.page.index = 0;
	player_Step_Page(1);
}

void player_Rewind_Page(void) {
	int16_t i;

	map_target_x = 0;
	map_target_y = 0;
	map_scale_x = 16;
	map_scale_y = 16;
	map_scale_target_x = 16;
	map_scale_target_y = 16;
	map_center_x = 0;
	map_center_y = 0;

	clear_brief_state();

	int16_t cmd_index = 0;
	brief.page.index = 0;
	int16_t cmd_time = brief.page.commands[0];
	brief.page.time = 0;
	int16_t saved_index = 0;
	int16_t params[4];
	brief.para_off = 0;
	brief.target_off = 0;
	brief.text_off = 0;
	brief.move_on = 0;
	brief.scale_on = 0;
	brief.seek_on = 0;

	while (cmd_time <= brief.page.time) {
		saved_index = cmd_index;
		cmd_time = brief.page.commands[cmd_index];
		int16_t opcode = brief.page.commands[cmd_index + 1];
		cmd_index += 2;

		for (i = 0; i < map_cmd_size[opcode]; i++)
			params[i] = brief.page.commands[cmd_index++];

		if (cmd_time != brief.page.time)
			continue;

		switch (opcode) {
			case BCMD_SEEK:
				brief.seek_on = 1;
				break;
			case BCMD_CLEAR_PARA:
				for (i = 0; i < 2; i++)
					brief.para_on[i] = 0;
				brief.para_off = 1;
				break;
			case BCMD_SHOW_PARA0:
			case BCMD_SHOW_PARA1: {
				int16_t slot = opcode - BCMD_SHOW_PARA0;
				brief.para_on[slot] = 1;
				brief.para_id[slot] = params[0];
				/* Retail PLAYER_Step_Page case 5 stamps the slot-1
				 * paragraph id into talk_voice_question. The briefing
				 * map's end-view callback watches for that to change
				 * and fires voice/<sp>m<m>/...i<id>.voc each time. */
				if (slot == 1)
					talk_voice_question = params[0];
				break;
			}
			case BCMD_MOVE:
				map_center_x = params[0];
				map_center_y = params[1];
				map_target_x = params[0];
				map_target_y = params[1];
				brief.move_on = 1;
				break;
			case BCMD_ZOOM:
				map_scale_x = params[0];
				map_scale_y = params[1];
				map_scale_target_x = params[0];
				map_scale_target_y = params[1];
				brief.scale_on = 1;
				break;
			case BCMD_CLEAR_TARGET:
				for (i = 0; i < 8; i++)
					brief.target_on[i] = 0;
				brief.target_off = 1;
				break;
			case BCMD_SHOW_TARGET0:
			case BCMD_SHOW_TARGET0 + 1:
			case BCMD_SHOW_TARGET0 + 2:
			case BCMD_SHOW_TARGET0 + 3:
			case BCMD_SHOW_TARGET0 + 4:
			case BCMD_SHOW_TARGET0 + 5:
			case BCMD_SHOW_TARGET0 + 6:
			case BCMD_SHOW_TARGET7: {
				int16_t slot = opcode - BCMD_SHOW_TARGET0;
				brief.target_on[slot] = 1;
				brief.target_state[slot] = 80;
				brief.target_id[slot] = params[0];
				break;
			}
			case BCMD_CLEAR_TEXT:
				for (i = 0; i < 8; i++)
					brief.text_on[i] = 0;
				brief.text_off = 1;
				break;
			case BCMD_SHOW_TEXT0:
			case BCMD_SHOW_TEXT0 + 1:
			case BCMD_SHOW_TEXT0 + 2:
			case BCMD_SHOW_TEXT0 + 3:
			case BCMD_SHOW_TEXT0 + 4:
			case BCMD_SHOW_TEXT0 + 5:
			case BCMD_SHOW_TEXT0 + 6:
			case BCMD_SHOW_TEXT7: {
				int16_t slot = opcode - BCMD_SHOW_TEXT0;
				brief.text_on[slot] = 1;
				brief.text_state[slot] = 80;
				brief.text_id[slot] = params[0];
				brief.text_x[slot] = params[1];
				brief.text_y[slot] = params[2];
				brief.text_color[slot] = params[3];
				break;
			}
			default:
				break;
		}
	}

	brief.page.index = saved_index;
	brief.page.time++;
}

void player_Reseek_Page(void) {
	int16_t time = brief.page.time;
	if (!time)
		time = 1;

	map_target_x = 0;
	map_target_y = 0;
	map_scale_x = 16;
	map_scale_y = 16;
	map_scale_target_x = 16;
	map_scale_target_y = 16;
	map_center_x = 0;
	map_center_y = 0;

	clear_brief_state();

	brief.page.time = 0;
	brief.page.index = 0;
	player_Step_Page(1);

	int16_t target_time = time - 1;
	if (target_time != brief.page.time - 1) {
		if (target_time < brief.page.time)
			player_Rewind_Page();
		while (target_time >= brief.page.time)
			player_Step_Page(1);
	}
}

void player_Seek_Page(int16_t page, int32_t time) {
	int16_t target_page = page;
	int16_t seek_time = (int16_t)time;

	if (target_page == brief.page.time - 1)
		return;

	if (target_page < brief.page.time) {
		map_target_x = 0;
		map_target_y = 0;
		map_scale_x = 16;
		map_scale_y = 16;
		map_scale_target_x = 16;
		map_scale_target_y = 16;
		map_center_x = 0;
		map_center_y = 0;

		clear_brief_state();

		brief.page.time = 0;
		brief.page.index = 0;
		player_Step_Page(1);
	}

	while (target_page >= brief.page.time)
		player_Step_Page(seek_time ? 1 : 0);
}

void player_Seek_Page_Section(void) {
	int16_t start_time = brief.page.time;
	int16_t section_done = 0;
	int16_t para_count = 0;
	int16_t has_para = 0;
	int16_t i;

	map_center_x = 0;
	map_center_y = 0;
	map_target_x = 0;
	map_target_y = 0;
	map_scale_x = 16;
	map_scale_y = 16;
	map_scale_target_x = 16;
	map_scale_target_y = 16;

	clear_brief_state();

	brief.page.time = 0;
	brief.page.index = 0;
	player_Step_Page(1);

	int16_t next_opcode = 0;
	while (!section_done) {
		if (next_opcode == BCMD_END_PAGE)
			break;

		next_opcode = brief.page.commands[brief.page.index + 1];

		if (brief.para_off) {
			has_para = 0;
			para_count = 0;
		}
		for (i = 0; i < 2; i++) {
			if (brief.para_on[i])
				has_para = 1;
		}
		if (has_para)
			para_count++;

		if ((brief.scale_on || brief.move_on || brief.seek_on || para_count == 1) &&
			start_time <= brief.page.time) {
			section_done = 1;
		} else {
			player_Step_Page(1);
		}
	}

	/* Determine the time to seek to */
	int16_t time_val;
	int16_t next_cmd;
	if (brief.seek_on || para_count == 1) {
		time_val = brief.page.time;
		next_cmd = 0;
	} else {
		time_val = brief.page.commands[brief.page.index];
		next_cmd = brief.page.commands[brief.page.index + 1];
	}

	if (next_cmd == BCMD_END_PAGE) {
		/* End of page — reset to beginning */
		map_target_x = 0;
		map_target_y = 0;
		map_scale_x = 16;
		map_scale_y = 16;
		map_scale_target_x = 16;
		map_scale_target_y = 16;
		map_center_x = 0;
		map_center_y = 0;

		clear_brief_state();

		brief.page.time = 0;
		brief.page.index = 0;
		player_Step_Page(1);
	} else {
		int16_t target_page = time_val;
		if (target_page != brief.page.time - 1) {
			if (target_page < brief.page.time)
				player_Rewind_Page();
			while (target_page >= brief.page.time)
				player_Step_Page(0);
		}
	}
}

/* ================================================================
 * Map display movement / animation
 * ================================================================ */

void player_Move_Display_Map(void) {
	/* Compute scale step speed */
	int16_t scale_dx = abs16(map_scale_x - map_scale_target_x);
	int16_t scale_dy = abs16(map_scale_y - map_scale_target_y);
	if (scale_dx < scale_dy)
		scale_dx = scale_dy;

	int16_t scale_speed = 2;
	if (scale_dx >= 12)
		scale_speed = 8;
	if (map_scale_x < 10)
		scale_speed = 1;

	map_scale_x = player_Move_To_Value(map_scale_x, map_scale_target_x, scale_speed);
	map_scale_y = player_Move_To_Value(map_scale_y, map_scale_target_y, scale_speed);

	/* Compute move step speed */
	int16_t pixels_per_unit = map_scale_x ? (256 / map_scale_x + 1) : 1;

	int16_t move_dx = abs16(map_center_x - map_target_x);
	int16_t move_dy = abs16(map_center_y - map_target_y);
	if (move_dx < move_dy)
		move_dx = move_dy;

	int16_t move_dist = (int16_t)(move_dx / pixels_per_unit);
	int16_t move_speed = 2 * pixels_per_unit;
	if (move_dist >= 16)
		move_speed *= 2;

	map_center_x = player_Move_To_Value(map_center_x, map_target_x, move_speed);
	map_center_y = player_Move_To_Value(map_center_y, map_target_y, move_speed);

	/* Advance target/text animation state */
	int16_t i;
	for (i = 0; i < 8; i++) {
		if (brief.target_on[i])
			brief.target_state[i]++;
	}
	for (i = 0; i < 8; i++) {
		if (brief.text_on[i])
			brief.text_state[i]++;
	}
}

void player_Step_Display_Map(void) {
	if (brief.page.len <= brief.page.time) {
		/* Page ended — reset */
		map_target_x = 0;
		map_target_y = 0;
		map_scale_x = 16;
		map_scale_y = 16;
		map_scale_target_x = 16;
		map_scale_target_y = 16;
		map_center_x = 0;
		map_center_y = 0;

		clear_brief_state();

		brief.page.time = 0;
		brief.page.index = 0;
		player_Step_Page(1);
		return;
	}

	/* Process page commands for current time */
	int16_t cmd_index = brief.page.index;
	int16_t cmd_time = brief.page.commands[cmd_index];
	int16_t saved_index = cmd_index;
	int16_t params[4];
	int16_t sfx_volume = brief_poly_used ? 48 : 70;
	int16_t i;

	brief.para_off = 0;
	brief.target_off = 0;
	brief.text_off = 0;
	brief.move_on = 0;
	brief.scale_on = 0;
	brief.seek_on = 0;

	while (cmd_time <= brief.page.time) {
		saved_index = cmd_index;
		cmd_time = brief.page.commands[cmd_index];
		int16_t opcode = brief.page.commands[cmd_index + 1];
		cmd_index += 2;

		for (i = 0; i < map_cmd_size[opcode]; i++)
			params[i] = brief.page.commands[cmd_index++];

		if (cmd_time != brief.page.time)
			continue;

		switch (opcode) {
			case BCMD_SEEK:
				brief.seek_on = 1;
				break;
			case BCMD_CLEAR_PARA:
				for (i = 0; i < 2; i++)
					brief.para_on[i] = 0;
				brief.para_off = 1;
				break;
			case BCMD_SHOW_PARA0:
			case BCMD_SHOW_PARA1: {
				int16_t slot = opcode - BCMD_SHOW_PARA0;
				brief.para_on[slot] = 1;
				brief.para_id[slot] = params[0];
				/* Retail PLAYER_Step_Page case 5 stamps the slot-1
				 * paragraph id into talk_voice_question. The briefing
				 * map's end-view callback watches for that to change
				 * and fires voice/<sp>m<m>/...i<id>.voc each time. */
				if (slot == 1)
					talk_voice_question = params[0];
				break;
			}
			case BCMD_MOVE:
				if (!cmd_time)
					map_center_x = params[0], map_center_y = params[1];
				map_target_x = params[0];
				map_target_y = params[1];
				brief.move_on = 1;
				break;
			case BCMD_ZOOM:
				if (!cmd_time)
					map_scale_x = params[0], map_scale_y = params[1];
				map_scale_target_x = params[0];
				map_scale_target_y = params[1];
				brief.scale_on = 1;
				break;
			case BCMD_CLEAR_TARGET:
				for (i = 0; i < 8; i++)
					brief.target_on[i] = 0;
				brief.target_off = 1;
				break;
			case BCMD_SHOW_TARGET0:
			case BCMD_SHOW_TARGET0 + 1:
			case BCMD_SHOW_TARGET0 + 2:
			case BCMD_SHOW_TARGET0 + 3:
			case BCMD_SHOW_TARGET0 + 4:
			case BCMD_SHOW_TARGET0 + 5:
			case BCMD_SHOW_TARGET0 + 6:
			case BCMD_SHOW_TARGET7: {
				int16_t side = fgroup.fg[params[0]].side;
				if (side > 2)
					side = 2;
				soundext_Play_SFX(side == 1 ? sfxTarget2 : sfxTarget1, sfx_volume);
				int16_t slot = opcode - BCMD_SHOW_TARGET0;
				brief.target_on[slot] = 1;
				brief.target_state[slot] = 0;
				brief.target_id[slot] = params[0];
				break;
			}
			case BCMD_CLEAR_TEXT:
				for (i = 0; i < 8; i++)
					brief.text_on[i] = 0;
				brief.text_off = 1;
				break;
			case BCMD_SHOW_TEXT0:
			case BCMD_SHOW_TEXT0 + 1:
			case BCMD_SHOW_TEXT0 + 2:
			case BCMD_SHOW_TEXT0 + 3:
			case BCMD_SHOW_TEXT0 + 4:
			case BCMD_SHOW_TEXT0 + 5:
			case BCMD_SHOW_TEXT0 + 6:
			case BCMD_SHOW_TEXT7: {
				char* locked = (char*)brief.text_data[params[0]];
				if (locked) {
					int16_t len = (int16_t)strlen(locked);
					if (len) {
						soundext_Play_SFX(sfxText, 0);
						soundext_Fade_SFX(sfxText, 0, 4 * len);
					}
				}
				int16_t slot = opcode - BCMD_SHOW_TEXT0;
				brief.text_on[slot] = 1;
				brief.text_state[slot] = 0;
				brief.text_id[slot] = params[0];
				brief.text_x[slot] = params[1];
				brief.text_y[slot] = params[2];
				brief.text_color[slot] = params[3];
				break;
			}
			default:
				break;
		}
	}

	brief.page.index = saved_index;
	brief.page.time++;
}

// FUNCTION: TIE 0x7E85C
void player_Update_Display_Map(int16_t mouse_x, int16_t mouse_y) {
	int16_t min_dist = 999;
	int16_t closest_fg = 0;
	Rect map_rect;
	int16_t scr_x, scr_y;
	int16_t i;

	lrect_Copy_Rect(&map_rect, &map_src_rect);

	for (i = 0; i < fgroup.num_fgs; i++) {
		if (!fgroup.fg[i].way_used[14])
			continue;

		player_Map_To_Screen_Pos(&map_rect, fgroup.fg[i].way_x[14], fgroup.fg[i].way_y[14], &scr_x, &scr_y);

		int16_t dx = mouse_x - scr_x;
		if (abs16(dx) >= min_dist)
			continue;

		int16_t dy = mouse_y - scr_y;
		if (abs16(dy) >= min_dist)
			continue;

		int16_t chebyshev;
		if (abs16(dx) >= abs16(dy))
			chebyshev = dx;
		else
			chebyshev = dy;

		min_dist = abs16(chebyshev);
		closest_fg = i;
	}

	int16_t found = 0;
	if (min_dist != 999) {
		selected_fg_idx = closest_fg;
		found = 1;
	}
	if (found)
		lview_Refresh_View();
}

/* ================================================================
 * Drawing: readout text
 * ================================================================ */

// FUNCTION: TIE 0x7FA14
void player_Draw_Readout_Text(const char* text, int16_t color, int16_t y, int16_t x, int16_t index,
							  int16_t state) {
	if (index < 0)
		return;

	int16_t str_len = (int16_t)strlen(text);
	char str[64];
	strcpy(str, text);

	int16_t base_ramp = 8 * state + 224;

	if (index >= str_len + 2) {
		strcpy(str, text);
		int16_t final_color;
		if (index >= str_len + 5)
			final_color = base_ramp + 4;
		else
			final_color = base_ramp + 9 - (index - str_len);
		lfont_Print_Clipped_Text((const char*)str, y, x, color, final_color);
	} else {
		int16_t char_count = index;
		if (str_len < index)
			char_count = str_len;
		else
			str[index] = 0;

		int16_t ramp_steps = index;
		if (ramp_steps > 3)
			ramp_steps = 3;
		int16_t ramp_color = base_ramp + 6 - 2 * ramp_steps;

		int16_t saved_font = lfont_Get_Font();
		lfont_Set_Font(color);
		int16_t text_width = lfont_Get_String_Width((const char*)str);
		lfont_Set_Font(saved_font);

		while (ramp_color <= base_ramp + 6 && char_count > 0) {
			int16_t loop_color = ramp_color++;
			str[char_count--] = 0;
			lfont_Print_Clipped_Text((const char*)str, y, x, color, loop_color);
		}

		Rect r;
		lrect_Set_Rect(&r, text_width + y + 2, x, text_width + y + 8, x + 6);
		if (index < str_len)
			lpaint_Paint_Clipped_Rect(&r, base_ramp + 7);
	}
}

// FUNCTION: TIE 0x7F9E8
void player_Draw_Double_Readout_Text(const char* text, int16_t color, int16_t screen_x, int16_t screen_y,
									 int16_t text_y, int16_t text_state) {
	if (text_y < 0)
		return;

	int16_t anim_progress = 2 * text_y;
	if (anim_progress < 0)
		return;

	int16_t str_len = (int16_t)strlen(text);
	char str[64];
	strcpy(str, text);

	int16_t base_ramp = 8 * text_state + 224;

	if (anim_progress >= str_len + 2) {
		strcpy(str, text);
		int16_t final_color;
		if (anim_progress >= str_len + 5)
			final_color = base_ramp + 4;
		else
			final_color = base_ramp + 9 - (anim_progress - str_len);
		lfont_Print_Clipped_Text((const char*)str, screen_x, screen_y, color, final_color);
	} else {
		int16_t char_count;
		if (anim_progress > str_len)
			char_count = str_len;
		else {
			char_count = anim_progress;
			str[anim_progress] = 0;
		}

		int16_t ramp_steps = anim_progress;
		if (ramp_steps > 3)
			ramp_steps = 3;
		int16_t ramp_color = base_ramp + 6 - 2 * ramp_steps;

		int16_t saved_font = lfont_Get_Font();
		lfont_Set_Font(color);
		int16_t text_width = lfont_Get_String_Width((const char*)str);
		lfont_Set_Font(saved_font);

		while (ramp_color <= base_ramp + 6 && char_count > 0) {
			int16_t loop_color = ramp_color++;
			str[char_count--] = 0;
			lfont_Print_Clipped_Text((const char*)str, screen_x, screen_y, color, loop_color);
		}

		Rect r;
		lrect_Set_Rect(&r, text_width + screen_x + 2, screen_y, text_width + screen_x + 8, screen_y + 6);
		if (anim_progress < str_len)
			lpaint_Paint_Clipped_Rect(&r, base_ramp + 7);
	}
}

/* ================================================================
 * Drawing: paragraph text
 * ================================================================ */

// FUNCTION: TIE 0x7F624
void player_Draw_Map_Paragraph(Rect* clip, void* handle, int16_t flag) {
	Rect text_rect;
	lrect_Copy_Rect(&text_rect, clip);
	text_rect.bottom = text_rect.top + 10;
	int16_t avail_width = text_rect.right - text_rect.left;

	char* text_data = (char*)handle;
	if (!text_data)
		return;

	char line_buf[128];
	line_buf[0] = 0;
	int16_t char_idx = 0;
	int16_t line_start = -1;
	int16_t line_end_pos = -1;
	int16_t text_width_idx = 0;
	int16_t word_pos = 0;
	int16_t done = 0;

	lfont_Enable_FontID_Shadow(0);

	do {
		int ch = (unsigned char)text_data[char_idx];
		if (ch != '$' && ch != 0) {
			if (ch == ' ')
				word_pos = char_idx;

			/* Accumulate word into line buffer */
			if (isspace(ch)) {
				line_buf[char_idx - text_width_idx] = (char)ch;
				char_idx++;
			} else {
				while (1) {
					int nc = (unsigned char)text_data[char_idx];
					if (isspace(nc) || nc == '$' || nc == 0)
						break;
					if (nc == '[')
						line_buf[char_idx - text_width_idx] = 2;
					else if (nc == ']')
						line_buf[char_idx - text_width_idx] = 1;
					else
						line_buf[char_idx - text_width_idx] = (char)nc;
					char_idx++;
				}
			}
			line_buf[char_idx - text_width_idx] = 0;

			/* Check if line overflows */
			int16_t saved_font = lfont_Get_Font();
			lfont_Set_Font(0);
			int16_t str_width = lfont_Get_String_Width(line_buf);
			lfont_Set_Font(saved_font);

			if (str_width >= avail_width) {
				line_end_pos = text_width_idx;
				line_start = word_pos;
				text_width_idx = word_pos + 1;
				char_idx = word_pos + 1;
			}
		} else {
			/* End of section or end of string */
			line_end_pos = text_width_idx;
			if (!flag && text_data[char_idx] == '$')
				line_start = char_idx - 1;
			else
				line_start = char_idx;
			text_width_idx = ++char_idx;
			if (!text_data[char_idx])
				done = 1;
		}

		/* Emit line if ready */
		if (line_end_pos != -1) {
			/* Build output line, replacing [ ] with color codes */
			char out[128];
			int16_t out_idx = 0;
			int16_t in_bracket = 0;

			/* Check bracket state at line start */
			int16_t scan;
			for (scan = 0; scan < line_end_pos; scan++) {
				if (text_data[scan] == '[')
					in_bracket = 1;
				if (text_data[scan] == ']')
					in_bracket = 0;
			}
			if (in_bracket) {
				out[out_idx++] = 2;
			}

			for (scan = line_end_pos; scan <= line_start; scan++) {
				int sc = (unsigned char)text_data[scan];
				if (sc == '[' || sc == ']') {
					out[out_idx++] = (sc == '[') ? 2 : 1;
				} else {
					out[out_idx++] = (char)sc;
				}
			}
			out[out_idx] = 0;

			/* Render the just-built `out` buffer, not the work buffer
			 * `line_buf`. After an overflow line_buf still holds the
			 * word that triggered it; printing it here reproduces the
			 * "overflow word clipped on this line + repeated at start
			 * of next line" symptom. The binary prints `&v25` (the
			 * unified line buffer it just filled) — that's `out[]`.
			 *
			 * The centered ('>') branch tests the rendered line's
			 * first byte (binary `v25 != 62`); when in_bracket we set
			 * out[0]=2 so this naturally falls through to non-centered.
			 * Pass `out + 1` to skip the '>' just like the binary's
			 * Print_Centered_Text(v26, ...) call (v26 = unified+1). */
			if (!flag && out[0] == '>') {
				lrect_Offset_Rect(&text_rect, 0, 1);
				lfont_Print_Centered_Text(out + 1, &text_rect, 14, 0);
				lrect_Offset_Rect(&text_rect, 0, -1);
			} else {
				lfont_Print_Clipped_Text(out, text_rect.left + 2, text_rect.top + 2, 0, 31);
			}

			line_start = -1;
			line_end_pos = -1;
			lrect_Offset_Rect(&text_rect, 0, 10);
		}
	} while (!done);

	lfont_Disable_FontID_Shadow(0);
}

/* ================================================================
 * Drawing: grid
 * ================================================================ */

// FUNCTION: TIE 0x7EE58
void player_Draw_Display_Grid(Rect* clip) {
	Rect clip_rect;
	lrect_Copy_Rect(&clip_rect, clip);
	lpaint_Frame_Clipped_Rect(&clip_rect, 232);

	int16_t major_color = 234;
	int16_t minor_color = 232;

	/* Compute grid origin from map center */
	int16_t col_idx = map_center_x / 256;
	if (map_center_x > 0 && (map_center_x & 0xFF))
		col_idx++;

	int16_t frac_x = (int16_t)(((uint8_t)(-map_center_x) * map_scale_x) >> 8);
	int16_t start_x = clip_rect.left + ((clip_rect.right - clip_rect.left) >> 1) + frac_x;
	while (start_x > clip_rect.left) {
		col_idx--;
		start_x -= map_scale_x;
	}

	int16_t row_idx = map_center_y / 256;
	if (map_center_y > 0 && (map_center_y & 0xFF))
		row_idx++;

	int16_t frac_y = (int16_t)(((uint8_t)(-map_center_y) * map_scale_y) >> 8);
	int16_t start_y = clip_rect.top + ((clip_rect.bottom - clip_rect.top) >> 1) + frac_y;
	while (start_y > clip_rect.top) {
		row_idx--;
		start_y -= map_scale_y;
	}

	int16_t save_col_idx = col_idx;
	int16_t save_row_idx = row_idx;
	int16_t grid_y = start_y;
	int16_t grid_x = start_x;

	/* Minor grid lines (drawn if scale >= 16) */
	if (map_scale_x >= 16) {
		int16_t show_minor = (map_scale_x >= 32) ? 1 : 0;

		/* Vertical minor lines */
		int16_t x = start_x;
		int16_t ci = col_idx;
		while (x < clip_rect.right) {
			int16_t m = ci & 3;
			if (m != 0 && (m == 2 || show_minor)) {
				lpaint_Vert_Clipped_Line(x, clip_rect.top, clip_rect.bottom - clip_rect.top, minor_color);
				if (brief_poly_used) {
					/* Thickness padding for the classic-FB polygon
					 * minification — rasters into brief_buffer but
					 * stays out of the HD snapshot (source RT
					 * renders crisp 1-classic-px lines that don't
					 * need the trick). */
					lpaint_Set_Thickness_Duplicate(true);
					lpaint_Vert_Clipped_Line(x + 1, clip_rect.top, clip_rect.bottom - clip_rect.top,
											 minor_color);
					lpaint_Vert_Clipped_Line(x + 2, clip_rect.top, clip_rect.bottom - clip_rect.top,
											 minor_color);
					lpaint_Set_Thickness_Duplicate(false);
				}
			}
			x += map_scale_x;
			ci++;
		}

		/* Horizontal minor lines */
		int16_t y = start_y;
		int16_t ri = row_idx;
		while (y < clip_rect.bottom) {
			int16_t m = ri & 3;
			if (m != 0 && (m == 2 || show_minor)) {
				lpaint_Horiz_Clipped_Line(clip_rect.left, y, clip_rect.right - clip_rect.left, minor_color);
				if (brief_poly_used) {
					lpaint_Set_Thickness_Duplicate(true);
					lpaint_Horiz_Clipped_Line(clip_rect.left, y + 1, clip_rect.right - clip_rect.left,
											  minor_color);
					lpaint_Set_Thickness_Duplicate(false);
				}
			}
			y += map_scale_y;
			ri++;
		}

		col_idx = save_col_idx;
		row_idx = save_row_idx;
		grid_y = start_y;
		grid_x = start_x;
	}

	/* Major grid lines (every 4 cells) */
	while (grid_x < clip_rect.right) {
		if ((col_idx & 3) == 0) {
			lpaint_Vert_Clipped_Line(grid_x, clip_rect.top, clip_rect.bottom - clip_rect.top, major_color);
			if (brief_poly_used) {
				lpaint_Set_Thickness_Duplicate(true);
				lpaint_Vert_Clipped_Line(grid_x + 1, clip_rect.top, clip_rect.bottom - clip_rect.top,
										 major_color);
				lpaint_Vert_Clipped_Line(grid_x + 2, clip_rect.top, clip_rect.bottom - clip_rect.top,
										 major_color);
				lpaint_Set_Thickness_Duplicate(false);
			}
		}
		grid_x += map_scale_x;
		col_idx++;
	}

	while (grid_y < clip_rect.bottom) {
		if ((row_idx & 3) == 0) {
			lpaint_Horiz_Clipped_Line(clip_rect.left, grid_y, clip_rect.right - clip_rect.left, major_color);
			if (brief_poly_used) {
				lpaint_Set_Thickness_Duplicate(true);
				lpaint_Horiz_Clipped_Line(clip_rect.left, grid_y + 1, clip_rect.right - clip_rect.left,
										  major_color);
				lpaint_Set_Thickness_Duplicate(false);
			}
		}
		grid_y += map_scale_y;
		row_idx++;
	}
}

/* ================================================================
 * Drawing: zoom target animation
 * ================================================================ */

// FUNCTION: TIE 0x7FBD0
void player_Draw_Map_Zoom(Rect* clip, Rect* dest, int16_t fg_index, int16_t target_id) {
	int16_t species = fgroup.fg[fg_index].species - 1;
	int16_t way_x = fgroup.fg[fg_index].way_x[14];
	int16_t way_y = fgroup.fg[fg_index].way_y[14];
	int16_t side = fgroup.fg[fg_index].side;
	int16_t target_color = side_to_color(side);

	if (map_scale_x < 32)
		species += 88;
	if (species < 0)
		return;

	/* Compute screen position */
	int32_t sx = (int32_t)(way_x - map_center_x) * map_scale_x;
	int16_t screen_x = sdiv256(sx) + clip->left + ((clip->right - clip->left) >> 1);

	int32_t sy = (int32_t)(way_y - map_center_y) * map_scale_y;
	int16_t screen_y = sdiv256(sy) + clip->top + ((clip->bottom - clip->top) >> 1);

	Rect r;
	lrect_Set_Rect(&r, screen_x - 4, screen_y - 4, screen_x + 5, screen_y + 5);

	lactor_Set_Actor_State(icon_actors[0], species, 0);
	int16_t offx, offy;
	lactor_Get_Actor_Offset(icon_actors[0], &offx, &offy);
	screen_x -= offx + (icon_actors[0]->w / 2);
	screen_y -= offy + (icon_actors[0]->h / 2);
	icon_actors[0]->flags |= AF_REMAP_COLOR;

	if (target_id >= 12) {
		lrect_Inset_Rect(&r, -2, -2);
		lpaint_Paint_Clipped_Rect(&r, target_color + 2);
		lpaint_Frame_Clipped_Rect(&r, target_color + 6);
		icon_actors[0]->flags &= ~AF_REMAP_COLOR;
		return;
	}

	int16_t anim_size, anim_count, base_color;
	if (target_id < 4) {
		anim_size = 16;
		anim_count = target_id + 1;
		base_color = target_color + 7 - 2 * target_id;
	} else if (target_id < 8) {
		base_color = target_color + 1;
		anim_size = 16 - 2 * (target_id - 3);
		anim_count = 4;
	} else {
		anim_size = 16 - 2 * (target_id - 3);
		anim_count = 12 - target_id;
		base_color = target_color + 1;
	}

	if (target_id >= 8) {
		lrect_Inset_Rect(&r, 11 - target_id, 11 - target_id);
		lpaint_Paint_Clipped_Rect(&r, target_color + 2);
		lpaint_Frame_Clipped_Rect(&r, target_color + target_id - 6);
	}

	int16_t i;
	for (i = 0; i < anim_count; i++) {
		icon_actors[0]->foreColor = base_color;
		lactanim_Draw_Anim_Actor(icon_actors[0], clip, dest, screen_x - anim_size, screen_y - anim_size, 1);
		lactanim_Draw_Anim_Actor(icon_actors[0], clip, dest, screen_x + anim_size, screen_y - anim_size, 1);
		lactanim_Draw_Anim_Actor(icon_actors[0], clip, dest, screen_x - anim_size, screen_y + anim_size, 1);
		lactanim_Draw_Anim_Actor(icon_actors[0], clip, dest, screen_x + anim_size, screen_y + anim_size, 1);
		anim_size -= 2;
		base_color += 2;
	}

	icon_actors[0]->flags &= ~AF_REMAP_COLOR;
}

/* ================================================================
 * Drawing: ship icons
 * ================================================================ */

void player_Draw_Display_Ship(Rect* clip, Rect* dest) {
	Rect dst;
	lrect_Copy_Rect(&dst, clip);
	int16_t i;

	/* Draw target highlighting for active targets */
	for (i = 0; i < 8; i++) {
		if (!brief.target_on[i])
			continue;

		int16_t fg_idx = brief.target_id[i];
		int16_t way_x = fgroup.fg[fg_idx].way_x[14];
		int16_t way_y = fgroup.fg[fg_idx].way_y[14];
		int16_t species = fgroup.fg[fg_idx].species - 1;
		int16_t side = fgroup.fg[fg_idx].side;
		int16_t target_color = side_to_color(side);

		if (map_scale_x < 32)
			species += 88;
		if (species < 0)
			continue;

		int16_t scr_x, scr_y;
		player_Map_To_Screen_Pos(clip, way_x, way_y, &scr_x, &scr_y);

		Rect r;
		lrect_Set_Rect(&r, scr_x - 4, scr_y - 4, scr_x + 5, scr_y + 5);

		lactor_Set_Actor_State(icon_actors[0], species, 0);
		int16_t offx, offy;
		lactor_Get_Actor_Offset(icon_actors[0], &offx, &offy);
		scr_x -= offx + (icon_actors[0]->w / 2);
		scr_y -= offy + (icon_actors[0]->h / 2);

		icon_actors[0]->flags |= AF_REMAP_COLOR;

		int16_t state = brief.target_state[i];
		if (state >= 12) {
			lrect_Inset_Rect(&r, -2, -2);
			lpaint_Paint_Clipped_Rect(&r, target_color + 2);
			lpaint_Frame_Clipped_Rect(&r, target_color + 6);
		} else {
			int16_t anim_size, anim_count, base_color;
			if (state < 4) {
				base_color = target_color + 7 - 2 * state;
				anim_count = state + 1;
				anim_size = 16;
			} else if (state < 8) {
				base_color = target_color + 1;
				anim_size = 16 - 2 * (state - 3);
				anim_count = 4;
			} else {
				base_color = target_color + 1;
				anim_size = 16 - 2 * (state - 3);
				anim_count = 12 - state;
			}

			if (state >= 8) {
				lrect_Inset_Rect(&r, 11 - state, 11 - state);
				lpaint_Paint_Clipped_Rect(&r, target_color + 2);
				lpaint_Frame_Clipped_Rect(&r, target_color + state - 6);
			}

			int16_t j;
			for (j = 0; j < anim_count; j++) {
				icon_actors[0]->foreColor = base_color;
				lactanim_Draw_Anim_Actor(icon_actors[0], clip, dest, scr_x - anim_size, scr_y - anim_size, 1);
				lactanim_Draw_Anim_Actor(icon_actors[0], clip, dest, scr_x + anim_size, scr_y - anim_size, 1);
				lactanim_Draw_Anim_Actor(icon_actors[0], clip, dest, scr_x - anim_size, scr_y + anim_size, 1);
				lactanim_Draw_Anim_Actor(icon_actors[0], clip, dest, scr_x + anim_size, scr_y + anim_size, 1);
				anim_size -= 2;
				base_color += 2;
			}
		}

		icon_actors[0]->flags &= ~AF_REMAP_COLOR;
	}

	/* Draw text labels for active text overlays */
	for (i = 0; i < 8; i++) {
		if (!brief.text_on[i])
			continue;

		int16_t text_id = brief.text_id[i];
		int16_t tx = (int16_t)(map_scale_x * (brief.text_x[i] - map_center_x) / 256);
		int16_t screen_x = (dst.right - dst.left) / 2 + dst.left + tx;

		int16_t ty = (int16_t)((brief.text_y[i] - map_center_y) * map_scale_y / 256);
		int16_t screen_y = dst.top + (dst.bottom - dst.top) / 2 + ty;

		char* text_ptr = (char*)brief.text_data[text_id];
		char str[43];
		if (text_ptr)
			strcpy(str, text_ptr);
		else
			str[0] = 0;

		/* Replace [ ] with color control codes */
		int16_t m;
		for (m = 0; str[m]; m++) {
			if (str[m] == '[')
				str[m] = 2;
			if (str[m] == ']')
				str[m] = 1;
		}

		int16_t state = brief.text_state[i];
		if (state >= 0)
			player_Draw_Readout_Text(str, 1, screen_x, screen_y, 2 * state, brief.text_color[i]);
	}

	/* Draw all flight group ship icons */
	int16_t fg;
	for (fg = 0; fg < fgroup.num_fgs; fg++) {
		if (!fgroup.fg[fg].way_used[14])
			continue;

		int16_t way_x = fgroup.fg[fg].way_x[14];
		int16_t way_y = fgroup.fg[fg].way_y[14];
		int16_t species = fgroup.fg[fg].species - 1;
		int16_t icon_idx = side_to_icon(fgroup.fg[fg].side);

		if (map_scale_x < 32)
			species += 88;
		if (species < 0)
			continue;

		int32_t sx = (int32_t)(way_x - map_center_x) * map_scale_x;
		int16_t scr_x = sdiv256(sx) + (dst.right - dst.left) / 2 + dst.left;

		int32_t sy = (int32_t)(way_y - map_center_y) * map_scale_y;
		int16_t scr_y = sdiv256(sy) + (dst.bottom - dst.top) / 2 + dst.top;

		lactor_Set_Actor_State(icon_actors[icon_idx], species, 0);
		int16_t offx, offy;
		lactor_Get_Actor_Offset(icon_actors[icon_idx], &offx, &offy);

		scr_x -= offx + (icon_actors[icon_idx]->w / 2);
		scr_y -= offy + (icon_actors[icon_idx]->h / 2);

		lactanim_Draw_Anim_Actor(icon_actors[icon_idx], clip, dest, scr_x, scr_y, 1);
	}
}

/* ================================================================
 * Drawing: full map composite
 * ================================================================ */

void player_Draw_Display_Map(Rect* view_rect, Rect* clip_rect) {
	Rect dst, map_area, draw_clip;

	player_Stars_To_Back(view_rect->top);

	if (!brief_poly_used) {
		/* Top paragraph area */
		lrect_Copy_Rect(&dst, &map_src_rect);
		lrect_Offset_Rect(&dst, view_rect->left, view_rect->top);
		dst.bottom = dst.top + 12;
		if (shellext_Get_Cur_Scene() != SCENE_COMBAT_MAP_A)
			lpaint_Paint_Clipped_Rect(&dst, 1);
		if (brief.para_on[0])
			player_Draw_Map_Paragraph(&dst, brief.para_data[brief.para_id[0]], 0);

		/* Bottom status area */
		Rect src;
		lrect_Copy_Rect(&src, &map_src_rect);
		lrect_Offset_Rect(&src, view_rect->left, view_rect->top);
		src.top = src.bottom - 22;
		if (shellext_Get_Cur_Scene() != SCENE_COMBAT_MAP_A)
			lpaint_Paint_Clipped_Rect(&src, 1);
		if (brief.para_on[1])
			player_Draw_Map_Paragraph(&src, brief.para_data[brief.para_id[1]], 0);

		/* Map area (between top paragraph and bottom status) */
		lrect_Copy_Rect(&map_area, &map_src_rect);
		lrect_Offset_Rect(&map_area, view_rect->left, view_rect->top);
		map_area.top += 12;
		map_area.bottom -= 22;
	} else {
		lrect_Copy_Rect(&map_area, &map_src_rect);
		lrect_Offset_Rect(&map_area, view_rect->left, view_rect->top);
	}

	/* Clip and draw grid + ships */
	lrect_Copy_Rect(&draw_clip, clip_rect);
	lrect_Clip_Rect(&draw_clip, &map_area);
	lcanvas_Set_Drawing_Canvas_Clip(&draw_clip);

	player_Draw_Display_Grid(&map_area);
	player_Draw_Display_Ship(&map_area, &draw_clip);

	/* Draw target zoom animations */
	int16_t i;
	Rect ship_rect;
	lrect_Copy_Rect(&ship_rect, &map_area);

	for (i = 0; i < 8; i++) {
		if (!brief.target_on[i])
			continue;
		int16_t fg_idx = brief.target_id[i];
		int16_t scr_x, scr_y;
		player_Map_To_Screen_Pos(&ship_rect, fgroup.fg[fg_idx].way_x[14], fgroup.fg[fg_idx].way_y[14], &scr_x,
								 &scr_y);
		player_Draw_Map_Zoom(&map_area, &draw_clip, fg_idx, brief.target_state[i]);
	}

	/* Draw text labels */
	for (i = 0; i < 8; i++) {
		if (!brief.text_on[i])
			continue;

		int16_t scr_x, scr_y;
		player_Map_To_Screen_Pos(&ship_rect, brief.text_x[i], brief.text_y[i], &scr_x, &scr_y);

		char text_buf[40];
		char* txt = (char*)brief.text_data[brief.text_id[i]];
		if (txt)
			strcpy(text_buf, txt);
		else
			text_buf[0] = 0;

		int16_t m;
		for (m = 0; text_buf[m]; m++) {
			if (text_buf[m] == '[')
				text_buf[m] = 2;
			if (text_buf[m] == ']')
				text_buf[m] = 1;
		}

		player_Draw_Double_Readout_Text(text_buf, 1, scr_x, scr_y, brief.text_state[i], brief.text_color[i]);
	}

	/* Draw all flight group icons */
	for (i = 0; i < fgroup.num_fgs; i++) {
		if (!fgroup.fg[i].way_used[14])
			continue;

		int16_t species = fgroup.fg[i].species - 1;
		int16_t icon_idx = side_to_icon(fgroup.fg[i].side);

		if (map_scale_x < 32)
			species += 88;
		if (species < 0)
			continue;

		int16_t scr_x, scr_y;
		player_Map_To_Screen_Pos(&ship_rect, fgroup.fg[i].way_x[14], fgroup.fg[i].way_y[14], &scr_x, &scr_y);

		lactor_Set_Actor_State(icon_actors[icon_idx], species, 0);
		int16_t offx, offy;
		lactor_Get_Actor_Offset(icon_actors[icon_idx], &offx, &offy);
		scr_x -= offx + (icon_actors[icon_idx]->w / 2);
		scr_y -= offy + (icon_actors[icon_idx]->h / 2);

		lactanim_Draw_Anim_Actor(icon_actors[icon_idx], &map_area, &draw_clip, scr_x, scr_y, 1);
	}
}

/* ================================================================
 * XINPUT callbacks
 * ================================================================ */

static int16_t iupdate_Map(Input* input, Rect* bounds, Rect* clip, int16_t key, uint8_t left, uint8_t right,
						   int16_t mouse_x, int16_t mouse_y) {
	(void)input;
	(void)left;
	(void)right;
	Rect inset_bounds, clipped_rect, map_rect;
	lrect_Copy_Rect(&inset_bounds, bounds);
	lrect_Inset_Rect(&inset_bounds, 1, 1);
	lrect_Copy_Rect(&clipped_rect, clip);
	lrect_Clip_Rect(&clipped_rect, &inset_bounds);

	if (key)
		return 0;

	lrect_Copy_Rect(&map_rect, &map_src_rect);
	if (player_Find_Ship_On_Screen(&map_rect, mouse_x - 1, mouse_y - 1))
		lview_Refresh_View();

	return 1;
}

static void iuser_Map(Input* input, int32_t time) {
	(void)input;
	(void)time;
	if (!map_playing)
		return;

	/* Animate scale toward target */
	int16_t scale_dx = abs16(map_scale_x - map_scale_target_x);
	int16_t scale_dy = abs16(map_scale_y - map_scale_target_y);
	if (scale_dx < scale_dy)
		scale_dx = scale_dy;

	int16_t scale_speed = 2;
	if (scale_dx >= 12)
		scale_speed = 8;
	if (map_scale_x < 10)
		scale_speed = 1;

	map_scale_x = player_Move_To_Value(map_scale_x, map_scale_target_x, scale_speed);
	map_scale_y = player_Move_To_Value(map_scale_y, map_scale_target_y, scale_speed);

	/* Animate center toward target */
	int16_t pixels_per_unit = map_scale_x ? (256 / map_scale_x + 1) : 1;

	int16_t move_dx = abs16(map_center_x - map_target_x);
	int16_t move_dy = abs16(map_center_y - map_target_y);
	if (move_dx < move_dy)
		move_dx = move_dy;

	int16_t move_speed = 2 * pixels_per_unit;
	if ((int16_t)(move_dx / pixels_per_unit) >= 16)
		move_speed *= 2;

	map_center_x = player_Move_To_Value(map_center_x, map_target_x, move_speed);
	map_center_y = player_Move_To_Value(map_center_y, map_target_y, move_speed);

	/* Advance animation state */
	int16_t i;
	for (i = 0; i < 8; i++) {
		if (brief.target_on[i])
			brief.target_state[i]++;
	}
	for (i = 0; i < 8; i++) {
		if (brief.text_on[i])
			brief.text_state[i]++;
	}

	/* Step or rewind page */
	if (brief.page.len <= brief.page.time)
		player_Rewind_Page();
	else
		player_Step_Page(0);

	TieMapSnapshot_Capture();
}

static void idraw_Map(Input* input, Rect* bounds, Rect* clip, int16_t refresh) {
	(void)refresh;
	Rect draw_clip, map_area, r;

	/* Stamp `start_z` so the merge dispatch knows where to slot the
	 * brief-map quad: at this z, before any widget content the engine
	 * emits below. In rect mode the widget's paint/draw/text records
	 * naturally land just after this slot (target=CUTSCENE), giving
	 * Painter's-algorithm layering: backdrop quad → widget content.
	 * In poly mode the widget's records carry target=BRIEF_SOURCE and
	 * are routed onto the source RT; the rect quad slot still anchors
	 * the polygon-warp quad at the right point in the cutscene-RT
	 * draw order. */
	TieMapHeader* map_h = TieSnapshotBuilder_MapMut();
	if (map_h && map_h->active)
		map_h->start_z = TieSnapshotBuilder_NextEmitZ();

	if (brief_poly_used) {
		/* Bypass the canvas-leak gate that normally suppresses emits
		 * while a non-screen canvas is bound. The engine renders the
		 * brief widget into a 320×200 brief_buffer scratch (so the
		 * classic FB's stub_Map_Clipped_Image warp can read it back),
		 * but the HD snapshot needs those same emits — the application
		 * routes them to the source RT and applies the polygon warp
		 * at composite time. Other scratch-canvas paths (tielogo,
		 * title) leave the override off so they stay suppressed. */
		lcanvas_Set_Render_Allow_Non_Screen(true);
		lcanvas_Push_Canvas(&brief_buffer);
		lrect_Set_Rect(&r, 0, 0, 292, 147);
		lrect_Copy_Rect(&draw_clip, &r);
		lcanvas_Set_Drawing_Canvas_Clip(&draw_clip);
	} else {
		lrect_Copy_Rect(&r, bounds);
		lrect_Inset_Rect(&r, 1, 1);
		lrect_Copy_Rect(&draw_clip, clip);
		lrect_Clip_Rect(&draw_clip, &r);
	}

	player_Stars_To_Back(r.top);

	if (brief_poly_used) {
		lrect_Copy_Rect(&map_area, &map_src_rect);
		lrect_Offset_Rect(&map_area, r.left, r.top);
	} else {
		/* Top paragraph */
		Rect dst;
		lrect_Copy_Rect(&dst, &map_src_rect);
		lrect_Offset_Rect(&dst, r.left, r.top);
		dst.bottom = dst.top + 12;
		if (shellext_Get_Cur_Scene() != SCENE_COMBAT_MAP_A)
			lpaint_Paint_Clipped_Rect(&dst, 1);
		if (brief.para_on[0])
			player_Draw_Map_Paragraph(&dst, brief.para_data[brief.para_id[0]], 0);

		/* Bottom status */
		Rect status_rect;
		lrect_Copy_Rect(&status_rect, &map_src_rect);
		lrect_Offset_Rect(&status_rect, r.left, r.top);
		status_rect.top = status_rect.bottom - 22;
		if (shellext_Get_Cur_Scene() != SCENE_COMBAT_MAP_A)
			lpaint_Paint_Clipped_Rect(&status_rect, 1);
		if (brief.para_on[1])
			player_Draw_Map_Paragraph(&status_rect, brief.para_data[brief.para_id[1]], 0);

		/* Map area */
		lrect_Copy_Rect(&map_area, &map_src_rect);
		lrect_Offset_Rect(&map_area, r.left, r.top);
		map_area.top += 12;
		map_area.bottom -= 22;
	}

	Rect dest;
	lrect_Copy_Rect(&dest, &draw_clip);
	lrect_Clip_Rect(&dest, &map_area);
	lcanvas_Set_Drawing_Canvas_Clip(&dest);

	player_Draw_Display_Grid(&map_area);
	player_Draw_Display_Ship(&map_area, &dest);

	if (brief_poly_used) {
		lcanvas_Pop_Canvas();
		Rect src_rect;
		lrect_Copy_Rect(&src_rect, &map_src_rect);
		lrect_Inset_Rect(&src_rect, 32, 16);
		char* pixels = (char*)lbitmap_Lock_Bitmap(&brief_buffer);
		stub_Map_Clipped_Image(pixels, brief_poly.x, &src_rect, 320, 150);
		lbitmap_Unlock_Bitmap(&brief_buffer);
		/* Restore the canvas-leak gate so subsequent scratch-canvas
		 * emits (tielogo / title backgrounds, etc.) stay suppressed. */
		lcanvas_Set_Render_Allow_Non_Screen(false);
	}

	if (linpattr_Is_Input_Dirty(input))
		ldirty_Dirty_Rect(clip);
}

/* ================================================================
 * Init / Free
 * ================================================================ */

void player_Init_Display_Map(void) {
	int16_t i;

	map_playing = 1;
	selected_fg_idx = 0;
	map_center_x = 0;
	map_center_y = 0;
	map_target_x = 0;
	map_target_y = 0;
	map_scale_x = 16;
	map_scale_y = 16;
	map_scale_target_x = 16;
	map_scale_target_y = 16;

	memset(&fgroup, 0, sizeof(fgroup));
	fgroup.num_fgs = 1;
	brief.page.size = 2;
	fgroup.mission.all_way_shown = 0;
	fgroup.mission.win_type = 1;
	brief.page.commands[0] = 9999;
	brief.page.len = 200;
	brief.page.time = 0;
	brief.page.index = 0;
	brief.page.tile = 0;
	brief.page.commands[1] = BCMD_END_PAGE;
	player_Rewind_Page();

	map_center_x = 0;
	map_center_y = 0;
	map_target_x = 0;
	map_target_y = 0;
	map_scale_x = 16;
	map_scale_y = 16;
	map_scale_target_x = 16;
	map_scale_target_y = 16;

	clear_brief_state();

	brief.page.time = 0;
	brief.page.index = 0;
	player_Step_Page(1);

	/* Allocate text/paragraph/talk data buffers */
	for (i = 0; i < 32; i++)
		brief.text_data[i] = calloc(1, 40);
	for (i = 0; i < 32; i++)
		brief.para_data[i] = calloc(1, 160);
	for (i = 0; i < 20; i++)
		brief.talk_data[i] = calloc(1, 1024);

	clear_brief_state();

	lrect_Set_Rect(&map_src_rect, 0, 0, 292, 147);
}

// FUNCTION: TIE 0x7DD0C
void player_Free_Display_Map(void) {
	int16_t i;
	for (i = 0; i < 32; i++) {
		if (brief.text_data[i]) {
			free(brief.text_data[i]);
			brief.text_data[i] = 0;
		}
	}
	for (i = 0; i < 32; i++) {
		if (brief.para_data[i]) {
			free(brief.para_data[i]);
			brief.para_data[i] = 0;
		}
	}
	for (i = 0; i < 20; i++) {
		if (brief.talk_data[i]) {
			free(brief.talk_data[i]);
			brief.talk_data[i] = 0;
		}
	}
}

// FUNCTION: TIE 0x7DDDC
void player_Load_Display_Map(void) {
	char name[64];
	int16_t version_flag = 0;
	int16_t fg_count = 0, event_count = 0, object_count = 0;
	int16_t read_len;
	uint8_t event_discard[90];
	uint8_t object_discard[28];

	shipext_Get_Mission_Path(name);
	memset(&fgroup, 0, sizeof(fgroup));

	TieFile* fp = TieStorage_Open(TIE_FILE_ROOT_FLIGHT_ASSET, name, "rb");
	if (!fp)
		return;

	TieStorage_Read(&version_flag, 2, 1, fp);
	if (version_flag < 0) {
		TieStorage_Read(&fg_count, 2, 1, fp);
	} else {
		fg_count = version_flag;
		version_flag = 0;
	}

	TieStorage_Read(&event_count, 2, 1, fp);
	TieStorage_Read(&object_count, 2, 1, fp);
	fgroup.num_fgs = fg_count;
	uint8_t mis_buf[EMISSIONSTRUCT_DISK_SIZE];
	TieStorage_Read(mis_buf, EMISSIONSTRUCT_DISK_SIZE, 1, fp);
	EMissionStruct_decode(&fgroup.mission, mis_buf);
	shipext_Set_Mission_Ship(0);

	int16_t i;
	uint8_t fg_buf[EFGSTRUCT_DISK_SIZE];
	for (i = 0; i < fgroup.num_fgs; i++) {
		TieStorage_Read(fg_buf, EFGSTRUCT_DISK_SIZE, 1, fp);
		EFGStruct_decode(&fgroup.fg[i], fg_buf);
		if (fgroup.fg[i].player_flag) {
			int16_t ms = -1;
			switch (fgroup.fg[i].species) {
				case 5:
					ms = 0;
					break;
				case 6:
					ms = 1;
					break;
				case 7:
					ms = 2;
					break;
				case 8:
					ms = 3;
					break;
				case 9:
					ms = 5;
					break;
				case 12:
					ms = 6;
					break;
				case 16:
					ms = 4;
					break;
				case 10:
				case 11:
				case 13:
					ms = fgroup.fg[i].species - 3;
					break;
				default:
					continue;
			}
			if (ms >= 0)
				shipext_Set_Mission_Ship(ms);
		}
	}

	/* Skip events and objects */
	for (i = 0; i < event_count; i++)
		TieStorage_Read(event_discard, 90, 1, fp);
	for (i = 0; i < object_count; i++)
		TieStorage_Read(object_discard, 28, 1, fp);

	/* Read briefing page commands */
	uint8_t page_buf[EBRIEFPAGE_DISK_SIZE];
	TieStorage_Read(page_buf, EBRIEFPAGE_DISK_SIZE, 1, fp);
	TieRecoveredBrief_DecodePage(&brief.page, page_buf);

	/* Read text strings */
	for (i = 0; i < 32; i++) {
		char* buf = (char*)(uintptr_t)brief.text_data[i];
		if (version_flag)
			TieStorage_Read(&read_len, 2, 1, fp);
		else
			read_len = 40;
		if (read_len && buf)
			TieStorage_Read(buf, read_len, 1, fp);
		if (version_flag && buf)
			buf[read_len] = 0;
	}

	/* Read paragraph strings */
	for (i = 0; i < 32; i++) {
		char* buf = (char*)(uintptr_t)brief.para_data[i];
		if (version_flag)
			TieStorage_Read(&read_len, 2, 1, fp);
		else
			read_len = 160;
		if (read_len && buf)
			TieStorage_Read(buf, read_len, 1, fp);
		if (version_flag && buf)
			buf[read_len] = 0;
	}

	/* Read talk strings */
	for (i = 0; i < 20; i++) {
		char* buf = (char*)(uintptr_t)brief.talk_data[i];
		if (version_flag)
			TieStorage_Read(&read_len, 2, 1, fp);
		else
			read_len = 0;
		if (read_len && buf)
			TieStorage_Read(buf, read_len, 1, fp);
		if (buf)
			buf[read_len] = 0;
	}

	TieStorage_Close(fp);
}

// FUNCTION: TIE95 0x7D594; TIE98 0x469580
void player_Init_Brief_Display(Input* input, void* poly) {
	int16_t i;

	map_playing = 1;
	selected_fg_idx = 0;
	map_center_x = 0;
	map_center_y = 0;
	map_target_x = 0;
	map_target_y = 0;
	map_scale_x = 16;
	map_scale_y = 16;
	map_scale_target_x = 16;
	map_scale_target_y = 16;

	/* Snapshot emitter: cache the widget pointer — its frame is the
	 * dst-rect anchor for non-polygon scenes. */
	map_emit_widget = input;

	memset(&fgroup, 0, sizeof(fgroup));
	fgroup.num_fgs = 1;
	fgroup.mission.all_way_shown = 0;
	fgroup.mission.win_type = 1;
	player_Clear_Page_Commands();
	player_Rewind_Page();

	/* Allocate data buffers */
	for (i = 0; i < 32; i++)
		brief.text_data[i] = calloc(1, 40);
	for (i = 0; i < 32; i++)
		brief.para_data[i] = calloc(1, 160);
	for (i = 0; i < 20; i++)
		brief.talk_data[i] = calloc(1, 1024);

	clear_brief_state();
	lrect_Set_Rect(&map_src_rect, 0, 0, 292, 147);

	/* Set up polygon projection if provided */
	if (poly) {
		lrect_Copy_Poly(&brief_poly, (Poly*)poly);
		brief_poly_used = 1;
	} else {
		brief_poly_used = 0;
	}

	/* Load icon actors from player.lfd */
	ResFile* player_res = shellext_Open_Empire_Resource("player.lfd");
	Rect r;
	lrect_Set_Rect(&r, 0, 0, draw_bm_gbl->w, draw_bm_gbl->h);

	icon_actors[0] = lactanim_Res_Anim_Actor("iconsgrn", &r, 0, 0, 0);
	lactor_Set_Actor_Time(icon_actors[0], 0, 0);
	lactor_Non_Dirty_Actor(icon_actors[0]);

	icon_actors[1] = lactanim_Res_Anim_Actor("iconsred", &r, 0, 0, 0);
	lactor_Set_Actor_Time(icon_actors[1], 0, 0);
	lactor_Non_Dirty_Actor(icon_actors[1]);

	icon_actors[2] = lactanim_Res_Anim_Actor("iconsblu", &r, 0, 0, 0);
	lactor_Set_Actor_Time(icon_actors[2], 0, 0);
	lactor_Non_Dirty_Actor(icon_actors[2]);

	icon_actors[3] = lactanim_Res_Anim_Actor("iconspur", &r, 0, 0, 0);
	lactor_Set_Actor_Time(icon_actors[3], 0, 0);
	lactor_Non_Dirty_Actor(icon_actors[3]);

	/* Load star background */
	lrect_Set_Rect(&r, 0, 0, 320, 150);
	if (shellext_Get_Cur_Scene() == SCENE_COMBAT_MAP_A || shellext_Get_Cur_Scene() == SCENE_COMBAT_MAP_B) {
		ResFile* combat_res = shellext_Open_Empire_Resource("map.lfd");
		stars_actor = lactdelt_Res_Delta_Actor("cmbtmap2", &r, 0, 0, 0);
		lres_Close_Resource(combat_res);
	} else {
		stars_actor = lactdelt_Res_Delta_Actor("stars", &r, 0, 0, 0);
	}
	lactor_Set_Actor_Time(stars_actor, 0, 0);

	if (brief_poly_used) {
		lbitmap_Init_Bitmap(&brief_buffer);
		lbitmap_Alloc_Bitmap(&brief_buffer, 320, 200);
	}

	/* Allocate star buffer and render star background into it */
	star_buffer_data = malloc(48000);

	if (shellext_Get_Cur_Scene() == SCENE_COMBAT_MAP_A)
		stars_actor->var1 = 17;
	else
		stars_actor->var1 = 16;

	/* Render star actor into buffer */
	{
		Rect src_rect;
		lrect_Set_Rect(&src_rect, 0, 0, 320, 150);
		if (stars_actor->draw) {
			lpaint_Paint_Clipped_Rect(&src_rect, stars_actor->var1);
			stars_actor->draw(stars_actor, &src_rect, &src_rect, stars_actor->x, stars_actor->y, 1);
		}
		stub_Copy_To_Clipped_Buffer(star_buffer_data, &src_rect, 0, 0, 320, 150);
	}

	lres_Close_Resource(player_res);

	/* Set up XINPUT callbacks */
	if (!brief_poly_used) {
		linpattr_Set_Input_Update_Function(input, iupdate_Map);
		linpattr_Set_Input_User_Function(input, iuser_Map);
		input->mouseUsage = downMoveUpInput;
	}
	linpattr_Set_Input_Draw_Function(input, idraw_Map);

	/* Load mission data */
	player_Load_Display_Map();

	map_center_x = 0;
	map_center_y = 0;
	map_target_x = 0;
	map_target_y = 0;
	map_scale_x = 16;
	map_scale_y = 16;
	map_scale_target_x = 16;
	map_scale_target_y = 16;

	clear_brief_state();

	brief.page.time = 0;
	brief.page.index = 0;
	player_Step_Page(1);

	/* Set up weapon state from player's flight group */
	mission.beam_used = 0;
	mission.torp_used = 0;

	int16_t player_fg;
	for (player_fg = 0; player_fg < fgroup.num_fgs; player_fg++) {
		if (fgroup.fg[player_fg].player_flag) {
			mission.torp_used = fgroup.fg[player_fg].warhead;
			mission.beam_used = fgroup.fg[player_fg].beam;
			beam_level = mission.beam_used;

			uint8_t battle = shipext_Get_Tour_Battle();
			if (battle <= 3)
				weapon_level = 4;
			else
				weapon_level = 6;

			if (mission.torp_used > weapon_level) {
				if (mission.torp_used >= 6)
					weapon_level = mission.torp_used;
				else
					weapon_level = 6;
			}
			break;
		}
	}
}

// FUNCTION: TIE 0x7DB7C
void player_Init_Brief_For_Talk(void) {
	int16_t i;

	map_playing = 1;
	selected_fg_idx = 0;
	map_center_x = 0;
	map_center_y = 0;
	map_target_x = 0;
	map_target_y = 0;
	map_scale_x = 16;
	map_scale_y = 16;
	map_scale_target_x = 16;
	map_scale_target_y = 16;

	memset(&fgroup, 0, sizeof(fgroup));
	fgroup.num_fgs = 1;
	fgroup.mission.all_way_shown = 0;
	fgroup.mission.win_type = 1;
	player_Clear_Page_Commands();
	player_Rewind_Page();

	for (i = 0; i < 32; i++)
		brief.text_data[i] = calloc(1, 40);
	for (i = 0; i < 32; i++)
		brief.para_data[i] = calloc(1, 160);
	for (i = 0; i < 20; i++)
		brief.talk_data[i] = calloc(1, 1024);

	clear_brief_state();
	lrect_Set_Rect(&map_src_rect, 0, 0, 292, 147);
	player_Load_Display_Map();
}

// FUNCTION: TIE 0x7D8C0
void player_Free_Brief_Display(void) {
	int16_t i;

	/* Snapshot emitter: drop the widget pointer so a stray
	 * post-teardown emit produces active=0 instead of dereferencing
	 * freed Input state. */
	map_emit_widget = NULL;

	free(star_buffer_data);
	star_buffer_data = NULL;

	if (brief_poly_used)
		lbitmap_Free_Bitmap(&brief_buffer);

	for (i = 0; i < 32; i++) {
		if (brief.text_data[i]) {
			free(brief.text_data[i]);
			brief.text_data[i] = 0;
		}
	}
	for (i = 0; i < 32; i++) {
		if (brief.para_data[i]) {
			free(brief.para_data[i]);
			brief.para_data[i] = 0;
		}
	}
	for (i = 0; i < 20; i++) {
		if (brief.talk_data[i]) {
			free(brief.talk_data[i]);
			brief.talk_data[i] = 0;
		}
	}
}

bool TieRecoveredMap_ReadSnapshotView(TieRecoveredMapSnapshotView* out) {
	if (!out)
		return false;
	memset(out, 0, sizeof *out);
	if (!map_emit_widget || !map_playing)
		return true;

	const int16_t scene = shellext_Get_Cur_Scene();
	out->active = true;
	out->background_kind = scene == SCENE_COMBAT_MAP_A   ? TIE_MAP_BG_COMBAT
						   : scene == SCENE_COMBAT_MAP_B ? TIE_MAP_BG_COMBAT_DEBRIEF
						   : scene == SCENE_TRAIN_MAP    ? TIE_MAP_BG_TRAINING
														 : TIE_MAP_BG_STARS;
	out->has_polygon = brief_poly_used != 0;
	out->source_width = (int16_t)(map_src_rect.right - map_src_rect.left);
	out->source_height = (int16_t)(map_src_rect.bottom - map_src_rect.top);
	if (out->has_polygon) {
		for (int index = 0; index < 4; ++index) {
			out->polygon_x[index] = brief_poly.x[index];
			out->polygon_y[index] = brief_poly.y[index];
		}
	} else {
		Rect frame = map_emit_widget->frame;
		lrect_Inset_Rect(&frame, 1, 1);
		out->destination_x = frame.left;
		out->destination_y = frame.top;
	}
	out->scene_time = brief.page.time;
	return true;
}
