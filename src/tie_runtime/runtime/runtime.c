#include "tie_runtime/runtime/runtime.h"
#include "tie_runtime/display/classic_framebuffer.h"

#include "tie/fediskio.h"
#include "tie/frontend_display_tie98.h"
#include "tie/fsfx.h"
#include "tie/gamesnd.h"
#include "tie/panel.h"
#include "tie/shell.h"
#include "tie/textext.h"
#include "tie/tie.h"
#include "tie/tielogo.h"
#include "tie/title.h"
#include "tie/user.h"
#include "tie_runtime/audio/imuse_session.h"
#include "tie_runtime/audio/music_policy.h"
#include "tie_runtime/diagnostics/flight_trace.h"
#include "tie_runtime/display/classic_display.h"
#include "tie_runtime/flight_assets/service.h"
#include "tie_runtime/integration/landru_adapter.h"
#include "tie_runtime/runtime/flight_screen.h"
#include "tie_runtime/runtime/inflight_state.h"
#include "tie_runtime/runtime/profile.h"
#include "tie_runtime/snapshot/snapshot.h"
#include "tie_runtime/snapshot/snapshot_flight.h"
#include "tie_runtime/snapshot/snapshot_frontend.h"
#include "tie_runtime/snapshot/snapshot_hud.h"
#include "tie_runtime/snapshot/snapshot_internal.h"
#include "tie_runtime/storage/storage.h"
#include "tie_runtime/timing/replay_timing.h"
#include "tie_runtime/timing/sim_clock.h"

#include <landru/error.h>
#include <landru/surface.h>
#include <landru/task.h>

#include <string.h>

static bool s_quit_requested;
static bool s_tie98_display_initialized;
static bool s_flight_resource_release_requested;
static bool s_settings_menu_requested;

bool TieRuntime_Init(const TieRuntimeConfig* config, char* error, size_t error_capacity) {
	if (!config) {
		TieRuntime_Shutdown();
		return false;
	}

	TieClassicFramebuffer_InvalidatePresentedVga();
	TieStorage_Init(&config->storage);
	if (!TieFlightAssets_Init(&config->flight_assets, error, error_capacity)) {
		TieStorage_Shutdown();
		return false;
	}
	TieInflightOptions_Load();
	TieAudio_Configure(&config->audio);
	TieProfile_SetFrontend(config->frontend_profile);
	TieProfile_SetFlight(&config->flight_profile);

	TieSimClock_Init();
	TieMusicPolicy_ResetClock();
	TieFlightScreen_Reset();
	s_settings_menu_requested = false;
	g_quitRequested = 0;
	/* The recovered Win32 global is only an opaque platform token. */
	g_flightWindowHandle = (void*)(uintptr_t)1;

	const bool initialize_tie98_display = TieProfile_UsesDx5() || TieFlightAssets_Tie98Available();
	uint16_t initial_mode =
		TieProfile_FrontendId() == TIE_FRONTEND_PROFILE_TIE98 ? TieProfile_Frontend()->vesa_mode
		: config->flight_profile.version == TIE_GAME_VERSION_TIE98 &&
				config->flight_profile.tie98_original_renderer == TIE98_ORIGINAL_RENDERER_D3D
			? TIE98_DISPLAY_MODE_HARDWARE_FLIGHT
			: TIE98_DISPLAY_MODE_SOFTWARE_FLIGHT;
	if (initialize_tie98_display && !tie98_display_startup(initial_mode)) {
		TieDiagnostics_Log(TIE_LOG_ERROR, "TIE98 display initialization failed\n");
		TieRuntime_Shutdown();
		return false;
	}
	s_tie98_display_initialized = initialize_tie98_display;
	if (!TieLandruAdapter_Init()) {
		if (s_tie98_display_initialized)
			tie98_display_shutdown();
		s_tie98_display_initialized = false;
		TieRuntime_Shutdown();
		return false;
	}

	musicenabled = 1;
	sfxenabled = 1;
	voiceenabled = 1;
	g_playerEngineSoundUpdateEnabled = TieProfile_Flight()->player_engine_sound_enabled ? 1 : 0;
	strcpy(resourcedir, "RESOURCE/");

	shell_session_begin(6, 1);
	return true;
}

void TieRuntime_Shutdown(void) {
	/* The application can close while the flight task is still active. Flush
	 * diagnostics while mission state and storage are still available. */
	TIE_FLIGHT_TRACE_SHUTDOWN();
	landru_task_clear_all();
	TieFlightScreen_Reset();
	TieLandruAdapter_Shutdown();
	if (s_tie98_display_initialized)
		tie98_display_shutdown();
	s_tie98_display_initialized = false;
	TieAudio_Configure(NULL);
	TieFlightAssets_Shutdown();
	TieInflightOptions_Reset();
	TieStorage_Shutdown();
	s_quit_requested = false;
	s_flight_resource_release_requested = false;
	s_settings_menu_requested = false;
	g_quitRequested = 0;
	g_flightWindowHandle = NULL;
	TieClassicDisplay_Reset();
}

bool TieRuntime_IsActive(void) { return !landru_task_stack_empty(); }

void TieRuntime_RequestExit(void) {
	s_quit_requested = true;
	g_quitRequested = 1;
	lerror_Set_Landru_Exit(0);
}

bool TieRuntime_ShouldExit(void) { return s_quit_requested; }

void TieRuntime_RequestSettingsMenu(void) { s_settings_menu_requested = true; }

bool TieRuntime_ConsumeSettingsMenuRequest(void) {
	const bool requested = s_settings_menu_requested;
	s_settings_menu_requested = false;
	return requested;
}

/* SDL focus events replace the recovered WM_ACTIVATEAPP delivery. */
void TieRuntime_SetWindowActive(bool active) {
	if (!TieProfile_UsesDx5())
		return;
	const int new_state = active ? 1 : 0;
	if (g_windowActive == new_state)
		return;
	g_windowActive = new_state;
	if (new_state) {
		g_windowReactivated = 1;
		if (g_frontendDisplayWndProcMode == 1)
			FrontendDisplay_RestoreBackBuffer();
	} else if (g_frontendDisplayWndProcMode == 1) {
		FrontendDisplay_SaveBackBuffer();
	}
}

void TieRuntime_Tick(int32_t delta_us) {
	delta_us = TieReplayTiming_SelectEngineDeltaUs(delta_us);
	TieSimClock_Advance(delta_us);
	TieMusicPolicy_AdvanceTime(delta_us);

	gamesnd_AdvanceAudio(delta_us);
	gamesnd_drive_palette_cycle();

	/* Imperative draw hooks require a writable snapshot before tasks run. */
	TieSnapshotBuilder_BeginTick();
	landru_task_run_frame();

	const TieSceneKind settled_scene = TieSnapshotBuilder_GetSceneKind();
	const bool emit_flight = settled_scene == TIE_SCENE_FLIGHT_LOADING || settled_scene == TIE_SCENE_FLIGHT;
	if (emit_flight)
		TieFlightSnapshot_Capture();

	if (TieClassicDisplay_FrontendActive()) {
		LandruVideoTarget target;
		if (lsurface_Get_Active_Video_Target(&target)) {
			TieSnapshotBuilder_SetClassicDims((uint16_t)target.width, (uint16_t)target.height);
			TieSnapshotBuilder_SetLandruPresentation((uint16_t)target.width, (uint16_t)target.height,
													 target.width == 320 ? TIE_SOURCE_PIXEL_ASPECT_VGA_4_3
																		 : TIE_SOURCE_PIXEL_ASPECT_SQUARE,
													 (uint8_t)TieProfile_FrontendId(), target.generation);
		}
	}
	if (emit_flight) {
		TieSnapshotBuilder_SetFlightScreen(TieFlightScreen_Active());
		TieHudSnapshot_Capture();
	}
	TieLandruAdapter_EmitRenderState();
	TieFrontendSnapshot_CaptureText();
	TieFrontendSnapshot_CaptureTitle();
	TieFrontendSnapshot_CaptureLogo();
	TiePaletteSnapshot_Capture();
	TieSnapshotBuilder_FinalizeTick();
}

uint64_t TieRuntime_NextWakeDelayUs(void) { return landru_task_next_wake_delay_us(); }

void TieRuntime_RequestFlightResourceRelease(void) { s_flight_resource_release_requested = true; }

bool TieRuntime_FlightResourceReleaseRequested(void) { return s_flight_resource_release_requested; }

void TieRuntime_CompleteFlightResourceRelease(void) {
	if (!s_flight_resource_release_requested)
		return;
	TieFlightAssets_ClearRuntimeCaches();
	s_flight_resource_release_requested = false;
}
