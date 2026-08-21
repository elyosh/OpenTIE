#include "tie_app/settings/launch_options.h"

#include <string.h>

static struct {
	TieAppLaunchOptions active;
	TieAppLaunchOptions persisted;
	TieAppLaunchOptions requested;
	TieLaunchOptionsPersistFn persist;
	void* user;
	bool configured;
	bool dirty;
} g_launch_options;

static bool TieLaunchOptions_LaunchOptionsValid(const TieAppLaunchOptions* options) {
	return options && memchr(options->tie95_data, '\0', sizeof options->tie95_data) &&
		   memchr(options->tie98_data, '\0', sizeof options->tie98_data) &&
		   memchr(options->fluidsynth_soundfont_file, '\0', sizeof options->fluidsynth_soundfont_file) &&
		   memchr(options->sc55_rom_directory, '\0', sizeof options->sc55_rom_directory) &&
		   (options->frontend_version == TIE_VERSION_SELECTION_TIE95 ||
			options->frontend_version == TIE_VERSION_SELECTION_TIE98) &&
		   (options->flight_version == TIE_VERSION_SELECTION_TIE95 ||
			options->flight_version == TIE_VERSION_SELECTION_TIE98) &&
		   (options->model_source == TIE_FLIGHT_MODEL_SOURCE_ORIGINAL ||
			options->model_source == TIE_FLIGHT_MODEL_SOURCE_REMASTERED) &&
		   (options->midi_backend == TIE_MIDI_BACKEND_FLUIDSYNTH ||
			options->midi_backend == TIE_MIDI_BACKEND_FM4_OPL3 ||
			options->midi_backend == TIE_MIDI_BACKEND_SC55) &&
		   (options->music_source == TIE_MUSIC_IMUSE || options->music_source == TIE_MUSIC_TIE98);
}

static void TieLaunchOptions_LaunchOptionsNormalize(TieAppLaunchOptions* options) {
	options->sb16_filter_enabled = options->sb16_filter_enabled != 0;
}

static bool TieLaunchOptions_LaunchOptionsEqual(const TieAppLaunchOptions* left,
												const TieAppLaunchOptions* right) {
	return strcmp(left->tie95_data, right->tie95_data) == 0 &&
		   strcmp(left->tie98_data, right->tie98_data) == 0 &&
		   strcmp(left->fluidsynth_soundfont_file, right->fluidsynth_soundfont_file) == 0 &&
		   strcmp(left->sc55_rom_directory, right->sc55_rom_directory) == 0 &&
		   left->frontend_version == right->frontend_version &&
		   left->flight_version == right->flight_version && left->model_source == right->model_source &&
		   left->midi_backend == right->midi_backend &&
		   left->sb16_filter_enabled == right->sb16_filter_enabled &&
		   left->music_source == right->music_source;
}

bool TieLaunchOptions_Configure(const TieAppLaunchOptions* active, TieLaunchOptionsPersistFn persist,
								void* user) {
	memset(&g_launch_options, 0, sizeof g_launch_options);
	if (!TieLaunchOptions_LaunchOptionsValid(active) || !persist)
		return false;
	g_launch_options.active = *active;
	g_launch_options.persisted = *active;
	g_launch_options.requested = *active;
	TieLaunchOptions_LaunchOptionsNormalize(&g_launch_options.active);
	TieLaunchOptions_LaunchOptionsNormalize(&g_launch_options.persisted);
	TieLaunchOptions_LaunchOptionsNormalize(&g_launch_options.requested);
	g_launch_options.persist = persist;
	g_launch_options.user = user;
	g_launch_options.configured = true;
	return true;
}

void TieLaunchOptions_Shutdown(void) { memset(&g_launch_options, 0, sizeof g_launch_options); }

void TieLaunchOptions_Get(TieAppLaunchOptions* out) {
	if (out && g_launch_options.configured)
		*out = g_launch_options.requested;
}

bool TieLaunchOptions_Set(const TieAppLaunchOptions* options) {
	if (!g_launch_options.configured || !TieLaunchOptions_LaunchOptionsValid(options))
		return false;
	g_launch_options.requested = *options;
	TieLaunchOptions_LaunchOptionsNormalize(&g_launch_options.requested);
	g_launch_options.dirty =
		!TieLaunchOptions_LaunchOptionsEqual(&g_launch_options.requested, &g_launch_options.persisted);
	return true;
}

bool TieLaunchOptions_RestartRequired(void) {
	if (!g_launch_options.configured)
		return false;
	TieAppLaunchOptions active = g_launch_options.active;
	active.flight_version = g_launch_options.requested.flight_version;
	return !TieLaunchOptions_LaunchOptionsEqual(&g_launch_options.requested, &active);
}

bool TieLaunchOptions_Flush(char* error, size_t error_capacity) {
	if (!g_launch_options.configured || !g_launch_options.dirty)
		return true;
	if (!g_launch_options.persist(&g_launch_options.requested, g_launch_options.user, error, error_capacity))
		return false;
	g_launch_options.persisted = g_launch_options.requested;
	g_launch_options.dirty = false;
	return true;
}
