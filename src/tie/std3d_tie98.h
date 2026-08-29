#ifndef TIE_STD3D_TIE98_H
#define TIE_STD3D_TIE98_H

#include "aeron/compat/d3d.h"

#include <stdint.h>

typedef enum Std3DRenderStateFlags {
	STD3D_RS_FOG_ENABLE = 0x000040,
	STD3D_RS_TEXTURE_MAG_LINEAR = 0x000080,
	STD3D_RS_TEXTURE_MIN_LINEAR = 0x000100,
	STD3D_RS_ALPHA_BLEND = 0x000200,
	STD3D_RS_TEXTURE_MODULATE_ALPHA = 0x000400,
	STD3D_RS_Z_COMPARE_ENABLE = 0x000800,
	STD3D_RS_Z_WRITE_ENABLE = 0x001000,
	STD3D_RS_TEXTURE_ADDRESS_CLAMP = 0x002000,
	STD3D_RS_MONO_DISABLE = 0x008000,
} Std3DRenderStateFlags;

typedef enum StdColorMode {
	STDCOLOR_PAL = 0,
	STDCOLOR_RGB = 1,
	STDCOLOR_RGBA = 2,
} StdColorMode;

typedef struct ColorInfo {
	StdColorMode colorMode;
	int bpp;
	int redBPP;
	int greenBPP;
	int blueBPP;
	int redPosShift;
	int greenPosShift;
	int bluePosShift;
	int redPosShiftRight;
	int greenPosShiftRight;
	int bluePosShiftRight;
	int alphaBPP;
	int alphaPosShift;
	int alphaPosShiftRight;
} ColorInfo;

typedef struct Std3DRenderTargetDesc {
	uint32_t width;
	uint32_t height;
	uint32_t sizeBytes;
	int32_t pitch;
	uint32_t widthPixels;
	ColorInfo colorInfo;
} Std3DRenderTargetDesc;

typedef struct Std3DViewportRect {
	int x;
	int y;
	int width;
	int height;
} Std3DViewportRect;

typedef struct Std3DDeviceCaps {
	int bHardware;
	int bTexturePerspective;
	int bHasZBuffer;
	int bColorKeyTexture;
	int bAlphaTexture;
	int bStippledShade;
	int bAlphaBlend;
	int bSquareOnlyTexture;
	int bClampSupported;
	uint32_t colorModelFlags;
	uint32_t renderBitDepthMask;
	uint32_t zCmpCapsMask;
	uint32_t minTextureWidth;
	uint32_t minTextureHeight;
	uint32_t maxTextureWidth;
	uint32_t maxTextureHeight;
	uint32_t maxBufferSize;
	uint32_t maxVertexCount;
} Std3DDeviceCaps;

typedef struct Tie98D3DDeviceDesc {
	uint32_t dwSize;
	uint32_t dwFlags;
	uint32_t dcmColorModel;
	uint32_t dwDevCaps;
	uint8_t dtcTransformCaps[8];
	int32_t bClipping;
	uint8_t dlcLightingCaps[16];
	D3DPRIMCAPS dpcLineCaps;
	D3DPRIMCAPS dpcTriCaps;
	uint32_t dwDeviceRenderBitDepth;
	uint32_t dwDeviceZBufferBitDepth;
	uint32_t dwMaxBufferSize;
	uint32_t dwMaxVertexCount;
	uint32_t dwMinTextureWidth;
	uint32_t dwMinTextureHeight;
	uint32_t dwMaxTextureWidth;
	uint32_t dwMaxTextureHeight;
	uint32_t dwMinStippleWidth;
	uint32_t dwMaxStippleWidth;
	uint32_t dwMinStippleHeight;
	uint32_t dwMaxStippleHeight;
} Tie98D3DDeviceDesc;

typedef struct Std3DDevice {
	Std3DDeviceCaps caps;
	char deviceName[128];
	char deviceDescription[128];
	uint32_t totalMemory;
	uint32_t availableMemory;
	Tie98D3DDeviceDesc d3dDesc;
	DxGuid guid;
} Std3DDevice;

typedef struct Std3DTexFmt {
	ColorInfo colorInfo;
	DDSURFACEDESC ddsd;
} Std3DTexFmt;

typedef struct Std3DRasterInfo {
	uint32_t width;
	uint32_t height;
	uint32_t tileFactor;
	uint32_t rowPitch;
	uint32_t unk10;
	uint32_t sourceType;
	uint32_t bitsPerPixel;
	uint32_t unk1c;
	uint32_t unk20;
	uint32_t unk24;
	uint32_t unk28;
	uint32_t unk2c;
	uint32_t unk30;
	uint32_t unk34;
	uint32_t unk38;
	uint32_t unk3c;
	uint32_t unk40;
	uint32_t unk44;
	uint32_t unk48;
} Std3DRasterInfo;

typedef struct Std3DVBuffer {
	int storageType;
	int lockCount;
	int unk08;
	Std3DRasterInfo raster;
	int unk58;
	void* pixels;
	uint32_t transparentColor;
	IDirectDrawSurface* ddSurface;
	uint8_t reserved68[112];
} Std3DVBuffer;

typedef struct Std3DTextureSurface {
	IDirect3DTexture* pCachedTexture;
	IDirectDrawSurface* pCachedSurface;
	DDSURFACEDESC ddsd;
	D3DTEXTUREHANDLE texHandle;
	uint8_t bCached;
	uint8_t reserved79[3];
	uint32_t usesAlphaFormat;
	uint32_t width;
	uint32_t height;
	uint32_t byteSize;
	uint32_t cacheFrameTag;
	struct Std3DTextureSurface* pPrev;
	struct Std3DTextureSurface* pNext;
} Std3DTextureSurface;

typedef struct Std3DRenderTri {
	int v0;
	int v1;
	int v2;
	Std3DRenderStateFlags flags;
	Std3DTextureSurface* texture;
} Std3DRenderTri;

AERON_DX_ASSERT(tie98_color_info_size, sizeof(ColorInfo) == 56);
AERON_DX_ASSERT(tie98_device_caps_size, sizeof(Std3DDeviceCaps) == 72);
AERON_DX_ASSERT(tie98_d3d_device_desc_size, sizeof(Tie98D3DDeviceDesc) == 204);
AERON_DX_ASSERT(tie98_std3d_device_size, sizeof(Std3DDevice) == 556);
AERON_DX_ASSERT(tie98_raster_info_size, sizeof(Std3DRasterInfo) == 76);
AERON_DX_ASSERT32(tie98_vbuffer_size, sizeof(Std3DVBuffer) == 216);
AERON_DX_ASSERT32(tie98_texture_surface_size, sizeof(Std3DTextureSurface) == 152);

extern IDirectDraw* g_std3DDirectDraw;
extern IDirectDrawSurface* g_std3DRenderSurface;
extern IDirect3DDevice* g_d3dDevice;
extern IDirect3DViewport* g_d3dViewport;
extern Std3DViewportRect g_std3DQuadRect;
extern uint32_t g_std3DCapFlags;
extern uint32_t g_std3DExecBufMaxVerts;
extern Std3DDevice* g_pStd3DCurDevice;
extern Std3DTexFmt* g_pFmtRGB565;
extern int g_std3DZBufferBitDepth;
extern Std3DDevice g_std3DDevices[4];
extern IDirectDrawSurface* g_rendererAttachedZBufferSurface;

void std3D_BuildRenderTargetDesc(unsigned int width, unsigned int height, int pitch);
void std3D_SetRenderSurface(IDirectDrawSurface* render_surface);
void std3D_ResetUnusedStartupState(int value0, int value1, int value2, int flag);
int std3D_Startup(void);
void std3D_Shutdown(void);
unsigned int std3D_SelectBestDevice(const Std3DDeviceCaps* required_caps);
int std3D_CreateDevice(unsigned int device_index, int use_z_buffer);
void std3D_DestroyDevice(void);
char std3D_StartScene(void);
char std3D_EndScene(void);
char std3D_LockExecuteBuffer(void);
char std3D_AddVertices(D3DTLVERTEX* vertices, int count);
char std3D_BeginInstructions(void);
char std3D_AddTriangles(Std3DRenderTri* triangles, unsigned int count);
char std3D_ExecuteBuffer(void);
void std3D_SetRenderState(Std3DRenderStateFlags flags);
char std3D_ClearZBuffer(void);
void Renderer_ClearCockpitCrtZBuffer(void);
int std3D_CopyPaletteToScratch16(const uint16_t* palette, int color_count);
uint16_t* std3D_ConvertTexTo1555(uint16_t* source, int count);
uint16_t* std3D_ConvertTexTo4444(uint16_t* source, int count);
Std3DVBuffer* std3D_AllocVBuffer(Std3DRasterInfo* raster_info);
void std3D_FreeVBuffer(Std3DVBuffer* vbuffer);
void std3D_LockVBuffer(Std3DVBuffer* vbuffer);
void std3D_UnlockVBuffer(Std3DVBuffer* vbuffer);
void std3D_BlitVBuffer(Std3DVBuffer* destination, Std3DVBuffer* source, int destination_x, int destination_y);
void std3D_FillVBuffer(Std3DVBuffer* vbuffer, int color);
uint32_t std3D_GetCapabilityFlags(void);
int std3D_SetFogColor8(uint32_t red, uint32_t green, uint32_t blue);
int std3D_SetFogTableRangeBits(uint32_t start_bits, uint32_t end_bits);
int std3D_SetTextureSizeCaps(int min_width, int min_height, int max_width, int max_height);
char std3D_SetPaletteConversionSource(const void* palette_rgb888, uint8_t alpha);
void std3D_ClampTextureDimensions(uint32_t source_width, uint32_t source_height, uint32_t* output_width,
								  uint32_t* output_height);
char std3D_CreateMipSurface(Std3DVBuffer* source, Std3DTextureSurface* surface, int texture_format_mode,
							int alpha_mask);
void std3D_FlushTextureCache(void);
void std3D_CacheTextureSurface(Std3DTextureSurface* surface);

#endif
