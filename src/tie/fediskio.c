#include "tie/fediskio.h"
#include "tie/fmusic.h"
#include "tie/fscript.h"
#include "tie/rtsvga2.h" /* rtsvga2_remapRGBImage */
#include "tie/spec.h"    /* spec_name_ptrs[] */
#include "tie/tie.h"     /* flightResolution */
#include "tie_runtime/audio/config.h"
#include "tie_runtime/audio/music_policy.h"
#include "tie_runtime/diagnostics/diagnostics.h"
#include "tie_runtime/display/classic_display.h"
#include "tie_runtime/display/classic_framebuffer.h"
#include "tie_runtime/flight_assets/model_types.h"
#include "tie_runtime/flight_assets/service.h"
#include "tie_runtime/input/input.h"
#include "tie_runtime/runtime/exports.h"
#include "tie_runtime/runtime/profile.h"
#include "tie_runtime/storage/storage.h"

#include "tie/feinput.h"
#include "tie/festring.h"
#include "tie/flight_surface_tie98.h"
#include "tie/frontend_display_tie98.h"
#include "tie/mission.h"
#include "tie/modelbounds.h"
#include "tie/modelmesh.h"
#include "tie/render_scene_tie98.h"
#include "tie/render_texture_tie98.h"
#include "tie/replay.h"
#include "tie/shell.h"
#include "tie_runtime/runtime/profile.h"

#include <stdio.h> /* snprintf */
#include <stdlib.h>
#include <string.h>

#include "tie/damage.h" /* systemstrings owner */
#include "tie/gate.h"
#include "tie/logbuf2.h"
#include "tie/msg.h"
#include "tie/panel.h"
#include "tie/panelrts.h" /* buoystr / unknownstring / statusstrings / warheadstrings */
#include "tie_runtime/runtime/inflight_state.h"
#include "tie_runtime/snapshot/snapshot.h" /* TieTextSnapshot_StringCell */

/* --- Unimplemented module functions (forward declarations) --- */

#include "tie/fsfx.h"    /* fsfx_loadsfx / fsfx_freesfx */
#include "tie/goals.h"   /* condstrings / goal*strings string-table globals */
#include "tie/help.h"    /* helpkeystrings / helpscreenstrings */
#include "tie/maproom.h" /* NHIstatusstrings / hostilestr / imperialstr ... */
#include "tie/option.h"  /* optionstrings / settingstrings */
#include "tie/overlay.h" /* initData[] (iMUSE init descriptor) */
#include "tie/trace2.h"  /* TRACE2_{EDGEINFO,EDGEHEADER}_CAP + record sizes */
#include "tie/wingman.h" /* wingmanstrings */
#include "util/binio.h"

/* --- Static data --- */

static const char* fatal_error_strings[] = { "Error! Not Enough Memory!\n",
											 "Error! The following file is missing or inaccessible: " };

/* --- Globals --- */

// GLOBAL: TIE 0xD4068
char pilotname[16];
// GLOBAL: TIE 0xD4078, TIE98 0x6267E0
char openfilename[256];
static TieFileRoot openfileroot;
// GLOBAL: TIE 0xD4178, TIE98 0x6267C8
TieFile* fileptr;
uint8_t currentmission;
uint8_t currentbattle;

char resourcedir[10];
char** fatalerrstrings;
// GLOBAL: TIE 0xC1E08, TIE98 0x4E00FC
static char** flightloadstrings;

/* Per-species model allocation size used by the classic renderer's
 * internal bounds checks. */
uint32_t species_model_handle_sizes[NUM_SPECIES];

uint32_t rankscores[5] = { 20000, 50000, 100000, 250000, 500000 };

uint32_t secretscores[12] = { 20000,   50000,   100000,  250000,  400000,  800000,
							  1000000, 1200000, 1400000, 1600000, 1800000, 2000000 };

uint8_t secretcompletioncnts[12] = { 2, 4, 6, 9, 12, 15, 18, 20, 22, 24, 26, 28 };

uint8_t battlemask[8] = { 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80 };

/* Species LFD filenames (3 files, 9 chars each, without .lfd extension) */
char specieslfds[3][9] = { "SPECIES", "SPECIES2", "SPECIES3" };

/* Weapon system type classification (maps weapon ID to 1=laser, 2=missile, 0=none).
 * IDs 1-6,9-11 = laser (1), IDs 7-8,12-18 = missile/warhead (2). */
uint8_t weaponsystype[33] = { 0, 1, 1, 1, 1, 1, 1, 2, 2, 1, 1, 1, 2, 2, 2, 2, 2,
							  2, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

// GLOBAL: TIE98 0x4E01A0
static const uint8_t tie98_hardpoint_weapon_class[40] = { 0, 1, 1, 1, 1, 1, 1, 2, 2, 1, 1, 1, 2, 2,
														  2, 2, 2, 2, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0,
														  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

/* Module-owned asset buffers. */
static void* stringdata_buf;
static void* font1_buf;
static void* font2_buf;
static void* log1_buf;
static void* log2_buf;
static void* panelparts_buf;
void* maproomicons_buf; /* exposed: maproom_maproom uses this as the icon-ptr table + shape data */
static void* rundiff_buf;
static void* replaybuffer_buf;
static void* messagelog_buf;
static void* music_handle_buf;

// GLOBAL: TIE98 0x50F858
static uint8_t tie98_flight_inverse_palette[0x10000];
/* TRACE2 edge pools. BPFLIGHT may allocate them before FEDISKIO initializes.
 * Allocate by record count because EdgeHeader grows with host pointer size. */
void* flightbuf_small; /* retail word_D4188, TRACE2_EDGEINFO_CAP records */
void* flightbuf_big;   /* retail word_D4186, TRACE2_EDGEHEADER_CAP records */

/* --- File I/O wrappers --- */

// FUNCTION: TIE 0x226D0, TIE98 0x41C5F0
int8_t fediskio_displayerror(void) {
	int16_t saved_cursor_x = cursorx;
	int16_t saved_cursor_y = cursory;
	int16_t saved_left = leftmargin;
	int16_t saved_top = topmargin;
	int16_t saved_right = rightmargin;
	int16_t saved_bottom = bottommargin;
	int16_t saved_line_wrap = lwrapflag;
	int16_t saved_reserved = flight_text_reserved_flag;
	int16_t saved_autofill = autofillflag;
	uint8_t saved_text_color = textcolor;
	uint8_t saved_back_color = backcolor;
	uint8_t saved_drop_color = dropcolor;
	uint8_t saved_drop_flag = dropflag;
	uint8_t saved_font = fontflag;
	uint8_t* saved_box;
	int8_t response;
	const int tie98 = TieClassicDisplay_OutputKind() == TIE_CLASSIC_OUTPUT_DX5_SURFACE;

	if (tie98) {
		FlightSurface_Lock();
		g_flightDrawToOffscreenSurface = 0;
	}
	colorcycleuserflag = 1;
	festring_setfontsize(1);
	if (tie98) {
		saved_box = (uint8_t*)newbuf + (size_t)g_surfacePitch * (screenYRes - 4 * fontheight - 1);
	} else {
		saved_box = (uint8_t*)newbuf + (size_t)bytesPerPixel * screenXRes * (screenYRes - 4 * fontheight - 1);
	}
	if (tie98)
		rtsvga2_saveboxVGA_tie98(saved_box, 0, (uint16_t)((screenYRes >> 1) - 2 * fontheight),
								 (uint16_t)screenXRes, (uint16_t)(4 * fontheight + 1));
	else
		rtsvga2_saveboxVGA(saved_box, 0, (uint16_t)((screenYRes >> 1) - 2 * fontheight), (uint16_t)screenXRes,
						   (uint16_t)(4 * fontheight + 1));
	festring_setbound((int16_t)(screenXRes >> 4), (int16_t)((screenYRes >> 1) - 2 * fontheight),
					  (int16_t)(screenXRes - (screenXRes >> 4)),
					  (int16_t)((screenYRes >> 1) + 2 * fontheight));
	backcolor = 0xF9;
	clearwindow();
	festring_setbound((int16_t)((screenXRes >> 4) + 1), (int16_t)((screenYRes >> 1) - 2 * fontheight + 1),
					  (int16_t)(screenXRes - (screenXRes >> 4) - 1),
					  (int16_t)((screenYRes >> 1) + 2 * fontheight - 1));
	backcolor = 0;
	clearwindow();
	if (tie98) {
		textcolor = 0xF9;
		dropcolor = 0;
		dropflag = 0;
	} else {
		dropcolor = 0;
		dropflag = 0;
		textcolor = 0xF9;
	}
	festring_setcursor(0, (int16_t)((screenYRes >> 1) - fontheight - 2));
	festring_outstringcenter((const uint8_t*)flightloadstrings[5]);
	festring_setcursor(0, (int16_t)((screenYRes >> 1) + 2));
	festring_outstringcenter((const uint8_t*)flightloadstrings[6]);
	if (tie98) {
		FlightSurface_Unlock();
		g_flightDrawToOffscreenSurface = 1;
		FrontendDisplay_PresentFrame();
		response = FlightInput_GetChar();
		FrontendDisplay_PresentFrame();
	} else {
		response = (int8_t)TieInput_ReadKey();
	}
	colorcycleuserflag = 0;
	if (tie98)
		rtsvga2_restoreboxVGA_tie98(saved_box, 0, (uint16_t)((screenYRes >> 1) - 2 * fontheight),
									(uint16_t)screenXRes, (uint16_t)(4 * fontheight + 1));
	else
		rtsvga2_restoreboxVGA(saved_box, 0, (uint16_t)((screenYRes >> 1) - 2 * fontheight),
							  (uint16_t)screenXRes, (uint16_t)(4 * fontheight + 1));
	if (tie98) {
		memset(newbuf, 0x40, (size_t)screenXRes * screenYRes * g_flight16bppBytesPerPixel);
		festring_setfontsize(saved_font);
		cursorx = saved_cursor_x;
		leftmargin = saved_left;
		cursory = saved_cursor_y;
		topmargin = saved_top;
		bottommargin = saved_bottom;
		rightmargin = saved_right;
		lwrapflag = saved_line_wrap;
		autofillflag = saved_autofill;
		flight_text_reserved_flag = saved_reserved;
		textcolor = saved_text_color;
		dropcolor = saved_drop_color;
		backcolor = saved_back_color;
		dropflag = saved_drop_flag;
	} else {
		festring_setfontsize(saved_font);
		cursorx = saved_cursor_x;
		cursory = saved_cursor_y;
		leftmargin = saved_left;
		topmargin = saved_top;
		rightmargin = saved_right;
		bottommargin = saved_bottom;
		lwrapflag = saved_line_wrap;
		flight_text_reserved_flag = saved_reserved;
		autofillflag = saved_autofill;
		textcolor = saved_text_color;
		backcolor = saved_back_color;
		dropcolor = saved_drop_color;
		dropflag = saved_drop_flag;
	}
	return response;
}

// FUNCTION: TIE 0x229CC, TIE98 0x41C910
int16_t fediskio_tryopenfile(TieFileRoot root, const char* name, const char* mode, int16_t fatal) {
	int16_t attempt_count = TieProfile_UsesTie98Logic() ? 2 : 4;

	strcpy(openfilename, name);
	openfileroot = root;
	/* MODERN ADAPTATION: the VFS root replaces TIE98's final
	 * install-drive pathname attempt. Removable-media retries are obsolete. */
	for (int16_t attempt = 0; attempt < attempt_count; ++attempt) {
		fileptr = TieStorage_Open(root, name, mode);
		if (fileptr)
			return 1;
	}
	if (fatal)
		fediskio_fatalerror(FATAL_ERROR_THE_FOLLOWING_FILE_IS_MISSING_);
	return 0;
}
// FUNCTION: TIE 0x22BE4
int16_t fediskio_tryclosefile(int16_t delete_on_error) {
	int16_t had_error = 0;

	/* ferror dropped — the host's fclose return code surfaces both
	 * pending write errors and the close itself. */
	if (TieStorage_Close(fileptr) != 0)
		had_error = 1;
	fileptr = NULL;

	if (delete_on_error && had_error)
		TieStorage_Remove(openfileroot, openfilename);

	return had_error;
}

// FUNCTION: TIE 0x22C24
int16_t fediskio_readfileblock(void* buf, unsigned int size, unsigned int count, TieFile* fp) {
	int16_t result = (int16_t)TieStorage_Read(buf, size, count, fp);
	if ((unsigned int)result == count) {
		fileerror = 0;
	} else {
		fileerror = 1;
		return 0;
	}
	return result;
}

// FUNCTION: TIE 0x22D38
int16_t fediskio_writefileblock(void* buf, unsigned int size, int count, TieFile* fp) {
	int16_t result = (int16_t)TieStorage_Write(buf, size, count, fp);
	if (result == count) {
		fileerror = 0;
	} else {
		fileerror = 1;
		return 0;
	}
	return result;
}

// FUNCTION: TIE 0x22D60
void fediskio_fatalerror(FatalErrId error_code) {
	char str[128];
	int i;

	for (i = 0; i < 128; i++) {
		str[i] = fatal_error_strings[error_code][i];
		if (!str[i])
			break;
	}

	if (error_code == 1) {
		int j = 0;
		while (i < 128) {
			str[i] = openfilename[j++];
			if (!str[i])
				break;
			i++;
		}
		str[i] = '\n';
		str[(uint16_t)(i + 1)] = '\0';
	}

	shell_programexit(str);
}

/* --- Pilot record I/O --- */

// FUNCTION: TIE 0x204A0
void fediskio_initpilotrecord(int16_t clear_name) {
	uint8_t* raw;

	if (clear_name)
		pilotname[0] = '\0';

	/* Initialize both disk slots (primary + backup) of the pilot record
	 * in loadbuffer with version=1, game_level=1, everything else 0. The
	 * version/game_level bytes happen to be at the same offsets (+0x000
	 * and +0x003) in both the in-memory struct and the disk format, but
	 * the rest of the layout differs -- always touch loadbuffer as raw
	 * bytes, never cast it as PilotRecord *. */
	raw = (uint8_t*)loadbuffer;
	memset(raw, 0, 2u * PILOTRECORD_DISK_SIZE);
	raw[0x000] = 1;                         /* version (primary) */
	raw[0x003] = 1;                         /* game_level (primary) */
	raw[PILOTRECORD_DISK_SIZE + 0x000] = 1; /* version (backup) */
	raw[PILOTRECORD_DISK_SIZE + 0x003] = 1; /* game_level (backup) */
}

/* loadbuffer holds the raw disk-format pilot record (two PILOTRECORD_DISK_SIZE
 * slots back-to-back: primary at +0, backup at +PILOTRECORD_DISK_SIZE). The
 * in-memory PilotRecord struct is naturally aligned and 1936 bytes, which
 * differs from the 1928-byte on-disk record -- never raw-cast loadbuffer as
 * `PilotRecord *`. Use PilotRecord_decode / _encode at every access. */

// FUNCTION: TIE 0x205B8
int16_t fediskio_readpilotrecord(const char* name) {
	if (!fediskio_tryopenfile(TIE_FILE_ROOT_USER, name, "rb", 0))
		return 0;

	fediskio_readfileblock(loadbuffer, PILOTRECORD_DISK_SIZE, 2, fileptr);
	fediskio_tryclosefile(0);
	return 1;
}

// FUNCTION: TIE 0x205F8
int16_t fediskio_writepilotrecord(const char* name) {
	if (!fediskio_tryopenfile(TIE_FILE_ROOT_USER, name, "wb", 0))
		return 0;

	fediskio_writefileblock(loadbuffer, PILOTRECORD_DISK_SIZE, 2, fileptr);
	fediskio_tryclosefile(0);
	return 1;
}

// FUNCTION: TIE 0x204DC
void fediskio_createpilotrecord(void) {
	PilotRecord pilot;
	int i;

	if (!fediskio_readpilotrecord(pilotname)) {
		uint8_t* raw = (uint8_t*)loadbuffer;
		memset(raw, 0, 2u * PILOTRECORD_DISK_SIZE);
		raw[0x000] = 1;                         /* version (primary) */
		raw[0x003] = 1;                         /* game_level (primary) */
		raw[PILOTRECORD_DISK_SIZE + 0x000] = 1; /* version (backup) */
		raw[PILOTRECORD_DISK_SIZE + 0x003] = 1; /* game_level (backup) */
		if (pilotname[0])
			fediskio_writepilotrecord(pilotname);
	}

	PilotRecord_decode(&pilot, (const uint8_t*)loadbuffer);
	mission.difficulty = pilot.game_level;

	if (mission.mission_mode == 4) {
		memcpy(mission.mission_linked_data, pilot.linked_data, 256);
		currentbattle = pilot.cur_battle;
		currentmission = pilot.battle_cursor[currentbattle];
		/* Snapshot tour cursor for fsfx_loadvoicelfd. */
		voice_tour_battle = pilot.cur_battle;
		voice_tour_mission = pilot.battle_cursor[voice_tour_battle];
	} else {
		for (i = 0; i < 256; i++)
			mission.mission_linked_data[i] = 0;
		/* Snapshot combat-sim ship/course for fsfx_loadvoicelfd. In
		 * mode 5 (combat-of-tour) cur_combat_ship is encoded as
		 * 12 + battle, which the loader subtracts back out to reach
		 * the battle digit. The course cursor is read at disk offset
		 * 0x67+voice_id_a -- for voice_id_a < 12 this lines up with
		 * combat_course_cursor[voice_id_a]; for voice_id_a >= 12 it
		 * sits in the reserved-73 hole the retail binary repurposes
		 * for tour-of-combat course tracking. Read via raw disk-byte
		 * offset to capture both ranges uniformly. */
		const uint8_t* raw = (const uint8_t*)loadbuffer;
		voice_id_a = pilot.cur_combat_ship;
		voice_id_b = raw[0x67 + voice_id_a];
	}
}

/* Map CraftType → ship index (0-6) for training score tracking */
static uint16_t train_craft_type_to_ship_idx(uint8_t train_craft_type) {
	switch (train_craft_type) {
		case 5:
			return 0; /* TIE Fighter */
		case 6:
			return 1; /* TIE Interceptor */
		case 7:
			return 2; /* TIE Bomber */
		case 8:
			return 3; /* TIE Advanced */
		case 16:
			return 4; /* Assault Gunboat */
		case 9:
			return 5; /* TIE Defender */
		default:
			return 6;
	}
}

// FUNCTION: TIE 0x20668
int16_t fediskio_updatepilotrecord(int16_t exit_status, int16_t ejected) {
	PilotRecord pilot;
	PilotRecord* p = &pilot;
	CraftData* craft;
	int score, total_score, score_quarter;
	uint16_t ship_idx, i;

	if (replayviewmode || !maingameflag)
		return 0;
	if (!pilotname[0])
		return 0;

	craft = pstate.player_craft;
	if (!fediskio_readpilotrecord(pilotname))
		return 0;

	/* Decode the disk-format primary slot in loadbuffer into the local
	 * naturally-aligned PilotRecord, mutate, then encode back to both
	 * slots before writing. Never raw-cast loadbuffer as `PilotRecord *`
	 * -- the in-memory struct's natural-alignment padding shifts every
	 * field after secret_score by 4 bytes (cur_battle in-mem +0x26C vs
	 * disk +0x268, battle_cursor[0] in-mem +0x281 vs disk +0x27D). */
	PilotRecord_decode(p, (const uint8_t*)loadbuffer);

	/* --- Training mode (train_craft_type nonzero = training mission) --- */
	if (mission.train_craft_type) {
		ship_idx = train_craft_type_to_ship_idx(mission.train_craft_type_src);

		if ((uint32_t)mission.mission_score > (uint32_t)p->train_score[ship_idx])
			p->train_score[ship_idx] = mission.mission_score;

		uint8_t prev_level = mission.train_level - 1;
		if (prev_level > p->train_max_level[ship_idx])
			p->train_max_level[ship_idx] = prev_level;

		if (mission.train_level >= 9 && !p->rank) {
			p->rank = 1;
			mission.mission_new_rank = p->rank;
		}
		goto write_and_exit;
	}

	/* --- Combat/battle mode: score calculation --- */

	score = 50 * craft->total_kills;

	for (i = 0; i < NUM_SPEC; i++) {
		uint8_t kv = spec_data[i].kill_value;
		score += 200 * mission.captures_by_type[i] * kv + 40 * kv * craft->kills_by_species[i];
	}

	score_quarter = score >> 2;
	if (mission.difficulty == 0)
		score -= score_quarter;
	else if (mission.difficulty == 2)
		score += score_quarter;

	if (inflight_collision == 1)
		score += score >> 3;

	total_score = score + 3 * craft->laser_hit - craft->laser_fired + 100 * craft->warhead_hit -
				  50 * craft->warhead_fired;

	if (ejected)
		total_score -= 5000;
	if (pstate.friendly_kill_count)
		total_score -= 10000;
	if (mission.penalty_flag)
		total_score -= 5000;

	if (mission.primary_complete == 1) {
		total_score += 2500 * (mission.difficulty + 1);
		if (inflight_collision == 1)
			total_score += 250;
	}

	if (mission.secondary_complete == 1) {
		total_score += 2500 * (mission.difficulty + 1);
		if (inflight_collision == 1)
			total_score += 250;
	}

	/* Per-flight-group bonus (50 × bonus_points for completed FGs) */
	for (i = 0; i < (uint16_t)mission_file_header.num_fg; i++) {
		if (fgstatus[i].fg_complete == 1)
			total_score += (int16_t)(50 * fg_array[i].bonus_points);
	}

	if (mission.bonus_complete == 1) {
		total_score += 1000 * (mission.difficulty + 1);
		if (inflight_collision == 1)
			total_score += 100;
	}

	if (total_score < 0)
		total_score = 0;
	if (cheatingflag)
		total_score /= 10;

	/* --- Battle mode (mode 4): accumulate career stats --- */
	if (mission.mission_mode == 4) {
		p->exit_status = (uint8_t)exit_status;

		p->laser_total += craft->laser_fired;
		p->laser_hits += craft->laser_hit;
		p->warhead_total += craft->warhead_fired;
		p->warhead_hits += craft->warhead_hit;

		p->total_kills += craft->total_kills;
		for (i = 0; i < NUM_SPEC; i++) {
			p->kills_by_ship_type[i] += craft->kills_by_species[i];
			p->total_kills += craft->kills_by_species[i];
			p->captures_by_ship_type[i] += mission.captures_by_type[i];
			p->total_captures += mission.captures_by_type[i];
		}

		if (ejected)
			p->ejection_count++;

		p->score += total_score;
		uint32_t avg = ((uint32_t)p->score) / 4;
		if (avg > 0xFFFF)
			avg = 0xFFFF;
		if ((uint16_t)avg > p->avg_score)
			p->avg_score = (uint16_t)avg;

		if (p->rank < 5 && mission.primary_complete == 1 && (uint32_t)p->score > rankscores[p->rank]) {
			p->rank++;
			mission.mission_new_rank = p->rank;
		}
	}

	mission.mission_score = total_score;

	/* --- Combat sim mode (mode 1): per-ship/course high scores --- */
	if (mission.mission_mode == 1) {
		uint8_t ship = p->cur_combat_ship;
		uint8_t course = p->combat_course_cursor[ship];

		if ((uint32_t)total_score > (uint32_t)p->combat_score[ship][course])
			p->combat_score[ship][course] = total_score;

		if (mission.primary_complete == 1 && !p->combat_complete[ship][course]) {
			p->combat_complete[ship][course] = 1;

			uint16_t won = 0;
			for (uint16_t s = 0; s < NUM_SHIPS; s++)
				for (uint16_t c = 0; c < 8; c++)
					if (p->combat_complete[s][c])
						won++;

			if (won >= 8 && !p->rank) {
				p->rank = 1;
				mission.mission_new_rank = 1;
			}
		}
	}
	/* --- Battle mode (mode 4): battle progression --- */
	else if (mission.mission_mode == 4) {
		uint8_t battle = p->cur_battle;
		uint8_t cur_mis = p->battle_cursor[battle];

		if ((uint32_t)total_score > (uint32_t)p->tour_score[battle][cur_mis])
			p->tour_score[battle][cur_mis] = total_score;

		/* player_status: 0 = dead, 1 = captured/failed */
		if (mission.player_status == 0 || mission.player_status == 1) {
			for (i = 0; i < NUM_BATTLES; i++) {
				if (p->battle_status[i] == 1)
					p->battle_status[i] = 2;
			}
		} else {
			/* Player survived: check for mission completion.
			 * Primary complete, OR secondary complete when mis_var[2]==3
			 * (special mission type that allows secondary-only progression). */
			uint8_t mis_var2 = mission_file_header.mission.win_type;
			if (mission.primary_complete == 1 || (mission.secondary_complete == 1 && mis_var2 == 3)) {
				p->battle_cursor[battle]++;

				for (i = 0; i < 256; i++)
					p->linked_data[i] = mission.mission_linked_data[i];

				if (mission.secondary_complete == 1 && mis_var2 != 2) {
					p->secret_complete_bits[battle] |= battlemask[cur_mis];

					p->secret_completions++;
					uint32_t sec_total = total_score + p->secret_score;
					p->secret_score = sec_total;

					uint8_t sec_rank = p->secret_order_rank;
					if (sec_rank < 9 && sec_total >= secretscores[sec_rank] &&
						p->secret_completions >= secretcompletioncnts[sec_rank]) {
						p->secret_order_rank++;
						mission.mission_secret_medal = p->secret_order_rank;
					}
				}

				if (mission.bonus_complete == 1)
					p->mission_bonus_bits[battle] |= battlemask[cur_mis];
			}
		}
	}

write_and_exit:
	/* Preserve the pre-mission backup slot for automatic pilot restore. */
	PilotRecord_encode((uint8_t*)loadbuffer, p);

	if (!fediskio_tryopenfile(TIE_FILE_ROOT_USER, pilotname, "wb", 1))
		return 0;

	fediskio_writefileblock(loadbuffer, PILOTRECORD_DISK_SIZE, 2, fileptr);
	fediskio_tryclosefile(0);
	return 0;
}

/* --- Buffer loading --- */

// FUNCTION: TIE 0x20E48
int fediskio_readfiletofarmemory(TieFileRoot root, const char* filename, void* dest) {
	uint8_t buf[512];
	int total = 0;

	fediskio_tryopenfile(root, filename, "rb", 1);

	if (fileptr) {
		uint16_t nread = 512;
		while (nread == 512) {
			nread = (uint16_t)TieStorage_Read(buf, 1, 512, fileptr);
			memcpy((uint8_t*)dest + total, buf, nread);
			total += nread;
		}
	}

	fediskio_tryclosefile(0);
	return total;
}

// FUNCTION: TIE 0x20D8C
void fediskio_loadbufferdata(const char* filename, uint16_t buf_index, int16_t num_entries,
							 uint16_t skip_count) {
	int16_t line_idx = 0;

	fediskio_tryopenfile(TIE_FILE_ROOT_FLIGHT_ASSET, filename, "rb", 1);

	while (num_entries > 0) {
		farbufferptrs[buf_index] = farbufferptr;
		if (line_idx >= (int16_t)skip_count)
			buf_index++;

		int ch;
		while ((ch = TieStorage_Getc(fileptr)) != -1 && ch != 0xFF) {
			if (line_idx >= (int16_t)skip_count)
				*farbufferptr++ = (uint8_t)ch;
		}

		if (line_idx >= (int16_t)skip_count) {
			*farbufferptr++ = 0xFF;
			num_entries--;
		}
		line_idx++;
	}

	fediskio_tryclosefile(0);
}

/* --- Flight engine buffer management --- */

// FUNCTION: TIE 0x20ED4, TIE98 0x41AAC0
void fediskio_Init_Buffers_and_Fonts(void) {
	int fail = 0;
	char path[64];
	const int dx5_display = TieClassicDisplay_UsesDx5();
	const size_t screen_buffer_size =
		dx5_display ? (size_t)screenYRes * g_surfacePitch : (size_t)bytesPerPixel * screenYRes * screenXRes;

	/* Error formatting depends on stringdata; later allocation failures can
	 * be accumulated and reported after it loads. */
	stringdata_buf = malloc(16000);
	if (!stringdata_buf)
		fediskio_fatalerror(FATAL_ERROR_NOT_ENOUGH_MEMORY_X0A);

	font1_buf = malloc(34600);
	if (!font1_buf)
		fail = 1;

	font2_buf = malloc(20600);
	if (!font2_buf)
		fail = 1;

	log1_buf = malloc(screen_buffer_size);
	if (!log1_buf)
		fail = 1;

	log2_buf = malloc(screen_buffer_size);
	if (!log2_buf)
		fail = 1;

	/* EdgeHeader contains host pointers, so allocate by record count. */
	flightbuf_small = malloc(TRACE2_EDGEINFO_CAP * sizeof(trace2_EdgeInfo));
	if (!flightbuf_small)
		fail = 1;

	flightbuf_big = malloc(TRACE2_EDGEHEADER_CAP * sizeof(trace2_EdgeHeader));
	if (!flightbuf_big)
		fail = 1;

	panelparts_buf = malloc(0x1ADB0);
	if (!panelparts_buf)
		fail = 1;
	/* Host panel storage remains permanently addressable. */
	panelpartsptr = panelparts_buf;

	maproomicons_buf = malloc(31060);
	if (!maproomicons_buf)
		fail = 1;

	rundiff_buf = malloc(19452);
	if (!rundiff_buf)
		fail = 1;

	/* One complete fixed-size replay chunk. calloc keeps temporary-buffer
	 * snapshots deterministic when the final chunk is only partially used. */
	replaybuffer_buf = calloc(REPLAY_INPUT_CHUNK_FRAMES, REPLAYINPUTFRAME_DISK_SIZE);
	if (!replaybuffer_buf)
		fail = 1;

	messagelog_buf = malloc(32000);
	if (!messagelog_buf)
		fail = 1;

	if (musicenabled && TieMusicPolicy_UsesImuse()) {
		fmusic_allocmusicbuffer();
		music_handle_buf = malloc(0x8000);
		if (!music_handle_buf)
			fail = 1;
	} else {
		music_buffer = NULL;
	}

	fediskio_loadstringdata();

	if (fail)
		fediskio_fatalerror(FATAL_ERROR_NOT_ENOUGH_MEMORY_X0A);

	if (musicenabled && TieMusicPolicy_UsesImuse())
		music_buffer = music_handle_buf;

	fontptrtiny = font1_buf;
	fontptrmicro = font2_buf;

	newbuf = log1_buf;
	logbuf2_selectbuffer(newbuf);

	xtransdataptr = log2_buf;
	loadbuffer = log2_buf;

	replaybufferstart = replaybuffer_buf;
	if (TieProfile_UsesTie98Logic()) {
		memset(newbuf, 0x40, (size_t)screenXRes * screenYRes * g_flight16bppBytesPerPixel);
		fediskio_readfiletofarmemory(TIE_FILE_ROOT_FLIGHT_ASSET, "vga.pac", xtransdataptr);
		buildpalette(xtransdataptr, 64, 192);
		unblank();
	}

	if (tie_is_high_resolution_flight()) {
		fediskio_readfiletofarmemory(TIE_FILE_ROOT_FLIGHT_ASSET, "tiny64.fnt", fontptrtiny);
		fediskio_readfiletofarmemory(TIE_FILE_ROOT_FLIGHT_ASSET, "micro64.fnt", fontptrmicro);
	} else {
		fediskio_readfiletofarmemory(TIE_FILE_ROOT_FLIGHT_ASSET, "tiny.fnt", fontptrtiny);
		fediskio_readfiletofarmemory(TIE_FILE_ROOT_FLIGHT_ASSET, "micro.fnt", fontptrmicro);
	}

	festring_setfontsize(1);
	festring_setbound(0, 0, (int16_t)screenXRes, (int16_t)screenYRes);
	backcolor = 0;
	dropcolor = 0;
	dropflag = 0;
	textcolor = 0xFA;

	/* Loading-screen banner (retail 0x211c3..0x211d1): print STRINGS.DAT
	 * slot 12 — "Initializing Combat Sequence..." — centered, one line
	 * above the follow-up cursor position set after the SFX load below.
	 * Text color 0xFA is one of the three engine-glow DAC slots that
	 * gamesnd_drive_palette_cycle rewrites every few PIT ticks, which is
	 * what makes the banner pulse blue while the rest of the DAC is
	 * still blanked from rtsvga2_blankVGA. (Slots 13-16, "Charging
	 * Weapons Systems..." etc., are X-Wing-era leftovers retail never
	 * prints.) tie_simulator holds its INIT phase for a minimum display
	 * time so the banner survives modern near-instant asset loads. */
	{
		const char* banner = flightloadstrings[0];
		festring_setcursor(0, (int16_t)((screenYRes >> 1) - 5 * fontheight));
		if (banner)
			festring_outstringcenter((const uint8_t*)banner);
	}

	/* Reset both panel-loaded signals. */
	panels_in_ems = 0;
	panelsloadedflag = 0;

	if (voiceenabled | sfxenabled) {
		snprintf(path, sizeof(path), "%ssfxblast.lfd", resourcedir);
		fsfx_loadsfx(path);
	}

	festring_setcursor(0, (int16_t)((screenYRes >> 1) - 4 * fontheight));

	if (musicenabled && TieMusicPolicy_UsesImuse()) {
		snprintf(path, sizeof(path), "%sadlib.lfd", resourcedir);
		if (!fmusic_loadmusic(path))
			fediskio_fatalerror(FATAL_ERROR_NOT_ENOUGH_MEMORY_X0A);
		fscript_MsStartScript(&initData);
	}
}

// FUNCTION: TIE 0x212E4
void fediskio_UnlockGlobals(void) {
	/* In the binary: unlocks 7 XMEMHDL handles.
	 * With malloc, pointers remain valid — nothing to do. */
}

// FUNCTION: TIE 0x21348
void fediskio_RelockGlobals(void) {
	/* In the binary: re-locks handles into global pointers.
	 * With malloc, pointers are already valid. Just refresh string data
	 * and reassign the buffer pointers. */
	if (musicenabled && TieMusicPolicy_UsesImuse())
		music_buffer = music_handle_buf;

	fediskio_loadstringdata();

	fontptrtiny = font1_buf;
	fontptrmicro = font2_buf;
	/* Re-bind the active font to the freshly relocked handle. Without
	 * this, callers continue to read through the old curfontptr which
	 * may have been freed/recycled by the resource swap. */
	if (fontflag == 1)
		curfontptr = fontptrtiny;
	else if (fontflag == 2)
		curfontptr = fontptrmicro;
	newbuf = log1_buf;
	logbuf2_selectbuffer(newbuf);
	xtransdataptr = log2_buf;
	loadbuffer = log2_buf;
	replaybufferstart = replaybuffer_buf;
	/* panelpartsptr is bound once at fediskio_AllocateFlightHandles
	 * allocation time; retail relock doesn't touch it either. */
}

/* Resolved host-native pointer table built from strings.dat's 32-bit
 * offset table. Retail walked a void** cursor straight through the file
 * buffer; on LP64 that stride was wrong (sizeof(void*)=8 vs the file's
 * 4-byte entries) and pointer bits exceeding 32 got truncated by the
 * relocation pass. Build a parallel table of real host pointers so the
 * existing p + N / *(p - N) arithmetic works on 64-bit. Entries point INTO
 * stringdata_buf, so the table is invalidated when that buffer is freed. */
static char** sdata_resolved;
static size_t sdata_resolved_cap;
static size_t sdata_resolved_count; /* valid entries from the last load */

// FUNCTION: TIE 0x213F0
void fediskio_FreeFlightHandles(void) {
	uint16_t i;

	if (musicenabled && TieMusicPolicy_UsesImuse()) {
		free(music_handle_buf);
		music_handle_buf = NULL;
		fmusic_freemusic();
	}
	fsfx_freesfx();

	free(stringdata_buf);
	stringdata_buf = NULL;
	/* sdata_resolved[] holds pointers INTO stringdata_buf; freeing the buffer
	 * leaves them dangling. Invalidate the table so TieTextSnapshot_StringCell()
	 * returns NULL until the next fediskio_loadstringdata rebuilds it --
	 * otherwise a late HUD-text render (cockpit_text str_cell) reads freed
	 * memory on flight exit. */
	sdata_resolved_count = 0;
	free(font1_buf);
	font1_buf = NULL;
	free(font2_buf);
	font2_buf = NULL;
	free(log1_buf);
	log1_buf = NULL;
	free(log2_buf);
	log2_buf = NULL;
	free(flightbuf_small);
	flightbuf_small = NULL;
	free(flightbuf_big);
	flightbuf_big = NULL;
	free(panelparts_buf);
	panelparts_buf = NULL;
	panelpartsptr = NULL;
	panel_freeviewbufs();
	free(maproomicons_buf);
	maproomicons_buf = NULL;
	free(rundiff_buf);
	rundiff_buf = NULL;
	free(replaybuffer_buf);
	replaybuffer_buf = NULL;
	free(messagelog_buf);
	messagelog_buf = NULL;

	/* Free species model blobs. fediskio_loadspecies shares one malloc
	 * across every species[] entry that maps to the same lfd_file +
	 * lfd_entry (matching the binary's XMEMHDL_Free_Handle refcount
	 * dedup). To free each unique pointer exactly once, free the slot
	 * then null out every slot that aliased it. */
	for (i = 0; i < NUM_SPECIES; i++) {
		void* p = species_table[i].model_handle;
		if (!p)
			continue;
		free(p);
		uint16_t j;
		for (j = i; j < NUM_SPECIES; j++) {
			if (species_table[j].model_handle == p)
				species_table[j].model_handle = NULL;
		}
	}
}

/* String-table consumers: declarations live in their owning headers
 * (goals.h / help.h / maproom.h / option.h / wingman.h, included above;
 * tie.h provides viewfilmstr). Local-only references below need no
 * extra forward declarations here. */

/* Cell-indexed STRINGS.DAT accessors, exposed via snapshot.h. */
const char* TieTextSnapshot_StringCell(int cell) {
	if (cell < 0 || (size_t)cell >= sdata_resolved_count)
		return NULL;
	return sdata_resolved[cell];
}
int TieTextSnapshot_StringCount(void) { return (int)sdata_resolved_count; }

// FUNCTION: TIE 0x215B0
void fediskio_loadstringdata(void) {
	void** p;
	int i;

	fediskio_readfiletofarmemory(TIE_FILE_ROOT_FLIGHT_ASSET, "strings.dat", stringdata_buf);

	/* File layout: 32-bit file offsets (null-terminated), then string data.
	 * Count entries in the offset header without relocating in place. */
	const int32_t* offsets = (const int32_t*)stringdata_buf;
	size_t n_offsets = 0;
	while (offsets[n_offsets])
		++n_offsets;

	if (n_offsets > sdata_resolved_cap) {
		free(sdata_resolved);
		sdata_resolved = (char**)malloc(n_offsets * sizeof(char*));
		if (!sdata_resolved)
			fediskio_fatalerror(FATAL_ERROR_NOT_ENOUGH_MEMORY_X0A);
		sdata_resolved_cap = n_offsets;
	}
	for (size_t k = 0; k < n_offsets; ++k)
		sdata_resolved[k] = (char*)stringdata_buf + (uint32_t)offsets[k];
	sdata_resolved_count = n_offsets;

	/* `base + N` byte offsets in retail are `N/4` into the pointer table. */
	char** base_pp = sdata_resolved;

	systemstrings = base_pp;          /* base + 0    */
	fatalerrstrings = base_pp + 10;   /* base + 40   */
	flightloadstrings = base_pp + 12; /* base + 48   */
	condstrings = base_pp + 28;       /* base + 112  */
	condverbstrings = base_pp + 49;   /* base + 196  */

	p = (void**)(base_pp + 69); /* base + 276  */
	percentstrings = p;
	p += 16;                 /* p = base + 85  */
	goaloperatorstrings = p; /* base + 85  */
	p += 2;                  /* p = base + 87  */
	goaltitlestrings = p;    /* base + 87  */
	p += 9;                  /* p = base + 96  */

	gatelevelstr = *(p - 74);
	p++;
	gateremainstr = *(p - 74);
	p++;
	gatepassedstr = *(p - 74);
	p++;
	targetshitstr = *(p - 74);
	p++;
	scorestr = *(p - 74);
	p++;
	goalescapestr = *(p - 74);
	p++;

	goal_of_string = *(p - 6);
	p++;
	goal_ofall_string = *(p - 6);
	goalskillstrings = p;
	p += 6;
	goal_group_string = *(p - 11);
	goalaistrings = p;
	p += 30;
	goal_allbut_string = *(p - 40);
	goalsidestrings = p;
	p += 3;
	goal_and_string = *(p - 42);
	goalfamilystrings = p;
	p += 7;
	goal_comma_string = *(p - 48);
	goalgenusstrings = p;
	p += 16;
	goalallfgstring = *(p - 63);

	helpkeystrings = (char**)p;                   /* base + 660  */
	helpscreenstrings = (char**)(p + 48);         /* base + 852  */
	NHIstatusstrings = (const char**)(p + 96);    /* base + 1044 */
	maproomhelpstrings = (const char**)(p + 102); /* base + 1068 */
	messagetable = (char**)(p + 105);             /* base + 1080 */
	optionstrings = (char**)(p + 320);            /* base + 1940 */
	settingstrings = (char**)(p + 334);           /* base + 1996 */
	p += 350;                                     /* p now base + 2060 (index 515) */

	hostilestr = *(p - 251);
	p++; /* base + 1056 (index 264) */
	imperialstr = *(p - 251);
	p++; /* base + 1060 (index 265) */
	neutralstr = *(p - 251);
	p++; /* base + 1064 (index 266) */

	diststring = *(p - 4);
	p++;
	shieldstring = *(p - 4);
	p++;
	hullstring = *(p - 4);
	p++;
	sysstring = *(p - 4);
	p++;
	targetstring = *(p - 4);
	p++;
	nonestring = *(p - 4);
	p++;
	ourstring = *(p - 4);
	p++;
	currentorderstring = *(p - 4);
	p++;
	notargetstring = *(p - 4);
	p++;
	curtargetstring = *(p - 4);
	p++;
	curdeststring = *(p - 4);
	p++;

	distfromtargetstring = *(p - 4);
	waypointstrings = (char**)(p + 1);
	disttodeststring = *(p - 3);
	componentnames = p + 15;
	timeremstring = *(p - 2);
	statusstrings = p + 48;
	timetotargetstring = *(p - 1);
	warheadstrings = p + 57;
	timetodeststring = *p;
	p += 69;

	unknownstring = *p;
	buoystr = p + 1;

	/* Store species names in a side table because the packed 32-bit field
	 * cannot hold host pointers on LP64.
	 *
	 * Retail starts the loop with i = base+2460 (index 615) and
	 * increments i before each `*(i-1)` store, so iteration v25=k reads
	 * the pointer at base[615 + k]. After 69 iterations i ends at
	 * base[684]; retail's `v26 = *i` becomes viewfilmstr and
	 * `result = i + 1` (base[685]) becomes wingmanstrings. With p at
	 * base[598] here, name_ptr = p + 18 (= base[616]) lets
	 * `*(name_ptr - 1)` read base[615..683] for spec indices 0..68. */
	void** name_ptr = p + 18;
	for (i = 0; i < NUM_SPEC_DATA; i++) {
		spec_name_ptrs[i] = (const char*)*(name_ptr - 1);
		/* Keep the struct field's low-32 bits populated for any
		 * legacy reader that still inspects the layout directly. */
		spec_data[i].name_ptr = (int32_t)(uintptr_t)*(name_ptr - 1);
		name_ptr++;
	}
	/* name_ptr now points one past the last spec slot (= retail i+1
	 * after break); *(name_ptr - 1) is retail's `v26 = *i`. */
	viewfilmstr = *(name_ptr - 1);
	wingmanstrings = (const char**)name_ptr;
}

/* Monotonically-increasing counter bumped after fediskio_loadspecies
 * completes. Starts at 0 (no mission loaded yet) and increments by 1
 * on each successful load. Hosts read it via TieRecoveredData_MissionLoadGeneration()
 * and compare against their cached value to detect a mission change. */
static uint32_t s_mission_load_generation;

/* PORT: optional exact-size asset read replacing the original FILE-based
 * mission/newpal inverse-table loading branches. */
static int fediskio_try_load_tie98_inverse_palette(const char* filename) {
	if (!fediskio_tryopenfile(TIE_FILE_ROOT_FLIGHT_ASSET, filename, "rb", 0))
		return 0;
	const size_t bytes_read =
		TieStorage_Read(tie98_flight_inverse_palette, 1, sizeof tie98_flight_inverse_palette, fileptr);
	fediskio_tryclosefile(0);
	if (bytes_read == sizeof tie98_flight_inverse_palette)
		return 1;
	TieDiagnostics_Log(TIE_LOG_WARN, "TIE98 inverse palette has invalid size: %s\n", filename);
	return 0;
}

/* RECOVERY HELPER: extracted from the 8-bpp preamble of TIE98
 * FEDISKIO_loadspecies. */
static void fediskio_prepare_tie98_inverse_palette(void) {
	char inverse_filename[sizeof missionfilename];
	int loaded = 0;
	g_inversePaletteTable = tie98_flight_inverse_palette;

	const size_t mission_name_length = strlen(missionfilename);
	if (mission_name_length < sizeof inverse_filename) {
		memcpy(inverse_filename, missionfilename, mission_name_length + 1);
		char* extension = strrchr(inverse_filename, '.');
		if (extension && (size_t)(extension - inverse_filename) + sizeof ".inv" <= sizeof inverse_filename) {
			memcpy(extension, ".inv", sizeof ".inv");
			loaded = fediskio_try_load_tie98_inverse_palette(inverse_filename);
		}
	}
	if (!loaded)
		loaded = fediskio_try_load_tie98_inverse_palette("newpal.inv");
	if (!loaded)
		Color_BuildRgb565ToPaletteIndexTable(tie98_flight_inverse_palette, 0x40, 0x100);
	RenderTexture_ResetSoftwareShadeTableCache();
}

// FUNCTION: TIE 0x218D8, TIE98 0x41BA70
void fediskio_loadspecies(void) {
	/* Load ship species data from 3 LFD files.
	 * For each file: read directory, match against species_table entries,
	 * allocate per-species buffers, call fillinspec for hardpoint setup. */
	char path[64];
	uint8_t lfd_header[16];
	int lfd_idx, entry_idx;
	uint16_t i;

	if (TieProfile_UsesTie98Logic() && !g_useHardware3D && g_flight16bppBytesPerPixel == 1)
		fediskio_prepare_tie98_inverse_palette();

	if (TieProfile_UsesTie98Logic()) {
		char error[768];
		TieFlightModelApi models = TieFlightAssets_ModelApi();
		if (!models.begin_generation(models.context, error, sizeof error))
			shell_programexit(error);
	}
	if (TieProfile_UsesTie98Logic())
		g_hardwarePixelFormatAvailable = 1;

	/* Zero all species model handles + per-species blob sizes. */
	for (i = 0; i < NUM_SPECIES; i++) {
		species_table[i].model_handle = NULL;
		species_model_handle_sizes[i] = 0;
	}

	/* Binary calls UnlockGlobals here; with malloc this is a no-op */

	for (lfd_idx = 0; lfd_idx < 3; lfd_idx++) {
		/* Retail FEDISKIO_loadspecies selects its LFD directory based
		 * on flightResolution: "RES640/" for 640x480 (257) and "RES320/"
		 * for anything else (typically 19 = 320x200). This differs from
		 * the default `resourcedir` ("RESOURCE/") used by the rest of
		 * the disk I/O surface. */
		const char* species_dir = tie_is_high_resolution_flight() ? "RES640/" : "RES320/";
		snprintf(path, sizeof(path), "%s%s.lfd", species_dir, specieslfds[lfd_idx]);

		fediskio_tryopenfile(TIE_FILE_ROOT_FLIGHT_ASSET, path, "rb", 1);

		/* Read LFD file header (16 bytes) */
		TieStorage_Read(lfd_header, 1, 16, fileptr);

		/* Read the directory into log2_buf. */
		loadbuffer = log2_buf;
		/* LFD sub-header layout: type[4] + name[8] + size[4]. The size
		 * dword lives at offset +12, not +8. Every other LFD parser in
		 * the codebase (fsfx, fmusic, landru/res)
		 * reads it from +12 — this one was wrong. */
		uint32_t dir_size = br_u32le(lfd_header + 12);
		fediskio_readfileblock(loadbuffer, dir_size, 1, fileptr);

		/* Number of directory entries (16 bytes each) */
		int num_entries = (uint16_t)dir_size >> 4;
		int file_offset = 0;

		for (entry_idx = 0; entry_idx < num_entries; entry_idx++) {
			file_offset += 16;

			/* Check if any species entry references this LFD file + entry */
			int found = 0;
			int entry_flags = 0;

			for (i = 0; i < NUM_SPECIES; i++) {
				if (!(species_table[i].flags & 2))
					continue;
				if (species_table[i].lfd_file != lfd_idx)
					continue;
				if (species_table[i].lfd_entry != entry_idx)
					continue;
				if (!(species_table[i].load_flags & 0x18))
					continue;
				if ((species_table[i].load_flags & 0x40) && !mission.train_craft_type)
					continue;
				found = 1;
				entry_flags = species_table[i].load_flags;
			}

			/* Get entry data size from directory */
			uint32_t* dir_entry = (uint32_t*)((uint8_t*)loadbuffer + 16 * entry_idx);
			uint32_t entry_size = dir_entry[3];

			if (found) {
				if (TieClassicDisplay_UsesDx5()) {
					FrontendDisplay_BlitOffscreenToRenderSurface();
					FrontendDisplay_PresentFrame();
				}

				void* species_buf = NULL;
				uint32_t rgb_v39 = 0, rgb_v38 = 0;

				/* Read orientation header if present */
				if (entry_flags & 1) {
					uint8_t orient[2];
					entry_size -= 2;
					if (file_offset) {
						TieStorage_Seek(fileptr, file_offset, TIE_SEEK_CUR);
						file_offset = 0;
					}
					TieStorage_Read(orient, 1, 2, fileptr);
				} else if (entry_flags & 2) {
					/* RGB-sprite path (retail FEDISKIO_loadspecies @ 0x21BDC,
					 * TIE98 FEDISKIO_Load_Species_Models @ 0x41B600).
					 * 8-byte prefix (v39=body_off, v38=num_pixels) from file,
					 * then (entry_size-8) bytes of input payload loaded at
					 * buffer+8. The converter appends either VGA indices or
					 * RGB565 palette entries at buffer[v39]. */
					if (file_offset) {
						TieStorage_Seek(fileptr, file_offset, TIE_SEEK_CUR);
						file_offset = 0;
					}
					TieStorage_Read(&rgb_v39, 4, 1, fileptr);
					TieStorage_Read(&rgb_v38, 4, 1, fileptr);
					entry_size -= 8;

					const bool tie98_16bpp = TieProfile_UsesTie98Logic() && g_flight16bppBytesPerPixel == 2;
					const size_t palette_entry_size = tie98_16bpp ? 2u : (size_t)bytesPerPixel;
					species_buf = malloc(rgb_v39 + palette_entry_size * rgb_v38);
					if (!species_buf)
						fediskio_fatalerror(FATAL_ERROR_NOT_ENOUGH_MEMORY_X0A);

					((uint32_t*)species_buf)[0] = rgb_v39;
					((uint32_t*)species_buf)[1] = rgb_v38;
					fediskio_readfileblock((uint8_t*)species_buf + 8, entry_size, 1, fileptr);
					if (tie98_16bpp)
						rtsvga2_remapRGBImage_tie98((uint32_t*)species_buf);
					else
						rtsvga2_remapRGBImage((uint32_t*)species_buf);
				}

				if (!species_buf) {
					/* Allocate buffer for plain species data */
					species_buf = malloc(entry_size);
					if (!species_buf)
						fediskio_fatalerror(FATAL_ERROR_NOT_ENOUGH_MEMORY_X0A);

					if (file_offset) {
						TieStorage_Seek(fileptr, file_offset, TIE_SEEK_CUR);
						file_offset = 0;
					}
					fediskio_readfileblock(species_buf, entry_size, 1, fileptr);
				}

				/* Share one buffer across aliases; shutdown frees each unique pointer once. */
				for (i = 0; i < NUM_SPECIES; i++) {
					if (!(species_table[i].flags & 2))
						continue;
					if (species_table[i].lfd_file != lfd_idx)
						continue;
					if (species_table[i].lfd_entry != entry_idx)
						continue;
					if (!(species_table[i].load_flags & 0x18))
						continue;
					if ((species_table[i].load_flags & 0x40) && !mission.train_craft_type)
						continue;

					species_table[i].model_handle = species_buf;
					species_model_handle_sizes[i] = entry_size;

					if (entry_flags & 1) {
						if (TieProfile_UsesTie98Logic())
							fediskio_fillinspec_tie98(species_table[i].spec_num, (uint8_t)i);
						else
							fediskio_fillinspec(species_buf, species_table[i].spec_num, (uint8_t)i);
					}
				}
				/* DO NOT free here -- the buffer must outlive load and
				 * stay reachable for ANIM/DRAW/STATIC rendering. Freed
				 * in FreeFlightHandles with dedup. */
				if (TieClassicDisplay_UsesDx5()) {
					FrontendDisplay_BlitOffscreenToRenderSurface();
					FrontendDisplay_PresentFrame();
				}
			} else {
				file_offset += entry_size;
			}
		}

		fediskio_tryclosefile(0);
	}

	fediskio_RelockGlobals();
	if (TieProfile_UsesTie98Logic()) {
		g_hardwarePixelFormatAvailable = 0;
		const uint8_t deep_space_rgb[3] = { 0, 0, 2 };
		const uint8_t deep_space_index =
			(uint8_t)rtsvga2_findNearestColor(deep_space_rgb, rtsvga2_vgapalette, 0, 256);
		g_flightColorKeyIndex = deep_space_index;
		deepspacecolor = deep_space_index;
	}

	/* Bump the mission-load generation. Hosts use this to detect that
	 * the species_table model_handle set has been refreshed and they
	 * can warm their own per-species caches in one shot (or evict + re-
	 * warm on mission switch). See TieRecoveredData_MissionLoadGeneration(). */
	s_mission_load_generation++;
}

uint32_t TieRecoveredData_MissionLoadGeneration(void) { return s_mission_load_generation; }

static bool tie_species_lfd_location(uint16_t species_idx, uint8_t expected_source,
									 TieSpeciesLfdLocation* out) {
	if (out)
		memset(out, 0, sizeof *out);
	if (!out || species_idx >= NUM_SPECIES)
		return false;

	const SpeciesEntry* entry = &species_table[species_idx];
	if (!(entry->flags & 2) || !(entry->load_flags & 0x18) || (entry->load_flags & 3) != expected_source ||
		((entry->load_flags & 0x40) && !mission.train_craft_type) || !entry->model_handle ||
		entry->lfd_file >= 3)
		return false;

	out->entry = entry->lfd_entry;
	out->resource_set = tie_is_high_resolution_flight() ? TIE_SPECIES_LFD_RES640 : TIE_SPECIES_LFD_RES320;
	out->lfd_file = entry->lfd_file;
	return true;
}

bool TieRecoveredData_SpeciesDosModelLocation(uint16_t species_idx, TieSpeciesLfdLocation* out) {
	return tie_species_lfd_location(species_idx, 1, out);
}

bool TieRecoveredData_SpeciesXactLocation(uint16_t species_idx, TieSpeciesLfdLocation* out) {
	return tie_species_lfd_location(species_idx, 2, out);
}

/* RECOVERY HELPER: removes the repeated hardpoint-to-group construction for
 * both laser slots and both missile slots in TIE98 FEDISKIO_fillinspec. */
static uint8_t fediskio_fillinspec_tie98_appendweapongroup(uint8_t result, SpecData* spec,
														   uint8_t weapon_type, bool laser,
														   uint8_t model_type) {
	const int mesh_count = modelmesh_getcount(model_type);
	const int target_type = (uint8_t)(weapon_type + 120);
	const uint8_t start = result;
	for (int mesh = 0; mesh < mesh_count && result < 16; ++mesh) {
		const int mesh_type = modelmesh_gettype(model_type, mesh);
		int paired = -1;
		const int count = modelmesh_counthardpoints(model_type, mesh);
		for (int hardpoint = 0; hardpoint < count && result < 16; ++hardpoint) {
			int type, x, y, z;
			modelmesh_gethardpoint(model_type, mesh, hardpoint, &type, &x, &y, &z);
			if (type != target_type)
				continue;
			if (laser && paired >= 0) {
				spec->hp[paired].link = modelmesh_getalternatehardpointindex(model_type, mesh, hardpoint);
				paired = -1;
				continue;
			}
			if (model_type == 53) {
				x /= 2;
				y /= 2;
				z /= 2;
			}
			/* Shared SpecData uses flight-local (side, up, forward) order. */
			spec->hp[result].x = x;
			spec->hp[result].y = z;
			spec->hp[result].z = y;
			spec->hp[result].component = mesh;
			spec->hp[result].link = -1;
			if (laser && (mesh_type == TIE_MESH_GUN_TURRET || mesh_type == TIE_MESH_ROTARY_GUN_TURRET))
				paired = result;
			++result;
		}
	}
	if (result != start) {
		uint8_t* first = laser ? spec->laser_start : spec->missile_start;
		uint8_t* last = laser ? spec->laser_end : spec->missile_end;
		uint8_t* count = laser ? spec->laser_count : spec->missile_count;
		const int slot = laser ? (weapon_type == spec->laser_type[0] ? 0 : 1)
							   : (weapon_type == spec->missile_type[0] ? 0 : 1);
		first[slot] = start;
		last[slot] = result - 1;
		count[slot] = result - start;
	}
	return result;
}

// FUNCTION: TIE98 0x41BE70 FEDISKIO_fillinspec
// PORT: writes the recovered TIE95 runtime SpecData layout from OPT metadata.
void fediskio_fillinspec_tie98(uint8_t spec_index, uint8_t model_type) {
	modelmesh_require_craft_capacity(model_type);
	const int extent = modelbounds_getmaxextent(model_type);
	species_table[model_type].bound_hwidth = extent;
	species_table[model_type].bound_qdepth = extent >> 1;
	if (spec_index == 255)
		return;

	SpecData* spec = &spec_data[spec_index];
	int width = modelbounds_getsizex(model_type);
	int depth = modelbounds_getsizey(model_type);
	int height = modelbounds_getsizez(model_type);
	int shift = 0;
	while (width > 640 || depth > 640 || height > 640) {
		width >>= 1;
		depth >>= 1;
		height >>= 1;
		++shift;
	}
	spec->model_scale_shift = shift;
	spec->bound_width = width;
	spec->bound_depth = depth;
	spec->bound_height = height;
	if (!spec->dock_active_light) {
		spec->dock_active_light = modelbounds_getminz(model_type);
		spec->dock_active_heavy = modelbounds_getminz(model_type);
	}
	if (!spec->dock_passive_light) {
		spec->dock_passive_light = modelbounds_getmaxz(model_type);
		spec->dock_passive_heavy = modelbounds_getmaxz(model_type);
	}

	const int mesh_count = modelmesh_getcount(model_type);
	for (int mesh = 0; mesh < mesh_count; ++mesh) {
		const int mesh_type = modelmesh_gettype(model_type, mesh);
		const int hardpoint_count = modelmesh_counthardpoints(model_type, mesh);
		for (int hardpoint = 0; hardpoint < hardpoint_count; ++hardpoint) {
			int type, x, y, z;
			modelmesh_gethardpoint(model_type, mesh, hardpoint, &type, &x, &y, &z);
			int special_hardpoint = 1;
			switch (type) {
				case 25:
					spec->cockpit_x = x;
					spec->cockpit_y = z;
					spec->cockpit_z = y;
					break;
				case 26:
					spec->engine_x = x;
					spec->engine_y = z;
					spec->engine_z = y;
					break;
				case 27:
					spec->dock_passive_heavy = z;
					spec->dock_fwd = y;
					break;
				case 28:
					spec->dock_passive_light = z;
					spec->dock_fwd = y;
					break;
				case 29:
					spec->dock_active_heavy = z;
					spec->dock_fwd = y;
					break;
				case 30:
					spec->dock_active_light = z;
					spec->dock_fwd = y;
					break;
				case 31:
					spec->gun_muzzle_up = z;
					spec->gun_muzzle_fwd = y;
					break;
				default:
					special_hardpoint = 0;
					break;
			}
			if (special_hardpoint)
				continue;

			const uint8_t weapon_type = (uint8_t)(type - 120);
			int known_weapon_type = 0;
			for (int slot = 0; slot < 2; ++slot) {
				if (spec->laser_type[slot] == weapon_type || spec->missile_type[slot] == weapon_type) {
					known_weapon_type = 1;
					break;
				}
			}
			if (known_weapon_type)
				continue;

			if (tie98_hardpoint_weapon_class[type] == 1) {
				int slot;
				for (slot = 0; slot < 2 && spec->laser_type[slot]; ++slot)
					;
				if (slot == 2)
					continue;
				spec->laser_type[slot] = weapon_type;
				if (mesh_type == TIE_MESH_GUN_TURRET || mesh_type == TIE_MESH_ROTARY_GUN_TURRET ||
					mesh_type == TIE_MESH_SMALL_GUN || species_table[model_type].ship_class == 3 ||
					species_table[model_type].ship_class == 4 || species_table[model_type].ship_class == 5)
					spec->laser_fire_mode[slot] = 2;
				else
					spec->laser_fire_mode[slot] = (type == 5 || type == 16);
			} else if (tie98_hardpoint_weapon_class[type] == 2) {
				int slot;
				for (slot = 0; slot < 2 && spec->missile_type[slot]; ++slot)
					;
				if (slot < 2)
					spec->missile_type[slot] = weapon_type;
			}
		}
	}

	uint8_t result = 0;
	for (int slot = 0; slot < 2; ++slot) {
		if (result == 16) {
			spec->laser_type[slot] = 0;
			spec->laser_fire_mode[slot] = 0;
			continue;
		}
		result = fediskio_fillinspec_tie98_appendweapongroup(result, spec, spec->laser_type[slot], true,
															 model_type);
	}
	for (int slot = 0; slot < 2; ++slot) {
		if (result == 16) {
			spec->missile_type[slot] = 0;
			continue;
		}
		result = fediskio_fillinspec_tie98_appendweapongroup(result, spec, spec->missile_type[slot], false,
															 model_type);
	}
}

// FUNCTION: TIE 0x21E48
void fediskio_fillinspec(void* data, uint8_t lfd_idx, uint8_t species_idx) {
	/* Skip the 2-byte file-size prefix so struct offsets line up with
	 * retail FEDISKIO_fillinspec's `v48 = a1 + 2` convention. */
	ShipModelData* model = (ShipModelData*)((uint8_t*)data + 2);
	SpecData* spec;
	uint16_t half_width, half_height, half_depth;
	int16_t extra_shift, total_shift;
	ShipModelMesh* cur_mesh;
	uint8_t result;
	int i;

	/* Set species bounding half-length */
	uint16_t half_length = model->length >> 1;
	species_table[species_idx].bound_hwidth = half_length;
	if (model->model_scale_shift) {
		species_table[species_idx].bound_hwidth = species_table[species_idx].bound_hwidth
												  << model->model_scale_shift;
	}
	species_table[species_idx].bound_qdepth = model->length >> 2;

	/* Sentinel 0xFF means "no spec entry" -- skip every spec_data
	 * write. The retail does this by gating all `59 * a2` offsets on
	 * `a2 != 255`; we have to defer taking &spec_data[lfd_idx] until
	 * after this check, otherwise the address arithmetic is UB
	 * (spec_data has only NUM_SPECIES==69 elements). */
	if (lfd_idx == 255)
		return;
	spec = &spec_data[lfd_idx];

	/* Compute dimensions with LOD scaling */
	half_width = model->width >> 1;
	half_height = model->height >> 1;
	half_depth = model->depth >> 1;
	cur_mesh = (ShipModelMesh*)&model->lod_records[model->num_lods];
	extra_shift = 0;

	while (half_width > 640 || half_depth > 640 || half_height > 640) {
		half_width >>= 1;
		half_height >>= 1;
		half_depth >>= 1;
		extra_shift++;
	}

	total_shift = extra_shift + (int8_t)model->model_scale_shift;
	spec->model_scale_shift = total_shift;
	/* Retail writes (model+8)>>1 to spec+0xE8 and (model+6)>>1 to spec+0xEA
	 * (FEDISKIO_fillinspec: v9 and i). We currently label spec+0xE8
	 * bound_height and spec+0xEA bound_depth, and ShipModelData+6/+8
	 * height/depth -- so to stay byte-faithful the values must cross over
	 * here rather than copy straight through. Most likely one of those name
	 * pairs is mislabeled (the field at +0xE8 is not really "height"); the
	 * physical axis identities are unverified. The cross-assignment below is
	 * what matters; revisit the names once the axes are pinned down. */
	spec->bound_width = half_width;
	spec->bound_height = half_depth;
	spec->bound_depth = half_height;

	/* Default active-dock ranges from shields and passive ranges from speed. */
	if (!spec->dock_active_light) {
		spec->dock_active_light = model->shield_default >> 17;
		spec->dock_active_heavy = model->shield_default >> 17;
	}
	if (!spec->dock_passive_light) {
		spec->dock_passive_light = model->speed_default >> 17;
		spec->dock_passive_heavy = model->speed_default >> 17;
	}

	/* Walk mesh components, extract hardpoints and reference points */
	for (i = 0; i < model->num_meshes; i++) {
		if (cur_mesh->num_hardpoints) {
			ShipModelHardpoint* hp = (ShipModelHardpoint*)((uint8_t*)cur_mesh + cur_mesh->hardpoint_offset);

			for (uint16_t hp_scan = 0; hp_scan < cur_mesh->num_hardpoints; hp_scan++, hp++) {
				int16_t matched = 0;

				switch (hp->type) {
					case 0x19: /* cockpit position: v0=X, v1=Y, v2=Z */
						spec->cockpit_x = hp->local_x >> 1;
						spec->cockpit_y = hp->local_y >> 1;
						spec->cockpit_z = hp->local_z >> 1;
						matched = 1;
						break;
					case 0x1A: /* engine position: v0=X, v1=Y, v2=Z */
						spec->engine_x = hp->local_x >> 1;
						spec->engine_y = hp->local_y >> 1;
						spec->engine_z = hp->local_z >> 1;
						matched = 1;
						break;
					case 0x1B:
						spec->dock_passive_heavy = hp->local_y >> 1;
						spec->dock_fwd = hp->local_z >> 1;
						matched = 1;
						break;
					case 0x1C:
						spec->dock_passive_light = hp->local_y >> 1;
						spec->dock_fwd = hp->local_z >> 1;
						matched = 1;
						break;
					case 0x1D:
						spec->dock_active_heavy = hp->local_y >> 1;
						spec->dock_fwd = hp->local_z >> 1;
						matched = 1;
						break;
					case 0x1E:
						spec->dock_active_light = hp->local_y >> 1;
						spec->dock_fwd = hp->local_z >> 1;
						matched = 1;
						break;
					case 0x1F: /* turret position */
						spec->gun_muzzle_up = hp->local_y >> 1;
						spec->gun_muzzle_fwd = hp->local_z >> 1;
						matched = 1;
						break;
				}

				if (!matched) {
					/* Weapon hardpoint — classify type.
					 * weapon_id MUST stay uint8_t: hp->type is in [1..18]
					 * for real weapons, so (hp->type - 120) is always
					 * negative. A signed char here would sign-extend in the
					 * dedup compares below while the uint8_t array entries
					 * zero-extend, so no two equal weapon ids ever match. */
					uint16_t scan;
					uint8_t weapon_id = (uint8_t)(hp->type - 120);

					/* Check if this weapon type already has a laser slot */
					for (scan = 0; scan < 2 && weapon_id != spec->laser_type[scan]; scan++)
						;
					if (scan >= 2) {
						/* Check missile slots */
						for (scan = 0; scan < 2 && weapon_id != spec->missile_type[scan]; scan++)
							;
						if (scan >= 2) {
							int ws_type = weaponsystype[hp->type];
							if (ws_type == 1) {
								/* Laser — find free slot */
								uint16_t free_slot;
								for (free_slot = 0; free_slot < 2 && spec->laser_type[free_slot]; free_slot++)
									;
								if (free_slot < 2) {
									spec->laser_type[free_slot] = weapon_id;
									uint16_t mesh_type = cur_mesh->mesh_type;
									uint8_t craft_class = species_table[species_idx].ship_class;
									if (mesh_type == 4 || mesh_type == 21 || mesh_type == 5 ||
										craft_class == 5 || craft_class == 4) {
										spec->laser_fire_mode[free_slot] = 2;
									} else {
										int8_t wid = (int8_t)hp->type;
										if (wid == 5 || wid == 16)
											spec->laser_fire_mode[free_slot] = 1;
										else
											spec->laser_fire_mode[free_slot] = 0;
									}
								}
							} else if (ws_type == 2) {
								/* Missile — find free slot */
								uint16_t free_slot;
								for (free_slot = 0; free_slot < 2 && spec->missile_type[free_slot];
									 free_slot++)
									;
								if (free_slot < 2)
									spec->missile_type[free_slot] = weapon_id;
							}
						}
					}
				}
			}
		}
		cur_mesh++;
	}

	/* Build laser hardpoint position tables (up to 16 total) */
	result = 0;
	for (uint16_t slot = 0; slot < 2; slot++) {
		if (result == 16) {
			spec->laser_type[slot] = 0;
			spec->laser_fire_mode[slot] = 0;
			continue;
		}

		ShipModelMesh* mesh = (ShipModelMesh*)&model->lod_records[model->num_lods];
		uint8_t target_id = spec->laser_type[slot] + 120;
		uint8_t start_hp = result;

		for (uint16_t mi = 0; mi < model->num_meshes; mi++) {
			if (mesh->num_hardpoints) {
				int16_t paired = 255;
				ShipModelHardpoint* hp = (ShipModelHardpoint*)((uint8_t*)mesh + mesh->hardpoint_offset);

				for (uint16_t hp_idx = 0; hp_idx < mesh->num_hardpoints; hp_idx++, hp++) {
					if (hp->type != target_id)
						continue;
					if (paired == 255) {
						/* Weapon hardpoint coords: v0=X, v1=Y, v2=Z. */
						spec->hp[result].x = hp->local_x >> 1;
						spec->hp[result].y = hp->local_y >> 1;
						spec->hp[result].z = hp->local_z >> 1;
						spec->hp[result].component = mi;
						spec->hp[result].link = -1;
						if (mesh->mesh_type == 4 || mesh->mesh_type == 21)
							paired = result;
						if (++result == 16)
							break;
					} else {
						spec->hp[paired].link = hp->link;
						paired = 255;
					}
				}
				if (result == 16)
					break;
			}
			mesh++;
		}

		if (result != start_hp) {
			spec->laser_start[slot] = start_hp;
			spec->laser_end[slot] = result - 1;
			spec->laser_count[slot] = result - start_hp;
		}
	}

	/* Build missile/warhead hardpoint position tables */
	for (uint16_t slot = 0; slot < 2; slot++) {
		if (result == 16) {
			spec->missile_type[slot] = 0;
			continue;
		}

		ShipModelMesh* mesh = (ShipModelMesh*)&model->lod_records[model->num_lods];
		uint8_t target_id = spec->missile_type[slot] + 120;
		uint8_t start_hp = result;

		for (uint16_t mi = 0; mi < model->num_meshes; mi++) {
			if (mesh->num_hardpoints) {
				ShipModelHardpoint* hp = (ShipModelHardpoint*)((uint8_t*)mesh + mesh->hardpoint_offset);

				for (uint16_t hp_idx = 0; hp_idx < mesh->num_hardpoints; hp_idx++, hp++) {
					if (hp->type != target_id)
						continue;
					/* Missile hardpoint coords: v0=X, v1=Y, v2=Z. */
					spec->hp[result].x = hp->local_x >> 1;
					spec->hp[result].y = hp->local_y >> 1;
					spec->hp[result].z = hp->local_z >> 1;
					spec->hp[result].component = mi;
					if (++result == 16)
						break;
				}
				if (result == 16)
					break;
			}
			mesh++;
		}

		if (result != start_hp) {
			spec->missile_start[slot] = start_hp;
			spec->missile_end[slot] = result - 1;
			spec->missile_count[slot] = result - start_hp;
		}
	}
}
