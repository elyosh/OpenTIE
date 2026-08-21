#ifndef TIE_RUNTIME_AUDIO_CONFIG_H
#define TIE_RUNTIME_AUDIO_CONFIG_H

#include <stdbool.h>

#include "tie_runtime/runtime/profile_types.h"

typedef enum TieMidiBackendKind {
	TIE_MIDI_BACKEND_NONE,
	TIE_MIDI_BACKEND_FLUIDSYNTH,
	TIE_MIDI_BACKEND_FM4_OPL3,
	TIE_MIDI_BACKEND_SC55,
} TieMidiBackendKind;

struct ImuseNukedSc55Romset;

typedef struct TieMidiBackendConfig {
	TieMidiBackendKind kind;
	const char* soundfont_path;
	const struct ImuseNukedSc55Romset* sc55_romset;
} TieMidiBackendConfig;

typedef struct TieAudioConfig {
	TieMidiBackendConfig midi_backend;
	bool sb16_filter_enabled;
	TieMusicSource music_source;
} TieAudioConfig;

bool TieMidiBackend_Available(TieMidiBackendKind kind);
void TieAudio_Configure(const TieAudioConfig* config);
const TieAudioConfig* TieAudio_Config(void);

#endif
