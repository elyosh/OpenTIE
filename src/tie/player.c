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
#ifdef TIE_MODERN
#include "tie_runtime/snapshot/capture_views.h"
#include "tie_runtime/snapshot/snapshot.h"
#include "tie_runtime/snapshot/snapshot_internal.h"
#include "tie_runtime/snapshot/snapshot_map.h"
#include "tie_runtime/storage/mission_records.h"
#endif
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

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

/* The original TU calls the library string routines rather than the inline forms. */
#ifdef __WATCOMC__
#pragma function(strcpy)
#pragma function(strlen)
#pragma function(abs)
#endif

/* ---- Page command opcodes and parameter counts ---- */

/* Parameter count per opcode (indexed by BriefCmd) */
// GLOBAL: TIE95 0xD1484
// GLOBAL: TIE98 0x4E9490
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
// GLOBAL: TIE95 0xF6E68
// GLOBAL: TIE98 0x584DC0
static Poly brief_poly;
// GLOBAL: TIE95 0xFABC2
// GLOBAL: TIE98 0x584DB8
static int16_t brief_poly_used;

/* Icon actors: [0]=green(enemy), [1]=red, [2]=blue(friendly), [3]=purple(neutral) */
// GLOBAL: TIE95 0xF6E78
// GLOBAL: TIE98 0x588698
static Actor* icon_actors[4];

/* Brief buffer bitmap for polygon projection */
// GLOBAL: TIE95 0xF6E88
// GLOBAL: TIE98 0x588678
static BitmapStruct brief_buffer;

/* Flight group data + briefing state */
// GLOBAL: TIE95 0xF6EC0
// GLOBAL: TIE98 0x584DF0
static EFArrayStruct fgroup;
// GLOBAL: TIE95 0xFA744
// GLOBAL: TIE98 0x5886A8
static EBriefStruct brief;

typedef struct PlayerMapState {
	int16_t playing;
	int16_t selected_fg_idx;
	int16_t center_x, center_y;
	int16_t target_x, target_y;
	int16_t scale_x, scale_y;
	int16_t scale_target_x, scale_target_y;
	Rect source_rect;
} PlayerMapState;

// GLOBAL: TIE95 0xF6EA0
// GLOBAL: TIE98 0x584DD0
static PlayerMapState map_state;

/* Star background */
// GLOBAL: TIE95 0xF6EBC
// GLOBAL: TIE98 0x584DEC
static Actor* stars_actor;
// GLOBAL: TIE95 0xFABC4
// GLOBAL: TIE98 0x588B24
static LandruHandle star_buffer; /* 320x150 pixel cache */

/* Weapon state */
// GLOBAL: TIE95 0xFABC0
static int16_t beam_level;
// GLOBAL: TIE95 0xFABC6
static int16_t weapon_level;

/* ---- Forward declarations for XINPUT callbacks ---- */
static int16_t player_iupdate_Map(Input* inp, Rect* bounds, Rect* clip, int16_t key, uint8_t left,
								  uint8_t right, int16_t mouse_x, int16_t mouse_y);
static void player_iuser_Map(Input* inp, int32_t time);
static void player_idraw_Map(Input* inp, Rect* bounds, Rect* clip, int16_t refresh);

/* ================================================================
 * Weapon selectors
 * ================================================================ */

// FUNCTION: TIE95 0x7D8EC
int16_t player_Get_Beam_Used(void) { return mission.beam_used; }

// FUNCTION: TIE95 0x7D8F4
void player_Next_Beam(void) {
	if (++mission.beam_used > beam_level)
		mission.beam_used = 1;
}

// FUNCTION: TIE95 0x7D918
void player_Last_Beam(void) {
	mission.beam_used--;
	if (mission.beam_used < 1)
		mission.beam_used = beam_level;
}

// FUNCTION: TIE95 0x7D938
int16_t player_Get_Torp_Used(void) { return mission.torp_used; }

// FUNCTION: TIE95 0x7D940
void player_Next_Torp(void) {
	if (++mission.torp_used > weapon_level)
		mission.torp_used = 1;
}

// FUNCTION: TIE95 0x7D964
void player_Last_Torp(void) {
	mission.torp_used--;
	if (mission.torp_used < 1)
		mission.torp_used = weapon_level;
}

/* ================================================================
 * Data accessors
 * ================================================================ */

// FUNCTION: TIE95 0x7D8B0
// FUNCTION: TIE98 0x469930
EBriefStruct* player_Fetch_Brief(void) { return &brief; }

// FUNCTION: TIE95 0x7D8B8
EFArrayStruct* player_Fetch_FGroup(void) { return &fgroup; }

// FUNCTION: TIE95 0x7D99C
// FUNCTION: TIE98 0x469A40
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

// FUNCTION: TIE95 0x7E838
int16_t player_Is_Map_Playing(void) { return map_state.playing; }

// FUNCTION: TIE95 0x7E840
int player_Toggle_Map_Play(void) {
	map_state.playing = (map_state.playing == 0) ? 1 : 0;
	return 1;
}

/* ================================================================
 * Move_To_Value — step current toward target
 * ================================================================ */

// FUNCTION: TIE95 0x7EAA4
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

// FUNCTION: TIE95 0x7E990
// FUNCTION: TIE98 0x46A8D0
void player_Map_To_Screen_Pos(Rect* view_rect, int16_t map_x, int16_t map_y, int16_t* out_x, int16_t* out_y) {
	*out_x = (map_x - map_state.center_x) * map_state.scale_x / 256;
	*out_x += view_rect->left + ((view_rect->right - view_rect->left) >> 1);
	*out_y = (map_y - map_state.center_y) * map_state.scale_y / 256;
	*out_y += view_rect->top + ((view_rect->bottom - view_rect->top) >> 1);
}

// FUNCTION: TIE95 0x7EA20
// FUNCTION: TIE98 0x46A960
void player_Screen_To_Map_Pos(Rect* view_rect, int16_t screen_x, int16_t screen_y, int16_t* out_x,
							  int16_t* out_y) {
	/* Retail narrows the pixel displacement before scaling it. */
	*out_x =
		map_state.center_x +
		(int16_t)(256 * (int16_t)(screen_x - ((view_rect->right - view_rect->left) >> 1) - view_rect->left) /
				  map_state.scale_x);
	*out_y =
		map_state.center_y +
		(int16_t)(256 * (int16_t)(screen_y - ((view_rect->bottom - view_rect->top) >> 1) - view_rect->top) /
				  map_state.scale_y);
}

// FUNCTION: TIE95 0x7E85C
// FUNCTION: TIE98 0x46A800
int16_t player_Find_Ship_On_Screen(Rect* bounds, int16_t screen_x, int16_t screen_y) {
	int16_t min_dist = 999;
	int16_t found_fg = 0;
	int16_t i;
	int16_t scr_x, scr_y;

	for (i = 0; i < fgroup.num_fgs; i++) {
		if (!fgroup.fg[i].way_used[14])
			continue;
		player_Map_To_Screen_Pos(bounds, fgroup.fg[i].way_x[14], fgroup.fg[i].way_y[14], &scr_x, &scr_y);
		if (abs(screen_x - scr_x) >= min_dist)
			continue;
		if (abs(screen_y - scr_y) >= min_dist)
			continue;
		if (abs(screen_x - scr_x) >= abs(screen_y - scr_y))
			min_dist = abs(screen_x - scr_x);
		else
			min_dist = abs(screen_y - scr_y);
		found_fg = i;
	}
	if (min_dist == 999)
		return 0;
	map_state.selected_fg_idx = found_fg;
	return 1;
}

/* ================================================================
 * Actor / star buffer helpers
 * ================================================================ */

// FUNCTION: TIE95 0x7F55C
// FUNCTION: TIE98 0x46B3E0
void player_Actor_To_Buffer(Actor* actor, LandruHandle buffer) {
	Rect r;
	void* pixels;
	xrect_Set_Rect(&r, 0, 0, 320, 150);
	if (actor->draw) {
		xpaint_Paint_Clipped_Rect(&r, actor->var1);
		actor->draw(actor, &r, &r, actor->x, actor->y, 1);
	}
	pixels = xmemhdl_Lock_Handle(buffer);
	stub_Copy_To_Clipped_Buffer(pixels, &r, 0, 0, 320, 150);
	xmemhdl_Unlock_Handle(buffer);
}

// FUNCTION: TIE95 0x7F5D0
// FUNCTION: TIE98 0x46B460
void player_Stars_To_Back(int16_t screen_y) {
	if (star_buffer) {
		Rect r;
		void* pixels;
		xrect_Set_Rect(&r, 0, 0, 320, 150);
		pixels = xmemhdl_Lock_Handle(star_buffer);
		stub_Copy_From_Clipped_Buffer(pixels, &r, 0, screen_y, 320, 150);
		xmemhdl_Unlock_Handle(star_buffer);
	}
}

/* ================================================================
 * Page command processing
 * ================================================================ */

/* Process a single page command step. `flag` nonzero means skip SFX and
 * snap move/zoom instantly. Used for initial seek and rewind. */
// FUNCTION: TIE95 0x7E460
void player_Step_Page(int16_t flag) {
	int16_t cmd_index = brief.page.index;
	int16_t cmd_time = brief.page.commands[cmd_index];
	int16_t saved_index = cmd_index;
	int16_t params[8];
	int16_t sfx_volume;
	int16_t page_time;

	brief.para_off = 0;
	brief.target_off = 0;
	brief.text_off = 0;
	brief.move_on = 0;
	brief.scale_on = 0;
	brief.seek_on = 0;
	if (brief_poly_used)
		sfx_volume = 48;
	else
		sfx_volume = 70;

	for (;;) {
		int16_t opcode;
		int16_t j;

		page_time = brief.page.time;
		if (cmd_time > page_time) {
			brief.page.index = saved_index;
			brief.page.time = page_time + 1;
			return;
		}

		saved_index = cmd_index;
		cmd_time = brief.page.commands[cmd_index++];
		opcode = brief.page.commands[cmd_index++];

		for (j = 0; j < map_cmd_size[opcode]; j++)
			params[j] = brief.page.commands[cmd_index++];

		if (cmd_time == brief.page.time) {
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
					if (cmd_time == 0 || flag) {
						map_state.center_x = params[0];
						map_state.target_x = params[0];
						map_state.target_y = params[1];
						map_state.center_y = params[1];
					} else {
						map_state.target_x = params[0];
						map_state.target_y = params[1];
					}
					brief.move_on = 1;
					break;
				case BCMD_ZOOM:
					if (cmd_time == 0 || flag) {
						map_state.scale_x = params[0];
						map_state.scale_target_x = params[0];
						map_state.scale_target_y = params[1];
						map_state.scale_y = params[1];
					} else {
						map_state.scale_target_x = params[0];
						map_state.scale_target_y = params[1];
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
					int16_t slot;

					if (!flag) {
						int16_t side = fgroup.fg[params[0]].side;
						if (side > 2)
							side = 2;
						if (side == 1)
							soundext_Play_SFX(sfxTarget2, sfx_volume);
						else
							soundext_Play_SFX(sfxTarget1, sfx_volume);
					}
					slot = opcode - BCMD_SHOW_TARGET0;
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
					int16_t slot;

					if (!flag) {
						char text_buf[40];
						int16_t text_len;

						strcpy(text_buf, (char*)xmemhdl_Lock_Handle(brief.text_data[params[0]]));
						xmemhdl_Unlock_Handle(brief.text_data[params[0]]);
						text_len = (int16_t)strlen(text_buf);
						if (text_len) {
							soundext_Play_SFX(sfxText, 0);
							soundext_Fade_SFX(sfxText, 0, 4 * text_len);
						}
					}
					slot = opcode - BCMD_SHOW_TEXT0;
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
	}
}

// FUNCTION: TIE95 0x7E18C
// FUNCTION: TIE98 0x46A160
void player_Clear_Page_Commands(void) {
	brief.page.len = 200;
	brief.page.time = 0;
	brief.page.index = 0;
	brief.page.size = 2;
	brief.page.tile = 0;
	brief.page.commands[0] = 9999;
	brief.page.commands[1] = BCMD_END_PAGE;
	player_Rewind_Page();
}

// FUNCTION: TIE95 0x7E1E0
// FUNCTION: TIE98 0x46A1A0
void player_Rewind_Page(void) {
	int16_t i;

	map_state.center_x = 0;
	map_state.center_y = 0;
	map_state.target_x = 0;
	map_state.target_y = 0;
	map_state.scale_x = 16;
	map_state.scale_y = 16;
	map_state.scale_target_x = 16;
	map_state.scale_target_y = 16;

	for (i = 0; i < 2; ++i)
		brief.para_on[i] = 0;
	for (i = 0; i < 8; ++i)
		brief.target_on[i] = 0;
	for (i = 0; i < 8; ++i)
		brief.text_on[i] = 0;

	brief.page.time = 0;
	brief.page.index = 0;
	player_Step_Page(1);
}

// FUNCTION: TIE95 0x7E810
// FUNCTION: TIE98 0x46A7A0
int16_t player_Reseek_Page(void) {
	int16_t time = brief.page.time;
	if (!time)
		time = 1;
	player_Rewind_Page();
	return player_Seek_Page((int16_t)(time - 1), 1);
}

// FUNCTION: TIE95 0x7E29C
// FUNCTION: TIE98 0x46A230
int16_t player_Seek_Page(int16_t time, int16_t flag) {
	if (time == brief.page.time - 1)
		return 0;
	if (time < brief.page.time)
		player_Rewind_Page();
	while (time >= brief.page.time)
		player_Step_Page(flag);
	return 1;
}

// FUNCTION: TIE95 0x7E2E0
// FUNCTION: TIE98 0x46A280
void player_Seek_Page_Section(void) {
	int16_t start_time = brief.page.time;
	int16_t section_done = 0;
	int16_t has_para = 0;
	int16_t para_count = 0;
	int16_t i;

	int16_t next_opcode;
	int16_t time_val;
	int16_t next_cmd;

	player_Rewind_Page();

	for (next_opcode = 0; !section_done;) {
		if (next_opcode == BCMD_END_PAGE)
			break;

		next_opcode = brief.page.commands[brief.page.index + 1];

		if (brief.para_off) {
			para_count = 0;
			has_para = 0;
		}
		for (i = 0; i < 2; i++) {
			if (brief.para_on[i])
				has_para = 1;
		}
		if (has_para)
			para_count++;

		if ((brief.scale_on || brief.move_on || brief.seek_on || para_count == 1) &&
			brief.page.time >= start_time) {
			section_done = 1;
		} else {
			player_Step_Page(1);
		}
	}

	/* Determine the time to seek to */

	if (brief.seek_on || para_count == 1) {
		time_val = brief.page.time;
		next_cmd = 0;
	} else {
		time_val = brief.page.commands[brief.page.index];
		next_cmd = brief.page.commands[brief.page.index + 1];
	}

	if (next_cmd == BCMD_END_PAGE)
		player_Rewind_Page();
	else
		player_Seek_Page(time_val, 0);
}

/* ================================================================
 * Map display movement / animation
 * ================================================================ */

// FUNCTION: TIE95 0x7EAC4
// FUNCTION: TIE98 0x46AA10
void player_Move_Display_Map(void) {
	int16_t scale_dx;
	int16_t scale_dy;
	int16_t scale_speed;
	int16_t move_dx;
	int16_t move_dy;
	int16_t move_speed;
	int16_t i;

	/* Compute scale step speed */
	scale_dx = (int16_t)abs(map_state.scale_x - map_state.scale_target_x);
	scale_dy = (int16_t)abs(map_state.scale_y - map_state.scale_target_y);

	if (scale_dx < scale_dy)
		scale_dx = scale_dy;

	scale_speed = 2;
	if (scale_dx >= 12)
		scale_speed = 8;
	if (map_state.scale_x < 10)
		scale_speed = 1;

	map_state.scale_x = player_Move_To_Value(map_state.scale_x, map_state.scale_target_x, scale_speed);
	map_state.scale_y = player_Move_To_Value(map_state.scale_y, map_state.scale_target_y, scale_speed);

	/* Compute move step speed */
	if (map_state.scale_x)
		move_speed = 256 / map_state.scale_x + 1;
	else
		move_speed = 1;

	move_dx = (int16_t)abs(map_state.center_x - map_state.target_x);
	move_dy = (int16_t)abs(map_state.center_y - map_state.target_y);
	if (move_dx < move_dy)
		move_dx = move_dy;

	move_dx /= move_speed;
	move_speed *= 2;
	if (move_dx >= 16)
		move_speed *= 2;

	map_state.center_x = player_Move_To_Value(map_state.center_x, map_state.target_x, move_speed);
	map_state.center_y = player_Move_To_Value(map_state.center_y, map_state.target_y, move_speed);

	/* Advance target/text animation state */

	for (i = 0; i < 8; i++) {
		if (brief.target_on[i])
			brief.target_state[i]++;
	}
	for (i = 0; i < 8; i++) {
		if (brief.text_on[i])
			brief.text_state[i]++;
	}
}

// FUNCTION: TIE95 0x7DA58
// FUNCTION: TIE98 0x46AB70
void player_Step_Display_Map(void) {
	if (brief.page.len <= brief.page.time)
		player_Rewind_Page();
	else
		player_Step_Page(0);
}

// FUNCTION: TIE95 0x7EC84
// FUNCTION: TIE98 0x46AB90
int16_t player_Update_Display_Map(Rect* bounds, Rect* clip, uint8_t left, uint8_t right, int16_t mouse_x,
								  int16_t mouse_y) {
	Rect map_rect;
	(void)bounds;
	(void)clip;
	(void)left;
	(void)right;
	xrect_Copy_Rect(&map_rect, &map_state.source_rect);
	if (player_Find_Ship_On_Screen(&map_rect, mouse_x, mouse_y))
		xview_Refresh_View();
	return 1;
}

/* ================================================================
 * Drawing: readout text
 * ================================================================ */

// FUNCTION: TIE95 0x7FA14
void player_Draw_Readout_Text(const char* text, int16_t font, int16_t screen_x, int16_t screen_y,
							  int16_t index, int16_t state) {
	int16_t str_len;
	int16_t base_ramp;

	char str[64];

	if (index < 0)
		return;

	str_len = (int16_t)strlen(text);

	strcpy(str, text);

	base_ramp = 8 * state + 224;

	if (index >= str_len + 2) {
		int16_t final_color;

		strcpy(str, text);

		if (index >= str_len + 5)
			final_color = base_ramp + 4;
		else
			final_color = base_ramp + 9 - (index - str_len);
		xfont_Print_Clipped_Text((const char*)str, screen_x, screen_y, font, final_color);
	} else {
		int16_t char_count = index;
		int16_t ramp_steps;
		int16_t ramp_color;
		int16_t saved_font;
		int16_t text_width;
		Rect r;

		if (str_len < index)
			char_count = str_len;
		else
			str[index] = 0;

		ramp_steps = index;
		if (ramp_steps > 3)
			ramp_steps = 3;
		ramp_color = base_ramp + 6 - 2 * ramp_steps;

		saved_font = xfont_Get_Font();
		xfont_Set_Font(font);
		text_width = xfont_Get_String_Width((const char*)str);
		xfont_Set_Font(saved_font);

		while (ramp_color <= base_ramp + 6 && char_count > 0) {
			int16_t loop_color = ramp_color++;
			str[char_count--] = 0;
			xfont_Print_Clipped_Text((const char*)str, screen_x, screen_y, font, loop_color);
		}

		xrect_Set_Rect(&r, text_width + screen_x + 2, screen_y, text_width + screen_x + 8, screen_y + 6);
		if (index < str_len)
			xpaint_Paint_Clipped_Rect(&r, base_ramp + 7);
	}
}

// FUNCTION: TIE95 0x7F9E8
// FUNCTION: TIE98 0x46B7F0
void player_Draw_Double_Readout_Text(const char* text, int16_t font, int16_t screen_x, int16_t screen_y,
									 int16_t index, int16_t state) {
	if (index >= 0)
		player_Draw_Readout_Text(text, font, screen_x, screen_y, (int16_t)(2 * index), state);
}

/* ================================================================
 * Drawing: paragraph text
 * ================================================================ */

// FUNCTION: TIE95 0x7F624
void player_Draw_Map_Paragraph(Rect* clip, LandruHandle handle, int16_t flag) {
	Rect text_rect;
	int16_t avail_width;
	char* text_data;
	int16_t char_idx;
	int16_t line_start;
	int16_t line_end_pos;
	int16_t text_width_idx;
	int16_t word_pos;
	int16_t done;

	char line_buf[128];

	xrect_Copy_Rect(&text_rect, clip);
	text_rect.bottom = text_rect.top + 10;
	avail_width = text_rect.right - text_rect.left;

	text_data = (char*)xmemhdl_Lock_Handle(handle);
	if (!text_data)
		return;

	line_buf[0] = 0;
	char_idx = 0;
	line_start = -1;
	line_end_pos = -1;
	text_width_idx = 0;
	word_pos = 0;
	done = 0;

	xfont_Enable_FontID_Shadow(0);

	do {
		int ch = (unsigned char)text_data[char_idx];
		if (ch != '$' && ch != 0) {
			int16_t saved_font;
			int16_t str_width;

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
			saved_font = xfont_Get_Font();
			xfont_Set_Font(0);
			str_width = xfont_Get_String_Width(line_buf);
			xfont_Set_Font(saved_font);

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
				xrect_Offset_Rect(&text_rect, 0, 1);
				xfont_Print_Centered_Text(out + 1, &text_rect, 0, 14);
				xrect_Offset_Rect(&text_rect, 0, -1);
			} else {
				xfont_Print_Clipped_Text(out, text_rect.left + 2, text_rect.top + 2, 0, 31);
			}

			line_start = -1;
			line_end_pos = -1;
			xrect_Offset_Rect(&text_rect, 0, 10);
		}
	} while (!done);

	xfont_Disable_FontID_Shadow(0);
	xmemhdl_Unlock_Handle(handle);
}

/* ================================================================
 * Drawing: grid
 * ================================================================ */

// FUNCTION: TIE95 0x7EE58
void player_Draw_Display_Grid(Rect* clip) {
	Rect clip_rect;
	int16_t major_color;
	int16_t minor_color;
	int16_t col_idx;
	int16_t frac_x;
	int16_t start_x;
	int16_t row_idx;
	int16_t frac_y;
	int16_t start_y;
	int16_t save_col_idx;
	int16_t save_row_idx;
	int16_t grid_y;
	int16_t grid_x;

	xrect_Copy_Rect(&clip_rect, clip);
	xpaint_Frame_Clipped_Rect(&clip_rect, 232);

	major_color = 234;
	minor_color = 232;

	/* Compute grid origin from map center */
	col_idx = map_state.center_x / 256;
	if (map_state.center_x > 0 && (map_state.center_x & 0xFF))
		col_idx++;

	frac_x = (int16_t)(((uint8_t)(-map_state.center_x) * map_state.scale_x) >> 8);
	start_x = clip_rect.left + ((clip_rect.right - clip_rect.left) >> 1) + frac_x;
	while (start_x > clip_rect.left) {
		col_idx--;
		start_x -= map_state.scale_x;
	}

	row_idx = map_state.center_y / 256;
	if (map_state.center_y > 0 && (map_state.center_y & 0xFF))
		row_idx++;

	frac_y = (int16_t)(((uint8_t)(-map_state.center_y) * map_state.scale_y) >> 8);
	start_y = clip_rect.top + ((clip_rect.bottom - clip_rect.top) >> 1) + frac_y;
	while (start_y > clip_rect.top) {
		row_idx--;
		start_y -= map_state.scale_y;
	}

	save_col_idx = col_idx;
	save_row_idx = row_idx;
	grid_y = start_y;
	grid_x = start_x;

	/* Minor grid lines (drawn if scale >= 16) */
	if (map_state.scale_x >= 16) {
		int16_t show_minor = (map_state.scale_x >= 32) ? 1 : 0;

		/* Vertical minor lines */
		int16_t x = start_x;
		int16_t ci = col_idx;
		int16_t y;
		int16_t ri;

		while (x < clip_rect.right) {
			int16_t m = ci & 3;
			if (m != 0 && (m == 2 || show_minor)) {
				xpaint_Vert_Clipped_Line(x, clip_rect.top, clip_rect.bottom - clip_rect.top, minor_color);
				if (brief_poly_used) {
					/* Thickness padding for the classic-FB polygon
					 * minification — rasters into brief_buffer but
					 * stays out of the HD snapshot (source RT
					 * renders crisp 1-classic-px lines that don't
					 * need the trick). */
					xpaint_Set_Thickness_Duplicate(true);
					xpaint_Vert_Clipped_Line(x + 1, clip_rect.top, clip_rect.bottom - clip_rect.top,
											 minor_color);
					xpaint_Vert_Clipped_Line(x + 2, clip_rect.top, clip_rect.bottom - clip_rect.top,
											 minor_color);
					xpaint_Set_Thickness_Duplicate(false);
				}
			}
			x += map_state.scale_x;
			ci++;
		}

		/* Horizontal minor lines */
		y = start_y;
		ri = row_idx;
		while (y < clip_rect.bottom) {
			int16_t m = ri & 3;
			if (m != 0 && (m == 2 || show_minor)) {
				xpaint_Horiz_Clipped_Line(clip_rect.left, y, clip_rect.right - clip_rect.left, minor_color);
				if (brief_poly_used) {
					xpaint_Set_Thickness_Duplicate(true);
					xpaint_Horiz_Clipped_Line(clip_rect.left, y + 1, clip_rect.right - clip_rect.left,
											  minor_color);
					xpaint_Set_Thickness_Duplicate(false);
				}
			}
			y += map_state.scale_y;
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
			xpaint_Vert_Clipped_Line(grid_x, clip_rect.top, clip_rect.bottom - clip_rect.top, major_color);
			if (brief_poly_used) {
				xpaint_Set_Thickness_Duplicate(true);
				xpaint_Vert_Clipped_Line(grid_x + 1, clip_rect.top, clip_rect.bottom - clip_rect.top,
										 major_color);
				xpaint_Vert_Clipped_Line(grid_x + 2, clip_rect.top, clip_rect.bottom - clip_rect.top,
										 major_color);
				xpaint_Set_Thickness_Duplicate(false);
			}
		}
		grid_x += map_state.scale_x;
		col_idx++;
	}

	while (grid_y < clip_rect.bottom) {
		if ((row_idx & 3) == 0) {
			xpaint_Horiz_Clipped_Line(clip_rect.left, grid_y, clip_rect.right - clip_rect.left, major_color);
			if (brief_poly_used) {
				xpaint_Set_Thickness_Duplicate(true);
				xpaint_Horiz_Clipped_Line(clip_rect.left, grid_y + 1, clip_rect.right - clip_rect.left,
										  major_color);
				xpaint_Set_Thickness_Duplicate(false);
			}
		}
		grid_y += map_state.scale_y;
		row_idx++;
	}
}

/* ================================================================
 * Drawing: zoom target animation
 * ================================================================ */

// FUNCTION: TIE95 0x7FBD0
// FUNCTION: TIE98 0x46B9F0
void player_Draw_Map_Zoom(Rect* clip, Rect* dest, int16_t fg_index, int16_t target_id) {
	int16_t species = fgroup.fg[fg_index].species - 1;
	int16_t way_x = fgroup.fg[fg_index].way_x[14];
	int16_t way_y = fgroup.fg[fg_index].way_y[14];
	int16_t side = fgroup.fg[fg_index].side;
	int16_t target_color;

	int16_t screen_x;
	int16_t screen_y;
	Rect r;
	int16_t offx, offy;
	int16_t anim_size, anim_count, base_color;
	int16_t i;

	switch (side) {
		case 1:
		case 4:
			target_color = 232;
			break;
		case 2:
			target_color = 248;
			break;
		case 3:
		case 5:
			target_color = 240;
			break;
		default:
			target_color = 224;
			break;
	}

	if (map_state.scale_x < 32)
		species += 88;
	if (species < 0)
		return;

	player_Map_To_Screen_Pos(clip, way_x, way_y, &screen_x, &screen_y);

	xrect_Set_Rect(&r, screen_x - 4, screen_y - 4, screen_x + 5, screen_y + 5);

	xactor_Set_Actor_State(icon_actors[0], species, 0);

	xactor_Get_Actor_Offset(icon_actors[0], &offx, &offy);
	screen_x -= offx + (icon_actors[0]->w >> 1);
	screen_y -= offy + (icon_actors[0]->h >> 1);
	icon_actors[0]->flags |= AF_REMAP_COLOR;

	if (target_id >= 12) {
		xrect_Inset_Rect(&r, -2, -2);
		xpaint_Paint_Clipped_Rect(&r, target_color + 2);
		xpaint_Frame_Clipped_Rect(&r, target_color + 6);
		icon_actors[0]->flags &= ~AF_REMAP_COLOR;
		return;
	}

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
		xrect_Inset_Rect(&r, 11 - target_id, 11 - target_id);
		xpaint_Paint_Clipped_Rect(&r, target_color + 2);
		xpaint_Frame_Clipped_Rect(&r, target_color + target_id - 6);
	}

	for (i = 0; i < anim_count; i++) {
		icon_actors[0]->foreColor = base_color;
		xactanim_Draw_Anim_Actor(icon_actors[0], clip, dest, screen_x - anim_size, screen_y - anim_size, 1);
		xactanim_Draw_Anim_Actor(icon_actors[0], clip, dest, screen_x + anim_size, screen_y - anim_size, 1);
		xactanim_Draw_Anim_Actor(icon_actors[0], clip, dest, screen_x - anim_size, screen_y + anim_size, 1);
		xactanim_Draw_Anim_Actor(icon_actors[0], clip, dest, screen_x + anim_size, screen_y + anim_size, 1);
		anim_size -= 2;
		base_color += 2;
	}

	icon_actors[0]->flags &= ~AF_REMAP_COLOR;
}

/* ================================================================
 * Drawing: ship icons
 * ================================================================ */

// FUNCTION: TIE95 0x7F294
// FUNCTION: TIE98 0x46B120
void player_Draw_Display_Ship(Rect* clip, Rect* dest) {
	Rect dst;
	int16_t i;
	int16_t scr_x, scr_y;
	xrect_Copy_Rect(&dst, clip);

	for (i = 0; i < 8; i++) {
		int16_t fg;
		if (!brief.target_on[i])
			continue;
		fg = brief.target_id[i];
		player_Map_To_Screen_Pos(&dst, fgroup.fg[fg].way_x[14], fgroup.fg[fg].way_y[14], &scr_x, &scr_y);
		player_Draw_Map_Zoom(clip, dest, fg, brief.target_state[i]);
	}

	for (i = 0; i < 8; i++) {
		char str[40];
		char* text;
		int16_t m;
		if (!brief.text_on[i])
			continue;
		player_Map_To_Screen_Pos(&dst, brief.text_x[i], brief.text_y[i], &scr_x, &scr_y);
		text = (char*)xmemhdl_Lock_Handle(brief.text_data[brief.text_id[i]]);
#ifdef TIE_MODERN
		if (text) {
#endif
			strcpy(str, text);
			xmemhdl_Unlock_Handle(brief.text_data[brief.text_id[i]]);
#ifdef TIE_MODERN
		} else
			str[0] = 0;
#endif
		for (m = 0; str[m]; m++) {
			if (str[m] == '[')
				str[m] = 2;
			if (str[m] == ']')
				str[m] = 1;
		}
		player_Draw_Double_Readout_Text(str, 1, scr_x, scr_y, brief.text_state[i], brief.text_color[i]);
	}

	for (i = 0; i < fgroup.num_fgs; i++) {
		int16_t species = fgroup.fg[i].species - 1;
		int16_t icon_idx;
		int16_t offx, offy;
		if (!fgroup.fg[i].way_used[14])
			continue;
		switch (fgroup.fg[i].side) {
			case 1:
			case 4:
				icon_idx = 1;
				break;
			case 2:
				icon_idx = 2;
				break;
			case 3:
			case 5:
				icon_idx = 3;
				break;
			default:
				icon_idx = 0;
				break;
		}
		if (map_state.scale_x < 32)
			species += 88;
		if (species < 0)
			continue;
		player_Map_To_Screen_Pos(&dst, fgroup.fg[i].way_x[14], fgroup.fg[i].way_y[14], &scr_x, &scr_y);
		xactor_Set_Actor_State(icon_actors[icon_idx], species, 0);
		xactor_Get_Actor_Offset(icon_actors[icon_idx], &offx, &offy);
		scr_x -= offx + (icon_actors[icon_idx]->w >> 1);
		scr_y -= offy + (icon_actors[icon_idx]->h >> 1);
		xactanim_Draw_Anim_Actor(icon_actors[icon_idx], clip, dest, scr_x, scr_y, 1);
	}
}

/* ================================================================
 * Drawing: full map composite
 * ================================================================ */

// FUNCTION: TIE95 0x7ECC0
// FUNCTION: TIE98 0x46ABD0
int16_t player_Draw_Display_Map(Rect* view_rect, Rect* clip_rect, int16_t refresh) {
	Rect dst, map_area, draw_clip;

	(void)refresh;

	player_Stars_To_Back(view_rect->top);

	if (!brief_poly_used) {
		/* Top paragraph area */
		Rect src;

		xrect_Copy_Rect(&dst, &map_state.source_rect);
		xrect_Offset_Rect(&dst, view_rect->left, view_rect->top);
		dst.bottom = dst.top + 12;
		if (shellext_Get_Cur_Scene() != SCENE_COMBAT_MAP_A)
			xpaint_Paint_Clipped_Rect(&dst, 1);
		if (brief.para_on[0])
			player_Draw_Map_Paragraph(&dst, brief.para_data[brief.para_id[0]], 0);

		/* Bottom status area */

		xrect_Copy_Rect(&src, &map_state.source_rect);
		xrect_Offset_Rect(&src, view_rect->left, view_rect->top);
		src.top = src.bottom - 22;
		if (shellext_Get_Cur_Scene() != SCENE_COMBAT_MAP_A)
			xpaint_Paint_Clipped_Rect(&src, 1);
		if (brief.para_on[1])
			player_Draw_Map_Paragraph(&src, brief.para_data[brief.para_id[1]], 0);

		/* Map area (between top paragraph and bottom status) */
		xrect_Copy_Rect(&map_area, &map_state.source_rect);
		xrect_Offset_Rect(&map_area, view_rect->left, view_rect->top);
		map_area.top += 12;
		map_area.bottom -= 22;
	} else {
		xrect_Copy_Rect(&map_area, &map_state.source_rect);
		xrect_Offset_Rect(&map_area, view_rect->left, view_rect->top);
	}

	/* Clip and draw grid + ships */
	xrect_Copy_Rect(&draw_clip, clip_rect);
	xrect_Clip_Rect(&draw_clip, &map_area);
	xcanvas_Set_Drawing_Canvas_Clip(&draw_clip);

	player_Draw_Display_Grid(&map_area);
	player_Draw_Display_Ship(&map_area, &draw_clip);

	return 1;
}

/* ================================================================
 * XINPUT callbacks
 * ================================================================ */

// FUNCTION: TIE95 0x7D9E0
// FUNCTION: TIE98 0x469A90
static int16_t player_iupdate_Map(Input* input, Rect* bounds, Rect* clip, int16_t key, uint8_t left,
								  uint8_t right, int16_t mouse_x, int16_t mouse_y) {
	Rect inset_bounds, clipped_rect;

	(void)input;

	xrect_Copy_Rect(&inset_bounds, bounds);
	xrect_Inset_Rect(&inset_bounds, 1, 1);
	xrect_Copy_Rect(&clipped_rect, clip);
	xrect_Clip_Rect(&clipped_rect, &inset_bounds);

	if (key)
		return 0;

	return player_Update_Display_Map(&inset_bounds, &clipped_rect, left, right, (int16_t)(mouse_x - 1),
									 (int16_t)(mouse_y - 1));
}

// FUNCTION: TIE95 0x7DA48
// FUNCTION: TIE98 0x469B20
static void player_iuser_Map(Input* input, int32_t time) {
	(void)input;
	(void)time;
	if (!map_state.playing)
		return;
	player_Move_Display_Map();
	player_Step_Display_Map();
#ifdef TIE_MODERN
	TieMapSnapshot_Capture();
#endif
}

// FUNCTION: TIE95 0x7DA78
// FUNCTION: TIE98 0x469B40
static void player_idraw_Map(Input* input, Rect* bounds, Rect* clip, int16_t refresh) {
	Rect r, draw_clip;
#ifdef TIE_MODERN
	TieMapHeader* map_h;
#endif

#ifdef TIE_MODERN

	/* Stamp `start_z` so the merge dispatch knows where to slot the
	 * brief-map quad: at this z, before any widget content the engine
	 * emits below. In rect mode the widget's paint/draw/text records
	 * naturally land just after this slot (target=CUTSCENE), giving
	 * Painter's-algorithm layering: backdrop quad → widget content.
	 * In poly mode the widget's records carry target=BRIEF_SOURCE and
	 * are routed onto the source RT; the rect quad slot still anchors
	 * the polygon-warp quad at the right point in the cutscene-RT
	 * draw order. */
	map_h = TieSnapshotBuilder_MapMut();
	if (map_h && map_h->active)
		map_h->start_z = TieSnapshotBuilder_NextEmitZ();
#endif

	if (brief_poly_used) {
		/* Bypass the canvas-leak gate that normally suppresses emits
		 * while a non-screen canvas is bound. The engine renders the
		 * brief widget into a 320×200 brief_buffer scratch (so the
		 * classic FB's stub_Map_Clipped_Image warp can read it back),
		 * but the HD snapshot needs those same emits — the application
		 * routes them to the source RT and applies the polygon warp
		 * at composite time. Other scratch-canvas paths (tielogo,
		 * title) leave the override off so they stay suppressed. */

#ifdef TIE_MODERN
		xcanvas_Set_Render_Allow_Non_Screen(true);
#endif
		xcanvas_Push_Canvas(&brief_buffer);
		xrect_Set_Rect(&r, 0, 0, 292, 147);
		xrect_Copy_Rect(&draw_clip, &r);
		xcanvas_Set_Drawing_Canvas_Clip(&draw_clip);
	} else {
		xrect_Copy_Rect(&r, bounds);
		xrect_Inset_Rect(&r, 1, 1);
		xrect_Copy_Rect(&draw_clip, clip);
		xrect_Clip_Rect(&draw_clip, &r);
	}

	player_Draw_Display_Map(&r, &draw_clip, refresh);

	if (brief_poly_used) {
		Rect src_rect;
		char* pixels;

		xcanvas_Pop_Canvas();

		xrect_Copy_Rect(&src_rect, &map_state.source_rect);
		xrect_Inset_Rect(&src_rect, 32, 16);
		pixels = (char*)xbm_Lock_Bitmap(&brief_buffer);
		stub_Map_Clipped_Image(pixels, brief_poly.x, &src_rect, 320, 150);
		xbm_Unlock_Bitmap(&brief_buffer);
		/* Restore the canvas-leak gate so subsequent scratch-canvas
		 * emits (tielogo / title backgrounds, etc.) stay suppressed. */

#ifdef TIE_MODERN
		xcanvas_Set_Render_Allow_Non_Screen(false);
#endif
	}

	if (xinpattr_Is_Input_Dirty(input))
		xdirty_Dirty_Rect(clip);
}

/* ================================================================
 * Init / Free
 * ================================================================ */

// FUNCTION: TIE95 0x7DB7C
// FUNCTION: TIE98 0x469C80
void player_Init_Display_Map(void) {
	int16_t i;

	map_state.playing = 1;
	map_state.selected_fg_idx = 0;
	map_state.center_x = 0;
	map_state.center_y = 0;
	map_state.target_x = 0;
	map_state.target_y = 0;
	map_state.scale_x = 16;
	map_state.scale_y = 16;
	map_state.scale_target_x = 16;
	map_state.scale_target_y = 16;

	memset(&fgroup, 0, sizeof(fgroup));
	fgroup.num_fgs = 1;
	fgroup.mission.all_way_shown = 0;
	fgroup.mission.win_type = 1;
	player_Clear_Page_Commands();
	player_Rewind_Page();

	for (i = 0; i < 32; i++)
		brief.text_data[i] = xmemhdl_Alloc_Clear_Handle(40, LANDRU_MEMORY_RESOURCE);
	for (i = 0; i < 32; i++)
		brief.para_data[i] = xmemhdl_Alloc_Clear_Handle(160, LANDRU_MEMORY_RESOURCE);
	for (i = 0; i < 20; i++)
		brief.talk_data[i] = xmemhdl_Alloc_Clear_Handle(1024, LANDRU_MEMORY_RESOURCE);

	for (i = 0; i < 2; ++i)
		brief.para_on[i] = 0;
	for (i = 0; i < 8; ++i)
		brief.target_on[i] = 0;
	for (i = 0; i < 8; ++i)
		brief.text_on[i] = 0;

	xrect_Set_Rect(&map_state.source_rect, 0, 0, 292, 147);
}

// FUNCTION: TIE95 0x7DD0C
void player_Free_Display_Map(void) {
	int16_t i;
	for (i = 0; i < 32; i++) {
		if (brief.text_data[i]) {
			xmemhdl_Free_Handle(brief.text_data[i]);
#ifdef TIE_MODERN
			brief.text_data[i] = 0;
#endif
		}
	}
	for (i = 0; i < 32; i++) {
		if (brief.para_data[i]) {
			xmemhdl_Free_Handle(brief.para_data[i]);
#ifdef TIE_MODERN
			brief.para_data[i] = 0;
#endif
		}
	}
	for (i = 0; i < 20; i++) {
		if (brief.talk_data[i]) {
			xmemhdl_Free_Handle(brief.talk_data[i]);
#ifdef TIE_MODERN
			brief.talk_data[i] = 0;
#endif
		}
	}
}

// FUNCTION: TIE95 0x7DDDC
void player_Load_Display_Map(void) {
	char name[64];
	int16_t version_flag = 0;
	int16_t fg_count = 0, event_count = 0, object_count = 0;
	int16_t read_len;
	uint8_t event_discard[90];
	uint8_t object_discard[28];

	TieFile* fp;
	int16_t i;

#ifdef TIE_MODERN
	uint8_t mis_buf[EMISSIONSTRUCT_DISK_SIZE];
	uint8_t fg_buf[EFGSTRUCT_DISK_SIZE];
	uint8_t page_buf[EBRIEFPAGE_DISK_SIZE];
#endif

	shipext_Get_Mission_Path(name);
	memset(&fgroup, 0, sizeof(fgroup));

	fp = TieStorage_Open(TIE_FILE_ROOT_FLIGHT_ASSET, name, "rb");
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

#ifdef TIE_MODERN
	TieStorage_Read(mis_buf, EMISSIONSTRUCT_DISK_SIZE, 1, fp);
	EMissionStruct_decode(&fgroup.mission, mis_buf);
#else
	TieStorage_Read(&fgroup.mission, EMISSIONSTRUCT_DISK_SIZE, 1, fp);
#endif
	shipext_Set_Mission_Ship(0);

	for (i = 0; i < fgroup.num_fgs; i++) {
#ifdef TIE_MODERN
		TieStorage_Read(fg_buf, EFGSTRUCT_DISK_SIZE, 1, fp);
		EFGStruct_decode(&fgroup.fg[i], fg_buf);
#else
		TieStorage_Read(&fgroup.fg[i], EFGSTRUCT_DISK_SIZE, 1, fp);
#endif
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

#ifdef TIE_MODERN
	TieStorage_Read(page_buf, EBRIEFPAGE_DISK_SIZE, 1, fp);
	TieBriefing_DecodePage(&brief.page, page_buf);
#else
	TieStorage_Read(&brief.page, EBRIEFPAGE_DISK_SIZE, 1, fp);
#endif

	/* Read text strings */
	for (i = 0; i < 32; i++) {
		char* buf = (char*)xmemhdl_Lock_Handle(brief.text_data[i]);
		if (version_flag)
			TieStorage_Read(&read_len, 2, 1, fp);
		else
			read_len = 40;
		if (read_len && buf)
			TieStorage_Read(buf, read_len, 1, fp);
		if (version_flag && buf)
			buf[read_len] = 0;
		xmemhdl_Unlock_Handle(brief.text_data[i]);
	}

	/* Read paragraph strings */
	for (i = 0; i < 32; i++) {
		char* buf = (char*)xmemhdl_Lock_Handle(brief.para_data[i]);
		if (version_flag)
			TieStorage_Read(&read_len, 2, 1, fp);
		else
			read_len = 160;
		if (read_len && buf)
			TieStorage_Read(buf, read_len, 1, fp);
		if (version_flag && buf)
			buf[read_len] = 0;
		xmemhdl_Unlock_Handle(brief.para_data[i]);
	}

	/* Read talk strings */
	for (i = 0; i < 20; i++) {
		char* buf = (char*)xmemhdl_Lock_Handle(brief.talk_data[i]);
		if (version_flag)
			TieStorage_Read(&read_len, 2, 1, fp);
		else
			read_len = 0;
		if (read_len && buf)
			TieStorage_Read(buf, read_len, 1, fp);
		if (buf)
			buf[read_len] = 0;
		xmemhdl_Unlock_Handle(brief.talk_data[i]);
	}

	TieStorage_Close(fp);
}

// FUNCTION: TIE95 0x7D594
// FUNCTION: TIE98 0x469580
void player_Init_Brief_Display(Input* input, void* poly) {
	ResFile* player_res;
	Rect r;
	int16_t player_fg;

	player_Init_Display_Map();
#ifdef TIE_MODERN
	TieMapSnapshot_SetInput(input);
#endif

	/* Set up polygon projection if provided */
	if (poly) {
		xrect_Copy_Poly(&brief_poly, (Poly*)poly);
		brief_poly_used = 1;
	} else {
		brief_poly_used = 0;
	}

	/* Load icon actors from player.lfd */
	player_res = shellext_Open_Empire_Resource("player.lfd");

	xrect_Set_Rect(&r, 0, 0, draw_bm_gbl->w, draw_bm_gbl->h);

	icon_actors[0] = xactanim_Res_Anim_Actor("iconsgrn", &r, 0, 0, 0);
	xactor_Set_Actor_Time(icon_actors[0], 0, 0);
	xactor_Non_Dirty_Actor(icon_actors[0]);

	icon_actors[1] = xactanim_Res_Anim_Actor("iconsred", &r, 0, 0, 0);
	xactor_Set_Actor_Time(icon_actors[1], 0, 0);
	xactor_Non_Dirty_Actor(icon_actors[1]);

	icon_actors[2] = xactanim_Res_Anim_Actor("iconsblu", &r, 0, 0, 0);
	xactor_Set_Actor_Time(icon_actors[2], 0, 0);
	xactor_Non_Dirty_Actor(icon_actors[2]);

	icon_actors[3] = xactanim_Res_Anim_Actor("iconspur", &r, 0, 0, 0);
	xactor_Set_Actor_Time(icon_actors[3], 0, 0);
	xactor_Non_Dirty_Actor(icon_actors[3]);

	/* Load star background */
	xrect_Set_Rect(&r, 0, 0, 320, 150);
	if (shellext_Get_Cur_Scene() == SCENE_COMBAT_MAP_A || shellext_Get_Cur_Scene() == SCENE_COMBAT_MAP_B) {
		ResFile* combat_res = shellext_Open_Empire_Resource("map.lfd");
		stars_actor = xactdelt_Res_Delta_Actor("cmbtmap2", &r, 0, 0, 0);
		xres_Close_Resource(combat_res);
	} else {
		stars_actor = xactdelt_Res_Delta_Actor("stars", &r, 0, 0, 0);
	}
	xactor_Set_Actor_Time(stars_actor, 0, 0);

	if (brief_poly_used) {
		xbm_Init_Bitmap(&brief_buffer);
		xbm_Alloc_Bitmap(&brief_buffer, 320, 200);
	}

	/* Allocate star buffer and render star background into it */
	star_buffer = xmemhdl_Alloc_Handle(48000, LANDRU_MEMORY_DEFAULT);

	if (shellext_Get_Cur_Scene() == SCENE_COMBAT_MAP_A)
		stars_actor->var1 = 17;
	else
		stars_actor->var1 = 16;

	player_Actor_To_Buffer(stars_actor, star_buffer);

	xres_Close_Resource(player_res);

	/* Set up XINPUT callbacks */
	if (!brief_poly_used) {
		xinpattr_Set_Input_Update_Function(input, player_iupdate_Map);
		xinpattr_Set_Input_User_Function(input, player_iuser_Map);
		input->mouseUsage = downMoveUpInput;
	}
	xinpattr_Set_Input_Draw_Function(input, player_idraw_Map);

	/* Load mission data */
	player_Load_Display_Map();

	player_Rewind_Page();

	/* Set up weapon state from player's flight group */
	mission.beam_used = 0;
	mission.torp_used = 0;

	for (player_fg = 0; player_fg < fgroup.num_fgs; player_fg++) {
		if (fgroup.fg[player_fg].player_flag) {
			uint8_t battle;

			mission.torp_used = fgroup.fg[player_fg].warhead;
			mission.beam_used = fgroup.fg[player_fg].beam;
			beam_level = mission.beam_used;

			battle = shipext_Get_Tour_Battle();
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

// FUNCTION: TIE95 0x7D8A4
// FUNCTION: TIE98 0x469920
EBriefStruct* player_Init_Brief_For_Talk(void) {
	player_Init_Display_Map();
	player_Load_Display_Map();
	return &brief;
}

// FUNCTION: TIE95 0x7D8C0
void player_Free_Brief_Display(void) {
	/* Snapshot emitter: drop the widget pointer so a stray
	 * post-teardown emit produces active=0 instead of dereferencing
	 * freed Input state. */
#ifdef TIE_MODERN
	TieMapSnapshot_SetInput(NULL);
#endif

	xmemhdl_Free_Handle(star_buffer);
#ifdef TIE_MODERN
	star_buffer = LANDRU_NULL_HANDLE;
#endif

	if (brief_poly_used)
		xbm_Free_Bitmap(&brief_buffer);

	player_Free_Display_Map();
}

#ifdef TIE_MODERN
bool TieRecoveredMap_ReadSnapshotView(TieRecoveredMapSnapshotView* out) {
	int16_t scene;

	if (!out)
		return false;
	memset(out, 0, sizeof *out);
	if (!map_state.playing)
		return true;

	scene = shellext_Get_Cur_Scene();
	out->active = true;
	out->background_kind = scene == SCENE_COMBAT_MAP_A   ? TIE_MAP_BG_COMBAT
						   : scene == SCENE_COMBAT_MAP_B ? TIE_MAP_BG_COMBAT_DEBRIEF
						   : scene == SCENE_TRAIN_MAP    ? TIE_MAP_BG_TRAINING
														 : TIE_MAP_BG_STARS;
	out->has_polygon = brief_poly_used != 0;
	out->source_width = (int16_t)(map_state.source_rect.right - map_state.source_rect.left);
	out->source_height = (int16_t)(map_state.source_rect.bottom - map_state.source_rect.top);
	if (out->has_polygon) {
		int index;

		for (index = 0; index < 4; ++index) {
			out->polygon_x[index] = brief_poly.x[index];
			out->polygon_y[index] = brief_poly.y[index];
		}
	}
	out->scene_time = brief.page.time;
	return true;
}
#endif
