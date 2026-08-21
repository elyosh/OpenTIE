#include "tie_runtime/audio/imuse_session.h"

static TieAudioConfig s_audio_config;

void TieAudio_Configure(const TieAudioConfig* config) {
	s_audio_config = config ? *config : (TieAudioConfig) { 0 };
}

const TieAudioConfig* TieAudio_Config(void) { return &s_audio_config; }
