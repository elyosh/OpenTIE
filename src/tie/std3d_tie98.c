#include "tie/std3d_tie98.h"

#include "aeron/dx5/compat.h"

#include "tie/frontend_display_tie98.h"
#include "tie/logbuf2.h"
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

#include <stdlib.h>
#include <string.h>

#define STD3D_DEVICE_LIMIT 4
#define STD3D_TEXTURE_FORMAT_LIMIT 8

// GLOBAL: TIE98 0x58C478
IDirectDraw* g_std3DDirectDraw;
// GLOBAL: TIE98 0x6B1EE4
IDirectDrawSurface* g_std3DRenderSurface;
// GLOBAL: TIE98 0x58BCF0
IDirect3DDevice* g_d3dDevice;
// GLOBAL: TIE98 0x58C11C
IDirect3DViewport* g_d3dViewport;
// GLOBAL: TIE98 0x6B0FB0
Std3DViewportRect g_std3DQuadRect;
// GLOBAL: TIE98 0x6B0F20
static D3DTLVERTEX g_std3DQuadVerts[4];
// GLOBAL: TIE98 0x6B0EE0
static Std3DRenderTri g_std3DQuadTris[2];
// GLOBAL: TIE98 0x58C430
uint32_t g_std3DCapFlags;
// GLOBAL: TIE98 0x58C450
uint32_t g_std3DExecBufMaxVerts;

// GLOBAL: TIE98 0x58C118
static IDirect3D* g_lpD3D;
// GLOBAL: TIE98 0x58C464
static IDirect3DExecuteBuffer* g_d3dExecuteBuffer;
// GLOBAL: TIE98 0x58BF00
static D3DEXECUTEBUFFERDESC g_d3dExecBufDesc;
// GLOBAL: TIE98 0x58C428
static uint8_t* g_d3dWritePtr;
// GLOBAL: TIE98 0x58BEFC
static uint8_t* g_d3dExecBufBase;
// GLOBAL: TIE98 0x58C42C
static uint8_t* g_d3dInstrStart;
// GLOBAL: TIE98 0x58C46C
static int g_d3dBufVertCount;
// GLOBAL: TIE98 0x58C470
static unsigned int g_std3DExecBufTriCount;
// GLOBAL: TIE98 0x4F5300
static unsigned int g_std3DTextureFrameTag;
// GLOBAL: TIE98 0x4F5304
static Std3DTextureSurface* g_d3dCurTexture;
// GLOBAL: TIE98 0x58C434
static Std3DRenderStateFlags g_d3dStateFlags;
// GLOBAL: TIE98 0x58C47C
static int g_std3DStartupDone;
// GLOBAL: TIE98 0x58C480
static int g_std3DDeviceOpen;
// GLOBAL: TIE98 0x4F5308
static int g_std3DZBufferEnabled;
// GLOBAL: TIE98 0x6B0ED0
int g_std3DZBufferBitDepth;
// GLOBAL: TIE98 0x58C438
static unsigned int g_std3DNumDevices;
// GLOBAL: TIE98 0x58C440
static unsigned int g_std3DNumTexFormats;
// GLOBAL: TIE98 0x6B0E1C
static unsigned int g_std3DCurDeviceIdx;
// GLOBAL: TIE98 0x58C468
static unsigned int g_std3DExecBufSize;
// GLOBAL: TIE98 0x6B14E0
Std3DDevice g_std3DDevices[STD3D_DEVICE_LIMIT];
// GLOBAL: TIE98 0x58C43C
Std3DDevice* g_pStd3DCurDevice;
// GLOBAL: TIE98 0x6B0FC0
static Std3DTexFmt g_std3DTextureFormats[STD3D_TEXTURE_FORMAT_LIMIT];
// GLOBAL: TIE98 0x58BCA0
static Std3DRenderTargetDesc g_std3DRenderTargetDesc;
// GLOBAL: TIE98 0x6B0FA0
static Std3DRenderTargetDesc* g_pStd3DRenderTarget;
// GLOBAL: TIE98 0x6B1E04 (surface field of the original std3D surface block)
static IDirectDrawSurface* g_std3DZBufferSurface;
// GLOBAL: TIE98 0x5FE88C
IDirectDrawSurface* g_rendererAttachedZBufferSurface;
// GLOBAL: TIE98 0x58C458
static Std3DTextureSurface* g_std3DTextureCacheHead;
// GLOBAL: TIE98 0x58C45C
static Std3DTextureSurface* g_std3DTextureCacheTail;
// GLOBAL: TIE98 0x58C454
static unsigned int g_std3DTextureSurfaceCount;
// GLOBAL: TIE98 0x58C444
Std3DTexFmt* g_pFmtRGB565;
// GLOBAL: TIE98 0x58C448
static Std3DTexFmt* g_pFmtRGBA1555;
// GLOBAL: TIE98 0x58C44C
static Std3DTexFmt* g_pFmtRGBA4444;
// GLOBAL: TIE98 0x6B0F08
static int g_fmtIdxRGB565;
// GLOBAL: TIE98 0x6B1F58
static int g_fmtIdxRGBA1555;
// GLOBAL: TIE98 0x6B0E18
static int g_fmtIdxRGBA4444;
// GLOBAL: TIE98 0x58C460
static Std3DVBuffer* g_pStd3DVBuffer;
// GLOBAL: TIE98 0x4F52F0
static unsigned int g_std3DMinTextureWidth = 1;
// GLOBAL: TIE98 0x4F52F4
static unsigned int g_std3DMinTextureHeight = 1;
// GLOBAL: TIE98 0x4F52F8
static unsigned int g_std3DMaxTextureWidth = 256;
// GLOBAL: TIE98 0x4F52FC
static unsigned int g_std3DMaxTextureHeight = 256;
// GLOBAL: TIE98 0x58BEF8
static int g_std3DFogColorRed8;
// GLOBAL: TIE98 0x58C120
static int g_std3DFogColorGreen8;
// GLOBAL: TIE98 0x58BC9C
static int g_std3DFogColorBlue8;
// GLOBAL: TIE98 0x58BCEC
static int g_std3DFogTableStartBits;
// GLOBAL: TIE98 0x58BC98
static int g_std3DFogTableEndBits;
// GLOBAL: TIE98 0x6B0EC0
static int g_std3DViewportOverlayRed;
// GLOBAL: TIE98 0x6B0EC4
static int g_std3DViewportOverlayGreen;
// GLOBAL: TIE98 0x6B0EC8
static int g_std3DViewportOverlayBlue;
// GLOBAL: TIE98 0x6B0ECC
static int g_std3DViewportOverlayEnabled;
// GLOBAL: TIE98 0x58C128
static uint8_t g_std3DPaletteConversionSourceRgb[768];
// GLOBAL: TIE98 0x58BF18
static uint16_t g_std3DPaletteScratch16[256];
// GLOBAL: TIE98 0x58BCF8
static uint16_t g_texConvBuf1555[256];
// GLOBAL: TIE98 0x58BA98
static uint16_t g_texConvBuf4444[256];
// FUNCTION: TIE98 0x427230
static IDirectDraw* Renderer_GetDirectDraw(void) { return g_flightDirectDraw; }

// FUNCTION: TIE98 0x4C96C0
static char std3D_PackRenderBitDepths(uint32_t flags) {
	char result = (flags & 0x4000) != 0;
	if (flags & 0x2000)
		result |= 2;
	if (flags & 0x1000)
		result |= 4;
	if (flags & 0x800)
		result |= 8;
	if (flags & 0x400)
		result |= 0x10;
	if (flags & 0x200)
		result |= 0x20;
	if (flags & 0x100)
		result |= 0x40;
	return result;
}

// FUNCTION: TIE98 0x4C9750
static char std3D_PackZCmpCaps(uint32_t flags) {
	char result = (flags & 1) != 0;
	if (flags & 4)
		result |= 4;
	if (flags & 2)
		result |= 2;
	if (flags & 8)
		result |= 8;
	if (flags & 0x10)
		result |= 0x10;
	if (flags & 0x20)
		result |= 0x20;
	if (flags & 0x40)
		result |= 0x40;
	if (flags & 0x80)
		result |= (char)0x80;
	return result;
}

// FUNCTION: TIE98 0x4C97A0
static char std3D_MapZCmpFunc(char caps_mask) {
	char result = (caps_mask & 1) != 0;
	if (caps_mask & 4)
		result |= 3;
	if (caps_mask & 2)
		result |= 2;
	if (caps_mask & 8)
		result |= 4;
	if (caps_mask & 0x10)
		result |= 5;
	if (caps_mask & 0x20)
		result |= 6;
	if (caps_mask & 0x40)
		result |= 7;
	if ((unsigned char)caps_mask & 0x80)
		result |= 8;
	return result;
}

// FUNCTION: TIE98 0x4C63D0
void std3D_BuildRenderTargetDesc(unsigned int width, unsigned int height, int pitch) {
	g_std3DRenderTargetDesc.width = width;
	g_std3DRenderTargetDesc.height = height;
	g_std3DRenderTargetDesc.sizeBytes = height * pitch;
	g_std3DRenderTargetDesc.pitch = pitch;
	g_std3DRenderTargetDesc.widthPixels = (unsigned int)pitch / 2;
	g_std3DRenderTargetDesc.colorInfo = (ColorInfo) {
		.colorMode = STDCOLOR_RGB,
		.bpp = 16,
		.redBPP = 5,
		.greenBPP = 6,
		.blueBPP = 5,
		.redPosShift = 11,
		.greenPosShift = 5,
		.bluePosShift = 0,
		.redPosShiftRight = 3,
		.greenPosShiftRight = 2,
		.bluePosShiftRight = 3,
	};
	g_pStd3DRenderTarget = &g_std3DRenderTargetDesc;
}

// FUNCTION: TIE98 0x4C6490
void std3D_SetRenderSurface(IDirectDrawSurface* render_surface) { g_std3DRenderSurface = render_surface; }

// FUNCTION: TIE98 0x4C64A0
void std3D_ResetUnusedStartupState(int value0, int value1, int value2, int flag) {
	g_std3DViewportOverlayRed = value0;
	g_std3DViewportOverlayGreen = value1;
	g_std3DViewportOverlayBlue = value2;
	g_std3DViewportOverlayEnabled = flag;
}

// FUNCTION: TIE98 0x4C9150
static int AERON_DXAPI std3D_EnumDevicesCallback(DxGuid* guid, char* device_description, char* device_name,
												 D3DDEVICEDESC* hardware, D3DDEVICEDESC* software,
												 void* argument) {
	(void)argument;
	if (g_std3DNumDevices >= STD3D_DEVICE_LIMIT)
		return 0;

	Std3DDevice* device = &g_std3DDevices[g_std3DNumDevices];
	device->guid = *guid;
	strncpy(device->deviceDescription, device_description, sizeof device->deviceDescription);
	strncpy(device->deviceName, device_name, sizeof device->deviceName);
	D3DDEVICEDESC* descriptor = hardware;
	if (hardware->dcmColorModel != 0) {
		device->caps.bHardware = 1;
	} else {
		descriptor = software;
		device->caps.bHardware = 0;
	}
	memcpy(&device->d3dDesc, descriptor, sizeof device->d3dDesc);
	device->caps.colorModelFlags = 0;
	if (descriptor->dcmColorModel & 2)
		device->caps.colorModelFlags = 2;
	if (descriptor->dcmColorModel & 1)
		device->caps.colorModelFlags |= 1;
	device->caps.bTexturePerspective = (descriptor->dpcTriCaps.dwTextureCaps & 1) != 0;
	device->caps.bHasZBuffer = descriptor->dwDeviceZBufferBitDepth != 0;
	device->caps.bSquareOnlyTexture = (descriptor->dpcTriCaps.dwTextureCaps & 0x20) != 0;
	device->caps.bAlphaTexture = (descriptor->dpcTriCaps.dwTextureCaps & 4) != 0;
	device->caps.bStippledShade = (descriptor->dpcTriCaps.dwShadeCaps & 0x1000) == 0 &&
								  (descriptor->dpcTriCaps.dwShadeCaps & 0x2000) != 0;
	device->caps.bAlphaBlend = ((descriptor->dpcTriCaps.dwTextureBlendCaps & 8) != 0 &&
								(descriptor->dpcTriCaps.dwShadeCaps & 0x4000) != 0) ||
							   device->caps.bStippledShade != 0;
	device->caps.bColorKeyTexture = (descriptor->dpcTriCaps.dwTextureCaps & 8) != 0;
	device->caps.renderBitDepthMask =
		(unsigned char)std3D_PackRenderBitDepths(descriptor->dwDeviceRenderBitDepth);
	device->caps.zCmpCapsMask = (unsigned char)std3D_PackZCmpCaps(descriptor->dpcTriCaps.dwZCmpCaps);
	device->caps.minTextureWidth = 1;
	device->caps.minTextureHeight = 1;
	device->caps.maxTextureWidth = 256;
	device->caps.maxTextureHeight = 256;
	device->caps.maxBufferSize = descriptor->dwMaxBufferSize;
	device->caps.maxVertexCount = descriptor->dwMaxVertexCount;
	++g_std3DNumDevices;
	return 1;
}

// FUNCTION: TIE98 0x4C64D0
static int std3D_Log2Floor(unsigned int value) {
	int result = 0;
	while (value > 1) {
		value >>= 1;
		++result;
	}
	return result;
}

// FUNCTION: TIE98 0x4C93B0
static int AERON_DXAPI std3D_EnumTextureFormats(DDSURFACEDESC* descriptor, void* argument) {
	(void)argument;
	if (g_std3DNumTexFormats >= STD3D_TEXTURE_FORMAT_LIMIT)
		return 0;

	Std3DTexFmt* format = &g_std3DTextureFormats[g_std3DNumTexFormats];
	memcpy(&format->ddsd, descriptor, sizeof format->ddsd);
	ColorInfo* color = &format->colorInfo;
	if (descriptor->ddpfPixelFormat.dwFlags & 0x20) {
		color->colorMode = STDCOLOR_PAL;
		color->bpp = 8;
		color->redBPP = 0;
		color->greenBPP = 0;
		color->blueBPP = 0;
		color->redPosShift = 0;
		color->greenPosShift = 0;
		color->bluePosShift = 0;
		color->redPosShiftRight = 0;
		color->greenPosShiftRight = 0;
		color->bluePosShiftRight = 0;
		color->alphaBPP = 0;
		color->alphaPosShift = 0;
		color->alphaPosShiftRight = 0;
	} else {
		if (descriptor->ddpfPixelFormat.dwFlags & 8)
			return 1;

		color->colorMode = descriptor->ddpfPixelFormat.dwFlags & 1 ? STDCOLOR_RGBA : STDCOLOR_RGB;
		color->bpp = (int)descriptor->ddpfPixelFormat.dwRGBBitCount;

		uint32_t mask = descriptor->ddpfPixelFormat.dwRBitMask;
		color->redPosShift = 0;
		while ((mask & 1) == 0) {
			mask >>= 1;
			++color->redPosShift;
		}
		color->redPosShiftRight =
			std3D_Log2Floor(0xffu / (descriptor->ddpfPixelFormat.dwRBitMask >> color->redPosShift));
		color->redBPP = 0;
		while (mask & 1) {
			mask >>= 1;
			++color->redBPP;
		}

		mask = descriptor->ddpfPixelFormat.dwGBitMask;
		color->greenPosShift = 0;
		while ((mask & 1) == 0) {
			mask >>= 1;
			++color->greenPosShift;
		}
		color->greenPosShiftRight =
			std3D_Log2Floor(0xffu / (descriptor->ddpfPixelFormat.dwGBitMask >> color->greenPosShift));
		color->greenBPP = 0;
		while (mask & 1) {
			mask >>= 1;
			++color->greenBPP;
		}

		mask = descriptor->ddpfPixelFormat.dwBBitMask;
		color->bluePosShift = 0;
		while ((mask & 1) == 0) {
			mask >>= 1;
			++color->bluePosShift;
		}
		color->bluePosShiftRight =
			std3D_Log2Floor(0xffu / (descriptor->ddpfPixelFormat.dwBBitMask >> color->bluePosShift));
		color->blueBPP = 0;
		while (mask & 1) {
			mask >>= 1;
			++color->blueBPP;
		}

		if (color->colorMode == STDCOLOR_RGBA) {
			mask = descriptor->ddpfPixelFormat.dwRGBAlphaBitMask;
			color->alphaPosShift = 0;
			while ((mask & 1) == 0) {
				mask >>= 1;
				++color->alphaPosShift;
			}
			color->alphaPosShiftRight = std3D_Log2Floor(
				0xffu / (descriptor->ddpfPixelFormat.dwRGBAlphaBitMask >> color->alphaPosShift));
			color->alphaBPP = 0;
			while (mask & 1) {
				mask >>= 1;
				++color->alphaBPP;
			}
		} else {
			color->alphaBPP = 0;
			color->alphaPosShift = 0;
			color->alphaPosShiftRight = 0;
		}
	}
	++g_std3DNumTexFormats;
	return 1;
}

// FUNCTION: TIE98 0x4C5B30
int std3D_CopyPaletteToScratch16(const uint16_t* palette, int color_count) {
	memcpy(g_std3DPaletteScratch16, palette, (size_t)color_count * sizeof *palette);
	return color_count;
}

// FUNCTION: TIE98 0x4C5B60
uint16_t* std3D_ConvertTexTo1555(uint16_t* source, int count) {
	if (g_pFmtRGBA1555 == g_pFmtRGB565) {
		memcpy(g_texConvBuf1555, source, (size_t)count * sizeof *source);
		return g_texConvBuf1555;
	}
	for (int i = 0; i < count; ++i) {
		uint8_t channel = (uint8_t)((source[i] >> g_pFmtRGB565->colorInfo.redPosShift)
									<< g_pFmtRGB565->colorInfo.redPosShiftRight);
		g_texConvBuf1555[i] = (uint16_t)((channel >> g_pFmtRGBA1555->colorInfo.redPosShiftRight)
										 << g_pFmtRGBA1555->colorInfo.redPosShift);
		channel = (uint8_t)((source[i] >> g_pFmtRGB565->colorInfo.greenPosShift)
							<< g_pFmtRGB565->colorInfo.greenPosShiftRight);
		g_texConvBuf1555[i] |= (uint16_t)((channel >> g_pFmtRGBA1555->colorInfo.greenPosShiftRight)
										  << g_pFmtRGBA1555->colorInfo.greenPosShift);
		channel = (uint8_t)((source[i] >> g_pFmtRGB565->colorInfo.bluePosShift)
							<< g_pFmtRGB565->colorInfo.bluePosShiftRight);
		g_texConvBuf1555[i] |= (uint16_t)((channel >> g_pFmtRGBA1555->colorInfo.bluePosShiftRight)
										  << g_pFmtRGBA1555->colorInfo.bluePosShift);
		if (i != 0)
			g_texConvBuf1555[i] |= (uint16_t)((0xffu >> g_pFmtRGBA1555->colorInfo.alphaPosShiftRight)
											  << g_pFmtRGBA1555->colorInfo.alphaPosShift);
	}
	return g_texConvBuf1555;
}

// FUNCTION: TIE98 0x4C5C60
uint16_t* std3D_ConvertTexTo4444(uint16_t* source, int count) {
	if (g_pFmtRGBA4444 == g_pFmtRGB565) {
		memcpy(g_texConvBuf4444, source, (size_t)count * sizeof *source);
		return g_texConvBuf4444;
	}
	for (int i = 0; i < count; ++i) {
		uint8_t channel = (uint8_t)((source[i] >> g_pFmtRGB565->colorInfo.redPosShift)
									<< g_pFmtRGB565->colorInfo.redPosShiftRight);
		g_texConvBuf4444[i] = (uint16_t)((channel >> g_pFmtRGBA4444->colorInfo.redPosShiftRight)
										 << g_pFmtRGBA4444->colorInfo.redPosShift);
		channel = (uint8_t)((source[i] >> g_pFmtRGB565->colorInfo.greenPosShift)
							<< g_pFmtRGB565->colorInfo.greenPosShiftRight);
		g_texConvBuf4444[i] |= (uint16_t)((channel >> g_pFmtRGBA4444->colorInfo.greenPosShiftRight)
										  << g_pFmtRGBA4444->colorInfo.greenPosShift);
		channel = (uint8_t)((source[i] >> g_pFmtRGB565->colorInfo.bluePosShift)
							<< g_pFmtRGB565->colorInfo.bluePosShiftRight);
		g_texConvBuf4444[i] |= (uint16_t)((channel >> g_pFmtRGBA4444->colorInfo.bluePosShiftRight)
										  << g_pFmtRGBA4444->colorInfo.bluePosShift);
		if (i != 0)
			g_texConvBuf4444[i] |= (uint16_t)((0xffu >> g_pFmtRGBA4444->colorInfo.alphaPosShiftRight)
											  << g_pFmtRGBA4444->colorInfo.alphaPosShift);
	}
	return g_texConvBuf4444;
}

// FUNCTION: TIE98 0x4C6530
static void std3D_BuildColormap16(uint8_t* rgb888, uint16_t* output, ColorInfo* format, uint8_t default_alpha,
								  int color_key) {
	for (int i = 0; i < 256; ++i) {
		uint16_t color = (uint16_t)((rgb888[0] >> format->redPosShiftRight) << format->redPosShift);
		color |= (uint16_t)((rgb888[1] >> format->greenPosShiftRight) << format->greenPosShift);
		color |= (uint16_t)((rgb888[2] >> format->bluePosShiftRight) << format->bluePosShift);
		uint8_t alpha = color_key ? (uint8_t)-(color != 0) : default_alpha;
		if (format->alphaBPP == 1)
			alpha = (uint8_t)(0xff - alpha);
		if (format->alphaBPP != 0) {
			color |= (uint16_t)((alpha >> format->alphaPosShiftRight) << format->alphaPosShift);
		}
		*output++ = color;
		rgb888 += 3;
	}
}

// FUNCTION: TIE98 0x4C6600
static void std3D_BuildColormapOpaque(uint8_t* rgb888, uint16_t* output, ColorInfo* format) {
	std3D_BuildColormap16(rgb888, output, format, 0xff, 0);
}

// FUNCTION: TIE98 0x4C6620
static void std3D_BuildColormapColorKey(uint8_t* rgb888, uint16_t* output, ColorInfo* format) {
	std3D_BuildColormap16(rgb888, output, format, 0xff, 1);
}

// FUNCTION: TIE98 0x4C6640
static void std3D_BuildColormapAlpha(uint8_t* rgb888, uint16_t* output, ColorInfo* format, uint8_t alpha) {
	std3D_BuildColormap16(rgb888, output, format, alpha, 0);
}

// FUNCTION: TIE98 0x4C6720
Std3DVBuffer* std3D_AllocVBuffer(Std3DRasterInfo* raster_info) {
	Std3DVBuffer* vbuffer = malloc(sizeof *vbuffer);
	memset(vbuffer, 0, sizeof *vbuffer);
	vbuffer->storageType = 0;
	vbuffer->raster = *raster_info;
	vbuffer->pixels =
		malloc((size_t)raster_info->width * raster_info->height * (raster_info->bitsPerPixel >> 3));
	vbuffer->raster.rowPitch = raster_info->width * (raster_info->bitsPerPixel >> 3);
	return vbuffer;
}

// FUNCTION: TIE98 0x4C6780
void std3D_FreeVBuffer(Std3DVBuffer* vbuffer) {
	if (vbuffer->storageType == 1) {
		if (vbuffer->ddSurface)
			vbuffer->ddSurface->lpVtbl->Release(vbuffer->ddSurface);
	} else {
		free(vbuffer->pixels);
	}
	memset(vbuffer, 0, sizeof *vbuffer);
	free(vbuffer);
}

// FUNCTION: TIE98 0x4C67C0
void std3D_LockVBuffer(Std3DVBuffer* vbuffer) {
	if (vbuffer->storageType == 1 && vbuffer->lockCount == 0) {
		DDSURFACEDESC descriptor;
		memset(&descriptor, 0, sizeof descriptor);
		descriptor.dwSize = sizeof descriptor;
		if (vbuffer->ddSurface->lpVtbl->Lock(vbuffer->ddSurface, NULL, &descriptor, 1, NULL) != 0)
			return;
		vbuffer->pixels = descriptor.lpSurface;
		vbuffer->raster.rowPitch = (uint32_t)descriptor.lPitch;
	}
	++vbuffer->lockCount;
}

// FUNCTION: TIE98 0x4C68C0
void std3D_UnlockVBuffer(Std3DVBuffer* vbuffer) {
	if (vbuffer->lockCount == 0 ||
		(vbuffer->lockCount == 1 && vbuffer->storageType == 1 &&
		 vbuffer->ddSurface->lpVtbl->Unlock(vbuffer->ddSurface, vbuffer->pixels) != 0))
		return;
	--vbuffer->lockCount;
}

// FUNCTION: TIE98 0x4C69D0
void std3D_BlitVBuffer(Std3DVBuffer* destination, Std3DVBuffer* source, int destination_x,
					   int destination_y) {
	std3D_LockVBuffer(destination);
	std3D_LockVBuffer(source);
	uint8_t* source_row = source->pixels;
	uint8_t* destination_row = (uint8_t*)destination->pixels +
							   (size_t)destination->raster.rowPitch * destination_y +
							   (size_t)(destination->raster.bitsPerPixel >> 3) * destination_x;
	const size_t row_bytes = (size_t)(source->raster.bitsPerPixel >> 3) * source->raster.width;
	for (unsigned int row = 0; row < source->raster.height; ++row) {
		memcpy(destination_row, source_row, row_bytes);
		destination_row += destination->raster.rowPitch;
		source_row += source->raster.rowPitch;
	}
	std3D_UnlockVBuffer(destination);
	std3D_UnlockVBuffer(source);
}

// FUNCTION: TIE98 0x4C6B70
void std3D_FillVBuffer(Std3DVBuffer* vbuffer, int color) {
	std3D_LockVBuffer(vbuffer);
	uint8_t* row = vbuffer->pixels;
	for (unsigned int y = 0; y < vbuffer->raster.height; ++y) {
		switch (vbuffer->raster.bitsPerPixel) {
			case 8:
				memset(row, color, vbuffer->raster.width);
				break;
			case 16:
				for (unsigned int x = 0; x < vbuffer->raster.width; ++x)
					((uint16_t*)row)[x] = (uint16_t)color;
				break;
			case 32:
				for (unsigned int x = 0; x < vbuffer->raster.width; ++x)
					((uint32_t*)row)[x] = (uint32_t)color;
				break;
		}
		row += vbuffer->raster.rowPitch;
	}
	std3D_UnlockVBuffer(vbuffer);
}

// FUNCTION: TIE98 0x4C6840
uint32_t std3D_GetCapabilityFlags(void) { return g_std3DCapFlags; }

// FUNCTION: TIE98 0x4C6880
int std3D_SetFogColor8(uint32_t red, uint32_t green, uint32_t blue) {
	g_std3DFogColorRed8 = (int)red;
	g_std3DFogColorGreen8 = (int)green;
	g_std3DFogColorBlue8 = (int)blue;
	return (int)red;
}

// FUNCTION: TIE98 0x4C68A0
int std3D_SetFogTableRangeBits(uint32_t start_bits, uint32_t end_bits) {
	g_std3DFogTableStartBits = (int)start_bits;
	g_std3DFogTableEndBits = (int)end_bits;
	return (int)start_bits;
}

// FUNCTION: TIE98 0x4C6920
int std3D_SetTextureSizeCaps(int min_width, int min_height, int max_width, int max_height) {
	g_std3DMinTextureWidth = (unsigned int)min_width;
	g_std3DMinTextureHeight = (unsigned int)min_height;
	g_std3DMaxTextureWidth = (unsigned int)max_width;
	g_std3DMaxTextureHeight = (unsigned int)max_height;
	return max_height;
}

// FUNCTION: TIE98 0x4C7510
char std3D_SetPaletteConversionSource(const void* palette_rgb888, uint8_t alpha) {
	memcpy(g_std3DPaletteConversionSourceRgb, palette_rgb888, sizeof g_std3DPaletteConversionSourceRgb);
	if (g_pFmtRGB565->colorInfo.colorMode == STDCOLOR_RGB && g_pFmtRGB565->colorInfo.bpp == 16) {
		std3D_BuildColormapOpaque((uint8_t*)palette_rgb888, g_std3DPaletteScratch16,
								  &g_pFmtRGB565->colorInfo);
	}
	if (g_pStd3DCurDevice->caps.colorModelFlags != 0) {
		if (g_pFmtRGBA1555->colorInfo.colorMode == STDCOLOR_RGBA && g_pFmtRGBA1555->colorInfo.bpp == 16) {
			std3D_BuildColormapColorKey((uint8_t*)palette_rgb888, g_texConvBuf1555,
										&g_pFmtRGBA1555->colorInfo);
		}
		if (g_pStd3DCurDevice->caps.zCmpCapsMask == 0 &&
			g_pFmtRGBA4444->colorInfo.colorMode == STDCOLOR_RGBA && g_pFmtRGBA4444->colorInfo.bpp == 16) {
			std3D_BuildColormapAlpha((uint8_t*)palette_rgb888, g_texConvBuf4444, &g_pFmtRGBA4444->colorInfo,
									 alpha);
		}
	}
	return 1;
}

// FUNCTION: TIE98 0x4C75C0
void std3D_ClampTextureDimensions(uint32_t source_width, uint32_t source_height, uint32_t* output_width,
								  uint32_t* output_height) {
	uint32_t width = source_width < 1                        ? 1
					 : source_width > g_std3DMaxTextureWidth ? g_std3DMaxTextureWidth
															 : source_width;
	uint32_t height = source_height < 1                        ? 1
					  : source_height > g_std3DMaxTextureWidth ? g_std3DMaxTextureWidth
															   : source_height;
	if (width < g_std3DMinTextureWidth || height < g_std3DMinTextureHeight ||
		(g_pStd3DCurDevice->caps.bSquareOnlyTexture && width != height)) {
		if (g_pStd3DCurDevice->caps.bSquareOnlyTexture && width != height) {
			width = width > height ? width : height;
			height = width;
		} else {
			if (width < g_std3DMinTextureWidth)
				width = g_std3DMinTextureWidth;
			if (height < g_std3DMinTextureHeight)
				height = g_std3DMinTextureHeight;
		}
	}
	*output_width = width;
	*output_height = height;
}

// FUNCTION: TIE98 0x4C83F0
static int std3D_FindClosestFormat(const ColorInfo* match, Std3DTexFmt* formats, unsigned int count) {
	int best_index = 0;
	int best_score = 0;
	for (unsigned int i = 0; i < count; ++i) {
		ColorInfo* format = &formats[i].colorInfo;
		int score = 0;
		if (format->colorMode == match->colorMode) {
			score = 1;
			if (format->bpp == match->bpp) {
				score = 2;
				if (match->colorMode == STDCOLOR_RGBA)
					score = 3;
				if (format->redBPP == match->redBPP && format->greenBPP == match->greenBPP &&
					format->blueBPP == match->blueBPP &&
					(match->colorMode != STDCOLOR_RGBA || format->alphaBPP == match->alphaBPP))
					return (int)i;
			}
		}
		if (score > best_score) {
			best_index = (int)i;
			best_score = score;
		}
	}
	return best_index;
}

// FUNCTION: TIE98 0x4C81C0
static char std3D_QueryTextureVidMem(unsigned int* total_bytes, unsigned int* free_bytes) {
	IDirectDraw* direct_draw2 = NULL;
	if (g_std3DDirectDraw->lpVtbl->QueryInterface(g_std3DDirectDraw, &CLSID_IDirectDraw2,
												  (void**)&direct_draw2) != 0)
		return 0;
	DDSCAPS caps = { DDSCAPS_TEXTURE };
	if (direct_draw2->lpVtbl->GetAvailableVidMem(direct_draw2, &caps, total_bytes, free_bytes) != 0)
		return 0;
	direct_draw2->lpVtbl->Release(direct_draw2);
	return 1;
}

// FUNCTION: TIE98 0x4C80A0
static void std3D_CacheListAppend(Std3DTextureSurface* surface) {
	if (g_std3DTextureCacheHead) {
		g_std3DTextureCacheTail->pNext = surface;
		surface->pPrev = g_std3DTextureCacheTail;
		surface->pNext = NULL;
		g_std3DTextureCacheTail = surface;
	} else {
		g_std3DTextureCacheHead = surface;
		g_std3DTextureCacheTail = surface;
		surface->pPrev = NULL;
		surface->pNext = NULL;
	}
	++g_std3DTextureSurfaceCount;
	g_pStd3DCurDevice->availableMemory -= surface->byteSize;
}

// FUNCTION: TIE98 0x4C8110
static void std3D_CacheListRemove(Std3DTextureSurface* surface) {
	if (surface == g_std3DTextureCacheHead) {
		g_std3DTextureCacheHead = surface->pNext;
		if (g_std3DTextureCacheHead) {
			g_std3DTextureCacheHead->pPrev = NULL;
			if (!g_std3DTextureCacheHead->pNext)
				g_std3DTextureCacheTail = g_std3DTextureCacheHead;
		} else {
			g_std3DTextureCacheTail = NULL;
		}
	} else if (surface == g_std3DTextureCacheTail) {
		g_std3DTextureCacheTail = surface->pPrev;
		g_std3DTextureCacheTail->pNext = NULL;
	} else {
		surface->pPrev->pNext = surface->pNext;
		surface->pNext->pPrev = surface->pPrev;
	}
	--g_std3DTextureSurfaceCount;
	g_pStd3DCurDevice->availableMemory += surface->byteSize;
}

// FUNCTION: TIE98 0x4C8020
void std3D_FlushTextureCache(void) {
	Std3DTextureSurface* surface = g_std3DTextureCacheHead;
	while (surface) {
		Std3DTextureSurface* next = surface->pNext;
		if (surface->pCachedSurface) {
			surface->pCachedSurface->lpVtbl->Release(surface->pCachedSurface);
			surface->pCachedSurface = NULL;
		}
		if (surface->pCachedTexture) {
			surface->pCachedTexture->lpVtbl->Release(surface->pCachedTexture);
			surface->pCachedTexture = NULL;
		}
		surface->bCached = 0;
		surface->cacheFrameTag = 0;
		surface->pPrev = NULL;
		surface->pNext = NULL;
		surface = next;
	}
	g_std3DTextureCacheHead = NULL;
	g_std3DTextureCacheTail = NULL;
	g_std3DTextureSurfaceCount = 0;
	g_pStd3DCurDevice->availableMemory = g_pStd3DCurDevice->totalMemory;
	g_std3DTextureFrameTag = 1;
}

// FUNCTION: TIE98 0x4C8230
void std3D_CacheTextureSurface(Std3DTextureSurface* surface) {
	surface->cacheFrameTag = g_std3DTextureFrameTag;
	std3D_CacheListRemove(surface);
	std3D_CacheListAppend(surface);
}

// FUNCTION: TIE98 0x4C7660
char std3D_CreateMipSurface(Std3DVBuffer* source, Std3DTextureSurface* surface, int texture_format_mode,
							int alpha_mask) {
	IDirectDrawSurface* source_surface = NULL;
	IDirectDrawSurface* cached_surface = NULL;
	IDirect3DTexture* source_texture = NULL;
	IDirect3DTexture* cached_texture = NULL;
	Std3DVBuffer* scaled = NULL;
	Std3DVBuffer* input = source;
	uint32_t width = source->raster.width;
	if (width == 0)
		width = 1;
	else if (width > 256)
		width = 256;
	uint32_t height = source->raster.height;
	if (height == 0)
		height = 1;
	else if (height > 256)
		height = 256;

	if (width < g_std3DMinTextureWidth || height < g_std3DMinTextureHeight ||
		(g_pStd3DCurDevice->caps.bSquareOnlyTexture && width != height)) {
		Std3DRasterInfo raster = source->raster;
		uint32_t target_width;
		uint32_t target_height;
		if (g_pStd3DCurDevice->caps.bSquareOnlyTexture && width != height) {
			target_width = width > height ? width : height;
			target_height = target_width;
		} else {
			target_width = width < g_std3DMinTextureWidth ? g_std3DMinTextureWidth : width;
			target_height = height < g_std3DMinTextureHeight ? g_std3DMinTextureHeight : height;
		}
		const unsigned int x_tiles = (unsigned int)((double)target_width / (double)(int)width + 0.5);
		const unsigned int y_tiles = (unsigned int)((double)target_height / (double)(int)height + 0.5);
		raster.width *= x_tiles;
		raster.height *= y_tiles;
		scaled = std3D_AllocVBuffer(&raster);
		for (unsigned int y = 0; y < y_tiles; ++y) {
			for (unsigned int x = 0; x < x_tiles; ++x) {
				std3D_BlitVBuffer(scaled, source, (int)(x * width), (int)(y * height));
			}
		}
		input = scaled;
		width = raster.width;
		height = raster.height;
	}

	DDSURFACEDESC descriptor;
	if (texture_format_mode != 0 && g_pStd3DCurDevice->caps.bAlphaTexture != 0) {
		surface->usesAlphaFormat = 1;
		descriptor = alpha_mask ? g_pFmtRGBA4444->ddsd : g_pFmtRGBA1555->ddsd;
	} else if (alpha_mask) {
		surface->usesAlphaFormat = 1;
		descriptor = g_pFmtRGBA4444->ddsd;
	} else {
		surface->usesAlphaFormat = 0;
		descriptor = g_pFmtRGB565->ddsd;
	}
	descriptor.dwSize = 108;
	descriptor.dwFlags = DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH | DDSD_PIXELFORMAT;
	descriptor.dwWidth = width;
	descriptor.dwHeight = height;
	descriptor.ddsCaps.dwCaps = DDSCAPS_TEXTURE | DDSCAPS_SYSTEMMEMORY;
	HRESULT result =
		g_std3DDirectDraw->lpVtbl->CreateSurface(g_std3DDirectDraw, &descriptor, &source_surface, NULL);
	if (result != 0)
		goto fail;

	DDSURFACEDESC lock;
	memset(&lock, 0, sizeof lock);
	lock.dwSize = 108;
	result = source_surface->lpVtbl->Lock(source_surface, NULL, &lock, 1, NULL);
	if (result != 0)
		goto fail;
	if (input->raster.sourceType <= 2) {
		std3D_LockVBuffer(input);
		for (uint32_t y = 0; y < height; ++y) {
			uint8_t* destination = (uint8_t*)lock.lpSurface + (size_t)lock.lPitch * y;
			uint8_t* source_row = (uint8_t*)input->pixels + (size_t)input->raster.rowPitch * y;
			if (input->raster.sourceType != 0) {
				memcpy(destination, source_row, (size_t)width * 2);
			} else {
				uint16_t* output = (uint16_t*)destination;
				uint16_t* palette = g_std3DPaletteScratch16;
				if (texture_format_mode != 0 && g_pStd3DCurDevice->caps.bAlphaTexture != 0)
					palette = alpha_mask ? g_texConvBuf4444 : g_texConvBuf1555;
				else if (alpha_mask)
					palette = g_texConvBuf4444;
				for (uint32_t x = 0; x < width; ++x)
					output[x] = palette[source_row[x]];
			}
		}
		std3D_UnlockVBuffer(input);
	}
	result = source_surface->lpVtbl->Unlock(source_surface, NULL);
	if (result != 0)
		goto fail;

	if (texture_format_mode != 0 && g_pStd3DCurDevice->caps.bAlphaTexture == 0) {
		DDCOLORKEY key;
		if (input->raster.sourceType == 0) {
			key.dwColorSpaceLowValue = g_std3DPaletteScratch16[0];
			key.dwColorSpaceHighValue = g_std3DPaletteScratch16[0];
			source_surface->lpVtbl->SetColorKey(source_surface, DDCKEY_SRCBLT, &key);
		} else if (input->raster.sourceType == 1) {
			key.dwColorSpaceLowValue = input->transparentColor;
			key.dwColorSpaceHighValue = input->transparentColor;
			source_surface->lpVtbl->SetColorKey(source_surface, DDCKEY_SRCBLT, &key);
		}
		/* PORT: TIE98 passes an uninitialized key for source type 2. That
		 * has no source transparency value to preserve, so omit the call. */
	}
	result = source_surface->lpVtbl->QueryInterface(source_surface, &IID_IDirect3DTexture_Compat,
													(void**)&source_texture);
	if (result != 0)
		goto fail;
	result = source_surface->lpVtbl->GetSurfaceDesc(source_surface, &descriptor);
	if (result != 0)
		goto fail;
	surface->ddsd = descriptor;
	descriptor.dwFlags = DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH | DDSD_PIXELFORMAT;
	descriptor.ddsCaps.dwCaps = DDSCAPS_TEXTURE | DDSCAPS_VIDEOMEMORY | DDSCAPS_ALLOCONLOAD;
	result = g_std3DDirectDraw->lpVtbl->CreateSurface(g_std3DDirectDraw, &descriptor, &cached_surface, NULL);
	if (result == DX_DDERR_OUTOFVIDEOMEMORY) {
		Std3DTextureSurface* candidate = g_std3DTextureCacheHead;
		const unsigned int needed = width * height;
		for (;;) {
			unsigned int freed = 0;
			while (freed < needed && candidate && candidate->cacheFrameTag != g_std3DTextureFrameTag) {
				Std3DTextureSurface* next = candidate->pNext;
				candidate->pCachedSurface->lpVtbl->Release(candidate->pCachedSurface);
				candidate->pCachedTexture->lpVtbl->Release(candidate->pCachedTexture);
				freed += candidate->byteSize;
				candidate->bCached = 0;
				std3D_CacheListRemove(candidate);
				candidate = next;
			}
			if (freed < needed)
				goto fail;
			result = g_std3DDirectDraw->lpVtbl->CreateSurface(g_std3DDirectDraw, &descriptor, &cached_surface,
															  NULL);
			if (result == 0)
				break;
			if (result != DX_DDERR_OUTOFVIDEOMEMORY)
				goto fail;
		}
	} else if (result != 0) {
		goto fail;
	}
	result = cached_surface->lpVtbl->QueryInterface(cached_surface, &IID_IDirect3DTexture_Compat,
													(void**)&cached_texture);
	if (result != 0)
		goto fail;
	result = cached_texture->lpVtbl->Load(cached_texture, source_texture);
	if (result != 0)
		goto fail;
	D3DTEXTUREHANDLE handle = 0;
	result = cached_texture->lpVtbl->GetHandle(cached_texture, g_d3dDevice, &handle);
	if (result != 0)
		handle = 0;
	source_texture->lpVtbl->Release(source_texture);
	source_surface->lpVtbl->Release(source_surface);
	if (scaled)
		std3D_FreeVBuffer(scaled);
	surface->pCachedTexture = cached_texture;
	surface->pCachedSurface = cached_surface;
	surface->texHandle = handle;
	surface->width = width;
	surface->height = height;
	surface->byteSize = width * height;
	surface->bCached = 1;
	surface->cacheFrameTag = g_std3DTextureFrameTag;
	std3D_CacheListAppend(surface);
	return 1;

fail:
	if (source_surface)
		source_surface->lpVtbl->Release(source_surface);
	if (source_texture)
		source_texture->lpVtbl->Release(source_texture);
	if (scaled)
		std3D_FreeVBuffer(scaled);
	if (cached_surface)
		cached_surface->lpVtbl->Release(cached_surface);
	if (cached_texture)
		cached_texture->lpVtbl->Release(cached_texture);
	surface->pCachedTexture = NULL;
	surface->pCachedSurface = NULL;
	surface->texHandle = 0;
	surface->bCached = 0;
	surface->cacheFrameTag = 0;
	return 0;
}

// FUNCTION: TIE98 0x4C5D60
int std3D_Startup(void) {
	g_std3DDirectDraw = Renderer_GetDirectDraw();
	if (!g_std3DDirectDraw)
		return 0;
	g_std3DCapFlags = 6579;
	g_std3DZBufferEnabled = 1;
	HRESULT result =
		g_std3DDirectDraw->lpVtbl->QueryInterface(g_std3DDirectDraw, &CLSID_IDirect3D, (void**)&g_lpD3D);
	if (result != 0)
		return 0;
	g_std3DNumDevices = 0;
	result = g_lpD3D->lpVtbl->EnumDevices(g_lpD3D, std3D_EnumDevicesCallback, NULL);
	if (result != 0 || g_std3DNumDevices == 0)
		return 0;
	g_std3DStartupDone = 1;
	return 1;
}

// FUNCTION: TIE98 0x4C5EA0
void std3D_Shutdown(void) {
	if (g_lpD3D)
		g_lpD3D->lpVtbl->Release(g_lpD3D);
	g_std3DStartupDone = 0;
}

// FUNCTION: TIE98 0x4C8330
unsigned int std3D_SelectBestDevice(const Std3DDeviceCaps* required_caps) {
	unsigned int best_index = 0;
	int best_score = 0;
	for (unsigned int i = 0; i < g_std3DNumDevices; ++i) {
		const Std3DDeviceCaps* caps = &g_std3DDevices[i].caps;
		int score = 0;
		if (!required_caps->bTexturePerspective ||
			caps->bTexturePerspective == required_caps->bTexturePerspective) {
			score = 1;
			if (!required_caps->bHasZBuffer || caps->bHasZBuffer == required_caps->bHasZBuffer) {
				score = 2;
				if ((required_caps->colorModelFlags & caps->colorModelFlags) != 0) {
					score = 3;
					if (caps->bHardware == required_caps->bHardware)
						return i;
				}
			}
		}
		if (score > best_score) {
			best_index = i;
			best_score = score;
		}
	}
	return best_index;
}

// FUNCTION: TIE98 0x4C8F00
static char std3D_CreateZBuffer(int width, int height) {
	DDSURFACEDESC descriptor;
	memset(&descriptor, 0, sizeof descriptor);
	descriptor.dwSize = 108;
	descriptor.dwFlags = 71;
	descriptor.ddsCaps.dwCaps = 0x20000;
	descriptor.dwWidth = (uint32_t)width;
	descriptor.dwHeight = (uint32_t)height;
	if (g_pStd3DCurDevice->caps.bHardware)
		descriptor.ddsCaps.dwCaps |= 0x4000;
	else
		descriptor.ddsCaps.dwCaps |= 0x800;
	if (g_pStd3DCurDevice->d3dDesc.dwDeviceZBufferBitDepth & 0x100)
		descriptor.dwZBufferBitDepth = 32;
	else if (g_pStd3DCurDevice->d3dDesc.dwDeviceZBufferBitDepth & 0x400)
		descriptor.dwZBufferBitDepth = 16;
	else if (g_pStd3DCurDevice->d3dDesc.dwDeviceZBufferBitDepth & 0x800)
		descriptor.dwZBufferBitDepth = 8;
	else
		return 0;
	HRESULT result = g_std3DDirectDraw->lpVtbl->CreateSurface(g_std3DDirectDraw, &descriptor,
															  &g_std3DZBufferSurface, NULL);
	if (result != 0)
		return 0;
	result = g_std3DRenderSurface->lpVtbl->AddAttachedSurface(g_std3DRenderSurface, g_std3DZBufferSurface);
	if (result != 0)
		return 0;
	return g_std3DZBufferSurface->lpVtbl->GetSurfaceDesc(g_std3DZBufferSurface, &descriptor) == 0;
}

// FUNCTION: TIE98 0x4C8800
static void std3D_BuildViewportQuad(const Std3DViewportRect* rect) {
	g_std3DQuadRect = *rect;
	memset(g_std3DQuadVerts, 0, sizeof g_std3DQuadVerts);
	g_std3DQuadVerts[0].sx = (float)rect->x;
	g_std3DQuadVerts[0].sy = (float)rect->y;
	g_std3DQuadVerts[1].sx = (float)(rect->x + rect->width);
	g_std3DQuadVerts[1].sy = g_std3DQuadVerts[0].sy;
	g_std3DQuadVerts[2].sx = g_std3DQuadVerts[1].sx;
	g_std3DQuadVerts[2].sy = (float)(rect->y + rect->height);
	g_std3DQuadVerts[3].sx = g_std3DQuadVerts[0].sx;
	g_std3DQuadVerts[3].sy = g_std3DQuadVerts[2].sy;

	g_std3DQuadTris[0].v0 = 0;
	g_std3DQuadTris[0].v1 = 1;
	g_std3DQuadTris[0].v2 = 2;
	g_std3DQuadTris[0].flags = 0x8200;
	g_std3DQuadTris[0].texture = NULL;
	g_std3DQuadTris[1].v0 = 0;
	g_std3DQuadTris[1].v1 = 2;
	g_std3DQuadTris[1].v2 = 3;
	g_std3DQuadTris[1].flags = 0x8200;
	g_std3DQuadTris[1].texture = NULL;
}

// FUNCTION: TIE98 0x4C8D70
static char std3D_CreateViewport(unsigned int width, unsigned int height) {
	HRESULT result = g_lpD3D->lpVtbl->CreateViewport(g_lpD3D, &g_d3dViewport, NULL);
	if (result != 0)
		return 0;
	result = g_d3dDevice->lpVtbl->AddViewport(g_d3dDevice, g_d3dViewport);
	if (result != 0)
		return 0;
	D3DVIEWPORT viewport;
	memset(&viewport, 0, sizeof viewport);
	viewport.dwSize = sizeof viewport;
	viewport.dwWidth = width;
	viewport.dwHeight = height;
	viewport.dvScaleX = (float)width * 0.5f;
	viewport.dvScaleY = (float)height * 0.5f;
	viewport.dvMaxX = (float)width / (viewport.dvScaleX * 2.0f);
	viewport.dvMaxY = (float)height / (viewport.dvScaleY * 2.0f);
	if (g_d3dViewport->lpVtbl->SetViewport(g_d3dViewport, &viewport) != 0)
		return 0;
	const Std3DViewportRect rect = { 0, 0, (int)width, (int)height };
	std3D_BuildViewportQuad(&rect);
	return 1;
}

// FUNCTION: TIE98 0x4C88F0
static char std3D_SetInitialRenderState(void) {
	D3DEXECUTEBUFFERDESC descriptor = {
		.dwSize = sizeof descriptor,
		.dwFlags = 1,
		.dwBufferSize = 4096,
	};
	IDirect3DExecuteBuffer* buffer = NULL;
	g_d3dDevice->lpVtbl->CreateExecuteBuffer(g_d3dDevice, &descriptor, &buffer, NULL);
	buffer->lpVtbl->Lock(buffer, &descriptor);
	memset(descriptor.lpData, 0, descriptor.dwBufferSize);

	uint8_t* base = descriptor.lpData;
	D3DINSTRUCTION* instruction = (D3DINSTRUCTION*)base;
	instruction->bOpcode = D3DOP_STATERENDER;
	instruction->bSize = sizeof(D3DSTATE);
	instruction->wCount = 25;
	D3DSTATE* states = (D3DSTATE*)(instruction + 1);
	states[0] = (D3DSTATE) { D3DRENDERSTATE_TEXTUREPERSPECTIVE, (g_std3DCapFlags & 1) != 0 };
	states[1] = (D3DSTATE) { D3DRENDERSTATE_TEXTUREMAG, ((g_std3DCapFlags & 0x80) != 0) + 1 };
	states[2] = (D3DSTATE) { D3DRENDERSTATE_TEXTUREMIN, ((g_std3DCapFlags & 0x100) != 0) + 1 };
	states[3] = (D3DSTATE) { D3DRENDERSTATE_SUBPIXEL, (g_std3DCapFlags & 0x10) != 0 };
	states[4] = (D3DSTATE) { D3DRENDERSTATE_SUBPIXELX, (g_std3DCapFlags & 0x20) != 0 };
	states[5] = (D3DSTATE) { D3DRENDERSTATE_WRAPU, 0 };
	states[6] = (D3DSTATE) { D3DRENDERSTATE_WRAPV, 0 };
	states[7] = (D3DSTATE) { D3DRENDERSTATE_BLENDENABLE, (g_std3DCapFlags & 0x600) != 0 };
	if (g_std3DCapFlags & 0x600) {
		states[8] = (D3DSTATE) { D3DRENDERSTATE_TEXTUREMAPBLEND, (g_std3DCapFlags & 0x400) ? 4u : 2u };
		states[9] = (D3DSTATE) { D3DRENDERSTATE_SRCBLEND, 5 };
		states[10] = (D3DSTATE) { D3DRENDERSTATE_DESTBLEND, 6 };
	} else {
		states[8] = (D3DSTATE) { D3DRENDERSTATE_TEXTUREMAPBLEND, 2 };
		states[9] = (D3DSTATE) { D3DRENDERSTATE_SRCBLEND, 2 };
		states[10] = (D3DSTATE) { D3DRENDERSTATE_DESTBLEND, 1 };
	}
	states[11] = (D3DSTATE) { D3DRENDERSTATE_ALPHATESTENABLE, 1 };
	states[12] = (D3DSTATE) { D3DRENDERSTATE_ALPHAFUNC, 6 };
	states[13] = (D3DSTATE) { D3DRENDERSTATE_STIPPLEDALPHA, g_pStd3DCurDevice->caps.renderBitDepthMask != 0 };
	states[14] = (D3DSTATE) { D3DRENDERSTATE_SHADEMODE, 2 };
	states[15] = (D3DSTATE) { D3DRENDERSTATE_MONOENABLE, (g_std3DCapFlags & 0x8000) == 0 };
	states[16] = (D3DSTATE) { D3DRENDERSTATE_SPECULARENABLE, (g_std3DCapFlags & 4) != 0 };
	states[17] = (D3DSTATE) { D3DRENDERSTATE_FOGENABLE, (g_std3DCapFlags & 0x40) != 0 };
	states[18] = (D3DSTATE) { D3DRENDERSTATE_FILLMODE, 3 };
	states[19] = (D3DSTATE) { D3DRENDERSTATE_DITHERENABLE, (g_std3DCapFlags & 2) != 0 };
	states[20] = (D3DSTATE) { D3DRENDERSTATE_ANTIALIAS, (g_std3DCapFlags & 8) != 0 };
	states[21] = (D3DSTATE) { D3DRENDERSTATE_ZENABLE, g_std3DZBufferEnabled != 0 };
	states[22] = (D3DSTATE) { D3DRENDERSTATE_ZWRITEENABLE, g_std3DZBufferEnabled != 0 };
	states[23] =
		(D3DSTATE) { D3DRENDERSTATE_ZFUNC, (unsigned char)std3D_MapZCmpFunc(g_std3DZBufferBitDepth) };
	states[24] = (D3DSTATE) { D3DRENDERSTATE_CULLMODE, 1 };
	instruction = (D3DINSTRUCTION*)&states[25];
	instruction->bOpcode = D3DOP_EXIT;
	instruction->bSize = 0;
	instruction->wCount = 0;
	const uint32_t instruction_length = (uint32_t)((uint8_t*)(instruction + 1) - (uint8_t*)descriptor.lpData);
	buffer->lpVtbl->Unlock(buffer);
	D3DEXECUTEDATA data;
	memset(&data, 0, sizeof data);
	data.dwSize = sizeof data;
	data.dwInstructionLength = instruction_length;
	buffer->lpVtbl->SetExecuteData(buffer, &data);
	g_d3dDevice->lpVtbl->BeginScene(g_d3dDevice);
	g_d3dDevice->lpVtbl->Execute(g_d3dDevice, buffer, g_d3dViewport, D3DEXECUTE_UNCLIPPED);
	g_d3dDevice->lpVtbl->EndScene(g_d3dDevice);
	buffer->lpVtbl->Release(buffer);
	g_d3dStateFlags = (Std3DRenderStateFlags)g_std3DCapFlags;
	return 1;
}

// FUNCTION: TIE98 0x4C5ED0
int std3D_CreateDevice(unsigned int device_index, int use_z_buffer) {
	if (g_std3DDeviceOpen || device_index >= g_std3DNumDevices)
		return 0;
	g_std3DCurDeviceIdx = device_index;
	g_pStd3DCurDevice = &g_std3DDevices[device_index];
	if (use_z_buffer && g_pStd3DCurDevice->caps.bHasZBuffer && (g_std3DCapFlags & 0x1800)) {
		g_std3DZBufferEnabled = 1;
		if (!std3D_CreateZBuffer((int)g_pStd3DRenderTarget->width, (int)g_pStd3DRenderTarget->height))
			return 0;
		g_std3DZBufferBitDepth = (g_pStd3DCurDevice->caps.zCmpCapsMask & 0x10) ? 16 : 2;
	} else {
		g_std3DZBufferEnabled = 0;
	}
	HRESULT result = g_std3DRenderSurface->lpVtbl->QueryInterface(
		g_std3DRenderSurface, &g_pStd3DCurDevice->guid, (void**)&g_d3dDevice);
	if (result != 0)
		return 0;
	g_std3DNumTexFormats = 0;
	result = g_d3dDevice->lpVtbl->EnumTextureFormats(g_d3dDevice, (void*)std3D_EnumTextureFormats, NULL);
	if (result != 0 || g_std3DNumTexFormats == 0)
		return 0;
	if (!std3D_CreateViewport(g_pStd3DRenderTarget->width, g_pStd3DRenderTarget->height) ||
		!std3D_SetInitialRenderState())
		return 0;
	g_std3DExecBufSize =
		g_pStd3DCurDevice->caps.maxBufferSize ? g_pStd3DCurDevice->caps.maxBufferSize : 0x10000;
	g_d3dExecBufDesc = (D3DEXECUTEBUFFERDESC) {
		.dwSize = sizeof g_d3dExecBufDesc,
		.dwFlags = 1,
		.dwBufferSize = g_std3DExecBufSize,
	};
	g_std3DExecBufMaxVerts = g_pStd3DCurDevice->caps.maxVertexCount;
	if (g_std3DExecBufMaxVerts == 0 || g_std3DExecBufMaxVerts >= 512)
		g_std3DExecBufMaxVerts = 512;
	g_d3dDevice->lpVtbl->CreateExecuteBuffer(g_d3dDevice, &g_d3dExecBufDesc, &g_d3dExecuteBuffer, NULL);
	g_std3DTextureFrameTag = 1;
	g_std3DTextureSurfaceCount = 0;
	g_std3DTextureCacheHead = NULL;
	g_std3DTextureCacheTail = NULL;
	g_fmtIdxRGB565 = std3D_FindClosestFormat(&g_pStd3DRenderTarget->colorInfo, g_std3DTextureFormats,
											 g_std3DNumTexFormats);
	g_pFmtRGB565 = &g_std3DTextureFormats[g_fmtIdxRGB565];
	if (g_pStd3DCurDevice->caps.bAlphaTexture) {
		ColorInfo match;
		match.colorMode = STDCOLOR_RGBA;
		match.bpp = 16;
		match.redBPP = 5;
		match.greenBPP = 5;
		match.blueBPP = 5;
		match.alphaBPP = 1;
		g_fmtIdxRGBA1555 = std3D_FindClosestFormat(&match, g_std3DTextureFormats, g_std3DNumTexFormats);
		g_pFmtRGBA1555 = &g_std3DTextureFormats[g_fmtIdxRGBA1555];
		if (!g_pStd3DCurDevice->caps.bAlphaBlend) {
			match.redBPP = 4;
			match.greenBPP = 4;
			match.blueBPP = 4;
			match.alphaBPP = 4;
			g_fmtIdxRGBA4444 = std3D_FindClosestFormat(&match, g_std3DTextureFormats, g_std3DNumTexFormats);
			g_pFmtRGBA4444 = &g_std3DTextureFormats[g_fmtIdxRGBA4444];
			Std3DRasterInfo raster;
			raster.width = 32;
			raster.height = 32;
			memcpy(&raster.sourceType, &g_pFmtRGBA4444->colorInfo, sizeof(ColorInfo));
			g_pStd3DVBuffer = std3D_AllocVBuffer(&raster);
		}
	}
	std3D_QueryTextureVidMem(&g_pStd3DCurDevice->totalMemory, &g_pStd3DCurDevice->availableMemory);
	g_std3DDeviceOpen = 1;
	return 1;
}

// FUNCTION: TIE98 0x4C6660
void std3D_DestroyDevice(void) {
	if (!g_std3DDeviceOpen)
		return;
	if (g_pStd3DVBuffer) {
		std3D_FreeVBuffer(g_pStd3DVBuffer);
		g_pStd3DVBuffer = NULL;
	}
	g_d3dExecuteBuffer->lpVtbl->Release(g_d3dExecuteBuffer);
	std3D_FlushTextureCache();
	if (g_d3dViewport) {
		g_d3dViewport->lpVtbl->Release(g_d3dViewport);
		g_d3dViewport = NULL;
	}
	if (g_std3DZBufferSurface) {
		g_std3DZBufferSurface->lpVtbl->Release(g_std3DZBufferSurface);
		g_std3DZBufferSurface = NULL;
	}
	if (g_d3dDevice) {
		g_d3dDevice->lpVtbl->Release(g_d3dDevice);
		g_d3dDevice = NULL;
	}
	g_std3DDeviceOpen = 0;
}

// FUNCTION: TIE98 0x4C6950
char std3D_StartScene(void) { return (char)g_d3dDevice->lpVtbl->BeginScene(g_d3dDevice); }

// FUNCTION: TIE98 0x4C6990
char std3D_EndScene(void) { return (char)g_d3dDevice->lpVtbl->EndScene(g_d3dDevice); }

// FUNCTION: TIE98 0x4C6A80
char std3D_LockExecuteBuffer(void) {
	g_d3dBufVertCount = 0;
	g_std3DExecBufTriCount = 0;
	++g_std3DTextureFrameTag;
	g_d3dCurTexture = (Std3DTextureSurface*)(uintptr_t)1;
	if (g_d3dExecuteBuffer->lpVtbl->Lock(g_d3dExecuteBuffer, &g_d3dExecBufDesc) != 0)
		return 0;
	g_d3dExecBufBase = (uint8_t*)g_d3dExecBufDesc.lpData;
	g_d3dWritePtr = g_d3dExecBufBase;
	return 1;
}

// FUNCTION: TIE98 0x4C6B00
char std3D_AddVertices(D3DTLVERTEX* vertices, int count) {
	if ((unsigned int)(count + g_d3dBufVertCount) > g_std3DExecBufMaxVerts)
		return 0;
	if ((D3DTLVERTEX*)g_d3dWritePtr != vertices)
		memcpy(g_d3dWritePtr, vertices, (size_t)count * sizeof *vertices);
	g_d3dWritePtr += (size_t)count * sizeof *vertices;
	g_d3dBufVertCount += count;
	return 1;
}

// FUNCTION: TIE98 0x4C6C90
char std3D_BeginInstructions(void) {
	g_d3dInstrStart = g_d3dWritePtr;
	D3DINSTRUCTION* instruction = (D3DINSTRUCTION*)g_d3dWritePtr;
	instruction->bOpcode = D3DOP_PROCESSVERTICES;
	instruction->bSize = sizeof(D3DPROCESSVERTICES);
	instruction->wCount = 1;
	g_d3dWritePtr += sizeof *instruction;
	D3DPROCESSVERTICES* process = (D3DPROCESSVERTICES*)g_d3dWritePtr;
	process->dwFlags = D3DPROCESSVERTICES_COPY;
	process->wStart = 0;
	process->wDest = 0;
	process->dwCount = (uint32_t)g_d3dBufVertCount;
	process->dwReserved = 0;
	g_d3dWritePtr += sizeof *process;
	return 1;
}

// FUNCTION: TIE98 0x4C6D10
char std3D_AddTriangles(Std3DRenderTri* triangles, unsigned int count) {
	unsigned int triangle_index = 0;
	while (triangle_index < count) {
		Std3DTextureSurface* texture = triangles->texture;
		Std3DRenderStateFlags flags = triangles->flags;
		unsigned int run = 0;
		while (triangle_index + run < count && triangles[run].texture == texture &&
			   triangles[run].flags == flags)
			++run;
		std3D_SetRenderState(flags);
		if (texture != g_d3dCurTexture) {
			D3DINSTRUCTION* instruction = (D3DINSTRUCTION*)g_d3dWritePtr;
			instruction->bOpcode = D3DOP_STATERENDER;
			instruction->bSize = sizeof(D3DSTATE);
			instruction->wCount = 1;
			g_d3dWritePtr += sizeof *instruction;
			D3DSTATE* state = (D3DSTATE*)g_d3dWritePtr;
			state->dwState = D3DRENDERSTATE_TEXTUREHANDLE;
			state->dwArg = texture ? texture->texHandle : 0;
			g_d3dWritePtr += sizeof *state;
			g_d3dCurTexture = texture;
		}
		D3DINSTRUCTION* instruction = (D3DINSTRUCTION*)g_d3dWritePtr;
		instruction->bOpcode = D3DOP_TRIANGLE;
		instruction->bSize = sizeof(D3DTRIANGLE);
		instruction->wCount = (uint16_t)run;
		g_d3dWritePtr += sizeof *instruction;
		for (unsigned int i = 0; i < run; ++i) {
			D3DTRIANGLE* output = (D3DTRIANGLE*)g_d3dWritePtr;
			output->v1 = (uint16_t)triangles[i].v0;
			output->v2 = (uint16_t)triangles[i].v1;
			output->v3 = (uint16_t)triangles[i].v2;
			output->wFlags = 0x700;
			g_d3dWritePtr += sizeof *output;
		}
		triangles += run;
		triangle_index += run;
	}
	g_std3DExecBufTriCount += count;
	return 1;
}

// FUNCTION: TIE98 0x4C6F90
char std3D_ExecuteBuffer(void) {
	D3DINSTRUCTION* instruction = (D3DINSTRUCTION*)g_d3dWritePtr;
	instruction->bOpcode = D3DOP_EXIT;
	instruction->bSize = 0;
	instruction->wCount = 0;
	g_d3dWritePtr += sizeof *instruction;
	if (g_d3dExecuteBuffer->lpVtbl->Unlock(g_d3dExecuteBuffer) != 0)
		return 0;
	D3DEXECUTEDATA data;
	memset(&data, 0, sizeof data);
	data.dwSize = sizeof data;
	data.dwVertexCount = (uint32_t)g_d3dBufVertCount;
	data.dwInstructionOffset = (uint32_t)(g_d3dInstrStart - g_d3dExecBufBase);
	data.dwInstructionLength = (uint32_t)(g_d3dWritePtr - g_d3dInstrStart);
	g_d3dExecuteBuffer->lpVtbl->SetExecuteData(g_d3dExecuteBuffer, &data);
	return g_d3dDevice->lpVtbl->Execute(g_d3dDevice, g_d3dExecuteBuffer, g_d3dViewport,
										D3DEXECUTE_UNCLIPPED) == 0;
}

// FUNCTION: TIE98 0x4C70A0
void std3D_SetRenderState(Std3DRenderStateFlags flags) {
	if (g_d3dStateFlags == flags)
		return;
	if (((g_d3dStateFlags ^ flags) & 0x8000) != 0) {
		D3DINSTRUCTION* instruction = (D3DINSTRUCTION*)g_d3dWritePtr;
		instruction->bOpcode = D3DOP_STATERENDER;
		instruction->bSize = sizeof(D3DSTATE);
		instruction->wCount = 1;
		g_d3dWritePtr += sizeof *instruction;
		D3DSTATE* state = (D3DSTATE*)g_d3dWritePtr;
		state->dwState = D3DRENDERSTATE_MONOENABLE;
		state->dwArg = (flags & 0x8000) == 0;
		g_d3dWritePtr += sizeof *state;
	}
	if (((g_d3dStateFlags ^ flags) & 0x600) != 0) {
		D3DINSTRUCTION* instruction = (D3DINSTRUCTION*)g_d3dWritePtr;
		instruction->bOpcode = D3DOP_STATERENDER;
		instruction->bSize = sizeof(D3DSTATE);
		instruction->wCount = 4;
		g_d3dWritePtr += sizeof *instruction;
		D3DSTATE* states = (D3DSTATE*)g_d3dWritePtr;
		if (flags & 0x600) {
			states[0] = (D3DSTATE) { D3DRENDERSTATE_SRCBLEND, 5 };
			states[1] = (D3DSTATE) { D3DRENDERSTATE_DESTBLEND, 6 };
			states[2] = (D3DSTATE) { D3DRENDERSTATE_TEXTUREMAPBLEND, (flags & 0x400) ? 4u : 2u };
			states[3] = (D3DSTATE) { D3DRENDERSTATE_BLENDENABLE, 1 };
		} else {
			states[0] = (D3DSTATE) { D3DRENDERSTATE_SRCBLEND, 2 };
			states[1] = (D3DSTATE) { D3DRENDERSTATE_DESTBLEND, 1 };
			states[2] = (D3DSTATE) { D3DRENDERSTATE_TEXTUREMAPBLEND, (flags & 0x400) ? 4u : 2u };
			states[3] = (D3DSTATE) { D3DRENDERSTATE_BLENDENABLE, 0 };
		}
		g_d3dWritePtr += 4 * sizeof *states;
	}
	if (((g_d3dStateFlags ^ flags) & 0x1800) != 0) {
		D3DINSTRUCTION* instruction = (D3DINSTRUCTION*)g_d3dWritePtr;
		instruction->bOpcode = D3DOP_STATERENDER;
		instruction->bSize = sizeof(D3DSTATE);
		instruction->wCount = 2;
		g_d3dWritePtr += sizeof *instruction;
		D3DSTATE* states = (D3DSTATE*)g_d3dWritePtr;
		states[0] = (D3DSTATE) {
			D3DRENDERSTATE_ZFUNC,
			(flags & 0x800) ? (unsigned char)std3D_MapZCmpFunc(g_std3DZBufferBitDepth) : 8u,
		};
		states[1] = (D3DSTATE) { D3DRENDERSTATE_ZWRITEENABLE, (flags & 0x1000) != 0 };
		g_d3dWritePtr += 2 * sizeof *states;
	}
	if (((g_d3dStateFlags ^ flags) & 0x180) != 0) {
		D3DINSTRUCTION* instruction = (D3DINSTRUCTION*)g_d3dWritePtr;
		instruction->bOpcode = D3DOP_STATERENDER;
		instruction->bSize = sizeof(D3DSTATE);
		instruction->wCount = 2;
		g_d3dWritePtr += sizeof *instruction;
		D3DSTATE* states = (D3DSTATE*)g_d3dWritePtr;
		states[0] = (D3DSTATE) { D3DRENDERSTATE_TEXTUREMAG, ((flags & 0x80) != 0) + 1 };
		states[1] = (D3DSTATE) { D3DRENDERSTATE_TEXTUREMIN, ((flags & 0x100) != 0) + 1 };
		g_d3dWritePtr += 2 * sizeof *states;
	}
	if (((g_d3dStateFlags ^ flags) & 0x40) != 0) {
		if (flags & 0x40) {
			D3DINSTRUCTION* instruction = (D3DINSTRUCTION*)g_d3dWritePtr;
			instruction->bOpcode = D3DOP_STATERENDER;
			instruction->bSize = sizeof(D3DSTATE);
			instruction->wCount = 5;
			g_d3dWritePtr += sizeof *instruction;
			D3DSTATE* states = (D3DSTATE*)g_d3dWritePtr;
			states[0] = (D3DSTATE) { D3DRENDERSTATE_FOGENABLE, 1 };
			states[1] = (D3DSTATE) {
				D3DRENDERSTATE_FOGCOLOR,
				(uint32_t)g_std3DFogColorBlue8 | ((uint32_t)g_std3DFogColorGreen8 << 8) |
					((uint32_t)g_std3DFogColorRed8 << 16),
			};
			states[2] = (D3DSTATE) { D3DRENDERSTATE_FOGTABLEMODE, 3 };
			states[3] = (D3DSTATE) { D3DRENDERSTATE_FOGTABLESTART, (uint32_t)g_std3DFogTableStartBits };
			states[4] = (D3DSTATE) { D3DRENDERSTATE_FOGTABLEEND, (uint32_t)g_std3DFogTableEndBits };
			g_d3dWritePtr += 5 * sizeof *states;
		} else {
			D3DINSTRUCTION* instruction = (D3DINSTRUCTION*)g_d3dWritePtr;
			instruction->bOpcode = D3DOP_STATERENDER;
			instruction->bSize = sizeof(D3DSTATE);
			instruction->wCount = 1;
			g_d3dWritePtr += sizeof *instruction;
			D3DSTATE* state = (D3DSTATE*)g_d3dWritePtr;
			state->dwState = D3DRENDERSTATE_FOGENABLE;
			state->dwArg = 0;
			g_d3dWritePtr += sizeof *state;
		}
	}
	g_d3dStateFlags = flags;
}

// FUNCTION: TIE98 0x4C8260
char std3D_ClearZBuffer(void) {
	D3DRECT rect = {
		.x1 = g_std3DQuadRect.x,
		.y1 = g_std3DQuadRect.y,
		.x2 = g_std3DQuadRect.x + g_std3DQuadRect.width,
		.y2 = g_std3DQuadRect.y + g_std3DQuadRect.height,
	};
	for (;;) {
		HRESULT result = g_d3dViewport->lpVtbl->Clear2(g_d3dViewport, 1, &rect, 2, 0, 0.0f, 0);
		if (result == 0)
			return 1;
		if (result == DX_DDERR_SURFACELOST)
			result = g_std3DZBufferSurface->lpVtbl->Restore(g_std3DZBufferSurface);
		if (result != 0)
			return 0;
	}
}

// FUNCTION: TIE98 0x42B300 Renderer_ClearCockpitCrtZBuffer
void Renderer_ClearCockpitCrtZBuffer(void) {
	uint8_t negative_run_fill;
	uint8_t positive_run_fill;
	DDSURFACEDESC descriptor;
	HRESULT result;

	if (g_std3DZBufferBitDepth == 16) {
		negative_run_fill = 0xff;
		positive_run_fill = 0;
	} else {
		negative_run_fill = 0;
		positive_run_fill = 0xff;
	}

	memset(&descriptor, 0, sizeof descriptor);
	descriptor.dwSize = sizeof descriptor;
	do {
		result = g_std3DZBufferSurface->lpVtbl->Lock(g_std3DZBufferSurface, NULL, &descriptor, 0, NULL);
		if (result != 0 && result != DX_DDERR_WASSTILLDRAWING)
			return;
	} while (result != 0);

	void* locked_surface = descriptor.lpSurface;
	memset(&descriptor, 0, sizeof descriptor);
	descriptor.dwSize = sizeof descriptor;
	g_std3DZBufferSurface->lpVtbl->GetSurfaceDesc(g_std3DZBufferSurface, &descriptor);

	int depth_x = displaycorner_columns + ((int)descriptor.dwWidth - (int)g_pStd3DRenderTarget->width) / 2;
	int depth_y = displaycorner_lines + ((int)descriptor.dwHeight - (int)g_pStd3DRenderTarget->height) / 2;
	uint8_t* row = (uint8_t*)locked_surface + 2 * depth_x + descriptor.lPitch * depth_y;
	const uint8_t* mask = (const uint8_t*)xtransdataptr + (uint16_t)maskbufptr;
	for (uint16_t y = 0; y < pixelsdeep; ++y) {
		uint8_t* destination = row;
		int8_t run_type = (int8_t)*mask++;
		uint32_t drawn = 0;
		if (pixelswide != 0) {
			do {
				uint32_t run = *mask++;
				if (run == 0) {
					run = *mask++;
					if (run == 0)
						run = *mask++ + 256;
					run += 255;
				}
				memset(destination, run_type < 0 ? negative_run_fill : positive_run_fill, 2 * run);
				destination += 2 * run;
				drawn += run;
				run_type = (int8_t)-run_type;
			} while (drawn < pixelswide);
		}
		row += descriptor.lPitch;
	}
	g_std3DZBufferSurface->lpVtbl->Unlock(g_std3DZBufferSurface, locked_surface);
	/* PORT: publish the precise write made through the CPU DirectDraw attachment
	 * to the Aeron depth target before the PiP scene is submitted. */
	AeronDx5_CommitDepthSurfaceRect(g_std3DZBufferSurface,
									&(AeronDx5Rect) { depth_x, depth_y, pixelswide, pixelsdeep });
}
