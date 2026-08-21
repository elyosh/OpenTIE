#include "tie_app/frame_loop.h"

#include <stdbool.h>
#include <stdint.h>

#include "aeron/aeron.h"
#include "aeron/dx5/compat.h"
#include "tie_app/hotkeys.h"
#include "tie_app/settings/flight_options.h"
#include "tie_app/settings/settings.h"
#include "tie_remaster/remaster.h"
#include "tie_runtime/display/classic_display.h"
#include "tie_runtime/display/classic_framebuffer.h"
#include "tie_runtime/flight_assets/service.h"
#include "tie_runtime/input/input.h"
#include "tie_runtime/presentation/classic_layer.h"
#include "tie_runtime/presentation/presentation.h"
#include "tie_runtime/runtime/profile.h"
#include "tie_runtime/runtime/runtime.h"
#include "tie_runtime/snapshot/snapshot.h"

static uint64_t TieFrameLoop_PresentationIntervalUs(void) {
	const double rate = Aeron_PresentationRate();
	return (uint64_t)(1000000.0 / rate + 0.5);
}

static const TieFramebuffer* TieFrameLoop_Tie98AspectCorrectedFramebuffer(bool enabled,
																		  TieClassicOutputKind output_kind,
																		  const TieSnapshot* snapshot) {
	if (!enabled || output_kind != TIE_CLASSIC_OUTPUT_DX5_SURFACE || !snapshot ||
		snapshot->frontend_profile_id != TIE_FRONTEND_PROFILE_TIE98)
		return NULL;
	return TieClassicFramebuffer_PresentedVga();
}

static bool TieFrameLoop_UpdateFocus(const AeronInputSnapshot* input, bool* ever_had_focus) {
	if (input && input->has_focus)
		*ever_had_focus = true;
	const bool focus_paused = input && !input->has_focus && *ever_had_focus;
	TieRuntime_SetWindowActive(!focus_paused);
	return focus_paused;
}

static void TieFrameLoop_UpdatePresentation(const AeronInputSnapshot* input, int32_t delta_us) {
	const TiePresentationChange change = TiePresentation_BeginFrame(input, delta_us);
	if (!(change & TIE_PRESENTATION_CHANGE_LAYOUT))
		return;
	const TiePresentationLayout* presentation = TiePresentation_Layout();
	if (presentation)
		TieInput_SetClassicLayout(&presentation->frame, &presentation->classic);
}

static bool TieFrameLoop_SynchronizeFlightProfile(uint32_t* generation) {
	if (TieRuntime_FlightResourceReleaseRequested()) {
		TieRemaster_ReleaseFlightResources();
		TieRuntime_CompleteFlightResourceRelease();
	}
	const uint32_t current = TieFlightAssets_ProfileGeneration();
	if (current == *generation)
		return true;
	const TieFlightAssetSource* source = TieFlightAssets_CurrentSource();
	if (!source || !TiePresentation_SetModernAspect(source->presentation_aspect)) {
		Aeron_RequestFatalRendererError("flight profile presentation update");
		return false;
	}
	if (!TieRemaster_SetFlightSource(source)) {
		Aeron_RequestFatalRendererError("flight profile renderer rebind");
		return false;
	}
	*generation = current;
	return true;
}

static void TieFrameLoop_SubmitGameLayers(TieClassicOutputKind* previous_output_kind,
										  const TieSnapshot* snapshot,
										  const TieAppLiveFlightOptions* flight_options, int32_t delta_us,
										  bool paused) {
	const bool suppress_classic = TieRemaster_SuppressesClassicFlight(snapshot);
	TieClassicLayer_SetSuppressed(suppress_classic);

	const TieClassicOutputKind output_kind = TieClassicDisplay_OutputKind();
	const TieFramebuffer* corrected_vga = TieFrameLoop_Tie98AspectCorrectedFramebuffer(
		flight_options->aspect_correct_legacy_scenes, output_kind, snapshot);
	if (*previous_output_kind == TIE_CLASSIC_OUTPUT_DX5_SURFACE &&
		output_kind == TIE_CLASSIC_OUTPUT_INDEXED_FRAMEBUFFER)
		AeronDx5_ResetPresentationState();
	*previous_output_kind = output_kind;

	if (output_kind == TIE_CLASSIC_OUTPUT_DX5_SURFACE)
		AeronDx5_EndFrame();
	if (!suppress_classic && output_kind == TIE_CLASSIC_OUTPUT_INDEXED_FRAMEBUFFER)
		TieClassicLayer_Submit(TieClassicFramebuffer_Current());
	else if (!suppress_classic && corrected_vga)
		TieClassicLayer_Submit(corrected_vga);
	TieRemaster_Frame(snapshot, delta_us, paused);
}

void TieFrameLoop_Run(void) {
	const TiePresentationLayout* presentation = TiePresentation_Layout();
	if (presentation)
		TieInput_SetClassicLayout(&presentation->frame, &presentation->classic);

	TieClassicLayer_Init();
	TieHotkeys hotkeys;
	TieHotkeys_Init(&hotkeys);
	TieClassicOutputKind previous_output_kind = TieClassicDisplay_OutputKind();
	uint32_t profile_generation = TieFlightAssets_ProfileGeneration();
	bool audio_paused = false;
	bool ever_had_focus = false;

	while (TieRuntime_IsActive() && !TieRuntime_ShouldExit() && !Aeron_FatalErrorRequested()) {
		const int32_t delta_us = Aeron_BeginFrame();
		if (Aeron_FatalErrorRequested())
			break;
		if (Aeron_QuitRequested())
			TieRuntime_RequestExit();

		const AeronInputSnapshot* input = Aeron_InputSnapshot();
		const bool focus_paused = TieFrameLoop_UpdateFocus(input, &ever_had_focus);
		TieFrameLoop_UpdatePresentation(input, delta_us);
		const TieHotkeysFrame hotkey_frame = TieHotkeys_Process(&hotkeys, input);
		bool menu_open = hotkey_frame.menu_open;
		bool settings_opened_from_runtime = false;
		TieInput_UpdateCapture(TieSnapshot_Current(), TieSettings_Open());
		TieClassicLayer_SyncInputExtent();

		TieAppLiveFlightOptions flight_options;
		TieFlightOptions_Get(&flight_options);

		TieRemaster_BeginFrame(input);
		if (Aeron_FatalErrorRequested())
			break;
		if (!menu_open)
			TieInput_BeginFrame(delta_us);
		if (!hotkey_frame.paused && !menu_open && !focus_paused)
			TieRuntime_Tick(delta_us);
		if (TieRuntime_ConsumeSettingsMenuRequest()) {
			const bool was_open = TieSettings_Open();
			TieSettings_Show();
			settings_opened_from_runtime = !was_open && TieSettings_Open();
			menu_open = menu_open || TieSettings_Open();
		}
		if (Aeron_FatalErrorRequested() || !TieFrameLoop_SynchronizeFlightProfile(&profile_generation))
			break;

		const bool pause_requested = hotkey_frame.paused || menu_open || focus_paused;
		if (pause_requested != audio_paused) {
			Aeron_AudioSetPaused(pause_requested ? 1 : 0);
			audio_paused = pause_requested;
			Aeron_LogInfo("tie", "%s", audio_paused ? "paused" : "resumed");
		}

		TieClassicLayer_SyncInputExtent();
		const TieSnapshot* snapshot = TieSnapshot_Current();
		TieFrameLoop_SubmitGameLayers(&previous_output_kind, snapshot, &flight_options, delta_us,
									  hotkey_frame.paused || menu_open);
		/* Do not let the input edge that opened the menu close it again. */
		if (!settings_opened_from_runtime)
			TieSettings_Frame(input, (float)delta_us * 1e-6f);
		TieInput_SyncSystemCursor(TieSettings_Open());

		if (Aeron_FatalErrorRequested())
			break;
		if (!Aeron_Present()) {
			Aeron_RequestFatalRendererError("frame presentation");
			break;
		}

		uint64_t wake_delay_us = TieFrameLoop_PresentationIntervalUs();
		if (!pause_requested) {
			const uint64_t engine_delay_us = TieRuntime_NextWakeDelayUs();
			if (engine_delay_us < wake_delay_us)
				wake_delay_us = engine_delay_us;
		}
		Aeron_WaitForNextFrame(wake_delay_us);
	}
	TieClassicLayer_Shutdown();
}
