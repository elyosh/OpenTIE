/* OpenTIE application construction and ordered teardown. */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "tie_app/application.h"
#include "tie_app/frame_loop.h"
#include "tie_app/window_icon.h"

#include "aeron/aeron.h"
#include "aeron/dx5/compat.h"
#include "aeron/winmm/compat.h"
#include "tie_runtime/display/classic_display.h"
#include "tie_runtime/display/classic_framebuffer.h"
#include "tie_runtime/runtime/runtime.h"
#include "tie_runtime/snapshot/snapshot.h"

#include <imuse/midi_nuked_sc55.h>

#include "tie_app/config/app_config.h"
#include "tie_app/midi_resources.h"
#include "tie_app/settings/flight_options.h"
#include "tie_app/settings/settings.h"
#include "tie_app/settings/video_options.h"
#include "tie_app/setup/installation.h"
#include "tie_app/setup/setup.h"
#include "tie_app/ui.h"
#include "tie_remaster/remaster.h"
#include "tie_runtime/flight_assets/service.h"
#include "tie_runtime/input/actions.h"
#include "tie_runtime/input/controller_mapping.h"
#include "tie_runtime/input/input.h"
#include "tie_runtime/presentation/presentation.h"
#ifdef AERON_DEBUG_UI
#include "tie_remaster/debug/debug_tools.h"
#endif

static const char* TieApplication_MidiBackendName(TieMidiBackendKind backend) {
	switch (backend) {
		case TIE_MIDI_BACKEND_FLUIDSYNTH:
			return "fluidsynth";
		case TIE_MIDI_BACKEND_FM4_OPL3:
			return "fm4_opl3";
		case TIE_MIDI_BACKEND_SC55:
			return "sc55";
		case TIE_MIDI_BACKEND_NONE:
			return "none";
	}
	return "unknown";
}

static void TieApplication_WarnMidiFallback(const char* reason) {
	static const AeronMessageBoxButton button = {
		.id = 0,
		.label = "Continue",
		.is_default = 1,
		.is_cancel = 1,
	};
	char message[768];
	snprintf(message, sizeof message, "%s\n\nThe OPL3 MIDI backend will be used for this session.", reason);
	Aeron_LogWarn("tie.audio", "%s", message);
	const AeronMessageBoxOptions options = {
		.kind = AERON_MESSAGE_BOX_WARNING,
		.title = "MIDI Synthesizer Warning",
		.message = message,
		.buttons = &button,
		.button_count = 1,
	};
	if (!Aeron_ShowMessageBox(&options, NULL))
		Aeron_LogError("tie.audio", "could not display the MIDI fallback warning");
	else
		Aeron_RaiseWindow();
}

static AeronConfig TieApplication_AeronConfig(const TieLaunchOptions* launch) {
	AeronConfig config = { 0 };
	config.org_name = "TotallyOpen";
	config.app_name = "OpenTIE";
	config.resource_root = launch->resource_root;
	config.resource_path = "resources";
	config.shader_path = TIE_SHADER_RELATIVE_DIR;
	config.window_title = "OpenTIE";
	config.window_icon_bmp = tie_window_icon_bmp;
	config.window_icon_bmp_size = sizeof tie_window_icon_bmp;
	config.logical_width = TIE_PRESENTATION_INITIAL_WIDTH;
	config.logical_height = TIE_PRESENTATION_LOGICAL_HEIGHT;
	config.presentation_mode = AERON_PRESENTATION_ASPECT_FIT;
	config.clear_color_enabled = 1;
	config.clear_color_rgba[3] = 1.0f;
	return config;
}

static void TieApplication_Shutdown(AeronVfs* vfs, TieAppConfigState* config,
									TieInstallationSet* installations, TieUi* ui,
									ImuseNukedSc55Romset* sc55_romset) {
	char error[512];
	if (!TieSettings_Flush(error, sizeof error))
		Aeron_LogError("tie.config", "%s", error);
	if (!TieAppConfig_Save(vfs, config, error, sizeof error))
		Aeron_LogError("tie.config", "%s", error);
	TieSettings_Shutdown();
#ifdef AERON_DEBUG_UI
	TieDebugTools_Shutdown();
#endif
	TieRemaster_Shutdown();
	TiePresentation_Shutdown();
	TieRuntime_Shutdown();
	imuse_nuked_sc55_romset_release(sc55_romset);
	AeronWinmm_Shutdown();
	AeronDx5_Shutdown();
	TieInstallation_SetClose(installations);
	TieUi_Shutdown(ui);
	TieAppConfig_Destroy(config);
	Aeron_Shutdown();
}

static AeronDx5Rect TieApplication_Dx5PresentationRect(void* context, int surface_width, int surface_height) {
	(void)context;
	(void)surface_width;
	(void)surface_height;
	const TiePresentationLayout* presentation = TiePresentation_Layout();
	if (presentation == NULL)
		return (AeronDx5Rect) { 0, 0, surface_width, surface_height };
	return (AeronDx5Rect) {
		.x = presentation->classic.x,
		.y = presentation->classic.y,
		.width = presentation->classic.width,
		.height = presentation->classic.height,
	};
}

int TieApplication_Run(const TieLaunchOptions* launch) {
	const AeronConfig config = TieApplication_AeronConfig(launch);

	if (!Aeron_Init(&config)) {
		fprintf(stderr, "[tie-aeron] Aeron_Init failed\n");
		return 1;
	}
	/* Activation is asynchronous on macOS. Request it before the remaining
	 * synchronous startup work, then service the events queued by Aeron_Init. */
	Aeron_RaiseWindow();
	Aeron_PumpEvents();
	/* Establish cursor visibility after activation events, which may alter the
	 * platform cursor state. This also makes the cached state authoritative. */
	TieInput_SyncSystemCursor(false);

	AeronVfs* vfs = Aeron_GetVfs();
	const uint32_t dos_lookup = AERON_VFS_ROOT_OPTION_CASE_INSENSITIVE_LOOKUP;
	if (!AeronVfs_SetRootOptions(vfs, AERON_VFS_ROOT_USER, dos_lookup) ||
		!AeronVfs_SetRootOptions(vfs, AERON_VFS_ROOT_TEMP, dos_lookup)) {
		Aeron_RequestFatalError("Storage Configuration Error", "Could not configure the user storage roots.");
		Aeron_Shutdown();
		return 1;
	}

	TieAppConfigState app_config = { 0 };
	char config_error[512];
	if (!TieAppConfig_Load(vfs, &app_config, config_error, sizeof config_error)) {
		Aeron_RequestFatalError("Configuration Error", config_error);
	} else {
		TieInputActions_InstallKeyboard(&app_config.requested.keyboard);
		TieControllerMapping_SetOptions(&app_config.requested.controller);
		if (!Aeron_SetFullscreen(app_config.requested.video.fullscreen))
			Aeron_LogWarn("tie.config", "could not apply fullscreen setting");
	}
	if (Aeron_FatalErrorRequested()) {
		TieAppConfig_Destroy(&app_config);
		Aeron_Shutdown();
		return 1;
	}
	Aeron_PumpEvents();
	TieUi app_ui = { 0 };
	if (!TieUi_Init(&app_ui, &app_config.requested.ui, config_error, sizeof config_error))
		Aeron_RequestFatalError("Application UI Error", config_error);

	TieInstallationSet installations = { 0 };
	TieSetupResult setup_result = TIE_SETUP_ERROR;
	if (!Aeron_FatalErrorRequested()) {
		TieInput_SyncSystemCursor(true);
		setup_result =
			TieSetup_ResolveInstallations(&app_ui, vfs, &app_config, launch->tie95_data, launch->tie98_data,
										  &installations, config_error, sizeof config_error);
		if (setup_result == TIE_SETUP_ERROR)
			Aeron_RequestFatalError("Installation Setup Error", config_error);
	}
	if (setup_result == TIE_SETUP_CANCELLED) {
		TieInstallation_SetClose(&installations);
		TieUi_Shutdown(&app_ui);
		TieAppConfig_Destroy(&app_config);
		Aeron_Shutdown();
		return 0;
	}
	Aeron_PumpEvents();
	TieInput_SyncSystemCursor(false);
	if (!Aeron_FatalErrorRequested()) {
		const bool available = TieMidiBackend_Available(app_config.requested.midi_backend);
		if (!available) {
			const char* name = TieApplication_MidiBackendName(app_config.requested.midi_backend);
			char message[256];
			snprintf(message, sizeof message,
					 "audio.midi_backend selects '%s', but that backend "
					 "is disabled or unavailable in this build.",
					 name);
			Aeron_RequestFatalError("MIDI Backend Error", message);
		}
	}
	if (Aeron_FatalErrorRequested()) {
		TieInstallation_SetClose(&installations);
		TieUi_Shutdown(&app_ui);
		TieAppConfig_Destroy(&app_config);
		Aeron_Shutdown();
		return 1;
	}
	TieLaunchConfig launch_config = { 0 };
	if (!TieAppConfig_ResolveLaunch(&app_config.requested, installations.has_tie95, installations.has_tie98,
									&launch_config, config_error, sizeof config_error))
		Aeron_RequestFatalError("Installation Selection Error", config_error);
	const TieInstallation* frontend_installation =
		TieInstallation_Get(&installations, launch_config.frontend_version);
	const TieInstallation* flight_installation =
		TieInstallation_Get(&installations, launch_config.flight_profile.version);
	const TieInstallation* tie98_installation = TieInstallation_Get(&installations, TIE_GAME_VERSION_TIE98);
	if (!Aeron_FatalErrorRequested() && tie98_installation &&
		app_config.requested.requested_model_source == TIE_FLIGHT_MODEL_SOURCE_REMASTERED &&
		!AeronVfs_Exists(tie98_installation->vfs, AERON_VFS_ROOT_ASSET, "tie_remaster/flight/assets.yaml"))
		Aeron_RequestFatalError(
			"Flight Model Configuration",
			"Remastered flight models require remaster/flight/assets.yaml in the TIE98 installation.");
	if (Aeron_FatalErrorRequested()) {
		TieInstallation_SetClose(&installations);
		TieUi_Shutdown(&app_ui);
		TieAppConfig_Destroy(&app_config);
		Aeron_Shutdown();
		return 1;
	}
	const TieFrontendProfileId frontend_profile = (TieFrontendProfileId)launch_config.frontend_version;
	TieProfile_SetFrontend(frontend_profile);
	TieProfile_SetFlight(&launch_config.flight_profile);
	const char* frontend_profile_name = frontend_profile == TIE_FRONTEND_PROFILE_TIE98 ? "tie98" : "tie95";
	Aeron_LogInfo("tie.frontend", "selected %s assets from %s",
				  frontend_profile == TIE_FRONTEND_PROFILE_TIE98 ? "TIE98" : "TIE95",
				  frontend_installation->root);
	Aeron_LogInfo(
		"tie.flight",
		"selected %s flight with %s models, %s original renderer, and player engine sound %s from %s",
		launch_config.flight_profile.version == TIE_GAME_VERSION_TIE98 ? "TIE98" : "TIE95",
		launch_config.flight_profile.model_source == TIE_FLIGHT_MODEL_SOURCE_REMASTERED ? "remastered"
																						: "original",
		launch_config.flight_profile.tie98_original_renderer == TIE98_ORIGINAL_RENDERER_D3D ? "d3d"
																							: "software",
		launch_config.flight_profile.player_engine_sound_enabled ? "enabled" : "disabled",
		flight_installation->root);
	Aeron_LogInfo("tie.flight", "OPT emissive strength %.3g, projectile emissive strength %.3g",
				  (double)app_config.requested.flight_model_opt_emissive_strength,
				  (double)app_config.requested.flight_model_opt_projectile_emissive_strength);

	const char* soundfont_path = NULL;
	ImuseNukedSc55Romset* sc55_romset = NULL;
	TieMidiBackendKind effective_midi_backend = app_config.requested.music_source == TIE_MUSIC_IMUSE
													? app_config.requested.midi_backend
													: TIE_MIDI_BACKEND_NONE;
	if (effective_midi_backend == TIE_MIDI_BACKEND_FLUIDSYNTH) {
		if (!TieMidiResources_Soundfontvalidate(app_config.requested.fluidsynth_soundfont_file, config_error,
												sizeof config_error)) {
			TieApplication_WarnMidiFallback(config_error);
			effective_midi_backend = TIE_MIDI_BACKEND_FM4_OPL3;
		} else {
			soundfont_path = app_config.requested.fluidsynth_soundfont_file;
			Aeron_LogInfo("tie.audio", "using FluidSynth MIDI backend with %s", soundfont_path);
		}
	} else if (effective_midi_backend == TIE_MIDI_BACKEND_SC55) {
		sc55_romset = imuse_nuked_sc55_romset_load(app_config.requested.sc55_rom_directory, config_error,
												   sizeof config_error);
		if (!sc55_romset) {
			TieApplication_WarnMidiFallback(config_error);
			effective_midi_backend = TIE_MIDI_BACKEND_FM4_OPL3;
		} else {
			const char* romset_name = imuse_nuked_sc55_romset_name(sc55_romset);
			Aeron_LogInfo("tie.audio", "using Nuked SC-55 %s MIDI backend from %s", romset_name,
						  app_config.requested.sc55_rom_directory);
		}
	}
	if (effective_midi_backend == TIE_MIDI_BACKEND_FM4_OPL3) {
		Aeron_LogInfo("tie.audio", "using recovered FM4 OPL3 MIDI backend");
	} else if (effective_midi_backend == TIE_MIDI_BACKEND_NONE) {
		Aeron_LogInfo("tie.audio", "iMUSE MIDI synthesis disabled for the selected music source");
	}
	Aeron_LogInfo("tie.audio", "SB16 digital PCM filter %s",
				  app_config.requested.sb16_filter_enabled ? "enabled" : "disabled");
	Aeron_PumpEvents();

	const char* remaster_dir = NULL;
	if (AeronVfs_Exists(vfs, AERON_VFS_ROOT_RESOURCE, "tie_formats/fonts/subtitle/font8.fnt")) {
		remaster_dir = Aeron_ResourceRoot();
	}

	TieStorageConfig storage_routes = {
		.application = vfs,
		.frontend = frontend_installation->vfs,
		.flight = flight_installation->vfs,
		.tie95_flight = installations.has_tie95 ? installations.tie95.vfs : NULL,
		.tie98_flight = tie98_installation ? tie98_installation->vfs : NULL,
		.tie98_media = tie98_installation ? tie98_installation->vfs : NULL,
	};
	if (app_config.requested.music_source == TIE_MUSIC_TIE98) {
		if (tie98_installation &&
			AeronVfs_Exists(tie98_installation->vfs, AERON_VFS_ROOT_ASSET, "MUSIC/Track02.ogg")) {
			if (!AeronWinmm_ConfigureCdAudio(&(AeronWinmmCdAudioDesc) {
					.vfs = tie98_installation->vfs,
					.root = AERON_VFS_ROOT_ASSET,
					.directory = "MUSIC",
				}))
				Aeron_LogWarn("tie.audio", "could not configure TIE98 flight music");
		} else {
			Aeron_LogWarn("tie.audio", "TIE98 flight music is unavailable; flight will be silent");
		}
	}
	TieRuntimeConfig runtime_config = {
		.storage = storage_routes,
		.flight_assets = {
			.tie95_vfs = installations.has_tie95 ? installations.tie95.vfs : NULL,
			.tie98_vfs = tie98_installation ? tie98_installation->vfs : NULL,
			.source = {
				.profile = launch_config.flight_profile,
				.smooth_angle_degrees = app_config.requested.flight_model_smooth_angle_degrees,
				.opt_emissive_strength = app_config.requested.flight_model_opt_emissive_strength,
				.opt_projectile_emissive_strength =
					app_config.requested.flight_model_opt_projectile_emissive_strength,
			},
		},
		.frontend_profile = frontend_profile,
		.flight_profile = launch_config.flight_profile,
		.audio = {
			.midi_backend = {
				.kind = effective_midi_backend,
				.soundfont_path = soundfont_path,
				.sc55_romset = sc55_romset,
			},
			.sb16_filter_enabled = app_config.requested.sb16_filter_enabled,
			.music_source = app_config.requested.music_source,
		},
	};
	AeronDx5_Configure(&(AeronDx5Config) {
		.presentation_rect = TieApplication_Dx5PresentationRect,
	});
	if (!TieRuntime_Init(&runtime_config, config_error, sizeof config_error))
		Aeron_RequestFatalError("Game Runtime Error", config_error);
	const TieFlightAssetSource* flight_source = NULL;
	if (!Aeron_FatalErrorRequested()) {
		flight_source = TieFlightAssets_CurrentSource();
		if (!flight_source || !TiePresentation_Init(flight_source->presentation_aspect))
			Aeron_RequestFatalRendererError("presentation initialization");
	}
	Aeron_PumpEvents();

	AeronCommandBuffer* startup_cmd = Aeron_AcquireCommandBuffer();
	if (!startup_cmd) {
		Aeron_RequestFatalRendererError("remaster startup command-buffer acquisition");
	}
	TieRemasterConfig remaster_config = {
			.remaster_dir = remaster_dir,
			.frontend_profile_id = frontend_profile_name,
			.flight_source = flight_source,
			.aspect_correct_legacy_scenes = app_config.requested.aspect_correct_legacy_scenes,
			.video_options = {
					.hdr = app_config.requested.video.output.hdr,
					.sdr_content_gamma = app_config.requested.video.output.sdr_content_gamma,
					.paper_white_auto = app_config.requested.video.output.paper_white_auto,
					.paper_white_nits = app_config.requested.video.output.paper_white_nits,
					.ssao_quality = app_config.requested.video.output.ssao_quality,
					.shadows_enabled = app_config.requested.video.output.shadows_enabled,
					.shadow_atlas_size = app_config.requested.video.output.shadow_atlas_size,
					.fsr_mode = app_config.requested.video.output.fsr_mode,
					.fsr_sharpness = app_config.requested.video.output.fsr_sharpness,
					.motion_blur_quality = app_config.requested.video.output.motion_blur_quality,
					.motion_blur_shutter = app_config.requested.video.output.motion_blur_shutter,
					.msaa_samples = app_config.requested.video.output.msaa_samples,
					.starfield_style = app_config.requested.video.output.starfield_style,
			},
			.render = app_config.requested.render,
			.pbr = app_config.requested.pbr,
			.point_lights = app_config.requested.point_lights,
	};
	if (!Aeron_FatalErrorRequested() && !TieRemaster_Init(startup_cmd, &remaster_config) &&
		!Aeron_FatalErrorRequested()) {
		Aeron_RequestFatalRendererError("remaster initialization");
	}
	if (!Aeron_FatalErrorRequested()) {
		char settings_error[768];
		if (!TieSettings_Init(&app_ui, &app_config, installations.has_tie95, installations.has_tie98,
							  settings_error, sizeof settings_error))
			Aeron_RequestFatalError("Settings Menu Error", settings_error);
	}
	if (startup_cmd) {
		if (Aeron_FatalErrorRequested()) {
			Aeron_CancelCommandBuffer(startup_cmd);
		} else if (!Aeron_SubmitCommandBuffer(startup_cmd)) {
			Aeron_RequestFatalRendererError("remaster startup upload submission");
		}
	}
	Aeron_LogInfo("tie.render", "HDR output %s (headroom %.2f)",
				  Aeron_OutputHdrStatusName(Aeron_OutputHdrStatus()), Aeron_OutputHdrHeadroom());
	Aeron_PumpEvents();

	if (Aeron_FatalErrorRequested()) {
		TieSettings_Shutdown();
		TieRemaster_Shutdown();
		TiePresentation_Shutdown();
		TieRuntime_Shutdown();
		imuse_nuked_sc55_romset_release(sc55_romset);
		TieInstallation_SetClose(&installations);
		TieUi_Shutdown(&app_ui);
		TieAppConfig_Destroy(&app_config);
		Aeron_Shutdown();
		return 1;
	}

#ifdef AERON_DEBUG_UI
	TieDebugTools_Register();
#endif
	TieFrameLoop_Run();
	const int exit_status = Aeron_FatalErrorRequested() ? 1 : 0;
	TieApplication_Shutdown(vfs, &app_config, &installations, &app_ui, sc55_romset);
	return exit_status;
}
