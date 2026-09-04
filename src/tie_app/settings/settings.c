/* Modern settings overlay and typed option-owner integration. */

#include "tie_app/settings/settings.h"

#include <stdio.h>
#include <string.h>

#include "aeron/scene/ui.h"
#include "aeron/scene/ui_file_picker.h"

#include "tie_app/midi_resources.h"
#include "tie_app/settings/audio_options.h"
#include "tie_app/settings/controller_page.h"
#include "tie_app/settings/flight_options.h"
#include "tie_app/settings/inflight_options.h"
#include "tie_app/settings/launch_options.h"
#include "tie_app/settings/video_options.h"
#include "tie_app/setup/installation_ui.h"
#include "tie_remaster/remaster.h"
#include "tie_runtime/input/controller_mapping.h"
#include "tie_runtime/runtime/runtime.h"

typedef enum TiePathPickerTarget {
	TIE_PATH_PICKER_TIE95,
	TIE_PATH_PICKER_TIE98,
	TIE_PATH_PICKER_FLUIDSYNTH_SOUNDFONT,
	TIE_PATH_PICKER_SC55_ROMS,
} TiePathPickerTarget;

typedef struct TieMidiBackendChoice {
	const char* label;
	TieMidiBackendKind kind;
} TieMidiBackendChoice;

static struct {
	AeronUiContext* ui;
	TieAppConfigState* config;
	bool available;
	bool has_tie95;
	bool has_tie98;
	bool open;
	bool captures_controller;
	int exit_confirmation_open;
	int page;
	char error[512];
	AeronUiFilePicker* path_picker;
	TiePathPickerTarget path_picker_target;
	TieControllerSettings controller;
} g_settings;

static bool TieSettings_RequestSelectedFlightProfile(const TieAppLaunchOptions* launch,
													 const TieAppLiveFlightOptions* flight, char* error,
													 size_t error_capacity) {
	TieAppConfig candidate = g_settings.config->requested;
	candidate.flight_version = launch->flight_version;
	candidate.requested_model_source = launch->model_source;
	candidate.requested_tie98_original_renderer = flight->tie98_original_renderer;
	candidate.requested_flight_update_rate = flight->update_rate;
	candidate.player_engine_sound_enabled = flight->player_engine_sound_enabled;
	candidate.player_engine_sound_volume_percent = flight->player_engine_sound_volume_percent;
	TieFlightProfile profile;
	if (!TieAppConfig_ResolveFlightProfile(&candidate, g_settings.has_tie95, g_settings.has_tie98, &profile,
										   error, error_capacity))
		return false;
	if (TieProfile_RequestFlight(&profile))
		return true;
	if (error && error_capacity)
		snprintf(error, error_capacity, "invalid flight profile request");
	return false;
}

static void TieSettings_SettingsReportError(const char* error) {
	snprintf(g_settings.error, sizeof g_settings.error, "%s",
			 error && error[0] ? error : "settings update failed");
	Aeron_LogError("tie.config", "%s", g_settings.error);
}

static bool TieSettings_ApplyVideoOptions(const TieAppVideoConfig* previous,
										  const TieAppVideoConfig* requested, void* user, char* error,
										  size_t error_capacity) {
	(void)user;
	if (previous->fullscreen != requested->fullscreen && !Aeron_SetFullscreen(requested->fullscreen)) {
		snprintf(error, error_capacity, "could not change fullscreen mode");
		return false;
	}
	if (!TieRemaster_ApplyVideoOptions(&requested->output)) {
		if (previous->fullscreen != requested->fullscreen)
			(void)Aeron_SetFullscreen(previous->fullscreen);
		snprintf(error, error_capacity, "could not apply video options");
		return false;
	}
	return true;
}

static bool TieSettings_PersistVideoOptions(const TieAppVideoConfig* options, void* user, char* error,
											size_t error_capacity) {
	TieAppConfigState* config = (TieAppConfigState*)user;
	const TieAppVideoConfig* defaults = &config->defaults.video;
	const bool use_defaults = options->fullscreen == defaults->fullscreen &&
							  options->output.hdr == defaults->output.hdr &&
							  options->output.sdr_content_gamma == defaults->output.sdr_content_gamma &&
							  options->output.paper_white_auto == defaults->output.paper_white_auto &&
							  options->output.paper_white_nits == defaults->output.paper_white_nits;
	/* Render-quality fields share the transactional video-options flush. */
	const bool use_quality_defaults =
		options->output.ssao_quality == defaults->output.ssao_quality &&
		options->output.shadows_enabled == defaults->output.shadows_enabled &&
		options->output.shadow_atlas_size == defaults->output.shadow_atlas_size &&
		options->output.fsr_mode == defaults->output.fsr_mode &&
		options->output.fsr_sharpness == defaults->output.fsr_sharpness &&
		options->output.motion_blur_quality == defaults->output.motion_blur_quality &&
		options->output.motion_blur_shutter == defaults->output.motion_blur_shutter &&
		options->output.msaa_samples == defaults->output.msaa_samples &&
		options->output.starfield_style == defaults->output.starfield_style;
	return use_defaults && use_quality_defaults
			   ? TieAppConfig_RestoreVideo(config, error, error_capacity)
			   : TieAppConfig_SetVideo(config, options, error, error_capacity);
}

static bool TieSettings_ApplyFlightOptions(const TieAppLiveFlightOptions* previous,
										   const TieAppLiveFlightOptions* requested, void* user, char* error,
										   size_t error_capacity) {
	(void)user;
	const bool tie98_active = TieProfile_Flight()->version == TIE_GAME_VERSION_TIE98;
	if (previous->aspect_correct_legacy_scenes != requested->aspect_correct_legacy_scenes &&
		!TieRemaster_SetAspectCorrectLegacyScenes(requested->aspect_correct_legacy_scenes)) {
		snprintf(error, error_capacity, "could not apply frontend aspect correction");
		return false;
	}
	if (tie98_active && previous->tie98_original_renderer != requested->tie98_original_renderer &&
		!TieProfile_SetTie98Renderer(requested->tie98_original_renderer)) {
		(void)TieRemaster_SetAspectCorrectLegacyScenes(previous->aspect_correct_legacy_scenes);
		snprintf(error, error_capacity, "could not request the TIE98 original renderer");
		return false;
	}
	if (previous->update_rate != requested->update_rate &&
		!TieProfile_SetFlightUpdateRate(requested->update_rate)) {
		if (previous->tie98_original_renderer != requested->tie98_original_renderer)
			(void)TieProfile_SetTie98Renderer(previous->tie98_original_renderer);
		(void)TieRemaster_SetAspectCorrectLegacyScenes(previous->aspect_correct_legacy_scenes);
		snprintf(error, error_capacity, "could not apply the flight update rate");
		return false;
	}
	if (tie98_active && previous->player_engine_sound_enabled != requested->player_engine_sound_enabled &&
		!TieProfile_SetPlayerEngineSoundEnabled(requested->player_engine_sound_enabled)) {
		if (previous->update_rate != requested->update_rate)
			(void)TieProfile_SetFlightUpdateRate(previous->update_rate);
		if (previous->tie98_original_renderer != requested->tie98_original_renderer)
			(void)TieProfile_SetTie98Renderer(previous->tie98_original_renderer);
		(void)TieRemaster_SetAspectCorrectLegacyScenes(previous->aspect_correct_legacy_scenes);
		snprintf(error, error_capacity, "could not apply the player engine sound setting");
		return false;
	}
	if (previous->player_engine_sound_volume_percent != requested->player_engine_sound_volume_percent &&
		!TieProfile_SetPlayerEngineSoundVolume(requested->player_engine_sound_volume_percent)) {
		if (tie98_active && previous->player_engine_sound_enabled != requested->player_engine_sound_enabled)
			(void)TieProfile_SetPlayerEngineSoundEnabled(previous->player_engine_sound_enabled);
		if (previous->update_rate != requested->update_rate)
			(void)TieProfile_SetFlightUpdateRate(previous->update_rate);
		if (tie98_active && previous->tie98_original_renderer != requested->tie98_original_renderer)
			(void)TieProfile_SetTie98Renderer(previous->tie98_original_renderer);
		(void)TieRemaster_SetAspectCorrectLegacyScenes(previous->aspect_correct_legacy_scenes);
		snprintf(error, error_capacity, "could not apply the player engine sound volume");
		return false;
	}
	return true;
}

static bool TieSettings_PersistFlightOptions(const TieAppLiveFlightOptions* options, void* user, char* error,
											 size_t error_capacity) {
	return TieAppConfig_SetLiveFlightOptions((TieAppConfigState*)user, options, error, error_capacity);
}

static bool TieSettings_ApplyAudioOptions(const TieAppLiveAudioOptions* previous,
										  const TieAppLiveAudioOptions* requested, void* user, char* error,
										  size_t error_capacity) {
	(void)previous;
	(void)user;
	if (TieRuntime_SetMusicDuckingVolumePercent(requested->music_ducking_volume_percent))
		return true;
	snprintf(error, error_capacity, "could not apply the music ducking volume");
	return false;
}

static bool TieSettings_PersistAudioOptions(const TieAppLiveAudioOptions* options, void* user, char* error,
											size_t error_capacity) {
	return TieAppConfig_SetLiveAudioOptions((TieAppConfigState*)user, options, error, error_capacity);
}

static bool TieSettings_PersistLaunchOptions(const TieAppLaunchOptions* options, void* user, char* error,
											 size_t error_capacity) {
	if (options->music_source == TIE_MUSIC_IMUSE) {
		if (options->midi_backend == TIE_MIDI_BACKEND_FLUIDSYNTH &&
			!TieMidiResources_Soundfontvalidate(options->fluidsynth_soundfont_file, error, error_capacity))
			return false;
		if (options->midi_backend == TIE_MIDI_BACKEND_SC55 &&
			!TieMidiResources_Sc55RomDirectoryvalidate(options->sc55_rom_directory, error, error_capacity))
			return false;
	}
	return TieAppConfig_SetLaunchOptions((TieAppConfigState*)user, options, error, error_capacity);
}

static void TieSettings_LaunchRestartNotice(AeronUiContext* ui) {
	if (TieLaunchOptions_RestartRequired())
		AeronUi_Error(ui, "Some changes will take effect after restarting the game.");
}

bool TieSettings_Init(TieUi* ui, TieAppConfigState* config, bool has_tie95, bool has_tie98, char* error,
					  size_t error_capacity) {
	TieAppLiveFlightOptions flight;
	TieAppLiveAudioOptions audio;
	TieInflightOptions inflight;
	TieAppLaunchOptions launch;
	memset(&g_settings, 0, sizeof g_settings);
	g_settings.config = config;
	g_settings.ui = TieUi_Context(ui);
	g_settings.has_tie95 = has_tie95;
	g_settings.has_tie98 = has_tie98;
	if (!g_settings.ui || !config) {
		snprintf(error, error_capacity, "invalid settings menu initialization arguments");
		return false;
	}
	g_settings.path_picker = AeronUiFilePicker_Create();
	if (!g_settings.path_picker) {
		snprintf(error, error_capacity, "could not create the path picker");
		return false;
	}
	TieAppConfig_GetLiveFlightOptions(&config->requested, &flight);
	TieAppConfig_GetLiveAudioOptions(&config->requested, &audio);
	TieInflightOptions_Get(&inflight);
	TieAppConfig_GetLaunchOptions(&config->requested, &launch);
	if (!TieVideoOptions_Configure(&config->defaults.video, &config->requested.video,
								   TieSettings_ApplyVideoOptions, TieSettings_PersistVideoOptions, config) ||
		!TieFlightOptions_Configure(&flight, TieSettings_ApplyFlightOptions, TieSettings_PersistFlightOptions,
									config) ||
		!TieAudioOptions_Configure(&audio, TieSettings_ApplyAudioOptions, TieSettings_PersistAudioOptions,
								   config) ||
		!TieInflightSettings_Configure(&inflight) ||
		!TieLaunchOptions_Configure(&launch, TieSettings_PersistLaunchOptions, config)) {
		snprintf(error, error_capacity, "could not configure modern settings state");
		TieVideoOptions_Shutdown();
		TieFlightOptions_Shutdown();
		TieAudioOptions_Shutdown();
		TieInflightSettings_Shutdown();
		TieLaunchOptions_Shutdown();
		AeronUiFilePicker_Destroy(g_settings.path_picker);
		g_settings.path_picker = NULL;
		return false;
	}
	g_settings.available = true;
	return true;
}

void TieSettings_Shutdown(void) {
	if (g_settings.open) {
		TieControllerSettings_CancelCapture(&g_settings.controller, g_settings.ui);
		TieControllerMapping_Resume();
	}
	TieVideoOptions_Shutdown();
	TieFlightOptions_Shutdown();
	TieAudioOptions_Shutdown();
	TieInflightSettings_Shutdown();
	TieLaunchOptions_Shutdown();
	AeronUiFilePicker_Destroy(g_settings.path_picker);
	memset(&g_settings, 0, sizeof g_settings);
}

bool TieSettings_Available(void) { return g_settings.available; }
bool TieSettings_Open(void) { return g_settings.open; }
bool TieSettings_CapturesController(void) { return g_settings.open && g_settings.captures_controller; }

bool TieSettings_Flush(char* error, size_t error_capacity) {
	if (!TieVideoOptions_Flush(error, error_capacity) || !TieFlightOptions_Flush(error, error_capacity) ||
		!TieAudioOptions_Flush(error, error_capacity) || !TieInflightSettings_Flush(error, error_capacity) ||
		!TieLaunchOptions_Flush(error, error_capacity))
		return false;
	return !g_settings.open ||
		   TieControllerSettings_Commit(&g_settings.controller, g_settings.config, error, error_capacity);
}

static bool TieSettings_SettingsClose(void) {
	if (!g_settings.open)
		return true;
	char error[512];
	if (!TieSettings_Flush(error, sizeof error) ||
		!TieAppConfig_Save(Aeron_GetVfs(), g_settings.config, error, sizeof error)) {
		TieSettings_SettingsReportError(error);
		return false;
	}
	TieControllerSettings_CancelCapture(&g_settings.controller, g_settings.ui);
	AeronUiFilePicker_Cancel(g_settings.path_picker);
	g_settings.captures_controller = false;
	g_settings.exit_confirmation_open = 0;
	g_settings.open = false;
	TieControllerMapping_Resume();
	return true;
}

void TieSettings_Show(void) {
	if (!g_settings.available || g_settings.open)
		return;
	g_settings.open = true;
	g_settings.captures_controller = false;
	g_settings.error[0] = '\0';
	TieControllerSettings_Open(&g_settings.controller, g_settings.config);
	TieControllerMapping_Suspend();
}

void TieSettings_Toggle(void) {
	if (!g_settings.available)
		return;
	if (g_settings.open)
		(void)TieSettings_SettingsClose();
	else
		TieSettings_Show();
}

static void TieSettings_VideoPage(AeronUiContext* ui) {
	TieAppVideoConfig options;
	TieVideoOptions_Get(&options);
	bool changed = false;
	AeronUi_Header(ui, "Display");
	int fullscreen = options.fullscreen;
	if (AeronUi_Toggle(ui, "Fullscreen", &fullscreen)) {
		options.fullscreen = fullscreen != 0;
		changed = true;
	}
	int hdr = options.output.hdr;
	if (AeronUi_Toggle(ui, "HDR Output", &hdr)) {
		options.output.hdr = hdr != 0;
		changed = true;
	}
	AeronUi_Spacer(ui, 8.0f);
	AeronUi_Header(ui, "HDR Presentation");
	static const char* const gamma_labels[] = { "2.2", "2.4", "sRGB" };
	int gamma = options.output.sdr_content_gamma;
#if defined(__APPLE__)
	const int tone_mapping_enabled = 0;
#else
	const int tone_mapping_enabled = Aeron_OutputHdrEnabled();
#endif
	/* sRGB is displayable for platform/config state, but adjustments
	 * intentionally cycle through only the two power curves. */
	const int gamma_count = gamma == TIE_SDR_CONTENT_GAMMA_SRGB ? 3 : 2;
	if (AeronUi_SelectorEnabled(ui, "SDR Content Gamma", &gamma, gamma_labels, gamma_count,
								tone_mapping_enabled)) {
		options.output.sdr_content_gamma = (TieSdrContentGamma)gamma;
		changed = true;
	}

	static const float paper_white_values[] = { 0.0f, 100.0f, 150.0f, 200.0f, 250.0f, 300.0f, 400.0f };
	static const char* const paper_white_labels[] = { "Auto",     "100 nits", "150 nits", "200 nits",
													  "250 nits", "300 nits", "400 nits" };
	int paper_white = 0;
	if (!options.output.paper_white_auto) {
		paper_white = 1;
		float best_distance = options.output.paper_white_nits - paper_white_values[paper_white];
		if (best_distance < 0.0f)
			best_distance = -best_distance;
		for (int index = 2; index < 7; ++index) {
			float distance = options.output.paper_white_nits - paper_white_values[index];
			if (distance < 0.0f)
				distance = -distance;
			if (distance < best_distance) {
				paper_white = index;
				best_distance = distance;
			}
		}
	}
	if (AeronUi_SelectorEnabled(ui, "HDR Paper White", &paper_white, paper_white_labels, 7,
								tone_mapping_enabled)) {
		options.output.paper_white_auto = paper_white == 0;
		options.output.paper_white_nits = paper_white_values[paper_white];
		changed = true;
	}
	AeronUi_Spacer(ui, 8.0f);
	AeronUi_Header(ui, "Flight Rendering");
	static const char* const starfield_labels[] = { "TIE95", "TIE98" };
	int starfield_style = options.output.starfield_style;
	if (AeronUi_Selector(ui, "Starfield Style", &starfield_style, starfield_labels, 2)) {
		options.output.starfield_style = (TieFlightStarfieldStyle)starfield_style;
		changed = true;
	}
	static const char* const quality_labels[] = { "Off", "Low", "High" };
	int ssao_quality = options.output.ssao_quality;
	if (AeronUi_Selector(ui, "SSAO Quality", &ssao_quality, quality_labels, 3)) {
		options.output.ssao_quality = ssao_quality;
		changed = true;
	}
	static const int shadow_atlas_values[] = { 4096, 8192 };
	static const char* const shadow_quality_labels[] = { "Standard", "High" };
	int shadow_quality = options.output.shadow_atlas_size >= 8192 ? 1 : 0;
	if (AeronUi_Selector(ui, "Shadow Quality", &shadow_quality, shadow_quality_labels, 2)) {
		options.output.shadows_enabled = true;
		options.output.shadow_atlas_size = shadow_atlas_values[shadow_quality];
		changed = true;
	}

	static const int fsr_values[] = { TIE_FLIGHT_TEMPORAL_OFF, TIE_FLIGHT_TEMPORAL_PERFORMANCE,
									  TIE_FLIGHT_TEMPORAL_BALANCED, TIE_FLIGHT_TEMPORAL_QUALITY,
									  TIE_FLIGHT_TEMPORAL_NATIVE_AA };
	static const char* const fsr_labels[] = { "Off", "Performance", "Balanced", "Quality", "Native AA" };
	int fsr_index = 0;
	while (fsr_index < 4 && fsr_values[fsr_index] != options.output.fsr_mode)
		++fsr_index;
	if (AeronUi_Selector(ui, "FSR Upscaling", &fsr_index, fsr_labels, 5)) {
		options.output.fsr_mode = fsr_values[fsr_index];
		if (options.output.fsr_mode != TIE_FLIGHT_TEMPORAL_OFF)
			options.output.msaa_samples = 1;
		changed = true;
	}

	static const int sample_values[] = { 1, 2, 4, 8 };
	static const char* const sample_labels[] = { "Off", "2x", "4x", "8x" };
	int sample_index = 0;
	while (sample_index < 3 && sample_values[sample_index] != options.output.msaa_samples)
		++sample_index;
	if (AeronUi_Selector(ui, "MSAA", &sample_index, sample_labels, 4)) {
		options.output.msaa_samples = sample_values[sample_index];
		if (options.output.msaa_samples > 1)
			options.output.fsr_mode = TIE_FLIGHT_TEMPORAL_OFF;
		changed = true;
	}

	static const char* const motion_blur_labels[] = { "Off", "Low Quality", "High Quality" };
	int blur = options.output.motion_blur_quality;
	if (AeronUi_Selector(ui, "Motion Blur", &blur, motion_blur_labels, 3)) {
		options.output.motion_blur_quality = blur;
		changed = true;
	}
	if (blur > 0) {
		int amount = (int)(options.output.motion_blur_shutter * 100.0f + 0.5f);
		if (amount < 0)
			amount = 0;
		if (amount > 100)
			amount = 100;
		if (AeronUi_SliderInt(ui, "Motion Blur Amount", &amount, 0, 100, 10, "%d%%")) {
			options.output.motion_blur_shutter = (float)amount / 100.0f;
			changed = true;
		}
	}
	if (changed) {
		char error[512];
		if (!TieVideoOptions_Set(&options, error, sizeof error))
			TieSettings_SettingsReportError(error);
	}
	AeronUi_Spacer(ui, 8.0f);
	if (AeronUi_Button(ui, "Restore Defaults")) {
		char error[512];
		if (!TieVideoOptions_RestoreDefaults(error, sizeof error))
			TieSettings_SettingsReportError(error);
	}
}

static void TieSettings_OpenInstallationPicker(TieGameVersion version, const char* initial_path) {
	const char* version_name = version == TIE_GAME_VERSION_TIE98 ? "TIE98" : "TIE95";
	char title[64];
	char instructions[128];
	char error[512];
	snprintf(title, sizeof title, "SELECT %s INSTALLATION", version_name);
	snprintf(instructions, sizeof instructions, "Select the original %s installation folder.", version_name);
	const AeronUiFilePickerDesc desc = {
		.mode = AERON_UI_FILE_PICKER_SELECT_DIRECTORY,
		.title = title,
		.instructions = instructions,
		.accept_label = "Use This Folder",
		.cancel_label = "Cancel",
		.initial_path = initial_path && initial_path[0] ? initial_path : NULL,
	};
	if (!AeronUiFilePicker_Open(g_settings.path_picker, &desc, error, sizeof error)) {
		TieSettings_SettingsReportError(error);
		return;
	}
	g_settings.path_picker_target =
		version == TIE_GAME_VERSION_TIE98 ? TIE_PATH_PICKER_TIE98 : TIE_PATH_PICKER_TIE95;
}

static const char* TieSettings_FileParentDirectory(const char* path, char* directory, size_t capacity) {
	if (!path || !path[0] || !directory || capacity == 0)
		return NULL;
	snprintf(directory, capacity, "%s", path);
	char* slash = strrchr(directory, '/');
	char* backslash = strrchr(directory, '\\');
	char* separator = slash;
	if (!separator || (backslash && backslash > separator))
		separator = backslash;
	if (!separator) {
		snprintf(directory, capacity, ".");
		return directory;
	}
	if (separator == directory || (separator == directory + 2 && directory[1] == ':'))
		separator[1] = '\0';
	else
		*separator = '\0';
	return directory;
}

static void TieSettings_OpenSoundfontPicker(const char* initial_path) {
	static const char* const extensions[] = { "sf2" };
	static const AeronUiFileFilter filters[] = {
		{ .label = "SoundFont files (.sf2)", .extensions = extensions, .extension_count = 1 },
	};
	char initial_directory[TIE_GAME_DATA_PATH_MAX];
	char error[512];
	const AeronUiFilePickerDesc desc = {
		.mode = AERON_UI_FILE_PICKER_OPEN_FILE,
		.title = "SELECT SOUNDFONT",
		.instructions = "Select a SoundFont 2 file for FluidSynth.",
		.accept_label = "Use This File",
		.cancel_label = "Cancel",
		.initial_path =
			TieSettings_FileParentDirectory(initial_path, initial_directory, sizeof initial_directory),
		.filters = filters,
		.filter_count = sizeof filters / sizeof filters[0],
	};
	if (!AeronUiFilePicker_Open(g_settings.path_picker, &desc, error, sizeof error)) {
		TieSettings_SettingsReportError(error);
		return;
	}
	g_settings.path_picker_target = TIE_PATH_PICKER_FLUIDSYNTH_SOUNDFONT;
}

static void TieSettings_OpenSc55Picker(const char* initial_path) {
	char error[512];
	const AeronUiFilePickerDesc desc = {
		.mode = AERON_UI_FILE_PICKER_SELECT_DIRECTORY,
		.title = "SELECT SC-55 ROM DIRECTORY",
		.instructions = "Select the folder containing the original SC-55 ROM dumps.",
		.accept_label = "Use This Folder",
		.cancel_label = "Cancel",
		.initial_path = initial_path && initial_path[0] ? initial_path : NULL,
	};
	if (!AeronUiFilePicker_Open(g_settings.path_picker, &desc, error, sizeof error)) {
		TieSettings_SettingsReportError(error);
		return;
	}
	g_settings.path_picker_target = TIE_PATH_PICKER_SC55_ROMS;
}

static void TieSettings_DrawPathPicker(AeronUiContext* ui) {
	char selected_path[TIE_GAME_DATA_PATH_MAX];
	char error[512] = { 0 };
	const AeronUiFilePickerResult result = AeronUiFilePicker_Draw(g_settings.path_picker, ui, selected_path,
																  sizeof selected_path, error, sizeof error);
	if (result == AERON_UI_FILE_PICKER_ERROR) {
		TieSettings_SettingsReportError(error);
		return;
	}
	if (result != AERON_UI_FILE_PICKER_SELECTED)
		return;
	if (g_settings.path_picker_target == TIE_PATH_PICKER_FLUIDSYNTH_SOUNDFONT &&
		!TieMidiResources_Soundfontvalidate(selected_path, error, sizeof error)) {
		TieSettings_SettingsReportError(error);
		return;
	}
	if (g_settings.path_picker_target == TIE_PATH_PICKER_SC55_ROMS &&
		!TieMidiResources_Sc55RomDirectoryvalidate(selected_path, error, sizeof error)) {
		TieSettings_SettingsReportError(error);
		return;
	}

	TieAppLaunchOptions launch;
	TieLaunchOptions_Get(&launch);
	char* destination = launch.tie95_data;
	if (g_settings.path_picker_target == TIE_PATH_PICKER_TIE98)
		destination = launch.tie98_data;
	else if (g_settings.path_picker_target == TIE_PATH_PICKER_FLUIDSYNTH_SOUNDFONT)
		destination = launch.fluidsynth_soundfont_file;
	else if (g_settings.path_picker_target == TIE_PATH_PICKER_SC55_ROMS)
		destination = launch.sc55_rom_directory;
	snprintf(destination, TIE_GAME_DATA_PATH_MAX, "%s", selected_path);
	if (!TieLaunchOptions_Set(&launch))
		TieSettings_SettingsReportError("invalid selected path");
	else
		g_settings.error[0] = '\0';
}

static void TieSettings_GamePage(AeronUiContext* ui) {
	static const char* const versions[] = { "TIE95", "TIE98" };
	static const char* const renderers[] = { "Software", "Direct3D" };
	static const char* const model_sources[] = { "Original", "Remastered" };
	static const char* const update_rates[] = { "Native", "TIE95", "Unlocked" };
	static const char* const off_on[] = { "Off", "On" };
	static const char* const vulnerability[] = { "Vulnerable", "Invulnerable" };
	static const char* const ammunition[] = { "Limited", "Unlimited" };
	TieAppLaunchOptions launch;
	TieAppLiveFlightOptions flight;
	TieInflightOptions inflight;
	TieLaunchOptions_Get(&launch);
	TieFlightOptions_Get(&flight);
	TieInflightSettings_Get(&inflight);
	bool launch_changed = false;
	bool flight_changed = false;
	AeronUi_Header(ui, "Original Installations");
	uint32_t path_result = TieInstallation_PathRow(ui, "TIE95", launch.tie95_data, sizeof launch.tie95_data,
												   AERON_UI_INPUT_TEXT_NONE);
	if (path_result & AERON_UI_INPUT_TEXT_ACTION_ACTIVATED)
		TieSettings_OpenInstallationPicker(TIE_GAME_VERSION_TIE95, launch.tie95_data);
	launch_changed |= (path_result & AERON_UI_INPUT_TEXT_CHANGED) != 0;
	path_result = TieInstallation_PathRow(ui, "TIE98", launch.tie98_data, sizeof launch.tie98_data,
										  AERON_UI_INPUT_TEXT_NONE);
	if (path_result & AERON_UI_INPUT_TEXT_ACTION_ACTIVATED)
		TieSettings_OpenInstallationPicker(TIE_GAME_VERSION_TIE98, launch.tie98_data);
	launch_changed |= (path_result & AERON_UI_INPUT_TEXT_CHANGED) != 0;
	AeronUi_Spacer(ui, 8.0f);
	AeronUi_Header(ui, "Version Selection");
	int frontend = launch.frontend_version;
	int flight_version = launch.flight_version;
	int model_source = launch.model_source;
	int renderer = flight.tie98_original_renderer;
	int update_rate = flight.update_rate;
	launch_changed |= AeronUi_Selector(ui, "Cutscenes and Menus", &frontend, versions, 2);
	launch.frontend_version = (TieVersionSelection)frontend;
	if (launch.frontend_version != TIE_VERSION_SELECTION_TIE95) {
		int aspect = flight.aspect_correct_legacy_scenes;
		if (AeronUi_Toggle(ui, "Aspect-correct 320x200 Scenes", &aspect)) {
			flight.aspect_correct_legacy_scenes = aspect != 0;
			flight_changed = true;
		}
	}
	if (AeronUi_Selector(ui, "Flight Engine", &flight_version, versions, 2)) {
		const TieVersionSelection selected = (TieVersionSelection)flight_version;
		const bool installed =
			selected == TIE_VERSION_SELECTION_TIE98 ? g_settings.has_tie98 : g_settings.has_tie95;
		if (installed) {
			launch.flight_version = selected;
			launch_changed = true;
		} else {
			TieSettings_SettingsReportError(selected == TIE_VERSION_SELECTION_TIE98
												? "The TIE98 installation was not available at startup."
												: "The TIE95 installation was not available at startup.");
		}
	}
	if (launch.flight_version != TIE_VERSION_SELECTION_TIE95) {
		if (AeronUi_Selector(ui, "Classic Look", &renderer, renderers, 2)) {
			flight.tie98_original_renderer = (Tie98OriginalRenderer)renderer;
			flight_changed = true;
		}
		/*launch_changed |= AeronUi_Selector(ui, "Flight Models", &model_source, model_sources, 2);
		launch.model_source = (TieFlightModelSource)model_source;*/
	}
	if (AeronUi_Selector(ui, "Update Rate", &update_rate, update_rates, 3)) {
		flight.update_rate = (TieFlightUpdateRate)update_rate;
		flight_changed = true;
	}
	bool launch_accepted = true;
	if (launch_changed && !TieLaunchOptions_Set(&launch)) {
		TieSettings_SettingsReportError("invalid launch settings");
		launch_accepted = false;
	}
	bool flight_accepted = true;
	if (flight_changed) {
		char error[512];
		if (!TieFlightOptions_Set(&flight, error, sizeof error)) {
			TieSettings_SettingsReportError(error);
			flight_accepted = false;
		}
	}
	if ((launch_changed || flight_changed) && launch_accepted && flight_accepted) {
		char error[512] = { 0 };
		if (!TieSettings_RequestSelectedFlightProfile(&launch, &flight, error, sizeof error))
			TieSettings_SettingsReportError(error[0] ? error : "could not queue the selected flight engine");
	}
	AeronUi_Help(ui, "Flight-engine and update-rate changes take effect on the next flight.");
	TieSettings_LaunchRestartNotice(ui);

	AeronUi_Spacer(ui, 8.0f);
	AeronUi_Header(ui, "Flight Gameplay");
	bool inflight_changed = false;
	int collision = inflight.starfighter_collision_damage;
	if (AeronUi_Selector(ui, "Starfighter Collision Damage", &collision, off_on, 2)) {
		inflight.starfighter_collision_damage = collision != 0;
		inflight_changed = true;
	}
	int invulnerable = inflight.player_invulnerable;
	if (AeronUi_Selector(ui, "Player Spacecraft", &invulnerable, vulnerability, 2)) {
		inflight.player_invulnerable = invulnerable != 0;
		inflight_changed = true;
	}
	int unlimited = inflight.unlimited_ammunition;
	if (AeronUi_Selector(ui, "Ammunition", &unlimited, ammunition, 2)) {
		inflight.unlimited_ammunition = unlimited != 0;
		inflight_changed = true;
	}
	if (inflight_changed) {
		char error[512];
		if (!TieInflightSettings_Set(&inflight, error, sizeof error))
			TieSettings_SettingsReportError(error);
	}
}

static void TieSettings_AudioPage(AeronUiContext* ui) {
	static const char* const music_sources[] = { "iMUSE", "CD Music" };
	static const TieMidiBackendChoice all_midi_backends[] = {
		{ "FluidSynth", TIE_MIDI_BACKEND_FLUIDSYNTH },
		{ "OPL3", TIE_MIDI_BACKEND_FM4_OPL3 },
		{ "Roland SC-55", TIE_MIDI_BACKEND_SC55 },
	};
	TieAppLiveFlightOptions flight;
	TieAppLiveAudioOptions audio;
	TieInflightOptions inflight;
	TieAppLaunchOptions launch;
	TieFlightOptions_Get(&flight);
	TieAudioOptions_Get(&audio);
	TieInflightSettings_Get(&inflight);
	TieLaunchOptions_Get(&launch);
	bool launch_changed = false;
	AeronUi_Header(ui, "Audio Levels");
	bool inflight_changed = false;
	int sound_effects_volume = inflight.sound_effects_volume;
	if (AeronUi_SliderInt(ui, "Sound Effects Volume", &sound_effects_volume, 0, 16, 1, "%d / 16")) {
		inflight.sound_effects_volume = (uint8_t)sound_effects_volume;
		inflight_changed = true;
	}
	int music_volume = inflight.music_volume;
	if (AeronUi_SliderInt(ui, "Music Volume", &music_volume, 0, 16, 1, "%d / 16")) {
		inflight.music_volume = (uint8_t)music_volume;
		inflight_changed = true;
	}
	int speech_volume = inflight.speech_volume;
	if (AeronUi_SliderInt(ui, "Speech Volume", &speech_volume, 0, 16, 1, "%d / 16")) {
		inflight.speech_volume = (uint8_t)speech_volume;
		inflight_changed = true;
	}
	int ducking_volume = audio.music_ducking_volume_percent;
	if (AeronUi_SliderInt(ui, "Music Volume During Speech", &ducking_volume, 0, 100, 1, "%d%%")) {
		char error[512];
		audio.music_ducking_volume_percent = ducking_volume;
		if (!TieAudioOptions_Set(&audio, error, sizeof error))
			TieSettings_SettingsReportError(error);
	}
	if (inflight_changed) {
		char error[512];
		if (!TieInflightSettings_Set(&inflight, error, sizeof error))
			TieSettings_SettingsReportError(error);
	}
	AeronUi_Spacer(ui, 8.0f);
	AeronUi_Header(ui, "Music");
	int music_source = launch.music_source == TIE_MUSIC_TIE98 ? 1 : 0;
	const int music_source_count = g_settings.has_tie98 ? 2 : 1;
	if (music_source >= music_source_count) {
		music_source = 0;
		launch.music_source = TIE_MUSIC_IMUSE;
		launch_changed = true;
	}
	if (AeronUi_Selector(ui, "Music Source", &music_source, music_sources, music_source_count)) {
		launch.music_source = music_source == 1 ? TIE_MUSIC_TIE98 : TIE_MUSIC_IMUSE;
		launch_changed = true;
	}
	if (launch.music_source == TIE_MUSIC_IMUSE) {
		const char* midi_backend_labels[3];
		TieMidiBackendKind midi_backend_kinds[3];
		int midi_backend_count = 0;
		int midi_backend = 0;
		for (size_t index = 0; index < sizeof all_midi_backends / sizeof all_midi_backends[0]; ++index) {
			if (!TieMidiBackend_Available(all_midi_backends[index].kind))
				continue;
			midi_backend_labels[midi_backend_count] = all_midi_backends[index].label;
			midi_backend_kinds[midi_backend_count] = all_midi_backends[index].kind;
			if (launch.midi_backend == all_midi_backends[index].kind)
				midi_backend = midi_backend_count;
			++midi_backend_count;
		}
		if (AeronUi_Selector(ui, "Synthesizer", &midi_backend, midi_backend_labels, midi_backend_count)) {
			launch.midi_backend = midi_backend_kinds[midi_backend];
			launch_changed = true;
		}
		if (launch.midi_backend == TIE_MIDI_BACKEND_FLUIDSYNTH) {
			const uint32_t result = AeronUi_InputTextWithAction(
				ui, "SoundFont File", launch.fluidsynth_soundfont_file,
				sizeof launch.fluidsynth_soundfont_file, AERON_UI_INPUT_TEXT_NONE, "Browse...");
			if (result & AERON_UI_INPUT_TEXT_ACTION_ACTIVATED)
				TieSettings_OpenSoundfontPicker(launch.fluidsynth_soundfont_file);
			launch_changed |= (result & AERON_UI_INPUT_TEXT_CHANGED) != 0;
			AeronUi_Help(ui, "FluidSynth requires a SoundFont 2 file.");
		} else if (launch.midi_backend == TIE_MIDI_BACKEND_SC55) {
			const uint32_t result = AeronUi_InputTextWithAction(
				ui, "SC-55 ROM Directory", launch.sc55_rom_directory, sizeof launch.sc55_rom_directory,
				AERON_UI_INPUT_TEXT_NONE, "Browse...");
			if (result & AERON_UI_INPUT_TEXT_ACTION_ACTIVATED)
				TieSettings_OpenSc55Picker(launch.sc55_rom_directory);
			launch_changed |= (result & AERON_UI_INPUT_TEXT_CHANGED) != 0;
			AeronUi_Help(ui, "Requires SC-55 or SC-55mkII ROM dumps; prefers MKII when both are present.");
		}
	} else {
		AeronUi_Help(ui, "Uses the prerecorded soundtrack from the TIE98 installation.");
	}
	AeronUi_Spacer(ui, 8.0f);
	AeronUi_Header(ui, "Engine Sound");
	int engine_sound = flight.player_engine_sound_enabled;
	if (AeronUi_Toggle(ui, "Enabled", &engine_sound)) {
		char error[512];
		flight.player_engine_sound_enabled = engine_sound != 0;
		if (!TieFlightOptions_Set(&flight, error, sizeof error))
			TieSettings_SettingsReportError(error);
	}
	int volume = flight.player_engine_sound_volume_percent;
	if (AeronUi_SliderInt(ui, "Volume", &volume, 0, 100, 5, "%d%%")) {
		char error[512];
		flight.player_engine_sound_volume_percent = volume;
		if (!TieFlightOptions_Set(&flight, error, sizeof error))
			TieSettings_SettingsReportError(error);
	}
	AeronUi_Spacer(ui, 8.0f);
	AeronUi_Header(ui, "Sound Blaster");
	int sb16 = launch.sb16_filter_enabled;
	if (AeronUi_Toggle(ui, "SB16 Low-pass Filter", &sb16)) {
		launch.sb16_filter_enabled = sb16 != 0;
		launch_changed = true;
	}
	if (launch_changed && !TieLaunchOptions_Set(&launch))
		TieSettings_SettingsReportError("invalid audio settings");
	TieSettings_LaunchRestartNotice(ui);
}

static void TieSettings_DrawExitConfirmation(AeronUiContext* ui) {
	if (!AeronUi_BeginModal(ui, "EXIT GAME", &g_settings.exit_confirmation_open, NULL))
		return;
	AeronUi_Help(ui, "Exit OpenTIE and return to the desktop?");
	AeronUi_BeginColumns(ui, 2, NULL);
	if (AeronUi_Button(ui, "Cancel"))
		g_settings.exit_confirmation_open = 0;
	AeronUi_NextColumn(ui);
	if (AeronUi_Button(ui, "Exit Game##exit-confirmation")) {
		g_settings.exit_confirmation_open = 0;
		Aeron_RequestQuit();
	}
	AeronUi_EndColumns(ui);
	AeronUi_EndModal(ui);
}

void TieSettings_Frame(const AeronInputSnapshot* input, float dt_seconds) {
	static const char* const pages[] = { "Game", "Video", "Audio", "Controller" };
	if (!g_settings.available || !g_settings.open || !input)
		return;
	AeronUi_BeginFrame(g_settings.ui, &(AeronUiFrameDesc) {
										  .input = input,
										  .dt_seconds = dt_seconds,
									  });
	AeronUiWindowDesc window = { .width_ref = 980.0f, .height_ref = 1000.0f, .centered = 1 };
	if (AeronUi_BeginWindow(g_settings.ui, "OpenTIE SETTINGS", &window)) {
		AeronUi_BeginTabBar(g_settings.ui, "pages", pages, 4, &g_settings.page);
		if (g_settings.page == 0)
			TieSettings_GamePage(g_settings.ui);
		else if (g_settings.page == 1) {
			/* Keep the tab ending, separator and Close row outside the scroll view. */
			const float footer_reserve = 83.0f;
			const float scroll_height = AeronUi_AvailableHeight(g_settings.ui) - footer_reserve;
			if (AeronUi_BeginScroll(g_settings.ui, "Video Settings", scroll_height)) {
				TieSettings_VideoPage(g_settings.ui);
				if (g_settings.error[0])
					AeronUi_Error(g_settings.ui, g_settings.error);
				AeronUi_EndScroll(g_settings.ui);
			}
		} else if (g_settings.page == 2)
			TieSettings_AudioPage(g_settings.ui);
		else
			TieControllerSettings_Draw(&g_settings.controller, g_settings.ui, input);
		AeronUi_EndTabBar(g_settings.ui);
		if (g_settings.page != 1 && g_settings.error[0])
			AeronUi_Error(g_settings.ui, g_settings.error);
		AeronUi_Separator(g_settings.ui);
		if (g_settings.page == 0) {
			AeronUi_BeginColumns(g_settings.ui, 2, NULL);
			if (AeronUi_Button(g_settings.ui, "Exit Game"))
				g_settings.exit_confirmation_open = 1;
			AeronUi_NextColumn(g_settings.ui);
			if (AeronUi_Button(g_settings.ui, "Close"))
				(void)TieSettings_SettingsClose();
			AeronUi_EndColumns(g_settings.ui);
		} else if (AeronUi_Button(g_settings.ui, "Close")) {
			(void)TieSettings_SettingsClose();
		}
		if (g_settings.open && g_settings.page == 3)
			TieControllerSettings_DrawModals(&g_settings.controller, g_settings.ui, input, g_settings.config);
		if (g_settings.open && g_settings.exit_confirmation_open)
			TieSettings_DrawExitConfirmation(g_settings.ui);
		AeronUi_EndWindow(g_settings.ui);
	}
	TieSettings_DrawPathPicker(g_settings.ui);
	AeronUiOutput out = AeronUi_EndFrame(g_settings.ui);
	g_settings.captures_controller = out.capture_all != 0;
	if (out.cancel_pressed)
		(void)TieSettings_SettingsClose();
	AeronUi_Submit(g_settings.ui);
}
