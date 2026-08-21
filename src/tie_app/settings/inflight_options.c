#include "tie_app/settings/inflight_options.h"

#include <stdio.h>
#include <string.h>

static struct {
	TieInflightOptions persisted;
	TieInflightOptions requested;
	bool configured;
	bool dirty;
} g_inflight_settings;

static bool TieInflightSettings_Valid(const TieInflightOptions* options) {
	return options && options->sound_effects_volume <= 16 && options->music_volume <= 16 &&
		   options->speech_volume <= 16;
}

static void TieInflightSettings_Normalize(TieInflightOptions* options) {
	options->starfighter_collision_damage = options->starfighter_collision_damage != 0;
	options->player_invulnerable = options->player_invulnerable != 0;
	options->unlimited_ammunition = options->unlimited_ammunition != 0;
}

static bool TieInflightSettings_Equal(const TieInflightOptions* left, const TieInflightOptions* right) {
	return left->starfighter_collision_damage == right->starfighter_collision_damage &&
		   left->player_invulnerable == right->player_invulnerable &&
		   left->unlimited_ammunition == right->unlimited_ammunition &&
		   left->sound_effects_volume == right->sound_effects_volume &&
		   left->music_volume == right->music_volume && left->speech_volume == right->speech_volume;
}

bool TieInflightSettings_Configure(const TieInflightOptions* options) {
	memset(&g_inflight_settings, 0, sizeof g_inflight_settings);
	if (!TieInflightSettings_Valid(options))
		return false;
	g_inflight_settings.requested = *options;
	TieInflightSettings_Normalize(&g_inflight_settings.requested);
	g_inflight_settings.persisted = g_inflight_settings.requested;
	g_inflight_settings.configured = true;
	return true;
}

void TieInflightSettings_Shutdown(void) { memset(&g_inflight_settings, 0, sizeof g_inflight_settings); }

void TieInflightSettings_Get(TieInflightOptions* out) {
	if (out && g_inflight_settings.configured)
		*out = g_inflight_settings.requested;
}

bool TieInflightSettings_Set(const TieInflightOptions* options, char* error, size_t error_capacity) {
	TieInflightOptions normalized;
	if (!g_inflight_settings.configured || !TieInflightSettings_Valid(options)) {
		if (error && error_capacity)
			snprintf(error, error_capacity, "invalid in-flight options");
		return false;
	}
	normalized = *options;
	TieInflightSettings_Normalize(&normalized);
	if (TieInflightSettings_Equal(&normalized, &g_inflight_settings.requested))
		return true;
	if (!TieInflightOptions_Set(&normalized)) {
		if (error && error_capacity)
			snprintf(error, error_capacity, "could not apply in-flight options");
		return false;
	}
	g_inflight_settings.requested = normalized;
	g_inflight_settings.dirty =
		!TieInflightSettings_Equal(&g_inflight_settings.requested, &g_inflight_settings.persisted);
	return true;
}

bool TieInflightSettings_Flush(char* error, size_t error_capacity) {
	if (!g_inflight_settings.configured || !g_inflight_settings.dirty)
		return true;
	if (!TieInflightOptions_Flush(error, error_capacity))
		return false;
	g_inflight_settings.persisted = g_inflight_settings.requested;
	g_inflight_settings.dirty = false;
	return true;
}
