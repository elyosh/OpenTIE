#include "tie/frontend_display_tie98.h"

#include "tie/flight_surface_tie98.h"
#include "tie/logbuf2.h"
#include "tie/render_scene_tie98.h"
#include "tie/render_texture_tie98.h"
#include "tie/std3d_tie98.h"
#include "tie/tie.h"
#include "tie/xtrans2.h"
#include "tie_runtime/audio/config.h"
#include "tie_runtime/diagnostics/diagnostics.h"
#include "tie_runtime/display/classic_display.h"
#include "tie_runtime/display/classic_framebuffer.h"
#include "tie_runtime/flight_assets/model_types.h"
#include "tie_runtime/input/input.h"
#include "tie_runtime/runtime/exports.h"
#include "tie_runtime/runtime/profile.h"
#include "tie_runtime/storage/storage.h"
#include "tie_runtime/timing/sim_clock.h"

#include <landru/cursor.h>
#include <landru/vesa.h>
#include <landru/video.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct Tie98PaletteEntry {
	uint8_t red;
	uint8_t green;
	uint8_t blue;
	uint8_t flags;
} Tie98PaletteEntry;

#pragma pack(push, 1)
typedef struct Tie98BitmapFileHeader {
	uint16_t signature;
	uint32_t fileSize;
	uint16_t reserved0;
	uint16_t reserved1;
	uint32_t pixelOffset;
} Tie98BitmapFileHeader;

typedef struct Tie98BitmapInfoHeader {
	uint32_t headerSize;
	int32_t width;
	int32_t height;
	uint16_t planes;
	uint16_t bitsPerPixel;
	uint32_t compression;
	uint32_t imageSize;
	int32_t pixelsPerMeterX;
	int32_t pixelsPerMeterY;
	uint32_t colorsUsed;
	uint32_t colorsImportant;
} Tie98BitmapInfoHeader;
#pragma pack(pop)

// GLOBAL: TIE98 0x58CA70
IDirectDraw* g_flightDirectDraw;
// GLOBAL: TIE98 0x58DBC0
IDirectDrawSurface* g_primarySurface;
// GLOBAL: TIE98 0x58DFE0
IDirectDrawSurface* g_lpRenderSurface;
// GLOBAL: TIE98 0x58CA68
IDirectDrawSurface* g_flightOffscreenSurface;
// GLOBAL: TIE98 0x58DBC8
IDirectDrawSurface* g_landruSurface;
// GLOBAL: TIE98 0x58CA74
IDirectDrawPalette* g_ddPalette;
// GLOBAL: TIE98 0x4F3D80
uint16_t g_displayMode;
// GLOBAL: TIE98 0x4F3D84
int32_t g_surfaceWidth;
// GLOBAL: TIE98 0x4F3D88
int32_t g_surfaceHeight;
// GLOBAL: TIE98 0x4F3D8C
int32_t g_displayWidth;
// GLOBAL: TIE98 0x4F3D90
int32_t g_displayHeight;
// GLOBAL: TIE98 0x58DBD0
int32_t g_flightPrimaryPitch;
// GLOBAL: TIE98 0x58CA60
int g_windowActive = 1;
// GLOBAL: TIE98 0x58983C
int g_quitRequested;
// GLOBAL: TIE98 0x4F3D74
int g_flightFullscreen = 1;
// GLOBAL: TIE98 0x4F3D78
int g_flightPageFlip = 1;
// GLOBAL: TIE98 0x4F3D7C
int g_displayPixelFormat;
// GLOBAL: TIE98 0x58DBD4
int g_directDrawBlitCapability;
// GLOBAL: TIE98 0x58CE84
IDirectDrawSurface* g_unusedFrontendSurfaceAlias;
// GLOBAL: TIE98 0x58AC44
int g_vgaCompatBorderClearFrames;
// GLOBAL: TIE98 0x58ABB0
int g_flightConfFlicker;
// GLOBAL: TIE98 0x58AC40
uint32_t g_lastVBlankTimeMs;
// GLOBAL: TIE98 0x58AB7C
uint32_t g_monitorRefreshTimingScale;
// GLOBAL: TIE98 0x4F3DFC
int g_flickerSyncTolerance;
// GLOBAL: TIE98 0x58ABC8
int g_hardwarePixelFormatAvailable;
// GLOBAL: TIE98 0x4F3D6C
int g_frontendDisplayWndProcMode = 1;
// GLOBAL: TIE98 0x58ABA4
int g_windowReactivated;
// GLOBAL: TIE98 0x58AB78
static void* g_savedBackBufferPixels;

// GLOBAL: TIE98 0x58CA80
static Tie98PaletteEntry g_grayscalePaletteEntries[256];
// GLOBAL: TIE98 0x58D3C0
static Tie98PaletteEntry g_directDrawPaletteEntries[256];
// GLOBAL: TIE98 0x58AC14
static int g_grayscalePaletteInitialized;
// GLOBAL: TIE98 0x58AB88
static DxGuid g_configuredDirectDrawDriverGuid;
// GLOBAL: TIE98 0x58CA6C
void* g_flightWindowHandle;

typedef struct Tie98Rect {
	int32_t left;
	int32_t top;
	int32_t right;
	int32_t bottom;
} Tie98Rect;

typedef struct Tie98DirectDrawCaps {
	uint32_t size;
	uint32_t capabilities;
	uint32_t capabilities2;
	uint32_t color_key_capabilities;
	uint32_t reserved[87];
} Tie98DirectDrawCaps;

// FUNCTION: TIE98 0x49A0B0
void Flight_PumpWindowMessages(void) {
	/* PORT: the application pumps platform events before TieRuntime_Tick. */
}

// FUNCTION: TIE98 0x49AA60
int FrontendDisplay_OnSurfaceRestored(void) { return 1; }

// FUNCTION: TIE98 0x403E70
static int Bitmap_WriteBmp24(const char* file_name, const void* pixels, int width, int height, int pitch,
							 int bits_per_pixel, int pixel_format_555, const uint8_t* palette_bgra) {
	if (bits_per_pixel == 8 && palette_bgra == NULL)
		return 0;

	FILE* file = fopen(file_name, "wb");
	if (file == NULL)
		return 0;

	fseek(file, 54, SEEK_SET);
	uint32_t file_size = 54;
	if (bits_per_pixel == 16) {
		for (int y = height - 1; y >= 0; --y) {
			const uint16_t* row = (const uint16_t*)((const uint8_t*)pixels + pitch * y);
			for (int x = 0; x < width; ++x) {
				const uint16_t pixel = row[x];
				uint8_t color[3];
				color[0] = (uint8_t)(pixel << 3);
				if (pixel_format_555) {
					color[1] = (uint8_t)((pixel >> 5) << 3);
					color[2] = (uint8_t)((pixel >> 10) << 3);
				} else {
					color[1] = (uint8_t)((pixel >> 5) << 2);
					color[2] = (uint8_t)((pixel >> 11) << 3);
				}
				if (fwrite(color, 1, sizeof color, file) != sizeof color) {
					fclose(file);
					return 0;
				}
				file_size += 3;
			}
			if (width & 1) {
				const uint8_t padding[3] = { 0, 0, 0 };
				if (fwrite(padding, 1, sizeof padding, file) != sizeof padding) {
					fclose(file);
					return 0;
				}
				file_size += 3;
			}
		}
	} else if (bits_per_pixel == 8) {
		for (int y = height - 1; y >= 0; --y) {
			const uint8_t* row = (const uint8_t*)pixels + pitch * y;
			for (int x = 0; x < width; ++x) {
				const uint8_t* palette_color = palette_bgra + 4 * row[x];
				uint8_t color[3];
				color[0] = palette_color[0];
				color[1] = palette_color[1];
				color[2] = palette_color[2];
				if (fwrite(color, 1, sizeof color, file) != sizeof color) {
					fclose(file);
					return 0;
				}
				file_size += 3;
			}
			if (width & 1) {
				const uint8_t padding[3] = { 0, 0, 0 };
				if (fwrite(padding, 1, sizeof padding, file) != sizeof padding) {
					fclose(file);
					return 0;
				}
				file_size += 3;
			}
		}
	}

	Tie98BitmapFileHeader file_header = {
		.signature = 0x4D42,
		.fileSize = file_size,
		.pixelOffset = 54,
	};
	Tie98BitmapInfoHeader info_header = {
		.headerSize = sizeof info_header,
		.width = width + (width & 1),
		.height = height,
		.planes = 1,
		.bitsPerPixel = 24,
		.imageSize = file_size - 54,
	};
	fseek(file, 0, SEEK_SET);
	if (fwrite(&file_header, sizeof file_header, 1, file) != 1 ||
		fwrite(&info_header, sizeof info_header, 1, file) != 1) {
		fclose(file);
		return 0;
	}
	fclose(file);
	return 1;
}

// FUNCTION: TIE98 0x49CBE0
int FrontendDisplay_GetPixelFormat555(void) {
	if (g_hardwarePixelFormatAvailable && g_useHardware3D)
		return g_pFmtRGB565->colorInfo.greenBPP == 5;
	return g_displayPixelFormat == 555;
}

// FUNCTION: TIE98 0x49D180
HRESULT FrontendDisplay_CaptureScreenshot(void) {
	return DX_DD_OK;

	char file_name[64];
	int sequence = 0;
	for (;;) {
		sprintf(file_name, "tiescreen%d.bmp", sequence);
		FILE* file = fopen(file_name, "rb");
		if (file == NULL)
			break;
		fclose(file);
		++sequence;
	}

	uint8_t palette_bgra[1024];
	for (int index = 0; index < 256; ++index) {
		palette_bgra[4 * index] = g_directDrawPaletteEntries[index].blue;
		palette_bgra[4 * index + 1] = g_directDrawPaletteEntries[index].green;
		palette_bgra[4 * index + 2] = g_directDrawPaletteEntries[index].red;
		palette_bgra[4 * index + 3] = 0;
	}

	int lock_count = FlightSurface_GetLockCount();
	for (int index = 0; index < lock_count; ++index)
		FlightSurface_Unlock();
	FrontendDisplay_PresentFrame();

	const int saved_offscreen_route = g_flightDrawToOffscreenSurface;
	const uint16_t saved_maingameflag = maingameflag;
	g_flightDrawToOffscreenSurface = 0;
	maingameflag = 1;
	FlightSurface_Lock();
	Bitmap_WriteBmp24(file_name, xtrans2_videobaseptr, g_surfaceWidth, g_surfaceHeight, (int)g_surfacePitch,
					  8 * g_flight16bppBytesPerPixel, FrontendDisplay_GetPixelFormat555(), palette_bgra);
	FlightSurface_Unlock();
	g_flightDrawToOffscreenSurface = saved_offscreen_route;
	maingameflag = saved_maingameflag;
	HRESULT result = FrontendDisplay_PresentFrame();
	for (int index = 0; index < lock_count; ++index)
		FlightSurface_Lock();
	return result;
}
// FUNCTION: TIE98 0x49CC10
int FrontendDisplay_SaveBackBuffer(void) {
	if (g_landruSurface != NULL) {
		const size_t size = 640u * 480u * (size_t)g_flight16bppBytesPerPixel;
		g_savedBackBufferPixels = malloc(size);
		if (g_savedBackBufferPixels != NULL) {
			DDSURFACEDESC descriptor;
			memset(&descriptor, 0, sizeof descriptor);
			descriptor.dwSize = 108;
			HRESULT result;
			do {
				result = g_landruSurface->lpVtbl->Lock(g_landruSurface, NULL, &descriptor, 0, NULL);
			} while (result == DX_DDERR_WASSTILLDRAWING);
			if (result != 0) {
				free(g_savedBackBufferPixels);
				g_savedBackBufferPixels = NULL;
				return 0;
			}
			memcpy(g_savedBackBufferPixels, descriptor.lpSurface, size);
			g_landruSurface->lpVtbl->Unlock(g_landruSurface, descriptor.lpSurface);
		}
	}
	return 1;
}

// FUNCTION: TIE98 0x49CCF0
int FrontendDisplay_RestoreBackBuffer(void) {
	if (g_savedBackBufferPixels != NULL) {
		if (g_landruSurface != NULL) {
			DDSURFACEDESC descriptor;
			memset(&descriptor, 0, sizeof descriptor);
			descriptor.dwSize = 108;
			HRESULT result;
			do {
				result = g_landruSurface->lpVtbl->Lock(g_landruSurface, NULL, &descriptor, 0, NULL);
			} while (result == DX_DDERR_WASSTILLDRAWING);
			if (result == 0) {
				memcpy(descriptor.lpSurface, g_savedBackBufferPixels,
					   640u * 480u * (size_t)g_flight16bppBytesPerPixel);
				g_landruSurface->lpVtbl->Unlock(g_landruSurface, descriptor.lpSurface);
			}
		}
		free(g_savedBackBufferPixels);
		g_savedBackBufferPixels = NULL;
	}
	return 1;
}

// FUNCTION: TIE98 0x49AA70
const DxGuid* FrontendDisplay_LoadDriverGuid(void) {
	FILE* file = fopen("video.cfg", "rb");
	if (file) {
		if (fread(&g_configuredDirectDrawDriverGuid, 1, sizeof g_configuredDirectDrawDriverGuid, file) ==
			sizeof g_configuredDirectDrawDriverGuid) {
			fclose(file);
			return &g_configuredDirectDrawDriverGuid;
		}
		fclose(file);
	}
	return NULL;
}

// FUNCTION: TIE98 0x49B3E0
void FrontendDisplay_InitGrayscalePalette(void) {
	if (!g_grayscalePaletteInitialized) {
		for (int index = 0; index < 256; ++index) {
			g_grayscalePaletteEntries[index].red = (uint8_t)index;
			g_grayscalePaletteEntries[index].green = (uint8_t)index;
			g_grayscalePaletteEntries[index].blue = (uint8_t)index;
		}
		g_grayscalePaletteInitialized = 1;
	}
}

// FUNCTION: TIE98 0x49B430
void FrontendDisplay_SetPalette(const uint8_t* rgb6, int first_entry, int entry_count) {
	Tie98PaletteEntry entries[256];
	while (!g_windowActive) {
		if (g_quitRequested)
			break;
		Flight_PumpWindowMessages();
	}
	if (g_softwareCursorEnabled)
		XCURSOR_Select_Contrast_Colors(rgb6);
	const int lock_count = FlightSurface_GetLockCount();
	for (int index = 0; index < lock_count; ++index)
		FlightSurface_Unlock();
	for (int index = first_entry; index < first_entry + entry_count; ++index) {
		entries[index].red = (uint8_t)(rgb6[index * 3] << 2);
		entries[index].green = (uint8_t)(rgb6[index * 3 + 1] << 2);
		entries[index].blue = (uint8_t)(rgb6[index * 3 + 2] << 2);
		entries[index].flags = 0;
	}
	if (g_ddPalette)
		g_ddPalette->lpVtbl->SetEntries(g_ddPalette, 0, (uint32_t)first_entry, (uint32_t)entry_count,
										entries);
	for (int index = 0; index < lock_count; ++index)
		FlightSurface_Lock();
}

// FUNCTION: TIE98 0x49B510
void FrontendDisplay_UpdatePalette(const uint8_t* rgb6, int first_entry, int entry_count) {
	while (!g_windowActive) {
		if (g_quitRequested)
			break;
		Flight_PumpWindowMessages();
	}
	if (g_softwareCursorEnabled)
		XCURSOR_Select_Contrast_Colors(rgb6);
	const int lock_count = FlightSurface_GetLockCount();
	for (int index = 0; index < lock_count; ++index)
		FlightSurface_Unlock();
	for (int index = first_entry; index < first_entry + entry_count; ++index) {
		g_directDrawPaletteEntries[index].red = (uint8_t)(rgb6[index * 3] << 2);
		g_directDrawPaletteEntries[index].green = (uint8_t)(rgb6[index * 3 + 1] << 2);
		g_directDrawPaletteEntries[index].blue = (uint8_t)(rgb6[index * 3 + 2] << 2);
	}
	if (g_ddPalette)
		g_ddPalette->lpVtbl->SetEntries(g_ddPalette, 0, (uint32_t)first_entry, (uint32_t)entry_count,
										g_directDrawPaletteEntries);
	for (int index = 0; index < lock_count; ++index)
		FlightSurface_Lock();
}

// FUNCTION: TIE98 0x4279F0
static void Renderer_InitD3DDevice(void) {
	Std3DDeviceCaps required_caps;

	/* PORT: Aeron's DX5 device is not a PowerVR PCX device, so the original
	 * hardware/file probe has no compatible host device to select. */
	g_powerVrSceneWorkaround = 0;
	g_renderTextureCacheCursor = -1;
	std3D_BuildRenderTargetDesc((unsigned int)g_displayWidth, (unsigned int)g_displayHeight,
								(int)g_surfacePitch);
	std3D_SetRenderSurface(g_lpRenderSurface);
	std3D_ResetUnusedStartupState(0, 0, 0, 0);
	std3D_Startup();
	memset(&required_caps, 0, sizeof required_caps);
	required_caps.bHardware = 1;
	required_caps.bTexturePerspective = 1;
	required_caps.bHasZBuffer = 1;
	required_caps.colorModelFlags = 2;
	const unsigned int device_index = std3D_SelectBestDevice(&required_caps);
	const Std3DDeviceCaps* available = &g_std3DDevices[device_index].caps;
	if (available->bHardware && available->bTexturePerspective && available->bHasZBuffer) {
		std3D_CreateDevice(device_index, 1);
		DDSCAPS z_buffer_caps = { DDSCAPS_ZBUFFER };
		HRESULT result = g_lpRenderSurface->lpVtbl->GetAttachedSurface(g_lpRenderSurface, &z_buffer_caps,
																	   &g_rendererAttachedZBufferSurface);
		if (result != DX_DD_OK) {
			/* PORT: the host logger replaces TIE98's DebugPrintf output. */
			TieDiagnostics_Log(TIE_LOG_ERROR, "ERROR(%x)! Failed to get HW Zbuffer\n", (unsigned int)result);
			std3D_DestroyDevice();
			std3D_Shutdown();
			g_useHardware3D = 0;
		}
	} else {
		std3D_Shutdown();
		g_useHardware3D = 0;
	}
}

// FUNCTION: TIE98 0x42B950
void Renderer_ReleaseHardwareZBuffer(void) {
	if (g_rendererAttachedZBufferSurface) {
		g_lpRenderSurface->lpVtbl->DeleteAttachedSurface(g_lpRenderSurface, 0,
														 g_rendererAttachedZBufferSurface);
		g_rendererAttachedZBufferSurface->lpVtbl->Release(g_rendererAttachedZBufferSurface);
		g_rendererAttachedZBufferSurface = NULL;
	}
}

// FUNCTION: TIE98 0x49B5E0
int FrontendDisplay_ReportDirectDrawInitFailure(int error_code) {
	/* PORT: the host logger replaces OutputDebugString and MessageBox. */
	TieDiagnostics_Log(TIE_LOG_ERROR, "Game could not start (DirectDraw initialization error %d)\n",
					   error_code);
	if (g_primarySurface) {
		g_primarySurface->lpVtbl->Release(g_primarySurface);
		g_primarySurface = NULL;
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
	return 0;
}

// FUNCTION: TIE98 0x49AAC0
int FrontendDisplay_InitSurfaces(void) {
	DDSURFACEDESC descriptor;
	DDSCAPS surface_caps;
	Tie98DirectDrawCaps driver_caps;
	HRESULT display_result;

	if (DirectDrawCreate((DxGuid*)(uintptr_t)2, &g_flightDirectDraw, NULL) != DX_DD_OK)
		return FrontendDisplay_ReportDirectDrawInitFailure(1);
	if (!g_flightFullscreen) {
		if (g_flightDirectDraw->lpVtbl->SetCooperativeLevel(g_flightDirectDraw, g_flightWindowHandle,
															DDSCL_FULLSCREEN | DDSCL_EXCLUSIVE |
																DDSCL_ALLOWMODEX) != DX_DD_OK ||
			g_flightDirectDraw->lpVtbl->SetDisplayMode(
				g_flightDirectDraw, (uint32_t)g_displayWidth, (uint32_t)g_displayHeight,
				(uint32_t)(8 * g_flight16bppBytesPerPixel)) != DX_DD_OK)
			return FrontendDisplay_ReportDirectDrawInitFailure(2);
		if (g_flightDirectDraw->lpVtbl->SetCooperativeLevel(g_flightDirectDraw, g_flightWindowHandle,
															DDSCL_NORMAL) != DX_DD_OK)
			return FrontendDisplay_ReportDirectDrawInitFailure(2);
	} else if (g_flightDirectDraw->lpVtbl->SetCooperativeLevel(g_flightDirectDraw, g_flightWindowHandle,
															   DDSCL_FULLSCREEN | DDSCL_EXCLUSIVE |
																   DDSCL_ALLOWMODEX) != DX_DD_OK) {
		return FrontendDisplay_ReportDirectDrawInitFailure(2);
	}

	if (g_useHardware3D)
		g_flight16bppBytesPerPixel = 2;
	if (g_flightFullscreen) {
		display_result = g_flightDirectDraw->lpVtbl->SetDisplayMode(
			g_flightDirectDraw, (uint32_t)g_displayWidth, (uint32_t)g_displayHeight,
			(uint32_t)(8 * g_flight16bppBytesPerPixel));
		if (display_result != DX_DD_OK && g_displayWidth == 320) {
			g_displayWidth = 512;
			g_displayHeight = 384;
			display_result = g_flightDirectDraw->lpVtbl->SetDisplayMode(
				g_flightDirectDraw, 512, 384, (uint32_t)(8 * g_flight16bppBytesPerPixel));
			if (display_result != DX_DD_OK) {
				g_displayWidth = 640;
				g_displayHeight = 480;
				display_result = g_flightDirectDraw->lpVtbl->SetDisplayMode(
					g_flightDirectDraw, 640, 480, (uint32_t)(8 * g_flight16bppBytesPerPixel));
				if (display_result != DX_DD_OK) {
					g_displayWidth = 320;
					g_displayHeight = 240;
				}
			}
		} else if (display_result != DX_DD_OK && g_displayWidth == 512) {
			g_displayWidth = 640;
			g_displayHeight = 480;
			display_result = g_flightDirectDraw->lpVtbl->SetDisplayMode(
				g_flightDirectDraw, 640, 480, (uint32_t)(8 * g_flight16bppBytesPerPixel));
			if (display_result != DX_DD_OK) {
				g_displayWidth = 512;
				g_displayHeight = 384;
			}
		}
		if (display_result != DX_DD_OK) {
			g_flight16bppBytesPerPixel = g_flight16bppBytesPerPixel == 2 ? 1 : 2;
			display_result = g_flightDirectDraw->lpVtbl->SetDisplayMode(
				g_flightDirectDraw, (uint32_t)g_displayWidth, (uint32_t)g_displayHeight,
				(uint32_t)(8 * g_flight16bppBytesPerPixel));
			if (display_result != DX_DD_OK && g_displayWidth == 320) {
				g_displayWidth = 512;
				g_displayHeight = 384;
				display_result = g_flightDirectDraw->lpVtbl->SetDisplayMode(
					g_flightDirectDraw, 512, 384, (uint32_t)(8 * g_flight16bppBytesPerPixel));
				if (display_result != DX_DD_OK) {
					g_displayWidth = 640;
					g_displayHeight = 480;
					display_result = g_flightDirectDraw->lpVtbl->SetDisplayMode(
						g_flightDirectDraw, 640, 480, (uint32_t)(8 * g_flight16bppBytesPerPixel));
					if (display_result != DX_DD_OK) {
						g_displayWidth = 320;
						g_displayHeight = 240;
					}
				}
			} else if (display_result != DX_DD_OK && g_displayWidth == 512) {
				g_displayWidth = 640;
				g_displayHeight = 480;
				display_result = g_flightDirectDraw->lpVtbl->SetDisplayMode(
					g_flightDirectDraw, 640, 480, (uint32_t)(8 * g_flight16bppBytesPerPixel));
				if (display_result != DX_DD_OK) {
					g_displayWidth = 512;
					g_displayHeight = 384;
				}
			}
		}
		if (display_result != DX_DD_OK)
			return FrontendDisplay_ReportDirectDrawInitFailure(3);
	}
	if (g_flight16bppBytesPerPixel != 2)
		g_useHardware3D = 0;

	memset(&driver_caps, 0, sizeof driver_caps);
	driver_caps.size = sizeof driver_caps;
	g_directDrawBlitCapability = 1;
	if (g_flightDirectDraw->lpVtbl->GetCaps(g_flightDirectDraw, &driver_caps, NULL) == DX_DD_OK &&
		(driver_caps.capabilities & 0x400000) != 0 && (driver_caps.color_key_capabilities & 1) != 0 &&
		(driver_caps.color_key_capabilities & 0x200) == 0)
		g_directDrawBlitCapability = 2;

	memset(&descriptor, 0, sizeof descriptor);
	descriptor.dwSize = 108;
	if (g_flightFullscreen) {
		if (g_flightPageFlip) {
			descriptor.dwFlags = DDSD_CAPS | DDSD_BACKBUFFERCOUNT;
			descriptor.dwBackBufferCount = 1;
			descriptor.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE | DDSCAPS_COMPLEX | DDSCAPS_FLIP;
			if (g_useHardware3D)
				descriptor.ddsCaps.dwCaps |= DDSCAPS_3DDEVICE;
		} else {
			descriptor.dwFlags = DDSD_CAPS;
			descriptor.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE;
			if (g_useHardware3D)
				descriptor.ddsCaps.dwCaps |= DDSCAPS_3DDEVICE;
		}
		if (g_flightDirectDraw->lpVtbl->CreateSurface(g_flightDirectDraw, &descriptor, &g_primarySurface,
													  NULL) != DX_DD_OK)
			return FrontendDisplay_ReportDirectDrawInitFailure(4);
		memset(&descriptor, 0, sizeof descriptor);
		descriptor.dwSize = 108;
		g_primarySurface->lpVtbl->GetSurfaceDesc(g_primarySurface, &descriptor);
		g_flightPrimaryPitch = descriptor.lPitch;
		g_surfacePitch = (uint32_t)descriptor.lPitch;
	} else {
		descriptor.dwFlags = DDSD_CAPS;
		descriptor.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE;
		if (g_flightDirectDraw->lpVtbl->CreateSurface(g_flightDirectDraw, &descriptor, &g_primarySurface,
													  NULL) != DX_DD_OK)
			return FrontendDisplay_ReportDirectDrawInitFailure(4);
		if (!FrontendDisplay_OnSurfaceRestored())
			return FrontendDisplay_ReportDirectDrawInitFailure(12);
		memset(&descriptor, 0, sizeof descriptor);
		descriptor.dwSize = 108;
		g_primarySurface->lpVtbl->GetSurfaceDesc(g_primarySurface, &descriptor);
		g_flightPrimaryPitch = descriptor.lPitch;
	}
	if (descriptor.ddpfPixelFormat.dwFlags & DDPF_PALETTEINDEXED8) {
		g_flight16bppBytesPerPixel = 1;
		g_displayPixelFormat = 8;
	} else if (descriptor.ddpfPixelFormat.dwFlags & DDPF_RGB) {
		g_flight16bppBytesPerPixel = 2;
		g_displayPixelFormat = (descriptor.ddpfPixelFormat.dwGBitMask & 0x400) ? 565 : 555;
	}

	if (g_flightFullscreen) {
		FrontendDisplay_ClearSurface(g_primarySurface);
		if (g_flightPageFlip) {
			surface_caps.dwCaps = DDSCAPS_BACKBUFFER;
			if (g_primarySurface->lpVtbl->GetAttachedSurface(g_primarySurface, &surface_caps,
															 &g_lpRenderSurface) != DX_DD_OK)
				return FrontendDisplay_ReportDirectDrawInitFailure(5);
		} else {
			g_unusedFrontendSurfaceAlias = g_primarySurface;
			g_lpRenderSurface = g_primarySurface;
		}
	} else {
		memset(&descriptor, 0, sizeof descriptor);
		descriptor.dwSize = 108;
		descriptor.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT;
		descriptor.dwWidth = (uint32_t)(g_flightPrimaryPitch / g_flight16bppBytesPerPixel);
		descriptor.dwHeight = (uint32_t)g_displayHeight;
		descriptor.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_SYSTEMMEMORY;
		if (g_flightDirectDraw->lpVtbl->CreateSurface(g_flightDirectDraw, &descriptor, &g_lpRenderSurface,
													  NULL) != DX_DD_OK)
			return FrontendDisplay_ReportDirectDrawInitFailure(6);
	}

	if (g_flightPageFlip || !g_flightFullscreen) {
		memset(&descriptor, 0, sizeof descriptor);
		descriptor.dwSize = 108;
		descriptor.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT;
		descriptor.dwWidth = (uint32_t)g_surfaceWidth;
		descriptor.dwHeight = (uint32_t)g_surfaceHeight;
		descriptor.ddsCaps.dwCaps =
			g_useHardware3D ? DDSCAPS_OFFSCREENPLAIN | DDSCAPS_SYSTEMMEMORY : DDSCAPS_OFFSCREENPLAIN;
		if (g_flightDirectDraw->lpVtbl->CreateSurface(g_flightDirectDraw, &descriptor,
													  &g_flightOffscreenSurface, NULL) != DX_DD_OK)
			return FrontendDisplay_ReportDirectDrawInitFailure(6);
		descriptor.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_SYSTEMMEMORY;
		if (g_flightDirectDraw->lpVtbl->CreateSurface(g_flightDirectDraw, &descriptor, &g_landruSurface,
													  NULL) != DX_DD_OK)
			return FrontendDisplay_ReportDirectDrawInitFailure(6);
		if (g_flightFullscreen) {
			FrontendDisplay_ClearSurface(g_lpRenderSurface);
			g_unusedFrontendSurfaceAlias = g_lpRenderSurface;
			FrontendDisplay_ClearSurface(g_flightOffscreenSurface);
			FrontendDisplay_ClearSurface(g_landruSurface);
		}
	}
	if (g_flightFullscreen && !FrontendDisplay_OnSurfaceRestored())
		return FrontendDisplay_ReportDirectDrawInitFailure(12);

	if (g_flight16bppBytesPerPixel == 1) {
		for (int index = 0; index < 256; ++index) {
			g_directDrawPaletteEntries[index].red = (uint8_t)index;
			g_directDrawPaletteEntries[index].green = (uint8_t)index;
			g_directDrawPaletteEntries[index].blue = (uint8_t)index;
		}
		g_flightDirectDraw->lpVtbl->CreatePalette(g_flightDirectDraw, DDPCAPS_8BIT | DDPCAPS_ALLOW256,
												  g_directDrawPaletteEntries, &g_ddPalette, NULL);
		if (g_ddPalette)
			g_primarySurface->lpVtbl->SetPalette(g_primarySurface, g_ddPalette);
	} else {
		g_ddPalette = NULL;
	}
	if (g_useHardware3D)
		Renderer_InitD3DDevice();
	FrontendDisplay_InitGrayscalePalette();
	return 1;
}

// FUNCTION: TIE98 0x49C150
void FrontendDisplay_SetDisplayMode(uint16_t mode) {
	DDSURFACEDESC descriptor;
	DDSCAPS surface_caps;
	const DxGuid* driver_guid;
	int render_bytes_per_pixel;

	if (g_displayMode == mode || !g_flightDirectDraw)
		return;
	if (g_useHardware3D) {
		Renderer_ReleaseHardwareZBuffer();
		std3D_DestroyDevice();
		std3D_Shutdown();
	}
	if (g_flightOffscreenSurface) {
		g_flightOffscreenSurface->lpVtbl->Release(g_flightOffscreenSurface);
		g_flightOffscreenSurface = NULL;
	}
	if (g_landruSurface) {
		g_landruSurface->lpVtbl->Release(g_landruSurface);
		g_landruSurface = NULL;
	}
	if (g_lpRenderSurface) {
		FrontendDisplay_ClearSurface(g_lpRenderSurface);
		if (g_lpRenderSurface != g_primarySurface)
			g_lpRenderSurface->lpVtbl->Release(g_lpRenderSurface);
		g_lpRenderSurface = NULL;
	}
	if (g_primarySurface) {
		FrontendDisplay_ClearSurface(g_primarySurface);
		g_primarySurface->lpVtbl->Release(g_primarySurface);
		g_primarySurface = NULL;
	}
	if (g_ddPalette) {
		g_ddPalette->lpVtbl->Release(g_ddPalette);
		g_ddPalette = NULL;
	}
	if (!g_flightFullscreen && g_flightDirectDraw->lpVtbl->SetCooperativeLevel(
								   g_flightDirectDraw, g_flightWindowHandle,
								   DDSCL_FULLSCREEN | DDSCL_EXCLUSIVE | DDSCL_ALLOWMODEX) != DX_DD_OK)
		exit(100);

	driver_guid = FrontendDisplay_LoadDriverGuid();
	if (driver_guid) {
		if (g_displayMode == TIE98_DISPLAY_MODE_HARDWARE_FLIGHT) {
			g_flightDirectDraw->lpVtbl->Release(g_flightDirectDraw);
			if (DirectDrawCreate(NULL, &g_flightDirectDraw, NULL) != DX_DD_OK)
				exit(102);
			if (g_flightDirectDraw->lpVtbl->SetCooperativeLevel(g_flightDirectDraw, g_flightWindowHandle,
																DDSCL_FULLSCREEN | DDSCL_EXCLUSIVE |
																	DDSCL_ALLOWMODEX) != DX_DD_OK)
				exit(104);
		} else if (mode == TIE98_DISPLAY_MODE_HARDWARE_FLIGHT) {
			g_flightDirectDraw->lpVtbl->Release(g_flightDirectDraw);
			if (DirectDrawCreate(driver_guid, &g_flightDirectDraw, NULL) != DX_DD_OK)
				exit(103);
			if (g_flightDirectDraw->lpVtbl->SetCooperativeLevel(g_flightDirectDraw, g_flightWindowHandle,
																DDSCL_FULLSCREEN | DDSCL_EXCLUSIVE |
																	DDSCL_ALLOWMODEX) != DX_DD_OK)
				exit(105);
		}
	}

	if (mode == TIE98_DISPLAY_MODE_VGA) {
		if (g_flightDirectDraw->lpVtbl->SetDisplayMode(g_flightDirectDraw, 320, 200, 8) == DX_DD_OK) {
			g_displayMode = mode;
			g_flight16bppBytesPerPixel = 1;
			g_surfaceWidth = 320;
			g_surfaceHeight = 200;
			g_displayWidth = 320;
			g_displayHeight = 200;
		}
	} else if (mode == TIE98_DISPLAY_MODE_SVGA) {
		if (g_flightDirectDraw->lpVtbl->SetDisplayMode(g_flightDirectDraw, 640, 480, 8) == DX_DD_OK) {
			g_displayMode = mode;
			g_flight16bppBytesPerPixel = 1;
			g_surfaceWidth = 640;
			g_surfaceHeight = 480;
			g_displayWidth = 640;
			g_displayHeight = 480;
		}
	} else if (mode == TIE98_DISPLAY_MODE_SOFTWARE_FLIGHT) {
		if (g_flightDirectDraw->lpVtbl->SetDisplayMode(g_flightDirectDraw, 640, 480, 16) == DX_DD_OK) {
			g_displayMode = mode;
			g_flight16bppBytesPerPixel = 2;
			g_surfaceWidth = 640;
			g_surfaceHeight = 480;
			g_displayWidth = 640;
			g_displayHeight = 480;
			g_useHardware3D = 0;
		}
	} else if (mode == TIE98_DISPLAY_MODE_HARDWARE_FLIGHT &&
			   g_flightDirectDraw->lpVtbl->SetDisplayMode(g_flightDirectDraw, 640, 480, 16) == DX_DD_OK) {
		g_displayMode = mode;
		g_flight16bppBytesPerPixel = 2;
		g_surfaceWidth = 640;
		g_surfaceHeight = 480;
		g_displayWidth = 640;
		g_displayHeight = 480;
		g_useHardware3D = 1;
	}
	if (g_flight16bppBytesPerPixel != 2)
		g_useHardware3D = 0;

	memset(&descriptor, 0, sizeof descriptor);
	descriptor.dwSize = 108;
	if (g_flightFullscreen) {
		descriptor.dwFlags = DDSD_CAPS | DDSD_BACKBUFFERCOUNT;
		descriptor.dwBackBufferCount = 1;
		descriptor.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE | DDSCAPS_COMPLEX | DDSCAPS_FLIP;
		if (g_useHardware3D)
			descriptor.ddsCaps.dwCaps |= DDSCAPS_3DDEVICE;
		if (g_flightDirectDraw->lpVtbl->CreateSurface(g_flightDirectDraw, &descriptor, &g_primarySurface,
													  NULL) != DX_DD_OK) {
			FrontendDisplay_ReportDirectDrawInitFailure(4);
			return;
		}
		memset(&descriptor, 0, sizeof descriptor);
		descriptor.dwSize = 108;
		g_primarySurface->lpVtbl->GetSurfaceDesc(g_primarySurface, &descriptor);
		g_flightPrimaryPitch = descriptor.lPitch;
		g_surfacePitch = (uint32_t)descriptor.lPitch;
		g_displayPixelFormat = g_flight16bppBytesPerPixel == 2 ? 565 : 8;
		if (descriptor.ddpfPixelFormat.dwFlags & DDPF_PALETTEINDEXED8) {
			g_flight16bppBytesPerPixel = 1;
			g_displayPixelFormat = 8;
		} else if (descriptor.ddpfPixelFormat.dwFlags & DDPF_RGB) {
			g_flight16bppBytesPerPixel = 2;
			g_displayPixelFormat = (descriptor.ddpfPixelFormat.dwGBitMask & 0x400) ? 565 : 555;
		}
		FrontendDisplay_ClearSurface(g_primarySurface);
		surface_caps.dwCaps = DDSCAPS_BACKBUFFER;
		if (g_primarySurface->lpVtbl->GetAttachedSurface(g_primarySurface, &surface_caps,
														 &g_lpRenderSurface) != DX_DD_OK) {
			FrontendDisplay_ReportDirectDrawInitFailure(5);
			return;
		}
		g_unusedFrontendSurfaceAlias = g_lpRenderSurface;
	} else if (g_useHardware3D) {
		descriptor.dwFlags = DDSD_CAPS | DDSD_BACKBUFFERCOUNT;
		descriptor.dwBackBufferCount = 1;
		descriptor.ddsCaps.dwCaps =
			DDSCAPS_PRIMARYSURFACE | DDSCAPS_COMPLEX | DDSCAPS_FLIP | DDSCAPS_3DDEVICE;
		if (g_flightDirectDraw->lpVtbl->CreateSurface(g_flightDirectDraw, &descriptor, &g_primarySurface,
													  NULL) != DX_DD_OK) {
			FrontendDisplay_ReportDirectDrawInitFailure(4);
			return;
		}
		memset(&descriptor, 0, sizeof descriptor);
		descriptor.dwSize = 108;
		g_primarySurface->lpVtbl->GetSurfaceDesc(g_primarySurface, &descriptor);
		g_flightPrimaryPitch = descriptor.lPitch;
		g_surfacePitch = (uint32_t)descriptor.lPitch;
		g_displayPixelFormat = 565;
		if (descriptor.ddpfPixelFormat.dwFlags & DDPF_PALETTEINDEXED8) {
			g_flight16bppBytesPerPixel = 1;
			g_displayPixelFormat = 8;
		} else if (descriptor.ddpfPixelFormat.dwFlags & DDPF_RGB) {
			g_flight16bppBytesPerPixel = 2;
			g_displayPixelFormat = (descriptor.ddpfPixelFormat.dwGBitMask & 0x400) ? 565 : 555;
		}
		FrontendDisplay_ClearSurface(g_primarySurface);
		surface_caps.dwCaps = DDSCAPS_BACKBUFFER;
		if (g_primarySurface->lpVtbl->GetAttachedSurface(g_primarySurface, &surface_caps,
														 &g_lpRenderSurface) != DX_DD_OK) {
			FrontendDisplay_ReportDirectDrawInitFailure(5);
			return;
		}
		g_unusedFrontendSurfaceAlias = g_lpRenderSurface;
		if (g_flightDirectDraw->lpVtbl->SetCooperativeLevel(g_flightDirectDraw, g_flightWindowHandle,
															DDSCL_NORMAL) != DX_DD_OK)
			exit(100);
	} else {
		if (g_flightDirectDraw->lpVtbl->SetCooperativeLevel(g_flightDirectDraw, g_flightWindowHandle,
															DDSCL_NORMAL) != DX_DD_OK)
			exit(100);
		descriptor.dwFlags = DDSD_CAPS;
		descriptor.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE;
		if (g_flightDirectDraw->lpVtbl->CreateSurface(g_flightDirectDraw, &descriptor, &g_primarySurface,
													  NULL) != DX_DD_OK)
			exit(101);
		memset(&descriptor, 0, sizeof descriptor);
		descriptor.dwSize = 108;
		g_primarySurface->lpVtbl->GetSurfaceDesc(g_primarySurface, &descriptor);
		g_flightPrimaryPitch = descriptor.lPitch;
		if (descriptor.ddpfPixelFormat.dwFlags & DDPF_PALETTEINDEXED8) {
			render_bytes_per_pixel = 1;
			g_flight16bppBytesPerPixel = 1;
			g_displayPixelFormat = 8;
		} else {
			render_bytes_per_pixel = 2;
			g_flight16bppBytesPerPixel = 2;
			g_displayPixelFormat = (descriptor.ddpfPixelFormat.dwGBitMask & 0x400) ? 565 : 555;
		}
		descriptor.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT;
		descriptor.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_SYSTEMMEMORY;
		descriptor.dwWidth = (uint32_t)(descriptor.lPitch / render_bytes_per_pixel);
		descriptor.dwHeight = (uint32_t)g_displayHeight;
		if (g_flightDirectDraw->lpVtbl->CreateSurface(g_flightDirectDraw, &descriptor, &g_lpRenderSurface,
													  NULL) != DX_DD_OK) {
			FrontendDisplay_ReportDirectDrawInitFailure(6);
			return;
		}
	}

	descriptor.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT;
	descriptor.dwWidth = (uint32_t)g_surfaceWidth;
	descriptor.dwHeight = (uint32_t)g_surfaceHeight;
	descriptor.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_SYSTEMMEMORY;
	if (g_flightDirectDraw->lpVtbl->CreateSurface(g_flightDirectDraw, &descriptor, &g_flightOffscreenSurface,
												  NULL) != DX_DD_OK) {
		FrontendDisplay_ReportDirectDrawInitFailure(6);
		return;
	}
	if (g_flightDirectDraw->lpVtbl->CreateSurface(g_flightDirectDraw, &descriptor, &g_landruSurface, NULL) !=
		DX_DD_OK) {
		FrontendDisplay_ReportDirectDrawInitFailure(6);
		return;
	}
	if (g_flightFullscreen) {
		FrontendDisplay_ClearSurface(g_lpRenderSurface);
		FrontendDisplay_ClearSurface(g_flightOffscreenSurface);
		FrontendDisplay_ClearSurface(g_landruSurface);
	}

	if (g_flight16bppBytesPerPixel == 1) {
		for (int index = 0; index < 256; ++index) {
			g_directDrawPaletteEntries[index].red = (uint8_t)index;
			g_directDrawPaletteEntries[index].green = (uint8_t)index;
			g_directDrawPaletteEntries[index].blue = (uint8_t)index;
		}
		g_flightDirectDraw->lpVtbl->CreatePalette(g_flightDirectDraw, DDPCAPS_8BIT | DDPCAPS_ALLOW256,
												  g_directDrawPaletteEntries, &g_ddPalette, NULL);
		if (g_ddPalette)
			g_primarySurface->lpVtbl->SetPalette(g_primarySurface, g_ddPalette);
	} else {
		g_ddPalette = NULL;
	}
	if (g_useHardware3D)
		Renderer_InitD3DDevice();
	FrontendDisplay_InitGrayscalePalette();
}

// MODERN ADAPTATION: application-lifetime entry point for the recovered display module.
bool tie98_display_startup(uint16_t initial_mode) {
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

// MODERN ADAPTATION: application-lifetime exit point containing the recovered
// Flight_Main display shutdown block at TIE98 0x499EAE-0x499FCF.
void tie98_display_shutdown(void) {
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

// FUNCTION: TIE98 0x49CB10
void FrontendDisplay_ClearSurface(IDirectDrawSurface* surface) {
	if (!surface)
		return;
	while (!g_windowActive) {
		if (g_quitRequested)
			break;
		Flight_PumpWindowMessages();
	}
	DDBLTFX effects;
	effects.dwSize = 100;
	effects.dwFillColor = 0;
	HRESULT result;
	do {
		result = surface->lpVtbl->Blt(surface, NULL, NULL, NULL, DDBLT_COLORFILL, &effects);
		if (result == DX_DDERR_SURFACELOST) {
			if (!FrontendDisplay_RestorePrimarySurface())
				return;
			FrontendDisplay_OnSurfaceRestored();
		}
	} while (result == DX_DDERR_WASSTILLDRAWING);
}

// FUNCTION: TIE98 0x49CB90
int FrontendDisplay_RestorePrimarySurface(void) {
	return g_primarySurface->lpVtbl->Restore(g_primarySurface) == DX_DD_OK;
}

// FUNCTION: TIE98 0x49CA90
HRESULT FrontendDisplay_ClearBackBuffer(void) {
	DDBLTFX effects;
	HRESULT result;
	while (!g_windowActive) {
		if (g_quitRequested)
			break;
		Flight_PumpWindowMessages();
	}
	effects.dwSize = 100;
	effects.dwFillColor = 0;
	do {
		result =
			g_lpRenderSurface->lpVtbl->Blt(g_lpRenderSurface, NULL, NULL, NULL, DDBLT_COLORFILL, &effects);
		if (result == DX_DDERR_SURFACELOST) {
			if (!FrontendDisplay_RestorePrimarySurface())
				return 0;
			result = FrontendDisplay_OnSurfaceRestored();
		}
	} while (result == DX_DDERR_WASSTILLDRAWING);
	return result;
}

// FUNCTION: TIE98 0x49CE80
void FrontendDisplay_ClearPresentationSurfaces(void) {
	FrontendDisplay_ClearSurface(g_primarySurface);
	FrontendDisplay_ClearSurface(g_lpRenderSurface);
	FrontendDisplay_ClearSurface(g_landruSurface);
}

// FUNCTION: TIE98 0x49BAD0
HRESULT FrontendDisplay_PresentFrame(void) {
	DDSURFACEDESC descriptor;
	int32_t in_vertical_blank;
	HRESULT result;
	while (!g_windowActive && !g_quitRequested)
		Flight_PumpWindowMessages();
	if (g_softwareCursorEnabled && XCURSOR_Get_Display_Count() >= 0) {
		memset(&descriptor, 0, sizeof descriptor);
		descriptor.dwSize = 108;
		if (g_lpRenderSurface->lpVtbl->Lock(g_lpRenderSurface, NULL, &descriptor,
											DDLOCK_WAIT | DDLOCK_NOSYSLOCK, NULL) == DX_DDERR_SURFACELOST)
			return FrontendDisplay_RestorePrimarySurface();
		XCURSOR_Draw_Software_Cursor_To_Surface((uint8_t*)descriptor.lpSurface, descriptor.lPitch, 480);
		g_lpRenderSurface->lpVtbl->Unlock(g_lpRenderSurface, descriptor.lpSurface);
	}
	if (!g_flightPageFlip)
		return g_primarySurface->lpVtbl->Blt(g_primarySurface, NULL, g_lpRenderSurface, NULL, DDBLT_WAIT,
											 NULL);
	if (g_flightConfFlicker) {
		if (g_lastVBlankTimeMs) {
			const int phase =
				(int)(g_monitorRefreshTimingScale * (TieSimClock_NowMs() - g_lastVBlankTimeMs)) % 100000;
			if ((phase < g_flickerSyncTolerance || phase > 100000 - g_flickerSyncTolerance) &&
				g_flightDirectDraw->lpVtbl->GetVerticalBlankStatus(g_flightDirectDraw, &in_vertical_blank) ==
					DX_DD_OK &&
				!in_vertical_blank) {
				while (g_flightDirectDraw->lpVtbl->GetVerticalBlankStatus(g_flightDirectDraw,
																		  &in_vertical_blank) == DX_DD_OK &&
					   !in_vertical_blank) {
				}
				g_lastVBlankTimeMs = TieSimClock_NowMs();
			}
		} else {
			if (g_flightDirectDraw->lpVtbl->GetVerticalBlankStatus(g_flightDirectDraw, &in_vertical_blank) ==
				DX_DD_OK) {
				while (in_vertical_blank && g_flightDirectDraw->lpVtbl->GetVerticalBlankStatus(
												g_flightDirectDraw, &in_vertical_blank) == DX_DD_OK) {
				}
				while (!in_vertical_blank && g_flightDirectDraw->lpVtbl->GetVerticalBlankStatus(
												 g_flightDirectDraw, &in_vertical_blank) == DX_DD_OK) {
				}
			}
			g_lastVBlankTimeMs = TieSimClock_NowMs();
			if (g_flightDirectDraw->lpVtbl->GetMonitorFrequency(g_flightDirectDraw,
																&g_monitorRefreshTimingScale) != DX_DD_OK) {
				for (int blank = 0; blank < 100; ++blank) {
					while (in_vertical_blank && g_flightDirectDraw->lpVtbl->GetVerticalBlankStatus(
													g_flightDirectDraw, &in_vertical_blank) == DX_DD_OK) {
					}
					while (!in_vertical_blank && g_flightDirectDraw->lpVtbl->GetVerticalBlankStatus(
													 g_flightDirectDraw, &in_vertical_blank) == DX_DD_OK) {
					}
				}
				g_monitorRefreshTimingScale = 10000000 / (int)(TieSimClock_NowMs() - g_lastVBlankTimeMs);
			}
		}
	}

	result = g_primarySurface->lpVtbl->Flip(g_primarySurface, NULL, DDFLIP_WAIT);
	if (result == DX_DDERR_NOEXCLUSIVEMODE) {
		if (g_flightDirectDraw->lpVtbl->SetCooperativeLevel(g_flightDirectDraw, g_flightWindowHandle,
															DDSCL_FULLSCREEN | DDSCL_EXCLUSIVE |
																DDSCL_ALLOWMODEX) != DX_DD_OK)
			exit(1);
		result = g_primarySurface->lpVtbl->Flip(g_primarySurface, NULL, DDFLIP_WAIT);
		if (result == DX_DDERR_SURFACELOST) {
			g_primarySurface->lpVtbl->Restore(g_primarySurface);
			g_lpRenderSurface->lpVtbl->Restore(g_lpRenderSurface);
			g_rendererAttachedZBufferSurface->lpVtbl->Restore(g_rendererAttachedZBufferSurface);
			result = g_primarySurface->lpVtbl->Flip(g_primarySurface, NULL, DDFLIP_WAIT);
		}
		if (result != DX_DD_OK)
			exit(1);
		result = g_flightDirectDraw->lpVtbl->SetCooperativeLevel(g_flightDirectDraw, g_flightWindowHandle,
																 DDSCL_NORMAL);
		if (result != DX_DD_OK)
			exit(1);
	}
	if (result == DX_DDERR_SURFACELOST) {
		result = FrontendDisplay_RestorePrimarySurface();
		if (result)
			return FrontendDisplay_OnSurfaceRestored();
	}
	return result;
}

// FUNCTION: TIE98 0x49BDE0
HRESULT FrontendDisplay_PresentFrontSurface(void) {
	while (!g_windowActive) {
		if (g_quitRequested)
			break;
		Flight_PumpWindowMessages();
	}
	return g_primarySurface->lpVtbl->Blt(g_primarySurface, NULL, g_lpRenderSurface, NULL, DDBLT_WAIT, NULL);
}

// FUNCTION: TIE98 0x49BE20
HRESULT FrontendDisplay_ClearAndPresentFrame(void) {
	while (!g_windowActive) {
		if (g_quitRequested)
			break;
		Flight_PumpWindowMessages();
	}
	FrontendDisplay_ClearBackBuffer();
	return FrontendDisplay_PresentFrame();
}

// FUNCTION: TIE98 0x49C050
HRESULT FrontendDisplay_BlitOffscreenToRenderSurface(void) {
	DDBLTFX effects;
	Tie98Rect source;
	Tie98Rect destination;
	HRESULT result;
	while (!g_windowActive) {
		if (g_quitRequested)
			break;
		Flight_PumpWindowMessages();
	}
	effects.dwSize = 100;
	effects.dwROP = DDROP_SRCCOPY;
	destination.left = (g_displayWidth - g_surfaceWidth) >> 1;
	destination.top = (g_displayHeight - g_surfaceHeight) >> 1;
	destination.right = destination.left + g_surfaceWidth;
	destination.bottom = destination.top + g_surfaceHeight;
	source = (Tie98Rect) { 0, 0, g_surfaceWidth, g_surfaceHeight };
	do {
		result = g_lpRenderSurface->lpVtbl->Blt(g_lpRenderSurface, &destination, g_flightOffscreenSurface,
												&source, DDBLT_ROP, &effects);
		if (result == DX_DDERR_SURFACELOST) {
			if (!FrontendDisplay_RestorePrimarySurface())
				return 0;
			result = FrontendDisplay_OnSurfaceRestored();
		}
	} while (result == DX_DDERR_WASSTILLDRAWING);
	return result;
}

// FUNCTION: TIE98 0x49BE50
HRESULT DDRAW_Present_Landru_Frame(void) {
	while (!g_windowActive) {
		if (g_quitRequested)
			break;
		Flight_PumpWindowMessages();
	}
	if (landru_video_flags_gbl & LANDRU_VIDEO_VGA_COMPAT) {
		DDSURFACEDESC descriptor;
		memset(&descriptor, 0, sizeof descriptor);
		descriptor.dwSize = 108;
		if (g_lpRenderSurface->lpVtbl->Lock(g_lpRenderSurface, NULL, &descriptor,
											DDLOCK_WAIT | DDLOCK_NOSYSLOCK, NULL) == DX_DDERR_SURFACELOST) {
			g_vgaCompatBorderClearFrames = 2;
			return FrontendDisplay_RestorePrimarySurface();
		}
		if (g_vgaCompatBorderClearFrames) {
			--g_vgaCompatBorderClearFrames;
			uint8_t* upper = descriptor.lpSurface;
			uint8_t* lower = (uint8_t*)descriptor.lpSurface + 440 * g_surfacePitch;
			for (int row = 0; row < 40; ++row) {
				memset(upper, 0, 640);
				memset(lower, 0, 640);
				upper += g_surfacePitch;
				lower += g_surfacePitch;
			}
		}
		VIDEO_Blit_Indexed_Rect(descriptor.lpSurface, descriptor.lPitch, 480, vga_compat_buffer_gbl, 320, 200,
								320, 0, 0, landru_video_flags_gbl);
		return g_lpRenderSurface->lpVtbl->Unlock(g_lpRenderSurface, descriptor.lpSurface);
	}

	DDBLTFX effects;
	Tie98Rect source = { 0, 0, g_surfaceWidth, g_surfaceHeight };
	Tie98Rect destination = {
		(g_displayWidth - g_surfaceWidth) >> 1,
		(g_displayHeight - g_surfaceHeight) >> 1,
		(g_displayWidth + g_surfaceWidth) >> 1,
		(g_displayHeight + g_surfaceHeight) >> 1,
	};
	effects.dwSize = 100;
	effects.dwROP = DDROP_SRCCOPY;
	HRESULT result;
	do {
		result = g_lpRenderSurface->lpVtbl->Blt(g_lpRenderSurface, &destination, g_landruSurface, &source,
												DDBLT_ROP, &effects);
		if (result == DX_DDERR_SURFACELOST) {
			if (!FrontendDisplay_RestorePrimarySurface())
				return 0;
			result = FrontendDisplay_OnSurfaceRestored();
		}
	} while (result == DX_DDERR_WASSTILLDRAWING);
	return result;
}
