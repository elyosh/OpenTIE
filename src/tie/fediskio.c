#include "tie/fediskio.h"
#ifdef TIE_MODERN
#include "tie_runtime/storage/pilot_storage.h"
#endif
#include "tie/edition.h"
#include "tie/fmusic.h"
#include "tie/fscript.h"
#include "tie/rtsvga2.h" /* rtsvga2_remapRGBImage */
#include "tie/spec.h"
#include "tie/tie.h" /* flightResolution */
#include "tie_runtime/audio/config.h"
#include "tie_runtime/audio/music_policy.h"
#include "tie_runtime/diagnostics/diagnostics.h"
#include "tie_runtime/display/classic_display.h"
#include "tie_runtime/display/classic_framebuffer.h"
#include "tie_runtime/flight_assets/model_types.h"
#include "tie_runtime/flight_assets/service.h"
#include "tie_runtime/input/input.h"
#include "tie_runtime/runtime/exports.h"
#include "tie_runtime/runtime/panel_view_buffers.h"
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

#include "tie/damage.h" /* systemstrings owner */
#include "tie/gate.h"
#include "tie/logbuf2.h"
#include "tie/msg.h"
#include "tie/panel.h"
#include "tie/panelrts.h" /* buoystr / unknownstring / statusstrings / warheadstrings */
#include "tie_runtime/runtime/inflight_state.h"

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

#ifdef TIE_MODERN
#include "tie_runtime/storage/string_table.h"
#endif
#include <landru/memhdl.h>

#include <stdio.h> /* snprintf */
#include <stdlib.h>
#include <string.h>

#ifndef TIE_MODERN
#include <conio.h>
#endif

/* --- Static data --- */

// GLOBAL: TIE95 0xC1C98
// GLOBAL: TIE98 0x4DFF70
char resourcedir[10] = "RESOURCE\\";
// GLOBAL: TIE95 0xC1CB2
// GLOBAL: TIE98 0x4DFF90
char fatalmemorystr[27] = "Error! Not Enough Memory!\n";
// GLOBAL: TIE95 0xC1CCD
// GLOBAL: TIE98 0x4DFFB0
char fatalfilemissingstr[55] = "Error! The following file is missing or inaccessible: ";
/* Built-in messages used until STRINGS.DAT rebinds fatalerrstrings. */
// GLOBAL: TIE95 0xC1D04
// GLOBAL: TIE98 0x4DFFE8
char* fatalerrstr[2] = { fatalmemorystr, fatalfilemissingstr };
// GLOBAL: TIE95 0xC1D0C
// GLOBAL: TIE98 0x4DFFF0
char** fatalerrstrings = fatalerrstr;
// GLOBAL: TIE95 0xC1E08
// GLOBAL: TIE98 0x4E00FC
char** flightloadstrings;

// GLOBAL: TIE95 0xC1E10
// GLOBAL: TIE98 0x4E0100
uint32_t rankscores[5] = { 20000, 50000, 100000, 250000, 500000 };

// GLOBAL: TIE95 0xC1E24
// GLOBAL: TIE98 0x4E0118
uint32_t secretscores[12] = { 20000,   50000,   100000,  250000,  400000,  800000,
							  1000000, 1200000, 1400000, 1600000, 1800000, 2000000 };

// GLOBAL: TIE95 0xC1E54
// GLOBAL: TIE98 0x4E0148
uint8_t secretcompletioncnts[12] = { 2, 4, 6, 9, 12, 15, 18, 20, 22, 24, 26, 28 };

// GLOBAL: TIE95 0xC1E60
// GLOBAL: TIE98 0x4E0158
uint8_t battlemask[8] = { 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80 };

/* Species LFD filenames (3 files, 9 chars each, without .lfd extension) */
// GLOBAL: TIE95 0xC1E68
char specieslfds[3][9] = { "SPECIES", "SPECIES2", "SPECIES3" };

/* Weapon system type classification (maps weapon ID to 1=laser, 2=missile, 0=none).
 * IDs 1-6,9-11 = laser (1), IDs 7-8,12-18 = missile/warhead (2). */
// GLOBAL: TIE95 0xC1E83
// GLOBAL: TIE98 0x4E01A0
uint8_t weaponsystype[33] = { 0, 1, 1, 1, 1, 1, 1, 2, 2, 1, 1, 1, 2, 2, 2, 2, 2,
							  2, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

/* --- Globals --- */

// GLOBAL: TIE95 0xD4068
// GLOBAL: TIE98 0x6267A0
// PORT: shared storage includes the common 16-character name and ".tfr".
char pilotname[TIE_PILOT_FILENAME_CAPACITY];
// GLOBAL: TIE95 0xD4078
// GLOBAL: TIE98 0x6267E0
char openfilename[256];
// GLOBAL: TIE95 0xD4178
// GLOBAL: TIE98 0x6267C8
TieFile* fileptr;
// GLOBAL: TIE95 0xD417C
// GLOBAL: TIE98 0x51F858
static LandruHandle font1handle;
// GLOBAL: TIE95 0xD417E
// GLOBAL: TIE98 0x51F85C
static LandruHandle font2handle;
// GLOBAL: TIE95 0xD4180
// GLOBAL: TIE98 0x50F854
static LandruHandle log1handle;
// GLOBAL: TIE95 0xD4182
// GLOBAL: TIE98 0x6268E0
LandruHandle log2handle;
// GLOBAL: TIE95 0xD4184
// GLOBAL: TIE98 0x50F848
static LandruHandle musichandle;
/* TRACE2 edge pools, locked by xtrans2_initxtrans. BPFLIGHT allocates them
 * for frontend previews when FEDISKIO has not. */
// GLOBAL: TIE95 0xD4186
// GLOBAL: TIE98 0x626786
LandruHandle flightbuf_big_handle;
// GLOBAL: TIE95 0xD4188
// GLOBAL: TIE98 0x6268E4
LandruHandle flightbuf_small_handle;
// GLOBAL: TIE95 0xD418C
// GLOBAL: TIE98 0x626788
uint8_t currentmission;
// GLOBAL: TIE95 0xD418F
// GLOBAL: TIE98 0x6267C6
uint8_t currentbattle;

/* Shared flight handles (tie.c-owned in the TIE95 demo symbols). */
// GLOBAL: TIE95 0xEB6B6
// GLOBAL: TIE98 0x5926A8
LandruHandle panelpartshandle;
// GLOBAL: TIE95 0xEB6C6
// GLOBAL: TIE98 0x595F68
LandruHandle rundiffhandle;
// GLOBAL: TIE95 0xEB6C8
// GLOBAL: TIE98 0x591E2A
LandruHandle replaybufferhandle;
// GLOBAL: TIE95 0xEB6CA
// GLOBAL: TIE98 0x5A26B4
LandruHandle maproomiconshandle;
#ifndef TIE_MODERN
// GLOBAL: TIE95 0xEB6BA
// GLOBAL: TIE98 0x592646
static LandruHandle stringdatahandle;
// GLOBAL: TIE95 0xD4194
// GLOBAL: TIE98 0x50F84C
static char* stringdata_base;
#endif

// GLOBAL: TIE98 0x50F858
static uint8_t tie98_flight_inverse_palette[0x10000];

/* --- Pilot record I/O --- */

// FUNCTION: TIE95 0x204A0
void fediskio_initpilotrecord(int16_t clear_name) {
	uint8_t* raw;
	uint16_t i;

	if (clear_name)
		pilotname[0] = '\0';

	/* Initialize the primary disk slot of the pilot record in loadbuffer
	 * with version=1, game_level=1, everything else 0. The version and
	 * game_level bytes happen to be at the same offsets (+0x000 and +0x003)
	 * in both the in-memory struct and the disk format, but the rest of the
	 * layout differs -- always touch loadbuffer as raw bytes, never cast it
	 * as PilotRecord *. */
	raw = (uint8_t*)loadbuffer;
	for (i = 0; i < PILOTRECORD_DISK_SIZE; i++)
		*raw++ = 0;
	((uint8_t*)loadbuffer)[0x000] = 1; /* version */
	((uint8_t*)loadbuffer)[0x003] = 1; /* game_level */
}

// FUNCTION: TIE95 0x204DC
void fediskio_createpilotrecord(void) {
#ifdef TIE_MODERN
	PilotRecord pilot_value;
	PilotRecord* pilot = &pilot_value;
#else
	PilotRecord* pilot;
#endif
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

#ifdef TIE_MODERN
	PilotRecord_decode(pilot, (const uint8_t*)loadbuffer);
#else
	pilot = (PilotRecord*)loadbuffer;
#endif
	mission.difficulty = pilot->game_level;

	if (mission.mission_mode == 4) {
		memcpy(mission.mission_linked_data, pilot->linked_data, 256);
		currentbattle = pilot->cur_battle;
		currentmission = pilot->battle_cursor[currentbattle];
	} else {
		const uint8_t* raw;

		for (i = 0; i < 256; i++)
			mission.mission_linked_data[i] = 0;
		/* Snapshot combat-sim ship/course for fsfx_loadvoicelfd. In
		 * mode 5 (combat-of-tour) cur_combat_ship is encoded as
		 * 12 + battle, which the loader subtracts back out to reach
		 * the battle digit. The course cursor is read at disk offset
		 * 0x67+voice_id_a -- for voice_id_a < 12 this lines up with
		 * combat_course_cursor[voice_id_a]. Read via the raw disk-byte
		 * offset because the retail function addresses this field from
		 * the serialized pilot image. */
		raw = (const uint8_t*)loadbuffer;
		voice_id_a = pilot->cur_combat_ship;
		voice_id_b = raw[0x67 + voice_id_a];
	}
}
/* loadbuffer holds the raw disk-format pilot record (two PILOTRECORD_DISK_SIZE
 * slots back-to-back: primary at +0, backup at +PILOTRECORD_DISK_SIZE). The
 * in-memory PilotRecord struct is naturally aligned and 1936 bytes, which
 * differs from the 1928-byte on-disk record -- never raw-cast loadbuffer as
 * `PilotRecord *`. Use PilotRecord_decode / _encode at every access. */

// FUNCTION: TIE95 0x205B8
int16_t fediskio_readpilotrecord(const char* name) {
	if (!fediskio_tryopenfile(TIE_FILE_ROOT_USER, name, "rb", 0))
		return 0;

	fediskio_readfileblock(loadbuffer, PILOTRECORD_DISK_SIZE, 2, fileptr);
	fediskio_tryclosefile(0);
	return 1;
}

// FUNCTION: TIE95 0x205F8
int16_t fediskio_writepilotrecord(const char* name) {
	if (!fediskio_tryopenfile(TIE_FILE_ROOT_USER, name, "wb", 0))
		return 0;

	fediskio_writefileblock(loadbuffer, PILOTRECORD_DISK_SIZE, 2, fileptr);
	fediskio_tryclosefile(0);
	return 1;
}

// FUNCTION: TIE95 0x20668
int16_t fediskio_updatepilotrecord(int16_t exit_status, int16_t ejected) {
#ifdef TIE_MODERN
	PilotRecord pilot;
	PilotRecord* p = &pilot;
#else
	PilotRecord* p;
#endif
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
#ifdef TIE_MODERN
	PilotRecord_decode(p, (const uint8_t*)loadbuffer);
#else
	p = (PilotRecord*)loadbuffer;
#endif

	/* --- Training mode (train_craft_type nonzero = training mission) --- */
	if (mission.train_craft_type) {
		uint8_t prev_level;

		switch (mission.train_craft_type_src) {
			case 5:
				ship_idx = 0; /* TIE Fighter */
				break;
			case 6:
				ship_idx = 1; /* TIE Interceptor */
				break;
			case 7:
				ship_idx = 2; /* TIE Bomber */
				break;
			case 8:
				ship_idx = 3; /* TIE Advanced */
				break;
			case 9:
				ship_idx = 5; /* TIE Defender */
				break;
			case 16:
				ship_idx = 4; /* Assault Gunboat */
				break;
			default:
				ship_idx = 6;
				break;
		}

		if ((uint32_t)mission.mission_score > (uint32_t)p->train_score[ship_idx])
			p->train_score[ship_idx] = mission.mission_score;

		prev_level = mission.train_level - 1;
		if (prev_level > p->train_max_level[ship_idx])
			p->train_max_level[ship_idx] = prev_level;

		if (mission.train_level >= 9 && !p->rank) {
			p->rank = 1;
			mission.mission_new_rank = p->rank;
		}
	} else {
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
			uint32_t avg;

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
			avg = ((uint32_t)p->score) / 4;
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
				uint16_t won;
				uint16_t s;
				uint16_t c;

				p->combat_complete[ship][course] = 1;

				won = 0;
				for (s = 0; s < NUM_SHIPS; s++)
					for (c = 0; c < 8; c++)
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
						uint32_t sec_total;
						uint8_t sec_rank;

						p->secret_complete_bits[battle] |= battlemask[cur_mis];

						p->secret_completions++;
						sec_total = total_score + p->secret_score;
						p->secret_score = sec_total;

						sec_rank = p->secret_order_rank;
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
	}

	/* Preserve the pre-mission backup slot for automatic pilot restore. */
#ifdef TIE_MODERN
	PilotRecord_encode((uint8_t*)loadbuffer, p);
#endif

	if (!fediskio_tryopenfile(TIE_FILE_ROOT_USER, pilotname, "wb", 1))
		return 0;

	fediskio_writefileblock(loadbuffer, PILOTRECORD_DISK_SIZE, 2, fileptr);
	fediskio_tryclosefile(0);
	return 0;
}

// FUNCTION: TIE95 0x20D8C
void fediskio_loadbufferdata(const char* filename, uint16_t buf_index, int16_t num_entries,
							 uint16_t skip_count) {
	int16_t line_idx = 0;

	fediskio_tryopenfile(TIE_FILE_ROOT_FLIGHT_ASSET, filename, "rb", 1);

	while (num_entries > 0) {
		int ch;

		farbufferptrs[buf_index] = farbufferptr;
		if (line_idx >= (int16_t)skip_count)
			buf_index++;

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

/* --- Buffer loading --- */

// FUNCTION: TIE95 0x20E48
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

/* --- Flight engine buffer management --- */

// FUNCTION: TIE95 0x20ED4
// FUNCTION: TIE98 0x41AAC0
void fediskio_Init_Buffers_and_Fonts(void) {
	int fail = 0;
	char path[64];
	const size_t screen_buffer_size = TIE_DISPLAY_EDITION((size_t)bytesPerPixel * screenYRes * screenXRes,
														  (size_t)screenYRes * g_surfacePitch);

	/* Error formatting depends on stringdata; later allocation failures can
	 * be accumulated and reported after it loads. */
#ifdef TIE_MODERN
	if (!TieStringTable_Allocate())
		fediskio_fatalerror(FATAL_ERROR_NOT_ENOUGH_MEMORY_X0A);
#else
	stringdatahandle = xmemhdl_Alloc_Handle(16000, LANDRU_MEMORY_DEFAULT);
	if (!stringdatahandle)
		fediskio_fatalerror(FATAL_ERROR_NOT_ENOUGH_MEMORY_X0A);
#endif

	font1handle = xmemhdl_Alloc_Handle(34600, LANDRU_MEMORY_DEFAULT);
	if (!font1handle)
		fail = 1;

	font2handle = xmemhdl_Alloc_Handle(20600, LANDRU_MEMORY_DEFAULT);
	if (!font2handle)
		fail = 1;

	log1handle = xmemhdl_Alloc_Handle((uint32_t)screen_buffer_size, LANDRU_MEMORY_DEFAULT);
	if (!log1handle)
		fail = 1;

	log2handle = xmemhdl_Alloc_Handle((uint32_t)screen_buffer_size, LANDRU_MEMORY_DEFAULT);
	if (!log2handle)
		fail = 1;

	/* EdgeHeader contains host pointers, so allocate by record count. */
	flightbuf_small_handle = xmemhdl_Alloc_Handle((uint32_t)(TRACE2_EDGEINFO_CAP * sizeof(trace2_EdgeInfo)),
												  LANDRU_MEMORY_DEFAULT);
	if (!flightbuf_small_handle)
		fail = 1;

	flightbuf_big_handle = xmemhdl_Alloc_Handle((uint32_t)(TRACE2_EDGEHEADER_CAP * sizeof(trace2_EdgeHeader)),
												LANDRU_MEMORY_DEFAULT);
	if (!flightbuf_big_handle)
		fail = 1;

	panelpartshandle = xmemhdl_Alloc_Handle(0x1ADB0, LANDRU_MEMORY_DEFAULT);
	if (!panelpartshandle)
		fail = 1;

	maproomiconshandle = xmemhdl_Alloc_Handle(31060, LANDRU_MEMORY_DEFAULT);
	if (!maproomiconshandle)
		fail = 1;

	rundiffhandle = xmemhdl_Alloc_Handle(19452, LANDRU_MEMORY_DEFAULT);
	if (!rundiffhandle)
		fail = 1;

	/* One complete fixed-size replay chunk. */
#ifdef TIE_MODERN
	/* PORT: cleared storage keeps temporary-buffer snapshots deterministic
	 * when the final chunk is only partially used. */
	replaybufferhandle = xmemhdl_Alloc_Clear_Handle(REPLAY_INPUT_BUFFER_BYTES, LANDRU_MEMORY_DEFAULT);
#else
	replaybufferhandle = xmemhdl_Alloc_Handle(REPLAY_INPUT_BUFFER_BYTES, LANDRU_MEMORY_DEFAULT);
#endif
	if (!replaybufferhandle)
		fail = 1;

	messageloghandle = xmemhdl_Alloc_Handle(32000, LANDRU_MEMORY_DEFAULT);
	if (!messageloghandle)
		fail = 1;

	if (musicenabled && TieMusicPolicy_UsesImuse()) {
		fmusic_allocmusicbuffer();
		musichandle = xmemhdl_Alloc_Handle(0x8000, LANDRU_MEMORY_DEFAULT);
		if (!musichandle)
			fail = 1;
	} else {
		music_buffer = NULL;
	}

	fediskio_loadstringdata(1);

	if (fail)
		fediskio_fatalerror(FATAL_ERROR_NOT_ENOUGH_MEMORY_X0A);

	if (musicenabled && TieMusicPolicy_UsesImuse())
		music_buffer = xmemhdl_Lock_Handle(musichandle);

	fontptrtiny = xmemhdl_Lock_Handle(font1handle);
	fontptrmicro = xmemhdl_Lock_Handle(font2handle);

	newbuf = xmemhdl_Lock_Handle(log1handle);
	logbuf2_selectbuffer(newbuf);

	xtransdataptr = xmemhdl_Lock_Handle(log2handle);
	loadbuffer = xtransdataptr;

	replaybufferstart = xmemhdl_Lock_Handle(replaybufferhandle);
	if (TIE_FLIGHT_TIE98) {
		memset(newbuf, 0x40, (size_t)screenXRes * screenYRes * g_flight16bppBytesPerPixel);
		fediskio_readfiletofarmemory(TIE_FILE_ROOT_FLIGHT_ASSET, "vga.pac", xtransdataptr);
		buildpalette(xtransdataptr, 64, 192);
		unblank();
	}

	if (flightResolution == TIE_FLIGHT_RES_SVGA || flightResolution == TIE_FLIGHT_RES_SVGA_16 ||
		flightResolution == TIE_FLIGHT_RES_SVGA_D3D) {
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

// FUNCTION: TIE95 0x212E4
void fediskio_UnlockGlobals(void) {
	if (musicenabled && TieMusicPolicy_UsesImuse())
		xmemhdl_Unlock_Handle(musichandle);
#ifndef TIE_MODERN
	xmemhdl_Unlock_Handle(stringdatahandle);
#endif
	xmemhdl_Unlock_Handle(font1handle);
	xmemhdl_Unlock_Handle(font2handle);
	xmemhdl_Unlock_Handle(log1handle);
	xmemhdl_Unlock_Handle(log2handle);
	xmemhdl_Unlock_Handle(replaybufferhandle);
}

// FUNCTION: TIE95 0x21348
void fediskio_RelockGlobals(void) {
	/* Rebind the string groups and active font/buffer pointers. */
	if (musicenabled && TieMusicPolicy_UsesImuse())
		music_buffer = xmemhdl_Lock_Handle(musichandle);

	fediskio_loadstringdata(0);

	fontptrtiny = xmemhdl_Lock_Handle(font1handle);
	fontptrmicro = xmemhdl_Lock_Handle(font2handle);
	/* Re-bind the active font to the freshly relocked handle. Without
	 * this, callers continue to read through the old curfontptr which
	 * may have been freed/recycled by the resource swap. */
	if (fontflag == 1)
		curfontptr = fontptrtiny;
	else if (fontflag == 2)
		curfontptr = fontptrmicro;
	newbuf = xmemhdl_Lock_Handle(log1handle);
	logbuf2_selectbuffer(newbuf);
	xtransdataptr = xmemhdl_Lock_Handle(log2handle);
	loadbuffer = xtransdataptr;
	replaybufferstart = xmemhdl_Lock_Handle(replaybufferhandle);
}

// FUNCTION: TIE95 0x213F0
void fediskio_FreeFlightHandles(void) {
	uint16_t i;
	uint16_t j;

	if (musicenabled && TieMusicPolicy_UsesImuse())
		xmemhdl_Unlock_Handle(musichandle);
#ifndef TIE_MODERN
	xmemhdl_Unlock_Handle(stringdatahandle);
#endif
	xmemhdl_Unlock_Handle(font1handle);
	xmemhdl_Unlock_Handle(font2handle);
	xmemhdl_Unlock_Handle(log1handle);
	xmemhdl_Unlock_Handle(log2handle);
	xmemhdl_Unlock_Handle(replaybufferhandle);

	if (musicenabled && TieMusicPolicy_UsesImuse()) {
		xmemhdl_Free_Handle(musichandle);
		fmusic_freemusic();
	}
	fsfx_freesfx();

#ifdef TIE_MODERN
	TieStringTable_Clear();
	/* PORT: the relocated STRINGS.DAT cells were released; later frontend
	 * fatal errors use the built-in messages instead of freed storage. */
	fatalerrstrings = fatalerrstr;
#else
	xmemhdl_Free_Handle(stringdatahandle);
#endif
	xmemhdl_Free_Handle(font1handle);
	xmemhdl_Free_Handle(font2handle);
	xmemhdl_Free_Handle(log1handle);
	xmemhdl_Free_Handle(log2handle);
	xmemhdl_Free_Handle(flightbuf_small_handle);
	xmemhdl_Free_Handle(flightbuf_big_handle);
#ifdef TIE_MODERN
	/* PORT: clear the released handles so a later release is a no-op. */
	flightbuf_small_handle = LANDRU_NULL_HANDLE;
	flightbuf_big_handle = LANDRU_NULL_HANDLE;
#endif
	xmemhdl_Free_Handle(panelpartshandle);
	TiePanelViewBuffers_FreeAll();
	xmemhdl_Free_Handle(maproomiconshandle);
	xmemhdl_Free_Handle(rundiffhandle);
	xmemhdl_Free_Handle(replaybufferhandle);
	xmemhdl_Free_Handle(messageloghandle);

	/* Free each species handle once; aliases held by earlier entries
	 * were already released. */
	for (i = 0; i < NUM_SPECIES; i++) {
		if (species_table[i].model_handle) {
			for (j = 0; j < i; j++) {
				if (species_table[j].model_handle == species_table[i].model_handle)
					break;
			}
			if (j >= i)
				xmemhdl_Free_Handle(species_table[i].model_handle);
		}
	}
	for (i = 0; i < NUM_SPECIES; i++)
		species_table[i].model_handle = LANDRU_NULL_HANDLE;
}

/* String-table consumers: declarations live in their owning headers
 * (goals.h / help.h / maproom.h / option.h / wingman.h, included above;
 * tie.h provides viewfilmstr). Local-only references below need no
 * extra forward declarations here. */

// FUNCTION: TIE95 0x215B0
// FUNCTION: TIE98 0x41B300
void fediskio_loadstringdata(int read_file) {
	int i;
	char** base_pp;

#ifdef TIE_MODERN
	if (read_file) {
		fediskio_readfiletofarmemory(TIE_FILE_ROOT_FLIGHT_ASSET, "strings.dat", TieStringTable_Data());
		base_pp = TieStringTable_Resolve();
	} else {
		base_pp = TieStringTable_Current();
	}
#else
	base_pp = (char**)xmemhdl_Lock_Handle(stringdatahandle);
	if (read_file) {
		fediskio_readfiletofarmemory(TIE_FILE_ROOT_FLIGHT_ASSET, "strings.dat", base_pp);
		for (i = 0; ((uint32_t*)base_pp)[i]; ++i)
			base_pp[i] = (char*)base_pp + ((uint32_t*)base_pp)[i];
	} else {
		for (i = 0; base_pp[i]; ++i)
			base_pp[i] = (char*)base_pp + (base_pp[i] - stringdata_base);
	}
#endif

	systemstrings = base_pp;          /* base + 0    */
	fatalerrstrings = base_pp + 10;   /* base + 40   */
	flightloadstrings = base_pp + 12; /* base + 48   */
	condstrings = base_pp + 28;       /* base + 112  */
	condverbstrings = base_pp + 49;   /* base + 196  */

	percentstrings = (void**)(base_pp + 69);
	goaloperatorstrings = (void**)(base_pp + 85);
	goaltitlestrings = (void**)(base_pp + 87);
	gatelevelstr = base_pp[22];
	gateremainstr = base_pp[23];
	gatepassedstr = base_pp[24];
	targetshitstr = base_pp[25];
	scorestr = base_pp[26];
	goalescapestr = base_pp[27];
	goal_of_string = base_pp[96];
	goal_ofall_string = base_pp[97];
	goalskillstrings = (void**)(base_pp + 103);
	goal_group_string = base_pp[98];
	goalaistrings = (void**)(base_pp + 109);
	goal_allbut_string = base_pp[99];
	goalsidestrings = (void**)(base_pp + 139);
	goal_and_string = base_pp[100];
	goalfamilystrings = (void**)(base_pp + 142);
	goal_comma_string = base_pp[101];
	goalgenusstrings = (void**)(base_pp + 149);
	goalallfgstring = base_pp[102];
	helpkeystrings = base_pp + 165;
	helpscreenstrings = base_pp + 213;
	NHIstatusstrings = (const char**)(base_pp + 261);
	maproomhelpstrings = (const char**)(base_pp + 267);
	messagetable = base_pp + 270;
	optionstrings = base_pp + 485;
	settingstrings = base_pp + 499;
	hostilestr = base_pp[264];
	imperialstr = base_pp[265];
	neutralstr = base_pp[266];
	diststring = base_pp[514];
	shieldstring = base_pp[515];
	hullstring = base_pp[516];
	sysstring = base_pp[517];
	targetstring = base_pp[518];
	nonestring = base_pp[519];
	ourstring = base_pp[520];
	currentorderstring = base_pp[521];
	notargetstring = base_pp[522];
	curtargetstring = base_pp[523];
	curdeststring = base_pp[524];
	distfromtargetstring = base_pp[525];
	waypointstrings = base_pp + 530;
	disttodeststring = base_pp[526];
	componentnames = (void**)(base_pp + 544);
	timeremstring = base_pp[527];
	statusstrings = (void**)(base_pp + 577);
	timetotargetstring = base_pp[528];
	warheadstrings = (void**)(base_pp + 586);
	timetodeststring = base_pp[529];
	unknownstring = base_pp[598];
	buoystr = (void**)(base_pp + 599);

	/* Retail cells 615..683 name the 69 species; the following cells
	 * contain the film label and the wingman command table. */
	for (i = 0; i < NUM_SPEC_DATA; i++) {
#ifdef TIE_MODERN
		spec_name_ptrs[i] = base_pp[615 + i];
#else
		spec_data[i].name_ptr = base_pp[615 + i];
#endif
	}
	viewfilmstr = base_pp[684];
	wingmanstrings = (const char**)(base_pp + 685);
#ifndef TIE_MODERN
	stringdata_base = (char*)base_pp;
#endif
}

// FUNCTION: TIE95 0x218D8
// FUNCTION: TIE98 0x41BA70
void fediskio_loadspecies(void) {
	/* Load ship species data from 3 LFD files.
	 * For each file: read directory, match against species_table entries,
	 * allocate per-species buffers, call fillinspec for hardpoint setup. */
	char path[64];
	uint8_t lfd_header[16];
	int lfd_idx, entry_idx;
	uint16_t i;

	if (TIE_FLIGHT_TIE98 && !g_useHardware3D && g_flight16bppBytesPerPixel == 1) {
		/* TIE98 8-bpp flight: load the mission's .inv table, then NEWPAL.INV,
		 * else build it. The mission extension is swapped in place. */
		size_t name_length = strlen(missionfilename);
		int loaded = 0;

		g_inversePaletteTable = tie98_flight_inverse_palette;
		if (name_length >= 3) {
			char saved_extension[3];

			memcpy(saved_extension, missionfilename + name_length - 3, 3);
			memcpy(missionfilename + name_length - 3, "inv", 3);
			if (fediskio_tryopenfile(TIE_FILE_ROOT_FLIGHT_ASSET, missionfilename, "rb", 0)) {
#ifdef TIE_MODERN
				/* PORT: accept only a complete table; the original read the file
				 * unchecked and wrote a rebuilt table back beside the mission. */
				loaded = TieStorage_FileLength(fileptr) == (int32_t)sizeof tie98_flight_inverse_palette;
#else
				loaded = 1;
#endif
				fediskio_tryclosefile(0);
				if (loaded)
					fediskio_readfiletofarmemory(TIE_FILE_ROOT_FLIGHT_ASSET, missionfilename,
												 g_inversePaletteTable);
			}
			memcpy(missionfilename + name_length - 3, saved_extension, 3);
		}
		if (!loaded && fediskio_tryopenfile(TIE_FILE_ROOT_FLIGHT_ASSET, "newpal.inv", "rb", 0)) {
#ifdef TIE_MODERN
			loaded = TieStorage_FileLength(fileptr) == (int32_t)sizeof tie98_flight_inverse_palette;
#else
			loaded = 1;
#endif
			fediskio_tryclosefile(0);
			if (loaded)
				fediskio_readfiletofarmemory(TIE_FILE_ROOT_FLIGHT_ASSET, "newpal.inv", g_inversePaletteTable);
		}
		if (!loaded)
			Color_BuildRgb565ToPaletteIndexTable(tie98_flight_inverse_palette, 0x40, 0x100);
		RenderTexture_ResetSoftwareShadeTableCache();
	}

	if (TIE_FLIGHT_TIE98) {
		char error[768];
		TieFlightModelApi models = TieFlightAssets_ModelApi();
		if (!models.begin_generation(models.context, error, sizeof error))
			shell_programexit(error);
	}
	if (TIE_FLIGHT_TIE98)
		g_hardwarePixelFormatAvailable = 1;

	for (i = 0; i < NUM_SPECIES; i++)
		species_table[i].model_handle = LANDRU_NULL_HANDLE;

	for (lfd_idx = 0; lfd_idx < 3; lfd_idx++) {
		/* Retail FEDISKIO_loadspecies selects its LFD directory based
		 * on flightResolution: "RES640/" for 640x480 (257) and "RES320/"
		 * for anything else (typically 19 = 320x200). This differs from
		 * the default `resourcedir` ("RESOURCE/") used by the rest of
		 * the disk I/O surface. */
		const char* species_dir =
			(flightResolution == TIE_FLIGHT_RES_SVGA || flightResolution == TIE_FLIGHT_RES_SVGA_16 ||
			 flightResolution == TIE_FLIGHT_RES_SVGA_D3D)
				? "RES640/"
				: "RES320/";
		uint32_t dir_size;
		int num_entries;
		int file_offset;

		snprintf(path, sizeof(path), "%s%s.lfd", species_dir, specieslfds[lfd_idx]);

		fediskio_tryopenfile(TIE_FILE_ROOT_FLIGHT_ASSET, path, "rb", 1);

		/* Read LFD file header (16 bytes) */
		TieStorage_Read(lfd_header, 1, 16, fileptr);

		/* Read the directory into loadbuffer (the locked log2handle). */
		/* LFD sub-header layout: type[4] + name[8] + size[4]. The size
		 * dword lives at offset +12, not +8. Every other LFD parser in
		 * the codebase (fsfx, fmusic, landru/res)
		 * reads it from +12 — this one was wrong. */
		dir_size = br_u32le(lfd_header + 12);
		fediskio_readfileblock(loadbuffer, dir_size, 1, fileptr);

		/* Number of directory entries (16 bytes each) */
		num_entries = (uint16_t)dir_size >> 4;
		file_offset = 0;

		for (entry_idx = 0; entry_idx < num_entries; entry_idx++) {
			int found;
			int entry_flags;
			uint32_t* dir_entry;
			uint32_t entry_size;

			file_offset += 16;

			/* Check if any species entry references this LFD file + entry */
			found = 0;
			entry_flags = 0;

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
			dir_entry = (uint32_t*)((uint8_t*)loadbuffer + 16 * entry_idx);
			entry_size = dir_entry[3];

			if (found) {
				LandruHandle species_handle;
				void* species_buf;
				uint32_t rgb_v39, rgb_v38;

				if (TIE_DISPLAY_DX5) {
					FrontendDisplay_BlitOffscreenToRenderSurface();
					FrontendDisplay_PresentFrame();
				}

				species_handle = LANDRU_NULL_HANDLE;
				species_buf = NULL;
				rgb_v39 = 0;
				rgb_v38 = 0;

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
					bool tie98_16bpp;
					size_t palette_entry_size;

					if (file_offset) {
						TieStorage_Seek(fileptr, file_offset, TIE_SEEK_CUR);
						file_offset = 0;
					}
					TieStorage_Read(&rgb_v39, 4, 1, fileptr);
					TieStorage_Read(&rgb_v38, 4, 1, fileptr);
					entry_size -= 8;

					tie98_16bpp = TIE_FLIGHT_TIE98 && g_flight16bppBytesPerPixel == 2;
					palette_entry_size = tie98_16bpp ? 2u : (size_t)bytesPerPixel;
					fediskio_UnlockGlobals();
					species_handle = xmemhdl_Alloc_Handle((uint32_t)(rgb_v39 + palette_entry_size * rgb_v38),
														  LANDRU_MEMORY_DEFAULT);
					fediskio_RelockGlobals();
					if (!species_handle)
						fediskio_fatalerror(FATAL_ERROR_NOT_ENOUGH_MEMORY_X0A);
					species_buf = xmemhdl_Lock_Handle(species_handle);

					((uint32_t*)species_buf)[0] = rgb_v39;
					((uint32_t*)species_buf)[1] = rgb_v38;
					fediskio_readfileblock((uint8_t*)species_buf + 8, entry_size, 1, fileptr);
					if (tie98_16bpp)
						rtsvga2_remapRGBImage_tie98((uint32_t*)species_buf);
					else
						rtsvga2_remapRGBImage((uint32_t*)species_buf);
				}

				if (!species_buf) {
					fediskio_UnlockGlobals();
					species_handle = xmemhdl_Alloc_Handle(entry_size, LANDRU_MEMORY_DEFAULT);
					fediskio_RelockGlobals();
					if (!species_handle)
						fediskio_fatalerror(FATAL_ERROR_NOT_ENOUGH_MEMORY_X0A);
					species_buf = xmemhdl_Lock_Handle(species_handle);

					if (file_offset) {
						TieStorage_Seek(fileptr, file_offset, TIE_SEEK_CUR);
						file_offset = 0;
					}
					fediskio_readfileblock(species_buf, entry_size, 1, fileptr);
				}

				/* Entries naming the same LFD resource share one handle. */
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

					species_table[i].model_handle = species_handle;

					if (entry_flags & 1) {
						if (TIE_FLIGHT_TIE98)
							fediskio_fillinspec_tie98(species_table[i].spec_num, (uint8_t)i);
						else
							fediskio_fillinspec(species_buf, species_table[i].spec_num, (uint8_t)i);
					}
				}
				xmemhdl_Unlock_Handle(species_handle);
				if (TIE_DISPLAY_DX5) {
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
	if (TIE_FLIGHT_TIE98)
		modelmesh_buildobjecttypemeshcache();
	if (TIE_FLIGHT_TIE98) {
		uint8_t deep_space_index;

		const uint8_t deep_space_rgb[3] = { 0, 0, 2 };
		g_hardwarePixelFormatAvailable = 0;
		deep_space_index = (uint8_t)rtsvga2_findNearestColor(deep_space_rgb, rtsvga2_vgapalette, 0, 256);
		g_flightColorKeyIndex = deep_space_index;
		deepspacecolor = deep_space_index;
	}

#ifdef TIE_MODERN
	/* Hosts refresh per-species caches when the generation changes. */
	TieRecoveredData_AdvanceMissionLoadGeneration();
#endif
}

// FUNCTION: TIE95 0x21E48
void fediskio_fillinspec(void* data, uint8_t lfd_idx, uint8_t species_idx) {
	/* Skip the 2-byte file-size prefix so struct offsets line up with
	 * retail FEDISKIO_fillinspec's `v48 = a1 + 2` convention. */
	ShipModelData* model = (ShipModelData*)((uint8_t*)data + 2);
	uint16_t half_width, half_height, half_depth;
	int16_t extra_shift, total_shift;
	ShipModelMesh* cur_mesh;
	uint8_t result;
	uint16_t i;

	/* Set species bounding half-length */
	uint16_t half_length = model->length >> 1;
	uint16_t slot;

	species_table[species_idx].bound_hwidth = half_length;
	if (model->model_scale_shift) {
		species_table[species_idx].bound_hwidth <<= (int8_t)model->model_scale_shift;
	}
	species_table[species_idx].bound_qdepth = model->length >> 2;

	/* Sentinel 0xFF means "no spec entry" -- skip every spec_data
	 * write. The retail does this by gating all `59 * a2` offsets on
	 * `a2 != 255`; we have to defer taking &spec_data[lfd_idx] until
	 * after this check, otherwise the address arithmetic is UB
	 * (spec_data has only NUM_SPECIES==69 elements). */
	if (lfd_idx == 255)
		return;

	/* Compute dimensions with LOD scaling */
	half_width = model->width >> 1;
	half_depth = model->depth >> 1;
	half_height = model->height >> 1;
	cur_mesh = (ShipModelMesh*)&model->lod_records[model->num_lods];
	extra_shift = 0;

	while (half_width > 640 || half_height > 640 || half_depth > 640) {
		half_width >>= 1;
		half_height >>= 1;
		half_depth >>= 1;
		extra_shift++;
	}

	total_shift = extra_shift + (int8_t)model->model_scale_shift;
	spec_data[lfd_idx].model_scale_shift = total_shift;
	/* Retail writes (model+8)>>1 to spec+0xE8 and (model+6)>>1 to spec+0xEA
	 * (FEDISKIO_fillinspec: v9 and i). We currently label spec+0xE8
	 * bound_height and spec+0xEA bound_depth, and ShipModelData+6/+8
	 * height/depth -- so to stay byte-faithful the values must cross over
	 * here rather than copy straight through. Most likely one of those name
	 * pairs is mislabeled (the field at +0xE8 is not really "height"); the
	 * physical axis identities are unverified. The cross-assignment below is
	 * what matters; revisit the names once the axes are pinned down. */
	spec_data[lfd_idx].bound_width = half_width;
	spec_data[lfd_idx].bound_depth = half_height;
	spec_data[lfd_idx].bound_height = half_depth;

	/* Default active-dock ranges from shields and passive ranges from speed. */
	if (!spec_data[lfd_idx].dock_active_light) {
		spec_data[lfd_idx].dock_active_light = model->shield_default >> 17;
		spec_data[lfd_idx].dock_active_heavy = model->shield_default >> 17;
	}
	if (!spec_data[lfd_idx].dock_passive_light) {
		spec_data[lfd_idx].dock_passive_light = model->speed_default >> 17;
		spec_data[lfd_idx].dock_passive_heavy = model->speed_default >> 17;
	}

	/* Walk mesh components, extract hardpoints and reference points */
	for (i = 0; i < model->num_meshes; i++) {
		if (cur_mesh->num_hardpoints) {
			ShipModelHardpoint* hp = (ShipModelHardpoint*)((uint8_t*)cur_mesh + cur_mesh->hardpoint_offset);

			uint16_t hp_scan;

			for (hp_scan = 0; hp_scan < cur_mesh->num_hardpoints; hp_scan++, hp++) {
				int16_t matched = 0;

				switch (hp->type) {
					case 0x1C:
						spec_data[lfd_idx].dock_passive_light = hp->local_y >> 1;
						spec_data[lfd_idx].dock_fwd = hp->local_z >> 1;
						matched = 1;
						break;
					case 0x1E:
						spec_data[lfd_idx].dock_active_light = hp->local_y >> 1;
						spec_data[lfd_idx].dock_fwd = hp->local_z >> 1;
						matched = 1;
						break;
					case 0x1B:
						spec_data[lfd_idx].dock_passive_heavy = hp->local_y >> 1;
						spec_data[lfd_idx].dock_fwd = hp->local_z >> 1;
						matched = 1;
						break;
					case 0x1D:
						spec_data[lfd_idx].dock_active_heavy = hp->local_y >> 1;
						spec_data[lfd_idx].dock_fwd = hp->local_z >> 1;
						matched = 1;
						break;
					case 0x19: /* cockpit position: v0=X, v1=Y, v2=Z */
						spec_data[lfd_idx].cockpit_x = hp->local_x >> 1;
						spec_data[lfd_idx].cockpit_y = hp->local_y >> 1;
						spec_data[lfd_idx].cockpit_z = hp->local_z >> 1;
						matched = 1;
						break;
					case 0x1A: /* engine position: v0=X, v1=Y, v2=Z */
						spec_data[lfd_idx].engine_x = hp->local_x >> 1;
						spec_data[lfd_idx].engine_y = hp->local_y >> 1;
						spec_data[lfd_idx].engine_z = hp->local_z >> 1;
						matched = 1;
						break;
					case 0x1F: /* turret position */
						spec_data[lfd_idx].gun_muzzle_up = hp->local_y >> 1;
						spec_data[lfd_idx].gun_muzzle_fwd = hp->local_z >> 1;
						matched = 1;
						break;
				}

				if (!matched) {
					/* Weapon hardpoint — classify type.
					 * weapon_id MUST stay uint8_t: hp->type is in [1..18]
					 * for real weapons, so (hp->type + 136) is always
					 * negative. A signed char here would sign-extend in the
					 * dedup compares below while the uint8_t array entries
					 * zero-extend, so no two equal weapon ids ever match. */
					uint16_t scan;
					uint8_t weapon_id = hp->type + 136;

					/* Check if this weapon type already has a laser slot */
					for (scan = 0; scan < 2; scan++)
						if (weapon_id == spec_data[lfd_idx].laser_type[scan])
							break;
					if (scan >= 2) {
						/* Check missile slots */
						for (scan = 0; scan < 2; scan++)
							if (weapon_id == spec_data[lfd_idx].missile_type[scan])
								break;
						if (scan >= 2) {
							int ws_type = weaponsystype[hp->type];
							if (ws_type == 1) {
								/* Laser — find free slot */
								uint16_t free_slot;
								for (free_slot = 0; free_slot < 2; free_slot++)
									if (!spec_data[lfd_idx].laser_type[free_slot])
										break;
								if (free_slot < 2) {
									spec_data[lfd_idx].laser_type[free_slot] = weapon_id;
									if (cur_mesh->mesh_type == 4 || cur_mesh->mesh_type == 21 ||
										cur_mesh->mesh_type == 5 ||
										species_table[species_idx].ship_class == 5 ||
										species_table[species_idx].ship_class == 4) {
										spec_data[lfd_idx].laser_fire_mode[free_slot] = 2;
									} else {
										if (hp->type == 5 || hp->type == 16)
											spec_data[lfd_idx].laser_fire_mode[free_slot] = 1;
										else
											spec_data[lfd_idx].laser_fire_mode[free_slot] = 0;
									}
								}
							} else if (ws_type == 2) {
								/* Missile — find free slot */
								uint16_t free_slot;
								for (free_slot = 0; free_slot < 2; free_slot++)
									if (!spec_data[lfd_idx].missile_type[free_slot])
										break;
								if (free_slot < 2)
									spec_data[lfd_idx].missile_type[free_slot] = weapon_id;
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
	for (slot = 0; slot < 2; slot++) {
		ShipModelMesh* mesh;
		uint8_t target_id;
		uint8_t start_hp;
		uint16_t mi;

		if (result == 16) {
			spec_data[lfd_idx].laser_type[slot] = 0;
			spec_data[lfd_idx].laser_fire_mode[slot] = 0;
			continue;
		}

		target_id = spec_data[lfd_idx].laser_type[slot] - 136;
		mesh = (ShipModelMesh*)&model->lod_records[model->num_lods];
		start_hp = result;

		for (mi = 0; mi < model->num_meshes; mi++) {
			if (mesh->num_hardpoints) {
				uint16_t paired = 255;
				ShipModelHardpoint* hp = (ShipModelHardpoint*)((uint8_t*)mesh + mesh->hardpoint_offset);

				uint16_t hp_idx;

				for (hp_idx = 0; mesh->num_hardpoints > hp_idx; hp_idx++, hp++) {
					if (target_id != hp->type)
						continue;
					if (paired == 255) {
						/* Weapon hardpoint coords: v0=X, v1=Y, v2=Z. */
						spec_data[lfd_idx].hp[result].x = hp->local_x >> 1;
						spec_data[lfd_idx].hp[result].z = hp->local_z >> 1;
						spec_data[lfd_idx].hp[result].y = hp->local_y >> 1;
						spec_data[lfd_idx].hp[result].component = mi;
						spec_data[lfd_idx].hp[result].link = -1;
						if (mesh->mesh_type == 4 || mesh->mesh_type == 21)
							paired = result;
						if (++result == 16)
							break;
					} else {
						spec_data[lfd_idx].hp[paired].link = hp->link;
						paired = 255;
					}
				}
				if (result == 16)
					break;
			}
			mesh++;
		}

		if (result != start_hp) {
			spec_data[lfd_idx].laser_start[slot] = start_hp;
			spec_data[lfd_idx].laser_end[slot] = result - 1;
			spec_data[lfd_idx].laser_count[slot] = result - start_hp;
		}
	}

	/* Build missile/warhead hardpoint position tables */
	for (slot = 0; slot < 2; slot++) {
		ShipModelMesh* mesh;
		uint8_t target_id;
		uint8_t start_hp;
		uint16_t mi;

		if (result == 16) {
			spec_data[lfd_idx].missile_type[slot] = 0;
			continue;
		}

		target_id = spec_data[lfd_idx].missile_type[slot] - 136;
		mesh = (ShipModelMesh*)&model->lod_records[model->num_lods];
		start_hp = result;

		for (mi = 0; mi < model->num_meshes; mi++) {
			if (mesh->num_hardpoints) {
				ShipModelHardpoint* hp = (ShipModelHardpoint*)((uint8_t*)mesh + mesh->hardpoint_offset);

				uint16_t hp_idx;

				for (hp_idx = 0; hp_idx < mesh->num_hardpoints; hp_idx++, hp++) {
					if (hp->type != target_id)
						continue;
					/* Missile hardpoint coords: v0=X, v1=Y, v2=Z. */
					spec_data[lfd_idx].hp[result].x = hp->local_x >> 1;
					spec_data[lfd_idx].hp[result].z = hp->local_z >> 1;
					spec_data[lfd_idx].hp[result].y = hp->local_y >> 1;
					spec_data[lfd_idx].hp[result].component = mi;
					if (++result == 16)
						break;
				}
				if (result == 16)
					break;
			}
			mesh++;
		}

		if (result != start_hp) {
			spec_data[lfd_idx].missile_start[slot] = start_hp;
			spec_data[lfd_idx].missile_end[slot] = result - 1;
			spec_data[lfd_idx].missile_count[slot] = result - start_hp;
		}
	}
}

/* --- File I/O wrappers --- */

// FUNCTION: TIE95 0x226D0
// FUNCTION: TIE98 0x41C5F0
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

	if (TIE_DISPLAY_DX5) {
		FlightSurface_Lock();
		g_flightDrawToOffscreenSurface = 0;
	}
	colorcycleuserflag = 1;
	festring_setfontsize(1);
	if (TIE_DISPLAY_DX5) {
		saved_box = (uint8_t*)newbuf;
		saved_box += (screenYRes - 4 * fontheight - 1) * g_surfacePitch;
	} else {
		saved_box = (uint8_t*)newbuf + screenXRes * screenYRes * bytesPerPixel;
		saved_box -= (4 * fontheight + 1) * screenXRes * bytesPerPixel;
	}
	if (TIE_DISPLAY_DX5)
		rtsvga2_saveboxVGA_tie98(saved_box, 0, (uint16_t)((screenYRes >> 1) - 2 * fontheight),
								 (uint16_t)screenXRes, (uint16_t)(4 * fontheight + 1));
	else
		rtsvga2_saveboxVGA(saved_box, 0, (uint16_t)((screenYRes >> 1) - 2 * fontheight), (uint16_t)screenXRes,
						   (uint16_t)(4 * fontheight + 1));
	festring_setbound(screenXRes >> 4, (screenYRes >> 1) - 2 * fontheight, screenXRes - (screenXRes >> 4),
					  (screenYRes >> 1) + 2 * fontheight);
	backcolor = 0xF9;
	clearwindow();
	festring_setbound((screenXRes >> 4) + 1, (screenYRes >> 1) - 2 * fontheight + 1,
					  screenXRes - (screenXRes >> 4) - 1, (screenYRes >> 1) + 2 * fontheight - 1);
	backcolor = 0;
	clearwindow();
	textcolor = 0xF9;
	dropcolor = 0;
	dropflag = 0;
	festring_setcursor(0, (screenYRes >> 1) - fontheight - 2);
	festring_outstringcenter((const uint8_t*)flightloadstrings[5]);
	festring_setcursor(0, (screenYRes >> 1) + 2);
	festring_outstringcenter((const uint8_t*)flightloadstrings[6]);
	if (TIE_DISPLAY_DX5) {
		FlightSurface_Unlock();
		g_flightDrawToOffscreenSurface = 1;
		FrontendDisplay_PresentFrame();
		response = FlightInput_GetChar();
		FrontendDisplay_PresentFrame();
	} else {
#ifdef TIE_MODERN
		response = (int8_t)TieInput_ReadKey();
#else
		response = (int8_t)getch();
#endif
	}
	colorcycleuserflag = 0;
	if (TIE_DISPLAY_DX5)
		rtsvga2_restoreboxVGA_tie98(saved_box, 0, (uint16_t)((screenYRes >> 1) - 2 * fontheight),
									(uint16_t)screenXRes, (uint16_t)(4 * fontheight + 1));
	else
		rtsvga2_restoreboxVGA(saved_box, 0, (uint16_t)((screenYRes >> 1) - 2 * fontheight),
							  (uint16_t)screenXRes, (uint16_t)(4 * fontheight + 1));
	if (TIE_DISPLAY_DX5)
		memset(newbuf, 0x40, (size_t)screenXRes * screenYRes * g_flight16bppBytesPerPixel);
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
	return response;
}

// FUNCTION: TIE95 0x229CC
// FUNCTION: TIE98 0x41C910
int16_t fediskio_tryopenfile(TieFileRoot root, const char* name, const char* mode, int16_t fatal) {
	int16_t attempt_count = TIE_FLIGHT_EDITION(4, 2);

	int16_t attempt;

	strcpy(openfilename, name);
	TieStorage_SetOpenFileRoot(root);
	/* MODERN ADAPTATION: the VFS root replaces TIE98's final
	 * install-drive pathname attempt. Removable-media retries are obsolete. */
	for (attempt = 0; attempt < attempt_count; ++attempt) {
		fileptr = TieStorage_Open(root, name, mode);
		if (fileptr)
			return 1;
	}
	if (fatal)
		fediskio_fatalerror(FATAL_ERROR_THE_FOLLOWING_FILE_IS_MISSING_);
	return 0;
}

// FUNCTION: TIE95 0x22BE4
int16_t fediskio_tryclosefile(int16_t delete_on_error) {
	int16_t had_error = 0;

	/* MODERN ADAPTATION: the original also failed on ferror(fileptr) and then
	 * skipped fclose. The storage close reports pending write errors itself. */
	if (TieStorage_Close(fileptr) == TIE_EOF)
		had_error = 1;

	if (delete_on_error && had_error)
		TieStorage_RemoveOpenFile(openfilename);

	return had_error;
}

// FUNCTION: TIE95 0x22C24
int16_t fediskio_readfileblock(void* buf, unsigned int size, unsigned int count, TieFile* fp) {
	int tries = 15;
	unsigned int requested = count;

	for (;;) {
		int8_t response;

		do {
			unsigned int got = (unsigned int)TieStorage_Read(buf, size, count, fp);
			buf = (uint8_t*)buf + size * got;
			count -= got;
			tries--;
		} while (count && tries);
		if (!count)
			break;
		while (count) {
			response = fediskio_displayerror();
			if (response == 'R' || response == 'r') {
				tries = 5;
				break;
			} else if (response == 'F' || response == 'f') {
				fileerror = 1;
				fediskio_fatalerror(FATAL_ERROR_THE_FOLLOWING_FILE_IS_MISSING_);
				return 0;
			}
		}
	}
	fileerror = 0;
	return (int16_t)requested;
}

// FUNCTION: TIE95 0x22D38
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

// FUNCTION: TIE98 0x41BE70
// FEDISKIO_fillinspec
// PORT: writes the recovered TIE95 runtime SpecData layout from OPT metadata.
void fediskio_fillinspec_tie98(uint8_t spec_index, uint8_t model_type) {
	int extent;
	SpecData* spec;
	int width;
	int depth;
	int height;
	int shift;
	int mesh_count;
	int mesh;
	uint8_t result;

	int slot;

	modelmesh_require_craft_capacity(model_type);
	extent = modelbounds_getmaxextent(model_type);
	species_table[model_type].bound_hwidth = extent;
	species_table[model_type].bound_qdepth = extent >> 1;
	if (spec_index == 255)
		return;

	spec = &spec_data[spec_index];
	width = modelbounds_getsizex(model_type);
	depth = modelbounds_getsizey(model_type);
	height = modelbounds_getsizez(model_type);
	shift = 0;
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

	mesh_count = modelmesh_getcount(model_type);
	for (mesh = 0; mesh < mesh_count; ++mesh) {
		const int mesh_type = modelmesh_gettype(model_type, mesh);
		const int hardpoint_count = modelmesh_counthardpoints(model_type, mesh);
		int hardpoint;

		for (hardpoint = 0; hardpoint < hardpoint_count; ++hardpoint) {
			int type, x, y, z;
			int special_hardpoint;
			uint8_t weapon_type;
			int known_weapon_type;
			uint8_t weapon_class;

			int slot;

			modelmesh_gethardpoint(model_type, mesh, hardpoint, &type, &x, &y, &z);
			special_hardpoint = 1;
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

			weapon_type = (uint8_t)(type - 120);
			known_weapon_type = 0;
			for (slot = 0; slot < 2; ++slot) {
				if (spec->laser_type[slot] == weapon_type || spec->missile_type[slot] == weapon_type) {
					known_weapon_type = 1;
					break;
				}
			}
			if (known_weapon_type)
				continue;

#ifdef TIE_MODERN
			/* PORT: retail reads the zero padding and strings after the
			 * 33-entry table for unknown OPT types; none classify as 1 or 2. */
			weapon_class = (unsigned int)type < sizeof weaponsystype ? weaponsystype[type] : 0;
#else
			weapon_class = weaponsystype[type];
#endif
			if (weapon_class == 1) {
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
			} else if (weapon_class == 2) {
				int slot;
				for (slot = 0; slot < 2 && spec->missile_type[slot]; ++slot)
					;
				if (slot < 2)
					spec->missile_type[slot] = weapon_type;
			}
		}
	}

	/* Build laser hardpoint position tables (up to 16 total). */
	result = 0;
	for (slot = 0; slot < 2; ++slot) {
		int target_type;
		uint8_t start;

		if (result == 16) {
			spec->laser_type[slot] = 0;
			spec->laser_fire_mode[slot] = 0;
			continue;
		}

		target_type = (uint8_t)(spec->laser_type[slot] + 120);
		start = result;
		for (mesh = 0; mesh < mesh_count; ++mesh) {
			const int hardpoint_count = modelmesh_counthardpoints(model_type, mesh);
			int mesh_type;
			int paired;
			int hardpoint;

			if (!hardpoint_count)
				continue;
			mesh_type = modelmesh_gettype(model_type, mesh);
			paired = 255;
			for (hardpoint = 0; hardpoint < hardpoint_count; ++hardpoint) {
				int type, x, y, z;

				modelmesh_gethardpoint(model_type, mesh, hardpoint, &type, &x, &y, &z);
				if (type != target_type)
					continue;
				if (paired == 255) {
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
					if (mesh_type == TIE_MESH_GUN_TURRET || mesh_type == TIE_MESH_ROTARY_GUN_TURRET)
						paired = result;
					if (++result == 16)
						break;
				} else {
					spec->hp[paired].link = modelmesh_getalternatehardpointindex(model_type, mesh, hardpoint);
					paired = 255;
				}
			}
			if (result == 16)
				break;
		}

		if (result != start) {
			spec->laser_start[slot] = start;
			spec->laser_end[slot] = result - 1;
			spec->laser_count[slot] = result - start;
		}
	}

	/* Build missile/warhead hardpoint position tables. */
	for (slot = 0; slot < 2; ++slot) {
		int target_type;
		uint8_t start;

		if (result == 16) {
			spec->missile_type[slot] = 0;
			continue;
		}

		target_type = (uint8_t)(spec->missile_type[slot] + 120);
		start = result;
		for (mesh = 0; mesh < mesh_count; ++mesh) {
			const int hardpoint_count = modelmesh_counthardpoints(model_type, mesh);
			int hardpoint;

			if (!hardpoint_count)
				continue;
			for (hardpoint = 0; hardpoint < hardpoint_count; ++hardpoint) {
				int type, x, y, z;

				modelmesh_gethardpoint(model_type, mesh, hardpoint, &type, &x, &y, &z);
				if (type != target_type)
					continue;
				if (model_type == 53) {
					x /= 2;
					y /= 2;
					z /= 2;
				}
				spec->hp[result].x = x;
				spec->hp[result].y = z;
				spec->hp[result].z = y;
				spec->hp[result].component = mesh;
				spec->hp[result].link = -1;
				if (++result == 16)
					break;
			}
			if (result == 16)
				break;
		}

		if (result != start) {
			spec->missile_start[slot] = start;
			spec->missile_end[slot] = result - 1;
			spec->missile_count[slot] = result - start;
		}
	}
}

// FUNCTION: TIE95 0x22D60
void fediskio_fatalerror(uint16_t error_code) {
	char str[128];
	const char* message;
	uint16_t i;

	message = fatalerrstrings[error_code];
	for (i = 0; i < 128; i++) {
		str[i] = message[i];
		if (!str[i])
			break;
	}

	if (error_code == FATAL_ERROR_THE_FOLLOWING_FILE_IS_MISSING_) {
		uint16_t j = 0;
		while (i < 128) {
			str[i] = openfilename[j++];
			if (!str[i])
				break;
			i++;
		}
		str[i++] = '\n';
		str[i] = '\0';
	}

	shell_programexit(str);
}
