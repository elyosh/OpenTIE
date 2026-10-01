#include "tie/fsfx.h"
#include "tie_runtime/audio/imuse_api.h"
#include "tie_runtime/audio/imuse_session.h"

#include "tie/collide.h"
#include "tie/create.h"
#include "tie/fediskio.h" /* resourcedir */
#include "tie/frontend_sound_tie98.h"
#include "tie/math2.h"
#include "tie/mission.h"
#include "tie/score.h"
#include "tie/shipext.h" /* MissionFile / mission_file_header (.win_msg1 etc.) */
#include "tie/tie.h"
#include "tie/transfm2.h"
#include "tie/trig2.h"
#include "tie_runtime/audio/config.h"
#include "tie_runtime/audio/flight_sound_bank.h"
#include "tie_runtime/diagnostics/diagnostics.h"
#include "tie_runtime/display/classic_display.h"
#include "tie_runtime/display/classic_framebuffer.h"
#include "tie_runtime/flight_assets/model_types.h"
#include "tie_runtime/input/input.h"
#include "tie_runtime/runtime/exports.h"
#include "tie_runtime/runtime/inflight_state.h"
#include "tie_runtime/runtime/profile.h"
#include "tie_runtime/storage/storage.h"

#include <imuse/hilevel.h>
#include <imuse/lolevel.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* --------------------------------------------------------------------------
 * FSFX-owned module globals.
 * -------------------------------------------------------------------------- */

/* soundhandles[0..3] are unused (the binary reserves them for the music
 * bank reused in other builds). [4..50] SFX, [51..106] voice, and
 * [107..] follows the edition-specific SFXDOE/mission-voice layout. */
// GLOBAL: TIE95 0xD49BC
void* soundhandles[FSFX_NUM_SOUND_HANDLES];
/* TIE98 per-slot sound names consumed by the name-based FrontendSound layer. */
// GLOBAL: TIE98 0x6259E0
char soundnames[FSFX_NUM_SOUND_HANDLES][FSFX_SOUND_NAME_CAPACITY];

// GLOBAL: TIE95 0xD4ABC
// GLOBAL: TIE98 0x626746
uint8_t currentdigital;

/* TIE98 recovered controller globals. The update-enable value is initialized
 * by tie_init and may be changed by the modern runtime option boundary. */
// GLOBAL: TIE98 0x5926AA
uint8_t g_playerEngineSoundUpdateEnabled;
// GLOBAL: TIE98 0x5601A0
int32_t g_engineSoundPreviousPlayerSpecies = -1;

/* Per-sound falloff radius and full-volume tables. */
// GLOBAL: TIE95 0xC5284
// GLOBAL: TIE98 0x4E37F8
uint16_t sounddist[FSFX_NUM_DIST_ENTRIES] = {
	0x0000, 0x0000, 0x0000, 0x0000,                 /*  0.. 3 unused */
	0x2000, 0x2000, 0x2000,                         /*  4.. 6 engine */
	0x2800, 0x2800, 0x2800, 0x2800, 0x2800,         /*  7..11 laser  */
	0x3000, 0x3000, 0x3000,                         /* 12..14 heavy  */
	0x2000, 0x2800, 0x2800,                         /* 15..17 misc   */
	0xC000,                                         /* 18    alert   */
	0x6000, 0x6000, 0x6000, 0x6000, 0x6000, 0x6000, /* 19..24 expl.  */
	0x2000, 0x2000, 0x2000, 0x2000, 0x2000, 0x2000, /* 25..30        */
	0x2000, 0x2000, 0x2000, 0x2000, 0x2000, 0x2000, /* 31..36        */
	0x2000, 0x2000, 0x2000, 0x2000, 0x2000, 0x2000, /* 37..42        */
	0x2000, 0x2000, 0x2000, 0x2000, 0x2000, 0x2000, /* 43..48        */
	0x2000, 0x2000,                                 /* 49..50        */
	0x2000, 0x2000, 0x9000, 0x2000,                 /* 51..54 unused */
};
// GLOBAL: TIE95 0xC52F2
// GLOBAL: TIE98 0x4E3868
uint8_t fullvolume[FSFX_NUM_DIST_ENTRIES] = {
	0x00, 0x00, 0x00, 0x00,             /*  0.. 3 unused */
	0x48, 0x48, 0x48,                   /*  4.. 6 engine */
	0x50, 0x50, 0x50, 0x50, 0x50,       /*  7..11 laser  */
	0x60, 0x60, 0x60,                   /* 12..14 heavy  */
	0x48, 0x50, 0x50,                   /* 15..17 misc   */
	0x7F,                               /* 18    alert   */
	0x68, 0x68, 0x68, 0x68, 0x68, 0x68, /* 19..24 expl.  */
	0x48, 0x70,                         /* 25..26        */
	0x7F, 0x7F, 0x7F, 0x7F,             /* 27..30 max    */
	0x70, 0x60, 0x50, 0x50, 0x50, 0x50, /* 31..36        */
	0x40, 0x70, 0x60, 0x70, 0x60, 0x70, /* 37..42        */
	0x70, 0x70, 0x70, 0x70, 0x70, 0x70, /* 43..48        */
	0x70, 0x70,                         /* 49..50        */
	0x70, 0x70, 0x70, 0x70,             /* 51..54 unused */
};

/* Per-mission voice-filename inputs (see fsfx.h). Snapshot of the
 * pilot's tour/combat cursor at mission entry; consumed by
 * fsfx_loadvoicelfd to build VOICE\<NAME>\<NAME>.LFD. */
// GLOBAL: TIE95 0xD4190
uint8_t voice_id_a;
// GLOBAL: TIE95 0xD418E
uint8_t voice_id_b;

/* "FIBAGDM" -- single-letter filename prefix for combat-sim ships in
 * mode 1. Indexed by voice_id_a (cur_combat_ship). The trailing NUL
 * keeps strlen() happy if anyone walks the table. */
// GLOBAL: TIE95 0xC5358
static const char combat_ship_voice_letters[8] = "FIBAGDM";

/* Five SFX-group prefix strings matched against EFGStruct.name by
 * fsfx_speakobjectname. Order is meaningful: the matched index feeds
 * directly into the group-voice offset (53 + index). */
// GLOBAL: TIE95 0xC5329
// GLOBAL: TIE98 0x4E38A0
static const char sfxgroupname1[] = "ALPHA";
// GLOBAL: TIE95 0xC532F
// GLOBAL: TIE98 0x4E38A8
static const char sfxgroupname2[] = "BETA";
// GLOBAL: TIE95 0xC5334
// GLOBAL: TIE98 0x4E38B0
static const char sfxgroupname3[] = "GAMMA";
// GLOBAL: TIE95 0xC533A
// GLOBAL: TIE98 0x4E38B8
static const char sfxgroupname4[] = "DELTA";
// GLOBAL: TIE95 0xC5340
// GLOBAL: TIE98 0x4E38C0
static const char sfxgroupname5[] = "MU";

// GLOBAL: TIE95 0xC5344
// GLOBAL: TIE98 0x4E38C8
const char* sfxgroupnameptrs[5] = {
	sfxgroupname1, sfxgroupname2, sfxgroupname3, sfxgroupname4, sfxgroupname5,
};

/* iMUSE parameter codes (see imuse/lolevel). Exposed here to keep the
 * FSFX call sites readable. */
enum {
	IM_PARAM_IS_PLAYING = 0x100, /* ImGetParam only */
	IM_PARAM_PRIORITY = 0x500,
	IM_PARAM_VOLUME = 0x600,
	IM_PARAM_PAN = 0x700,
};

// FUNCTION: TIE98 0x422760
void fsfx_UpdatePlayerEngineSound(void) {
	const TieFlightProfile* profile = TieProfile_Flight();
	uint16_t sound_id;
	int16_t species;
	CraftData* craft;
	int16_t quotient;
	uint32_t q16;
	uint32_t base_frequency;
	int frequency;
	int original_volume;
	int volume;
	const char* name;

	FlightObject* player;

	if (profile->version != TIE_GAME_VERSION_TIE98 || !sfxenabled || !g_playerEngineSoundUpdateEnabled)
		return;

	sound_id = UINT16_MAX;
	species = -1;
	if (pstate.object_idx != UINT16_MAX && !mapflag && pstate.object_idx < NUM_OBJECTS) {
		FlightObject* player = &objects[pstate.object_idx];
		species = player->ship_idx;
		switch (species) {
			case 5:
			case 6:
			case 7:
			case 8:
			case 9:
				sound_id = FSFX_PLAYER_ENGINE_TIE_ID;
				break;
			case 12:
			case 16:
				sound_id = FSFX_PLAYER_ENGINE_REBEL_ID;
				break;
		}
	}

	if (sound_id == UINT16_MAX) {
		uint16_t previous_id = UINT16_MAX;
		switch (g_engineSoundPreviousPlayerSpecies) {
			case 5:
			case 6:
			case 7:
			case 8:
			case 9:
				previous_id = FSFX_PLAYER_ENGINE_TIE_ID;
				break;
			case 12:
			case 16:
				previous_id = FSFX_PLAYER_ENGINE_REBEL_ID;
				break;
		}
		if (previous_id != UINT16_MAX && lolevel_ImGetParam_tie98(previous_id, 0x100) != 0)
			(void)lolevel_ImStopSound_tie98(previous_id);
		return;
	}

	g_engineSoundPreviousPlayerSpecies = species;
	player = &objects[pstate.object_idx];
	craft = player->craft_ptr;
	if (!craft || pstate.hyperin_state == 1 || !(craft->status_flags & 0x0040u)) {
		if (lolevel_ImGetParam_tie98(sound_id, 0x100) != 0)
			(void)lolevel_ImStopSound_tie98(sound_id);
		return;
	}

	quotient = math2_divide(craft->throttle_speed, UINT16_MAX);
	q16 = ((uint32_t)(uint16_t)quotient << 16) | (uint16_t)math2_remainder;
	base_frequency = sound_id == FSFX_PLAYER_ENGINE_TIE_ID ? 5500u : 11000u;
	frequency = (int)(base_frequency + 55u * (q16 / 655u));
	original_volume = 48 * inflight_sound_vol / 15;
	volume = original_volume * profile->player_engine_sound_volume_percent / 100;
	name = TieFlightSound_Name(sound_id);
	if (!name)
		return;

	if (lolevel_ImGetParam_tie98(sound_id, 0x100) != 0) {
		(void)lolevel_ImSetParamByName_tie98(name, 0x777, frequency);
		if (FrontendSound_GetVolume(name) != volume)
			(void)lolevel_ImSetParamByName_tie98(name, 0x600, volume);
		return;
	}

	(void)lolevel_ImSetParamByName_tie98(name, 0x777, frequency);
	(void)FrontendSound_QueueSound(name, 1, 1, 127, volume, 64, 0);
}

/* --------------------------------------------------------------------------
 * Setup / teardown.
 * -------------------------------------------------------------------------- */

// FUNCTION: TIE95 0x24740
void fsfx_allocsfxbuffer(void) {
	/* Empty stub in the binary -- module-level buffers are set up in
	 * fediskio_init_buffers_and_fonts. */
}

// FUNCTION: TIE95 0x24744
void fsfx_freesfx(void) {
	TieFlightSoundLayout layout = TieFlightSound_Layout();
	int i;

	if (layout.has_player_engine_loops) {
		const char* tie_name = TieFlightSound_Name(FSFX_PLAYER_ENGINE_TIE_ID);
		const char* rebel_name = TieFlightSound_Name(FSFX_PLAYER_ENGINE_REBEL_ID);
		while (tie_name && FrontendSound_CountPlaying(tie_name))
			FrontendSound_StopSoundByName(tie_name);
		while (rebel_name && FrontendSound_CountPlaying(rebel_name))
			FrontendSound_StopSoundByName(rebel_name);
	}
	for (i = 4; i < layout.table_count; i++) {
		if (soundhandles[i]) {
			free(soundhandles[i]);
			soundhandles[i] = NULL;
		}
	}
	memset(soundnames, 0, sizeof soundnames);
}

// FUNCTION: TIE95 0x247D8
int16_t fsfx_loadsfx(const char* filename) {
	int total = 0;
	TieFlightSoundLayout layout = TieFlightSound_Layout();

	/* MODERN ADAPTATION: the runtime sound-bank loader owns the native
	 * sample buffers and TIE98 sound names. Stop name-based loops before
	 * replacing the bank so no active track retains a freed VOC pointer. */
	int main_cap;

	char doe_path[32];

	fsfx_freesfx();

	/* Main SFX + voice file (SFX1.GMD / SFX2.GMD / ...). Capped at
	 * 47 records when voice is disabled, so the voice slots [51..]
	 * are never populated. */
	main_cap = voiceenabled ? 0 : 47;
	total += TieFlightSound_LoadBank(filename, 4, 107, main_cap);

	/* SFXDOE.LFD adjunct -- fills [107..108]. Built via resourcedir
	 * for platform-correct path separator (caller's `filename` arg
	 * already follows the same convention). */

	snprintf(doe_path, sizeof(doe_path), "%sSFXDOE.LFD", resourcedir);
	total += TieFlightSound_LoadBank(doe_path, 107, layout.mission_voice_base, 0);

	return (int16_t)total;
}

/* --------------------------------------------------------------------------
 * Per-mission voice .LFD loader.
 * -------------------------------------------------------------------------- */

// FUNCTION: TIE95 0x24ADC
int16_t fsfx_loadvoicelfd(void) {
	char path[64];
	char base[8];

	int n;
	TieFile* fp;
	uint32_t dir_size_dw;
	uint32_t dir_size;
	uint8_t* dir_buf;
	uint32_t entry_idx;
	int16_t loaded;
	TieFlightSoundLayout layout;
	uint16_t logical_index;
	int i;

	uint8_t hdr[16];

	/* Drop any voice cues left over from the previous mission: clear and
	 * free the selected edition's owned mission-voice range. */
	layout = TieFlightSound_Layout();
	for (i = layout.mission_voice_base; i < layout.mission_voice_base + layout.mission_voice_count; i++) {
		if (soundhandles[i]) {
			free(soundhandles[i]);
			soundhandles[i] = NULL;
		}
		soundnames[i][0] = '\0';
	}

	/* Training: no in-flight voice. train_craft_type is set non-zero
	 * by SHIPEXT_Mission_Enter for training scenes; CREATE_loadmission
	 * propagates it from train_craft_type_src into
	 * mission.train_craft_type before this loader runs. */
	if (mission.train_craft_type)
		return 0;

	/* Build the LFD basename ("<L>M<digit>" or "<battle>M<mission>").
	 * Battle/mission digits >=9 are encoded as "1<digit-9+'0'>" so each
	 * cursor stays single-byte. Casing matches retail (uppercase). */
	n = 0;
	if (mission.mission_mode == 1) {
		/* Combat-sim, non-tour. <ship-letter>M<course+1>.
		 * voice_id_a indexes the FIBAGDM table (0..6). */
		base[n++] = combat_ship_voice_letters[voice_id_a & 7];
		base[n++] = 'M';
		base[n++] = (char)(voice_id_b + '1');
	} else {
		/* Mode 4 (tour), 5 (combat-of-tour), and 0/2/3 fall through
		 * to the same battle/mission digit pair. The combat-of-tour
		 * branch encodes its battle as 12+battle in voice_id_a; the
		 * subtraction below recovers the 0-based battle index. */
		uint16_t battle;
		uint16_t mission_idx;

		if (mission.mission_mode == 4) {
			battle = currentbattle;
			mission_idx = currentmission;
		} else {
			battle = (uint16_t)(voice_id_a - 12);
			mission_idx = voice_id_b;
		}
		if (battle >= 9) {
			base[n++] = '1';
			base[n++] = (char)(battle + '0' - 9);
		} else {
			base[n++] = (char)(battle + '1');
		}
		base[n++] = 'M';
		if (mission_idx >= 9) {
			base[n++] = '1';
			base[n++] = (char)(mission_idx + '0' - 9);
		} else {
			base[n++] = (char)(mission_idx + '1');
		}
	}
	base[n] = '\0';

	/* Assemble the full relative path "VOICE/<NAME>/<NAME>.LFD". Retail
	 * prepends a CD drive letter from shell_drives_dw HIBYTE and uses
	 * backslashes; the port uses forward slashes (cross-platform) and the
	 * install directory's CD layout, where the asset folders ship as
	 * VOICE/<NAME>/<NAME>.LFD (case preserved). */
	memcpy(path, "VOICE/", 6);
	memcpy(path + 6, base, (size_t)n);
	path[6 + n] = '/';
	memcpy(path + 7 + n, base, (size_t)n);
	memcpy(path + 7 + 2 * n, ".LFD", 5); /* includes NUL */

	fp = TieStorage_Open(TIE_FILE_ROOT_FLIGHT_ASSET, path, "rb");
	if (!fp) {
		TieDiagnostics_Log(TIE_LOG_WARN, "fsfx: fopen(\"%s\") failed (voice bank not loaded)\n", path);
		return 0;
	}

	/* LFD file header: 16 bytes; only the trailing dword 'dir_size'
	 * (bytes 12..15) is consulted. Bytes 0..11 are format/magic. */

	if (TieStorage_Read(hdr, 1, 16, fp) != 16) {
		TieStorage_Close(fp);
		return 0;
	}

	memcpy(&dir_size_dw, &hdr[12], 4);
	dir_size = dir_size_dw;
	if (dir_size == 0 || dir_size > 0x4000u) {
		TieStorage_Close(fp);
		return 0;
	}

	dir_buf = (uint8_t*)malloc(dir_size);
	if (!dir_buf) {
		TieStorage_Close(fp);
		return 0;
	}
	if (TieStorage_Read(dir_buf, 1, dir_size, fp) != dir_size) {
		free(dir_buf);
		TieStorage_Close(fp);
		return 0;
	}

	/* Walk the edition-specific mission slots in order. Per-slot directory entries are
	 * consumed only for active slots; the LFD file is built per-
	 * mission so directory order matches the active-slot sequence
	 * (this is what the retail loader relies on -- entry_idx only
	 * advances when a slot is loaded). */
	entry_idx = 0;
	loaded = 0;
	for (logical_index = 0; logical_index < layout.mission_voice_count; logical_index++) {
		uint16_t slot = (uint16_t)(layout.mission_voice_base + logical_index);
		uint32_t entry_off;
		uint32_t sample_size;
		int active;

		uint8_t sub[16];

		/* Per-slot active gate (the four-way switch in FSFX_loadvoicelfd
		 * at 0x24da3-0x24de9): radio messages, then the primary,
		 * secondary, and loss win/loss message texts. */
		if (logical_index <= 15u)
			active = radiomsg[90u * logical_index] != 0;
		else if (logical_index == FSFX_MISSION_VOICE_PRIMARY)
			active = mission_file_header.mission.win_msg1[0][0] != 0;
		else if (logical_index == FSFX_MISSION_VOICE_SECONDARY)
			active = mission_file_header.mission.win_msg2[0][0] != 0;
		else if (logical_index == FSFX_MISSION_VOICE_LOSS)
			active = mission_file_header.mission.loss_msg[0][0] != 0;
		else
			active = 0;
		if (!active) {
			soundhandles[slot] = NULL;
			continue;
		}

		entry_off = entry_idx * 16u;
		if (entry_off + 16u > dir_size) {
			/* Truncated directory; drop remaining slots. */
			break;
		}

		memcpy(&sample_size, &dir_buf[entry_off + 12], 4);

		soundhandles[slot] = malloc(sample_size ? sample_size : 1);
		if (!soundhandles[slot])
			break;

		/* 16-byte sub-header before each payload (re-uses the file
		 * header scratch in retail, ignored content). */

		if (TieStorage_Read(sub, 1, 16, fp) != 16) {
			free(soundhandles[slot]);
			soundhandles[slot] = NULL;
			break;
		}
		if (TieStorage_Read(soundhandles[slot], 1, sample_size, fp) != sample_size) {
			free(soundhandles[slot]);
			soundhandles[slot] = NULL;
			TieStorage_Close(fp);
			free(dir_buf);
			return 0;
		}
		TieFlightSound_StoreName(slot, base, &dir_buf[entry_off + 4]);

		entry_idx++;
		loaded++;
	}

	free(dir_buf);
	TieStorage_Close(fp);
	return loaded;
}

/* --------------------------------------------------------------------------
 * Positional audio math.
 * -------------------------------------------------------------------------- */

// FUNCTION: TIE95 0x251B0
int16_t fsfx_calcvolume(uint16_t src_obj, uint16_t sound_id) {
	uint16_t max_dist;
	uint32_t dist;
	uint16_t vol;
	int32_t dx, dy, dz;
	uint16_t max_vol;

	/* 0xFFFF = "local / player" sound, max volume. */
	if (src_obj == (uint16_t)-1) {
		if (sound_id >= 51)
			return 112;
		return fullvolume[sound_id];
	}

	if (sound_id >= 51) {
		max_dist = 0x2000;
		max_vol = 112;
	} else {
		max_dist = sounddist[sound_id];
		max_vol = fullvolume[sound_id];
	}

	if (src_obj < 0x3800) {
		/* Previous-frame positions match what the renderer sees this
		 * frame (the binary uses world_*_prev throughout positional
		 * audio; see decompile at 0x23F0F). */
		dx = objects[src_obj].world_x_prev - camera.x;
		dy = objects[src_obj].world_y_prev - camera.y;
		dz = objects[src_obj].world_z_prev - camera.z;
	} else {
		/* Non-FlightObject refs (static objects, waypoints) resolve via
		 * create_getworldposition -> worldlocx/y/z. */
		create_getworldposition(src_obj, 0);
		dx = worldlocx - camera.x;
		dy = worldlocy - camera.y;
		dz = worldlocz - camera.z;
	}

	dist = collide_roughdistance3d(dx, dy, dz);

	/* 4-tier falloff.
	 *   dist >= max_dist*4  -> 0        (out of range)
	 *   dist >= max_dist*2  -> vol / 8  (far)
	 *   dist >= max_dist    -> vol / 4  (medium)
	 *   dist <  max_dist    -> linear interpolation over 31/32 of the
	 *                          range, capped at 127 */
	if (dist / 4 >= max_dist)
		return 0;
	if (dist / 2 >= max_dist)
		return max_vol >> 3;
	if (dist >= max_dist)
		return max_vol >> 2;

	vol =
		(uint16_t)((max_dist - dist) * (max_vol - (max_vol >> 2)) / (uint32_t)(max_dist - (max_dist >> 5))) +
		(max_vol >> 2);
	if (vol > 127)
		vol = 127;
	return vol;
}

// FUNCTION: TIE95 0x2530C
int32_t fsfx_calcpan(uint16_t src_obj, int16_t* volume_ptr) {
	int32_t dx;
	int32_t dy;
	int32_t dz;
	int32_t eye_x;
	int16_t eye_x_red;
	int32_t eye_z;
	int16_t eye_z_red;
	int16_t pan_angle;
	int32_t pan_out;

	if (src_obj == 0xFFFF)
		return 64;

	dx = objects[src_obj].world_x_prev - camera.x;
	dy = objects[src_obj].world_y_prev - camera.y;
	dz = objects[src_obj].world_z_prev - camera.z;

	/* Rotate into eye space. The matrix rows are stored as 32-bit
	 * Q16.16 values; the binary narrows dx/dy/dz to int16 before
	 * multiplying, which clamps large world deltas to the near
	 * quadrant. Result ends up in a 32-bit space with ~30 bits of
	 * usable range. */
	eye_x = worldeyeC1 * (int16_t)dz + worldeyeB1 * (int16_t)dy + worldeyeA1 * (int16_t)dx;
	if (eye_x >= 0x40000000)
		eye_x = 1073676288;
	if (eye_x <= -0x40000000)
		eye_x = -1073676288;
	eye_x_red = (int16_t)(eye_x >> 15);

	eye_z = worldeyeC3 * (int16_t)dz + worldeyeB3 * (int16_t)dy + worldeyeA3 * (int16_t)dx;
	if (eye_z >= 0x40000000)
		eye_z = 1073676288;
	if (eye_z <= -0x40000000)
		eye_z = -1073676288;
	eye_z_red = (int16_t)(eye_z >> 15);

	pan_angle = trig2_arctan((int32_t)eye_x_red, (int32_t)eye_z_red);
	pan_out = pan_angle;

	/* Back-hemisphere: |pan_angle| >= 0x4000 ( >= 90 deg). Derive a
	 * 2D "distance from directly behind" attenuation factor and
	 * subtract it from *volume_ptr, then mirror pan_angle to the
	 * front quadrant so the stereo image lands left/right instead of
	 * flipped. */
	if (pan_angle >= 0x4000 || pan_angle <= -16384) {
		int32_t eye_y = worldeyeC2 * (int16_t)dz + worldeyeB2 * (int16_t)dy + worldeyeA2 * (int16_t)dx;
		int16_t vert_angle;
		int32_t vert_dist_180;
		int32_t pan_dist_180;
		int16_t a;
		int16_t b;
		int16_t prod;
		int16_t factor;
		int16_t atten;
		int16_t hi;
		int16_t scaled;

		if (eye_y >= 0x40000000)
			eye_y = 1073676288;
		if (eye_y <= -0x40000000)
			eye_y = -1073676288;
		vert_angle = trig2_arctan((int32_t)(eye_y >> 15), (int32_t)eye_z_red);

		vert_dist_180 = 0x8000 - (int32_t)vert_angle;
		pan_dist_180 = 0x8000 - pan_out;
		pan_out = (uint16_t)(0x8000 - pan_out); /* mirror in LOWORD space */
		if (vert_dist_180 & 0x8000)
			vert_dist_180 = -vert_dist_180;
		if (pan_dist_180 & 0x8000)
			pan_dist_180 = -pan_dist_180;

		/* a,b each in [-64, 64] after the >>8. */
		a = (int16_t)(((int16_t)(0x4000 - (int16_t)vert_dist_180)) >> 8);
		b = (int16_t)(((int16_t)(0x4000 - (int16_t)pan_dist_180)) >> 8);
		prod = (int16_t)((int32_t)a * (int32_t)b);

		/* Watcom emits a toward-zero division by 64 / 128. Match it
		 * exactly: `prod - (prod >> 15 << N)` adds 2^N back when prod
		 * is negative so the arithmetic right shift rounds toward 0. */
		factor = (int16_t)((prod - (int16_t)((prod >> 15) << 6)) >> 6);
		atten = (int16_t)((int32_t)(*volume_ptr) * (int32_t)factor);
		hi = (int16_t)(((uint32_t)atten) >> 16);
		(void)hi;
		scaled = (int16_t)((atten - (int16_t)((atten >> 15) << 7)) >> 7);
		*volume_ptr = (int16_t)(*volume_ptr - scaled);
	}

	pan_out = (int32_t)(int16_t)((int16_t)pan_out >> 7);
	if ((int16_t)pan_out < -64)
		pan_out = -64;
	if ((int16_t)pan_out > 63)
		pan_out = 63;
	return pan_out + 64;
}

/* --------------------------------------------------------------------------
 * Trigger dispatchers.
 * -------------------------------------------------------------------------- */

// FUNCTION: TIE95 0x24F5C
int8_t fsfx_triggersfx(uint16_t sound_id, uint16_t src_obj) {
	int16_t vol_buf;
	int32_t pan;
	uint16_t priority;

	if (!sfxenabled)
		return 0;
	if (!soundhandles[sound_id])
		return 0;
	if (!inflight_sound_vol)
		return 0;

	vol_buf = fsfx_calcvolume(src_obj, sound_id);
	if (!vol_buf)
		return 0;

	/* calcpan may reduce vol_buf further for back-hemisphere sounds. */
	pan = fsfx_calcpan(src_obj, &vol_buf);

	priority = ((uint16_t)vol_buf < 0x7Eu) ? (uint16_t)vol_buf : 125;

	/* Local-sound bump: sfx emitted by the player's craft (or a
	 * child object that maps back to it via self_idx) wins the
	 * priority arbitration. */
	if (src_obj == 0xFFFF || src_obj == pstate.object_idx ||
		(src_obj < OBJ_REF_STATIC_BASE && objects[src_obj].self_idx == (int16_t)pstate.object_idx)) {
		priority = 126;
	}

	if (lolevel_ImGetParam(sound_id, IM_PARAM_IS_PLAYING)) {
		/* Looping engine/laser SFX -- don't retrigger while alive. */
		if (sound_id >= 0x2Au && sound_id <= 0x2Fu)
			return 0;
		if (lolevel_ImGetParam(sound_id, IM_PARAM_PRIORITY) > (int)priority)
			return 0;
		lolevel_ImStopSound(sound_id);
	}

	hilevel_ImStartSfx(sound_id, priority);
	lolevel_ImSetParam(sound_id, IM_PARAM_PRIORITY, priority);
	lolevel_ImSetParam(sound_id, IM_PARAM_PAN, (int)pan);
	lolevel_ImSetParam(sound_id, IM_PARAM_VOLUME, (uint16_t)vol_buf);
	return 1;
}

// FUNCTION: TIE95 0x25108
int8_t fsfx_triggerlasersfx(uint16_t projectile_obj) {
	uint8_t weapon_species;
	uint16_t sfx_id;

	if (!sfxenabled)
		return 0;
	if (!inflight_sound_vol)
		return 0;

	weapon_species = objects[projectile_obj].ship_idx;

	/* Laser / missile weapon-species mapping (ship_idx 0x89..0x9A). */
	switch (weapon_species) {
		case 137:
		case 138:
		case 139:
		case 140:
		case 141:
		case 142:
		case 143:
		case 144:
		case 145:
		case 146:
		case 147:
			sfx_id = (uint16_t)(weapon_species - 133); /*   4..14 */
			break;
		case 148:
		case 149:
			sfx_id = (uint16_t)(weapon_species - 138); /*  10..11 (intentional reuse) */
			break;
		case 150:
		case 151:
			sfx_id = (uint16_t)(weapon_species - 135); /*  15..16 */
			break;
		case 152:
		case 153:
		case 154:
			sfx_id = 17; /* single missile/torp clip */
			break;
		default:
			return 0;
	}
	return fsfx_triggersfx(sfx_id, projectile_obj);
}

// FUNCTION: TIE95 0x2554C
int32_t fsfx_triggergunsightsfx(uint16_t mode) {
	if (!sfxenabled)
		return 0;
	if (!inflight_sound_vol)
		return 0;

	if (mode == 0 || mode == 1) {
		/* Stop whichever gunsight channel is active. */
		if (lolevel_ImGetParam(36, 0x100))
			lolevel_ImStopSound(36);
		else if (lolevel_ImGetParam(35, 0x100))
			lolevel_ImStopSound(35);
	} else if (mode == 3) {
		/* Red lock: stop green (35), keep or play red (36). */
		if (lolevel_ImGetParam(35, 0x100))
			lolevel_ImStopSound(35);
		if (!lolevel_ImGetParam(36, 0x100))
			fsfx_triggersfx(36, 0xFFFF);
	} else {
		/* Green lock: stop red, keep or play green. */
		if (lolevel_ImGetParam(36, 0x100))
			lolevel_ImStopSound(36);
		if (!lolevel_ImGetParam(35, 0x100))
			fsfx_triggersfx(35, 0xFFFF);
	}
	return 1;
}

// FUNCTION: TIE95 0x25648
int8_t fsfx_triggerbeamsfx(int32_t firing) {
	uint16_t id;
	int was_playing;

	if (!sfxenabled || !inflight_sound_vol)
		return (int8_t)firing;

	if (!(uint16_t)firing) {
		/* Release: stop whichever beam channel is active. */
		int playing;

		if (lolevel_ImGetParam(38, IM_PARAM_IS_PLAYING))
			lolevel_ImStopSound(38);
		playing = lolevel_ImGetParam(37, IM_PARAM_IS_PLAYING);
		if (playing)
			playing = lolevel_ImStopSound(37);
		return (int8_t)playing;
	}

	if (bluetarget == 0xFFFF) {
		/* No locked target -- use the free-fire beam clip. */
		if (lolevel_ImGetParam(38, IM_PARAM_IS_PLAYING))
			lolevel_ImStopSound(38);
		was_playing = lolevel_ImGetParam(37, IM_PARAM_IS_PLAYING);
		if (was_playing)
			return (int8_t)was_playing;
		id = 37;
	} else {
		/* Locked target -- use the aimed-beam clip. */
		if (lolevel_ImGetParam(37, IM_PARAM_IS_PLAYING))
			lolevel_ImStopSound(37);
		was_playing = lolevel_ImGetParam(38, IM_PARAM_IS_PLAYING);
		if (was_playing)
			return (int8_t)was_playing;
		id = 38;
	}
	return fsfx_triggersfx(id, 0xFFFF);
}

// FUNCTION: TIE95 0x25734
int8_t fsfx_triggervoicesfx(uint16_t voice_id) {
	if (!voiceenabled)
		return 0;
	if (!soundhandles[voice_id])
		return 0;
	if (!inflight_speech_vol)
		return 0;

	if (lolevel_ImGetParam(currentdigital, IM_PARAM_IS_PLAYING) || blastcount) {
		/* Voice channel busy or queue non-empty -- preserve order. */
		if (blastcount == FSFX_BLAST_QUEUE_SIZE)
			return 0;
		blastqueue[blastcount] = (uint8_t)voice_id;
		blastcount++;
		return 1;
	}

	hilevel_ImStartVoice(voice_id, 127);
	lolevel_ImSetParam(voice_id, IM_PARAM_PRIORITY, 127);
	lolevel_ImSetParam(voice_id, IM_PARAM_PAN, 64);
	lolevel_ImSetParam(voice_id, IM_PARAM_VOLUME, 127);
	currentdigital = (uint8_t)voice_id;
	return 1;
}

/* --------------------------------------------------------------------------
 * Per-frame helpers (driven by TIE_doframe).
 * -------------------------------------------------------------------------- */

// FUNCTION: TIE95 0x25824
void fsfx_checkblastqueue(void) {
	uint16_t next_voice;
	uint8_t new_count;
	uint8_t i;

	if (!blastflag || !blastcount)
		return;
	if (currentdigital && lolevel_ImGetParam(currentdigital, IM_PARAM_IS_PLAYING))
		return;

	/* Dequeue head. */
	next_voice = blastqueue[0];
	new_count = (uint8_t)(blastcount - 1);
	for (i = 0; i < new_count; i++)
		blastqueue[i] = blastqueue[i + 1];
	blastcount = new_count;

	if (!inflight_speech_vol)
		return;
	if (!soundhandles[next_voice])
		return;

	hilevel_ImStartVoice(next_voice, 127);
	lolevel_ImSetParam(next_voice, IM_PARAM_PRIORITY, 127);
	lolevel_ImSetParam(next_voice, IM_PARAM_PAN, 64);
	lolevel_ImSetParam(next_voice, IM_PARAM_VOLUME, 127);
	currentdigital = (uint8_t)next_voice;
}

// FUNCTION: TIE95 0x25950
int16_t fsfx_checktieflyby(void) {
	uint16_t i;
	for (i = 0; i < NUM_CRAFTS; i++) {
		CraftData* craft;
		uint16_t species;
		uint16_t flyby_sound;
		int32_t curr_dist;
		int32_t prev_dist;
		int32_t threshold;

		if (i == pstate.object_idx)
			continue;

		craft = objects[i].craft_ptr;
		if (!craft)
			continue;
		if (craft->flight_flag)
			continue; /* not airborne */
		if (!craft->status_flags)
			continue; /* not spawned */
		if (!objects[i].current_speed)
			continue; /* not moving */

		species = objects[i].ship_idx;
		flyby_sound = 0xFFFF;
		switch (species) {
			case 1:
			case 4:
			case 14:
			case 15:
				flyby_sound = 44;
				break;
			case 2:
				flyby_sound = 45;
				break;
			case 3:
			case 13:
				flyby_sound = 46;
				break;
			case 5:
			case 6:
			case 7:
			case 8:
			case 9:
				flyby_sound = 42;
				break;
			case 12:
			case 16:
				flyby_sound = 43;
				break;
			default:
				continue;
		}
		if (flyby_sound == 0xFFFF)
			continue;

		/* Enter-the-range test: was outside the threshold last frame,
		 * within it this frame. Species-specific half-width gives the
		 * trigger distance. Note that the decompile's 'current_dist'
		 * is taken from world_x (current) and 'previous_dist' from
		 * world_x_prev -- the labels in the binary are inverted; we
		 * keep the physics right here. */
		curr_dist = collide_roughdistance3d(objects[i].world_x - pstate.player->world_x,
											objects[i].world_y - pstate.player->world_y,
											objects[i].world_z - pstate.player->world_z);
		prev_dist = collide_roughdistance3d(objects[i].world_x_prev - pstate.player->world_x_prev,
											objects[i].world_y_prev - pstate.player->world_y_prev,
											objects[i].world_z_prev - pstate.player->world_z_prev);
		threshold = (int32_t)species_table[species].bound_hwidth + 1024;

		if (threshold > curr_dist && prev_dist >= threshold)
			fsfx_triggersfx(flyby_sound, i);
	}
	return (int16_t)i;
}

// FUNCTION: TIE95 0x25AAC
int16_t fsfx_speakeravailable(void) {
	uint16_t i;

	if (!blastflag)
		return 0;

	for (i = 0; i < NUM_CRAFTS; i++) {
		if (i == pstate.object_idx)
			continue;
		if (!objects[i].ship_idx)
			continue;
		if (objects[i].side == pstate.player->side)
			return 1;
	}
	return 0;
}

/* --------------------------------------------------------------------------
 * Voice-clip stitching.
 * -------------------------------------------------------------------------- */

// FUNCTION: TIE95 0x25B0C
void fsfx_speakobjectname(uint16_t obj_idx, uint16_t prefix_voice) {
	/* Retail bails for obj_idx >= NUM_CRAFTS so warhead-slot CraftData*
	 * (a WarheadRecord*) is never reinterpreted as a craft. */
	CraftData* craft;
	EFGStruct* fg_ptr;
	const char* ref;
	uint16_t group_id;
	uint16_t name_idx;
	uint16_t wing_num;
	int16_t found;

	if (obj_idx >= NUM_CRAFTS || !objects[obj_idx].ship_idx || objects[obj_idx].category != 0)
		return; /* only craft have FG names */

	craft = objects[obj_idx].craft_ptr;

	/* 25% chance to pre-announce the speaking player (recursive call
	 * with prefix 0) when the enemy callout (prefix 51) is firing. */
	if (obj_idx != pstate.object_idx && prefix_voice == 51 && (uint16_t)math2_getrandom() < 0x4000)
		fsfx_speakobjectname(pstate.object_idx, 0);

	/* Case-insensitive prefix match against sfxgroupnameptrs[0..4].
	 * The ASCII '+ 32' trick lets us accept both upper and lower-case
	 * letters against an UPPER-case reference string. */
	fg_ptr = &fg_array[objects[obj_idx].fg_idx];

	for (group_id = 0; group_id < 5; group_id++) {
		ref = sfxgroupnameptrs[group_id];
		found = 0;
		for (name_idx = 0; name_idx < 12; name_idx++, ref++) {
			if (!*ref) {
				found = 1;
				break;
			}
			if (fg_ptr->name[name_idx] != *ref && (uint8_t)fg_ptr->name[name_idx] != (uint8_t)*ref + 32)
				break;
		}
		if (found)
			break;
	}
	if (!found)
		return;

	/* Extract wing number from the name suffix. */
	if (!fg_ptr->name[name_idx]) {
		/* No explicit suffix -- fall back to craft's FG index. */
		wing_num = (uint16_t)(craft->craft_idx_in_fg + 1);
	} else {
		if (fg_ptr->name[name_idx] == ' ')
			name_idx++;
		if ((uint8_t)fg_ptr->name[name_idx] >= '0' && (uint8_t)fg_ptr->name[name_idx] <= '9')
			wing_num = (uint16_t)((uint8_t)fg_ptr->name[name_idx] - '0');
	}

	/* Voice clips exist only for wing 1 and wing 2. Anything else
	 * silently fails (matches the binary). */
	if (wing_num < 1 || wing_num > 2)
		return;

	/* Emit the sequence:
	 *   [ "target" (0x33) if prefix==52 ]
	 *   [ prefix_voice if non-zero ]
	 *   [ group voice 53 + group_id ]        (ALPHA=53..MU=57)
	 *   [ wing voice 57 + wing_num ]         (one=58, two=59)
	 * When prefix==52 ("target callout"), the wing voice is suppressed
	 * in the binary. */
	if (prefix_voice == 52)
		fsfx_triggervoicesfx(0x33u);
	if (prefix_voice)
		fsfx_triggervoicesfx(prefix_voice);
	fsfx_triggervoicesfx((uint16_t)(group_id + 53));
	if (prefix_voice != 52)
		fsfx_triggervoicesfx((uint16_t)(wing_num + 57));
}

// FUNCTION: TIE95 0x25CDC
void fsfx_speakcongrats(void) {
	/* Pick one of 3 kudos clips (76..78). */
	uint16_t r = (uint16_t)math2_getrandom();
	uint16_t kudos;
	uint16_t excl;

	if (r < 21845)
		kudos = 76;
	else if (r < 43690)
		kudos = 77;
	else
		kudos = 78;
	fsfx_triggervoicesfx(kudos);

	/* Pick one of 3 exclamations (79..81). */
	r = (uint16_t)math2_getrandom();

	if (r < 21845)
		excl = 79;
	else if (r < 43690)
		excl = 80;
	else
		excl = 81;
	fsfx_triggervoicesfx(excl);

	/* 50% chance to also say the player's object name. */
	r = (uint16_t)math2_getrandom();
	if (r > 0x4000)
		fsfx_speakobjectname(pstate.object_idx, 0);
}

// FUNCTION: TIE95 0x25D60
int8_t fsfx_speakoperation(uint16_t order_voice, uint16_t verb_voice) {
	/* verb_voice 63 is the "take action" phrasing -- in that case,
	 * randomise the prefix (0x3C / 0x3D / fall-through). Otherwise
	 * always use the default "order + suffix (0x3E) + verb" form. */
	if (verb_voice == 63) {
		uint16_t r = (uint16_t)math2_getrandom();
		if (r < 21845) {
			fsfx_triggervoicesfx(0x3Cu);
			fsfx_triggervoicesfx(order_voice);
			return fsfx_triggervoicesfx(62u);
		}
		if (r < 43690) {
			fsfx_triggervoicesfx(0x3Du);
			fsfx_triggervoicesfx(order_voice);
			return fsfx_triggervoicesfx(62u);
		}
		/* fall through: default variant */
	}
	fsfx_triggervoicesfx(order_voice);
	fsfx_triggervoicesfx(0x3Eu);
	return fsfx_triggervoicesfx(verb_voice);
}

// FUNCTION: TIE95 0x25DDC
int8_t fsfx_speakobjectives(uint16_t objective_voice) {
	/* 50% chance to prepend a kudos + "objective" + player name. */
	uint16_t r;

	if ((uint16_t)math2_getrandom() > 0x4000u) {
		uint16_t r = (uint16_t)math2_getrandom();
		uint16_t kudos;
		if (r < 21845)
			kudos = 76;
		else if (r < 43690)
			kudos = 77;
		else
			kudos = 78;
		fsfx_triggervoicesfx(kudos);
		fsfx_triggervoicesfx(0x4Fu); /* "objective" */
		fsfx_speakobjectname(pstate.object_idx, 0);
	}

	fsfx_triggervoicesfx(objective_voice);
	if ((uint16_t)math2_getrandom() > 0x4000u)
		fsfx_triggervoicesfx(0x5Du); /* "completed" */
	fsfx_triggervoicesfx(0x5Fu);     /* "mission" */

	/* 25% chance to add "update". */
	r = (uint16_t)math2_getrandom();
	if (r < 0x4000)
		return fsfx_triggervoicesfx(0x60u);
	return (int8_t)r;
}

// FUNCTION: TIE95 0x25EA4
int8_t fsfx_speakorderack(int32_t target_idx, int32_t order_char, uint16_t cmdr_mode) {
	uint16_t target_obj = (uint16_t)target_idx;
	uint16_t r = (uint16_t)math2_getrandom();

	uint16_t order_voice;

	if (r >= 36864) {
		if (r >= 57344) {
			fsfx_triggervoicesfx(0x63u); /* "acknowledged" */
		} else {
			if ((uint16_t)math2_getrandom() > 0x4000u)
				fsfx_speakobjectname(pstate.object_idx, 0);
			if (cmdr_mode)
				fsfx_speakobjectname(pstate.object_idx, 0x34u); /* "target" prefix on self */
			else
				fsfx_speakobjectname(target_obj, 0x33u); /* "enemy"  prefix on target */
		}
	} else {
		uint16_t rm = (uint16_t)math2_getrandom();
		if (rm >= 21845) {
			uint16_t ack = (rm >= 43690) ? 98 : 97;
			fsfx_triggervoicesfx(ack);
			if ((uint16_t)math2_getrandom() > 0x4000u)
				fsfx_speakobjectname(pstate.object_idx, 0);
		} else {
			if ((uint16_t)math2_getrandom() < 0x4000u)
				fsfx_triggervoicesfx(0x66u); /* "roger" */
			fsfx_triggervoicesfx(0x68u);     /* "understood" */
			fsfx_speakobjectname(pstate.object_idx, 0);
		}
	}

	/* Order-specific tail clip. Default returns (order_char - 'p')
	 * without playing, matching the binary. */

	switch ((char)order_char) {
		case 'p':
			order_voice = 73;
			break; /* protect */
		case 'q':
			order_voice = 74;
			break; /* pursue */
		case 's':
			order_voice = 71;
			break; /* strafe */
		case 't':
			order_voice = 72;
			break; /* target */
		case 'u':
			order_voice = 69;
			break; /* unknown */
		case 'v':
			order_voice = ((uint16_t)math2_getrandom() >= 0x8000u) ? 100 : 70;
			break;
		default:
			return (int8_t)((int8_t)order_char - (int8_t)'p');
	}
	return fsfx_triggervoicesfx(order_voice);
}

/* --------------------------------------------------------------------------
 * Mission-critical kill announcer.
 * -------------------------------------------------------------------------- */

// FUNCTION: TIE95 0x26004
int32_t fsfx_checkcriticalcraft(int32_t obj_idx_arg, uint16_t action_voice) {
	uint16_t obj_idx_u16 = (uint16_t)obj_idx_arg;

	uint8_t pri_win_cond;
	int32_t is_critical;
	const ECondStruct* sa;
	const ECondStruct* sb;
	uint16_t death_voice;

	if (mission.primary_complete == 1)
		return 0;

	/* Auto-pass when the dead craft's FG has a destroy primary win
	 * condition. No group match needed for this branch -- the
	 * condition is "any craft in the FG with this kill-type goal". */
	pri_win_cond = fg_array[objects[obj_idx_u16].fg_idx].pri_win_cond;
	is_critical = 0;
	/* Win-condition codes 7, 9 and 12 count as "destroy / disable"; the
	 * binary compare chain excludes 10 and 11. */
	switch (pri_win_cond) {
		case 7:
		case 9:
		case 12:
			is_critical = 1;
			break;
	}

	/* Match the dead craft against each of the primary goal's two
	 * subconditions (cut[0].subcond[0] and [1]) via
	 * score_objectmemberofgroup; OR into is_critical. */
	sa = &cut[0].subcond[0];
	sb = &cut[0].subcond[1];
	switch (sa->cond) {
		case 7:
		case 9:
		case 12:
			is_critical |= score_objectmemberofgroup(obj_idx_u16, sa->type, sa->id);
			break;
	}
	switch (sb->cond) {
		case 7:
		case 9:
		case 12:
			is_critical |= score_objectmemberofgroup(obj_idx_u16, sb->type, sb->id);
			break;
	}
	if (!(uint16_t)is_critical)
		return 0;

	/* Voice the kill: player name + "critical" + platform/craft
	 * destroyed + caller's action clip. */
	fsfx_speakobjectname(pstate.object_idx, 0);
	fsfx_triggervoicesfx(0x55u); /* "critical" */
	death_voice = (objects[obj_idx_u16].genus == GENUS_PLATFORM) ? 87 : 86;
	fsfx_triggervoicesfx(death_voice);
	fsfx_triggervoicesfx(action_voice);
	return 1;
}
