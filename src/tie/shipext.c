#include "tie/shipext.h"
#ifdef TIE_MODERN
#include "tie_runtime/storage/mission_records.h"
#endif
#include "tie_runtime/audio/config.h"
#include "tie_runtime/diagnostics/diagnostics.h"
#include "tie_runtime/display/classic_display.h"
#include "tie_runtime/display/classic_framebuffer.h"
#include "tie_runtime/flight_assets/model_types.h"
#include "tie_runtime/input/input.h"
#include "tie_runtime/runtime/exports.h"
#include "tie_runtime/runtime/profile.h"
#include "tie_runtime/storage/pilot_storage.h"
#include "tie_runtime/storage/storage.h"

#include "tie/bpflight.h"
#include "tie/shellext.h"
#include "tie/textext.h"

#include "landru/actdelt.h" /* xactdelt_Res_Delta_Actor */
#include "landru/canvas.h"  /* xcanvas_Get_Drawing_Canvas_Bounds */
#include "landru/file.h"
#include "landru/paragrp.h"
#include "landru/res.h"

#include "tie/fediskio.h"
#include "tie/tie.h"

#include "../util/binio.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* --------------------------------------------------------------------------
 * PilotRecord codec.
 *
 * The .tfr file stores two 1928-byte slots back-to-back (primary at +0,
 * backup at +1928). Each slot is the canonical little-endian record
 * documented field-by-field in shipext.h. The runtime PilotRecord is
 * naturally aligned and may be wider on the host; always go through the
 * codec at file-format boundaries.
 *
 * Field layout:
 *   +0x000 version, exit_status, rank, game_level (4 x u8)
 *   +0x004 score (i32)
 *   +0x008 avg_score (u16)
 *   +0x00A secret_order_rank, reserved_0b (2 x u8)
 *   +0x00C secret_completions (u16), secret_score (i32)
 *   +0x012 reserved_12[10]
 *   +0x01C cur_train_ship, train_level[12], reserved_29
 *   +0x02A train_score[12] (i32 x 12)
 *   +0x05A train_max_level[12]
 *   +0x066 cur_combat_ship, combat_course_cursor[32], reserved_87
 *   +0x088 combat_score[12][8] (i32 x 96)
 *   +0x208 combat_complete[12][8]
 *   +0x268 cur_battle, battle_status[20], battle_cursor[20]
 *   +0x291 linked_data[256]
 *   +0x391 secret_complete_bits[20], mission_bonus_bits[20]
 *   +0x3B9 reserved_3b9[29], reserved_3d6[4]
 *   +0x3DA tour_score[20][8] (i32 x 160)
 *   +0x65A total_kills, total_captures (u16 x 2), reserved_65e[2]
 *   +0x660 kills_by_ship_type[69], captures_by_ship_type[69] (u16 x 138)
 *   +0x774 laser_total, laser_hits (i32 x 2), reserved_77c[4]
 *   +0x780 warhead_total, warhead_hits (u16 x 2), reserved_784[2]
 *   +0x786 ejection_count (u16)
 * -------------------------------------------------------------------------- */

void PilotRecord_decode(PilotRecord* dst, const uint8_t* src) {
	int i;
	int s;
	int c;
	int b;
	int m;

	dst->version = src[0x000];
	dst->exit_status = src[0x001];
	dst->rank = src[0x002];
	dst->game_level = src[0x003];
	dst->score = br_i32le(src + 0x004);
	dst->avg_score = br_u16le(src + 0x008);
	dst->secret_order_rank = src[0x00A];
	dst->reserved_0b = src[0x00B];
	dst->secret_completions = br_u16le(src + 0x00C);
	dst->secret_score = br_i32le(src + 0x00E);
	memcpy(dst->reserved_12, src + 0x012, 10);

	dst->cur_train_ship = src[0x01C];
	memcpy(dst->train_level, src + 0x01D, NUM_SHIPS);
	dst->reserved_29 = src[0x029];
	for (i = 0; i < NUM_SHIPS; ++i)
		dst->train_score[i] = br_i32le(src + 0x02A + i * 4);
	memcpy(dst->train_max_level, src + 0x05A, NUM_SHIPS);

	dst->cur_combat_ship = src[0x066];
	memcpy(dst->combat_course_cursor, src + 0x067, SHIP_INFO_SIZE);
	dst->reserved_87 = src[0x087];
	for (s = 0; s < NUM_SHIPS; ++s)
		for (c = 0; c < 8; ++c)
			dst->combat_score[s][c] = br_i32le(src + 0x088 + (s * 8 + c) * 4);
	for (s = 0; s < NUM_SHIPS; ++s)
		memcpy(dst->combat_complete[s], src + 0x208 + s * 8, 8);

	dst->cur_battle = src[0x268];
	memcpy(dst->battle_status, src + 0x269, NUM_BATTLES);
	memcpy(dst->battle_cursor, src + 0x27D, NUM_BATTLES);
	memcpy(dst->linked_data, src + 0x291, 256);
	memcpy(dst->secret_complete_bits, src + 0x391, NUM_BATTLES);
	memcpy(dst->mission_bonus_bits, src + 0x3A5, NUM_BATTLES);
	memcpy(dst->reserved_3b9, src + 0x3B9, 29);
	memcpy(dst->reserved_3d6, src + 0x3D6, 4);

	for (b = 0; b < NUM_BATTLES; ++b)
		for (m = 0; m < 8; ++m)
			dst->tour_score[b][m] = br_i32le(src + 0x3DA + (b * 8 + m) * 4);

	dst->total_kills = br_u16le(src + 0x65A);
	dst->total_captures = br_u16le(src + 0x65C);
	memcpy(dst->reserved_65e, src + 0x65E, 2);
	for (i = 0; i < 69; ++i)
		dst->kills_by_ship_type[i] = br_u16le(src + 0x660 + i * 2);
	for (i = 0; i < 69; ++i)
		dst->captures_by_ship_type[i] = br_u16le(src + 0x6EA + i * 2);

	dst->laser_total = br_i32le(src + 0x774);
	dst->laser_hits = br_i32le(src + 0x778);
	memcpy(dst->reserved_77c, src + 0x77C, 4);
	dst->warhead_total = br_u16le(src + 0x780);
	dst->warhead_hits = br_u16le(src + 0x782);
	memcpy(dst->reserved_784, src + 0x784, 2);
	dst->ejection_count = br_u16le(src + 0x786);
}

void PilotRecord_encode(uint8_t* dst, const PilotRecord* src) {
	int i;
	int s;
	int c;
	int b;
	int m;

	dst[0x000] = src->version;
	dst[0x001] = src->exit_status;
	dst[0x002] = src->rank;
	dst[0x003] = src->game_level;
	bw_i32le(dst + 0x004, src->score);
	bw_u16le(dst + 0x008, src->avg_score);
	dst[0x00A] = src->secret_order_rank;
	dst[0x00B] = src->reserved_0b;
	bw_u16le(dst + 0x00C, src->secret_completions);
	bw_i32le(dst + 0x00E, src->secret_score);
	memcpy(dst + 0x012, src->reserved_12, 10);

	dst[0x01C] = src->cur_train_ship;
	memcpy(dst + 0x01D, src->train_level, NUM_SHIPS);
	dst[0x029] = src->reserved_29;
	for (i = 0; i < NUM_SHIPS; ++i)
		bw_i32le(dst + 0x02A + i * 4, src->train_score[i]);
	memcpy(dst + 0x05A, src->train_max_level, NUM_SHIPS);

	dst[0x066] = src->cur_combat_ship;
	memcpy(dst + 0x067, src->combat_course_cursor, SHIP_INFO_SIZE);
	dst[0x087] = src->reserved_87;
	for (s = 0; s < NUM_SHIPS; ++s)
		for (c = 0; c < 8; ++c)
			bw_i32le(dst + 0x088 + (s * 8 + c) * 4, src->combat_score[s][c]);
	for (s = 0; s < NUM_SHIPS; ++s)
		memcpy(dst + 0x208 + s * 8, src->combat_complete[s], 8);

	dst[0x268] = src->cur_battle;
	memcpy(dst + 0x269, src->battle_status, NUM_BATTLES);
	memcpy(dst + 0x27D, src->battle_cursor, NUM_BATTLES);
	memcpy(dst + 0x291, src->linked_data, 256);
	memcpy(dst + 0x391, src->secret_complete_bits, NUM_BATTLES);
	memcpy(dst + 0x3A5, src->mission_bonus_bits, NUM_BATTLES);
	memcpy(dst + 0x3B9, src->reserved_3b9, 29);
	memcpy(dst + 0x3D6, src->reserved_3d6, 4);

	for (b = 0; b < NUM_BATTLES; ++b)
		for (m = 0; m < 8; ++m)
			bw_i32le(dst + 0x3DA + (b * 8 + m) * 4, src->tour_score[b][m]);

	bw_u16le(dst + 0x65A, src->total_kills);
	bw_u16le(dst + 0x65C, src->total_captures);
	memcpy(dst + 0x65E, src->reserved_65e, 2);
	for (i = 0; i < 69; ++i)
		bw_u16le(dst + 0x660 + i * 2, src->kills_by_ship_type[i]);
	for (i = 0; i < 69; ++i)
		bw_u16le(dst + 0x6EA + i * 2, src->captures_by_ship_type[i]);

	bw_i32le(dst + 0x774, src->laser_total);
	bw_i32le(dst + 0x778, src->laser_hits);
	memcpy(dst + 0x77C, src->reserved_77c, 4);
	bw_u16le(dst + 0x780, src->warhead_total);
	bw_u16le(dst + 0x782, src->warhead_hits);
	memcpy(dst + 0x784, src->reserved_784, 2);
	bw_u16le(dst + 0x786, src->ejection_count);
}

/* mission.player_status values:
 *   0 = DEAD       -> scene 240
 *   1 = CAPTURED   -> scene 210
 *   2 = rescued/ok -> debrief/tour path (fall-through via goto LABEL_12)
 *   3 = mission ended normally -> debrief/tour */
enum {
	PLAYER_DEAD = 0,
	PLAYER_CAPTURED = 1,
};

/* --- Module globals --- */

PilotRecord pilot_record;

// GLOBAL: TIE95 0xFB3A8
static LandruHandle ship_info[SHIP_INFO_SIZE]; /* paragraph handles */
// PORT: widened runtime storage required by TIE98 registration.
static char pilot_name[TIE_PILOT_NAME_CAPACITY];
/* Matches the "__temp__.tfr" literal the retail binary passes directly
 * to each temp-pilot open/delete call (no runtime setter; it's a fixed
 * filename). RE of the buffer was incomplete — callers asserted in
 * xfile_Open_File when scene 100 ran Delete_Temp_Pilot on startup. */
static char temp_pilot_name[16] = "__temp__.tfr";
// GLOBAL: TIE95 0xFB3E8
static char mission_name[12];
// GLOBAL: TIE95 0xD14D0
static int16_t mission_ship;
// GLOBAL: TIE95 0xD14D2
static int16_t mission_launch;
// GLOBAL: TIE95 0xD14CC
static int16_t mission_outcome;
// GLOBAL: TIE95 0xD14CE
static int16_t mission_officer;
// GLOBAL: TIE95 0xFB3F8
static int16_t battle_medal;
// GLOBAL: TIE95 0xFB3F6
static int16_t battle_secret_medal;
// GLOBAL: TIE95 0xFB3FE
static int16_t blueprint_ship;
// GLOBAL: TIE95 0xFB3FC
static int16_t blueprint_component;
// GLOBAL: TIE95 0xFB400
static int16_t num_blueprint_components;
// GLOBAL: TIE95 0xD14F2
static int16_t num_battle_cutscenes;
// GLOBAL: TIE95 0xD14F4
static int16_t cur_battle_cutscene;
// GLOBAL: TIE95 0xFB350
static int16_t battle_cutscene[32];

/* --- Lifecycle --- */

// FUNCTION: TIE95 0x7FEF4
void shipext_Open_Ships(void) {
	char buf[40];
	void* res;
	LandruFile* fp;
	int16_t i;

	blueprint_component = 0;
	battle_medal = 0;
	battle_secret_medal = 0;
	blueprint_ship = 0;
	memset(ship_info, 0, sizeof(ship_info));

	/* Load ship paragraph resources (ship1.lfd .. ship12.lfd) */
	for (i = 0; i < NUM_SHIPS; i++) {
		snprintf(buf, sizeof(buf), "ship%d.lfd", i + 1);
		fp = shellext_Open_Empire_File(buf, "r");
		if (fp) {
			xfile_Close_File(fp);
			res = shellext_Open_Empire_Resource(buf);
			if (res) {
				snprintf(buf, sizeof(buf), "ship%d", i + 1);
				ship_info[i] = xparagrp_Res_Paragraph(res, buf);
				xres_Close_Resource(res);
			}
		}
	}

	/* Load battle paragraph resources (battle1.lfd .. battle20.lfd) */
	for (i = 0; i < NUM_BATTLES; i++) {
		snprintf(buf, sizeof(buf), "battle%d.lfd", i + 1);
		fp = shellext_Open_Empire_File(buf, "r");
		if (fp) {
			xfile_Close_File(fp);
			res = shellext_Open_Empire_Resource(buf);
			if (res) {
				snprintf(buf, sizeof(buf), "battle%d", i + 1);
				ship_info[i + NUM_SHIPS] = xparagrp_Res_Paragraph(res, buf);
				xres_Close_Resource(res);
			}
		}
	}
}

// FUNCTION: TIE95 0x80034
void shipext_Close_Ships(void) {
	int16_t i;

	for (i = 0; i < NUM_SHIPS; i++) {
		if (ship_info[i])
			xparagrp_Free_Paragraph(ship_info[i]);
		ship_info[i] = LANDRU_NULL_HANDLE;
	}
	for (i = 0; i < NUM_BATTLES; i++) {
		if (ship_info[i + NUM_SHIPS])
			xparagrp_Free_Paragraph(ship_info[i + NUM_SHIPS]);
		ship_info[i + NUM_SHIPS] = LANDRU_NULL_HANDLE;
	}
}

/* --- Ship queries --- */

// FUNCTION: TIE95 0x80110
bool shipext_Is_Ship(int16_t ship_idx) { return ship_info[ship_idx] != LANDRU_NULL_HANDLE; }

// FUNCTION: TIE95 0x80120
bool shipext_Is_Ship_Available(int16_t ship_idx) {
	if (!ship_info[ship_idx])
		return false;

	/* Battle ships require completion prerequisites */
	if (ship_idx >= NUM_SHIPS) {
		int16_t battle = ship_idx - NUM_SHIPS;
		if (!pilot_record.battle_status[battle] || !pilot_record.battle_cursor[battle])
			return false;
	}

	/* Ship 5 (B-Wing) requires specific battle completion or expansion disks */
	if (ship_idx == 5 && pilot_record.battle_status[5] != 3 && !shipext_Is_Mission_Disk1() &&
		!shipext_Is_Mission_Disk2())
		return false;

	/* Ship 6 requires battle 8 complete OR Mission Disk 2 mounted */
	if (ship_idx == 6 && pilot_record.battle_status[8] != 3 && !shipext_Is_Mission_Disk2())
		return false;

	return true;
}

// FUNCTION: TIE95 0x800C0
bool shipext_Is_Mission_Disk1(void) { return ship_info[19] && ship_info[20] && ship_info[21]; }

// FUNCTION: TIE95 0x800E8
bool shipext_Is_Mission_Disk2(void) { return ship_info[22] && ship_info[23] && ship_info[24]; }

// FUNCTION: TIE95 0x807C4
ResFile* shipext_Open_Ship_Resource(int16_t ship_idx) {
	char buf[20];

	if (ship_idx >= NUM_SHIPS)
		snprintf(buf, sizeof(buf), "battle%d.lfd", ship_idx - 11);
	else
		snprintf(buf, sizeof(buf), "ship%d.lfd", ship_idx + 1);
	return shellext_Open_Empire_Resource(buf);
}

// FUNCTION: TIE95 0x807BC
// FUNCTION: TIE98 0x481A40
ResFile* shipext_Open_Launch_Resource(void) { return shipext_Open_Ship_Resource(mission_ship); }

// FUNCTION: TIE95 0x82284
void shipext_Get_Ship_Name(char* out, int16_t ship_idx, int16_t para_type, int16_t para_idx) {
	if (ship_info[ship_idx])
		xparagrp_Get_Paragraph_String(ship_info[ship_idx], out, para_type, para_idx);
	else
		*out = '\0';
}

// FUNCTION: TIE95 0x80768
void shipext_Get_Launch_Name(char* out) { xparagrp_Get_Paragraph_String(ship_info[mission_ship], out, 4, 0); }

// FUNCTION: TIE95 0x80790
void shipext_Get_Weapon_Select_Name(char* out) {
	xparagrp_Get_Paragraph_String(ship_info[mission_ship], out, 4, 1);
}

/* --- Pilot management --- */

// FUNCTION: TIE95 0x801B4
void shipext_Init_Pilot(void) {
	int16_t i;

	memset(&pilot_record, 0, sizeof(PilotRecord));
	pilot_record.game_level = options_gbl.game_level;
	for (i = 0; i < NUM_SHIPS; i++)
		pilot_record.train_max_level[i] = 2;
}

// FUNCTION: TIE95 0x801F4
// PORT: bounded copy into the widened runtime pilot name.
void shipext_Set_Pilot_Name(const char* name) {
	if (!name)
		name = "";
	strncpy(pilot_name, name, sizeof(pilot_name) - 1);
	pilot_name[sizeof(pilot_name) - 1] = '\0';
}

// FUNCTION: TIE95 0x8021C
// PORT: bounded host API.
void shipext_Get_Pilot_Name(char* out, size_t capacity) {
	if (!out || !capacity)
		return;
	strncpy(out, pilot_name, capacity - 1);
	out[capacity - 1] = '\0';
}

// FUNCTION: TIE95 0x80324
void shipext_Revive_Pilot(char* name) {
	int16_t i;

	pilot_record.exit_status = 0;
	for (i = 0; i < NUM_BATTLES; i++) {
		if (pilot_record.battle_status[i] == 2)
			pilot_record.battle_status[i] = 1;
	}
	shipext_Save_Pilot_Data(name);
}

// FUNCTION: TIE95 0x80364
void shipext_Update_Pilot(void) {
	if (pilot_name[0])
		shipext_Save_Pilot_Data(pilot_name);
}

// FUNCTION: TIE95 0x80668
int16_t shipext_Write_Temp_Pilot(void) {
	LandruFile* fp;

	pilot_record.game_level = options_gbl.game_level;
	fp = xfile_Open_File(LANDRU_FILE_ROOT_TEMP, temp_pilot_name, "wb");
	if (fp) {
		uint8_t buf[PILOTRECORD_DISK_SIZE];
		PilotRecord_encode(buf, &pilot_record);
		xfile_Write_Data_To_File(fp, buf, PILOTRECORD_DISK_SIZE);
		xfile_Close_File(fp);
		return 1;
	}
	return 0;
}

// FUNCTION: TIE95 0x80714
void shipext_Delete_Temp_Pilot(void) {
	LandruFile* fp;

	fp = xfile_Open_File(LANDRU_FILE_ROOT_TEMP, temp_pilot_name, "rb");
	if (fp) {
		xfile_Close_File(fp);
		TieStorage_Remove(TIE_FILE_ROOT_TEMP, temp_pilot_name);
	}
}

// FUNCTION: TIE95 0x8073C
void shipext_Link_Pilot(void) {
	int16_t i;

	if (!shipext_Is_Mission_Success())
		return;

	for (i = 0; i < 256; i++)
		pilot_record.linked_data[i] = mission.mission_linked_data[i];
}

// FUNCTION: TIE95 0x82838
LandruFile* shipext_Open_Pilot_File(char* name, char* mode) {
	char path[72];

	strcpy(path, name);
	return xfile_Open_File(LANDRU_FILE_ROOT_USER, path, mode);
}

// FUNCTION: TIE95 0x822B4
void shipext_Set_Mission_Name(const char* name) { strcpy(mission_name, name); }

const char* shipext_Get_Mission_Name(void) { return mission_name; }

/* --- Mission state getters/setters --- */

// FUNCTION: TIE95 0x82018
void shipext_Set_Mission_Ship(int16_t val) { mission_ship = val; }
// FUNCTION: TIE95 0x82020
int16_t shipext_Get_Mission_Ship(void) { return mission_ship; }
// FUNCTION: TIE95 0x82028
void shipext_Set_Mission_Launch(int16_t val) { mission_launch = val; }
// FUNCTION: TIE95 0x82030
int16_t shipext_Is_Mission_Launch(void) { return mission_launch; }
// FUNCTION: TIE95 0x82038
void shipext_Set_Mission_Outcome(int16_t val) { mission_outcome = val; }
// FUNCTION: TIE95 0x82040
int16_t shipext_Get_Mission_Outcome(void) { return mission_outcome; }
// FUNCTION: TIE95 0x820E4
void shipext_Set_Mission_Officer(int16_t val) { mission_officer = val; }
int16_t shipext_Get_Mission_Officer(void) { return mission_officer; }

// FUNCTION: TIE95 0x82048
bool shipext_Is_Mission_Success(void) {
	if (mission.player_status == PLAYER_DEAD || mission.player_status == PLAYER_CAPTURED)
		return false;
	if (mission_officer == 2)
		return mission.secondary_complete == 1;
	return mission.primary_complete == 1;
}

// FUNCTION: TIE95 0x82094
bool shipext_Is_Combat_Mission_Success(void) {
	if (mission_officer == 2)
		return mission.secondary_complete == 1;
	return mission.primary_complete == 1;
}

// FUNCTION: TIE95 0x820C4
bool shipext_Is_Player_OK(void) {
	return mission.player_status != PLAYER_DEAD && mission.player_status != PLAYER_CAPTURED;
}

/* --- Medal state --- */

// FUNCTION: TIE95 0x81FF8
void shipext_Set_TOD_Medal(int16_t val) { battle_medal = val; }
int16_t shipext_Get_TOD_Medal(void) { return battle_medal; }
// FUNCTION: TIE95 0x82008
void shipext_Set_Secret_Medal(int16_t val) { battle_secret_medal = val; }
// FUNCTION: TIE95 0x82010
int16_t shipext_Get_Secret_Medal(void) { return battle_secret_medal; }

/* --- Blueprint browsing --- */

// FUNCTION: TIE95 0x80A58
int16_t shipext_Get_Blueprint_Ship(void) { return blueprint_ship; }
void shipext_Set_Blueprint_Ship(int16_t val) { blueprint_ship = val; }

void shipext_Reset_Battle_Results(void) {
	blueprint_component = 0;
	battle_medal = 0;
	battle_secret_medal = 0;
	blueprint_ship = 0;
}

// FUNCTION: TIE95 0x80A68
void shipext_Next_Blueprint_Component(void) {
	blueprint_component = (blueprint_component + 1) % num_blueprint_components;
}

// FUNCTION: TIE95 0x80A90
void shipext_Last_Blueprint_Component(void) {
	blueprint_component = (num_blueprint_components + blueprint_component - 1) % num_blueprint_components;
}

// FUNCTION: TIE95 0x80AB8
int16_t shipext_Get_Blueprint_Component(void) { return blueprint_component; }

// FUNCTION: TIE95 0x80AC0
void shipext_Set_Num_Blueprint_Components(int16_t count) {
	num_blueprint_components = count;
	blueprint_component = 0;
}

/* --- Training --- */

// FUNCTION: TIE95 0x80EBC
uint8_t shipext_Get_Train_Level(void) { return pilot_record.train_level[pilot_record.cur_train_ship]; }

// FUNCTION: TIE95 0x81070
void shipext_Get_Train_Mission_Text(char* buf, int16_t line) { textext_Get_Train_Text(buf, line); }

int16_t shipext_Num_Train_Mission_Text_Lines(void) { return textext_Count_Train_Text_Lines(); }

/* --- Combat --- */

// FUNCTION: TIE95 0x812C4
uint8_t shipext_Get_Combat_Mission(void) {
	return pilot_record.combat_course_cursor[pilot_record.cur_combat_ship];
}

// FUNCTION: TIE95 0x811B4
bool shipext_Is_Combat_Ship_Tour(void) { return pilot_record.cur_combat_ship >= NUM_SHIPS; }

// FUNCTION: TIE95 0x81460
int16_t shipext_Num_Combat_Missions(void) {
	int16_t para_type = (pilot_record.cur_combat_ship >= NUM_SHIPS) ? 3 : 5;
	return xparagrp_Count_Paragraph_Strings(ship_info[pilot_record.cur_combat_ship], para_type);
}

// FUNCTION: TIE95 0x8149C
void shipext_Get_Combat_Mission_Name(char* out) {
	int16_t para_type = (pilot_record.cur_combat_ship >= NUM_SHIPS) ? 3 : 5;
	xparagrp_Get_Paragraph_String(ship_info[pilot_record.cur_combat_ship], out, para_type,
								  pilot_record.combat_course_cursor[pilot_record.cur_combat_ship]);
}

// FUNCTION: TIE95 0x814F4
void shipext_Get_Combat_Mission_Text(char* out, int16_t line) {
	int16_t base = (pilot_record.cur_combat_ship >= NUM_SHIPS) ? 4 : 6;
	int16_t para_type = pilot_record.combat_course_cursor[pilot_record.cur_combat_ship] + base;
	xparagrp_Get_Paragraph_String(ship_info[pilot_record.cur_combat_ship], out, para_type, line);
}

// FUNCTION: TIE95 0x81548
int16_t shipext_Num_Combat_Mission_Text_Lines(void) {
	int16_t base = (pilot_record.cur_combat_ship >= NUM_SHIPS) ? 4 : 6;
	int16_t para_type = pilot_record.combat_course_cursor[pilot_record.cur_combat_ship] + base;
	return xparagrp_Count_Paragraph_Strings(ship_info[pilot_record.cur_combat_ship], para_type);
}

/* --- Battle/Tour --- */

void shipext_Clear_Battle_Cutscenes(void) {
	cur_battle_cutscene = 0;
	num_battle_cutscenes = 0;
}

// FUNCTION: TIE95 0x82108
void shipext_Add_Battle_Cutscene(int16_t cutscene_id) {
	battle_cutscene[num_battle_cutscenes] = cutscene_id;
	num_battle_cutscenes++;
}

// FUNCTION: TIE95 0x82128
int16_t shipext_Next_Battle_Cutscene(void) { return battle_cutscene[cur_battle_cutscene++]; }

// FUNCTION: TIE95 0x81EF4
uint8_t shipext_Get_Tour_Battle(void) { return pilot_record.cur_battle; }

// FUNCTION: TIE95 0x81EFC
int16_t shipext_Get_Tour_Battle_Size(int16_t battle) {
	return xparagrp_Count_Paragraph_Strings(ship_info[battle + NUM_SHIPS], 3);
}

// FUNCTION: TIE95 0x81DC0
bool shipext_Is_Tour_Battle_End(void) {
	int16_t cur = pilot_record.cur_battle;
	return pilot_record.battle_cursor[cur] == xparagrp_Count_Paragraph_Strings(ship_info[cur + NUM_SHIPS], 3);
}

// FUNCTION: TIE95 0x81DFC
void shipext_Refly_Tour_Mission(void) {
	pilot_record.battle_cursor[pilot_record.cur_battle]--;
	if (pilot_name[0])
		shipext_Save_Pilot_Data(pilot_name);
}

// FUNCTION: TIE95 0x81FC4
void shipext_Get_Battle_Mission_Name(char* out) {
	int16_t cur = pilot_record.cur_battle;
	xparagrp_Get_Paragraph_String(ship_info[cur + NUM_SHIPS], out, 3, pilot_record.battle_cursor[cur]);
}

// FUNCTION: TIE95 0x81650
void shipext_Get_Battle_Ship_Name(char* out) {
	if (ship_info[mission_ship])
		xparagrp_Get_Paragraph_String(ship_info[mission_ship], out, 0, 0);
	else
		*out = '\0';
}

// FUNCTION: TIE95 0x816E8
void shipext_Get_Battle_Galaxy_Name(char* out) {
	xparagrp_Get_Paragraph_String(ship_info[pilot_record.cur_battle + NUM_SHIPS], out, 2, 1);
}

/* Ship name actor slots (for Show_Train/Combat_Ship_Name) */
static Actor* ship_name_actors[SHIP_INFO_SIZE];

/* Blueprint data */
static LandruHandle blueprint_info[4];
static int16_t blueprint_count[4];
static int16_t num_blueprint_ships;

/* Secret battle scene table: (battle, mission) pairs */
/* Secret battle scenes: (battle, mission) pairs that trigger secret cutscenes 21-28.
 * Entries with -1 are unused sentinels. */
static struct {
	int16_t battle;
	int16_t mission;
} secret_battle_scene[8] = {
	{ 4, 1 }, { 5, 1 }, { 6, 0 }, { 9, 2 }, { 12, 2 }, { -1, -1 }, { -1, -1 }, { -1, -1 },
};

/* --- Helper: parse space-separated integers from a paragraph string --- */
__attribute__((unused)) static void parse_position(const char* str, int32_t* out_x, int32_t* out_y,
												   int32_t* out_z) {
	int16_t pos = 0;
	while (str[pos] == ' ')
		pos++;
	*out_x = atol(&str[pos]);
	while (str[pos] && str[pos] != ' ')
		pos++;
	while (str[pos] == ' ')
		pos++;
	*out_y = atol(&str[pos]);
	while (str[pos] && str[pos] != ' ')
		pos++;
	while (str[pos] == ' ')
		pos++;
	*out_z = atol(&str[pos]);
}

/* --- Helper: find blueprint category and offset for a blueprint_ship >= NUM_SHIPS --- */
static void find_blueprint_cat(int16_t ship_offset, int16_t* out_cat, int16_t* out_offset) {
	int16_t cat = 0;
	int16_t remaining = ship_offset - NUM_SHIPS;
	*out_cat = 0;
	*out_offset = 0;
	while (cat < 4) {
		if (remaining < blueprint_count[cat]) {
			if (blueprint_info[cat]) {
				*out_cat = cat;
				*out_offset = remaining;
			}
			break;
		}
		remaining -= blueprint_count[cat];
		cat++;
	}
}

/* --- Pilot save/load --- */

// FUNCTION: TIE95 0x80244
bool shipext_Load_Pilot(const char* name) {
	char path[40];
	LandruFile* fp;

	uint8_t buf[PILOTRECORD_DISK_SIZE];

	strcpy(path, name);
	strcat(path, ".tfr");
	options_gbl.game_level = 1;
	fp = xfile_Open_File(LANDRU_FILE_ROOT_USER, path, "rb");
	if (!fp)
		return false;

	xfile_Read_Data_From_File(fp, buf, PILOTRECORD_DISK_SIZE);
	xfile_Close_File(fp);
	PilotRecord_decode(&pilot_record, buf);
	shipext_Set_Pilot_Name(name);
	options_gbl.game_level = pilot_record.game_level;
	return true;
}

// FUNCTION: TIE95 0x8030C
bool shipext_Create_Pilot(const char* name) {
	int16_t i;

	memset(&pilot_record, 0, sizeof(PilotRecord));
	pilot_record.game_level = options_gbl.game_level;
	for (i = 0; i < NUM_SHIPS; i++)
		pilot_record.train_max_level[i] = 2;
	shipext_Save_Pilot_Data(name);
	return true;
}

/* Two-slot disk image: primary at +0, backup at +PILOTRECORD_DISK_SIZE. */
enum {
	TFR_FILE_SIZE = (2u * PILOTRECORD_DISK_SIZE),
	TFR_BACKUP_OFFSET = PILOTRECORD_DISK_SIZE,
};

/* game_level lives at offset 3 within each PilotRecord slot. */
enum {
	TFR_GAME_LEVEL_OFFSET = 3u,
};

// FUNCTION: TIE95 0x80374
void shipext_Save_Pilot_Data(const char* name) {
	char path[40];
	uint8_t buf[TFR_FILE_SIZE];
	LandruFile* fp;

	strcpy(path, name);
	strcat(path, ".tfr");
	pilot_record.game_level = options_gbl.game_level;

	/* Try reading existing file (preserves backup slot) */
	fp = xfile_Open_File(LANDRU_FILE_ROOT_USER, path, "rb");
	if (fp) {
		xfile_Read_Data_From_File(fp, buf, sizeof(buf));
		xfile_Close_File(fp);
		buf[TFR_BACKUP_OFFSET + TFR_GAME_LEVEL_OFFSET] = options_gbl.game_level;
	} else {
		/* No existing file -- mirror current record into backup slot */
		PilotRecord_encode(buf + TFR_BACKUP_OFFSET, &pilot_record);
	}

	/* Encode current record into primary slot */
	PilotRecord_encode(buf, &pilot_record);

	fp = xfile_Open_File(LANDRU_FILE_ROOT_USER, path, "wb");
	if (fp) {
		xfile_Write_Data_To_File(fp, buf, sizeof(buf));
		xfile_Close_File(fp);
	}
}

// FUNCTION: TIE95 0x8048C
void shipext_Backup_Pilot(void) {
	char path[40];
	uint8_t buf[TFR_FILE_SIZE];
	LandruFile* fp;

	strcpy(path, pilot_name);
	strcat(path, ".tfr");
	pilot_record.game_level = options_gbl.game_level;

	PilotRecord_encode(buf, &pilot_record);
	PilotRecord_encode(buf + TFR_BACKUP_OFFSET, &pilot_record);

	fp = xfile_Open_File(LANDRU_FILE_ROOT_USER, path, "wb");
	if (fp) {
		xfile_Write_Data_To_File(fp, buf, sizeof(buf));
		xfile_Close_File(fp);
	}
}

// FUNCTION: TIE95 0x80560
void shipext_Restore_Pilot(void) {
	char path[40];
	uint8_t buf[TFR_FILE_SIZE];
	LandruFile* fp;

	strcpy(path, pilot_name);
	strcat(path, ".tfr");

	fp = xfile_Open_File(LANDRU_FILE_ROOT_USER, path, "rb");
	if (fp) {
		xfile_Read_Data_From_File(fp, buf, sizeof(buf));
		xfile_Close_File(fp);
	}

	/* Copy game_level byte from primary slot to backup slot, then
	 * promote backup to primary in the on-disk image and decode it
	 * into the live pilot_record. */
	buf[TFR_BACKUP_OFFSET + TFR_GAME_LEVEL_OFFSET] = buf[TFR_GAME_LEVEL_OFFSET];
	memcpy(buf, buf + TFR_BACKUP_OFFSET, PILOTRECORD_DISK_SIZE);
	PilotRecord_decode(&pilot_record, buf + TFR_BACKUP_OFFSET);

	fp = xfile_Open_File(LANDRU_FILE_ROOT_USER, path, "wb");
	if (fp) {
		xfile_Write_Data_To_File(fp, buf, sizeof(buf));
		xfile_Close_File(fp);
	}
}

// FUNCTION: TIE95 0x806A8
bool shipext_Read_Temp_Pilot(void) {
	LandruFile* fp;

	pilot_record.game_level = options_gbl.game_level;
	fp = xfile_Open_File(LANDRU_FILE_ROOT_TEMP, temp_pilot_name, "rb");
	if (fp) {
		uint8_t buf[PILOTRECORD_DISK_SIZE];
		if (!xfile_Read_Data_From_File(fp, buf, PILOTRECORD_DISK_SIZE)) {
			/* Read failed -- reload from the .tfr file */
			char name_buf[TIE_PILOT_NAME_CAPACITY];
			xfile_Close_File(fp);
			strcpy(name_buf, pilot_name);
			shipext_Load_Pilot(name_buf);
			return false;
		}
		PilotRecord_decode(&pilot_record, buf);
		xfile_Close_File(fp);
		return true;
	}
	return false;
}

/* --- Blueprint browsing --- */

// FUNCTION: TIE95 0x807E0
void shipext_Open_Blueprint_Ships(void) {
	char buf[40];
	LandruFile* fp;
	ResFile* res;
	int16_t i, para_count;

	num_blueprint_ships = 0;
	for (i = 0; i < 4; i++) {
		blueprint_info[i] = LANDRU_NULL_HANDLE;
		blueprint_count[i] = 0;
	}
	for (i = 0; i < 4; i++) {
		snprintf(buf, sizeof(buf), "shipset%d.lfd", i + 1);
		fp = shellext_Open_Empire_File(buf, "r");
		if (fp) {
			xfile_Close_File(fp);
			res = shellext_Open_Empire_Resource(buf);
			if (res) {
				snprintf(buf, sizeof(buf), "shipset%d", i + 1);
				blueprint_info[i] = xparagrp_Res_Paragraph(res, buf);
				xres_Close_Resource(res);
				if (blueprint_info[i]) {
					para_count = xparagrp_Count_Paragraphs(blueprint_info[i]);
					blueprint_count[i] = para_count;
					num_blueprint_ships += para_count;
				}
			}
		}
	}
}

// FUNCTION: TIE95 0x808D0
void shipext_Close_Blueprint_Ships(void) {
	int16_t i;
	for (i = 0; i < 4; i++) {
		if (blueprint_info[i])
			xparagrp_Free_Paragraph(blueprint_info[i]);
		blueprint_info[i] = LANDRU_NULL_HANDLE;
	}
}

// FUNCTION: TIE95 0x80918
void shipext_Next_Blueprint_Ship(void) {
	int16_t total = num_blueprint_ships + NUM_SHIPS;
	int16_t cat, offset;
	do {
		blueprint_ship = (blueprint_ship + 1) % total;
		if (blueprint_ship < NUM_SHIPS) {
			if (shipext_Is_Ship_Available(blueprint_ship))
				break;
		} else {
			find_blueprint_cat(blueprint_ship, &cat, &offset);
			if (blueprint_info[cat])
				break;
		}
	} while (1);
	shipext_Get_Blueprint_Ship_SHP();
}

// FUNCTION: TIE95 0x809B8
void shipext_Last_Blueprint_Ship(void) {
	int16_t total = num_blueprint_ships + NUM_SHIPS;
	int16_t cat, offset;
	do {
		blueprint_ship = (total + blueprint_ship - 1) % total;
		if (blueprint_ship < NUM_SHIPS) {
			if (shipext_Is_Ship_Available(blueprint_ship))
				break;
		} else {
			find_blueprint_cat(blueprint_ship, &cat, &offset);
			if (blueprint_info[cat])
				break;
		}
	} while (1);
	shipext_Get_Blueprint_Ship_SHP();
}

// FUNCTION: TIE95 0x80AD4
void shipext_Get_Blueprint_Ship_SHP(void) {
	char lfd_name[16], shp_name[16];
	int16_t cat, offset;

	if (blueprint_ship < NUM_SHIPS) {
		if (!ship_info[blueprint_ship])
			return;
		xparagrp_Get_Paragraph_String(ship_info[blueprint_ship], lfd_name, 3, 0);
		xparagrp_Get_Paragraph_String(ship_info[blueprint_ship], shp_name, 3, 1);
	} else {
		find_blueprint_cat(blueprint_ship, &cat, &offset);
		if (!blueprint_info[cat])
			return;
		if (cat)
			snprintf(lfd_name, sizeof(lfd_name), "species%d.lfd", cat + 1);
		else
			strcpy(lfd_name, "species.lfd");
		xparagrp_Get_Paragraph_String(blueprint_info[cat], shp_name, offset, 1);
	}
	bpflight_Load_Flight_Craft(lfd_name, shp_name, 0);
}

// FUNCTION: TIE95 0x80BB8
void shipext_Get_Blueprint_Ship_Name(char* out) {
	int16_t cat, offset;

	if (blueprint_ship < NUM_SHIPS) {
		if (ship_info[blueprint_ship])
			xparagrp_Get_Paragraph_String(ship_info[blueprint_ship], out, 0, 0);
		else
			*out = '\0';
	} else {
		find_blueprint_cat(blueprint_ship, &cat, &offset);
		if (blueprint_info[cat])
			xparagrp_Get_Paragraph_String(blueprint_info[cat], out, offset, 0);
		else
			*out = '\0';
	}
}

// FUNCTION: TIE95 0x80C1C
void shipext_Get_Blueprint_Ship_Line(char* out, int16_t line) {
	int16_t cat, offset, str_idx;

	str_idx = line + 2;
	if (blueprint_ship < NUM_SHIPS) {
		xparagrp_Get_Paragraph_String(ship_info[blueprint_ship], out, 3, str_idx);
	} else {
		find_blueprint_cat(blueprint_ship, &cat, &offset);
		xparagrp_Get_Paragraph_String(blueprint_info[cat], out, offset, str_idx);
	}
}

// FUNCTION: TIE95 0x80C80
int16_t shipext_Get_Num_Blueprint_Ship_Lines(void) {
	int16_t cat, offset;
	int16_t total;

	if (blueprint_ship < NUM_SHIPS)
		total = xparagrp_Count_Paragraph_Strings(ship_info[blueprint_ship], 3);
	else {
		find_blueprint_cat(blueprint_ship, &cat, &offset);
		total = xparagrp_Count_Paragraph_Strings(blueprint_info[cat], offset);
	}
	/* Drop the 2 leading paragraphs (ship name + class) that the caller
	 * renders separately as the info-screen header. */
	return total - 2;
}

// FUNCTION: TIE95 0x80CD4
void shipext_Get_Blueprint_Index(int16_t* out_category, int16_t* out_offset) {
	find_blueprint_cat(blueprint_ship, out_category, out_offset);
}

/* --- Training --- */

// FUNCTION: TIE95 0x80D34
void shipext_Get_Train_Ship_Name(char* out) {
	if (ship_info[pilot_record.cur_train_ship])
		xparagrp_Get_Paragraph_String(ship_info[pilot_record.cur_train_ship], out, 0, 0);
	else
		*out = '\0';
}

// FUNCTION: TIE95 0x80D4C
void shipext_Next_Train_Ship(void) {
	char shp_name[16], shp_model[16];
	do {
		pilot_record.cur_train_ship = (pilot_record.cur_train_ship + 1) % NUM_SHIPS;
	} while (!shipext_Is_Ship_Available(pilot_record.cur_train_ship));
	shipext_Show_Train_Ship_Name();
	if (ship_info[pilot_record.cur_train_ship]) {
		xparagrp_Get_Paragraph_String(ship_info[pilot_record.cur_train_ship], shp_name, 1, 0);
		xparagrp_Get_Paragraph_String(ship_info[pilot_record.cur_train_ship], shp_model, 1, 1);
		bpflight_Load_Flight_Craft(shp_name, shp_model, 0);
	}
}

// FUNCTION: TIE95 0x80DA0
void shipext_Last_Train_Ship(void) {
	char shp_name[16], shp_model[16];
	do {
		pilot_record.cur_train_ship = (pilot_record.cur_train_ship + NUM_SHIPS - 1) % NUM_SHIPS;
	} while (!shipext_Is_Ship_Available(pilot_record.cur_train_ship));
	shipext_Show_Train_Ship_Name();
	if (ship_info[pilot_record.cur_train_ship]) {
		xparagrp_Get_Paragraph_String(ship_info[pilot_record.cur_train_ship], shp_name, 1, 0);
		xparagrp_Get_Paragraph_String(ship_info[pilot_record.cur_train_ship], shp_model, 1, 1);
		bpflight_Load_Flight_Craft(shp_name, shp_model, 0);
	}
}

// FUNCTION: TIE95 0x80DEC
uint8_t shipext_Get_Train_Ship(void) {
	if (!shipext_Is_Ship_Available(pilot_record.cur_train_ship)) {
		pilot_record.train_level[pilot_record.cur_train_ship] = 0;
		pilot_record.cur_train_ship = 0;
	}
	return pilot_record.cur_train_ship;
}

// FUNCTION: TIE95 0x80E1C
void shipext_Next_Train_Level(void) {
	int16_t cur = pilot_record.cur_train_ship;
	pilot_record.train_level[cur] = (pilot_record.train_level[cur] + 1) % 16;
	if (pilot_record.train_level[cur] > pilot_record.train_max_level[cur])
		pilot_record.train_level[cur] = 0;
}

// FUNCTION: TIE95 0x80E68
void shipext_Last_Train_Level(void) {
	int16_t cur = pilot_record.cur_train_ship;
	pilot_record.train_level[cur] = (pilot_record.train_level[cur] + 15) % 16;
	if (pilot_record.train_level[cur] > pilot_record.train_max_level[cur])
		pilot_record.train_level[cur] = pilot_record.train_max_level[cur];
}

// FUNCTION: TIE95 0x80ECC
void shipext_Init_Train_Ship_Name(void) {
	int16_t i;
	for (i = 0; i < NUM_SHIPS; i++)
		ship_name_actors[i] = NULL;
	shipext_Show_Train_Ship_Name();
}

// FUNCTION: TIE95 0x80EFC
void shipext_Show_Train_Ship_Name(void) {
	Rect bounds;
	int16_t i, cur;
	char name[16];

	xcanvas_Get_Drawing_Canvas_Bounds(&bounds);
	for (i = 0; i < NUM_SHIPS; i++)
		if (ship_name_actors[i])
			xactor_Hide_Actor(ship_name_actors[i]);

	cur = pilot_record.cur_train_ship;
	if (ship_name_actors[cur]) {
		xactor_Show_Actor(ship_name_actors[cur]);
	} else {
		ResFile* res = shipext_Open_Ship_Resource(cur);
		if (res) {
			snprintf(name, sizeof(name), "train%d", cur + 1);
			ship_name_actors[cur] = xactdelt_Res_Delta_Actor(name, &bounds, 1, 0, 14);
			xactor_Set_Actor_Time(ship_name_actors[cur], -1, -1);
			xactor_Show_Actor(ship_name_actors[cur]);
			xres_Close_Resource(res);
		}
	}
}

// FUNCTION: TIE95 0x80FC8
void shipext_Get_Train_Ship_SHP(void) {
	char shp_name[16], shp_model[16];
	if (ship_info[pilot_record.cur_train_ship]) {
		xparagrp_Get_Paragraph_String(ship_info[pilot_record.cur_train_ship], shp_name, 1, 0);
		xparagrp_Get_Paragraph_String(ship_info[pilot_record.cur_train_ship], shp_model, 1, 1);
		bpflight_Load_Flight_Craft(shp_name, shp_model, 0);
	}
}

// FUNCTION: TIE95 0x81030
void shipext_Get_Train_Course_SHP(void) { bpflight_Load_Flight_Craft("species.lfd", "obstrt", 1); }

// FUNCTION: TIE95 0x8104C
void shipext_Get_Train_Ship_Pos(int32_t* out_x, int32_t* out_y, int32_t* out_z) {
	char buf[40];
	int16_t cat, offset;

	if (pilot_record.cur_train_ship < NUM_SHIPS) {
		if (!ship_info[pilot_record.cur_train_ship]) {
			*out_x = 0;
			*out_y = 0;
			*out_z = 30;
			return;
		}
		xparagrp_Get_Paragraph_String(ship_info[pilot_record.cur_train_ship], buf, 1, 2);
	} else {
		find_blueprint_cat(pilot_record.cur_train_ship, &cat, &offset);
		if (!blueprint_info[cat]) {
			*out_x = 0;
			*out_y = 0;
			*out_z = 30;
			return;
		}
		xparagrp_Get_Paragraph_String(blueprint_info[cat], buf, offset, 2);
	}
	parse_position(buf, out_x, out_y, out_z);
}

/* --- Combat --- */

// FUNCTION: TIE95 0x81448
void shipext_Get_Combat_Ship_Name(char* out) {
	if (ship_info[pilot_record.cur_combat_ship])
		xparagrp_Get_Paragraph_String(ship_info[pilot_record.cur_combat_ship], out, 0, 0);
	else
		*out = '\0';
}

// FUNCTION: TIE95 0x81080
void shipext_Next_Combat_Ship(void) {
	char mission_str[64], shp_name[16], shp_model[16];
	int16_t para_section;

	do {
		pilot_record.cur_combat_ship = (pilot_record.cur_combat_ship + 1) % SHIP_INFO_SIZE;
	} while (!shipext_Is_Ship_Available(pilot_record.cur_combat_ship));

	para_section = (pilot_record.cur_combat_ship >= NUM_SHIPS) ? 3 : 5;
	xparagrp_Get_Paragraph_String(ship_info[pilot_record.cur_combat_ship], mission_str, para_section,
								  pilot_record.combat_course_cursor[pilot_record.cur_combat_ship]);
	strcpy(mission_name, mission_str);

	if (pilot_record.cur_combat_ship >= NUM_SHIPS)
		shipext_Find_Mission_Ship();
	else
		mission_ship = pilot_record.cur_combat_ship;

	shipext_Show_Combat_Ship_Name();
	if (ship_info[pilot_record.cur_combat_ship]) {
		int16_t ms = shipext_Get_Mission_Ship();
		xparagrp_Get_Paragraph_String(ship_info[ms], shp_name, 2, 0);
		xparagrp_Get_Paragraph_String(ship_info[ms], shp_model, 2, 1);
		bpflight_Load_Flight_Craft(shp_name, shp_model, 0);
	}
}

// FUNCTION: TIE95 0x81100
void shipext_Last_Combat_Ship(void) {
	char mission_str[64], shp_name[16], shp_model[16];
	int16_t para_section;

	do {
		pilot_record.cur_combat_ship = (pilot_record.cur_combat_ship + SHIP_INFO_SIZE - 1) % SHIP_INFO_SIZE;
	} while (!shipext_Is_Ship_Available(pilot_record.cur_combat_ship));

	para_section = (pilot_record.cur_combat_ship >= NUM_SHIPS) ? 3 : 5;
	xparagrp_Get_Paragraph_String(ship_info[pilot_record.cur_combat_ship], mission_str, para_section,
								  pilot_record.combat_course_cursor[pilot_record.cur_combat_ship]);
	strcpy(mission_name, mission_str);

	if (pilot_record.cur_combat_ship >= NUM_SHIPS)
		shipext_Find_Mission_Ship();
	else
		mission_ship = pilot_record.cur_combat_ship;

	shipext_Show_Combat_Ship_Name();
	if (ship_info[pilot_record.cur_combat_ship]) {
		int16_t ms = shipext_Get_Mission_Ship();
		xparagrp_Get_Paragraph_String(ship_info[ms], shp_name, 2, 0);
		xparagrp_Get_Paragraph_String(ship_info[ms], shp_model, 2, 1);
		bpflight_Load_Flight_Craft(shp_name, shp_model, 0);
	}
}

// FUNCTION: TIE95 0x81184
uint8_t shipext_Get_Combat_Ship(void) {
	if (!shipext_Is_Ship_Available(pilot_record.cur_combat_ship)) {
		pilot_record.combat_course_cursor[pilot_record.cur_combat_ship] = 0;
		pilot_record.cur_combat_ship = 0;
	}
	return pilot_record.cur_combat_ship;
}

// FUNCTION: TIE95 0x812D4
void shipext_Init_Combat_Ship_Name(void) {
	int16_t i;
	for (i = 0; i < NUM_SHIPS; i++)
		ship_name_actors[i] = NULL;
	shipext_Show_Combat_Ship_Name();
}

// FUNCTION: TIE95 0x81304
void shipext_Show_Combat_Ship_Name(void) {
	Rect bounds;
	int16_t i;
	char name[16];

	xcanvas_Get_Drawing_Canvas_Bounds(&bounds);
	for (i = 0; i < NUM_SHIPS; i++)
		if (ship_name_actors[i])
			xactor_Hide_Actor(ship_name_actors[i]);

	if (ship_name_actors[mission_ship]) {
		xactor_Show_Actor(ship_name_actors[mission_ship]);
	} else {
		ResFile* res = shipext_Open_Ship_Resource(mission_ship);
		if (res) {
			snprintf(name, sizeof(name), "ship%d", mission_ship + 1);
			ship_name_actors[mission_ship] = xactdelt_Res_Delta_Actor(name, &bounds, 0, 0, 19);
			xactor_Set_Actor_Time(ship_name_actors[mission_ship], -1, -1);
			xactor_Show_Actor(ship_name_actors[mission_ship]);
			xres_Close_Resource(res);
		}
	}
}

// FUNCTION: TIE95 0x813B8
void shipext_Get_Combat_Ship_SHP(void) {
	char shp_name[16], shp_model[16];
	if (ship_info[pilot_record.cur_combat_ship]) {
		xparagrp_Get_Paragraph_String(ship_info[mission_ship], shp_name, 2, 0);
		xparagrp_Get_Paragraph_String(ship_info[mission_ship], shp_model, 2, 1);
		bpflight_Load_Flight_Craft(shp_name, shp_model, 0);
	}
}

// FUNCTION: TIE95 0x82148
void shipext_Get_Combat_Ship_Pos(int32_t* out_x, int32_t* out_y, int32_t* out_z) {
	char buf[40];
	int16_t cat, offset;

	if (mission_ship < NUM_SHIPS) {
		if (!ship_info[mission_ship]) {
			*out_x = 0;
			*out_y = 0;
			*out_z = 30;
			return;
		}
		xparagrp_Get_Paragraph_String(ship_info[mission_ship], buf, 2, 2);
	} else {
		find_blueprint_cat(mission_ship, &cat, &offset);
		if (!blueprint_info[cat]) {
			*out_x = 0;
			*out_y = 0;
			*out_z = 30;
			return;
		}
		xparagrp_Get_Paragraph_String(blueprint_info[cat], buf, offset, 2);
	}
	parse_position(buf, out_x, out_y, out_z);
}

// FUNCTION: TIE95 0x811C4
void shipext_Next_Combat_Mission(void) {
	char mission_str[64], shp_name[16], shp_model[16];
	int16_t para_section, mission_count, cur;

	para_section = (pilot_record.cur_combat_ship >= NUM_SHIPS) ? 3 : 5;
	mission_count = xparagrp_Count_Paragraph_Strings(ship_info[pilot_record.cur_combat_ship], para_section);
	cur = pilot_record.cur_combat_ship;
	pilot_record.combat_course_cursor[cur] = (pilot_record.combat_course_cursor[cur] + 1) % mission_count;

	/* For battle slots (cur >= NUM_SHIPS) cap the cursor at battle_cursor
	 * (pilot's current mission within the battle). Retail indexes via
	 * byte_FAE39 = &battle_cursor[0] offset by -12 so byte_FAE39[cur]
	 * resolves to battle_cursor[cur - 12]. */
	if (cur >= NUM_SHIPS &&
		pilot_record.combat_course_cursor[cur] >= pilot_record.battle_cursor[cur - NUM_SHIPS])
		pilot_record.combat_course_cursor[cur] = 0;

	para_section = (pilot_record.cur_combat_ship >= NUM_SHIPS) ? 3 : 5;
	xparagrp_Get_Paragraph_String(ship_info[pilot_record.cur_combat_ship], mission_str, para_section,
								  pilot_record.combat_course_cursor[pilot_record.cur_combat_ship]);
	strcpy(mission_name, mission_str);

	if (cur >= NUM_SHIPS)
		shipext_Find_Mission_Ship();

	shipext_Show_Combat_Ship_Name();
	if (ship_info[pilot_record.cur_combat_ship]) {
		int16_t ms = shipext_Get_Mission_Ship();
		xparagrp_Get_Paragraph_String(ship_info[ms], shp_name, 2, 0);
		xparagrp_Get_Paragraph_String(ship_info[ms], shp_model, 2, 1);
		bpflight_Load_Flight_Craft(shp_name, shp_model, 0);
	}
}

// FUNCTION: TIE95 0x81240
void shipext_Last_Combat_Mission(void) {
	char mission_str[64], shp_name[16], shp_model[16];
	int16_t para_section, mission_count, cur;

	para_section = (pilot_record.cur_combat_ship >= NUM_SHIPS) ? 3 : 5;
	mission_count = xparagrp_Count_Paragraph_Strings(ship_info[pilot_record.cur_combat_ship], para_section);
	cur = pilot_record.cur_combat_ship;
	pilot_record.combat_course_cursor[cur] =
		(mission_count + pilot_record.combat_course_cursor[cur] - 1) % mission_count;

	/* See Next_Combat_Mission for the battle_cursor cap rationale. */
	if (cur >= NUM_SHIPS &&
		pilot_record.combat_course_cursor[cur] >= pilot_record.battle_cursor[cur - NUM_SHIPS])
		pilot_record.combat_course_cursor[cur] = pilot_record.battle_cursor[cur - NUM_SHIPS] - 1;

	para_section = (pilot_record.cur_combat_ship >= NUM_SHIPS) ? 3 : 5;
	xparagrp_Get_Paragraph_String(ship_info[pilot_record.cur_combat_ship], mission_str, para_section,
								  pilot_record.combat_course_cursor[pilot_record.cur_combat_ship]);
	strcpy(mission_name, mission_str);

	if (cur >= NUM_SHIPS)
		shipext_Find_Mission_Ship();

	shipext_Show_Combat_Ship_Name();
	if (ship_info[pilot_record.cur_combat_ship]) {
		int16_t ms = shipext_Get_Mission_Ship();
		xparagrp_Get_Paragraph_String(ship_info[ms], shp_name, 2, 0);
		xparagrp_Get_Paragraph_String(ship_info[ms], shp_model, 2, 1);
		bpflight_Load_Flight_Craft(shp_name, shp_model, 0);
	}
}

/* --- Battle/Tour --- */

// FUNCTION: TIE95 0x81590
void shipext_Get_Battle_Title(char* out, int16_t mode) {
	int16_t battle_idx = pilot_record.cur_battle + NUM_SHIPS;
	int16_t str_idx;

	if (mode) {
		if (pilot_record.battle_status[pilot_record.cur_battle] == 3)
			str_idx = mode + 1;
		else
			str_idx = mode - 1;
		xparagrp_Get_Paragraph_String(ship_info[battle_idx], out, 1, str_idx);
	} else {
		if (pilot_record.battle_status[pilot_record.cur_battle] == 3)
			xparagrp_Get_Paragraph_String(ship_info[battle_idx], out, 0, 1);
		else
			xparagrp_Get_Paragraph_String(ship_info[battle_idx], out, 0, 0);
	}
}

// FUNCTION: TIE95 0x81670
Actor* shipext_Get_Battle_Galaxy_Image(void) {
	char name[32];
	Rect bounds;
	int16_t battle_idx = pilot_record.cur_battle + NUM_SHIPS;

	ResFile* res;
	Actor* actor;

	xparagrp_Get_Paragraph_String(ship_info[battle_idx], name, 2, 0);
	res = shipext_Open_Ship_Resource(battle_idx);
	xcanvas_Get_Drawing_Canvas_Bounds(&bounds);
	actor = xactdelt_Res_Delta_Actor(name, &bounds, 0, 0, 0);
	xactor_Set_Actor_Time(actor, -1, -1);
	xres_Close_Resource(res);
	return actor;
}

// FUNCTION: TIE95 0x81720
void shipext_Get_Battle_Galaxy_Rect(Rect* out) {
	char buf[44];
	int16_t pos;

	xparagrp_Get_Paragraph_String(ship_info[pilot_record.cur_battle + NUM_SHIPS], buf, 2, 2);
	pos = 0;
	while (buf[pos] == ' ')
		pos++;
	out->left = atoi(&buf[pos]);
	while (buf[pos] && buf[pos] != ' ')
		pos++;
	while (buf[pos] == ' ')
		pos++;
	out->top = atoi(&buf[pos]);
	while (buf[pos] && buf[pos] != ' ')
		pos++;
	while (buf[pos] == ' ')
		pos++;
	out->right = atoi(&buf[pos]);
	while (buf[pos] && buf[pos] != ' ')
		pos++;
	while (buf[pos] == ' ')
		pos++;
	out->bottom = atoi(&buf[pos]);
}

/* Battle prerequisite gate. Tour-progress chain:
 *   battle > 6  -> requires battle_status[6] == 3 (Tour 1 final)
 *   battle > 9  -> requires battle_status[9] == 3 (Tour 2 final)
 *   The second condition replaces the first.
 *
 * Per-battle prereqs (additional to the chain):
 *   case 4    : battles 0+1 complete                (chain not used)
 *   case 5    : battles 2+3 complete                (chain not used)
 *   case 6    : battles 4+5 complete                (chain not used)
 *   case 7,8  : chain || (status[7] in {1,3}) || (status[8] in {1,3})
 *   case 9    : battles 7+8 complete                (chain not used)
 *   case 12   : chain && battles 10+11 complete
 *   case 15   : chain && battles 13+14 complete
 *   default   : chain only
 */
static bool battle_prereqs_with_status_gate(int16_t battle, bool cur_status_ok) {
	bool chain;

	if (!ship_info[battle + NUM_SHIPS])
		return false;
	if (!cur_status_ok)
		return false;

	chain = true;
	if (battle > 6)
		chain = (pilot_record.battle_status[6] == 3);
	if (battle > 9)
		chain = (pilot_record.battle_status[9] == 3);

	switch (battle) {
		case 4:
			return pilot_record.battle_status[0] == 3 && pilot_record.battle_status[1] == 3;
		case 5:
			return pilot_record.battle_status[2] == 3 && pilot_record.battle_status[3] == 3;
		case 6:
			return pilot_record.battle_status[4] == 3 && pilot_record.battle_status[5] == 3;
		case 7:
		case 8: {
			uint8_t s7 = pilot_record.battle_status[7];
			uint8_t s8 = pilot_record.battle_status[8];
			return chain || s7 == 3 || s8 == 3 || s7 == 1 || s8 == 1;
		}
		case 9:
			return pilot_record.battle_status[7] == 3 && pilot_record.battle_status[8] == 3;
		case 12:
			return chain && pilot_record.battle_status[10] == 3 && pilot_record.battle_status[11] == 3;
		case 15:
			return chain && pilot_record.battle_status[13] == 3 && pilot_record.battle_status[14] == 3;
		default:
			return chain;
	}
}

static bool check_battle_prereqs(int16_t battle) {
	int16_t cur = pilot_record.cur_battle;
	return battle_prereqs_with_status_gate(battle, pilot_record.battle_status[cur] != 2);
}

static bool check_battle_incomplete(int16_t battle) {
	int16_t cur = pilot_record.cur_battle;
	return battle_prereqs_with_status_gate(battle, pilot_record.battle_status[cur] < 2);
}

// FUNCTION: TIE95 0x81828
int16_t shipext_Find_Battle(void) {
	int16_t start = pilot_record.cur_battle;
	if (check_battle_incomplete(start))
		return 1;
	do {
		pilot_record.cur_battle = (pilot_record.cur_battle + 1) % NUM_BATTLES;
	} while (pilot_record.cur_battle != start && !check_battle_incomplete(pilot_record.cur_battle));
	return check_battle_incomplete(pilot_record.cur_battle);
}

// FUNCTION: TIE95 0x81884
bool shipext_Next_Battle(void) {
	do {
		pilot_record.cur_battle = (pilot_record.cur_battle + 1) % NUM_BATTLES;
	} while (!check_battle_prereqs(pilot_record.cur_battle));
	return check_battle_prereqs(pilot_record.cur_battle);
}

// FUNCTION: TIE95 0x818DC
bool shipext_Last_Battle(void) {
	do {
		pilot_record.cur_battle = (pilot_record.cur_battle + NUM_BATTLES - 1) % NUM_BATTLES;
	} while (!check_battle_prereqs(pilot_record.cur_battle));
	return check_battle_prereqs(pilot_record.cur_battle);
}

// FUNCTION: TIE95 0x81968
bool shipext_Valid_Incomplete_Battle(int16_t battle) { return check_battle_incomplete(battle); }

// FUNCTION: TIE95 0x81BAC
bool shipext_Valid_Battle(int16_t battle) { return check_battle_prereqs(battle); }

// FUNCTION: TIE95 0x81E14
int16_t shipext_Set_Tour_Battle(void) {
	int16_t cur = pilot_record.cur_battle;
	char mission_str[64];

	if (!pilot_record.battle_status[cur]) {
		pilot_record.battle_status[cur] = 1;
		pilot_record.battle_cursor[cur] = 0;
	}

	if (pilot_record.battle_cursor[cur] == xparagrp_Count_Paragraph_Strings(ship_info[cur + NUM_SHIPS], 3)) {
		/* All missions done — mark battle complete */
		pilot_record.battle_status[cur] = 3;
		if (pilot_name[0])
			shipext_Save_Pilot_Data(pilot_name);
		return 0;
	}

	xparagrp_Get_Paragraph_String(ship_info[cur + NUM_SHIPS], mission_str, 3,
								  pilot_record.battle_cursor[cur]);
	strcpy(mission_name, mission_str);
	shipext_Find_Mission_Ship();
	return 1;
}

// FUNCTION: TIE95 0x81E94
void shipext_Validate_Tour_Battle(void) {
	int16_t cur;

	if (!pilot_name[0])
		return;
	cur = pilot_record.cur_battle;
	if (pilot_record.battle_status[cur] == 1 &&
		pilot_record.battle_cursor[cur] == xparagrp_Count_Paragraph_Strings(ship_info[cur + NUM_SHIPS], 3)) {
		pilot_record.battle_status[cur] = 3;
		if (pilot_name[0])
			shipext_Save_Pilot_Data(pilot_name);
	}
}

// FUNCTION: TIE95 0x81F18
int16_t shipext_Get_Tour_Cutscene(void) {
	int16_t cur = pilot_record.cur_battle;
	int16_t result = 0;

	int16_t battle_size;
	int16_t cursor;

	if (pilot_record.battle_status[cur] != 1)
		return 0;

	battle_size = shipext_Get_Tour_Battle_Size(cur);
	cursor = pilot_record.battle_cursor[cur];

	if (battle_size == cursor) {
		/* Battle complete — award TOD medal for tours 1..13 */
		result = cur + 1;
		if (cur + 1 >= 14)
			return 0;
		battle_medal = cur + 1;
	} else if (shipext_Is_Mission_Success()) {
		/* Check for secret battle scene triggers */
		int16_t scene_id = 21;
		int16_t i;

		for (i = 0; i < 8; i++) {
			if (cur == secret_battle_scene[i].battle && secret_battle_scene[i].mission == cursor - 1)
				result = scene_id;
			scene_id++;
		}
	}

	return result;
}

// FUNCTION: TIE95 0x82784
int16_t shipext_Set_Tourdesk_Cutscene(void) {
	int16_t result;

	if (pilot_record.battle_status[pilot_record.cur_battle] != 3)
		return 180;

	cur_battle_cutscene = 0;
	num_battle_cutscenes = 0;

	if (pilot_record.cur_battle == 6) {
		result = 281;
		battle_cutscene[0] = 560;
		battle_cutscene[1] = 257;
		num_battle_cutscenes = 2;
	} else {
		result = 10 * pilot_record.cur_battle + 500;
	}

	battle_cutscene[num_battle_cutscenes] = 160;
	num_battle_cutscenes++;

	return result;
}

void shipext_Get_Ship_Pos(int16_t ship, int16_t para_idx, int16_t str_idx, int32_t* out_x, int32_t* out_y,
						  int32_t* out_z) {
	char buf[40];
	int16_t cat, offset;

	if (ship < NUM_SHIPS) {
		if (!ship_info[ship]) {
			*out_x = 0;
			*out_y = 0;
			*out_z = 30;
			return;
		}
		xparagrp_Get_Paragraph_String(ship_info[ship], buf, para_idx, str_idx);
	} else {
		find_blueprint_cat(blueprint_ship, &cat, &offset);
		if (!blueprint_info[cat]) {
			*out_x = 0;
			*out_y = 0;
			*out_z = 30;
			return;
		}
		xparagrp_Get_Paragraph_String(blueprint_info[cat], buf, offset, 2);
	}
	parse_position(buf, out_x, out_y, out_z);
}

/* --- Mission flow --- */

// FUNCTION: TIE95 0x82300
void shipext_Mission_Enter(int16_t mission_type) {
	char name_buf[40];

	if (mission_type == 2) {
		/* Training mission */
		int16_t craft_id;

		if (!shipext_Is_Ship_Available(pilot_record.cur_train_ship)) {
			pilot_record.train_level[pilot_record.cur_train_ship] = 0;
			pilot_record.cur_train_ship = 0;
		}
		/* Map train ship index to flight craft ID */
		craft_id = pilot_record.cur_train_ship;
		switch (pilot_record.cur_train_ship) {
			case 0:
				craft_id = 5;
				break;
			case 1:
				craft_id = 6;
				break;
			case 2:
				craft_id = 7;
				break;
			case 3:
				craft_id = 8;
				break;
			case 4:
				craft_id = 16;
				break;
			case 5:
				craft_id = 9;
				break;
			case 6:
				craft_id = 12;
				break;
		}
		mission.train_craft_type_src = (uint8_t)craft_id;
		mission.train_level = pilot_record.train_level[pilot_record.cur_train_ship] + 1;
		mission.torp_used = 0;
		mission.beam_used = 0;
		strcpy(mission_name, "TRAIN");
	} else {
		mission.train_level = 0;
		mission.train_craft_type_src = 0;
	}

	/* Set mission mode byte */
	if (mission_type == 2)
		mission.mission_mode = 0;
	else if (mission_type == 3) {
		if (pilot_record.cur_combat_ship < NUM_SHIPS)
			mission.mission_mode = 1;
		else
			mission.mission_mode = 5;
	} else if (mission_type == 4)
		mission.mission_mode = 4;

	shipext_Get_Mission_Path(missionfilename);
	strcpy(name_buf, pilot_name);
	snprintf(pilotname, sizeof(pilotname), "%s.tfr", name_buf);
}

// FUNCTION: TIE95 0x82434
int16_t shipext_Mission_Exit(int16_t mission_type, int16_t exit_code) {
	char name_buf[44];

	strcpy(name_buf, pilot_name);
	shipext_Load_Pilot(name_buf);

	if (exit_code >= 10 && exit_code <= 13) {
		if (exit_code == 11 || exit_code == 12) {
			int16_t code = exit_code - 10;
			code ^= 3;
			return code + 292;
		}
		return exit_code + 282;
	}

	mission_outcome = exit_code + 1;

	if (mission_type == 2)
		return 123;
	if (mission_type == 3)
		return 134;
	if (mission_type == 4)
		return 300;
	if (mission_type == 290) {
		if (exit_code == 14)
			return 140;
		return 110;
	}
	if (mission_type == 291)
		return 110;

	return 131;
}

// FUNCTION: TIE95 0x82504
void shipext_Get_Mission_Path(char* out) {
	strcpy(out, "mission/");
	strcat(out, mission_name);
	strcat(out, ".tie");
}

// FUNCTION: TIE95 0x827EC
LandruFile* shipext_Open_Mission_File(const char* filename) {
	char path[72];
	strcpy(path, "mission/");
	strcat(path, filename);
	return xfile_Open_File(LANDRU_FILE_ROOT_AUXILIARY_ASSET, path, "rb");
}

// FUNCTION: TIE95 0x82580
void shipext_Set_Mission_Cutscenes(void) {
	int16_t promotion;

	shipext_Clear_Battle_Cutscenes();
	shipext_Set_Secret_Medal(mission.mission_secret_medal);

	switch (mission.player_status) {
		case PLAYER_DEAD:
			if (options_gbl.auto_backup && options_gbl.auto_restore) {
				shipext_Restore_Pilot();
				shipext_Add_Battle_Cutscene(240);
				shipext_Add_Battle_Cutscene(179);
			} else {
				shipext_Add_Battle_Cutscene(240);
				shipext_Add_Battle_Cutscene(101);
			}
			return;

		case PLAYER_CAPTURED:
			if (options_gbl.auto_backup && options_gbl.auto_restore) {
				shipext_Restore_Pilot();
				shipext_Add_Battle_Cutscene(210);
				shipext_Add_Battle_Cutscene(179);
			} else {
				shipext_Add_Battle_Cutscene(210);
				shipext_Add_Battle_Cutscene(101);
			}
			return;

		case 2:
			if (!shipext_Get_Tour_Cutscene())
				shipext_Add_Battle_Cutscene(280);
			break;

		case 3:
			break;

		default:
			return;
	}

	promotion = shipext_Get_Tour_Cutscene();
	if (promotion == 7) {
		shipext_Add_Battle_Cutscene(281);
		shipext_Add_Battle_Cutscene(560);
		shipext_Add_Battle_Cutscene(257);
	} else {
		if (promotion) {
			if (promotion <= NUM_BATTLES) {
				shipext_Add_Battle_Cutscene(10 * (promotion - 1) + 500);
			} else {
				if (promotion != 25)
					shipext_Add_Battle_Cutscene(25);
				shipext_Add_Battle_Cutscene(10 * (promotion - 21) + 700);
			}
		}

		if (battle_medal) {
			shipext_Add_Battle_Cutscene(battle_secret_medal ? 285 : 283);
			shipext_Add_Battle_Cutscene(250);
			shipext_Add_Battle_Cutscene(battle_medal + 250);
		} else if (battle_secret_medal) {
			shipext_Add_Battle_Cutscene(284);
		}
	}

	if (battle_secret_medal) {
		shipext_Add_Battle_Cutscene(390);
		shipext_Add_Battle_Cutscene(battle_secret_medal + 399);
	}

	if (promotion == 13)
		shipext_Add_Battle_Cutscene(91);

	if (shipext_Is_Mission_Success()) {
		if (mission_officer == 2)
			shipext_Add_Battle_Cutscene(192);
		else
			shipext_Add_Battle_Cutscene(191);
	} else {
		shipext_Add_Battle_Cutscene(190);
	}
}

// FUNCTION: TIE95 0x828BC
void shipext_Find_Mission_Ship(void) {
	char path[64];
	LandruFile* fp;
	int16_t num_fgs, fg_idx;
	int16_t mothership_fg = -1;
	EMissionStruct mission_data;
	EFGStruct fg;
	int16_t header_word;
	int16_t dummy;

#ifdef TIE_MODERN
	uint8_t mis_buf[EMISSIONSTRUCT_DISK_SIZE];
	uint8_t fg_buf[EFGSTRUCT_DISK_SIZE];
#endif

	shipext_Get_Mission_Path(path);
	fp = xfile_Open_File(LANDRU_FILE_ROOT_AUXILIARY_ASSET, path, "rb");
	if (!fp)
		return;

	/* Read file header: first word is platform/version.
	 * If negative → TIE format (next word = num_flight_groups).
	 * If positive → old X-Wing format (this word IS num_flight_groups). */
	xfile_Read_Word_From_File(fp, &header_word);
	if (header_word < 0) {
		xfile_Read_Word_From_File(fp, &num_fgs);
	} else {
		num_fgs = header_word;
	}
	xfile_Read_Word_From_File(fp, &dummy); /* num_messages */
	xfile_Read_Word_From_File(fp, &dummy); /* unknown */

	/* Read 450-byte mission global data */

#ifdef TIE_MODERN
	xfile_Read_Data_From_File(fp, mis_buf, EMISSIONSTRUCT_DISK_SIZE);
	EMissionStruct_decode(&mission_data, mis_buf);
#else
	xfile_Read_Data_From_File(fp, &mission_data, EMISSIONSTRUCT_DISK_SIZE);
#endif

	mission_ship = 0;
	mission_launch = 1;
	mission_officer = mission_data.win_type - 1;

	/* Scan flight groups to find the player's FG. Each on-disk record is
	 * EFGSTRUCT_DISK_SIZE bytes; sizeof(EFGStruct) is host-dependent and
	 * not safe to use as the read length. */

	for (fg_idx = 0; fg_idx < num_fgs; fg_idx++) {
#ifdef TIE_MODERN
		xfile_Read_Data_From_File(fp, fg_buf, EFGSTRUCT_DISK_SIZE);
		EFGStruct_decode(&fg, fg_buf);
#else
		xfile_Read_Data_From_File(fp, &fg, EFGSTRUCT_DISK_SIZE);
#endif

		if (!fg.player_flag)
			continue;

		/* Map CraftType (file format) to mission_ship (game ship slot).
		 * Only Imperial flyable ships appear in missions as player craft. */
		switch (fg.species) {
			case CRAFT_TIE_FIGHTER:
				mission_ship = 0;
				break;
			case CRAFT_TIE_INTERCEPTOR:
				mission_ship = 1;
				break;
			case CRAFT_TIE_BOMBER:
				mission_ship = 2;
				break;
			case CRAFT_TIE_ADVANCED:
				mission_ship = 3;
				break;
			case CRAFT_ASSAULT_GUNBOAT:
				mission_ship = 4;
				break;
			case CRAFT_TIE_DEFENDER:
				mission_ship = 5;
				break;
			case CRAFT_MISSILE_BOAT:
				mission_ship = 6;
				break;
			default:
				/* Fallback for unknown/unused craft types (10, 11, 13 in binary) */
				if (fg.species >= 10 && fg.species <= 13)
					mission_ship = fg.species - 3;
				break;
		}

		/* Resolve mothership: check arrival mothership first, then departure */
		if (fg.start_fg_used)
			mothership_fg = fg.start_fg;
		else if (fg.pri_stop_fg_used)
			mothership_fg = fg.pri_stop_fg;

		break;
	}

	/* If a mothership FG was found, read it to check if it's a capital ship.
	 * Capital ships (types 0x3C..0x45) mean the player launches from a hangar. */
	if (mothership_fg != -1) {
		int32_t seek_count;
		if (mothership_fg > fg_idx)
			seek_count = mothership_fg - fg_idx - 1;
		else
			seek_count = mothership_fg - fg_idx + 1;

		xfile_Seek_File(fp, (int32_t)EFGSTRUCT_DISK_SIZE * seek_count, 1); /* TIE_SEEK_CUR */

#ifdef TIE_MODERN
		if (xfile_Read_Data_From_File(fp, fg_buf, EFGSTRUCT_DISK_SIZE)) {
			EFGStruct_decode(&fg, fg_buf);
#else
		if (xfile_Read_Data_From_File(fp, &fg, EFGSTRUCT_DISK_SIZE)) {
#endif
			if (fg.species >= CRAFT_CAPITAL_FIRST && fg.species <= CRAFT_CAPITAL_LAST)
				mission_launch = 0; /* player launches from capital ship */
		}
	}

	xfile_Close_File(fp);
}
