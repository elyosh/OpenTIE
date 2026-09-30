#include "tie_runtime/display/tie98_display.h"
#include "tie/frontend_display_tie98.h"
#include "tie/logbuf2.h"
#include "tie/render_list_tie98.h"
#include "tie/render_scene_tie98.h"
#include "tie/std3d_tie98.h"

#include <stddef.h>
#include <stdlib.h>

/* Host storage for the render list the original locks from a flight handle. */
static RenderObjectListEntryTIE98 s_render_object_list[TIE98_RENDER_OBJECT_LIST_CAPACITY];
static int* s_emitted_vertex_map;
static size_t s_emitted_vertex_map_capacity;
static uint8_t* s_cockpit_coverage;
static size_t s_cockpit_coverage_capacity;

uint8_t* Tie98Display_ReserveCockpitCoverage(size_t size) {
	if (s_cockpit_coverage_capacity < size) {
		uint8_t* coverage = realloc(s_cockpit_coverage, size);
		if (!coverage)
			return NULL;
		s_cockpit_coverage = coverage;
		s_cockpit_coverage_capacity = size;
	}
	return s_cockpit_coverage;
}

int* Tie98Display_ReserveEmittedVertexMap(size_t count) {
	if (s_emitted_vertex_map_capacity < count) {
		int* map = realloc(s_emitted_vertex_map, count * sizeof *map);
		if (!map)
			return s_emitted_vertex_map;
		s_emitted_vertex_map = map;
		s_emitted_vertex_map_capacity = count;
	}
	return s_emitted_vertex_map;
}

/* Application-lifetime entry point for the recovered display module. */
bool Tie98Display_Startup(uint16_t initial_mode) {
	g_renderObjectListEntries = s_render_object_list;
	g_displayMode = initial_mode;
	g_surfaceWidth = initial_mode == TIE98_DISPLAY_MODE_VGA ? 320 : 640;
	g_surfaceHeight = initial_mode == TIE98_DISPLAY_MODE_VGA ? 200 : 480;
	g_displayWidth = g_surfaceWidth;
	g_displayHeight = g_surfaceHeight;
	g_flight16bppBytesPerPixel =
		initial_mode == TIE98_DISPLAY_MODE_VGA || initial_mode == TIE98_DISPLAY_MODE_SVGA ? 1 : 2;
	g_useHardware3D = initial_mode == TIE98_DISPLAY_MODE_HARDWARE_FLIGHT;
	return FrontendDisplay_InitSurfaces() != 0;
}

/* Application-lifetime exit point. Mirrors the Flight_Main display shutdown
 * block at TIE98 0x499EAE-0x499FCF, which the runtime flight task replaces. */
void Tie98Display_Shutdown(void) {
	if (g_useHardware3D) {
		Renderer_ReleaseHardwareZBuffer();
		std3D_DestroyDevice();
		std3D_Shutdown();
	}
	if (g_flightDirectDraw) {
		FrontendDisplay_ClearSurface(g_primarySurface);
		FrontendDisplay_ClearSurface(g_lpRenderSurface);
		FrontendDisplay_ClearSurface(g_flightOffscreenSurface);
		FrontendDisplay_ClearSurface(g_landruSurface);
		if (g_primarySurface) {
			g_primarySurface->lpVtbl->Release(g_primarySurface);
			g_primarySurface = NULL;
			if (!g_flightFullscreen)
				g_lpRenderSurface->lpVtbl->Release(g_lpRenderSurface);
			g_lpRenderSurface = NULL;
			g_unusedFrontendSurfaceAlias = NULL;
		}
		if (g_ddPalette) {
			g_ddPalette->lpVtbl->Release(g_ddPalette);
			g_ddPalette = NULL;
		}
		if (g_flightOffscreenSurface) {
			g_flightOffscreenSurface->lpVtbl->Release(g_flightOffscreenSurface);
			g_flightOffscreenSurface = NULL;
		}
		if (g_landruSurface) {
			g_landruSurface->lpVtbl->Release(g_landruSurface);
			g_landruSurface = NULL;
		}
		g_flightDirectDraw->lpVtbl->SetCooperativeLevel(
			g_flightDirectDraw, g_flightWindowHandle, DDSCL_FULLSCREEN | DDSCL_EXCLUSIVE | DDSCL_ALLOWMODEX);
		g_flightDirectDraw->lpVtbl->FlipToGDISurface(g_flightDirectDraw);
		g_flightDirectDraw->lpVtbl->RestoreDisplayMode(g_flightDirectDraw);
		g_flightDirectDraw->lpVtbl->Release(g_flightDirectDraw);
		g_flightDirectDraw = NULL;
		/* The host owns its native window; there is no Win32 DestroyWindow call. */
		if (g_flightWindowHandle)
			g_flightWindowHandle = NULL;
	}
	g_useHardware3D = 0;
}
