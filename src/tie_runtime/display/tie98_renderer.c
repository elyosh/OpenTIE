#include "tie_runtime/display/tie98_renderer.h"

#include "tie/flight_surface_tie98.h"
#include "tie/frontend_display_tie98.h"
#include "tie/logbuf2.h"
#include "tie/msg.h"
#include "tie/panel.h"
#include "tie/render_scene_tie98.h"
#include "tie/rtsvga2.h"
#include "tie/std3d_tie98.h"
#include "tie/tie.h"
#include "tie_runtime/audio/config.h"
#include "tie_runtime/diagnostics/diagnostics.h"
#include "tie_runtime/display/classic_display.h"
#include "tie_runtime/display/classic_framebuffer.h"
#include "tie_runtime/flight_assets/model_types.h"
#include "tie_runtime/input/input.h"
#include "tie_runtime/runtime/exports.h"
#include "tie_runtime/runtime/profile.h"
#include "tie_runtime/runtime/runtime.h"
#include "tie_runtime/storage/storage.h"

static bool TieTie98Renderer_RendererModeReady(Tie98OriginalRenderer renderer) {
	const uint16_t expected_mode = renderer == TIE98_ORIGINAL_RENDERER_D3D
									   ? TIE98_DISPLAY_MODE_HARDWARE_FLIGHT
									   : TIE98_DISPLAY_MODE_SOFTWARE_FLIGHT;
	if (g_displayMode != expected_mode || g_flight16bppBytesPerPixel != 2 || !g_primarySurface ||
		!g_lpRenderSurface || !g_flightOffscreenSurface || !g_landruSurface || g_surfaceWidth != 640 ||
		g_surfaceHeight != 480 || g_displayWidth != 640 || g_displayHeight != 480)
		return false;
	if (renderer == TIE98_ORIGINAL_RENDERER_SOFTWARE)
		return g_useHardware3D == 0;
	return g_useHardware3D != 0 && g_d3dDevice != NULL && g_rendererAttachedZBufferSurface != NULL;
}

static bool TieTie98Renderer_RebuildSurfaceResidentState(void) {
	lastpilotpaneldraw = -1;
	g_flightDrawToOffscreenSurface = 1;
	g_flightSurfaceAlreadyLocked = 0;
	FlightSurface_Lock();
	if (FlightSurface_GetLockCount() != 1) {
		if (FlightSurface_GetLockCount() > 0)
			FlightSurface_Unlock();
		return false;
	}
	panel_dosetnewpilotview(camera.pilotview);
	msg_messageinit();
	msg_messagerestore();
	FlightSurface_Unlock();
	if (FlightSurface_GetLockCount() != 0)
		return false;
	Tie98StarColors_Invalidate();
	return true;
}

static bool TieTie98Renderer_FailRendererSwitch(Tie98OriginalRenderer renderer, const char* stage) {
	TieDiagnostics_Log(TIE_LOG_ERROR, "TIE98 original renderer switch to %s failed during %s\n",
					   renderer == TIE98_ORIGINAL_RENDERER_D3D ? "Direct3D" : "software", stage);
	TieDiagnostics_RendererFailure("TIE98 original renderer switch");
	return false;
}

bool Tie98Renderer_ApplyPending(void) {
	if (!TieProfile_RendererChangePending())
		return true;
	const TieFlightProfile* profile = TieProfile_Flight();
	if (profile->version != TIE_GAME_VERSION_TIE98)
		return TieTie98Renderer_FailRendererSwitch(profile->tie98_original_renderer,
												   "runtime-profile reconciliation");
	const Tie98OriginalRenderer renderer = profile->tie98_original_renderer;
	const bool wants_hardware = renderer == TIE98_ORIGINAL_RENDERER_D3D;
	if ((g_useHardware3D != 0) == wants_hardware) {
		TieProfile_CompleteRendererChange();
		return true;
	}
	if (FlightSurface_GetLockCount() != 0)
		return true;
	if (!TieClassicDisplay_ActivateFlight())
		return TieTie98Renderer_FailRendererSwitch(renderer, "display-mode activation");
	if (!TieTie98Renderer_RendererModeReady(renderer))
		return TieTie98Renderer_FailRendererSwitch(renderer, "display readiness");
	if (!TieTie98Renderer_RebuildSurfaceResidentState())
		return TieTie98Renderer_FailRendererSwitch(renderer, "cockpit and HUD reconstruction");
	TieProfile_CompleteRendererChange();
	return true;
}
