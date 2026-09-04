#include "tie_app/settings/audio_options.h"

#include <string.h>

static struct {
	TieAppLiveAudioOptions persisted;
	TieAppLiveAudioOptions requested;
	TieAudioOptionsApplyFn apply;
	TieAudioOptionsPersistFn persist;
	void* user;
	bool configured;
	bool dirty;
} g_audio_options;

static bool TieAudioOptions_Valid(const TieAppLiveAudioOptions* options) {
	return options && (unsigned int)options->music_ducking_volume_percent <= 100u;
}

bool TieAudioOptions_Configure(const TieAppLiveAudioOptions* requested, TieAudioOptionsApplyFn apply,
							   TieAudioOptionsPersistFn persist, void* user) {
	memset(&g_audio_options, 0, sizeof g_audio_options);
	if (!TieAudioOptions_Valid(requested) || !apply || !persist)
		return false;
	g_audio_options.persisted = *requested;
	g_audio_options.requested = *requested;
	g_audio_options.apply = apply;
	g_audio_options.persist = persist;
	g_audio_options.user = user;
	g_audio_options.configured = true;
	return true;
}

void TieAudioOptions_Shutdown(void) { memset(&g_audio_options, 0, sizeof g_audio_options); }

void TieAudioOptions_Get(TieAppLiveAudioOptions* out) {
	if (out && g_audio_options.configured)
		*out = g_audio_options.requested;
}

bool TieAudioOptions_Set(const TieAppLiveAudioOptions* options, char* error, size_t error_capacity) {
	if (!g_audio_options.configured || !TieAudioOptions_Valid(options))
		return false;
	if (options->music_ducking_volume_percent == g_audio_options.requested.music_ducking_volume_percent)
		return true;
	if (!g_audio_options.apply(&g_audio_options.requested, options, g_audio_options.user, error,
							   error_capacity))
		return false;
	g_audio_options.requested = *options;
	g_audio_options.dirty = g_audio_options.requested.music_ducking_volume_percent !=
							g_audio_options.persisted.music_ducking_volume_percent;
	return true;
}

bool TieAudioOptions_Flush(char* error, size_t error_capacity) {
	if (!g_audio_options.configured || !g_audio_options.dirty)
		return true;
	if (!g_audio_options.persist(&g_audio_options.requested, g_audio_options.user, error, error_capacity))
		return false;
	g_audio_options.persisted = g_audio_options.requested;
	g_audio_options.dirty = false;
	return true;
}
