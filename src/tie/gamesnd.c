#include "tie/gamesnd.h"
#include "tie/cdaudio_tie98.h"
#include "tie/fmusic.h"
#include "tie/fsfx.h"
#include "tie/shell.h"
#include "tie/soundext.h"
#include "tie/tie.h" /* colorcycleflag, palette_cycle_user, colorcycleuserflag */
#include "tie_runtime/audio/config.h"
#include "tie_runtime/audio/imuse_session.h"
#include "tie_runtime/audio/midi_backend.h"
#include "tie_runtime/audio/output.h"
#include "tie_runtime/diagnostics/diagnostics.h"
#include "tie_runtime/display/classic_display.h"
#include "tie_runtime/display/classic_framebuffer.h"
#include "tie_runtime/flight_assets/model_types.h"
#include "tie_runtime/input/input.h"
#include "tie_runtime/runtime/exports.h"
#include "tie_runtime/runtime/profile.h"
#include "tie_runtime/storage/storage.h"
#include "tie_runtime/timing/sim_clock.h"

#include <imuse/commands.h>
#include <imuse/filelist.h>
#include <imuse/lolevel.h>
#include <imuse/midi_backend.h>

#include "landru/sound.h"

#include <limits.h>
#include <stdint.h>
#include <string.h>

/* TIE audio bridge. iMUSE owns its wave renderer and MIDI backend handle;
 * the application owns backend resources and the PCM output worker. Host and
 * engine configuration are copied during session creation. */

/* ===== Globals ===== */

// GLOBAL: TIE 0xFB620
int16_t frontendflag;

/* libimuse session handle. Created in gamesnd_Open_Pre_iMuse, destroyed
 * in gamesnd_Close_Pre_iMuse. Declared as `extern` in imuse_session.h
 * for every TIE source file that calls into libimuse. */
imuse_t* im;
static bool audio_output_started;

/* ===== iMUSE callbacks ===== */

/* ImuseGetSoundPtrFunc: the engine hands us an intptr_t soundId.
 *
 * Three address spaces depending on frontendflag (matches retail
 * GAMESND_GetSoundAddr at 0x88c1d):
 *   == 1 (front-end):  soundId is a Sound* — return snd->data.
 *   == 2 (transition): return NULL (engine is being drained).
 *   == 0 (flight):     soundId is a small integer id.
 *     id <  500  -> fsfx SFX index: soundhandles[id]
 *     id >= 500  -> fmusic track id: page in and return the paged
 *                   buffer (id - 500 is the track index).
 */
static void* GetSoundAddr(intptr_t soundId) {
	if (frontendflag == 1) {
		Sound* snd = (Sound*)soundId;
		if (snd && snd->data)
			return snd->data;
		return NULL;
	}
	if (frontendflag == 2)
		return NULL;

	/* frontendflag == 0: flight */
	if (soundId < 500) {
		uint16_t idx = (uint16_t)soundId;
		if (idx >= FSFX_NUM_SOUND_HANDLES)
			return NULL;
		return soundhandles[idx];
	}
	uint16_t track = (uint16_t)(soundId - 500);
	fmusic_PageSound(track);
	return fmusic_GetPagedSound(track);
}

__attribute__((unused)) static void CopySoundRange(void* dest, void* sound, int32_t start, int16_t length) {
	Sound* snd = (Sound*)sound;

	if (frontendflag == 2 || !frontendflag)
		return;

	if (snd && snd->data) {
		uint8_t* data = (uint8_t*)snd->data;
		memmove(dest, &data[start], length);
	} else {
		memset(dest, 0, length);
	}
}

/* iMUSE log sink: route all diagnostics to stderr. The library
 * pre-formats messages and tags each with a level — we add a
 * matching prefix so stderr output stays grep-friendly. */
static void gamesnd_imuse_log(void* user, ImuseLogLevel level, const char* msg) {
	(void)user;
	const char* tag;
	switch (level) {
		case IMUSE_LOG_TRACE:
			tag = "trace";
			break;
		case IMUSE_LOG_INFO:
			tag = "info";
			break;
		case IMUSE_LOG_WARN:
			tag = "warn";
			break;
		case IMUSE_LOG_ERROR:
			tag = "error";
			break;
		default:
			tag = "?";
			break;
	}
	TieDiagnostics_Log(TIE_LOG_INFO, "[imuse %s] %s\n", tag, msg);
}

/* ===== Public API ===== */

/* Output sample rate consumed by imuse_mix. 44100 Hz preserves the
 * SoundFont's native fidelity (running FluidSynth at 22050 would
 * alias every voice above 11 kHz). The wave engine still produces
 * at 22050 (waveSpeed=1) — the renderer's PullSamples upsamples 2×
 * on the audio thread and optionally applies the SB16 reconstruction
 * filter. Aeron accepts this 44100 Hz mix and resamples it to its fixed
 * device rate. */
#define GAMESND_AUDIO_RATE 44100

static void gamesnd_RenderAudio(void* userdata, int16_t* frames, size_t frame_count) {
	imuse_t* session = (imuse_t*)userdata;
	while (frame_count > 0) {
		size_t chunk = frame_count > (size_t)INT_MAX ? (size_t)INT_MAX : frame_count;
		imuse_mix_s16(session, frames, (int)chunk);
		frames += chunk * 2u;
		frame_count -= chunk;
	}
}

// FUNCTION: TIE 0x88905
int16_t gamesnd_Open_Pre_iMuse(void) {
	const TieAudioConfig* audio_config = TieAudio_Config();
	ImuseMidiBackend* midiBackend = TieMidiBackend_Create(&audio_config->midi_backend);
	if (!midiBackend && audio_config->midi_backend.kind != TIE_MIDI_BACKEND_NONE)
		TieDiagnostics_Log(TIE_LOG_WARN, "GAMESND: MIDI backend unavailable; synthesis disabled\n");

	/* Host callbacks (where data comes from, where logs go). */
	ImuseHost host = {
		.getSoundPtrFunc = GetSoundAddr,
		.logFunc = gamesnd_imuse_log,
		.logUser = NULL,
	};

	/* Engine settings. The original services iMUSE every 8 ms while each service
	 * advances its logical clock by 8060 us. The host's imuse_advance call
	 * rate is decoupled from that cadence. */
	ImuseConfig cfg = {
		.outputSampleRate = GAMESND_AUDIO_RATE,
		.waveSpeed = 1,
		.waveMixCount = 4,
		.waveOutputFilter =
			audio_config->sb16_filter_enabled ? IMUSE_WAVE_OUTPUT_FILTER_SB16 : IMUSE_WAVE_OUTPUT_FILTER_NONE,
	};

	/* imuse_create takes ownership of midiBackend unconditionally:
	 * on failure it has already released it, so we don't need any
	 * rollback here. host + cfg are read once and copied; the locals
	 * may go out of scope on return. */
	im = imuse_create(&host, &cfg, midiBackend);
	if (!im) {
		TieDiagnostics_Log(TIE_LOG_INFO, "GAMESND: iMUSE init failed\n");
		return 0;
	}
	(void)gamesnd_SetMusicDuckingVolumePercent(audio_config->music_ducking_volume_percent);

	audio_output_started = false;
	audio_output_started = TieAudioOutput_Start(GAMESND_AUDIO_RATE, 2, gamesnd_RenderAudio, im);

	/* The internal wave renderer is always present after I3, so the
	 * digital sub-system is always available. */
	digital_exists = 1;
	return 1;
}

// FUNCTION: TIE 0x88BDB
void gamesnd_Close_Pre_iMuse(void) {
	if (!im)
		return;

	if (audio_output_started)
		TieAudioOutput_Stop();
	audio_output_started = false;

	imuse_destroy(im);
	im = NULL;
}

/* Keep one large host delta from advancing the recovered engine through an
 * unbounded number of internal ticks. Time beyond this bound is dropped. */
#define GAMESND_MAX_ADVANCE_US (8000 * 8)

void gamesnd_AdvanceAudio(int32_t elapsed_us) {
	if (!im || elapsed_us <= 0)
		return;

	if (elapsed_us > GAMESND_MAX_ADVANCE_US)
		elapsed_us = GAMESND_MAX_ADVANCE_US;

	imuse_advance(im, elapsed_us);
}

// FUNCTION: TIE98 0x42FBA0
void gamesnd_Set_CD_Volume(int volume) {
	if (volume < 0)
		volume = 0;
	if (volume > 16)
		volume = 16;
	CDAUDIO_Set_Volume((uint32_t)(0xFFFFu * (uint32_t)volume / 16u));
}

bool gamesnd_SetMusicDuckingVolumePercent(int percent) {
	if ((unsigned int)percent > 100u)
		return false;
	if (!im)
		return true;
	/* iMUSE uses a /128 fixed-point multiplier. Rounded conversion keeps the
	 * original 37% default at its exact factor of 47. */
	return imuse_set_music_ducking_factor(im, (percent * 128 + 50) / 100) == 0;
}

/* Five-phase glow animation rewrites palette slots 0xF8..0xFA every 18 or
 * 36 PIT ticks when color cycling is enabled. */

static const uint8_t s_glow_table[45] = {
	/* slot 0xF8 — green pulse */
	0x00,
	0x3F,
	0x00,
	0x00,
	0x30,
	0x00,
	0x00,
	0x22,
	0x00,
	0x00,
	0x14,
	0x00,
	0x00,
	0x06,
	0x00,
	/* slot 0xF9 — pink pulse */
	0x3F,
	0x20,
	0x1F,
	0x3F,
	0x1A,
	0x15,
	0x3F,
	0x14,
	0x0E,
	0x3F,
	0x1A,
	0x15,
	0x3F,
	0x20,
	0x1F,
	/* slot 0xFA — blue pulse */
	0x1A,
	0x39,
	0x3F,
	0x11,
	0x32,
	0x3F,
	0x08,
	0x29,
	0x3F,
	0x11,
	0x32,
	0x3F,
	0x1A,
	0x39,
	0x3F,
};

static TieSimClockCursor s_glow_cursor;
static int s_glow_cursor_init;
static int s_glow_countdown; /* PIT ticks remaining */
static int s_glow_phase;     /* 0..4 */

void gamesnd_drive_palette_cycle(void) {
	if (!s_glow_cursor_init) {
		TieSimClock_CursorInit(&s_glow_cursor);
		s_glow_cursor_init = 1;
		/* countdown = 0 + phase = 0 from BSS; the first non-empty call
		 * will fall straight into the reload+fire branch, matching the
		 * retail ISR's behavior right after init when D2B15 starts at 0. */
		return;
	}

	int32_t pit_ticks = TieSimClock_CursorConsumePitTicks(&s_glow_cursor);
	while (pit_ticks > 0) {
		if (s_glow_countdown > 0) {
			int step = pit_ticks < s_glow_countdown ? pit_ticks : s_glow_countdown;
			s_glow_countdown -= step;
			pit_ticks -= step;
			if (s_glow_countdown > 0)
				break;
		}

		/* Reload runs unconditionally so cadence is preserved even
		 * when the gate is closed (matches retail 88af0..88b0f). */
		s_glow_countdown = colorcycleuserflag ? 36 : 18;

		int gate = ((palette_cycle_user & colorcycleflag) != 0) || (colorcycleuserflag != 0);
		if (!gate)
			continue;

		s_glow_phase = (s_glow_phase + 1) % 5;
		int off = s_glow_phase * 3;
		TieClassicFramebuffer_SetPalette(&s_glow_table[0 + off], 0xF8, 1);
		TieClassicFramebuffer_SetPalette(&s_glow_table[15 + off], 0xF9, 1);
		TieClassicFramebuffer_SetPalette(&s_glow_table[30 + off], 0xFA, 1);
	}
}

/* Adapters bridging fmusic's int16_t-id API to iMUSE's (void *handle)
 * filelist callback signatures. The id is cast pointer-width and never
 * dereferenced by the engine -- getSoundPtrFunc returns NULL in flight
 * mode, and fmusic paged-sound access goes through fmusic_GetPagedSound
 * directly, not through the handle. */
static void* fmusic_load_cb(const char* name) { return (void*)(intptr_t)fmusic_fmLoadSound(name); }
static void fmusic_unload_cb(void* handle) {
	(void)handle;
	fmusic_fmUnloadSound();
}

/* Transition the already-initialized iMUSE engine from front-end sound
 * (soundext callbacks) to flight music (fmusic callbacks). Retail's
 * GAMESND_game_Open_iMuse at 0x88EF6 does not re-initialize iMUSE -- that
 * would fail with "system already initialized". It only drains the
 * active sounds and swaps the filelist callback pair. */
// FUNCTION: TIE 0x88EF6
void gamesnd_game_Open_iMuse(void) {
	imuse_stop_all_sounds(im);
	imuse_filelist_unload_all(im);
	frontendflag = 0;
	imuse_pause(im);
	imuse_filelist_init(im, fmusic_load_cb, fmusic_unload_cb, NULL, NULL);
	imuse_resume(im);
}

void gamesnd_game_Close_iMuse(void) { gamesnd_Close_Pre_iMuse(); }

// FUNCTION: TIE 0x88E47
void gamesnd_game_Set_Front_Sound(void) {
	frontendflag = 1;
	imuse_pause(im);
	imuse_filelist_init(im, soundext_TIE_Load_Sound, soundext_TIE_Unload_Sound, NULL, NULL);
	imuse_resume(im);
}

// FUNCTION: TIE 0x88E8C
void gamesnd_game_Set_Flight_Sound(void) { gamesnd_Transition_Sound(); }

// FUNCTION: TIE 0x88EB0
void gamesnd_Transition_Sound(void) {
	imuse_stop_all_sounds(im);
	imuse_filelist_unload_all(im);
	imuse_clear_trigger(im, (intptr_t)-1, -1, -1);
	frontendflag = 2;
}

void gamesnd_End_Transition_Sound(void) {
	imuse_stop_all_sounds(im);
	imuse_filelist_unload_all(im);
	frontendflag = 0;
}
