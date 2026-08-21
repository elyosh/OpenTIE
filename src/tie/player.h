#ifndef __PLAYER_H__
#define __PLAYER_H__

#include "landru/actor.h"
#include "landru/bitmap.h"
#include "landru/input.h"
#include "landru/rect.h"
#include "tie/shipext.h"
#include <stdint.h>

/* Briefing page command opcodes.
 * Each command in the page buffer is: [time, opcode, params...].
 * Time is the frame tick at which the command fires.
 * Param counts are given by map_cmd_size[opcode]. */
typedef enum {
	BCMD_SEEK = 1,          /* pause/seek marker (0 params) */
	BCMD_CLEAR_PARA = 3,    /* clear both paragraph slots (0 params) */
	BCMD_SHOW_PARA0 = 4,    /* show paragraph in slot 0 (1 param: para_id) */
	BCMD_SHOW_PARA1 = 5,    /* show paragraph in slot 1 (1 param: para_id) */
	BCMD_MOVE = 6,          /* move map center (2 params: target_x, target_y) */
	BCMD_ZOOM = 7,          /* zoom map scale (2 params: scale_x, scale_y) */
	BCMD_CLEAR_TARGET = 8,  /* clear all 8 target slots (0 params) */
	BCMD_SHOW_TARGET0 = 9,  /* show target in slot 0 (1 param: fg_index) */
	BCMD_SHOW_TARGET7 = 16, /* show target in slot 7 */
	BCMD_CLEAR_TEXT = 17,   /* clear all 8 text slots (0 params) */
	BCMD_SHOW_TEXT0 = 18,   /* show text in slot 0 (4 params: text_id, x, y, color) */
	BCMD_SHOW_TEXT7 = 25,   /* show text in slot 7 */
	BCMD_END_PAGE = 34,     /* end-of-page sentinel (0 params) */
} BriefCmd;

/*
 * MAP_EBriefPage — briefing page command buffer.
 *
 * Naturally aligned in memory (all int16 fields, every field already
 * 2-aligned, so dropping the original pragma pack(1) doesn't change
 * sizeof). On-disk layout is the fixed 810-byte little-endian record
 * produced by the original game; loaded via TieRecoveredBrief_DecodePage so BE
 * hosts get the same in-memory values. Read-only at runtime, hence no
 * encoder.
 */
typedef struct {
	int16_t len;           /* +0x00: total length */
	int16_t time;          /* +0x02: current time */
	int16_t index;         /* +0x04: current command index */
	int16_t size;          /* +0x06: page data size */
	int16_t tile;          /* +0x08 */
	int16_t commands[400]; /* +0x0A: command words */
} EBriefPage;

#define EBRIEFPAGE_DISK_SIZE 810u

/* Port adapter for the recovered packed disk record. */
void TieRecoveredBrief_DecodePage(EBriefPage* dst, const uint8_t* src);

/*
 * MAP_EBriefStruct — briefing data. Runtime layout only; the embedded
 * EBriefPage is loaded from disk as a unit but EBriefStruct itself is
 * never serialized (it carries void * handles to allocated buffers).
 * Offsets in the comments below are the original DOS pack(1) offsets;
 * the runtime offsets on the host depend on natural alignment.
 */
typedef struct {
	EBriefPage page;         /* +0x000 */
	void* text_data[32];     /* +0x32A: text data (HANDLE → void*) */
	void* para_data[32];     /* +0x36A: paragraph data (HANDLE → void*) */
	void* talk_data[20];     /* +0x3AA: talk data (HANDLE → void*) */
	int16_t cur_text;        /* +0x3D2 */
	int16_t cur_back;        /* +0x3D4 */
	int16_t move_on;         /* +0x3D6 */
	int16_t scale_on;        /* +0x3D8 */
	int16_t para_on[2];      /* +0x3DA */
	int16_t para_id[2];      /* +0x3DE */
	int16_t para_off;        /* +0x3E2 */
	int16_t target_on[8];    /* +0x3E4 */
	int16_t target_id[8];    /* +0x3F4 */
	int16_t target_state[8]; /* +0x404 */
	int16_t target_off;      /* +0x414 */
	int16_t text_on[8];      /* +0x416 */
	int16_t text_id[8];      /* +0x426 */
	int16_t text_x[8];       /* +0x436 */
	int16_t text_y[8];       /* +0x446 */
	int16_t text_state[8];   /* +0x456 */
	int16_t text_color[8];   /* +0x466 */
	int16_t text_off;        /* +0x476 */
	int16_t seek_on;         /* +0x478 */
} EBriefStruct;

/*
 * MAP_EFArrayStruct — flight group array + mission header. Runtime
 * layout only; populated by player_Load_Display_Map via EFGStruct_-
 * decode + EMissionStruct_decode. Never serialized as a unit. Offsets
 * in the original DOS pack(1) image were +0x0000 fg[48], +0x36C0
 * mission, +0x3882 num_fgs (total 0x3884 = 14468 bytes); on the host
 * they depend on natural alignment.
 */
typedef struct {
	EFGStruct fg[48];
	EMissionStruct mission;
	int16_t num_fgs;
} EFArrayStruct;

/* --- Function declarations --- */

/* Weapon selectors */
int16_t player_Get_Beam_Used(void);
void player_Next_Beam(void);
void player_Last_Beam(void);
int16_t player_Get_Torp_Used(void);
void player_Next_Torp(void);
void player_Last_Torp(void);

/* Data accessors */
EBriefStruct* player_Fetch_Brief(void);
EFArrayStruct* player_Fetch_FGroup(void);
int16_t player_Is_Side_Enemy(int16_t side);
int16_t player_Is_Map_Playing(void);
int player_Toggle_Map_Play(void);

/* Init / Free */
void player_Init_Brief_Display(Input* input, void* poly);
void player_Init_Brief_For_Talk(void);
void player_Free_Brief_Display(void);
void player_Init_Display_Map(void);
void player_Free_Display_Map(void);
void player_Load_Display_Map(void);

/* Page commands */
void player_Clear_Page_Commands(void);
void player_Rewind_Page(void);
void player_Seek_Page(int16_t page, int32_t time);
void player_Seek_Page_Section(void);
void player_Step_Page(int16_t flag);
void player_Reseek_Page(void);

/* Map display */
void player_Move_Display_Map(void);
void player_Step_Display_Map(void);
void player_Update_Display_Map(int16_t mouse_x, int16_t mouse_y);

void player_Draw_Display_Map(Rect* view_rect, Rect* clip_rect);
void player_Draw_Display_Grid(Rect* clip);
void player_Draw_Display_Ship(Rect* clip, Rect* dest);
void player_Draw_Map_Paragraph(Rect* clip, void* handle, int16_t flag);
void player_Draw_Double_Readout_Text(const char* text, int16_t color, int16_t screen_x, int16_t screen_y,
									 int16_t text_y, int16_t text_state);
void player_Draw_Readout_Text(const char* text, int16_t color, int16_t y, int16_t x, int16_t index,
							  int16_t state);
void player_Draw_Map_Zoom(Rect* clip, Rect* dest, int16_t fg_index, int16_t target_id);

/* Map navigation */
int16_t player_Move_To_Value(int16_t current, int16_t target, int16_t step);
void player_Map_To_Screen_Pos(Rect* view_rect, int16_t map_x, int16_t map_y, int16_t* out_x, int16_t* out_y);
void player_Screen_To_Map_Pos(Rect* view_rect, int16_t screen_x, int16_t screen_y, int16_t* out_x,
							  int16_t* out_y);
int16_t player_Find_Ship_On_Screen(Rect* bounds, int16_t screen_x, int16_t screen_y);

/* Actor helpers */
void player_Actor_To_Buffer(Actor* actor, void* buffer);
void player_Stars_To_Back(int16_t screen_y);

/* XINPUT callbacks */
int16_t player_iupdate_Map(Input* input, Rect* bounds, Rect* clip, int16_t key, int16_t left, int16_t right,
						   int16_t mouse_x, int16_t mouse_y);
void player_iuser_Map(Input* input, int32_t time);
void player_idraw_Map(Input* input, Rect* bounds, Rect* clip, int16_t refresh);

#endif
