#ifndef TIE_RUNTIME_PROFILE_TYPES_H
#define TIE_RUNTIME_PROFILE_TYPES_H

#include <stdbool.h>

typedef enum TieGameVersion {
	TIE_GAME_VERSION_TIE95 = 0,
	TIE_GAME_VERSION_TIE98 = 1,
} TieGameVersion;

typedef TieGameVersion TieFrontendProfileId;
#define TIE_FRONTEND_PROFILE_TIE95 TIE_GAME_VERSION_TIE95
#define TIE_FRONTEND_PROFILE_TIE98 TIE_GAME_VERSION_TIE98

typedef enum Tie98OriginalRenderer {
	TIE98_ORIGINAL_RENDERER_SOFTWARE = 0,
	TIE98_ORIGINAL_RENDERER_D3D = 1,
} Tie98OriginalRenderer;

typedef enum TieFlightUpdateRate {
	TIE_FLIGHT_UPDATE_RATE_NATIVE = 0,
	TIE_FLIGHT_UPDATE_RATE_TIE95 = 1,
	TIE_FLIGHT_UPDATE_RATE_UNLOCKED = 2,
} TieFlightUpdateRate;

typedef enum TieFlightModelSource {
	TIE_FLIGHT_MODEL_SOURCE_ORIGINAL = 0,
	TIE_FLIGHT_MODEL_SOURCE_REMASTERED = 1,
} TieFlightModelSource;

typedef enum TieMusicSource {
	TIE_MUSIC_IMUSE = 0,
	TIE_MUSIC_TIE98 = 1,
} TieMusicSource;

typedef struct TieFlightProfile {
	TieGameVersion version;
	TieFlightModelSource model_source;
	Tie98OriginalRenderer tie98_original_renderer;
	TieFlightUpdateRate update_rate;
	bool player_engine_sound_enabled;
	int player_engine_sound_volume_percent;
} TieFlightProfile;

#endif
