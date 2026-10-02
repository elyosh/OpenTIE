#include "tie/create.h"
#ifdef TIE_MODERN
#include "tie_runtime/storage/mission_records.h"
#endif
#include "tie/edition.h"
#include "tie/feinput.h"
#include "tie/paiman.h"
#include "tie_runtime/runtime/inflight_state.h"

#include "tie/backdrp2.h"
#include "tie/collide.h"
#include "tie/draw.h"
#include "tie/fediskio.h"
#include "tie/festring.h"
#include "tie/fscript.h"
#include "tie/fsfx.h"
#include "tie/fview.h"
#include "tie/math2.h"
#include "tie/mission.h"
#include "tie/modelbounds.h"
#include "tie/modelmesh.h"
#include "tie/msg.h"
#include "tie/pai.h"
#include "tie/panel.h"
#include "tie/score.h"
#include "tie/shell.h"
#include "tie/shipext.h"
#include "tie/spec.h"
#include "tie/species.h"
#include "tie/tie.h"
#include "tie/tie_render_tie98.h"
#include "tie/trig2.h"
#include "tie/xtimer.h"
#include "tie_runtime/audio/config.h"
#include "tie_runtime/diagnostics/diagnostics.h"
#include "tie_runtime/display/classic_display.h"
#include "tie_runtime/display/classic_framebuffer.h"
#include "tie_runtime/flight_assets/model_types.h"
#include "tie_runtime/input/input.h"
#include "tie_runtime/runtime/exports.h"
#include "tie_runtime/runtime/profile.h"
#include "tie_runtime/storage/storage.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* CREATE-owned globals (watdbg: D:\GAMES\XTIE\CODE\create.c)         */
/* ------------------------------------------------------------------ */

// GLOBAL: TIE95 0xC17D8
// GLOBAL: TIE98 0x4DF7F0
uint8_t playerside = 1;

// GLOBAL: TIE95 0xC17DA
// GLOBAL: TIE98 0x4DF7F8
uint16_t skilltranslate[6] = { 0, 0x4000, 0x8000, 0xC000, 0xFFFF, 0xFFFF };
// GLOBAL: TIE95 0xC17E6
// GLOBAL: TIE98 0x4DF808
uint16_t aiupdatetranslate[6] = { 0x02C4, 0x01D8, 0x00EC, 0x0076, 0x003B, 0x001D };

// GLOBAL: TIE95 0xC17F2
// GLOBAL: TIE98 0x4DF818
uint8_t ordersldr[33] = {
	0x01, 0x2F, 0x03, 0x05, 0x27, 0x2A, 0x2B, 0x07, 0x08, 0x09, 0x14, 0x13, 0x1C, 0x1D, 0x1E, 0x1F, 0x20,
	0x21, 0x25, 0x42, 0x42, 0x38, 0x3A, 0x3B, 0x3C, 0x3C, 0x3E, 0x3F, 0x01, 0x35, 0x01, 0x22, 0x44,
};

/* Mission-file follower order to runtime order. */
// GLOBAL: TIE95 0xC1813
// GLOBAL: TIE98 0x4DF840
uint8_t ordersflw[33] = {
	0x02, 0x30, 0x04, 0x06, 0x29, 0x2A, 0x2B, 0x0E, 0x0E, 0x0E, 0x18, 0x0E, 0x1C, 0x1D, 0x1E, 0x1F, 0x20,
	0x1D, 0x04, 0x42, 0x42, 0x39, 0x3A, 0x3B, 0x39, 0x39, 0x39, 0x39, 0x02, 0x36, 0x02, 0x22, 0x44,
};

/* Mission-file species index to species_table index. */
// GLOBAL: TIE95 0xC1834
// GLOBAL: TIE98 0x4DF868
uint8_t speciesconvert[89] = {
	0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x08, 0x08, 0x0C, 0x0D, 0x0E,
	0x0F, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D,
	0x1E, 0x1A, 0x20, 0x21, 0x22, 0x23, 0x20, 0x25, 0x26, 0x26, 0x28, 0x29, 0x2A, 0x2B, 0x2C,
	0x2D, 0x2E, 0x2F, 0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x35, 0x37, 0x38, 0x39, 0x3A, 0x3B,
	0x3C, 0x3D, 0x3E, 0x3F, 0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4A,
	0x4B, 0x4C, 0x4D, 0x4E, 0x4F, 0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x64, 0x57, 0xEB,
};

// GLOBAL: TIE95 0xC188D
// GLOBAL: TIE98 0x4DF8C8
uint8_t genusconvert[9] = { 0x00, 0x01, 0x03, 0x04, 0x02, 0x05, 0x08, 0x09, 0x02 };
// GLOBAL: TIE95 0xC1896
// GLOBAL: TIE98 0x4DF8D4
uint8_t familyconvert[4] = { 0x00, 0x01, 0x02, 0x00 };

// GLOBAL: TIE95 0xC189A
// GLOBAL: TIE98 0x4DF8D8
uint8_t warheadconvert[8] = { 0x00, 0x96, 0x97, 0x90, 0x8F, 0x95, 0x94, 0x98 };
// GLOBAL: TIE95 0xC18A2
// GLOBAL: TIE98 0x4DF8E0
uint16_t warheadadjust[8] = {
	0x0000, 0x4000, 0x8000, 0xFFFF, 0xC000, 0xFFFF, 0xC000, 0xFFFF,
};

// GLOBAL: TIE95 0xC18B2
// GLOBAL: TIE98 0x4DF8F0
uint8_t initialdamagestate[32] = {
	0xFF, 0xFF, 0xFF, 0xFF, 0x18, 0x04, 0xFF, 0xFF, 0x40, 0xFF, 0x20, 0x30, 0x30, 0x30, 0x70, 0xFF,
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x18, 0x20, 0x30, 0x30, 0x30, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
};

// GLOBAL: TIE95 0xC18D2
// GLOBAL: TIE98 0x4DF910
uint8_t componentsgone[60] = {
	0x16, 0x17, 0x15, 0x14, 0x13, 0x05, 0x0F, 0x10, 0x11, 0x12, 0x18, 0x06, 0x03, 0x05, 0x1B,
	0x09, 0x0A, 0xFF, 0x01, 0x04, 0x1A, 0x07, 0x08, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x05, 0x0D, 0x0F, 0x13, 0x14, 0x18, 0x0B, 0x0E, 0x06,
	0x11, 0x12, 0x17, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0xFF, 0x15, 0x16, 0x17, 0x18, 0x1E, 0xFF,
};

// GLOBAL: TIE95 0xC190E
// GLOBAL: TIE98 0x4DF950
uint8_t fgdiffmask[6] = { 0x07, 0x01, 0x02, 0x04, 0x06, 0x03 };
// GLOBAL: TIE95 0xC1914
// GLOBAL: TIE98 0x4DF958
uint8_t diffmask[4] = { 0x01, 0x02, 0x04, 0x00 };

// GLOBAL: TIE95 0xD3560
// GLOBAL: TIE98 0x62698C
int32_t fglocx;
// GLOBAL: TIE95 0xD3564
// GLOBAL: TIE98 0x626990
int32_t fglocy;
// GLOBAL: TIE95 0xD3568
// GLOBAL: TIE98 0x626994
int32_t fglocz;
// GLOBAL: TIE95 0xD356C
// GLOBAL: TIE98 0x626970
int16_t staging_static_x;
// GLOBAL: TIE95 0xD356E
// GLOBAL: TIE98 0x626972
int16_t staging_static_y;
// GLOBAL: TIE95 0xD3570
// GLOBAL: TIE98 0x626974
int16_t staging_static_z;
// GLOBAL: TIE95 0xD3572
// GLOBAL: TIE98 0x626976
int8_t staging_static_heading;
// GLOBAL: TIE95 0xD3573
// GLOBAL: TIE98 0x626977
int8_t staging_static_pitch;
// GLOBAL: TIE95 0xD3574
// GLOBAL: TIE98 0x626978
int8_t staging_static_roll;
// GLOBAL: TIE95 0xD3578
// GLOBAL: TIE98 0x626986
uint16_t craftcnt;
// GLOBAL: TIE95 0xD3576
// GLOBAL: TIE98 0x62699C
uint16_t fgcnt;
// GLOBAL: TIE95 0xD357A
// GLOBAL: TIE98 0x62697E
int16_t fgheadingxy;
// GLOBAL: TIE95 0xD357C
// GLOBAL: TIE98 0x626984
int16_t fgheadingz;
// GLOBAL: TIE95 0xD357F
// GLOBAL: TIE98 0x626981
uint8_t fgversion;
// GLOBAL: TIE95 0xD3580
// GLOBAL: TIE98 0x626982
uint8_t fghangar;
// GLOBAL: TIE95 0xD3581
// GLOBAL: TIE98 0x626980
uint8_t fghyperspace;
// GLOBAL: TIE95 0xD3582
// GLOBAL: TIE98 0x626998
uint8_t fggenus;
// GLOBAL: TIE95 0xD3586
// GLOBAL: TIE98 0x626979
uint8_t leaderflag;
// GLOBAL: TIE95 0xD3589
// GLOBAL: TIE98 0x626988
uint8_t fgspecies;
// GLOBAL: TIE95 0xD357E
// GLOBAL: TIE98 0x62697B
uint8_t fgseparation;
// GLOBAL: TIE95 0xD3583
// GLOBAL: TIE98 0x62699A
uint8_t fgformation;
// GLOBAL: TIE95 0xD3584
// GLOBAL: TIE98 0x626989
uint8_t fgflightflag;
// GLOBAL: TIE95 0xD3585
// GLOBAL: TIE98 0x62697D
uint8_t fgskill;
// GLOBAL: TIE95 0xD3587
// GLOBAL: TIE98 0x62697A
uint8_t fgside;
// GLOBAL: TIE95 0xD3588
// GLOBAL: TIE98 0x626999
uint8_t fgsidecreated;

/* ============================================================== */
/*   Mission file loader                                           */
/* ============================================================== */

// FUNCTION: TIE95 0x165C0
int16_t create_loadmission(const char* filename) {
	uint16_t i, fg;
#ifdef TIE_MODERN
	uint8_t mfh_buf[MISSIONFILE_DISK_SIZE];
	uint8_t fg_buf[48 * EFGSTRUCT_DISK_SIZE];
#endif
	uint16_t wall_offsets[6];
	int16_t saved_seed;

	mission.train_craft_type = mission.train_craft_type_src;
	framerate = baseframerate;
	pstate.object_idx = 0xFF;
	playerside = 1;

	for (i = 0; i < NUM_OBJECTS; i++) {
		objects[i].ship_idx = 0;
		objects[i].death_timer = 0;
		objects[i].age_ticks = 0;
		objects[i].orient_dirty = 0;
	}
	for (i = 0; i < NUM_STATIC_OBJECTS; i++)
		staticobjects[i].species = 0;
	for (i = 0; i < NUM_CRAFTS; i++) /* craft slot side reset */
		objects[i].side = (uint8_t)-1;

	for (i = 0; i < NUM_SPECIES; i++)
		if (!species_table[i].flags)
			species_table[i].load_flags &= ~0x10u;

	/* Pre-clear the active flag on every radiomsg slot so the upcoming
	 * fediskio_readfileblock load doesn't carry over previous-mission
	 * stale entries when the new mission has fewer cut/radio cues. */
	for (i = 0; i < 16; i++)
		radiomsg[90 * i] = 0;

	if (!fediskio_tryopenfile(TIE_FILE_ROOT_FLIGHT_ASSET, filename, "rb", 1))
		return 0;

	fediskio_readfileblock(&missionversion, 2, 1, fileptr);
	if ((int16_t)missionversion > 0)
		TieStorage_Seek(fileptr, 0, TIE_SEEK_SET);
#ifdef TIE_MODERN
	fediskio_readfileblock(mfh_buf, MISSIONFILE_DISK_SIZE, 1, fileptr);
	MissionFile_decode(&mission_file_header, mfh_buf);
	{
		uint16_t n_fg;
		int16_t file_num_msg;
		uint16_t num_msg;
		uint16_t num_goals;

		if (mission_file_header.num_msg < 0 || mission_file_header.num_goals < 0)
			shell_programexit("Mission file has a negative message or goal count");

		/* Stage the FG records as the on-disk byte image, then decode each
		 * into the natively-aligned fg_array. The .TIE format hard-caps
		 * num_fg at 48; clamp here so a malformed file cannot overrun the
		 * buffer or fg_array (the original binary trusted num_fg blindly). */
		n_fg = (uint16_t)mission_file_header.num_fg;
		if (n_fg > 48u)
			n_fg = 48u;
		fediskio_readfileblock(fg_buf, EFGSTRUCT_DISK_SIZE, n_fg, fileptr);
		for (i = 0; i < n_fg; ++i)
			EFGStruct_decode(&fg_array[i], fg_buf + i * EFGSTRUCT_DISK_SIZE);
		mission_file_header.num_fg = (int16_t)n_fg;

		/* Mission files store messages before goals. Clamp both counts and skip
		 * excess message records so goal decoding remains aligned. */
		file_num_msg = mission_file_header.num_msg;
		num_msg = (uint16_t)file_num_msg;
		if (num_msg > 16u)
			num_msg = 16u;
		fediskio_readfileblock(radiomsg, 0x5A, num_msg, fileptr);
		if ((uint16_t)file_num_msg > num_msg)
			TieStorage_Seek(fileptr, (long)((uint16_t)file_num_msg - num_msg) * 0x5A, TIE_SEEK_CUR);
		mission_file_header.num_msg = (int16_t)num_msg;

		num_goals = (uint16_t)mission_file_header.num_goals;
		if (num_goals > 4u)
			num_goals = 4u;
		fediskio_readfileblock(cut, 0x1C, num_goals, fileptr);
		mission_file_header.num_goals = (int16_t)num_goals;
	}
#else
	fediskio_readfileblock(&mission_file_header, MISSIONFILE_DISK_SIZE, 1, fileptr);
	fediskio_readfileblock(fg_array, EFGSTRUCT_DISK_SIZE, mission_file_header.num_fg, fileptr);
	fediskio_readfileblock(radiomsg, 0x5A, mission_file_header.num_msg, fileptr);
	fediskio_readfileblock(cut, 0x1C, mission_file_header.num_goals, fileptr);
#endif

	math2_getrandom();

	pstate.player_fg_idx = 0xFF;
	for (fg = 0; fg < mission_file_header.num_fg; fg++) {
		uint8_t sp = speciesconvert[(int8_t)fg_array[fg].species];

		species_table[sp].load_flags |= 0x10u;
		if (sp == 100) { /* expansion-pack capital family */
			uint8_t k;

			for (k = 101; k <= 105; k++)
				species_table[k].load_flags |= 0x10u;
		}
		/* First flight group flagged as the player wins; later
		 * player_flag entries are ignored (matches retail's
		 * `player_fg_idx == 255` first-wins guard). */
		if (fg_array[fg].player_flag && pstate.player_fg_idx == 0xFF) {
			pstate.player_fg_idx = (uint8_t)fg;
			pstate.player_spec_num = (uint8_t)spec_getspecnum(sp);
		}
		fgstatus[fg].world_position = OBJ_REF_WAYPOINT_BASE;
		if (fg_array[fg].special_flag)
			fg_array[fg].special_craft = (uint8_t)create_maxrandom((int8_t)fg_array[fg].count);
	}

	if (fediskio_tryclosefile(0))
		return 0;

	/* Backdrop: reseed RNG with the mission's stored seed so skybox is
	 * deterministic, then restore the live seed. */
	saved_seed = math2_randomseed;
	/* The signed backdrop byte is the skybox RNG seed. */
	math2_randomseed = (int16_t)(int8_t)mission_file_header.mission.backdrop;
	create_createbackdrop();
	math2_randomseed = saved_seed;

	/* Start offsets for [front, back, left, right, top, bottom] backdrop tiles. */
	wall_offsets[0] = 0;
	wall_offsets[1] = backdropfrontcnt;
	wall_offsets[2] = (uint16_t)(backdropfrontcnt + backdropbackcnt);
	wall_offsets[3] = (uint16_t)(wall_offsets[2] + backdropleftcnt);
	wall_offsets[4] = (uint16_t)(wall_offsets[3] + backdroprightcnt);
	wall_offsets[5] = (uint16_t)(wall_offsets[4] + backdroptopcnt);

	/* Retail walks the flight groups with the global fgcnt, leaving it at
	 * num_fg once the backdrop tiles are placed. */
	for (fgcnt = 0; fgcnt < mission_file_header.num_fg; fgcnt++) {
		uint16_t sp;
		int wall_kind;
		uint16_t tile;
		uint16_t slot;

		if (!fg_array[fgcnt].species)
			continue;

		sp = speciesconvert[(int8_t)fg_array[fgcnt].species];
		if (!(species_table[(uint8_t)sp].side & 0x20))
			continue;

		if (sp == 87) {
			/* Planet version selects both the resource variant and palette. */
			uint16_t p;

			for (p = 114; p < 116; p++)
				if (!(species_table[p].load_flags & 0x10))
					break;
			if ((int8_t)fg_array[fgcnt].version < 8) {
				species_table[p].lfd_entry = (uint8_t)(species_table[sp].lfd_entry + fg_array[fgcnt].version);
				species_table[p].bitmap_data = planetpalptrs[(int8_t)fg_array[fgcnt].version];
			}
			sp = p;
		}

		species_table[sp].load_flags |= 0x10u;

		/* way_z[0] in the file is re-interpreted here as wall id (0..5). */
		wall_kind = (uint16_t)fg_array[fgcnt].way_z[0];
		if (wall_kind > 5)
			wall_kind = 5;
		tile = (uint16_t)(((-fg_array[fgcnt].way_y[0] & 0x0F) << 4) + (fg_array[fgcnt].way_x[0] & 0x0F));
		slot = wall_offsets[wall_kind];
		backdropspecies[slot] = (uint8_t)sp;
		backdropposition[slot] = (uint8_t)tile;
		wall_offsets[wall_kind] = (uint16_t)(slot + 1);
	}

	/* Reset the mission elapsed clock; tie_updatetime ticks subsec each
	 * frame and cascades into second/minute/hour. Without this, the
	 * previous mission's elapsed time persists into the new mission and
	 * skews HUD readouts plus any `date.minute >= N` triggers. The
	 * reserved_0..2 bytes are intentionally not cleared (binary parity).
	 * Matches retail's byte writes at 0x16A4B-0x16A69. */
	date.hour = 0;
	date.minute = 0;
	date.second = 0;
	date.subsec = 0;
	timeleft.hour = 0;

	if (!mission.train_craft_type) {
		/* Default the mission timer to 20:mission.time_sec when the
		 * .TIE record has time_min==0 && time_sec==0; otherwise use
		 * the file's specified time_min. Matches the binary's
		 * `time_min + time_sec` zero check at 0x16A85. */
		if ((int8_t)mission_file_header.mission.time_min + (int8_t)mission_file_header.mission.time_sec)
			timeleft.minute = mission_file_header.mission.time_min;
		else
			timeleft.minute = 20;
		timeleft.second = mission_file_header.mission.time_sec;
	} else {
		/* Training mode: swap in the player's chosen training ship. */
		fg_array[0].species = mission.train_craft_type_src;
		species_table[speciesconvert[mission.train_craft_type_src]].load_flags |= 0x10u;
		timeleft.second = 59;
		timeleft.minute = (uint8_t)(10 - mission.train_level);
	}

	return 1;
}

/* ============================================================== */
/*   Mission lifecycle                                             */
/* ============================================================== */

// FUNCTION: TIE95 0x16B04
CraftData* create_createhyperin(void) {
	uint16_t i;

	create_createmission();
	for (i = 0; i < NUM_OBJECTS; i++) {
		if (i == pstate.object_idx)
			continue;
		objects[i].ship_idx = 0;
		objects[i].age_ticks = 0;
		objects[i].death_timer = 0;
	}
	for (i = 0; i < NUM_STATIC_OBJECTS; i++)
		staticobjects[i].species = 0;

	pstate.player->roll = 0;
	pstate.player->pitch = 0x4000;
	pstate.player_craft->orient_pitch = 0x4000;
	pstate.player->heading = 0;
	mission_file_header.num_fg = 0;
	return pstate.player_craft;
}

// FUNCTION: TIE95 0x16B8C
// FUNCTION: TIE98 0x411200
void create_createmission(void) {
	uint16_t i;
	uint16_t si;

	idnumber = 0;

	for (fgcnt = 0; fgcnt < mission_file_header.num_fg; fgcnt++) {
		uint16_t count;
		uint16_t j;

		mission.primary_fg[fgcnt] = 0;
		mission.secondary_fg[fgcnt] = 0;
		mission.bonus_fg[fgcnt] = 0;
		fgstatus[fgcnt].active = 0;
		fgstatus[fgcnt].arrival_triggered = 0;
		fgstatus[fgcnt].waves_remaining = 0;

		count = (int8_t)fg_array[fgcnt].count;
		if (species_table[speciesconvert[(int8_t)fg_array[fgcnt].species]].ship_class == 8)
			count *= count;
		if (fg_array[fgcnt].link_flag) {
			count -= mission.mission_linked_data[fg_array[fgcnt].link_code];
			if (count >= 0x8000)
				count = 0;
		}

		if (diffmask[mission.difficulty] & fgdiffmask[fg_array[fgcnt].difficulty]) {
			fgstatus[fgcnt].counts[FG_COUNT_TOTAL] = (uint8_t)((fg_array[fgcnt].waves + 1) * count);
			if (fg_array[fgcnt].special_flag ||
				(int8_t)fg_array[fgcnt].special_craft < (int8_t)fg_array[fgcnt].count)
				fgstatus[fgcnt].special_counts[FG_COUNT_TOTAL] = (uint8_t)(fg_array[fgcnt].waves + 1);
			else
				fgstatus[fgcnt].special_counts[FG_COUNT_TOTAL] = 0;
		} else {
			fgstatus[fgcnt].counts[FG_COUNT_TOTAL] = 0;
			fgstatus[fgcnt].special_counts[FG_COUNT_TOTAL] = 0;
		}
		/* Clear the previous mission's counters; slot 0 is the total set above. */
		for (j = 1; j < FG_COUNT_SLOTS; j++) {
			fgstatus[fgcnt].counts[j] = 0;
			fgstatus[fgcnt].special_counts[j] = 0;
		}

		fgstatus[fgcnt].primary_status = 4;
		fgstatus[fgcnt].secondary_status = 4;
		fgstatus[fgcnt].fg_complete = 4;

		/* Immediate-spawn test: active, in-difficulty, no arrival trigger. */
		if (fg_array[fgcnt].species &&
			(fgdiffmask[fg_array[fgcnt].difficulty] & diffmask[mission.difficulty]) &&
			fgstatus[fgcnt].counts[FG_COUNT_TOTAL] && !fg_array[fgcnt].start_cond[0].cond &&
			!fg_array[fgcnt].start_delay_min && !fg_array[fgcnt].start_delay_sec) {
			create_startflightgroup(-1);
		}
	}

	currentdebrisslot = DEBRIS_FIRST_SLOT;
	hyperspaceflag = 0;
	hyperabortflag = 1;
	entercombatflag = 0;
	pstate.hyperin_state = 0;

	/* A loaded panel cache is retained across replay restoration. */
	if (!panelsloadedflag) {
		festring_setcursor(0, screenYRes / 2 - fontheight - fontheight);
		fsfx_loadvoicelfd();
		festring_setcursor(0, screenYRes / 2 - fontheight);
		panel_loadpaneldata();
	}

	acceleratedtimesetting = 1;
	currenttarget = 0xFFFF;
	targetblinkstate = 0;
	targetblinkflag = 0;
	pstate.radar_enable = 1;
	/* Preserve object_idx, which was bound while creating the player's craft. */
	pstate.target_obj_idx = 0xFFFFu;
	pstate.radar_target0 = -1;
	pstate.radar_target1 = -1;
	pstate.radar_target2 = -1;
	for (i = 0; i < 4; i++)
		pstate.target_presets[i] = 0xFFFFu;
	pstate.radar_subtarget_state = 0;
	pstate.radio_target = -1;

	for (i = 0; i < 10; i++) {
		pstate.subsystem_repair_priority[i] = (uint8_t)i;
		pstate.subsystem_health_percent[i] = 100;
		pstate.subsystem_repair_seconds[i] = 0;
	}
	for (i = 0; i < 20; i++)
		timers[i] = 0;

	camera.view_dir_dirty = 0;
	camera.side_angle = 0;
	camera.up_angle = 0;
	camera.view_zoom = 0x400;
	camera.view_camera_control = 0;
	camera.view_target_tracking = 0;
	camera.view_zoom_flag = 0;
	panel_forcenewviewdir(0);
	keypress = 0;

	/* Clear prev_x_roll_mode and the four adjacent input accumulators.
	 * Retail writes at 0xEB182, 0xEB184, 0xEB186, 0xEB188, 0xEB18A —
	 * pstate base + {0x2A, 0x2C, 0x2E, 0x30, 0x32}. msg_arg_obj_idx at
	 * +0x28 is intentionally preserved across the mission boundary
	 * (the binary's `& 0xFFFF` keeps it). */
	pstate.prev_x_roll_mode = 0;
	pstate.axis_x_accum = 0;
	pstate.axis_y_accum = 0;
#ifdef TIE_MODERN
	pstate.axis_roll_accum = 0;
#endif
	pstate.prev_inputbuttons = 0;
	pstate.double_tap_timer = 0;

	camera.view_target_obj = pstate.object_idx;
	thrustmastertopflag = 0;
	framerate = 15;
	if (TIE_FLIGHT_TIE98)
		g_flightInitialTextureCacheFlushPending = 1;
	fullupdateflag = 1;
	calcframerate = 1;
	tickcounter += xtimer_Time_Elapsed();
	tickcounter = 0;
	messagecnt = 0;

	for (si = 0; si < NUM_SPEC; si++) {
		uint16_t row;

		for (row = 0; row < 6; row++)
			mission.kills_losses[row][si] = 0;
		mission.kills_by_type[si] = 0;
		mission.captures_by_type[si] = 0;
	}
	for (i = 0; i < 16; i++)
		mission.radiomsg_triggered[i] = 0;

	mission.mission_new_rank = 0;
	mission.mission_medal = 0;
	mission.mission_secret_medal = 0;
	mission.player_status = 3;
	mission.end_flag = 0;
	mission.primary_complete = 0;
	mission.secondary_complete = 0;
	mission.bonus_complete = 0;
	mission.penalty_flag = 0;

	cheatingflag = (uint8_t)(inflight_unlimited | inflight_invulnerable);
}

/* ============================================================== */
/*   FG activation                                                 */
/* ============================================================== */

// FUNCTION: TIE95 0x1706C
int create_startflightgroup(uint16_t craft_slot) {
	fgstatus[fgcnt].active = 1;
	if (!(species_table[speciesconvert[(int8_t)fg_array[fgcnt].species]].side & 0x80)) {
		create_createflightgroup(craft_slot);
		fgstatus[fgcnt].waves_remaining = fg_array[fgcnt].waves;
	} else {
		create_createstaticflightgroup(craft_slot);
		fgstatus[fgcnt].waves_remaining = 0;
	}
	return 1;
}

/* ============================================================== */
/*   Per-frame FG spawn driver                                     */
/* ============================================================== */

// FUNCTION: TIE95 0x1713C
void create_updatefgstatus(void) {
	if (timers[TIMER_FG_ARRIVAL] == 0) {
		timers[TIMER_FG_ARRIVAL] = 236;
		for (fgcnt = 0; fgcnt < mission_file_header.num_fg; fgcnt++) {
			if (fgstatus[fgcnt].active == 0 && fgstatus[fgcnt].arrival_triggered == 0) {
				/* Arrival-condition check for dormant FG: start_op == 1
				 * selects OR of both start conditions, anything else AND. */
				uint16_t ok0;
				uint16_t ok1;
				uint8_t result;

				if ((fgdiffmask[fg_array[fgcnt].difficulty] & diffmask[mission.difficulty]) == 0)
					continue;
				if (fgstatus[fgcnt].counts[FG_COUNT_TOTAL] == 0)
					continue;
				ok0 = score_checkcondition(
					(int8_t)fg_array[fgcnt].start_cond[0].cond, (int8_t)fg_array[fgcnt].start_cond[0].type,
					(int8_t)fg_array[fgcnt].start_cond[0].id, (int8_t)fg_array[fgcnt].start_cond[0].pct, 1);
				ok1 = score_checkcondition(
					(int8_t)fg_array[fgcnt].start_cond[1].cond, (int8_t)fg_array[fgcnt].start_cond[1].type,
					(int8_t)fg_array[fgcnt].start_cond[1].id, (int8_t)fg_array[fgcnt].start_cond[1].pct, 1);
				if ((int8_t)fg_array[fgcnt].start_op == 1)
					result = (uint8_t)(ok0 | ok1);
				else
					result = (uint8_t)(ok0 & ok1);
				if (result & 1) {
					fgstatus[fgcnt].arrival_delay = 60 * (int8_t)fg_array[fgcnt].start_delay_min +
													(int8_t)fg_array[fgcnt].start_delay_sec;
					fgstatus[fgcnt].arrival_triggered = 1;
				}
			} else {
				/* Live FG: respawn its next wave once every craft is dead. */
				uint8_t all_dead;
				uint16_t j;

				if (fgstatus[fgcnt].waves_remaining == 0)
					continue;
				if (fgstatus[fgcnt].counts[FG_COUNT_TOTAL] == 0)
					continue;
				all_dead = 1;
				for (j = 0; j < NUM_CRAFTS; j++) {
					craftptr = objects[j].craft_ptr;
					if (objects[j].ship_idx != 0 && fgcnt == objects[j].fg_idx)
						all_dead = 0;
				}
				if (!all_dead)
					continue;
				create_createflightgroup(-1);
				if (fgstatus[fgcnt].waves_remaining != 0)
					fgstatus[fgcnt].waves_remaining--;
			}
		}
	}

	if (timers[TIMER_FG_SPAWN] == 0) {
		timers[TIMER_FG_SPAWN] = 236;
		for (fgcnt = 0; fgcnt < mission_file_header.num_fg; fgcnt++) {
			if (fgstatus[fgcnt].active != 0 || fgstatus[fgcnt].arrival_triggered != 1)
				continue;
			if (fgstatus[fgcnt].arrival_delay == 0)
				create_startflightgroup(-1);
			else
				fgstatus[fgcnt].arrival_delay--;
		}
	}
}

/* ============================================================== */
/*   Dynamic flight-group spawn                                    */
/* ============================================================== */

// FUNCTION: TIE95 0x17460
int create_createflightgroup(uint16_t craft_slot) {
	uint16_t species_idx;
	uint16_t carrier_fg;
	uint16_t j;
	uint16_t found;
	uint16_t anchor;
	uint8_t mission_clock_started;
	uint16_t adjust_if_hostile;

	/* Arriving FGs run their hyperspace-in / hangar-launch animation
	 * only once the mission clock has started (any of hour, minute or
	 * second non-zero), so the initial wave at mission load is placed
	 * directly. */
	mission_clock_started = date.hour | date.minute | date.second;
	fghyperspace = 0;
	fghangar = 0;

	if (!fg_array[fgcnt].start_fg_used || !mission_clock_started || mission.train_craft_type ||
		craft_slot != (uint16_t)-1) {
		/* Waypoint-anchored spawn. Use waypoint 0 (live) and heading
		 * derived from waypoint 4 when set; else default heading. */
		int16_t z_angle;

		create_getworldposition(OBJ_REF_WAYPOINT_BASE, fgcnt);
		fglocx = worldlocx;
		fglocy = worldlocy;
		fglocz = worldlocz;

		if (fg_array[fgcnt].way_used[4]) {
			create_getworldposition(0x8004, fgcnt);
			trig2_ctop(worldlocx - fglocx, worldlocy - fglocy, worldlocz - fglocz);
			fgheadingxy = trig2_xyangle;
			z_angle = trig2_zangle;
		} else {
			z_angle = 0x4000;
			trig2_xyangle = 0;
			fgheadingxy = 0;
			trig2_zangle = 0x4000;
		}
		fgheadingz = z_angle;

		/* Later "hyper-in" arrival: step 8x further behind the FG along
		 * the reversed approach vector so the jump-in animation has
		 * travel distance. */
		if (!mission.train_craft_type && !fg_array[fgcnt].start_fg_used && mission_clock_started &&
			craft_slot == (uint16_t)-1) {
			trig2_xyangle += 0x8000;
			trig2_zangle = 0x8000 - trig2_zangle;
			trig2_movexyz(0xFFFF, trig2_xyangle, trig2_zangle);
			trig2_xmovedist *= 8;
			trig2_ymovedist *= 8;
			trig2_zmovedist *= 8;
			fglocx += trig2_xmovedist;
			fglocy += trig2_ymovedist;
			fglocz += trig2_zmovedist;
			fghyperspace = 1;
		}
		fgformation = fg_array[fgcnt].formation;
		fgseparation = fg_array[fgcnt].form_spacing;
	} else {
		/* Carrier-spawn via a fleet leader FG (hangar launch). */
		carrier_fg = (int8_t)fg_array[fgcnt].start_fg;
		found = 0;
		for (j = 0; j < NUM_CRAFTS; j++) {
			if (objects[j].ship_idx) {
				craftptr = objects[j].craft_ptr;
				if (carrier_fg == objects[j].fg_idx && craftptr->leader_obj_idx == 255) {
					found = 1;
					anchor = j;
					break;
				}
			}
		}
		if (!found)
			return 0;

		species_idx = objects[anchor].craft_ptr->species_idx;
		craftptr = objects[anchor].craft_ptr;

		/* Two rotated points relative to the carrier: drop anchor
		 * (cockpit_x, cockpit_y, cockpit_z) and approach vector
		 * (engine_x, engine_y, engine_z). Difference feeds heading. */
		pai_calcrotatedpoint(&objects[anchor], spec_data[craftptr->species_idx].cockpit_x,
							 spec_data[craftptr->species_idx].cockpit_y,
							 spec_data[craftptr->species_idx].cockpit_z);
		fglocx = objects[anchor].world_x + rotatedx;
		fglocy = objects[anchor].world_y + rotatedy;
		fglocz = objects[anchor].world_z + rotatedz;
		pai_calcrotatedpoint(&objects[anchor], spec_data[species_idx].engine_x,
							 spec_data[species_idx].engine_y, spec_data[species_idx].engine_z);
		worldlocx = objects[anchor].world_x + rotatedx;
		worldlocy = objects[anchor].world_y + rotatedy;
		worldlocz = objects[anchor].world_z + rotatedz;
		trig2_ctop(worldlocx - fglocx, worldlocy - fglocy, worldlocz - fglocz);
		fgheadingxy = trig2_xyangle;
		fgheadingz = trig2_zangle;

		fghangar = 1;
		if ((int8_t)fg_array[fgcnt].count <= 3)
			fgformation = 0;
		else
			fgformation = 6;
		fgseparation = 0;
	}

	fgside = fg_array[fgcnt].side;
	fgversion = fg_array[fgcnt].version;
	fgflightflag = 0;
	fggenus = species_table[speciesconvert[(int8_t)fg_array[fgcnt].species]].ship_class;
	fgskill = fg_array[fgcnt].skill;

	/* Easy-mode friendly-side skill bump and hostile-craft skill dock.
	 * Fighter orders that map to plan 19 (rendezvous) skip the dock. */
	if (!mission.difficulty) {
		if (fggenus == GENUS_STARSHIP || fggenus == GENUS_PLATFORM) {
			adjust_if_hostile = 1;
		} else if (fggenus == GENUS_FIGHTER) {
			uint16_t k;

			adjust_if_hostile = 1;
			for (k = 0; k < 3; k++) {
#ifdef TIE_MODERN
				// HARDENING: orders past the 33-entry tables (retail HI1W.TIE uses 35) take the null plan.
				if (fg_array[fgcnt].ai[k].order < sizeof(ordersldr) &&
					ordersldr[(int8_t)fg_array[fgcnt].ai[k].order] == 19)
#else
				if (ordersldr[(int8_t)fg_array[fgcnt].ai[k].order] == 19)
#endif
					adjust_if_hostile = 0;
			}
		}
		if (fgside == 1) {
			fgskill++;
			if (fgskill >= 5)
				fgskill = 4;
		} else if (adjust_if_hostile && (fgside == 0 || fgside == 4) && fgskill > 0 && fgskill < 5) {
			fgskill--;
		}
	}

	/* Spawn loop: either the whole FG, or just one craft index. */
	if (craft_slot == (uint16_t)-1) {
		leaderflag = 0xFF;
		for (craftcnt = 0; craftcnt < (int8_t)fg_array[fgcnt].count; craftcnt++) {
			if (fgstatus[fgcnt].counts[FG_COUNT_ARRIVED] < fgstatus[fgcnt].counts[FG_COUNT_TOTAL]) {
				if (create_createcraft() == 0xFFFF)
					return 0;
				fgstatus[fgcnt].counts[FG_COUNT_ARRIVED]++;
				if ((int8_t)fg_array[fgcnt].special_craft == craftcnt)
					fgstatus[fgcnt].special_counts[FG_COUNT_ARRIVED]++;
			}
		}
	} else if (fgstatus[fgcnt].counts[FG_COUNT_ARRIVED] < fgstatus[fgcnt].counts[FG_COUNT_TOTAL]) {
		craftcnt = craft_slot;
		if (create_createcraft() == 0xFFFF)
			return 0;
		fgstatus[fgcnt].counts[FG_COUNT_ARRIVED]++;
		if (craftcnt == (int8_t)fg_array[fgcnt].special_craft)
			fgstatus[fgcnt].special_counts[FG_COUNT_ARRIVED]++;
	}

	fgstatus[fgcnt].counts[FG_COUNT_SLOT_17] = 0;

	/* Reinforcement chatter: radio report + MS sequence selector fires
	 * for FGs that arrive after the mission clock has started, so the
	 * initial-wave spawns at mission load are silent. */
	if (mission_clock_started && craft_slot == (uint16_t)-1) {
		int16_t seq;

		msg_reportfgcreation(fgcnt, spec_getspecnum(fgspecies));

		if (!fgsidecreated) {
			if (fggenus != GENUS_STARSHIP)
				seq = 13;
			else
				seq = 12;
		} else if (fgsidecreated == 1 || fgsidecreated == 4) {
			if (fggenus != GENUS_STARSHIP)
				seq = 11;
			else
				seq = 10;
		} else {
			if (fggenus != GENUS_STARSHIP)
				seq = 15;
			else
				seq = 14;
		}
		fscript_MsSetSequence(seq);
	}
	return 1;
}

/* ============================================================== */
/*   Per-craft spawn                                               */
/* ============================================================== */

// FUNCTION: TIE95 0x17BF8
uint16_t create_createcraft(void) {
	uint16_t ship_idx;
	uint16_t obj_slot;
	uint16_t gend;
	uint16_t spec_num;
	uint16_t bank;
	uint16_t k;
	uint8_t laser_total;
	uint16_t order_ldr;
	uint16_t order_flw;
	uint16_t throttle;
	uint8_t field_0f;

	ship_idx = speciesconvert[fg_array[fgcnt].species];
	fgspecies = (uint8_t)ship_idx;

	/* Find a free FlightObject slot in the genus range. */
	gend = genus_table[fggenus].limit;
	for (obj_slot = genus_table[fggenus].start; obj_slot < gend; obj_slot++)
		if (!objects[obj_slot].ship_idx)
			break;
	if (obj_slot >= gend)
		return 0xFFFF;

	/* Bind player if this is the training/player craft index. */
	if (fg_array[fgcnt].player_flag && craftcnt == fg_array[fgcnt].player_flag - 1) {
		pstate.object_idx = obj_slot;
		pstate.player = &objects[obj_slot];
	}

	objects[obj_slot].ship_idx = (uint8_t)ship_idx;
	objects[obj_slot].idnumber = idnumber;
	objects[obj_slot].craft_ptr = &crafts[obj_slot];
	craftptr = &crafts[obj_slot];
	idnumber++;

	if (species_table[ship_idx].bound_hwidth < 0x2000)
		objects[obj_slot].collision_radius = (int16_t)(4 * species_table[ship_idx].bound_hwidth);
	else
		objects[obj_slot].collision_radius = 0x7FFF;

	spec_num = craftptr->species_idx = (uint8_t)spec_getspecnum(ship_idx);
	fgsidecreated = fgside;
	objects[obj_slot].side = fgside;
	objects[obj_slot].genus = fggenus;
	objects[obj_slot].category = species_table[ship_idx].category;
	objects[obj_slot].age_ticks = 0;
	objects[obj_slot].death_timer = 0;
	objects[obj_slot].decal_color = fg_array[fgcnt].camoflage;
	objects[obj_slot].self_idx = (int16_t)obj_slot;
	objects[obj_slot].ship_type_override = (uint8_t)ship_idx;
	objects[obj_slot].fg_idx = (uint8_t)fgcnt;

	craftptr->leader_obj_idx = leaderflag;
	if (leaderflag == 0xFF)
		leaderflag = (uint8_t)obj_slot;

	craftptr->formation = fgformation;
	craftptr->craft_idx_in_fg = (uint8_t)craftcnt;
	craftptr->formation_separation = fghangar ? 0 : fgseparation;
	craftptr->push_accum_x = craftptr->push_accum_y = craftptr->push_accum_z = 0;

	/* Formation-relative spawn offset from leader craft. */
	if (craftptr->leader_obj_idx != 0xFF) {
		const int16_t sep = (int16_t)(craftptr->formation_separation + 1);
		int16_t form_x =
			sep * _formposx[craftptr->formation][craftptr->craft_idx_in_fg] * spec_data[spec_num].bound_width;
		int16_t form_y =
			sep * _formposy[craftptr->formation][craftptr->craft_idx_in_fg] * spec_data[spec_num].bound_depth;
		int16_t form_z = sep * _formposz[craftptr->formation][craftptr->craft_idx_in_fg] *
						 spec_data[spec_num].bound_height;

		if (sep == 1) {
			form_x += _formposx[craftptr->formation][craftptr->craft_idx_in_fg] *
					  (spec_data[spec_num].bound_width / 2);
			form_z += _formposz[craftptr->formation][craftptr->craft_idx_in_fg] *
					  (spec_data[spec_num].bound_height / 2);
			form_y += _formposy[craftptr->formation][craftptr->craft_idx_in_fg] *
					  (spec_data[spec_num].bound_depth / 4);
		}
		pai_calcrotatedpoint(&objects[craftptr->leader_obj_idx], form_x, form_z, form_y);
		if (spec_data[spec_num].model_scale_shift) {
			/* Binary emits `shl reg, cl` (sign-agnostic); shifting a
			 * negative int32_t in C is UB, so route through uint32_t. */
			const int shift = spec_data[spec_num].model_scale_shift;
			rotatedx = (int32_t)((uint32_t)rotatedx << shift);
			rotatedy = (int32_t)((uint32_t)rotatedy << shift);
			rotatedz = (int32_t)((uint32_t)rotatedz << shift);
		}
	} else {
		rotatedx = rotatedy = rotatedz = 0;
	}

	objects[obj_slot].world_x_prev = objects[obj_slot].world_x = fglocx + rotatedx;
	objects[obj_slot].world_y_prev = objects[obj_slot].world_y = fglocy + rotatedy;
	objects[obj_slot].world_z_prev = objects[obj_slot].world_z = fglocz + rotatedz;

	/* Cargo: special_craft index picks cargo[1] (unique name); others
	 * get cargo[0] (group name). */
	{
		const char* src;

		if (craftptr->craft_idx_in_fg == fg_array[fgcnt].special_craft)
			src = fg_array[fgcnt].contents[1];
		else
			src = fg_array[fgcnt].contents[0];
		for (k = 0; k < 16; k++)
			craftptr->cargo[k] = src[k];
	}

	craftptr->boarding_state = 0;
	craftptr->subsystem_active = 0x03FF;
	craftptr->installed_subsystems = 0x1FFF;

	/* Pose. */
	objects[obj_slot].heading = fgheadingxy;
	craftptr->orient_heading = (uint16_t)fgheadingxy;
	objects[obj_slot].pitch = fgheadingz;
	craftptr->orient_pitch = (uint16_t)fgheadingz;
	objects[obj_slot].roll = 0;
	objects[obj_slot].spin_rate = 0;
	objects[obj_slot].orient_dirty = 1;
	objects[obj_slot].move_dirty = 1;

	/* AI var clears. */
	craftptr->ai_roll_state = 0;
	craftptr->ai_pitch_state = 0;
	craftptr->ai_pitch_force = craftptr->ai_dive_state = craftptr->ai_climb_state = 0;
	craftptr->ai_heading_state = 0;
	craftptr->ai_target_a = -1;
	craftptr->ai_target_c = -1;
	craftptr->ai_target_b = -1;
	craftptr->ai_target_d = -1;

	/* Cached spec stats (queried each AI tick; readonly after createcraft). */
	craftptr->roll_rate_cache = spec_data[spec_num].roll_rate;
	craftptr->pitch_rate_cache = spec_data[spec_num].pitch_rate;
	craftptr->heading_rate_cache = spec_data[spec_num].heading_rate;
	craftptr->max_speed_cache = spec_data[spec_num].max_speed;

	/* Skill tier used by AI, turret cooldowns, and targeting jitter. */
	craftptr->skill_value = skilltranslate[fgskill];

	/* Laser banks. */
	craftptr->laser_group_cnt = 0;
	laser_total = 0;
	for (bank = 0; bank < 2; bank++) {
		uint16_t start;
		uint16_t end;

		craftptr->laser_type[bank] = spec_data[spec_num].laser_type[bank];
		craftptr->laser_owner_player[bank] = (uint8_t)(obj_slot == pstate.object_idx);
		craftptr->laser_burst_remaining[bank] = 0;
		craftptr->laser_first_slot[bank] = 0;
		craftptr->laser_cooldown[bank] = 0;

		if (!craftptr->laser_type[bank])
			continue;

		laser_total += spec_data[spec_num].laser_count[bank];
		start = spec_data[spec_num].laser_start[bank];
		end = spec_data[spec_num].laser_end[bank];
		if (spec_data[spec_num].laser_fire_mode[bank] != 2) {
			craftptr->laser_group_cnt++;
			craftptr->laser_first_slot[bank] = (uint8_t)start;
		}
		for (k = start; k <= end; k++) {
			if (spec_data[spec_num].laser_fire_mode[bank] == 2)
				craftptr->weapon_slots[k].type = 2;
			else
				craftptr->weapon_slots[k].type = craftptr->laser_type[bank];
			craftptr->weapon_slots[k].charge = 127;
			craftptr->weapon_slots[k].ammo = 0;
			craftptr->weapon_slots[k].target_obj = -1;
		}
	}
	craftptr->laser_power = 2;
	craftptr->weapon_group_cnt = laser_total;
	if (!craftptr->laser_group_cnt)
		craftptr->subsystem_active ^= 0x10u;

	/* Missile banks. Only the missile boat (spec 12) gets two. */
	craftptr->missile_group_cnt = 0;
	for (bank = 0; bank < 2; bank++) {
		uint16_t start;
		uint16_t end;

		if (special_features_flag) {
			if (bank && spec_getspecnum(0x0C) != spec_num)
				craftptr->warhead_type[bank] = 0;
			else
				craftptr->warhead_type[bank] = warheadconvert[fg_array[fgcnt].warhead];
		} else if (bank && spec_getspecnum(0x0C) != spec_num) {
			craftptr->warhead_type[bank] = 0;
		} else if (obj_slot == pstate.object_idx && mission.mission_mode == 4) {
			craftptr->warhead_type[bank] = warheadconvert[mission.torp_used];
		} else {
			craftptr->warhead_type[bank] = warheadconvert[fg_array[fgcnt].warhead];
		}
		/* Missile boat gets a magpulse override in bank 1 in combat mode. */
		if (bank == 1 && spec_getspecnum(0x0C) == spec_num && !mission.train_craft_type)
			craftptr->warhead_type[bank] = 0x95;
		craftptr->missile_armed[bank] = 1;
		craftptr->missile_state[bank] = 0;

		if (!craftptr->warhead_type[bank])
			continue;
		craftptr->missile_group_cnt++;

		start = spec_data[spec_num].missile_start[bank];
		end = spec_data[spec_num].missile_end[bank];
		for (k = start; k <= end; k++) {
			uint8_t base;
			uint16_t torp;
			uint8_t count;

			craftptr->weapon_slots[k].target_obj = -1;
			craftptr->weapon_slots[k].charge = 127;
			craftptr->weapon_slots[k].type = craftptr->warhead_type[bank];

			/* missile_fire_mode mirrors laser_fire_mode but
			 * FEDISKIO_fillinspec never populates it -- always
			 * BSS-zero. math2_fraction(0, ...) returns 0 and the
			 * `if (!count) count = 1` fallback below substitutes 1. */
			base = spec_data[spec_num].missile_fire_mode[bank];
			if (!special_features_flag && obj_slot == pstate.object_idx && mission.mission_mode == 4)
				torp = mission.torp_used;
			else
				torp = fg_array[fgcnt].warhead;
			if (bank == 1 && spec_getspecnum(0x0C) == spec_num)
				torp = 5;
			count = (uint8_t)math2_fraction(base, warheadadjust[torp]);
			if (!count)
				count = 1;
			if (fgversion == 1)
				count *= 2;
			else if (fgversion == 2)
				count >>= 1;
			if (!count)
				count = 1;
			/* Player cap: 9 warheads except the missile boat. */
			if (obj_slot == pstate.object_idx && count > 9 && pstate.player_spec_num != spec_getspecnum(0x0C))
				count = 9;
			craftptr->weapon_slots[k].ammo = count;
		}
	}
	craftptr->missile_count_total = 0;
	if (!craftptr->missile_group_cnt)
		craftptr->subsystem_active ^= 0x08u;

	/* Reset mission counters. */
	craftptr->laser_hit = 0;
	craftptr->missile_hit = 0;
	craftptr->warhead_hit = 0;
	craftptr->total_kills = 0;
	craftptr->laser_fired = craftptr->laser_hit;
	craftptr->missile_fired = craftptr->missile_hit;
	craftptr->warhead_fired = craftptr->warhead_hit;
	for (k = 0; k < 69; k++)
		craftptr->kills_by_species[k] = 0;

	if (obj_slot == pstate.object_idx) {
		pstate.player_laser_hit = 0;
		pstate.player_laser_fired = (uint16_t)(pstate.object_idx ^ obj_slot); /* always 0; binary parity */
		pstate.player_missile_hit = 0;
		pstate.player_missile_fired = 0;
		pstate.player_warhead_hit = 0;
		pstate.player_warhead_fired = 0;
		pstate.player_total_kills = 0;
		pstate.friendly_kill_count = 0;
		for (k = 0; k < 69; k++)
			pstate.player_kills_per_species[k] = 0;
	}

	/* Hull. */
	craftptr->hull_max = (uint16_t)spec_data[spec_num].hull_max;
	craftptr->hull_damage = 0;
	craftptr->dead_0B0 = 0;
	craftptr->ion_drain_timer = 0;
	craftptr->pad_0B4 = 0;
	craftptr->hull_strength = (uint16_t)spec_data[spec_num].hull_strength;
	craftptr->was_hit_flag = 0;
	craftptr->pad_0B6 = 0;
	craftptr->dock_state_flags = 0;
	craftptr->ai_anim_flags = 0;
	craftptr->beam_state = 0;

	/* Auto-identify: same-side craft are pre-known on IFF, and any
	 * fighter (own or hostile) is identified by silhouette. Everything
	 * else (freighters, transports, capital ships, neutrals) starts
	 * un-inspected and must be scanned to reveal cargo + bump the
	 * inspection counter. */
	if (objects[obj_slot].side == playerside || objects[obj_slot].genus == GENUS_FIGHTER) {
		craftptr->inspected = 1;
		fgstatus[fgcnt].counts[FG_COUNT_INSPECTED]++;
		if (craftcnt == fg_array[fgcnt].special_craft)
			fgstatus[fgcnt].special_counts[FG_COUNT_INSPECTED]++;
	} else {
		craftptr->inspected = 0;
	}

	if (obj_slot == pstate.object_idx && !mission.difficulty) {
		craftptr->hull_max *= 3;
		craftptr->hull_strength *= 3;
	}

	/* Hyperdrive subsystem gating: clear SF_HYPER_DRIVE (0x80) for species
	 * without a hyperdrive, and for fg version 6 (no-hyper variant). */
	if (!spec_data[spec_num].has_hyperdrive && fgversion != 9)
		craftptr->subsystem_active ^= 0x80u;
	if (fgversion == 6)
		craftptr->subsystem_active ^= 0x80u;

	craftptr->forward_shield = spec_data[spec_num].shield_points;
	if (obj_slot == pstate.object_idx) {
		craftptr->rear_shield = spec_data[spec_num].shield_points;
		craftptr->is_player_craft = 1;
		if (!mission.difficulty) {
			craftptr->forward_shield *= 2;
			craftptr->rear_shield *= 2;
		}
	} else {
		craftptr->rear_shield = 0;
		craftptr->is_player_craft = 0;
		craftptr->forward_shield += spec_data[spec_num].shield_points;
		if (!mission.difficulty) {
			if (fgside == 1) {
				/* +50%. */
				craftptr->forward_shield += craftptr->forward_shield >> 1;
			} else if (fgside == 0 || fgside == 4) {
				craftptr->forward_shield =
					(int16_t)math2_fraction((uint16_t)craftptr->forward_shield, 0xA000u);
			}
		}
		if (craftptr->forward_shield < 0)
			craftptr->forward_shield = 30000;
	}

	/* Per-version shield / subsystem adjustments. */
	switch (fgversion) {
		case 3:
			craftptr->forward_shield = 0;
			craftptr->rear_shield = 0;
			craftptr->subsystem_active ^= 0x01u;
			break;
		case 4:
			craftptr->forward_shield >>= 1;
			craftptr->rear_shield >>= 1;
			craftptr->subsystem_active ^= 0x01u;
			break;
		case 7:
			craftptr->forward_shield = 0;
			craftptr->rear_shield = 0;
			break;
	}

	craftptr->shield_power = 2;

	/* Craft without a shield generator (has_shields == 0) zeroes the shield
	 * capacity and clears SF_SHIELDS. fgversion 8 = override/bypass. */
	if (!spec_data[spec_num].has_shields && fgversion != 8) {
		craftptr->forward_shield = 0;
		craftptr->rear_shield = 0;
		craftptr->subsystem_active ^= 0x01u;
		craftptr->installed_subsystems ^= 0x0800u;
	}

	/* Beam weapon selection + state init. */
	if (!special_features_flag && obj_slot == pstate.object_idx)
		craftptr->beam_type = mission.beam_used;
	else
		craftptr->beam_type = fg_array[fgcnt].beam;
	if (fggenus == GENUS_PLATFORM && fgspecies >= 0x3C && fgspecies < 0x41) {
		/* capital class: no beam */
		if (special_features_flag && craftptr->beam_type)
			printf("Warning! Platform has Beam Weapon Set!\n");
		craftptr->beam_type = 0;
	}
	craftptr->beam_power = 2;
	craftptr->beam_charge = 9999;
	if (!craftptr->beam_type) {
		craftptr->beam_charge = 0;
		craftptr->installed_subsystems ^= 0x1000u;
		craftptr->subsystem_active ^= 0x0100u;
		craftptr->installed_subsystems ^= 0x0010u;
	}

	craftptr->status_flags = craftptr->subsystem_active;
	craftptr->working_subsystems = craftptr->installed_subsystems;

	/* Sprite anim slots zero out. */
	objects[obj_slot].anim_frame = 0;
	objects[obj_slot].anim_frame_alt = 0;

	/* Mesh state: default damaged-capable meshes preloaded from
	 * initialdamagestate[mesh_type]; fgversion 5 marks beam turrets as
	 * already destroyed; capital-class (genus 5, special range) destroys
	 * an explicit componentsgone[] list. */
	for (k = 0; k < 40; k++) {
		craftptr->mesh_component_hp[k] = 0xFF;
		craftptr->mesh_state[k] = MESH_STATE_VISIBLE;
		craftptr->mesh_rotation[k] = 0;
	}
	if (TIE_FLIGHT_TIE98) {
		int mesh_count;
		int mesh;

		modelmesh_require_craft_capacity(ship_idx);
		mesh_count = modelmesh_getcount(ship_idx);
		for (mesh = 0; mesh < mesh_count; ++mesh) {
			const int mesh_type = modelmesh_gettype(ship_idx, mesh);
			if (modelmesh_isobjecttypemeshdamageable(ship_idx, mesh))
				// HARDENING: mesh types outside the table keep full damage state.
				craftptr->mesh_component_hp[mesh] = mesh_type < 32 ? initialdamagestate[mesh_type] : 0xFF;
			if (fgversion == 5 && (mesh_type == TIE_MESH_GUN_TURRET || mesh_type == TIE_MESH_SMALL_GUN ||
								   mesh_type == TIE_MESH_ROTARY_GUN_TURRET)) {
				craftptr->mesh_component_hp[mesh] = 0;
				craftptr->mesh_state[mesh] = MESH_STATE_BLOWN_OFF;
			}
		}
	} else {
		draw_Lockshipfileptrs(ship_idx);
		for (k = 0; k < objectblockptr->num_meshes; k++) {
			if (componentblockptr->flags & 2)
				craftptr->mesh_component_hp[k] = initialdamagestate[componentblockptr->mesh_type];
			if (fgversion == 5 && (componentblockptr->mesh_type == 4 || componentblockptr->mesh_type == 21 ||
								   componentblockptr->mesh_type == 5)) {
				craftptr->mesh_component_hp[k] = 0;
				craftptr->mesh_state[k] = MESH_STATE_BLOWN_OFF;
			}
			componentblockptr++;
		}
	}
	/* Capital-class platforms with a beam lose their componentsgone[]
	 * meshes: the first 6 for beam type 1, all 12 otherwise. */
	if (fggenus == GENUS_PLATFORM && fgspecies >= 0x3C && fgspecies < 0x41 && fg_array[fgcnt].beam) {
		const uint16_t base = (uint16_t)(12 * (fgspecies - 60));
		const uint16_t len = (fg_array[fgcnt].beam == 1) ? 6 : 12;

		for (k = base; k < base + len; k++) {
			if (componentsgone[k] == 0xFF)
				continue;
			craftptr->mesh_component_hp[componentsgone[k]] = 0;
			craftptr->mesh_state[componentsgone[k]] = MESH_STATE_BLOWN_OFF;
		}
	}

	/* AI orders: leader + follower both indexed by ai[0].order.
	 * Hyper/hangar states override with fixed opcodes 52/50. */
#ifdef TIE_MODERN
	// HARDENING: orders past the 33-entry tables (retail HI1W.TIE uses 35) take the null plan.
	order_ldr = fg_array[fgcnt].ai[0].order < sizeof(ordersldr) ? ordersldr[fg_array[fgcnt].ai[0].order] : 0;
	order_flw = fg_array[fgcnt].ai[0].order < sizeof(ordersflw) ? ordersflw[fg_array[fgcnt].ai[0].order] : 0;
#else
	order_ldr = ordersldr[fg_array[fgcnt].ai[0].order];
	order_flw = ordersflw[fg_array[fgcnt].ai[0].order];
#endif
	craftptr->default_order_ldr = (uint8_t)order_ldr;
	if (fghyperspace)
		craftptr->current_order = 52;
	else if (fghangar)
		craftptr->current_order = 50;
	else if (craftptr->leader_obj_idx == 0xFF)
		craftptr->current_order = (uint8_t)order_ldr;
	else
		craftptr->current_order = (uint8_t)order_flw;

	/* Throttle: pick from throttleconvert[ai[0].speed] unless order is
	 * 20 (hold position -> full thrust). Orders 0..2 and order 42 stop
	 * at the start waypoint (throttle = 0) EXCEPT for the player. */
	if ((order_ldr > 2 && order_ldr != 42) || obj_slot == pstate.object_idx)
		throttle = (order_ldr == 20) ? 0x8000u : throttleconvert[fg_array[fgcnt].ai[0].speed];
	else
		throttle = 0;

	craftptr->flight_flag = fgflightflag;
	craftptr->slam_active = -1;
	field_0f = spec_data[spec_num].field_0F;
	craftptr->throttle_speed = throttle;
	objects[obj_slot].current_speed =
		(int16_t)math2_fraction((uint16_t)spec_data[spec_num].max_speed, throttle);
	objects[obj_slot].speed_remainder = 0;

	if (obj_slot == pstate.object_idx) {
		pstate.player_weapon_group = 0;
		pstate.player_weapon_mode = 0;
		pstate.player_spec_field_0F = field_0f;
		pstate.player_craft = craftptr;
		pstate.player_spec_num = (uint8_t)spec_num;
	}

	/* Clear the 6 AI-preamble bytes. */
	for (k = 0; k < 3; k++) {
		craftptr->ai_complete_state[k] = 0;
		craftptr->ai_goal_progress[k] = 0;
	}
	craftptr->ai_state_1C = 0;
	craftptr->ai_target_ref = (int16_t)0xFF;
	craftptr->link_target_2E = -1;
	craftptr->spin_done_flag = -1;
	craftptr->escortee_fg_idx = 0xFF;
	craftptr->special_order_flag = 0;
	craftptr->hit_count = 0;
	craftptr->board_count = 0;
	craftptr->capture_count = 0;
	craftptr->waypoint_x_cache = 0;
	craftptr->pending_radio_command = craftptr->ai_target_ref;
	craftptr->tow_slave_ref = craftptr->link_target_2E;
	if (obj_slot == pstate.object_idx)
		craftptr->current_order = 0;

	craftptr->active_waypoint_idx = 4;
	craftptr->ai_update_rate = aiupdatetranslate[fgskill];

	pai_setupcraftaivars(obj_slot);
	pai_initplan(obj_slot);
	return obj_slot;
}

/* ============================================================== */
/*   Static flight-group spawn (mines / planets / asteroids)      */
/* ============================================================== */

// FUNCTION: TIE95 0x19054
void create_createstaticflightgroup(uint16_t craft_slot) {
	uint8_t species_idx;

	if (!fgstatus[fgcnt].counts[FG_COUNT_TOTAL])
		return;

	species_idx = speciesconvert[(int8_t)fg_array[fgcnt].species];
	if (species_table[species_idx].side & 0x80) {
		switch (species_table[species_idx].ship_class) {
			case 9:
				/* Planet: single object anchored on waypoint 0 (negated Y). */
				staging_static_x = fg_array[fgcnt].way_x[0];
				staging_static_y = fg_array[fgcnt].way_y[0];
				staging_static_z = fg_array[fgcnt].way_z[0];
				staging_static_heading = fg_array[fgcnt].heading;
				staging_static_pitch = fg_array[fgcnt].pitch;
				staging_static_roll = fg_array[fgcnt].rotation;
				staging_static_y = -staging_static_y;
				create_createstaticobject(fgcnt, 9, species_idx);
				break;
			case 8: {
				/* Mine grid: count x count cube oriented per fg.version & 3. */
				uint16_t row_x, col_x, row_y, col_y, row_z, col_z;
				uint16_t x_base, y_base, z_base;
				uint16_t side_m1;
				uint16_t obj_seq;
				uint16_t row;
				uint16_t col;

				row_x = col_x = row_y = col_y = row_z = col_z = 0;
				switch (fg_array[fgcnt].version & 3) {
					case 0:
						col_x = 64;
						row_y = 64;
						break;
					case 1:
						row_y = 64;
						col_z = 64;
						break;
					case 2:
						col_x = 64;
						row_z = 64;
						break;
				}

				x_base = fg_array[fgcnt].way_x[0];
				y_base = fg_array[fgcnt].way_y[0];
				z_base = fg_array[fgcnt].way_z[0];
				side_m1 = (int8_t)fg_array[fgcnt].count - 1;
				x_base -= col_x * side_m1 / 2;
				x_base -= row_x * side_m1 / 2;
				y_base = -y_base;
				y_base -= col_y * side_m1 / 2;
				y_base -= row_y * side_m1 / 2;
				z_base -= col_z * side_m1 / 2;
				z_base -= row_z * side_m1 / 2;
				staging_static_heading = fg_array[fgcnt].heading;
				staging_static_pitch = fg_array[fgcnt].pitch;
				staging_static_roll = fg_array[fgcnt].rotation;

				obj_seq = 0;
				for (row = 0; row < (int8_t)fg_array[fgcnt].count; row++) {
					for (col = 0; col < (int8_t)fg_array[fgcnt].count; col++) {
						if ((craft_slot == (uint16_t)-1 || craft_slot == obj_seq) &&
							fgstatus[fgcnt].counts[FG_COUNT_ARRIVED] <
								fgstatus[fgcnt].counts[FG_COUNT_TOTAL]) {
							staging_static_x = x_base + col_x * col + row_x * row;
							staging_static_y = y_base + col_y * col + row_y * row;
							staging_static_z = z_base + col_z * col + row_z * row;
							create_createstaticobject(fgcnt, 8, species_idx);
						}
						obj_seq++;
					}
				}
				break;
			}
			case 10: {
				/* Asteroid cloud: count rocks in a +-256 cube around waypoint 0,
				 * skipping positions that overlap an existing static. */
				uint16_t anchor_x, anchor_y, anchor_z;
				uint16_t ast;

				anchor_x = fg_array[fgcnt].way_x[0];
				anchor_y = fg_array[fgcnt].way_y[0];
				anchor_y = -anchor_y;
				anchor_z = fg_array[fgcnt].way_z[0];
				for (ast = 0; ast < (int8_t)fg_array[fgcnt].count; ast++) {
					uint16_t x, y, z;
					int16_t j;

					do {
						x = math2_getrandom() & 0x1FF;
						x -= 256;
						x += anchor_x;
						y = math2_getrandom() & 0x1FF;
						y -= 256;
						y += anchor_y;
						z = math2_getrandom() & 0x1FF;
						z -= 256;
						z += anchor_z;
						for (j = 0; j < NUM_STATIC_OBJECTS; j++) {
							if (staticobjects[j].species && x == staticobjects[j].world_x &&
								y == staticobjects[j].world_y && z == staticobjects[j].world_z)
								break;
						}
					} while (j < NUM_STATIC_OBJECTS);

					staging_static_x = x;
					staging_static_y = y;
					staging_static_z = z;
					staging_static_heading = 0;
					staging_static_pitch = 0;
					staging_static_roll = 0;
					create_createstaticobject(fgcnt, 10, (uint16_t)math2_getrandom() % 6 + 100);
				}
				break;
			}
		}
	}

	fgstatus[fgcnt].waves_remaining = 0;
}

/* ============================================================== */
/*   Static-object spawning                                        */
/* ============================================================== */

// FUNCTION: TIE95 0x195D0
uint16_t create_createstaticobject(uint16_t fg_idx, uint16_t ship_class, uint8_t species_idx) {
	const uint16_t slot = create_findstaticslot();

	if (slot != 0xFFFF) {
		staticobjects[slot].world_x = staging_static_x;
		staticobjects[slot].world_y = staging_static_y;
		staticobjects[slot].world_z = staging_static_z;
		staticobjects[slot].heading_byte = staging_static_heading;
		staticobjects[slot].pitch_byte = staging_static_pitch;
		staticobjects[slot].roll_byte = staging_static_roll;
		staticobjects[slot].idnumber = idnumber;
		staticobjects[slot].ship_class = (uint8_t)ship_class;
		staticobjects[slot].status_flags = 0x3FF;
		staticobjects[slot].anim_frame = 0;
		staticobjects[slot].mine_cooldown = 0;
		staticobjects[slot].fg_idx = (uint8_t)fg_idx;
		staticobjects[slot].species = species_idx;

		idnumber++;
		fgstatus[fg_idx].counts[FG_COUNT_ARRIVED]++;
	}
	return slot;
}

/* Resolve a 16-bit object reference to the worldlocx/y/z globals. See
 * header comment for the encoding ranges. */
// FUNCTION: TIE95 0x196C0
void create_getworldposition(uint16_t obj_or_kind, uint16_t fg_idx) {
	int32_t wy;

	if ((int)obj_or_kind < (int)OBJ_REF_STATIC_BASE) {
		worldlocx = objects[obj_or_kind].world_x;
		wy = objects[obj_or_kind].world_y;
		worldlocz = objects[obj_or_kind].world_z;
	} else if ((int)obj_or_kind < (int)OBJ_REF_WAYPOINT_BASE) {
		obj_or_kind -= OBJ_REF_STATIC_BASE;
		worldlocx = (int32_t)staticobjects[obj_or_kind].world_x * 256;
		wy = (int32_t)staticobjects[obj_or_kind].world_y * 256;
		worldlocz = (int32_t)staticobjects[obj_or_kind].world_z * 256;
	} else {
		if ((int)obj_or_kind == (int)OBJ_REF_WAYPOINT_BASE)
			obj_or_kind = fgstatus[fg_idx].world_position;
		obj_or_kind -= OBJ_REF_WAYPOINT_BASE;
		worldlocx = (int32_t)fg_array[fg_idx].way_x[obj_or_kind] * 256;
		wy = -((int32_t)fg_array[fg_idx].way_y[obj_or_kind] * 256);
		worldlocz = (int32_t)fg_array[fg_idx].way_z[obj_or_kind] * 256;
	}
	worldlocy = wy;
}

/* ============================================================== */
/*   Backdrop                                                      */
/* ============================================================== */

// FUNCTION: TIE95 0x197B4
void create_createbackdrop(void) {
	uint16_t i;

	backdropfrontcnt = 4;
	backdropbackcnt = 4;
	backdropleftcnt = 4;
	backdroprightcnt = 4;
	backdroptopcnt = 3;
	backdropbottomcnt = 3;

	/* 22 tile-direction descriptors: each is 16*lo_nibble + hi_nibble
	 * where each nibble is (rand & 0xE) + 4 looped until <= 0xC.
	 * Retail CREATE_createbackdrop uses the C library rand() (cosmetic starfield
	 * RNG), not MATH2_getrandom (mission-deterministic RNG). */
	for (i = 0; i < 22; i++) {
		int hi, lo;
		do {
			hi = (rand() & 0xE) + 4;
		} while (hi > 0xC);
		do {
			lo = (rand() & 0xE) + 4;
		} while (lo > 0xC);
		backdropposition[i] = (uint8_t)(hi + 16 * lo);
	}

	/* backdropspecies[0..21]: weighted pick -- 3/32 → planet (117),
	 * 9/32 → ramp 117..122, 20/32 → misc planets 125/126. */
	for (i = 0; i < 22; i++) {
		const int r = rand() & 0x1F;
		int pick;
		if (r < 3)
			pick = 117;
		else if (r >= 0xC)
			pick = 125 + (r & 1);
		else
			pick = (r / 3) + 117;
		backdropspecies[i] = (uint8_t)pick;
	}
}

// FUNCTION: TIE95 0x198A4
void create_blowoffcomponent(uint16_t obj_idx, int16_t blow_all) {
	uint16_t num_meshes;
	CraftData* cp;
	ShipModelMesh* comp;
	uint16_t mi;

	if (!TIE_FLIGHT_TIE98)
		draw_Lockshipfileptrs(objects[obj_idx].ship_idx);

	num_meshes = TIE_FLIGHT_EDITION(objectblockptr->num_meshes,
									(uint16_t)modelmesh_getcount(objects[obj_idx].ship_idx));
	comp = componentblockptr;
	if (num_meshes <= 1)
		return;

	cp = objects[obj_idx].craft_ptr;

	for (mi = 0; mi < num_meshes; ++mi, ++comp) {
		uint16_t debris;
		int16_t dheading;
		int16_t dpitch;
		int16_t spin;

		if (cp->mesh_state[mi] != MESH_STATE_VISIBLE)
			continue;
		if (TIE_FLIGHT_TIE98) {
			if (!modelmesh_isobjecttypemeshdamageable(objects[obj_idx].ship_idx, mi))
				continue;
		} else if (!((uint16_t)comp->flags & 2)) {
			continue;
		}

		debris = create_createcomponent(obj_idx, mi);
		if (debris == 0xFFFF)
			continue;

		spin = (math2_getrandom() & 0x3FFF) + 0x4000;
		dheading = (math2_getrandom() & 0x7FF) + 0x400;
		dpitch = (math2_getrandom() & 0xFFF) + 0x400;
		if ((uint16_t)math2_getrandom() & 1) {
			spin = -spin;
			dheading = -dheading;
		}
		if ((uint16_t)math2_getrandom() & 1)
			dpitch = -dpitch;

		objects[debris].spin_rate = spin;
		objects[debris].heading += dheading;
		objects[debris].pitch += dpitch;
		if ((uint16_t)objects[debris].pitch >= 0x8000) {
			objects[debris].pitch = -objects[debris].pitch;
			objects[debris].heading += -0x8000;
		}
		objects[debris].orient_dirty = 1;
		objects[debris].move_dirty = 1;
		objects[debris].death_timer = (int16_t)(236 * ((math2_getrandom() & 1) + 1));
		objects[debris].anim_frame_alt = 2;

		cp->mesh_state[mi] = MESH_STATE_BLOWN_OFF;
		/* [num_meshes] is the overlaid lightning anim frame counter,
		 * not a per-mesh state. 2 = jump the bolt script to frame 2. */
		cp->mesh_state[num_meshes] = 2;

		if (!blow_all)
			break;
	}
}

/* ============================================================== */
/*   Debris / ember / component spawn                              */
/* ============================================================== */

// FUNCTION: TIE95 0x19A6C
uint16_t create_createcomponent(uint16_t parent_obj, uint16_t mesh_idx) {
	const uint16_t slot = create_findslot(11);

	if (slot == 0xFFFF)
		return 0xFFFF;

	objects[slot] = objects[parent_obj];
	objects[slot].category = 3;
	objects[slot].genus = GENUS_DEBRIS;
	objects[slot].damage_state = 0;
	objects[slot].ship_idx = 89; /* sparks2 / mesh-debris base */
	objects[slot].ship_type_override = objects[parent_obj].ship_idx;
	objects[slot].age_ticks = 0;
	objects[slot].death_timer = (int16_t)(236 * ((math2_getrandom() & 7) + 4));
	objects[slot].anim_frame_alt = 0;
	objects[slot].anim_frame = (uint8_t)(2 * mesh_idx);
	return slot;
}

// FUNCTION: TIE95 0x19B48
uint16_t create_createember(uint16_t parent_obj) {
	const uint16_t slot = create_findslot(GENUS_EXPLOSION);
	int16_t heading_delta;
	int16_t pitch_delta;

	if (slot == 0xFFFF)
		return 0xFFFF;

	objects[slot] = objects[parent_obj];
	objects[slot].category = 5;
	objects[slot].genus = GENUS_EXPLOSION;
	objects[slot].damage_state = 0;
	objects[slot].ship_idx = (uint8_t)((math2_getrandom() & 1) + 0x85);
	objects[slot].ship_type_override = objects[parent_obj].ship_idx;

	/* Heading/pitch jitter in [256..2303], randomly negated. */
	heading_delta = (math2_getrandom() & 0x7FF) + 0x100;
	pitch_delta = (math2_getrandom() & 0x7FF) + 0x100;
	if ((uint16_t)(math2_getrandom() & 1))
		heading_delta = -heading_delta;
	if ((uint16_t)(math2_getrandom() & 1))
		pitch_delta = -pitch_delta;

	objects[slot].heading += heading_delta;
	objects[slot].pitch += pitch_delta;
	if ((uint16_t)objects[slot].pitch >= 0x8000) {
		/* Pitched past a pole: reflect the pitch and turn the heading by 180 degrees. */
		objects[slot].pitch = -objects[slot].pitch;
		objects[slot].heading -= -0x8000;
	}

	objects[slot].orient_dirty = 1;
	objects[slot].move_dirty = 1;
	objects[slot].age_ticks = 0;
	objects[slot].current_speed += (uint8_t)math2_getrandom() + 50;
	objects[slot].death_timer = (int16_t)(236 * ((math2_getrandom() & 3) + 1));
	objects[slot].anim_frame = 0;
	return slot;
}

// FUNCTION: TIE95 0x19CEC
uint16_t create_findslot(uint16_t genus_idx) {
	const uint16_t start = genus_table[genus_idx].start;
	const uint16_t end = genus_table[genus_idx].limit;
	uint16_t i;

	for (i = start; i < end; i++) {
		if (!objects[i].ship_idx) {
			objects[i].self_idx = 0;
			objects[i].damage_state = 0;
			break;
		}
	}
	if (i >= end)
		return 0xFFFF;
	return i;
}

// FUNCTION: TIE95 0x19D44
uint16_t create_findstaticslot(void) {
	uint16_t i;

	for (i = 0; i < NUM_STATIC_OBJECTS; i++)
		if (!staticobjects[i].species)
			break;
	if (i >= NUM_STATIC_OBJECTS)
		return 0xFFFF;
	return i;
}

/* Cycle through the 8 reusable debris slots (DEBRIS_FIRST_SLOT..NUM_OBJECTS-1).
 * Retail: 112..119; demo was 108..115. When the current slot has drifted
 * more than 0x800 fixed-point units from the player, respawn it with a
 * random spark/dust species at a random offset in the player's forward-
 * below-side frame. Called once per frame. */
// FUNCTION: TIE95 0x19D74
void create_checkdebris(void) {
	const uint16_t slot = currentdebrisslot++;
	FlightObject* pl;
	int32_t dx;
	int32_t dy;
	int32_t dz;
	int16_t r;
	int16_t ox;
	int16_t oy;
	int16_t oz;

	if (currentdebrisslot == NUM_OBJECTS)
		currentdebrisslot = DEBRIS_FIRST_SLOT;

	pl = pstate.player;
	dx = objects[slot].world_x - pl->world_x;
	dy = objects[slot].world_y - pl->world_y;
	dz = objects[slot].world_z - pl->world_z;
	if (dx < 0)
		dx = -dx;
	if (dy < 0)
		dy = -dy;
	if (dz < 0)
		dz = -dz;
	if (collide_roughdistance3du(dx, dy, dz) <= 0x800)
		return;

	objects[slot].ship_idx = (uint8_t)((math2_getrandom() & 3) + 110);
	objects[slot].genus = GENUS_DEBRIS;
	objects[slot].category = 3;
	objects[slot].fg_idx = (uint8_t)-1;

	if (pstate.player->orient_dirty) {
		fview_calcrotatemove(pstate.player->pitch, pstate.player->heading, pstate.player);
		fview_calcrotateorient(pstate.player->roll, 0, pstate.player);
	}

	/* Scatter perpendicular to fwd: side*r1 + up*r2, with each random
	 * scalar in [-512, 511] applied as a Q15 multiplier. */
	r = (int16_t)((math2_getrandom() & 0x03FF) - 512);
	ox = (int16_t)((pstate.player->side_x * r) >> 15);
	oy = (int16_t)((pstate.player->side_y * r) >> 15);
	oz = (int16_t)((pstate.player->side_z * r) >> 15);
	r = (int16_t)((math2_getrandom() & 0x03FF) - 512);
	ox += (int16_t)((pstate.player->up_x * r) >> 15);
	oy += (int16_t)((pstate.player->up_y * r) >> 15);
	oz += (int16_t)((pstate.player->up_z * r) >> 15);

	/* Forward displacement: 1/16 of the forward vector. */
	ox += pstate.player->fwd_x >> 4;
	oy += pstate.player->fwd_y >> 4;
	oz += pstate.player->fwd_z >> 4;

	objects[slot].world_x = pstate.player->world_x + ox;
	objects[slot].world_y = pstate.player->world_y + oy;
	objects[slot].world_z = pstate.player->world_z + oz;
	objects[slot].anim_frame = 2;
}

/* staticobjects[] is tie.c-owned per watdbg; declared in tie.h. */

/* watdbg owner msg.c; kept here for clarity */

/* trig2 working globals (angular conversions write these). */

/* ============================================================== */
/*   Leaf functions                                                */
/* ============================================================== */

// FUNCTION: TIE95 0x19FA0
uint16_t create_maxrandom(uint16_t max) {
	uint16_t value;
	uint16_t quotient;

	if (!max)
		return 0;
	value = math2_getrandom();
	quotient = value / max;
	return value - quotient * max;
}

/* ============================================================== */
/*   Drop-position resolver                                        */
/* ============================================================== */

// FUNCTION: TIE95 0x19FD0
int create_getdropposition(uint16_t fg_idx, uint16_t craft_index, uint16_t anchor_obj) {
	uint16_t species_idx;
	uint16_t spec_num;
	int16_t z_drop;

	species_idx = speciesconvert[(int8_t)fg_array[fg_idx].species];
	spec_num = species_table[species_idx].spec_num;
	if (TIE_FLIGHT_TIE98) {
		z_drop = (int16_t)modelbounds_getmaxz(species_idx);
	} else {
		draw_Lockshipfileptrs(species_idx);
		z_drop = (int16_t)((uint32_t)objectblockptr->speed_default >> 16);
		z_drop >>= 1;
	}
	create_getworldposition(OBJ_REF_WAYPOINT_BASE, fg_idx);

	if (species_table[species_idx].side & 0x80) {
		if (species_table[species_idx].ship_class == 9) {
			/* Planet: waypoint[0] with negated Y and << 8 scaling. */
			worldlocx = fg_array[fg_idx].way_x[0];
			worldlocx <<= 8;
			worldlocy = fg_array[fg_idx].way_y[0];
			worldlocy = -worldlocy;
			worldlocy <<= 8;
			worldlocz = fg_array[fg_idx].way_z[0];
			worldlocz <<= 8;
		} else if (species_table[species_idx].ship_class == 8) {
			/* Mine grid: walk to the requested index within count*count. */
			int16_t row_dx = 0;
			int16_t col_dx = 0;
			int16_t row_dy = 0;
			int16_t col_dy = 0;
			int16_t row_dz = 0;
			int16_t col_dz = 0;
			int16_t side_m1;
			int16_t x_base, y_base, z_base;
			uint16_t seq;
			int16_t row, col;

			switch (fg_array[fg_idx].version & 3) {
				case 0:
					col_dx = 64;
					row_dy = 64;
					break;
				case 1:
					row_dy = 64;
					col_dz = 64;
					break;
				case 2:
					col_dx = 64;
					row_dz = 64;
					break;
			}

			side_m1 = (int8_t)fg_array[fg_idx].count - 1;
			x_base = fg_array[fg_idx].way_x[0];
			y_base = fg_array[fg_idx].way_y[0];
			y_base = -y_base;
			z_base = fg_array[fg_idx].way_z[0];
			x_base -= side_m1 * col_dx / 2;
			x_base -= side_m1 * row_dx / 2;
			y_base -= side_m1 * col_dy / 2;
			y_base -= side_m1 * row_dy / 2;
			z_base -= side_m1 * col_dz / 2;
			z_base -= side_m1 * row_dz / 2;

			seq = 0;
			for (row = 0; row < (int8_t)fg_array[fg_idx].count; row++) {
				for (col = 0; col < (int8_t)fg_array[fg_idx].count; col++) {
					if (seq == craft_index) {
						worldlocx = x_base + col_dx * col + row_dx * row;
						worldlocx <<= 8;
						worldlocy = col_dy * col + y_base + row_dy * row;
						worldlocy <<= 8;
						worldlocz = col_dz * col + z_base + row * row_dz;
						worldlocz <<= 8;
						worldlocz += z_drop;
						return z_drop;
					}
					seq++;
				}
			}
		}
	} else {
		if (craft_index) {
			/* Non-static craft at a formation offset. */
			int16_t width, height, depth;
			int16_t ox, oy, oz;
			int16_t sep;
			uint16_t formation;
			int16_t shift;

			ox = width = spec_data[spec_num].bound_width;
			oz = height = spec_data[spec_num].bound_height;
			oy = depth = spec_data[spec_num].bound_depth;
			formation = (int8_t)fg_array[fg_idx].formation;
			sep = (int8_t)fg_array[fg_idx].form_spacing + 1;
			ox *= _formposx[(int8_t)fg_array[fg_idx].formation][craft_index] * sep;
			oy *= _formposy[(int8_t)fg_array[fg_idx].formation][craft_index] * sep;
			oz *= _formposz[(int8_t)fg_array[fg_idx].formation][craft_index] * sep;
			if (sep == 1) {
				ox += width / 2 * _formposx[formation][craft_index];
				oz += height / 2 * _formposz[formation][craft_index];
				oy += depth / 4 * _formposy[formation][craft_index];
			}
			if (anchor_obj == 0xFFFF) {
				create_getworldposition(OBJ_REF_WAYPOINT_BASE, fg_idx);
				rotatedx = 0;
				rotatedy = 0;
				rotatedz = 0;
			} else {
				pai_calcrotatedpoint(&objects[anchor_obj], ox, oz, oy);
			}
			shift = spec_data[spec_num].model_scale_shift;
			if (shift) {
				/* Binary emits `shl reg, cl` (sign-agnostic); shifting a
				 * negative int32_t in C is UB, so route through uint32_t. */
				rotatedx = (int32_t)((uint32_t)rotatedx << shift);
				rotatedy = (int32_t)((uint32_t)rotatedy << shift);
				rotatedz = (int32_t)((uint32_t)rotatedz << shift);
			}
		} else {
			/* craft_index == 0: use waypoint 0 as-is. */
			create_getworldposition(OBJ_REF_WAYPOINT_BASE, fg_idx);
			rotatedx = 0;
			rotatedy = 0;
			rotatedz = 0;
		}
		worldlocx += rotatedx;
		worldlocy += rotatedy;
		worldlocz += rotatedz;
	}

	worldlocz += z_drop;
	return z_drop;
}
