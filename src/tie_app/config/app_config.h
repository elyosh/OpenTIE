#ifndef TIE_APP_CONFIG_H
#define TIE_APP_CONFIG_H

#include <stdbool.h>
#include <stddef.h>

#include "aeron/config_file.h"
#include "aeron/vfs.h"
#include "tie_remaster/flight/pbr.h"
#include "tie_remaster/flight/point_lights.h"
#include "tie_remaster/flight/render_config.h"
#include "tie_remaster/remaster.h"
#include "tie_runtime/audio/config.h"
#include "tie_runtime/diagnostics/diagnostics.h"
#include "tie_runtime/display/classic_display.h"
#include "tie_runtime/display/classic_framebuffer.h"
#include "tie_runtime/flight_assets/model_types.h"
#include "tie_runtime/input/actions.h"
#include "tie_runtime/input/controller_mapping.h"
#include "tie_runtime/input/input.h"
#include "tie_runtime/runtime/exports.h"
#include "tie_runtime/runtime/profile.h"
#include "tie_runtime/storage/storage.h"

#define TIE_GAME_DATA_PATH_MAX 1024
#define TIE_RESOURCE_PATH_MAX 512

typedef enum TieVersionSelection {
	TIE_VERSION_SELECTION_TIE95 = 0,
	TIE_VERSION_SELECTION_TIE98,
} TieVersionSelection;

typedef struct TieAppUiConfig {
	/* RESOURCE-relative basename; the atlas loader appends .fnt and .png. */
	char font[TIE_RESOURCE_PATH_MAX];
} TieAppUiConfig;

typedef struct TieAppVideoConfig {
	bool fullscreen;
	TieVideoOptions output;
} TieAppVideoConfig;

typedef struct TieAppLiveFlightOptions {
	bool aspect_correct_legacy_scenes;
	Tie98OriginalRenderer tie98_original_renderer;
	TieFlightUpdateRate update_rate;
	bool player_engine_sound_enabled;
	int player_engine_sound_volume_percent;
} TieAppLiveFlightOptions;

typedef struct TieAppLaunchOptions {
	char tie95_data[TIE_GAME_DATA_PATH_MAX];
	char tie98_data[TIE_GAME_DATA_PATH_MAX];
	char fluidsynth_soundfont_file[TIE_GAME_DATA_PATH_MAX];
	char sc55_rom_directory[TIE_GAME_DATA_PATH_MAX];
	TieVersionSelection frontend_version;
	TieVersionSelection flight_version;
	TieFlightModelSource model_source;
	TieMidiBackendKind midi_backend;
	bool sb16_filter_enabled;
	TieMusicSource music_source;
} TieAppLaunchOptions;

typedef struct TieAppConfig {
	char tie95_data[TIE_GAME_DATA_PATH_MAX];
	char tie98_data[TIE_GAME_DATA_PATH_MAX];
	char fluidsynth_soundfont_file[TIE_GAME_DATA_PATH_MAX];
	char sc55_rom_directory[TIE_GAME_DATA_PATH_MAX];
	TieVersionSelection frontend_version;
	bool aspect_correct_legacy_scenes;
	TieVersionSelection flight_version;
	TieMidiBackendKind midi_backend;
	bool sb16_filter_enabled;
	TieMusicSource music_source;
	int player_engine_sound_volume_percent;
	TieAppUiConfig ui;
	Tie98OriginalRenderer requested_tie98_original_renderer;
	TieFlightUpdateRate requested_flight_update_rate;
	TieFlightModelSource requested_model_source;
	bool player_engine_sound_enabled; /* requested YAML preference */
	float flight_model_smooth_angle_degrees;
	float flight_model_opt_emissive_strength;
	float flight_model_opt_projectile_emissive_strength;
	TieControllerOptions controller;
	TieKeyboardBindings keyboard;
	TieAppVideoConfig video;
	TieFlightRenderConfig render;
	TieFlightPbrConfig pbr;
	TieFlightPointLightParams point_lights;
} TieAppConfig;

typedef struct TieLaunchConfig {
	TieGameVersion frontend_version;
	TieFlightProfile flight_profile;
} TieLaunchConfig;

typedef struct TieAppConfigState {
	/* Parsed YAML only. Runtime-resolved launch state is kept separately. */
	TieAppConfig defaults;
	TieAppConfig requested;
	AeronConfigFile* shipped_document;
	AeronConfigFile* user_document;
	bool dirty;
} TieAppConfigState;

bool TieAppConfig_Load(AeronVfs* vfs, TieAppConfigState* state, char* error, size_t error_capacity);
void TieAppConfig_Destroy(TieAppConfigState* state);
TieAppConfigState* TieAppConfig_Current(void);

bool TieAppConfig_SetInstallation(TieAppConfigState* state, TieGameVersion version, const char* path,
								  char* error, size_t error_capacity);
void TieAppConfig_GetLiveFlightOptions(const TieAppConfig* config, TieAppLiveFlightOptions* out);
void TieAppConfig_GetLaunchOptions(const TieAppConfig* config, TieAppLaunchOptions* out);
bool TieAppConfig_SetLiveFlightOptions(TieAppConfigState* state, const TieAppLiveFlightOptions* options,
									   char* error, size_t error_capacity);
bool TieAppConfig_SetLaunchOptions(TieAppConfigState* state, const TieAppLaunchOptions* options, char* error,
								   size_t error_capacity);
bool TieAppConfig_ResolveFlightProfile(const TieAppConfig* config, bool has_tie95, bool has_tie98,
									   TieFlightProfile* out, char* error, size_t error_capacity);
bool TieAppConfig_ResolveLaunch(const TieAppConfig* config, bool has_tie95, bool has_tie98,
								TieLaunchConfig* out, char* error, size_t error_capacity);

bool TieAppConfig_SetVideo(TieAppConfigState* state, const TieAppVideoConfig* video, char* error,
						   size_t error_capacity);
bool TieAppConfig_RestoreVideo(TieAppConfigState* state, char* error, size_t error_capacity);
bool TieAppConfig_SetController(TieAppConfigState* state, const TieControllerOptions* controller, char* error,
								size_t error_capacity);
bool TieAppConfig_RestoreController(TieAppConfigState* state, char* error, size_t error_capacity);
bool TieAppConfig_SetShadows(TieAppConfigState* state, const AeronSceneShadowSettings* shadows, char* error,
							 size_t error_capacity);
bool TieAppConfig_RestoreShadows(TieAppConfigState* state, char* error, size_t error_capacity);
bool TieAppConfig_SetSsao(TieAppConfigState* state, const AeronSceneSsaoSettings* ssao, char* error,
						  size_t error_capacity);
bool TieAppConfig_RestoreSsao(TieAppConfigState* state, char* error, size_t error_capacity);
bool TieAppConfig_SetPbrGlobals(TieAppConfigState* state, const TieFlightPbrConfig* pbr, char* error,
								size_t error_capacity);
bool TieAppConfig_RestorePbrGlobals(TieAppConfigState* state, char* error, size_t error_capacity);
bool TieAppConfig_Save(AeronVfs* vfs, TieAppConfigState* state, char* error, size_t error_capacity);

#endif
