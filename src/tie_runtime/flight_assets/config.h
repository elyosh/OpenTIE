#ifndef TIE_FLIGHT_ASSET_CONFIG_H
#define TIE_FLIGHT_ASSET_CONFIG_H

#include <stdbool.h>

#include "tie_runtime/runtime/profile_types.h"

typedef struct TieFlightAssetConfig {
	TieFlightProfile profile;
	float smooth_angle_degrees;
	/* Runtime-converted TIE98 OPT rendering; ignored by TIE95 and cooked GLBs. */
	float opt_emissive_strength;
	float opt_projectile_emissive_strength;
} TieFlightAssetConfig;

static inline bool TieFlightAssetConfig_IsTie95(const TieFlightAssetConfig* config) {
	return config && config->profile.version == TIE_GAME_VERSION_TIE95;
}

static inline bool TieFlightAssetConfig_IsRemastered(const TieFlightAssetConfig* config) {
	return config && config->profile.model_source == TIE_FLIGHT_MODEL_SOURCE_REMASTERED;
}

#endif
