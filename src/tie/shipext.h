#ifndef __SHIPEXT_H__
#define __SHIPEXT_H__

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <landru/file.h>

#include "landru/actor.h"
#include "landru/rect.h"
#include "landru/res.h"

#define NUM_SHIPS 12
#define NUM_BATTLES 20
#define SHIP_INFO_SIZE (NUM_SHIPS + NUM_BATTLES)

/* .TIE mission file structures (binary layout, offsets verified from disasm) */

/* CraftType enum from the .TIE file format (1-based, 0=None).
 * Reference: Mission_TIE95.txt FlightGroup.CraftType
 *
 * Find_Mission_Ship maps CraftType → mission_ship (game ship slot):
 *   5 (T/F)  → 0     9 (T/D) → 5
 *   6 (T/I)  → 1    12 (MIS) → 6
 *   7 (T/B)  → 2    16 (GUN) → 4
 *   8 (T/A)  → 3
 *
 * Note: CraftType-1 indexes the tietext0 paragraph 2 ship name table
 * (p2.s0="X-wing", p2.s4="TIE Fighter", etc.) — this is a DIFFERENT
 * index space from mission_ship. */
typedef enum {
	CRAFT_NONE = 0,
	CRAFT_XWING = 1,
	CRAFT_YWING = 2,
	CRAFT_AWING = 3,
	CRAFT_BWING = 4,
	CRAFT_TIE_FIGHTER = 5,
	CRAFT_TIE_INTERCEPTOR = 6,
	CRAFT_TIE_BOMBER = 7,
	CRAFT_TIE_ADVANCED = 8,
	CRAFT_TIE_DEFENDER = 9,
	CRAFT_MISSILE_BOAT = 12,
	CRAFT_ASSAULT_GUNBOAT = 16,
	CRAFT_CAPITAL_FIRST = 0x3C, /* Platform A — first station/capital type */
	CRAFT_CAPITAL_LAST = 0x45,  /* X/7 Factory — last station/capital type */
} CraftType;

/* ECondStruct — single arrival/departure condition.
 * Field names from IDA type database (EFGStruct member inspection).
 *   cond : condition selector (what to measure)
 *   type : target-type class (species/FG/IFF)
 *   id   : target identifier (species idx, FG idx, IFF side, ...)
 *   pct  : triggering percentage (0/25/50/75/100 ...)
 *
 * Naturally aligned in memory; on-disk layout is the fixed 4-byte
 * record produced by ECondStruct_encode and consumed by ECondStruct_-
 * decode. */
typedef struct {
	uint8_t cond;
	uint8_t type;
	uint8_t id;
	uint8_t pct;
} ECondStruct;

#define ECONDSTRUCT_DISK_SIZE 4u

void ECondStruct_decode(ECondStruct* dst, const uint8_t* src);
void ECondStruct_encode(uint8_t* dst, const ECondStruct* src);

/* EAIStruct — single AI order record. Naturally aligned in memory;
 * on-disk layout is the fixed 18-byte record produced/consumed by the
 * codec helpers below. An EFGStruct embeds 3 of these. */
typedef struct {
	uint8_t order;          /* +0x00: order opcode (indexes ordersldr/flw) */
	uint8_t speed;          /* +0x01: throttle opcode (indexes throttleconvert) */
	uint8_t var[4];         /* +0x02: order-specific var/param bytes */
	uint8_t target_type[2]; /* +0x06: primary/secondary target type */
	uint8_t target_id[2];   /* +0x08: primary/secondary target id */
	uint8_t target_op;      /* +0x0A: primary/secondary target AND/OR */
	uint8_t target_unused;  /* +0x0B */
	uint8_t pri_type;       /* +0x0C: tertiary target type */
	uint8_t pri_id;         /* +0x0D: tertiary target id */
	uint8_t sec_type;       /* +0x0E: quaternary target type */
	uint8_t sec_id;         /* +0x0F: quaternary target id */
	uint8_t pri_sec_op;     /* +0x10: tert/quat AND/OR */
	uint8_t pri_sec_unused; /* +0x11 */
} EAIStruct;

#define EAISTRUCT_DISK_SIZE 18u

void EAIStruct_decode(EAIStruct* dst, const uint8_t* src);
void EAIStruct_encode(uint8_t* dst, const EAIStruct* src);

/* EFGStruct — TIE Fighter flight group.
 * Field names from watdbg debug info (original LucasArts source).
 * Cross-verified against binary disasm and Mission_TIE95.txt (non-
 * official spec). On-disk layout is the fixed 292-byte (0x124) record
 * produced/consumed by EFGStruct_encode / EFGStruct_decode. The runtime
 * layout is naturally aligned and is wider than the disk record on most
 * hosts -- always go through the codec at file-format boundaries. */
typedef struct {
	char name[12];         /* +0x00 */
	char cmdr[12];         /* +0x0C: commander/pilot (editor only) */
	char contents[2][12];  /* +0x18: cargo[0] and special_cargo[1] */
	uint8_t special_craft; /* +0x30 */
	uint8_t special_flag;  /* +0x31 */
	uint8_t species;       /* +0x32: CraftType enum (ship type) */
	uint8_t count;         /* +0x33: number of craft */
	uint8_t version;       /* +0x34: status/loadout variant */
	uint8_t warhead;       /* +0x35: warhead type */
	uint8_t beam;          /* +0x36: beam type (0=none, 1=tractor, 2=jamming) */
	uint8_t side;          /* +0x37: IFF allegiance */
	uint8_t skill;         /* +0x38: AI skill level */
	uint8_t camoflage;     /* +0x39: markings */
	uint8_t camo_flag;     /* +0x3A: obey player orders */
	uint8_t camo_unused;   /* +0x3B */
	uint8_t formation;     /* +0x3C */
	uint8_t form_spacing;  /* +0x3D */
	uint8_t set;           /* +0x3E: global group */
	uint8_t set_unused;    /* +0x3F */
	uint8_t waves;         /* +0x40: additional waves (0-indexed) */
	uint8_t wave_delay;    /* +0x41 */
	uint8_t player_flag;   /* +0x42: nonzero = player's flight group (1-indexed) */
	uint8_t heading;       /* +0x43: yaw (for space objects) */
	uint8_t pitch;         /* +0x44 */
	uint8_t rotation;      /* +0x45: roll */
	/* +0x46..+0x49: cross-mission attrition tracking ("linked permadeath").
	 *   link_flag  bool  — when 1, this FG participates in cross-mission
	 *                       attrition tracking. SCORE_craftexitscoring
	 *                       and STATIC_laserhitstatic increment
	 *                       mission.mission_linked_data[link_code] by 1
	 *                       (saturating at 0xFF) every time one of this
	 *                       FG's craft is destroyed. CREATE_createmission
	 *                       then subtracts that running counter from this
	 *                       FG's spawn count at the next mission load:
	 *                         count = max(0, count - linked_data[link_code])
	 *                       so destroyed craft do not return.
	 *   link_code  u8    — slot index into the 256-entry
	 *                       mission.mission_linked_data[] array. Multiple
	 *                       FGs across multiple missions of the same
	 *                       battle that share the same link_code form one
	 *                       attrition group (e.g. all "ISD Hammer"
	 *                       appearances). linked_data is persisted in the
	 *                       pilot record (.tfr +0x291) so the count
	 *                       carries across save/load and across missions.
	 *   link_unused      — pad byte; never read.
	 *   difficulty       — per-FG arrival difficulty mask index into
	 *                       fgdiffmask[]. Combined with diffmask[
	 *                       mission.difficulty] to gate spawn + goal
	 *                       evaluation. NOTE: IDA decompiler often shows
	 *                       this as `*(int *)&fg.link_flag >> 24` because
	 *                       Watcom emits an unaligned dword load that
	 *                       extracts the high byte (offset +0x49). That
	 *                       expression reads `difficulty`, not link_flag. */
	uint8_t link_flag;         /* +0x46 */
	uint8_t link_code;         /* +0x47 */
	uint8_t link_unused;       /* +0x48 */
	uint8_t difficulty;        /* +0x49 */
	ECondStruct start_cond[2]; /* +0x4A: arrival triggers (2 x 4 bytes) */
	uint8_t start_op;          /* +0x52: 1 = arrival1 OR arrival2, else AND */
	uint8_t start_unused;      /* +0x53 */
	uint8_t start_delay_min;   /* +0x54 */
	uint8_t start_delay_sec;   /* +0x55 */
	ECondStruct stop_cond;     /* +0x56: departure trigger */
	uint8_t stop_min;          /* +0x5A */
	uint8_t stop_sec;          /* +0x5B */
	uint8_t stop_abort;        /* +0x5C: abort condition */
	uint8_t stop_unused;       /* +0x5D */
	int16_t cur_start_fg;      /* +0x5E: editor use */
	uint8_t start_fg;          /* +0x60: arrival mothership FG index */
	uint8_t start_fg_used;     /* +0x61: true = arrive via mothership */
	uint8_t pri_stop_fg;       /* +0x62: primary departure mothership FG */
	uint8_t pri_stop_fg_used;  /* +0x63: true = depart via mothership */
	uint8_t sec_stop_fg;       /* +0x64: alternate mothership FG */
	uint8_t sec_stop_fg_used;  /* +0x65: true = alt mothership active */
	uint8_t capture_fg;        /* +0x66: capture departure mothership */
	uint8_t capture_fg_used;   /* +0x67 */
	EAIStruct ai[3];           /* +0x68: AI orders (3 × 18 = 54 bytes) */
	uint8_t pri_win_cond;      /* +0x9E */
	uint8_t pri_win_pct;       /* +0x9F */
	uint8_t sec_win_cond;      /* +0xA0 */
	uint8_t sec_win_pct;       /* +0xA1 */
	uint8_t loss_cond;         /* +0xA2 */
	uint8_t loss_pct;          /* +0xA3 */
	uint8_t bonus_cond;        /* +0xA4 */
	uint8_t bonus_pct;         /* +0xA5 */
	int8_t bonus_points;       /* +0xA6 */
	uint8_t bonus_unused;      /* +0xA7 */
	int16_t way_x[15];         /* +0xA8 */
	int16_t way_y[15];         /* +0xC6 */
	int16_t way_z[15];         /* +0xE4 */
	int16_t way_used[15];      /* +0x102 */
	uint8_t way_shown;         /* +0x120 */
	uint8_t way_unused;        /* +0x121 */
	uint8_t way_brief_link;    /* +0x122 */
	uint8_t way_brief_shown;   /* +0x123 */
} EFGStruct;

#define EFGSTRUCT_DISK_SIZE 292u

void EFGStruct_decode(EFGStruct* dst, const uint8_t* src);
void EFGStruct_encode(uint8_t* dst, const EFGStruct* src);

/* EMissionStruct — TIE mission header.
 * In the .TIE file: 2-byte version (read separately, then rewound),
 * followed by 456 bytes = 3 count shorts (num_fg, num_msg, num_goals)
 * + EMissionStruct. Full file header = 458 bytes (0x1CA).
 * Field names from watdbg debug info.
 *
 * Naturally aligned in memory; on-disk layout is the fixed 450-byte
 * little-endian record produced/consumed by EMissionStruct_encode /
 * EMissionStruct_decode. */
typedef struct {
	uint8_t time_min;         /* +0x00: mission time limit minutes */
	uint8_t time_sec;         /* +0x01: mission time limit seconds */
	uint8_t win_type;         /* +0x02: 1=both, 2=flight officer, 3=secret order */
	uint8_t backdrop;         /* +0x03 */
	uint8_t rescue;           /* +0x04 */
	uint8_t all_way_shown;    /* +0x05 */
	uint8_t mis_var[8];       /* +0x06 */
	int8_t win_bonus[2];      /* +0x0E */
	uint8_t win_msg1[2][64];  /* +0x10 */
	uint8_t win_msg2[2][64];  /* +0x90 */
	uint8_t loss_msg[2][64];  /* +0x110 */
	uint8_t loss_msg_delay;   /* +0x190 */
	uint8_t loss_unused;      /* +0x191 */
	char neutral_name[4][12]; /* +0x192: IFF names 3-6 */
} EMissionStruct;

#define EMISSIONSTRUCT_DISK_SIZE 450u

void EMissionStruct_decode(EMissionStruct* dst, const uint8_t* src);
void EMissionStruct_encode(uint8_t* dst, const EMissionStruct* src);

/* MissionFile — the 456-byte block read by CREATE_loadmission.
 * 3 count shorts + EMissionStruct. Naturally aligned in memory; on-disk
 * layout is the fixed 456-byte little-endian record. */
typedef struct {
	int16_t num_fg;         /* +0x00: number of flight groups */
	int16_t num_msg;        /* +0x02: number of radio messages */
	int16_t num_goals;      /* +0x04: number of goals / cut scenes */
	EMissionStruct mission; /* +0x06: mission header (450 bytes) */
} MissionFile;

#define MISSIONFILE_DISK_SIZE 456u

void MissionFile_decode(MissionFile* dst, const uint8_t* src);
void MissionFile_encode(uint8_t* dst, const MissionFile* src);

/* Pilot save record. The .tfr file stores two 1928-byte slots back-to-
 * back (primary + backup). Naturally aligned in memory; on-disk layout
 * is the fixed 1928-byte little-endian record produced/consumed by the
 * PilotRecord codec helpers below.
 *
 * RESERVED FIELDS: every `reserved_*` field below was scanned against the
 * full TIE.EXE binary by absolute address (pilot_record at 0xFABC8). None
 * have any direct readers or writers at runtime. They are zeroed by
 * SHIPEXT_Init_Pilot's memset and round-tripped through SHIPEXT_Save_/
 * Restore_Pilot via wholesale `memcpy(buffer, &pilot_record, 0x788)` --
 * i.e., they are preserved verbatim through save/load cycles but have no
 * runtime semantic. Two patterns explain them:
 *   - alignment padding (1- or 2-byte gaps before u16/i32 fields)
 *   - 10/29-byte holes likely intended as forward-compat reservation.
 *
 * IN-MEMORY vs ON-DISK: this struct uses NATURAL alignment (sizeof = 1936),
 * which differs from the on-disk layout (PILOTRECORD_DISK_SIZE = 1928) by
 * the 4-byte i32 pads compiler-inserted before secret_score (+0x0E),
 * combat_score (+0x088), and the trailing alignment. Never raw-cast a
 * disk byte buffer as `PilotRecord *`; always go through PilotRecord_decode
 * / _encode at file boundaries. */
typedef struct {
	uint8_t version;                           /* +0x000: record version (always 1) */
	uint8_t exit_status;                       /* +0x001: last mission exit code */
	uint8_t rank;                              /* +0x002: pilot rank (0-5) */
	uint8_t game_level;                        /* +0x003: difficulty (0=easy, 1=medium, 2=hard) */
	int32_t score;                             /* +0x004: career total score */
	uint16_t avg_score;                        /* +0x008: average score (capped 0xFFFF) */
	uint8_t secret_order_rank;                 /* +0x00A: secret order rank (0-9) */
	uint8_t reserved_0b;                       /* +0x00B: alignment pad before u16
												*         secret_completions; no readers. */
	uint16_t secret_completions;               /* +0x00C: secret mission completion count */
	int32_t secret_score;                      /* +0x00E: secret total score */
	uint8_t reserved_12[10];                   /* +0x012..+0x01B: 10-byte hole; no readers. */
	uint8_t cur_train_ship;                    /* +0x01C: current training ship index */
	uint8_t train_level[NUM_SHIPS];            /* +0x01D: per-ship training level */
	uint8_t reserved_29;                       /* +0x029: alignment pad before i32
												*         train_score; no readers. */
	int32_t train_score[NUM_SHIPS];            /* +0x02A: per-ship training high score */
	uint8_t train_max_level[NUM_SHIPS];        /* +0x05A: per-ship max training level reached */
	uint8_t cur_combat_ship;                         /* +0x066: current combat sim ship index */
	uint8_t combat_course_cursor[SHIP_INFO_SIZE];    /* +0x067: per-ship/battle course cursor */
	uint8_t reserved_87;                             /* +0x087: alignment pad before combat_score */
	int32_t combat_score[NUM_SHIPS][8];        /* +0x088: per-ship x course high scores */
	uint8_t combat_complete[NUM_SHIPS][8];     /* +0x208: per-ship x course completion */
	uint8_t cur_battle;                        /* +0x268: current battle index (0-19) */
	uint8_t battle_status[NUM_BATTLES];        /* +0x269: per-battle progress (1=active, 2=failed) */
	uint8_t battle_cursor[NUM_BATTLES];        /* +0x27D: per-battle current mission */
	uint8_t linked_data[256];                  /* +0x291: mission linked data */
	uint8_t secret_complete_bits[NUM_BATTLES]; /* +0x391: secret obj bitmask per battle */
	uint8_t mission_bonus_bits[NUM_BATTLES];   /* +0x3A5: bonus obj bitmask per battle */
	uint8_t reserved_3b9[29];                  /* +0x3B9..+0x3D5: 29-byte hole; no readers. */
	uint8_t reserved_3d6[4];                   /* +0x3D6..+0x3D9: 4-byte hole. The only
												*         apparent access is COMPUTER_Draw_
												*         Computer_Battle_Info at 0x85607,
												*         which uses &reserved_3d6 as the
												*         array base for tour_score so that
												*         `dword_FAF9E[battle*32 + mission*4]`
												*         indexes tour_score[battle][mission-1]
												*         (1-based mission indexing). The
												*         dword's value itself is read once
												*         when battle=0/mission=0 but never
												*         drives any branch -- semantically
												*         dead, just an indexing anchor. */
	int32_t tour_score[NUM_BATTLES][8];        /* +0x3DA: per-battle x mission high scores */
	uint16_t total_kills;                      /* +0x65A: career kill total */
	uint16_t total_captures;                   /* +0x65C: career capture total */
	uint8_t reserved_65e[2];                   /* +0x65E..+0x65F: alignment pad before
												*         u16 kills_by_ship_type[]; no readers. */
	uint16_t kills_by_ship_type[69];           /* +0x660: per-species kill counts */
	uint16_t captures_by_ship_type[69];        /* +0x6EA: per-species capture counts */
	int32_t laser_total;                       /* +0x774: lasers fired (career) */
	int32_t laser_hits;                        /* +0x778: laser hits (career) */
	uint8_t reserved_77c[4];                   /* +0x77C..+0x77F: 4-byte hole; no readers. */
	uint16_t warhead_total;                    /* +0x780: warheads fired (career) */
	uint16_t warhead_hits;                     /* +0x782: warhead hits (career) */
	uint8_t reserved_784[2];                   /* +0x784..+0x785: alignment pad before
												*         u16 ejection_count; no readers. */
	uint16_t ejection_count;                   /* +0x786: times ejected */
} PilotRecord;

#define PILOTRECORD_DISK_SIZE 1928u

void PilotRecord_decode(PilotRecord* dst, const uint8_t* src);
void PilotRecord_encode(uint8_t* dst, const PilotRecord* src);

extern PilotRecord pilot_record;

/* --- Lifecycle --- */
void shipext_Open_Ships(void);
void shipext_Close_Ships(void);

/* --- Ship queries --- */
bool shipext_Is_Ship(int16_t ship_idx);
bool shipext_Is_Ship_Available(int16_t ship_idx);
bool shipext_Is_Mission_Disk1(void);
bool shipext_Is_Mission_Disk2(void);
ResFile* shipext_Open_Ship_Resource(int16_t ship_idx);
int16_t shipext_Open_Launch_Resource(void);
void shipext_Get_Ship_Name(char* out, int16_t ship_idx, int16_t para_type, int16_t para_idx);
void shipext_Get_Launch_Name(char* out);
void shipext_Get_Weapon_Select_Name(char* out);

/* --- Pilot management --- */
void shipext_Init_Pilot(void);
void shipext_Set_Pilot_Name(const char* name);
/* PORT: capacity parameter is absent from the recovered getter. */
void shipext_Get_Pilot_Name(char* out, size_t capacity);
bool shipext_Load_Pilot(const char* name);
bool shipext_Create_Pilot(const char* name);
void shipext_Revive_Pilot(char* name);
void shipext_Update_Pilot(void);
void shipext_Save_Pilot_Data(const char* name);
void shipext_Backup_Pilot(void);
void shipext_Restore_Pilot(void);
int16_t shipext_Write_Temp_Pilot(void);
bool shipext_Read_Temp_Pilot(void);
void shipext_Delete_Temp_Pilot(void);
void shipext_Link_Pilot(void);
LandruFile* shipext_Open_Pilot_File(char* name, char* mode);

/* --- Mission state --- */
void shipext_Set_Mission_Ship(int16_t val);
int16_t shipext_Get_Mission_Ship(void);
void shipext_Set_Mission_Launch(int16_t val);
int16_t shipext_Is_Mission_Launch(void);
void shipext_Set_Mission_Outcome(int16_t val);
int16_t shipext_Get_Mission_Outcome(void);
void shipext_Set_Mission_Officer(int16_t val);
int16_t shipext_Get_Mission_Officer(void);
void shipext_Set_Mission_Name(const char* name);
const char* shipext_Get_Mission_Name(void);
bool shipext_Is_Mission_Success(void);
bool shipext_Is_Combat_Mission_Success(void);
bool shipext_Is_Player_OK(void);
void shipext_Mission_Enter(int16_t mission_type);
int16_t shipext_Mission_Exit(int16_t mission_type, int16_t exit_code);
void shipext_Get_Mission_Path(char* out);
LandruFile* shipext_Open_Mission_File(const char* filename);
void shipext_Set_Mission_Cutscenes(void);
void shipext_Find_Mission_Ship(void);

/* --- Medal state --- */
void shipext_Set_TOD_Medal(int16_t val);
int16_t shipext_Get_TOD_Medal(void);
void shipext_Set_Secret_Medal(int16_t val);
int16_t shipext_Get_Secret_Medal(void);

/* Clear scratch state shared across flights: blueprint_component,
 * battle_medal, battle_secret_medal, blueprint_ship. Mirrors retail
 * SHELL_Reset_Battle_Results (0x7fed0); called once between flights. */
void shipext_Reset_Battle_Results(void);

/* --- Blueprint browsing --- */
int16_t shipext_Get_Blueprint_Ship(void);
void shipext_Set_Blueprint_Ship(int16_t val);
void shipext_Next_Blueprint_Component(void);
void shipext_Last_Blueprint_Component(void);
int16_t shipext_Get_Blueprint_Component(void);
void shipext_Set_Num_Blueprint_Components(int16_t count);
void shipext_Open_Blueprint_Ships(void);
void shipext_Close_Blueprint_Ships(void);
void shipext_Next_Blueprint_Ship(void);
void shipext_Last_Blueprint_Ship(void);
void shipext_Get_Blueprint_Ship_SHP(void);
void shipext_Get_Blueprint_Ship_Name(char* out);
void shipext_Get_Blueprint_Ship_Line(char* out, int16_t line);
int16_t shipext_Get_Num_Blueprint_Ship_Lines(void);
void shipext_Get_Blueprint_Index(int16_t* out_category, int16_t* out_offset);

/* --- Training --- */
uint8_t shipext_Get_Train_Level(void);
void shipext_Next_Train_Level(void);
void shipext_Last_Train_Level(void);
void shipext_Get_Train_Ship_Name(char* out);
void shipext_Next_Train_Ship(void);
void shipext_Last_Train_Ship(void);
uint8_t shipext_Get_Train_Ship(void);
void shipext_Init_Train_Ship_Name(void);
void shipext_Show_Train_Ship_Name(void);
void shipext_Get_Train_Ship_SHP(void);
void shipext_Get_Train_Course_SHP(void);
void shipext_Get_Train_Ship_Pos(int32_t* out_x, int32_t* out_y, int32_t* out_z);
void shipext_Get_Train_Mission_Text(char* buf, int16_t line);
int16_t shipext_Num_Train_Mission_Text_Lines(void);

/* --- Combat --- */
uint8_t shipext_Get_Combat_Mission(void);
bool shipext_Is_Combat_Ship_Tour(void);
void shipext_Next_Combat_Ship(void);
void shipext_Last_Combat_Ship(void);
uint8_t shipext_Get_Combat_Ship(void);
void shipext_Init_Combat_Ship_Name(void);
void shipext_Show_Combat_Ship_Name(void);
void shipext_Get_Combat_Ship_SHP(void);
void shipext_Get_Combat_Ship_Pos(int32_t* out_x, int32_t* out_y, int32_t* out_z);
void shipext_Get_Combat_Ship_Name(char* out);
int16_t shipext_Num_Combat_Missions(void);
void shipext_Next_Combat_Mission(void);
void shipext_Last_Combat_Mission(void);
void shipext_Get_Combat_Mission_Name(char* out);
void shipext_Get_Combat_Mission_Text(char* out, int16_t line);
int16_t shipext_Num_Combat_Mission_Text_Lines(void);

/* --- Battle/Tour --- */
void shipext_Clear_Battle_Cutscenes(void);
void shipext_Add_Battle_Cutscene(int16_t cutscene_id);
int16_t shipext_Next_Battle_Cutscene(void);
uint8_t shipext_Get_Tour_Battle(void);
int16_t shipext_Get_Tour_Battle_Size(int16_t battle);
bool shipext_Is_Tour_Battle_End(void);
void shipext_Refly_Tour_Mission(void);
int16_t shipext_Set_Tour_Battle(void);
void shipext_Validate_Tour_Battle(void);
int16_t shipext_Get_Tour_Cutscene(void);
void shipext_Get_Battle_Mission_Name(char* out);
void shipext_Get_Battle_Title(char* out, int16_t mode);
void shipext_Get_Battle_Ship_Name(char* out);
Actor* shipext_Get_Battle_Galaxy_Image(void);
void shipext_Get_Battle_Galaxy_Name(char* out);
void shipext_Get_Battle_Galaxy_Rect(Rect* out);
int16_t shipext_Find_Battle(void);
bool shipext_Next_Battle(void);
bool shipext_Last_Battle(void);
bool shipext_Valid_Incomplete_Battle(int16_t battle);
bool shipext_Valid_Battle(int16_t battle);
int16_t shipext_Set_Tourdesk_Cutscene(void);
void shipext_Get_Ship_Pos(int16_t ship, int16_t para_idx, int16_t str_idx, int32_t* out_x, int32_t* out_y,
						  int32_t* out_z);

#endif
