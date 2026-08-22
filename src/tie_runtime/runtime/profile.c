#include "tie_runtime/runtime/profile.h"

#include "tie/fsfx.h"
#include "tie_runtime/audio/player_engine.h"
#include "tie_runtime/diagnostics/diagnostics.h"
#include "tie_runtime/flight_assets/service.h"
#include "tie_runtime/storage/storage.h"

static const TieFrontendProfile profiles[] = {
	{
		.id = TIE_FRONTEND_PROFILE_TIE95,
		.vesa_mode = 0x13,
		.width = 320,
		.height = 200,
		.scratch_size = 0x2000,
		.font_count = 2,
		.secondary_vga = false,
		.asset_source_id = "tie95",
	},
	{
		.id = TIE_FRONTEND_PROFILE_TIE98,
		.vesa_mode = 0x101,
		.width = 640,
		.height = 480,
		.scratch_size = 0x5000,
		.font_count = 4,
		.secondary_vga = true,
		.asset_source_id = "tie98",
	},
};

static TieFrontendProfileId selected_profile = TIE_FRONTEND_PROFILE_TIE95;
static TieFlightProfile selected_flight_profile = {
	.version = TIE_GAME_VERSION_TIE95,
	.model_source = TIE_FLIGHT_MODEL_SOURCE_ORIGINAL,
	.tie98_original_renderer = TIE98_ORIGINAL_RENDERER_SOFTWARE,
	.update_rate = TIE_FLIGHT_UPDATE_RATE_NATIVE,
	.player_engine_sound_enabled = false,
	.player_engine_sound_volume_percent = 100,
};
static TieFlightProfile pending_flight_profile;
static bool flight_profile_pending;
static bool tie98_original_renderer_pending;

static bool TieProfile_FlightProfileValid(const TieFlightProfile* profile) {
	return profile &&
		   (profile->version == TIE_GAME_VERSION_TIE95 || profile->version == TIE_GAME_VERSION_TIE98) &&
		   (profile->model_source == TIE_FLIGHT_MODEL_SOURCE_ORIGINAL ||
			profile->model_source == TIE_FLIGHT_MODEL_SOURCE_REMASTERED) &&
		   (profile->tie98_original_renderer == TIE98_ORIGINAL_RENDERER_SOFTWARE ||
			profile->tie98_original_renderer == TIE98_ORIGINAL_RENDERER_D3D) &&
		   (profile->update_rate == TIE_FLIGHT_UPDATE_RATE_NATIVE ||
			profile->update_rate == TIE_FLIGHT_UPDATE_RATE_TIE95 ||
			profile->update_rate == TIE_FLIGHT_UPDATE_RATE_UNLOCKED) &&
		   (unsigned int)profile->player_engine_sound_volume_percent <= 100u &&
		   (profile->version != TIE_GAME_VERSION_TIE95 ||
			(profile->model_source == TIE_FLIGHT_MODEL_SOURCE_ORIGINAL &&
			 profile->tie98_original_renderer == TIE98_ORIGINAL_RENDERER_SOFTWARE &&
			 !profile->player_engine_sound_enabled));
}

static bool TieProfile_FlightProfilesEqual(const TieFlightProfile* left, const TieFlightProfile* right) {
	return left->version == right->version && left->model_source == right->model_source &&
		   left->tie98_original_renderer == right->tie98_original_renderer &&
		   left->update_rate == right->update_rate &&
		   left->player_engine_sound_enabled == right->player_engine_sound_enabled &&
		   left->player_engine_sound_volume_percent == right->player_engine_sound_volume_percent;
}

void TieProfile_SetFrontend(TieFrontendProfileId id) {
	if (id == TIE_FRONTEND_PROFILE_TIE95 || id == TIE_FRONTEND_PROFILE_TIE98)
		selected_profile = id;
}

TieFrontendProfileId TieProfile_FrontendId(void) { return selected_profile; }

const TieFrontendProfile* TieProfile_Frontend(void) {
	return &profiles[selected_profile == TIE_FRONTEND_PROFILE_TIE98 ? 1 : 0];
}

void TieProfile_SetFlight(const TieFlightProfile* profile) {
	if (!TieProfile_FlightProfileValid(profile))
		return;
	selected_flight_profile = *profile;
	flight_profile_pending = false;
	tie98_original_renderer_pending = false;
}

const TieFlightProfile* TieProfile_Flight(void) { return &selected_flight_profile; }

bool TieProfile_RequestFlight(const TieFlightProfile* profile) {
	if (!TieProfile_FlightProfileValid(profile))
		return false;
	if (TieProfile_FlightProfilesEqual(profile, &selected_flight_profile)) {
		flight_profile_pending = false;
		return true;
	}
	pending_flight_profile = *profile;
	flight_profile_pending = true;
	return true;
}

bool TieProfile_ApplyPendingFlight(void) {
	if (!flight_profile_pending)
		return true;
	AeronVfs* flight_vfs = pending_flight_profile.version == TIE_GAME_VERSION_TIE98
							   ? TieStorage_Tie98FlightVfs()
							   : TieStorage_Tie95FlightVfs();
	if (!flight_vfs)
		return false;
	char error[512];
	if (!TieFlightAssets_SelectProfile(&pending_flight_profile, error, sizeof error)) {
		TieDiagnostics_Log(TIE_LOG_ERROR, "%s\n", error);
		return false;
	}
	TieStorage_SelectFlightVfs(flight_vfs);
	if (!pending_flight_profile.player_engine_sound_enabled && g_playerEngineSoundUpdateEnabled)
		TiePlayerEngineSound_StopActive();
	selected_flight_profile = pending_flight_profile;
	g_playerEngineSoundUpdateEnabled = selected_flight_profile.player_engine_sound_enabled ? 1 : 0;
	flight_profile_pending = false;
	tie98_original_renderer_pending = false;
	return true;
}

bool TieProfile_SetTie98Renderer(Tie98OriginalRenderer renderer) {
	if (selected_flight_profile.version != TIE_GAME_VERSION_TIE98 ||
		(renderer != TIE98_ORIGINAL_RENDERER_SOFTWARE && renderer != TIE98_ORIGINAL_RENDERER_D3D))
		return false;
	selected_flight_profile.tie98_original_renderer = renderer;
	tie98_original_renderer_pending = true;
	return true;
}

bool TieProfile_SetFlightUpdateRate(TieFlightUpdateRate update_rate) {
	if (update_rate != TIE_FLIGHT_UPDATE_RATE_NATIVE && update_rate != TIE_FLIGHT_UPDATE_RATE_TIE95 &&
		update_rate != TIE_FLIGHT_UPDATE_RATE_UNLOCKED)
		return false;
	selected_flight_profile.update_rate = update_rate;
	return true;
}

bool TieProfile_SetPlayerEngineSoundEnabled(bool enabled) {
	if (selected_flight_profile.version != TIE_GAME_VERSION_TIE98)
		return false;
	if (!enabled && g_playerEngineSoundUpdateEnabled)
		TiePlayerEngineSound_StopActive();
	selected_flight_profile.player_engine_sound_enabled = enabled;
	g_playerEngineSoundUpdateEnabled = enabled ? 1 : 0;
	return true;
}

bool TieProfile_RendererChangePending(void) { return tie98_original_renderer_pending; }

void TieProfile_CompleteRendererChange(void) { tie98_original_renderer_pending = false; }

bool TieProfile_SetPlayerEngineSoundVolume(int volume_percent) {
	if ((unsigned int)volume_percent > 100u)
		return false;
	selected_flight_profile.player_engine_sound_volume_percent = volume_percent;
	return true;
}

bool TieProfile_UsesTie98Logic(void) { return selected_flight_profile.version == TIE_GAME_VERSION_TIE98; }

bool TieProfile_UsesDx5(void) {
	return selected_profile == TIE_FRONTEND_PROFILE_TIE98 || TieProfile_UsesTie98Logic();
}
