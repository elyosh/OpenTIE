#include "tie_runtime/display/classic_display.h"
#include "tie_runtime/display/classic_framebuffer.h"

#include "tie/frontend_display_tie98.h"
#include "tie/shellext.h"
#include "tie/tie.h"
#include "tie_runtime/audio/config.h"
#include "tie_runtime/diagnostics/diagnostics.h"
#include "tie_runtime/display/classic_display.h"
#include "tie_runtime/display/classic_framebuffer.h"
#include "tie_runtime/flight_assets/model_types.h"
#include "tie_runtime/input/input.h"
#include "tie_runtime/runtime/exports.h"
#include "tie_runtime/runtime/profile.h"
#include "tie_runtime/storage/storage.h"

#include <landru/cursor.h>
#include <landru/surface.h>
#include <landru/vesa.h>

#include <string.h>

typedef enum TieClassicDisplayOwner {
	TIE_CLASSIC_DISPLAY_OWNER_FRONTEND,
	TIE_CLASSIC_DISPLAY_OWNER_FLIGHT,
} TieClassicDisplayOwner;

static TieClassicDisplayOwner s_active_display_owner = TIE_CLASSIC_DISPLAY_OWNER_FRONTEND;

static LandruPortVideoBackend TieClassicDisplay_BackendForVersion(TieGameVersion version) {
	return version == TIE_GAME_VERSION_TIE98 ? LANDRU_PORT_VIDEO_PLATFORM : LANDRU_PORT_VIDEO_SOFTWARE;
}

static bool TieClassicDisplay_Activate(TieClassicDisplayOwner owner, TieGameVersion version, uint16_t mode) {
	const bool entering_tie98_flight = owner == TIE_CLASSIC_DISPLAY_OWNER_FLIGHT &&
									   s_active_display_owner != TIE_CLASSIC_DISPLAY_OWNER_FLIGHT &&
									   version == TIE_GAME_VERSION_TIE98;
	if (!landru_port_Select_Video_Backend(TieClassicDisplay_BackendForVersion(version), mode))
		return false;
	if (version == TIE_GAME_VERSION_TIE98 && g_displayMode != mode)
		return false;
	if (entering_tie98_flight)
		FrontendDisplay_ClearSurface(g_flightOffscreenSurface);
	s_active_display_owner = owner;
	TieClassicFramebuffer_InvalidatePresentedVga();
	lcursor_port_Set_External_Presentation(owner == TIE_CLASSIC_DISPLAY_OWNER_FRONTEND);
	return true;
}

void TieClassicDisplay_Reset(void) {
	s_active_display_owner = TIE_CLASSIC_DISPLAY_OWNER_FRONTEND;
	TieClassicFramebuffer_InvalidatePresentedVga();
}

bool TieClassicDisplay_FrontendActive(void) {
	return s_active_display_owner == TIE_CLASSIC_DISPLAY_OWNER_FRONTEND;
}

bool TieClassicDisplay_InitializeFrontend(void) {
	if (!landru_port_Set_Initial_Video_Backend(TieClassicDisplay_BackendForVersion(TieProfile_FrontendId())))
		return false;
	TieClassicDisplay_Reset();
	lcursor_port_Set_External_Presentation(true);
	return true;
}

bool TieClassicDisplay_ActivateFrontend(void) {
	const TieGameVersion version = TieProfile_FrontendId();
	return TieClassicDisplay_Activate(TIE_CLASSIC_DISPLAY_OWNER_FRONTEND, version, (uint16_t)frontResolution);
}

bool TieClassicDisplay_ActivateFlight(void) {
	const TieFlightProfile* profile = TieProfile_Flight();
	uint16_t mode;
	if (profile->version == TIE_GAME_VERSION_TIE98) {
		mode = profile->tie98_original_renderer == TIE98_ORIGINAL_RENDERER_D3D ? TIE_FLIGHT_RES_SVGA_D3D
																			   : TIE_FLIGHT_RES_SVGA_16;
	} else
		mode = f_res == 1 ? TIE_FLIGHT_RES_SVGA : TIE_FLIGHT_RES_VGA;
	return TieClassicDisplay_ActivateFlightMode(mode);
}

bool TieClassicDisplay_ActivateFlightMode(uint16_t mode) {
	const TieFlightProfile* profile = TieProfile_Flight();
	flightResolution = (int16_t)mode;
	return TieClassicDisplay_Activate(TIE_CLASSIC_DISPLAY_OWNER_FLIGHT, profile->version, mode);
}

TieClassicOutputKind TieClassicDisplay_OutputKind(void) {
	const TieGameVersion version =
		TieClassicDisplay_FrontendActive() ? TieProfile_FrontendId() : TieProfile_Flight()->version;
	return version == TIE_GAME_VERSION_TIE98 ? TIE_CLASSIC_OUTPUT_DX5_SURFACE
											 : TIE_CLASSIC_OUTPUT_INDEXED_FRAMEBUFFER;
}

bool TieClassicDisplay_UsesDx5(void) {
	return TieClassicDisplay_OutputKind() == TIE_CLASSIC_OUTPUT_DX5_SURFACE;
}

bool TieClassicDisplay_UsesExternalCursor(void) { return TieClassicDisplay_FrontendActive(); }

void TieClassicDisplay_Dimensions(int* width, int* height) {
	if (TieClassicDisplay_OutputKind() == TIE_CLASSIC_OUTPUT_DX5_SURFACE) {
		if (width)
			*width = g_surfaceWidth;
		if (height)
			*height = g_surfaceHeight;
		return;
	}
	const TieFramebuffer* framebuffer = TieClassicFramebuffer_Current();
	if (width)
		*width = framebuffer ? framebuffer->width : 0;
	if (height)
		*height = framebuffer ? framebuffer->height : 0;
}
