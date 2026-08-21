#include "tie_runtime/flight_assets/service.h"

#include "tie_runtime/flight_assets/store.h"

#include <stdio.h>
#include <string.h>

typedef struct TieFlightAssetsState {
	TieFlightAssetStore tie95;
	TieFlightAssetStore tie98_original;
	TieFlightAssetStore tie98_remastered;
	TieFlightAssetStore* current;
	bool tie95_initialized;
	bool tie98_original_initialized;
	bool tie98_remastered_initialized;
	uint32_t profile_generation;
} TieFlightAssetsState;

static TieFlightAssetsState s_assets;

static TieFlightAssetStore* TieFlightAssets_StoreForProfile(const TieFlightProfile* profile) {
	if (!profile)
		return NULL;
	if (profile->version == TIE_GAME_VERSION_TIE95)
		return s_assets.tie95_initialized ? &s_assets.tie95 : NULL;
	if (profile->version != TIE_GAME_VERSION_TIE98)
		return NULL;
	if (profile->model_source == TIE_FLIGHT_MODEL_SOURCE_REMASTERED)
		return s_assets.tie98_remastered_initialized ? &s_assets.tie98_remastered : NULL;
	return s_assets.tie98_original_initialized ? &s_assets.tie98_original : NULL;
}

static void TieFlightAssets_SetError(char* error, size_t capacity, const char* message) {
	if (error && capacity)
		snprintf(error, capacity, "%s", message);
}

bool TieFlightAssets_Init(const TieFlightAssetsConfig* config, char* error, size_t error_capacity) {
	if (!config) {
		TieFlightAssets_SetError(error, error_capacity, "missing flight asset configuration");
		return false;
	}
	TieFlightAssets_Shutdown();
	if (config->tie95_vfs) {
		TieFlightAssetConfig source_config = config->source;
		source_config.profile.version = TIE_GAME_VERSION_TIE95;
		source_config.profile.model_source = TIE_FLIGHT_MODEL_SOURCE_ORIGINAL;
		source_config.profile.tie98_original_renderer = TIE98_ORIGINAL_RENDERER_SOFTWARE;
		if (!TieFlightAssetStore_Init(&s_assets.tie95, config->tie95_vfs, &source_config, error,
									  error_capacity))
			goto fail;
		s_assets.tie95_initialized = true;
	}
	if (config->tie98_vfs) {
		TieFlightAssetConfig original_config = config->source;
		original_config.profile.version = TIE_GAME_VERSION_TIE98;
		original_config.profile.model_source = TIE_FLIGHT_MODEL_SOURCE_ORIGINAL;
		if (!TieFlightAssetStore_Init(&s_assets.tie98_original, config->tie98_vfs, &original_config, error,
									  error_capacity))
			goto fail;
		s_assets.tie98_original_initialized = true;

		TieFlightAssetConfig remastered_config = config->source;
		remastered_config.profile.version = TIE_GAME_VERSION_TIE98;
		remastered_config.profile.model_source = TIE_FLIGHT_MODEL_SOURCE_REMASTERED;
		if (TieFlightAssetStore_Init(&s_assets.tie98_remastered, config->tie98_vfs, &remastered_config, error,
									 error_capacity)) {
			s_assets.tie98_remastered_initialized = true;
			TieFlightAssetStore_SetFallback(&s_assets.tie98_original, &s_assets.tie98_remastered);
			TieFlightAssetStore_SetFallback(&s_assets.tie98_remastered, &s_assets.tie98_original);
		} else if (error && error_capacity) {
			error[0] = '\0';
		}
	}
	if (s_assets.tie95_initialized && s_assets.tie98_original_initialized)
		TieFlightAssetStore_SetFallback(&s_assets.tie95, &s_assets.tie98_original);
	s_assets.current = TieFlightAssets_StoreForProfile(&config->source.profile);
	if (!s_assets.current) {
		TieFlightAssets_SetError(error, error_capacity, "selected flight model source is unavailable");
		goto fail;
	}
	s_assets.profile_generation = 1;
	return true;

fail:
	TieFlightAssets_Shutdown();
	return false;
}

void TieFlightAssets_Shutdown(void) {
	if (s_assets.tie95_initialized)
		TieFlightAssetStore_Release(&s_assets.tie95);
	if (s_assets.tie98_original_initialized)
		TieFlightAssetStore_Release(&s_assets.tie98_original);
	if (s_assets.tie98_remastered_initialized)
		TieFlightAssetStore_Release(&s_assets.tie98_remastered);
	memset(&s_assets, 0, sizeof s_assets);
}

bool TieFlightAssets_SelectProfile(const TieFlightProfile* profile, char* error, size_t error_capacity) {
	TieFlightAssetStore* store = TieFlightAssets_StoreForProfile(profile);
	if (!store) {
		TieFlightAssets_SetError(error, error_capacity, "requested flight model source is unavailable");
		return false;
	}
	s_assets.current = store;
	++s_assets.profile_generation;
	return true;
}

const TieFlightAssetSource* TieFlightAssets_CurrentSource(void) {
	return s_assets.current ? &s_assets.current->source : NULL;
}

TieFlightModelApi TieFlightAssets_ModelApi(void) {
	return s_assets.current ? TieFlightAssetStore_ModelApi(s_assets.current) : (TieFlightModelApi) { 0 };
}

TieFlightModelApi TieFlightAssets_Tie98OriginalModelApi(void) {
	return s_assets.tie98_original_initialized ? TieFlightAssetStore_ModelApi(&s_assets.tie98_original)
											   : (TieFlightModelApi) { 0 };
}

Tie98OptApi TieFlightAssets_NativeOptApi(void) {
	return s_assets.tie98_original_initialized ? TieFlightAssetStore_NativeOptApi(&s_assets.tie98_original)
											   : (Tie98OptApi) { 0 };
}

bool TieFlightAssets_Tie98Available(void) { return s_assets.tie98_original_initialized; }

uint32_t TieFlightAssets_ProfileGeneration(void) { return s_assets.profile_generation; }

void TieFlightAssets_ClearRuntimeCaches(void) {
	if (s_assets.tie95_initialized)
		TieFlightAssetStore_ClearRuntimeCaches(&s_assets.tie95);
	if (s_assets.tie98_original_initialized)
		TieFlightAssetStore_ClearRuntimeCaches(&s_assets.tie98_original);
	if (s_assets.tie98_remastered_initialized)
		TieFlightAssetStore_ClearRuntimeCaches(&s_assets.tie98_remastered);
}
