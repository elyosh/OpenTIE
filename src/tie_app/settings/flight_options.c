#include "tie_app/settings/flight_options.h"

#include <string.h>

static struct {
	TieAppLiveFlightOptions persisted;
	TieAppLiveFlightOptions requested;
	TieFlightOptionsApplyFn apply;
	TieFlightOptionsPersistFn persist;
	void* user;
	bool configured;
	bool dirty;
} g_flight_options;

static bool TieFlightOptions_FlightOptionsValid(const TieAppLiveFlightOptions* options) {
	return options &&
		   (options->tie98_original_renderer == TIE98_ORIGINAL_RENDERER_SOFTWARE ||
			options->tie98_original_renderer == TIE98_ORIGINAL_RENDERER_D3D) &&
		   (options->update_rate == TIE_FLIGHT_UPDATE_RATE_NATIVE ||
			options->update_rate == TIE_FLIGHT_UPDATE_RATE_TIE95 ||
			options->update_rate == TIE_FLIGHT_UPDATE_RATE_UNLOCKED) &&
		   (unsigned int)options->player_engine_sound_volume_percent <= 100u;
}

static bool TieFlightOptions_FlightOptionsEqual(const TieAppLiveFlightOptions* left,
												const TieAppLiveFlightOptions* right) {
	return left->aspect_correct_legacy_scenes == right->aspect_correct_legacy_scenes &&
		   left->tie98_original_renderer == right->tie98_original_renderer &&
		   left->update_rate == right->update_rate &&
		   left->player_engine_sound_enabled == right->player_engine_sound_enabled &&
		   left->player_engine_sound_volume_percent == right->player_engine_sound_volume_percent;
}

static void TieFlightOptions_FlightOptionsNormalize(TieAppLiveFlightOptions* options) {
	options->aspect_correct_legacy_scenes = options->aspect_correct_legacy_scenes != 0;
	options->player_engine_sound_enabled = options->player_engine_sound_enabled != 0;
}

bool TieFlightOptions_Configure(const TieAppLiveFlightOptions* requested, TieFlightOptionsApplyFn apply,
								TieFlightOptionsPersistFn persist, void* user) {
	memset(&g_flight_options, 0, sizeof g_flight_options);
	if (!TieFlightOptions_FlightOptionsValid(requested) || !apply || !persist)
		return false;
	g_flight_options.requested = *requested;
	TieFlightOptions_FlightOptionsNormalize(&g_flight_options.requested);
	g_flight_options.persisted = g_flight_options.requested;
	g_flight_options.apply = apply;
	g_flight_options.persist = persist;
	g_flight_options.user = user;
	g_flight_options.configured = true;
	return true;
}

void TieFlightOptions_Shutdown(void) { memset(&g_flight_options, 0, sizeof g_flight_options); }

void TieFlightOptions_Get(TieAppLiveFlightOptions* out) {
	if (out && g_flight_options.configured)
		*out = g_flight_options.requested;
}

bool TieFlightOptions_Set(const TieAppLiveFlightOptions* options, char* error, size_t error_capacity) {
	TieAppLiveFlightOptions normalized;
	if (!g_flight_options.configured || !TieFlightOptions_FlightOptionsValid(options))
		return false;
	normalized = *options;
	TieFlightOptions_FlightOptionsNormalize(&normalized);
	if (TieFlightOptions_FlightOptionsEqual(&normalized, &g_flight_options.requested))
		return true;
	if (!g_flight_options.apply(&g_flight_options.requested, &normalized, g_flight_options.user, error,
								error_capacity))
		return false;
	g_flight_options.requested = normalized;
	g_flight_options.dirty = !TieFlightOptions_FlightOptionsEqual(&normalized, &g_flight_options.persisted);
	return true;
}

bool TieFlightOptions_Flush(char* error, size_t error_capacity) {
	if (!g_flight_options.configured || !g_flight_options.dirty)
		return true;
	if (!g_flight_options.persist(&g_flight_options.requested, g_flight_options.user, error, error_capacity))
		return false;
	g_flight_options.persisted = g_flight_options.requested;
	g_flight_options.dirty = false;
	return true;
}
