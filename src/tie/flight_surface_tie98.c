#include "tie/flight_surface_tie98.h"

#include "tie/frontend_display_tie98.h"
#include "tie/logbuf2.h"
#include "tie/replay.h"
#include "tie/rtsvga2.h"
#include "tie/xtrans2.h"

#include <landru/vesa.h>

#include <string.h>

// GLOBAL: TIE98 0x4F3D9C
int g_flightDrawToOffscreenSurface = 1;
// GLOBAL: TIE98 0x58AC3C
static int g_surfaceLockCount;

// FUNCTION: TIE98 0x49B670
int FrontendDisplay_GetDrawSurfacePitch(void) { return g_flightPrimaryPitch; }

// FUNCTION: TIE98 0x49B6A0
int FlightSurface_GetLockCount(void) { return g_surfaceLockCount; }

// FUNCTION: TIE98 0x49B6B0
void FlightSurface_Lock(void) {
	DDSURFACEDESC descriptor;

	while (!g_windowActive) {
		if (g_quitRequested)
			break;
		Flight_PumpWindowMessages();
	}
	if (g_surfaceLockCount != 0) {
		++g_surfaceLockCount;
		return;
	}
	g_surfaceLockCount = 1;

	if (maingameflag || replayviewmode || reentersimflag) {
		if (g_flightDrawToOffscreenSurface) {
			memset(&descriptor, 0, sizeof descriptor);
			descriptor.dwSize = 108;
			while (g_flightOffscreenSurface->lpVtbl->Lock(g_flightOffscreenSurface, NULL, &descriptor,
														  DDLOCK_WAIT | DDLOCK_NOSYSLOCK,
														  NULL) == DX_DDERR_SURFACELOST)
				g_flightOffscreenSurface->lpVtbl->Restore(g_flightOffscreenSurface);
			g_flightPrimaryPitch = descriptor.lPitch;
			XVESA_Set_Video_Buffer((uint8_t*)descriptor.lpSurface);
			vgapointer = descriptor.lpSurface;
			xtrans2_videobaseptr = descriptor.lpSurface;
			memset(&descriptor, 0, sizeof descriptor);
			descriptor.dwSize = 108;
			g_flightOffscreenSurface->lpVtbl->GetSurfaceDesc(g_flightOffscreenSurface, &descriptor);
			g_flightPrimaryPitch = descriptor.lPitch;
		} else {
			memset(&descriptor, 0, sizeof descriptor);
			descriptor.dwSize = 108;
			while (g_lpRenderSurface->lpVtbl->Lock(g_lpRenderSurface, NULL, &descriptor,
												   DDLOCK_WAIT | DDLOCK_NOSYSLOCK,
												   NULL) == DX_DDERR_SURFACELOST)
				g_lpRenderSurface->lpVtbl->Restore(g_lpRenderSurface);
			g_flightPrimaryPitch = descriptor.lPitch;
			XVESA_Set_Video_Buffer((uint8_t*)descriptor.lpSurface);
			vgapointer = descriptor.lpSurface;
			xtrans2_videobaseptr = descriptor.lpSurface;
			memset(&descriptor, 0, sizeof descriptor);
			descriptor.dwSize = 108;
			g_lpRenderSurface->lpVtbl->GetSurfaceDesc(g_lpRenderSurface, &descriptor);
			g_flightPrimaryPitch = descriptor.lPitch;
			const size_t offset =
				(size_t)descriptor.lPitch * ((uint32_t)(g_displayHeight - g_surfaceHeight) >> 1) +
				(size_t)g_flight16bppBytesPerPixel * ((uint32_t)(g_displayWidth - g_surfaceWidth) >> 1);
			vgapointer += offset;
			xtrans2_videobaseptr += offset;
		}
	} else {
		memset(&descriptor, 0, sizeof descriptor);
		descriptor.dwSize = 108;
		while (g_landruSurface->lpVtbl->Lock(g_landruSurface, NULL, &descriptor,
											 DDLOCK_WAIT | DDLOCK_NOSYSLOCK, NULL) == DX_DDERR_SURFACELOST)
			g_landruSurface->lpVtbl->Restore(g_landruSurface);
		g_flightPrimaryPitch = descriptor.lPitch;
		XVESA_Set_Video_Buffer((uint8_t*)descriptor.lpSurface);
		vgapointer = descriptor.lpSurface;
		xtrans2_videobaseptr = descriptor.lpSurface;
		memset(&descriptor, 0, sizeof descriptor);
		descriptor.dwSize = 108;
		g_landruSurface->lpVtbl->GetSurfaceDesc(g_landruSurface, &descriptor);
		g_flightPrimaryPitch = descriptor.lPitch;
		const size_t offset =
			(size_t)descriptor.lPitch * ((uint32_t)(g_displayHeight - g_surfaceHeight) >> 1) +
			(size_t)g_flight16bppBytesPerPixel * ((uint32_t)(g_displayWidth - g_surfaceWidth) >> 1);
		vgapointer += offset;
		xtrans2_videobaseptr += offset;
	}

	XVESA_Set_Linear_Buffer_Size((uint32_t)(480 * FrontendDisplay_GetDrawSurfacePitch()));
	if (g_surfacePitch != (uint32_t)g_flightPrimaryPitch) {
		g_surfacePitch = (uint32_t)g_flightPrimaryPitch;
		rtsvga2_setvgapointers(vgapointer, (uint16_t)g_flightPrimaryPitch, 480);
	}
}

// FUNCTION: TIE98 0x49BA20
void FlightSurface_Unlock(void) {
	if (g_surfaceLockCount > 1) {
		--g_surfaceLockCount;
		return;
	}
	if (g_surfaceLockCount < 1) {
		g_surfaceLockCount = 0;
		return;
	}
	while (!g_windowActive) {
		if (g_quitRequested)
			break;
		Flight_PumpWindowMessages();
	}
	--g_surfaceLockCount;
	if (maingameflag || replayviewmode || reentersimflag) {
		if (g_flightDrawToOffscreenSurface) {
			g_flightOffscreenSurface->lpVtbl->Unlock(g_flightOffscreenSurface, xtrans2_videobaseptr);
			return;
		}
		g_lpRenderSurface->lpVtbl->Unlock(g_lpRenderSurface, xtrans2_videobaseptr);
		return;
	}
	g_landruSurface->lpVtbl->Unlock(g_landruSurface, xtrans2_videobaseptr);
}
