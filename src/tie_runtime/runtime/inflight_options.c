#include "tie/gamesnd.h"
#include "tie/option.h"
#include "tie/rtsvga2.h"
#include "tie/tie.h"
#include "tie/user.h"
#include "tie_runtime/audio/imuse_session.h"
#include "tie_runtime/audio/music_policy.h"
#include "tie_runtime/runtime/inflight_state.h"
#include "tie_runtime/storage/storage.h"

#include <imuse/hilevel.h>
#include <stdio.h>
#include <string.h>

static const char option_filename[] = "options.cfg";
static const uint8_t option_default_values[OPTION_ROW_COUNT] = {
	1, 2, 3, 1, 1, 1, 1, 1, 1, 0, 0, 16, 16, 16,
};

static const uint8_t option_max_values[OPTION_ROW_COUNT] = {
	1, 2, 3, 1, 1, 1, 1, 1, 1, 1, 1, 16, 16, 16,
};

static uint8_t option_values[OPTION_ROW_COUNT];
static bool option_values_loaded;

/* Game-state portion of the fourteen DOS options, as applied at the end of
 * option_optionsroom. */
static void apply_flight_values(const uint8_t* values) {
	inflight_collision = (int8_t)values[8];
	inflight_invulnerable = (int8_t)values[9];
	inflight_unlimited = (int8_t)values[10];
	inflight_sound_vol = (int8_t)values[11];
	inflight_music_vol = (int8_t)values[12];
	inflight_speech_vol = (int8_t)values[13];

	soundvolflag = (uint8_t)(inflight_speech_vol + inflight_sound_vol);
	musicvolflag = (uint8_t)inflight_music_vol;
	cheatingflag = (uint8_t)(cheatingflag | (uint8_t)inflight_invulnerable | (uint8_t)inflight_unlimited);
}

static void apply_values(const uint8_t* values) {
	gouraudflag = (uint8_t)(values[0] << 6);
	shipdetailvalue = (int16_t)(1 - values[1]);
	shipdetailpolycnt = (uint16_t)(4 * values[1] + 8);
	starshipdetail = (uint16_t)(values[2] + 1);
	starshipexplodetail = (uint16_t)(((uint32_t)4096 << values[1]) - 1);
	drawmarkingsflag = values[3];
	drawbackdropflag = values[4];
	drawdebrisflag = values[5];
	palette_cycle_user = values[6];
	stardetaillevel = (uint16_t)(2 - values[7]);
	hyperspacedetail = (int16_t)(75 - 25 * (2 - values[7]));
	apply_flight_values(values);
}

void TieInflightOptions_ApplyAudio(void) {
	/*
	 * Volume 0..16 -> iMUSE 0..127 (0 mutes, else 8*v - 1).
	 * The binary reads the three inflight_*_vol bytes via an unaligned
	 * dword load on *_vol_loadbase + sar 24. We read the byte field
	 * directly — the result is identical.
	 */
	imuse_set_sfx_vol(im, inflight_sound_vol ? (int)inflight_sound_vol * 8 - 1 : 0);
	imuse_set_voice_vol(im, inflight_speech_vol ? (int)inflight_speech_vol * 8 - 1 : 0);
	imuse_set_music_vol(im, inflight_music_vol ? (int)inflight_music_vol * 8 - 1 : 0);
	if (TieMusicPolicy_UsesTie98())
		gamesnd_Set_CD_Volume(inflight_music_vol);
}

void TieInflightOptions_Load(void) {
	if (option_values_loaded)
		return;
	memcpy(option_values, option_default_values, sizeof option_values);
	TieFile* file = TieStorage_Open(TIE_FILE_ROOT_USER, option_filename, "rb");
	if (file) {
		(void)TieStorage_Read(option_values, 1, sizeof option_values, file);
		(void)TieStorage_Close(file);
	}
	for (int i = 0; i < OPTION_ROW_COUNT; ++i) {
		if (option_values[i] > option_max_values[i])
			option_values[i] = option_default_values[i];
	}
	option_values_loaded = true;
}

void TieInflightOptions_Reset(void) {
	memset(option_values, 0, sizeof option_values);
	option_values_loaded = false;
}

void TieInflightOptions_Get(TieInflightOptions* out) {
	if (!out)
		return;
	TieInflightOptions_Load();
	*out = (TieInflightOptions) {
		.starfighter_collision_damage = option_values[8] != 0,
		.player_invulnerable = option_values[9] != 0,
		.unlimited_ammunition = option_values[10] != 0,
		.sound_effects_volume = option_values[11],
		.music_volume = option_values[12],
		.speech_volume = option_values[13],
	};
}

bool TieInflightOptions_Set(const TieInflightOptions* options) {
	if (!options || options->sound_effects_volume > 16 || options->music_volume > 16 ||
		options->speech_volume > 16)
		return false;
	TieInflightOptions_Load();
	option_values[8] = options->starfighter_collision_damage ? 1 : 0;
	option_values[9] = options->player_invulnerable ? 1 : 0;
	option_values[10] = options->unlimited_ammunition ? 1 : 0;
	option_values[11] = options->sound_effects_volume;
	option_values[12] = options->music_volume;
	option_values[13] = options->speech_volume;
	if (maingameflag && !replayviewmode) {
		apply_flight_values(option_values);
		TieInflightOptions_ApplyAudio();
	}
	return true;
}

bool TieInflightOptions_Flush(char* error, size_t error_capacity) {
	TieInflightOptions_Load();
	if (TieStorage_WriteAllAtomic(TIE_FILE_ROOT_USER, option_filename, option_values, sizeof option_values) ==
		0)
		return true;
	if (error && error_capacity)
		snprintf(error, error_capacity, "could not save in-flight options");
	return false;
}

void TieInflightOptions_Apply(void) {
	TieInflightOptions_Load();
	apply_values(option_values);
	TieInflightOptions_ApplyAudio();
}

void TieInflightOptions_StoreLegacy(const uint8_t* values) {
	memcpy(option_values, values, sizeof option_values);
	option_values_loaded = true;
}
