#include "tie_runtime/audio/imuse_session.h"
#include "tie/fmusic.h"
#include "tie_runtime/audio/midi_backend.h"
#include "tie_runtime/audio/output.h"
#include "tie_runtime/diagnostics/diagnostics.h"

#include <imuse/commands.h>
#include <imuse/midi_backend.h>
#include <limits.h>
#include <stddef.h>

imuse_t* im;
static bool audio_output_started;

/* Output sample rate consumed by imuse_mix. 44100 Hz preserves the
 * SoundFont's native fidelity (running FluidSynth at 22050 would
 * alias every voice above 11 kHz). The wave engine still produces
 * at 22050 (waveSpeed=1) — the renderer's PullSamples upsamples 2×
 * on the audio thread and optionally applies the SB16 reconstruction
 * filter. Aeron accepts this 44100 Hz mix and resamples it to its fixed
 * device rate. */
enum {
	TIE_IMUSE_AUDIO_RATE = 44100,
};

/* Keep one large host delta from advancing the recovered engine through an
 * unbounded number of internal ticks. Time beyond this bound is dropped. */
enum {
	TIE_IMUSE_MAX_ADVANCE_US = (8000 * 8),
};

/* iMUSE log sink. The library pre-formats messages and tags each with a
 * level; a matching prefix keeps diagnostics grep-friendly. */
static void TieImuseSession_Log(void* user, ImuseLogLevel level, const char* msg) {
	const char* tag;
	(void)user;
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

static void TieImuseSession_Render(void* userdata, int16_t* frames, size_t frame_count) {
	imuse_t* session = (imuse_t*)userdata;
	while (frame_count > 0) {
		size_t chunk = frame_count > (size_t)INT_MAX ? (size_t)INT_MAX : frame_count;
		imuse_mix_s16(session, frames, (int)chunk);
		frames += chunk * 2u;
		frame_count -= chunk;
	}
}

bool TieImuseSession_Open(ImuseGetSoundPtrFunc get_sound_ptr) {
	ImuseHost host = { 0 };
	ImuseConfig cfg = { 0 };
	const TieAudioConfig* audio_config = TieAudio_Config();
	ImuseMidiBackend* midiBackend = TieMidiBackend_Create(&audio_config->midi_backend);
	if (!midiBackend && audio_config->midi_backend.kind != TIE_MIDI_BACKEND_NONE)
		TieDiagnostics_Log(TIE_LOG_WARN, "GAMESND: MIDI backend unavailable; synthesis disabled\n");

	/* Host callbacks (where data comes from, where logs go). */
	host.getSoundPtrFunc = get_sound_ptr;
	host.logFunc = TieImuseSession_Log;
	host.logUser = NULL;

	/* Engine settings. The original services iMUSE every 8 ms while each service
	 * advances its logical clock by 8060 us. The host's imuse_advance call
	 * rate is decoupled from that cadence. */
	cfg.outputSampleRate = TIE_IMUSE_AUDIO_RATE;
	cfg.waveSpeed = 1;
	cfg.waveMixCount = 4;
	cfg.waveOutputFilter =
		audio_config->sb16_filter_enabled ? IMUSE_WAVE_OUTPUT_FILTER_SB16 : IMUSE_WAVE_OUTPUT_FILTER_NONE;

	/* imuse_create takes ownership of midiBackend unconditionally:
	 * on failure it has already released it, so we don't need any
	 * rollback here. host + cfg are read once and copied; the locals
	 * may go out of scope on return. */
	im = imuse_create(&host, &cfg, midiBackend);
	if (!im) {
		TieDiagnostics_Log(TIE_LOG_INFO, "GAMESND: iMUSE init failed\n");
		return false;
	}
	(void)TieImuseSession_SetMusicDuckingVolumePercent(audio_config->music_ducking_volume_percent);

	audio_output_started = false;
	audio_output_started = TieAudioOutput_Start(TIE_IMUSE_AUDIO_RATE, 2, TieImuseSession_Render, im);
	return true;
}

void TieImuseSession_Close(void) {
	if (!im)
		return;

	if (audio_output_started)
		TieAudioOutput_Stop();
	audio_output_started = false;

	imuse_destroy(im);
	im = NULL;
}

void TieImuseSession_Advance(int32_t elapsed_us) {
	if (!im || elapsed_us <= 0)
		return;

	if (elapsed_us > TIE_IMUSE_MAX_ADVANCE_US)
		elapsed_us = TIE_IMUSE_MAX_ADVANCE_US;

	imuse_advance(im, elapsed_us);
}

bool TieImuseSession_SetMusicDuckingVolumePercent(int percent) {
	if ((unsigned int)percent > 100u)
		return false;
	if (!im)
		return true;
	/* iMUSE uses a /128 fixed-point multiplier. Rounded conversion keeps the
	 * original 37% default at its exact factor of 47. */
	return imuse_set_music_ducking_factor(im, (percent * 128 + 50) / 100) == 0;
}

intptr_t TieImuse_SoundId(const void* sound) { return (intptr_t)sound; }

void* TieImuse_SoundHandle(intptr_t sound_id) { return (void*)sound_id; }

intptr_t TieImuse_CallbackOpcode(int (*callback)(int marker)) { return (intptr_t)callback; }

void TieImuse_Printf(const char* text) { (void)text; }

/* The fmusic id is carried pointer-width and never dereferenced by the
 * engine: getSoundPtrFunc resolves flight ids through fmusic paging. */
void* TieImuse_LoadFlightMusic(const char* name) { return TieImuse_SoundHandle(fmusic_fmLoadSound(name)); }

void TieImuse_UnloadFlightMusic(void* handle) {
	(void)handle;
	fmusic_fmUnloadSound();
}

int32_t TieImuse_AddressSeed(const void* address) { return (int32_t)(intptr_t)address; }
