#ifndef TIE_FRONTEND_DISPLAY_TIE98_H
#define TIE_FRONTEND_DISPLAY_TIE98_H

#include "aeron/dx5/ddraw.h"

#include <stdbool.h>
#include <stdint.h>

typedef enum Tie98DisplayMode {
	TIE98_DISPLAY_MODE_VGA = 0x13,
	TIE98_DISPLAY_MODE_SVGA = 0x101,
	TIE98_DISPLAY_MODE_SOFTWARE_FLIGHT = 0x111,
	TIE98_DISPLAY_MODE_HARDWARE_FLIGHT = 0x1FF,
} Tie98DisplayMode;

extern IDirectDraw* g_flightDirectDraw;
extern IDirectDrawSurface* g_primarySurface;
extern IDirectDrawSurface* g_lpRenderSurface;
extern IDirectDrawSurface* g_flightOffscreenSurface;
extern IDirectDrawSurface* g_landruSurface;
extern IDirectDrawPalette* g_ddPalette;
extern uint16_t g_displayMode;
extern int32_t g_surfaceWidth;
extern int32_t g_surfaceHeight;
extern int32_t g_displayWidth;
extern int32_t g_displayHeight;
extern int32_t g_flightPrimaryPitch;
extern int g_windowActive;
extern int g_quitRequested;
extern int g_hardwarePixelFormatAvailable;
extern int g_frontendDisplayWndProcMode;
extern int g_windowReactivated;
extern void* g_flightWindowHandle;

void Flight_PumpWindowMessages(void);
void Renderer_ReleaseHardwareZBuffer(void);
const DxGuid* FrontendDisplay_LoadDriverGuid(void);

bool tie98_display_startup(uint16_t initial_mode);
void tie98_display_shutdown(void);

int FrontendDisplay_InitSurfaces(void);
void FrontendDisplay_SetDisplayMode(uint16_t mode);
int FrontendDisplay_ReportDirectDrawInitFailure(int error_code);
void FrontendDisplay_InitGrayscalePalette(void);
void FrontendDisplay_SetPalette(const uint8_t* rgb6, int first_entry, int entry_count);
void FrontendDisplay_UpdatePalette(const uint8_t* rgb6, int first_entry, int entry_count);
void FrontendDisplay_ClearSurface(IDirectDrawSurface* surface);
void FrontendDisplay_ClearPresentationSurfaces(void);
int FrontendDisplay_OnSurfaceRestored(void);
int FrontendDisplay_RestorePrimarySurface(void);
int FrontendDisplay_GetPixelFormat555(void);
HRESULT FrontendDisplay_CaptureScreenshot(void);
int FrontendDisplay_SaveBackBuffer(void);
int FrontendDisplay_RestoreBackBuffer(void);
HRESULT FrontendDisplay_ClearBackBuffer(void);
HRESULT FrontendDisplay_PresentFrame(void);
HRESULT FrontendDisplay_PresentFrontSurface(void);
HRESULT FrontendDisplay_ClearAndPresentFrame(void);
HRESULT FrontendDisplay_BlitOffscreenToRenderSurface(void);
HRESULT DDRAW_Present_Landru_Frame(void);

#endif
