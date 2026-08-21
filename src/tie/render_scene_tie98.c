#include "tie/render_scene_tie98.h"

#include "anim.h"
#include "tie/drawpol.h"
#include "tie/flight_surface_tie98.h"
#include "tie/logbuf2.h"
#include "tie/model_texture_tie98.h"
#include "tie/modelmesh.h"
#include "tie/render_texture_tie98.h"
#include "tie/rtsvga2.h"
#include "tie/shell.h"
#include "tie/species.h"
#include "tie/tie.h"
#include "tie/transfm2.h"
#include "tie/trig2.h"
#include "tie/xtrans2.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define TIE98_SCENE_FACE_MAX 5000
#define TIE98_MESH_QUEUE_MAX 500
#define TIE98_SCENE_SPAN_MAX 20000
#define TIE98_SCENE_PHONG_DATA_BYTES 0x1806C
#define TIE98_SCENE_SPAN_BYTES 24
#define TIE98_EXEC_VERTEX_BUDGET_BYTES 64
#define TIE98_EXEC_TRIANGLE_BUDGET_BYTES 24
#define TIE98_OPT_INDEXED_SHADE_TABLE_BYTES 4096

enum {
	TIE98_HARDWARE_VERTEX_CAPACITY = 7500,
	TIE98_HARDWARE_TRIANGLE_CAPACITY = 12000,
};

// GLOBAL: TIE98 0x5FD2EA
static SceneFaceTIE98 g_visFaceList[TIE98_SCENE_FACE_MAX];
// GLOBAL: TIE98 0x5FD338
static SceneMeshTIE98 g_meshQueue[TIE98_MESH_QUEUE_MAX];
// GLOBAL: TIE98 0x5FD2FC
static ProjVertexTIE98* g_projVertList;
// GLOBAL: TIE98 0x5FD318
static int* g_vertexRemap;
/* PORT: records the hardware vertex emitted for each projected vertex. The
 * original 32-bit renderer stores this transient mapping in its scene buffers. */
static int* g_emittedVertexByProjection;
// GLOBAL: TIE98 0x5FD306
static int g_projVertCapacity;
// GLOBAL: TIE98 0x5FD31E
static int g_modelVertexCapacity;
// GLOBAL: TIE98 0x5FD314
static int g_sceneEdgeCapacity;
// GLOBAL: TIE98 0x5FD2F0
static int g_visFaceCount;
// GLOBAL: TIE98 0x5FD2F4
static int g_visFaceDrawStartIndex;
// GLOBAL: TIE98 0x5FD342
static int g_meshQueueIndex;
// GLOBAL: TIE98 0x5FD302
static int g_projVertCount;
// GLOBAL: TIE98 0x5833F0
static int g_curLayerId;
// GLOBAL: TIE98 0x5FD350
static Vec3f g_meshEyePos;
// GLOBAL: TIE98 0x5FD310
static int g_sceneEdgeCursor;
// GLOBAL: TIE98 0x5FD30A
static SceneEdgeTIE98* g_sceneEdgeList;
// GLOBAL: TIE98 0x5FD322
static int* g_sceneEdgeFlags;
// GLOBAL: TIE98 0x58032C
static ProjVertexTIE98* g_sw3dGeneratedClipVertex;
// GLOBAL: TIE98 0x580338
static ProjVertexTIE98* g_sw3dClipTop;
// GLOBAL: TIE98 0x580348
static ProjVertexTIE98* g_sw3dClipBottom;
// GLOBAL: TIE98 0x5FD2C0
static SceneSpanTIE98 g_sceneSpanData[TIE98_SCENE_SPAN_MAX];
// GLOBAL: TIE98 0x5FD2D2
static SceneSpanTIE98* g_sceneSpanPtrList[TIE98_SCENE_SPAN_MAX];
// GLOBAL: TIE98 0x5FD332
static SceneSpanTIE98* g_scanlineSpanHeads[480];
// GLOBAL: TIE98 0x5FD2CA
static SceneSpanTIE98* g_pSceneSpanDataCur;
// GLOBAL: TIE98 0x5FD2CE
static SceneSpanTIE98* g_pSceneSpanDataEnd;
// GLOBAL: TIE98 0x5FD2DC
static int g_sceneSpanPtrAvail;
// GLOBAL: TIE98 0x584B38
static SceneFaceTIE98 g_sw3dCockpitMaskSentinelFace;

// GLOBAL: TIE98 0x5702D0
static D3DTLVERTEX* g_flightVertexBuffer;
// GLOBAL: TIE98 0x5802E8
static Std3DRenderTri* g_triBuffer;
// GLOBAL: TIE98 0x5802F4
static int g_d3dVertexCount;
// GLOBAL: TIE98 0x580300
static int g_d3dIndexCount;
// GLOBAL: TIE98 0x580318
static int g_d3dVertexAlphaStateResetSlot;
// GLOBAL: TIE98 0x5802F0
static int g_maxBatchVerts;
// GLOBAL: TIE98 0x5601C8
static int g_maxBatchTris;
// GLOBAL: TIE98 0x4E39C4
static int g_capVertexAlpha;
// GLOBAL: TIE98 0x580304
static float g_flightVpOriginX;
// GLOBAL: TIE98 0x5702D4
static float g_flightVpOriginY;
// GLOBAL: TIE98 0x4F2A94
static float g_mipLodScale = 1.0f;
// GLOBAL: TIE98 0x4F2A90
static int g_sw3dMipmapEnabled = 1;
// GLOBAL: TIE98 0x4F3D68
int g_bilinearEnabled = 1;
// GLOBAL: TIE98 0x5833E8
static int g_bBackdropMeshMode;
// GLOBAL: TIE98 0x58ABA0
int g_useHardware3D;
// GLOBAL: TIE98 0x58A278
int g_flightSurfaceAlreadyLocked;
// GLOBAL: TIE98 0x4F2A9C
static int g_directionalLightingEnabled = 1;
// GLOBAL: TIE98 0x4F2AA0
static int g_specularLightingEnabled = 1;
// GLOBAL: TIE98 0x58A270
static int g_forcedLodLevel;
// GLOBAL: TIE98 0x4F2A8C
static float g_lodDistanceScale = 1.0f;

// GLOBAL: TIE98 0x58A274
static int g_nodeSwitchIndex;

/* PORT: replaces TIE98's temporary rewrite of a locked OPT header. The
 * native OPT cache owns parsed host structures rather than writable file
 * images, so DRAW_drawhyperstar selects its executable-defined quad here. */
const Tie98OptimizedPolyObject* g_flightModelOverride;

// GLOBAL: TIE98 0x5833F8
static int g_modelSelfOcclusionEnabled;

// FUNCTION: TIE98 0x434BC0
void FlightModel_ToggleSelfOcclusion(void) { g_modelSelfOcclusionEnabled = !g_modelSelfOcclusionEnabled; }

// FUNCTION: TIE98 0x434BE0
int FlightModel_GetSelfOcclusion(void) { return g_modelSelfOcclusionEnabled; }

// GLOBAL: TIE98 0x58030C
int g_powerVrSceneWorkaround;
// GLOBAL: TIE98 0x4E39C8
static int g_std3DStartScenePending = 1;
// GLOBAL: TIE98 0x4E4398
static int g_bwingBridgeMeshIndex = -1;
// GLOBAL: TIE98 0x5FD2E6
static int g_phongSlotIndex;
// GLOBAL: TIE98 0x5FD2A4
static int g_phongSlotStride;
// GLOBAL: TIE98 0x584B34
static int g_sw3dLightSampleCacheSceneStampBase;
// GLOBAL: TIE98 0x5FD2E0
static uint8_t g_scenePhongData[TIE98_SCENE_PHONG_DATA_BYTES];
// GLOBAL: TIE98 0x5845D4
static int g_sw3dLightSampleBlockSize = 16;
// GLOBAL: TIE98 0x584BD4
static int g_sw3dLightSampleBlockShift = 4;
// GLOBAL: TIE98 0x5845CC
static int g_sw3dLightSampleBlockMask = 15;
// GLOBAL: TIE98 0x58A27C
static int g_sw3dSkipOddScanlines;
// GLOBAL: TIE98 0x5845C8
static SceneFaceTIE98* g_sw3dCurrentFace;
// GLOBAL: TIE98 0x58461C
static int g_sw3dCurrentScanY;
// GLOBAL: TIE98 0x5845DC
static int g_sw3dScanlineByteOffset;
// GLOBAL: TIE98 0x5845E4
static float g_sw3dLightSampleSubrowLerpT;
// GLOBAL: TIE98 0x5845F4
static float g_sw3dLightSampleRowsToNextBlockFloat;
// GLOBAL: TIE98 0x5845F8
static float g_sw3dLightSampleSubrowFloat;
// GLOBAL: TIE98 0x584628
static int g_sw3dCurrentLightSampleCacheStamp;
// GLOBAL: TIE98 0x584BB0
static SceneMeshTIE98* g_sw3dSpanSceneMesh;
// GLOBAL: TIE98 0x584BB4
static float g_sw3dSpanTextureWidthFloat;
// GLOBAL: TIE98 0x584BB8
static float g_sw3dSpanTextureHeightFloat;
// GLOBAL: TIE98 0x584BBC
static int g_sw3dSpanTextureWidthShift;
// GLOBAL: TIE98 0x584BC0
static int g_sw3dSpanTextureHeightShift;
// GLOBAL: TIE98 0x584BC4
static uint8_t* g_sw3dSpanShadeTable;
// GLOBAL: TIE98 0x584BC8
static uint8_t* g_sw3dSpanTexels;
// GLOBAL: TIE98 0x584BCC
static int g_sw3dSpanTexelMask;
// GLOBAL: TIE98 0x5845D0
static int g_sw3dSpanShadeDitherAccum;
// GLOBAL: TIE98 0x584624
static int g_sw3dSpanStartX;
// GLOBAL: TIE98 0x584620
static int g_sw3dSpanLength;
// GLOBAL: TIE98 0x584BDC
static int g_sw3dSpanStepUQ8;
// GLOBAL: TIE98 0x584616
static int g_sw3dSpanStepVQ8;
// GLOBAL: TIE98 0x584BD8
static int g_sw3dSpanShadeQ8;
// GLOBAL: TIE98 0x584610
static int g_sw3dSpanShadeStepQ8;
// GLOBAL: TIE98 0x5845F0
static int g_sw3dSpanUQ8;
// GLOBAL: TIE98 0x584602
static int g_sw3dSpanVQ8;
// GLOBAL: TIE98 0x584630
static uint8_t g_panelBoxSpanScratch[1280];

typedef struct SoftwareLightSampleTIE98 {
	int stamp;
	float intensity;
	float rowDelta;
} SoftwareLightSampleTIE98;

// GLOBAL: TIE98 0x560150
static FlightObject* g_swFaceLightCachedObject;
// GLOBAL: TIE98 0x560158
static SceneFaceTIE98* g_swFaceLightCachedFace;
// GLOBAL: TIE98 0x560140
static Vec3f g_swFaceLightCachedNormal;
// GLOBAL: TIE98 0x5600E0
static Vec3f g_swFaceLightPositions[8];
// GLOBAL: TIE98 0x5600B0
static float g_swFaceLightIntensities[8];
// GLOBAL: TIE98 0x560154
static int g_swFaceLightCount;
// GLOBAL: TIE98 0x5600D0
static Vec3f g_swFaceDirectionalLight;

// GLOBAL: TIE98 0x4E43B0
static const int g_sw3dTextureShiftBySizeDiv16[60] = {
	3, 4, 5, 5, 6, 6, 6, 6, 7, 7, 7, 7, 7, 7, 7, 7, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
	8, 8, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9,
};

// GLOBAL: TIE98 0x580308
int g_drawSceneEffects;

// GLOBAL: TIE98 0x580328
static intptr_t g_modelNodeWalkUnusedScratch0;
// GLOBAL: TIE98 0x580350
static intptr_t g_modelNodeWalkUnusedScratch1;
// GLOBAL: TIE98 0x580370
static intptr_t g_curVertNormals;
// GLOBAL: TIE98 0x580358
static intptr_t g_modelNodeWalkUnusedScratch2;
// GLOBAL: TIE98 0x580340
static intptr_t g_curMeshFlags;
// GLOBAL: TIE98 0x58036C
static int g_curVertexCount;
// GLOBAL: TIE98 0x580354
static OptTextureDataTIE98* g_curTextureDesc;

/* PORT: fallback material for host OPT nodes without a texture node. */
static OptTextureDataTIE98 g_defaultMaterial = {
	.paletteAddress = 256,
	.paletteType = 16,
	.width = 8,
	.height = 8,
};
/* PORT: host-owned storage backing the fallback material. */
static uint8_t g_defaultTextureData[64 + 4096 + 8192];
/* PORT: initialization state for the host fallback material. */
static int g_defaultTextureInitialized;
/* PORT: source colors used to build the host fallback shade table. */
static uint8_t g_defaultTextureRgb24[8 * 8 * 3];

static int FlightModel_IsLightSegmentBlocked(FlightObject* object, const Vec3f* segment_start,
											 const Vec3f* segment_end);

/* RECOVERY HELPER: removes the texture-data binding duplicated by the
 * face-data and texture-node cases in FlightModel_Draw_OPT_Node. */
static void FlightModel_BindTextureData(const Tie98OptimizedPolyObject* model, SceneMeshTIE98* mesh,
										OptTextureDataTIE98* material) {
	mesh->pMaterial = material;
	if (material == &g_defaultMaterial) {
		mesh->pTexels = g_defaultTextureData;
		mesh->pPalette0 = g_defaultTextureData + 64;
		mesh->pPalette1 = g_defaultTextureData + 64 + 4096;
		return;
	}

	mesh->pTexels = (uint8_t*)material + sizeof *material;
	uint8_t* shade_table;
	if (material->paletteType == 0) {
		shade_table = (uint8_t*)TieNativeOpt_ResolveAddress(model, material->paletteAddress, 1);
	} else {
		shade_table = mesh->pTexels;
		const int base_size = material->width * material->height;
		if (base_size == material->textureSize)
			shade_table += material->dataSize;
		else
			shade_table += base_size;
	}
	if (g_useHardware3D) {
		/* pPalette1 carries the runtime-built RGB565 shade tables (analyzed
		 * level 0, level 8 base palette at +4096, overlay metadata) the
		 * hardware draw path consumes; pPalette0 keeps the serialized shade
		 * table and is only read by the software rasterizer. */
		mesh->pPalette0 = shade_table;
		mesh->pPalette1 = (uint8_t*)RenderTexture_GetHardwareShadeTables(
			(const uint16_t*)(shade_table + TIE98_OPT_INDEXED_SHADE_TABLE_BYTES));
	} else if (g_flight16bppBytesPerPixel == 1) {
		mesh->pPalette0 = (uint8_t*)RenderTexture_GetSoftwareShadeTable(
			(const uint16_t*)(shade_table + TIE98_OPT_INDEXED_SHADE_TABLE_BYTES));
		mesh->pPalette1 = mesh->pPalette0 + 4096;
	} else {
		/* The 16-bit software span reads the serialized RGB565 shades at
		 * pPalette0 + 4096, matching OptModel_BuildRuntimeHandle. */
		mesh->pPalette0 = shade_table;
		mesh->pPalette1 = shade_table + TIE98_OPT_INDEXED_SHADE_TABLE_BYTES;
	}
}

// FUNCTION: TIE98 0x427990
static void Math_SetFpuSinglePrecisionMode(void) {
	// PORT: the original changes x87 precision; supported hosts do not execute x87 arithmetic.
}

// FUNCTION: TIE98 0x4279C0
static void Math_SetFpuExtendedPrecisionMode(void) {
	// PORT: the original changes x87 precision; supported hosts do not execute x87 arithmetic.
}

// FUNCTION: TIE98 0x42AF00
static void RenderScene_InitHardwareFrame(void) {
	if (g_powerVrSceneWorkaround && g_std3DStartScenePending) {
		Math_SetFpuExtendedPrecisionMode();
		std3D_StartScene();
		Math_SetFpuSinglePrecisionMode();
		g_std3DStartScenePending = 0;
	}
	g_d3dIndexCount = 0;
	g_d3dVertexCount = 0;
	g_d3dVertexAlphaStateResetSlot = 0;
	g_capVertexAlpha = 1;
	/* PORT: the emulated render surface and display have the same logical
	 * dimensions, so TIE98's display/surface centering term is zero. */
	g_flightVpOriginX = (float)displaycorner_columns;
	g_flightVpOriginY = (float)displaycorner_lines;
	/* PORT: dynamic host storage replaces the original shared scene-span allocation. */
	int max_vertices = TIE98_SCENE_SPAN_BYTES * TIE98_SCENE_SPAN_MAX / 128;
	int max_triangles = TIE98_SCENE_SPAN_BYTES * TIE98_SCENE_SPAN_MAX / 80;
	if (max_vertices > (int)g_pStd3DCurDevice->caps.maxVertexCount)
		max_vertices = (int)g_pStd3DCurDevice->caps.maxVertexCount;
	if (max_vertices > 256)
		max_vertices = 256;
	if (max_triangles > 256)
		max_triangles = 256;
	const uint32_t remaining =
		g_pStd3DCurDevice->caps.maxBufferSize - (uint32_t)(max_vertices * TIE98_EXEC_VERTEX_BUDGET_BYTES);
	if (max_triangles > (int)(remaining / TIE98_EXEC_TRIANGLE_BUDGET_BYTES))
		max_triangles = (int)(remaining / TIE98_EXEC_TRIANGLE_BUDGET_BYTES);
	g_maxBatchVerts = max_vertices;
	g_maxBatchTris = max_triangles;
	/* Original TIE98 uses the two halves of its scene-span arena as
	 * oversized staging buffers; the batch limits are flush thresholds. */
	if (!g_flightVertexBuffer)
		g_flightVertexBuffer =
			malloc((size_t)TIE98_HARDWARE_VERTEX_CAPACITY * sizeof *g_flightVertexBuffer);
	if (!g_triBuffer)
		g_triBuffer = malloc((size_t)TIE98_HARDWARE_TRIANGLE_CAPACITY * sizeof *g_triBuffer);
	if (!g_flightVertexBuffer || !g_triBuffer) {
		free(g_flightVertexBuffer);
		free(g_triBuffer);
		g_flightVertexBuffer = NULL;
		g_triBuffer = NULL;
		shell_programexit("Unable to allocate TIE98 hardware staging buffers");
	}
}

// FUNCTION: TIE98 0x43D8A0
void RenderScene_Initialize_tie98(int reset_flag) {
	g_sw3dCockpitMaskSentinelFace.maxVertW = 1.0e32f;
	g_sw3dCockpitMaskSentinelFace.minVertW = 1.0e32f;
	g_sw3dCockpitMaskSentinelFace.gradients[6] = 0.0f;
	g_sw3dCockpitMaskSentinelFace.gradients[7] = 0.0f;
	g_sw3dCockpitMaskSentinelFace.gradients[8] = 1.0e32f;
	if (reset_flag) {
		g_sceneSpanPtrAvail = TIE98_SCENE_SPAN_MAX;
		g_pSceneSpanDataCur = g_sceneSpanData;
		g_pSceneSpanDataEnd = &g_sceneSpanData[TIE98_SCENE_SPAN_MAX - 1];
		g_visFaceDrawStartIndex = 0;
		g_visFaceCount = 0;
		g_meshQueueIndex = 0;
		g_phongSlotIndex = 0;

		const int viewport_width = (uint16_t)pixelswide;
		const int viewport_height = (uint16_t)pixelsdeep;
		const uint8_t* mask = (const uint8_t*)xtransdataptr + (uint16_t)maskbufptr;
		for (int scan_y = 0; scan_y < viewport_height; ++scan_y) {
			SceneSpanTIE98* previous = NULL;
			g_scanlineSpanHeads[scan_y] = NULL;
			int run_visible = (int8_t)*mask++;
			int scan_x = 0;
			while (scan_x < viewport_width) {
				int run_length = *mask++;
				if (run_length == 0) {
					run_length = *mask++;
					if (run_length == 0)
						run_length = 256 + *mask++;
					run_length += 255;
				}
				if (run_visible < 0) {
					if (previous)
						previous->next = g_pSceneSpanDataCur;
					else
						g_scanlineSpanHeads[scan_y] = g_pSceneSpanDataCur;
					previous = g_pSceneSpanDataCur++;
					previous->startX = scan_x;
					previous->endX = scan_x + run_length;
					previous->pFace = &g_sw3dCockpitMaskSentinelFace;
					previous->next = NULL;
				}
				scan_x += run_length;
				run_visible = -run_visible;
			}
		}
	} else {
		g_visFaceDrawStartIndex = g_visFaceCount;
	}
	g_phongSlotStride = (g_sw3dLightSampleBlockSize + (uint16_t)pixelswide - 1) / g_sw3dLightSampleBlockSize;
	g_sw3dLightSampleCacheSceneStampBase += (uint16_t)pixelsdeep;
	g_invProjScale = 1.0f / (float)perspFactor;
	if (g_useHardware3D)
		RenderScene_InitHardwareFrame();
}

// FUNCTION: TIE98 0x43DB90 sw3d_UnlockSceneBuffers
void RenderScene_UnlockSceneBuffers_tie98(void) {
	/* PORT: the original unlocks its handle-backed scene arrays. The host
	 * arrays remain directly addressable for their complete lifetime. */
}

// FUNCTION: TIE98 0x41F320
static void RenderScene_ComputeVertexLighting(SceneMeshTIE98* mesh, ProjVertexTIE98* output,
											  const Vec3f* normal, const Vec3f* position,
											  const Vec3f* eye_position) {
	if (mesh->pObject->genus == GENUS_PROJECTILE_PLAYER || mesh->pObject->genus == GENUS_PROJECTILE_NPC) {
		output->lightIntensity = 1.0f;
		return;
	}

	if (g_directionalLightingEnabled) {
		output->lightIntensity =
			((float)rotlightX * normal->x + (float)rotlightY * normal->y + (float)rotlightZ * normal->z) *
			(0.8f / 32768.0f);
		if (output->lightIntensity < 0.0f)
			output->lightIntensity = 0.0f;
		else {
			Vec3f light_position = {
				position->x + (float)rotlightX,
				position->y + (float)rotlightY,
				position->z + (float)rotlightZ,
			};
			if (FlightModel_IsLightSegmentBlocked(mesh->pObject, position, &light_position))
				output->lightIntensity = 0.0f;
		}
	} else {
		output->lightIntensity = 0.4f;
	}

	for (int i = 0; i < localLightCnt; ++i) {
		const float dx = (float)localLights[i].x - position->x;
		const float dy = (float)localLights[i].y - position->y;
		const float dz = (float)localLights[i].z - position->z;
		float light_dot = dx * normal->x + dy * normal->y + dz * normal->z;
		const Vec3f light_position = {
			(float)localLights[i].x,
			(float)localLights[i].y,
			(float)localLights[i].z,
		};
		if (FlightModel_IsLightSegmentBlocked(mesh->pObject, position, &light_position))
			continue;
		const float absolute_x = fabsf(dx);
		const float absolute_y = fabsf(dy);
		const float absolute_z = fabsf(dz);
		float distance;
		if (absolute_x >= absolute_y && absolute_x >= absolute_z)
			distance = absolute_x + (absolute_y + absolute_z) * 0.2941f;
		else if (absolute_y >= absolute_x && absolute_y >= absolute_z)
			distance = absolute_y + (absolute_x + absolute_z) * 0.2941f;
		else
			distance = absolute_z + (absolute_x + absolute_y) * 0.2941f;
		if (light_dot / distance < -0.3f)
			continue;
		light_dot = distance * 0.5f;
		const float diffuse = light_dot / (distance * distance);

		const float half_x = eye_position->x - position->x + dx;
		const float half_y = eye_position->y - position->y + dy;
		const float half_z = eye_position->z - position->z + dz;
		const float half_dot = (half_x * normal->x + half_y * normal->y + half_z * normal->z) * 0.5f;
		float specular = 0.0f;
		if (g_specularLightingEnabled) {
			const float absolute_half_x = fabsf(half_x);
			const float absolute_half_y = fabsf(half_y);
			const float absolute_half_z = fabsf(half_z);
			float half_distance;
			if (absolute_half_x >= absolute_half_y && absolute_half_x >= absolute_half_z)
				half_distance = absolute_half_x * 0.4632f + (absolute_half_y + absolute_half_z) * 0.1936f;
			else if (absolute_half_y >= absolute_half_x && absolute_half_y >= absolute_half_z)
				half_distance = absolute_half_y * 0.4632f + (absolute_half_x + absolute_half_z) * 0.1936f;
			else
				half_distance = absolute_half_z * 0.4632f + (absolute_half_x + absolute_half_y) * 0.1936f;
			const float cosine = half_dot / half_distance;
			if (cosine >= 0.5f) {
				const float cosine6 = cosine * (cosine * cosine) * (cosine * (cosine * cosine));
				const float cosine12 = cosine6 * cosine6;
				const float cosine24 = cosine12 * cosine12;
				specular = cosine24 * cosine24;
			}
		}
		const float contribution = (diffuse + specular) * (float)localLights[i].range;
		if (contribution > 0.0f) {
			output->lightIntensity += contribution;
			if (output->lightIntensity >= 1.0f) {
				output->lightIntensity = 1.0f;
				return;
			}
		}
	}
}

// FUNCTION: TIE98 0x41FEF0
static void RenderScene_TransformFaceTextureGradients(SceneFaceTIE98* face,
													  const FaceTextureGradientsTIE98* gradients,
													  SceneMeshTIE98* mesh) {
	*(Vec3f*)&face->gradients[0] = gradients->gradient0;
	Math3D_RotateVec3((Vec3f*)&face->gradients[0], &mesh->viewOrient);
	*(Vec3f*)&face->gradients[3] = gradients->gradient1;
	Math3D_RotateVec3((Vec3f*)&face->gradients[3], &mesh->viewOrient);
}

// FUNCTION: TIE98 0x430F70
static int RenderScene_CullMeshFacesFromView(SceneMeshTIE98* mesh) {
	mesh->faceBaseIndex = g_visFaceCount;
	g_meshEyePos = mesh->pos;
	if (g_bBackdropMeshMode) {
		g_meshEyePos = (Vec3f) { 0.0f, 0.0f, -100000.0f };
		Math3D_RotateVec3(&g_meshEyePos, &mesh->orient);
		g_meshEyePos.x += mesh->pos.x;
		g_meshEyePos.y += mesh->pos.y;
		g_meshEyePos.z += mesh->pos.z;
	}
	SceneFaceTIE98* output = &g_visFaceList[g_visFaceCount];
	for (int face_index = 0; face_index < mesh->faceCount; ++face_index) {
		const FaceRecordTIE98* record = &mesh->pFaceGeom[face_index];
		const Vec3f* vertex = &mesh->pModelVerts[record->vertexIdx[0]];
		const Vec3f view = {
			g_meshEyePos.x - vertex->x,
			g_meshEyePos.y - vertex->y,
			g_meshEyePos.z - vertex->z,
		};
		if (Math3D_Dot3(&view.x, &mesh->pFaceNormals[face_index].x) >= 0.0f) {
			output->faceIndex = face_index;
			output->pMesh = mesh;
			output->pPhongData = &g_scenePhongData[12 * g_phongSlotIndex * (g_phongSlotStride + 1)];
			if (g_phongSlotIndex < 199)
				++g_phongSlotIndex;
			output->packed = face_index + (g_curLayerId << 16);
			output->pScanEdge = NULL;
			++output;
			++g_visFaceCount;
		}
	}
	mesh->visFaceCount = g_visFaceCount - mesh->faceBaseIndex;
	return mesh->faceBaseIndex;
}

// FUNCTION: TIE98 0x427E30
static int RenderScene_ProjectMeshVertices(SceneMeshTIE98* mesh) {
	SceneFaceTIE98* face = &g_visFaceList[mesh->faceBaseIndex];
	ProjVertexTIE98* output = &g_projVertList[g_projVertCount];
	mesh->vertBaseIndex = g_projVertCount;
	mesh->projVertCursor = 0;
	for (int i = 0; i < mesh->vertexCount; ++i)
		g_vertexRemap[i] = -1;

	for (int face_iter = 0; face_iter < mesh->visFaceCount; ++face_iter, ++face) {
		RenderScene_TransformFaceTextureGradients(face, &mesh->pFaceTexturing[face->faceIndex], mesh);
		const FaceRecordTIE98* record = &mesh->pFaceGeom[face->faceIndex];
		face->maxVertW = 0.0f;
		face->minVertW = (float)perspFactor;
		face->nearClipState = 0;
		float face_w_total = 0.0f;
		for (int corner = 0; corner < 4; ++corner) {
			const int vertex_index = record->vertexIdx[corner];
			if (vertex_index == -1)
				break;
			int remapped = g_vertexRemap[vertex_index];
			float face_w;
			if (remapped == -1) {
				remapped = mesh->projVertCursor++;
				g_vertexRemap[vertex_index] = remapped;
				Vec3f position = mesh->pModelVerts[vertex_index];
				Math3D_RotateVec3(&position, &mesh->viewOrient);
				position.x += mesh->viewPos.x;
				position.y += mesh->viewPos.y;
				position.z += mesh->viewPos.z;
				if (position.z < 1.0f) {
					output->w = position.z - 1.0f;
					output->sx = position.x;
					output->sy = position.y;
					face->nearClipState = -1;
					face_w = (float)perspFactor;
				} else {
					output->w = (float)perspFactor / position.z;
					output->sx = (float)halfpixelswide + position.x * output->w;
					output->sy = (float)(transfm2_screenyoffset + halfpixelsdeep) + position.y * output->w;
					face_w = output->w;
				}
				RenderScene_ComputeVertexLighting(mesh, output,
												  &mesh->pVertNormals[record->normalIdx[corner]],
												  &mesh->pModelVerts[vertex_index], &g_meshEyePos);
				output->tu = mesh->pUVs[record->uvIdx[corner]].u;
				output->tv = mesh->pUVs[record->uvIdx[corner]].v;
				++output;
			} else {
				const ProjVertexTIE98* projected = &g_projVertList[mesh->vertBaseIndex + remapped];
				if (projected->w < 0.0f) {
					face->nearClipState = -1;
					face_w = (float)perspFactor;
				} else {
					face_w = projected->w;
				}
			}
			face_w_total += face_w;
			if (face_w > face->maxVertW)
				face->maxVertW = face_w;
			if (face_w < face->minVertW)
				face->minVertW = face_w;
		}
		if (mesh->pUVs) {
			const int uv_index = record->uvIdx[0];
			Vec3f position = mesh->pModelVerts[record->vertexIdx[0]];
			Math3D_RotateVec3(&position, &mesh->viewOrient);
			position.x += mesh->viewPos.x;
			position.y += mesh->viewPos.y;
			position.z += mesh->viewPos.z;
			const float u = mesh->pUVs[uv_index].u;
			const float v = mesh->pUVs[uv_index].v;
			face->gradients[6] = position.x - face->gradients[0] * u - v * face->gradients[3];
			face->gradients[7] = position.y - face->gradients[1] * u - v * face->gradients[4];
			face->gradients[8] = position.z - face->gradients[2] * u - v * face->gradients[5];

			const float c00 =
				face->gradients[8] * face->gradients[4] - face->gradients[5] * face->gradients[7];
			const float c01 =
				face->gradients[5] * face->gradients[6] - face->gradients[8] * face->gradients[3];
			float c20 = face->gradients[5] * face->gradients[1] - face->gradients[2] * face->gradients[4];
			const float c02 =
				face->gradients[7] * face->gradients[3] - face->gradients[4] * face->gradients[6];
			const float c10 =
				face->gradients[2] * face->gradients[7] - face->gradients[8] * face->gradients[1];
			const float c11 =
				face->gradients[8] * face->gradients[0] - face->gradients[2] * face->gradients[6];
			const float c12 =
				face->gradients[1] * face->gradients[6] - face->gradients[7] * face->gradients[0];
			float c21 = face->gradients[2] * face->gradients[3] - face->gradients[5] * face->gradients[0];
			float c22 = face->gradients[4] * face->gradients[0] - face->gradients[1] * face->gradients[3];
			if (c20 == 0.0f && c21 == 0.0f && c22 == 0.0f)
				c22 = 1.0f;

			const float inverse =
				1.0f / (c21 * face->gradients[7] + c22 * face->gradients[8] + c20 * face->gradients[6]);
			const float scaled = inverse * g_invProjScale;
			face->gradients[0] = scaled * c00;
			face->gradients[1] = scaled * c01;
			face->gradients[2] = inverse * c02;
			face->gradients[3] = scaled * c10;
			face->gradients[4] = scaled * c11;
			face->gradients[5] = inverse * c12;
			face->gradients[6] = scaled * c20;
			face->gradients[7] = scaled * c21;
			face->gradients[8] = inverse * c22;
			const float center_x = (float)halfpixelswide;
			const float center_y = (float)(transfm2_screenyoffset + halfpixelsdeep);
			face->gradients[2] -= center_x * face->gradients[0] + center_y * face->gradients[1];
			face->gradients[5] -= center_x * face->gradients[3] + center_y * face->gradients[4];
			face->gradients[8] -= center_x * face->gradients[6] + center_y * face->gradients[7];
			float area = face->gradients[0] * face->gradients[4] - face->gradients[3] * face->gradients[1];
			if (area < 0.0f)
				area = -area;
			const float corner_count = record->vertexIdx[3] == -1 ? 3.0f : 4.0f;
			const float lod_scale = corner_count / face_w_total * (float)perspFactor;
			face->mipLevel = (int)((float)((mesh->pMaterial->width * mesh->pMaterial->height) << 8) *
								   (lod_scale * lod_scale * area));
		}
	}
	g_projVertCount += mesh->projVertCursor;
	return g_projVertCount;
}

// FUNCTION: TIE98 0x4285C0
static int RenderScene_ProjectDistantMeshVertices(SceneMeshTIE98* mesh) {
	const float project_scale = (float)perspFactor / mesh->viewPos.z * 100000.0f;
	SceneFaceTIE98* face = &g_visFaceList[mesh->faceBaseIndex];
	ProjVertexTIE98* output = &g_projVertList[g_projVertCount];
	mesh->vertBaseIndex = g_projVertCount;
	mesh->projVertCursor = 0;
	for (int i = 0; i < mesh->vertexCount; ++i)
		g_vertexRemap[i] = -1;

	for (int face_iter = 0; face_iter < mesh->visFaceCount; ++face_iter, ++face) {
		const FaceRecordTIE98* record = &mesh->pFaceGeom[face->faceIndex];
		face->maxVertW = 0.0f;
		face->minVertW = (float)perspFactor;
		for (int corner = 0; corner < 4; ++corner) {
			const int vertex_index = record->vertexIdx[corner];
			if (vertex_index == -1)
				break;
			const int uv_index = record->uvIdx[corner];
			const int normal_index = record->normalIdx[corner];
			int remapped = g_vertexRemap[vertex_index];
			float w;
			if (remapped == -1) {
				g_vertexRemap[vertex_index] = mesh->projVertCursor++;
				Vec3f position = mesh->pModelVerts[vertex_index];
				Math3D_RotateVec3(&position, &mesh->viewOrient);
				position.x += mesh->viewPos.x;
				position.y += mesh->viewPos.y;
				position.z += mesh->viewPos.z + 100000.0f;
				output->w = project_scale / position.z;
				output->sx = (float)halfpixelswide + position.x * output->w;
				output->sy = (float)(transfm2_screenyoffset + halfpixelsdeep) + position.y * output->w;
				w = output->w;
				RenderScene_ComputeVertexLighting(mesh, output, &mesh->pVertNormals[normal_index],
												  &mesh->pModelVerts[vertex_index], &g_meshEyePos);
				output->tu = mesh->pUVs[uv_index].u;
				output->tv = mesh->pUVs[uv_index].v;
				++output;
			} else {
				w = g_projVertList[mesh->vertBaseIndex + remapped].w;
			}
			if (w > face->maxVertW)
				face->maxVertW = w;
			if (w < face->minVertW)
				face->minVertW = w;
		}
	}
	g_projVertCount += mesh->projVertCursor;
	return g_projVertCount;
}

// FUNCTION: TIE98 0x431130
static void sw3d_ProjectMeshVertices(SceneMeshTIE98* mesh) {
	SceneFaceTIE98* face = &g_visFaceList[mesh->faceBaseIndex];
	ProjVertexTIE98* output = &g_projVertList[g_projVertCount];
	mesh->vertBaseIndex = g_projVertCount;
	mesh->projVertCursor = 0;
	for (int vertex_index = 0; vertex_index < mesh->vertexCount; ++vertex_index)
		g_vertexRemap[vertex_index] = -1;

	for (int face_index = 0; face_index < mesh->visFaceCount; ++face_index, ++face) {
		RenderScene_TransformFaceTextureGradients(face, &mesh->pFaceTexturing[face->faceIndex], mesh);
		const FaceRecordTIE98* record = &mesh->pFaceGeom[face->faceIndex];
		face->maxVertW = 0.0f;
		face->minVertW = (float)perspFactor;
		face->nearClipState = 0;
		float face_w_total = 0.0f;
		for (int corner = 0; corner < 4; ++corner) {
			const int vertex_index = record->vertexIdx[corner];
			if (vertex_index == -1)
				break;
			int remapped = g_vertexRemap[vertex_index];
			float face_w;
			if (remapped == -1) {
				remapped = mesh->projVertCursor++;
				g_vertexRemap[vertex_index] = remapped;
				Vec3f position = mesh->pModelVerts[vertex_index];
				Math3D_RotateVec3(&position, &mesh->viewOrient);
				position.x += mesh->viewPos.x;
				position.y += mesh->viewPos.y;
				position.z += mesh->viewPos.z;
				if (position.z < 1.0f) {
					output->w = position.z - 1.0f;
					output->sx = position.x;
					output->sy = position.y;
					face->nearClipState = -1;
					face_w = (float)perspFactor;
				} else {
					output->w = (float)perspFactor / position.z;
					output->sx = (float)halfpixelswide + position.x * output->w;
					output->sy = (float)(transfm2_screenyoffset + halfpixelsdeep) + position.y * output->w;
					face_w = output->w;
				}
				RenderScene_ComputeVertexLighting(mesh, output,
												  &mesh->pVertNormals[record->normalIdx[corner]],
												  &mesh->pModelVerts[vertex_index], &g_meshEyePos);
				++output;
			} else {
				const ProjVertexTIE98* projected = &g_projVertList[mesh->vertBaseIndex + remapped];
				if (projected->w < 0.0f) {
					face->nearClipState = -1;
					face_w = (float)perspFactor;
				} else {
					face_w = projected->w;
				}
			}
			face_w_total += face_w;
			if (face_w > face->maxVertW)
				face->maxVertW = face_w;
			if (face_w < face->minVertW)
				face->minVertW = face_w;
		}

		if (mesh->pUVs) {
			const int uv_index = record->uvIdx[0];
			Vec3f position = mesh->pModelVerts[record->vertexIdx[0]];
			Math3D_RotateVec3(&position, &mesh->viewOrient);
			position.x += mesh->viewPos.x;
			position.y += mesh->viewPos.y;
			position.z += mesh->viewPos.z;
			const float u = mesh->pUVs[uv_index].u;
			const float v = mesh->pUVs[uv_index].v;
			face->gradients[6] = position.x - face->gradients[0] * u - face->gradients[3] * v;
			face->gradients[7] = position.y - face->gradients[1] * u - face->gradients[4] * v;
			face->gradients[8] = position.z - face->gradients[2] * u - face->gradients[5] * v;

			const float c00 =
				face->gradients[8] * face->gradients[4] - face->gradients[5] * face->gradients[7];
			const float c01 =
				face->gradients[5] * face->gradients[6] - face->gradients[8] * face->gradients[3];
			float c20 = face->gradients[5] * face->gradients[1] - face->gradients[2] * face->gradients[4];
			const float c02 =
				face->gradients[7] * face->gradients[3] - face->gradients[4] * face->gradients[6];
			const float c10 =
				face->gradients[2] * face->gradients[7] - face->gradients[8] * face->gradients[1];
			const float c11 =
				face->gradients[8] * face->gradients[0] - face->gradients[2] * face->gradients[6];
			const float c12 =
				face->gradients[1] * face->gradients[6] - face->gradients[7] * face->gradients[0];
			float c21 = face->gradients[2] * face->gradients[3] - face->gradients[5] * face->gradients[0];
			float c22 = face->gradients[4] * face->gradients[0] - face->gradients[1] * face->gradients[3];
			if (c20 == 0.0f && c21 == 0.0f && c22 == 0.0f)
				c22 = 1.0f;

			const float inverse =
				1.0f / (c20 * face->gradients[6] + c21 * face->gradients[7] + c22 * face->gradients[8]);
			const float scaled = inverse * g_invProjScale;
			face->gradients[0] = scaled * c00;
			face->gradients[1] = scaled * c01;
			face->gradients[2] = inverse * c02;
			face->gradients[3] = scaled * c10;
			face->gradients[4] = scaled * c11;
			face->gradients[5] = inverse * c12;
			face->gradients[6] = scaled * c20;
			face->gradients[7] = scaled * c21;
			face->gradients[8] = inverse * c22;
			const float center_x = (float)halfpixelswide;
			const float center_y = (float)(transfm2_screenyoffset + halfpixelsdeep);
			face->gradients[2] -= center_x * face->gradients[0] + center_y * face->gradients[1];
			face->gradients[5] -= center_x * face->gradients[3] + center_y * face->gradients[4];
			face->gradients[8] -= center_x * face->gradients[6] + center_y * face->gradients[7];
			float area = face->gradients[0] * face->gradients[4] - face->gradients[3] * face->gradients[1];
			if (area < 0.0f)
				area = -area;
			const float corner_count = record->vertexIdx[3] == -1 ? 3.0f : 4.0f;
			const float lod_scale = corner_count / face_w_total * (float)perspFactor;
			face->mipLevel = (int)((float)((mesh->pMaterial->width * mesh->pMaterial->height) << 8) *
								   (lod_scale * lod_scale * area));
		}
	}
	g_projVertCount += mesh->projVertCursor;
}

// FUNCTION: TIE98 0x4318A0
static void sw3d_ProjectMeshVerticesDistant(SceneMeshTIE98* mesh) {
	const float project_scale = (float)perspFactor / mesh->viewPos.z * 100000.0f;
	SceneFaceTIE98* face = &g_visFaceList[mesh->faceBaseIndex];
	ProjVertexTIE98* output = &g_projVertList[g_projVertCount];
	mesh->vertBaseIndex = g_projVertCount;
	mesh->projVertCursor = 0;
	for (int vertex_index = 0; vertex_index < mesh->vertexCount; ++vertex_index)
		g_vertexRemap[vertex_index] = -1;

	for (int face_index = 0; face_index < mesh->visFaceCount; ++face_index, ++face) {
		RenderScene_TransformFaceTextureGradients(face, &mesh->pFaceTexturing[face->faceIndex], mesh);
		const FaceRecordTIE98* record = &mesh->pFaceGeom[face->faceIndex];
		face->maxVertW = 0.0f;
		face->minVertW = (float)perspFactor;
		for (int corner = 0; corner < 4; ++corner) {
			const int vertex_index = record->vertexIdx[corner];
			if (vertex_index == -1)
				break;
			int remapped = g_vertexRemap[vertex_index];
			float w;
			if (remapped == -1) {
				remapped = mesh->projVertCursor++;
				g_vertexRemap[vertex_index] = remapped;
				Vec3f position = mesh->pModelVerts[vertex_index];
				Math3D_RotateVec3(&position, &mesh->viewOrient);
				position.x += mesh->viewPos.x;
				position.y += mesh->viewPos.y;
				position.z += mesh->viewPos.z + 100000.0f;
				output->w = project_scale / position.z;
				output->sx = (float)halfpixelswide + position.x * output->w;
				output->sy = (float)(transfm2_screenyoffset + halfpixelsdeep) + position.y * output->w;
				w = output->w;
				RenderScene_ComputeVertexLighting(mesh, output,
												  &mesh->pVertNormals[record->normalIdx[corner]],
												  &mesh->pModelVerts[vertex_index], &g_meshEyePos);
				++output;
			} else {
				w = g_projVertList[mesh->vertBaseIndex + remapped].w;
			}
			if (w > face->maxVertW)
				face->maxVertW = w;
			if (w < face->minVertW)
				face->minVertW = w;
		}

		if (mesh->pUVs) {
			const int uv_index = record->uvIdx[0];
			Vec3f position = mesh->pModelVerts[record->vertexIdx[0]];
			Math3D_RotateVec3(&position, &mesh->viewOrient);
			position.x += mesh->viewPos.x;
			position.y += mesh->viewPos.y;
			position.z += mesh->viewPos.z + 100000.0f;
			const float u = mesh->pUVs[uv_index].u;
			const float v = mesh->pUVs[uv_index].v;
			face->gradients[6] = position.x - face->gradients[0] * u - face->gradients[3] * v;
			face->gradients[7] = position.y - face->gradients[1] * u - face->gradients[4] * v;
			face->gradients[8] = position.z - face->gradients[2] * u - face->gradients[5] * v;

			const float c00 =
				face->gradients[8] * face->gradients[4] - face->gradients[5] * face->gradients[7];
			const float c01 =
				face->gradients[5] * face->gradients[6] - face->gradients[8] * face->gradients[3];
			float c20 = face->gradients[5] * face->gradients[1] - face->gradients[2] * face->gradients[4];
			const float c02 =
				face->gradients[7] * face->gradients[3] - face->gradients[4] * face->gradients[6];
			const float c10 =
				face->gradients[2] * face->gradients[7] - face->gradients[8] * face->gradients[1];
			const float c11 =
				face->gradients[8] * face->gradients[0] - face->gradients[2] * face->gradients[6];
			const float c12 =
				face->gradients[1] * face->gradients[6] - face->gradients[7] * face->gradients[0];
			float c21 = face->gradients[2] * face->gradients[3] - face->gradients[5] * face->gradients[0];
			float c22 = face->gradients[4] * face->gradients[0] - face->gradients[1] * face->gradients[3];
			if (c20 == 0.0f && c21 == 0.0f && c22 == 0.0f)
				c22 = 1.0f;

			const float inverse =
				1.0f / (c20 * face->gradients[6] + c21 * face->gradients[7] + c22 * face->gradients[8]);
			const float scaled = inverse / project_scale;
			face->gradients[0] = scaled * c00;
			face->gradients[1] = scaled * c01;
			face->gradients[2] = inverse * c02;
			face->gradients[3] = scaled * c10;
			face->gradients[4] = scaled * c11;
			face->gradients[5] = inverse * c12;
			face->gradients[6] = scaled * c20;
			face->gradients[7] = scaled * c21;
			face->gradients[8] = inverse * c22;
			const float center_x = (float)halfpixelswide;
			const float center_y = (float)(transfm2_screenyoffset + halfpixelsdeep);
			face->gradients[2] -= center_x * face->gradients[0] + center_y * face->gradients[1];
			face->gradients[5] -= center_x * face->gradients[3] + center_y * face->gradients[4];
			face->gradients[8] -= center_x * face->gradients[6] + center_y * face->gradients[7];
			const float texture_area = (float)((mesh->pMaterial->width * mesh->pMaterial->height) << 8);
			float mip_x = face->gradients[0] * position.z * position.z * face->gradients[4];
			float mip_y = position.z * position.z * face->gradients[1] * face->gradients[3];
			if (mip_x < 0.0f)
				mip_x = -mip_x;
			if (mip_y < 0.0f)
				mip_y = -mip_y;
			face->mipLevel = (int)(texture_area * mip_x) + (int)(texture_area * mip_y);
		}
	}
	g_projVertCount += mesh->projVertCursor;
}

static int RenderScene_HardwareStagingHasCapacity(int vertex_count, int triangle_count) {
	return vertex_count >= 0 && triangle_count >= 0 &&
		g_d3dVertexCount <= TIE98_HARDWARE_VERTEX_CAPACITY - vertex_count &&
		g_d3dIndexCount <= TIE98_HARDWARE_TRIANGLE_CAPACITY - triangle_count;
}

// FUNCTION: TIE98 0x42B130
static int RenderScene_EmitFlightVertex(int vertex_index, ProjVertexTIE98* vertices) {
	if (!RenderScene_HardwareStagingHasCapacity(1, 0))
		return -1;
	ProjVertexTIE98* source = &vertices[vertex_index];
	float w = source->w;
	if (w < 0.0f)
		w = (float)perspFactor;
	float depth = 1.0f / ((float)perspFactor / w * (1.0f / 2048.0f) + 1.0f);
	if (g_std3DZBufferBitDepth == 2)
		depth = 1.0f - depth;
	D3DTLVERTEX* output = &g_flightVertexBuffer[g_d3dVertexCount];
	output->sx = source->sx + g_flightVpOriginX;
	output->sy = source->sy + g_flightVpOriginY;
	output->sz = depth;
	output->rhw = w;
	output->tu = source->tu;
	output->tv = source->tv;
	int intensity = 48 + (int)(source->lightIntensity * 320.0f);
	if (intensity > 255)
		intensity = 255;
	output->color = (uint32_t)(65793 * intensity - (g_capVertexAlpha ? 0x2000000 : 0x1000000));
	output->specular = 0;
	return g_d3dVertexCount++;
}

// FUNCTION: TIE98 0x428860
static void RenderScene_DrawMeshFaces(SceneMeshTIE98* mesh) {
	ProjVertexTIE98* vertices = &g_projVertList[mesh->vertBaseIndex];
	const int projection_end = mesh->vertBaseIndex + mesh->projVertCursor;
	g_clipVertCursor = projection_end;
	for (int i = 0; i < projection_end; ++i)
		g_emittedVertexByProjection[i] = -1;
	SceneFaceTIE98* face = &g_visFaceList[mesh->faceBaseIndex];
	const uint8_t* previous_texels = NULL;
	Std3DTextureSurface* opaque = NULL;
	Std3DTextureSurface* color_key = NULL;

	for (int face_iter = 0; face_iter < mesh->visFaceCount; ++face_iter, ++face) {
		const FaceRecordTIE98* record = &mesh->pFaceGeom[face->faceIndex];
		g_clipCountA = record->edgeIdx[3] != -1 ? 4 : 3;
		float u_scale = 1.0f;
		float v_scale = 1.0f;
		if (g_pStd3DCurDevice->caps.bSquareOnlyTexture) {
			int width = mesh->pMaterial->width;
			int height = mesh->pMaterial->height;
			if (width > height) {
				while (height < width) {
					v_scale *= 0.5f;
					height *= 2;
				}
			} else if (height > width) {
				while (width < height) {
					u_scale *= 0.5f;
					width *= 2;
				}
			}
		}
		for (int corner = 0; corner < g_clipCountA; ++corner) {
			int projected = g_vertexRemap[record->vertexIdx[corner]];
			g_clipIdxA[corner] = projected;
			ProjVertexTIE98* vertex = &vertices[projected];
			OptTexCoordTIE98 uv = mesh->pUVs[record->uvIdx[corner]];
			uv.u *= u_scale;
			uv.v *= v_scale;
			if (vertex->tu != uv.u || vertex->tv != uv.v) {
				const int duplicate = g_clipVertCursor++;
				vertices[duplicate] = *vertex;
				vertices[duplicate].tu = uv.u;
				vertices[duplicate].tv = uv.v;
				g_clipIdxA[corner] = duplicate;
			}
		}

		int previous;
		if (face->nearClipState == -1) {
			memcpy(g_clipIdxB, g_clipIdxA, (size_t)g_clipCountA * sizeof *g_clipIdxA);
			g_clipCountB = g_clipCountA;
			g_clipCountA = 0;
			if (g_clipCountB > 0) {
				previous = g_clipIdxB[g_clipCountB - 1];
				for (int i = 0; i < g_clipCountB; ++i) {
					const int current = g_clipIdxB[i];
					RenderClip_ClipPolyNear(previous, current, vertices);
					previous = current;
				}
			}
		}
		g_clipCountB = 0;
		if (g_clipCountA > 0) {
			previous = g_clipIdxA[g_clipCountA - 1];
			const int count = g_clipCountA;
			for (int i = 0; i < count; ++i) {
				const int current = g_clipIdxA[i];
				RenderClip_ClipPolyTop(previous, current, vertices);
				previous = current;
			}
		}
		g_clipCountA = 0;
		if (g_clipCountB > 0) {
			previous = g_clipIdxB[g_clipCountB - 1];
			const int count = g_clipCountB;
			for (int i = 0; i < count; ++i) {
				const int current = g_clipIdxB[i];
				RenderClip_ClipPolyBottom(previous, current, vertices);
				previous = current;
			}
		}
		g_clipCountB = 0;
		if (g_clipCountA > 0) {
			previous = g_clipIdxA[g_clipCountA - 1];
			const int count = g_clipCountA;
			for (int i = 0; i < count; ++i) {
				const int current = g_clipIdxA[i];
				RenderClip_ClipPolyLeft(previous, current, vertices);
				previous = current;
			}
		}
		g_clipCountA = 0;
		if (g_clipCountB > 0) {
			previous = g_clipIdxB[g_clipCountB - 1];
			const int count = g_clipCountB;
			for (int i = 0; i < count; ++i) {
				const int current = g_clipIdxB[i];
				RenderClip_ClipPolyRight(previous, current, vertices);
				previous = current;
			}
		}
		face->nearClipState = pixelsdeep;
		for (int i = 0; i < g_clipCountA; ++i) {
			const int projected = g_clipIdxA[i];
			int emitted;
			if (projected < projection_end) {
				if (g_emittedVertexByProjection[projected] == -1)
					g_emittedVertexByProjection[projected] =
						RenderScene_EmitFlightVertex(projected, vertices);
				emitted = g_emittedVertexByProjection[projected];
			} else {
				emitted = RenderScene_EmitFlightVertex(projected, vertices);
			}
			if (emitted < 0)
				return;
			g_clipIdxA[i] = emitted;
		}

		if (g_clipCountA > 2) {
			int texture_width = mesh->pMaterial->width;
			int texture_height = mesh->pMaterial->height;
			int texel_offset = 0;
			int mip = (int)((float)face->mipLevel * g_mipLodScale);
			if (texture_width * texture_height == mesh->pMaterial->textureSize) {
				while (mip > 256 && texture_width != 8 && texture_height != 8) {
					mip >>= 2;
					texel_offset += texture_width * texture_height;
					texture_width >>= 1;
					texture_height >>= 1;
				}
			}
			uint8_t* texels = mesh->pTexels + texel_offset;
			if (texels != previous_texels) {
				previous_texels = texels;
				uint16_t* palette = (uint16_t*)(mesh->pPalette1 + 4096);
				opaque = RenderTexture_GetOrCreateOpaque(texture_width, texture_height, palette, texels);
				color_key = NULL;
				if (palette[256] != 0) {
					if (mesh->pObject->genus == GENUS_PROJECTILE_PLAYER ||
						mesh->pObject->genus == GENUS_PROJECTILE_NPC) {
						palette[256] = 0;
					} else if (mesh->textureName && mesh->textureName[0] != '_') {
						color_key = RenderTexture_GetOrCreateColorKey(texture_width, texture_height,
																	  (uint16_t*)mesh->pPalette1, texels);
						if (!color_key && texture_width == mesh->pMaterial->width &&
							texture_height == mesh->pMaterial->height)
							*(char*)mesh->textureName = '_';
					}
				}
			}
		}

		const int triangles_per_pass = g_clipCountA > 2 ? g_clipCountA - 2 : 0;
		const int additional_vertices = color_key ? g_clipCountA : 0;
		const int additional_triangles = triangles_per_pass * (color_key ? 2 : 1);
		if (!RenderScene_HardwareStagingHasCapacity(additional_vertices, additional_triangles))
			return;

		if (color_key) {
			int color_key_indices[32];
			for (int i = 0; i < g_clipCountA; ++i) {
				g_flightVertexBuffer[g_d3dVertexCount] = g_flightVertexBuffer[g_clipIdxA[i]];
				g_flightVertexBuffer[g_d3dVertexCount].color = 0xffffffffu;
				color_key_indices[i] = g_d3dVertexCount++;
			}
			for (int i = 2; i < g_clipCountA; ++i) {
				Std3DRenderTri* triangle = &g_triBuffer[g_d3dIndexCount++];
				triangle->v0 = color_key_indices[0];
				triangle->v1 = color_key_indices[i - 1];
				triangle->v2 = color_key_indices[i];
				triangle->texture = color_key;
				triangle->flags = (Std3DRenderStateFlags)(38931 + (g_bilinearEnabled ? 384 : 0) + 512);
			}
		}
		for (int i = 2; i < g_clipCountA; ++i) {
			Std3DRenderTri* triangle = &g_triBuffer[g_d3dIndexCount++];
			triangle->v0 = g_clipIdxA[0];
			triangle->v1 = g_clipIdxA[i - 1];
			triangle->v2 = g_clipIdxA[i];
			triangle->texture = opaque;
			triangle->flags =
				(Std3DRenderStateFlags)(38931 + (g_bilinearEnabled ? 384 : 0) + (g_capVertexAlpha ? 512 : 0));
			if (g_capVertexAlpha)
				g_capVertexAlpha = 0;
		}
	}
}

// FUNCTION: TIE98 0x42AD90
static void RenderScene_DrawMesh(SceneMeshTIE98* mesh) {
	g_projVertCount = 0;
	g_sceneEdgeCursor = 0;
	const int saved_visible_face_count = g_visFaceCount;
	if (g_meshQueueIndex == TIE98_MESH_QUEUE_MAX || g_visFaceCount + mesh->faceCount > TIE98_SCENE_FACE_MAX ||
		mesh->vertexCount > g_projVertCapacity || mesh->edgeCount > g_sceneEdgeCapacity)
		return;
	g_meshQueue[g_meshQueueIndex] = *mesh;
	SceneMeshTIE98* queued = &g_meshQueue[g_meshQueueIndex];
	RenderScene_CullMeshFacesFromView(queued);
	if (queued->visFaceCount != 0) {
		if (g_d3dVertexCount + 8 * queued->visFaceCount > g_maxBatchVerts ||
			g_d3dIndexCount + 2 * queued->visFaceCount > g_maxBatchTris) {
			Math_SetFpuExtendedPrecisionMode();
			if (!g_powerVrSceneWorkaround)
				std3D_StartScene();
			std3D_LockExecuteBuffer();
			std3D_AddVertices(g_flightVertexBuffer, g_d3dVertexCount);
			std3D_BeginInstructions();
			std3D_AddTriangles(g_triBuffer, (unsigned int)g_d3dIndexCount);
			std3D_ExecuteBuffer();
			if (!g_powerVrSceneWorkaround)
				std3D_EndScene();
			Math_SetFpuSinglePrecisionMode();
			g_d3dIndexCount = 0;
			g_d3dVertexCount = 0;
		}
		if (g_bBackdropMeshMode)
			RenderScene_ProjectDistantMeshVertices(queued);
		else
			RenderScene_ProjectMeshVertices(queued);
		RenderScene_DrawMeshFaces(queued);
		g_visFaceCount = saved_visible_face_count;
	}
}

// FUNCTION: TIE98 0x432CE0
static int sw3d_SetupEdge(SceneEdgeTIE98* edge, ProjVertexTIE98* first, ProjVertexTIE98* second) {
	int first_y;
	if (first->sy < 0.0f) {
		first_y = 0;
	} else {
		first_y = (int)first->sy;
		if ((float)first_y != first->sy)
			++first_y;
	}
	int second_y;
	if (second->sy < 0.0f) {
		second_y = 0;
	} else {
		second_y = (int)second->sy;
		if ((float)second_y != second->sy)
			++second_y;
	}
	if (first_y == second_y)
		return -1;
	if (first_y > second_y) {
		ProjVertexTIE98* swap_vertex = first;
		first = second;
		second = swap_vertex;
		const int swap_y = first_y;
		first_y = second_y;
		second_y = swap_y;
	}
	if (second_y <= 0 || first_y >= (uint16_t)pixelsdeep)
		return -1;
	if (second_y > (uint16_t)pixelsdeep)
		second_y = (uint16_t)pixelsdeep;

	edge->yEnd = second_y;
	const float inverse_height = 1.0f / (second->sy - first->sy);
	edge->dxdy = (second->sx - first->sx) * inverse_height;
	edge->dLightIntensityDy = (second->lightIntensity - first->lightIntensity) * inverse_height;
	edge->pClipVert = NULL;
	const float first_row_offset = (float)first_y - first->sy;
	if (first_y != 0 || first_row_offset <= second->sy) {
		edge->x = first->sx + first_row_offset * edge->dxdy;
		edge->lightIntensity = first->lightIntensity + first_row_offset * edge->dLightIntensityDy;
		edge->yStart = first_y;
		return first_y;
	}

	edge->x = second->sx - second->sy * edge->dxdy;
	edge->lightIntensity = second->lightIntensity - second->sy * edge->dLightIntensityDy;
	edge->yStart = 0;
	return 0;
}

// FUNCTION: TIE98 0x432980
static int sw3d_SetupClippedEdge(SceneMeshTIE98* mesh, SceneEdgeTIE98* edge, ProjVertexTIE98* first,
								 ProjVertexTIE98* second) {
	ProjVertexTIE98* inside;
	ProjVertexTIE98* outside;
	if (second->w >= 0.0f) {
		inside = second;
		outside = first;
	} else {
		if (first->w < 0.0f)
			return -1;
		inside = first;
		outside = second;
	}

	if (outside->w < 0.0f) {
		g_sw3dClipBottom = g_sw3dClipTop;
		g_sw3dClipTop = &g_projVertList[mesh->vertBaseIndex + mesh->projVertCursor];
		g_sw3dGeneratedClipVertex = g_sw3dClipTop;
		++mesh->projVertCursor;
		++g_projVertCount;

		const float inside_inverse_w = 1.0f / inside->w;
		const float inside_x = (inside->sx - (float)halfpixelswide) * inside_inverse_w;
		const float inside_y =
			(inside->sy - (float)(transfm2_screenyoffset + halfpixelsdeep)) * inside_inverse_w;
		const float inside_z = (float)perspFactor * inside_inverse_w;
		const float t = outside->w / (outside->w + 1.0f - inside_z);
		const float clipped_x = outside->sx + (inside_x - outside->sx) * t;
		const float clipped_y = outside->sy + (inside_y - outside->sy) * t;
		g_sw3dClipTop->sx = (float)halfpixelswide + clipped_x * (float)perspFactor;
		g_sw3dClipTop->sy = (float)(transfm2_screenyoffset + halfpixelsdeep) + clipped_y * (float)perspFactor;
		g_sw3dClipTop->w = (float)perspFactor;
		g_sw3dClipTop->lightIntensity =
			outside->lightIntensity + (inside->lightIntensity - outside->lightIntensity) * t;
		outside = g_sw3dClipTop;
	}

	return sw3d_SetupEdge(edge, outside, inside);
}

static void sw3d_InsertSpan(float left_x, float right_x, int scan_y, SceneFaceTIE98* face);

// FUNCTION: TIE98 0x432350
static void sw3d_ScanConvertFace(SceneFaceTIE98* face) {
	SceneEdgeTIE98* left = face->edges[0];
	SceneEdgeTIE98* bottom = left;
	for (int edge_index = 1; edge_index < face->edgeCount; ++edge_index) {
		SceneEdgeTIE98* edge = face->edges[edge_index];
		if (edge->yStart < left->yStart)
			left = edge;
		if (edge->yEnd > bottom->yEnd)
			bottom = edge;
	}
	face->yTop = left->yStart;
	face->yBot = bottom->yEnd;
	const int span_count = face->yBot - face->yTop;
	if (g_sceneSpanPtrAvail <= span_count) {
		face->yBot = face->yTop;
		return;
	}
	g_sceneSpanPtrAvail -= span_count;
	face->pSpans = &g_sceneSpanPtrList[g_sceneSpanPtrAvail];

	SceneEdgeTIE98* right = NULL;
	for (int edge_index = 0; edge_index < face->edgeCount; ++edge_index) {
		SceneEdgeTIE98* edge = face->edges[edge_index];
		if (edge != left && edge->yStart == left->yStart) {
			right = edge;
			break;
		}
	}
	if (!right) {
		face->yBot = face->yTop;
		return;
	}
	if (left->x > right->x || (left->x == right->x && left->dxdy > right->dxdy)) {
		SceneEdgeTIE98* swap = left;
		left = right;
		right = swap;
	}
	face->pScanEdge = left;
	int scan_y = left->yStart;
	int remaining_edges = face->edgeCount;
	float left_start_x = left->x;
	float right_start_x = right->x;
	float left_start_light = left->lightIntensity;
	float right_start_light = right->lightIntensity;

	while (remaining_edges > 0) {
		const int run_end = left->yEnd < right->yEnd ? left->yEnd : right->yEnd;
		int run_rows = run_end - scan_y;
		if (run_rows > 0) {
			const float end_left_x = left->x + left->dxdy * (float)run_rows;
			const float end_right_x = right->x + right->dxdy * (float)run_rows;
			const int constant_light_gradient = end_right_x - end_left_x <= 1.0f;
			if (constant_light_gradient) {
				const float end_left_light = left->lightIntensity + left->dLightIntensityDy * (float)run_rows;
				const float end_right_light =
					right->lightIntensity + right->dLightIntensityDy * (float)run_rows;
				face->spanLightIntensityDx = (end_right_light - end_left_light) / (end_right_x - end_left_x);
			}
			while (run_rows-- > 0) {
				if (!constant_light_gradient) {
					face->spanLightIntensityDx =
						(right->lightIntensity - left->lightIntensity) / (right->x - left->x);
				}
				sw3d_InsertSpan(left->x, right->x, scan_y++, face);
				if (run_rows > 0) {
					left->x += left->dxdy;
					left->lightIntensity += left->dLightIntensityDy;
					right->x += right->dxdy;
					right->lightIntensity += right->dLightIntensityDy;
				}
			}
		}

		if (left->yEnd == right->yEnd) {
			remaining_edges -= 2;
			if (remaining_edges == 0)
				break;
			left->x = left_start_x;
			left->lightIntensity = left_start_light;
			right->x = right_start_x;
			right->lightIntensity = right_start_light;
			left = NULL;
			right = NULL;
			for (int edge_index = 0; edge_index < face->edgeCount; ++edge_index) {
				SceneEdgeTIE98* edge = face->edges[edge_index];
				if (edge->yStart != scan_y)
					continue;
				if (!left)
					left = edge;
				else {
					right = edge;
					break;
				}
			}
			if (!left || !right) {
				face->yBot = scan_y;
				return;
			}
			if (left->x > right->x || (left->x == right->x && left->dxdy > right->dxdy)) {
				SceneEdgeTIE98* swap = left;
				left = right;
				right = swap;
			}
			face->pScanEdge = left;
			left_start_x = left->x;
			right_start_x = right->x;
			left_start_light = left->lightIntensity;
			right_start_light = right->lightIntensity;
		} else {
			--remaining_edges;
			if (scan_y == left->yEnd) {
				left->x = left_start_x;
				left->lightIntensity = left_start_light;
				left = NULL;
				for (int edge_index = 0; edge_index < face->edgeCount; ++edge_index) {
					if (face->edges[edge_index]->yStart == scan_y) {
						left = face->edges[edge_index];
						break;
					}
				}
				if (!left) {
					face->yBot = scan_y;
					return;
				}
				face->pScanEdge = left;
				left_start_x = left->x;
				left_start_light = left->lightIntensity;
				right->x += right->dxdy;
				right->lightIntensity += right->dLightIntensityDy;
			} else {
				right->x = right_start_x;
				right->lightIntensity = right_start_light;
				right = NULL;
				for (int edge_index = 0; edge_index < face->edgeCount; ++edge_index) {
					if (face->edges[edge_index]->yStart == scan_y) {
						right = face->edges[edge_index];
						break;
					}
				}
				if (!right) {
					face->yBot = scan_y;
					return;
				}
				right_start_x = right->x;
				right_start_light = right->lightIntensity;
				left->x += left->dxdy;
				left->lightIntensity += left->dLightIntensityDy;
			}
		}

		if (remaining_edges == 1)
			return;
		if (remaining_edges == 2 && left->yEnd != right->yEnd)
			return;
	}

	left->x = left_start_x;
	left->lightIntensity = left_start_light;
	right->x = right_start_x;
	right->lightIntensity = right_start_light;
}

/* RECOVERY HELPER: removes the repeated span-unlink sequence in
 * sw3d_InsertSpan. */
static SceneSpanTIE98* sw3d_UnlinkSpan(SceneSpanTIE98** link, SceneSpanTIE98* span, int scan_y) {
	span->pFace->pSpans[scan_y - span->pFace->yTop] = NULL;
	*link = span->next;
	return *link;
}

/* RECOVERY HELPER: removes the repeated start-edge adjustment and ordered
 * reinsertion sequence in sw3d_InsertSpan. */
static int sw3d_MoveSpanStart(SceneSpanTIE98** link, SceneSpanTIE98* span, int start_x, int scan_y) {
	span->startLightIntensity += (float)(start_x - span->startX) * span->dLightIntensityDx;
	span->startX = start_x;
	SceneSpanTIE98* next = span->next;
	if (!next || next->startX >= span->startX)
		return 0;

	*link = next;
	if (span->startX < next->endX) {
		span->startLightIntensity += (float)(next->endX - span->startX) * span->dLightIntensityDx;
		span->startX = next->endX;
	}
	SceneSpanTIE98** insert_link = &next->next;
	while (*insert_link && (*insert_link)->startX < span->startX) {
		if (span->startX < (*insert_link)->endX) {
			span->startLightIntensity +=
				(float)((*insert_link)->endX - span->startX) * span->dLightIntensityDx;
			span->startX = (*insert_link)->endX;
		}
		if (span->startX >= span->endX)
			break;
		insert_link = &(*insert_link)->next;
	}
	if (span->startX >= span->endX) {
		span->pFace->pSpans[scan_y - span->pFace->yTop] = NULL;
	} else {
		span->next = *insert_link;
		*insert_link = span;
	}
	return 1;
}

// FUNCTION: TIE98 0x43E2C0
static void sw3d_InsertSpan(float left_x, float right_x, int scan_y, SceneFaceTIE98* face) {
	face->pSpans[scan_y - face->yTop] = NULL;
	int start_x = left_x < 0.0f ? 0 : (int)left_x;
	if (left_x >= 0.0f && (float)start_x != left_x)
		++start_x;
	int end_x = right_x < 0.0f ? 0 : (int)right_x;
	if (right_x >= 0.0f && (float)end_x != right_x)
		++end_x;
	if (end_x > (uint16_t)pixelswide)
		end_x = (uint16_t)pixelswide;
	if (end_x <= start_x || start_x >= (uint16_t)pixelswide || (g_sw3dSkipOddScanlines && (scan_y & 1)))
		return;

	SceneSpanTIE98** link = &g_scanlineSpanHeads[scan_y];
	SceneSpanTIE98* current = *link;
	while (current) {
		if (current->endX <= start_x) {
			link = &current->next;
			current = current->next;
			continue;
		}
		if (current->startX > start_x)
			break;

		SceneFaceTIE98* current_face = current->pFace;
		if (face->maxVertW <= current_face->minVertW) {
			start_x = current->endX;
			if (start_x >= end_x)
				return;
			link = &current->next;
			current = current->next;
			continue;
		}
		if (face->minVertW >= current_face->maxVertW) {
			if (current->endX > end_x) {
				link = &current->next;
				current = current->next;
				continue;
			}
			current->endX = start_x;
			if (current->endX == current->startX)
				current = sw3d_UnlinkSpan(link, current, scan_y);
			else {
				link = &current->next;
				current = current->next;
			}
			continue;
		}

		const float new_depth =
			face->gradients[6] * (float)start_x + face->gradients[7] * (float)scan_y + face->gradients[8];
		const float old_depth = current_face->gradients[6] * (float)start_x +
								current_face->gradients[7] * (float)scan_y + current_face->gradients[8];
		const float new_slope = face->gradients[6];
		const float current_slope = current_face->gradients[6];
		if (new_depth <= old_depth) {
			if (new_slope > current_slope) {
				const int overlap_end = current->endX < end_x ? current->endX : end_x;
				const int overlap_width = overlap_end - start_x;
				const float new_end_depth = new_depth + (float)overlap_width * new_slope;
				const float current_end_depth = old_depth + (float)overlap_width * current_slope;
				if (new_end_depth > current_end_depth) {
					int covered_width = (int)((float)overlap_width - (new_end_depth - current_end_depth) /
																		 (new_slope - current_slope)) +
										1;
					if (covered_width < 0)
						covered_width = 0;
					start_x += covered_width;
				} else if (current->endX >= end_x) {
					return;
				} else {
					start_x = current->endX;
				}
			} else {
				start_x = current->endX;
			}
			if (start_x >= end_x)
				return;
			link = &current->next;
			current = current->next;
			continue;
		}

		if (new_slope >= current_slope) {
			if (current->endX > end_x) {
				link = &current->next;
				current = current->next;
				continue;
			}
			current->endX = start_x;
			if (current->endX == current->startX)
				current = sw3d_UnlinkSpan(link, current, scan_y);
			else {
				link = &current->next;
				current = current->next;
			}
			continue;
		}

		if (current->endX > end_x) {
			const int overlap_width = end_x - start_x;
			const float new_end_depth = new_depth + (float)overlap_width * new_slope;
			const float current_end_depth = old_depth + (float)overlap_width * current_slope;
			if (new_end_depth < current_end_depth) {
				int visible_width = (int)((float)overlap_width -
										  (new_end_depth - current_end_depth) / (new_slope - current_slope)) +
									1;
				if (visible_width < 0)
					visible_width = 0;
				if (visible_width > overlap_width)
					visible_width = overlap_width;
				end_x = start_x + visible_width;
				if (start_x >= end_x)
					return;
			}
			link = &current->next;
			current = current->next;
			continue;
		}

		const int overlap_width = current->endX - start_x;
		const float new_end_depth = new_depth + (float)overlap_width * new_slope;
		const float current_end_depth = old_depth + (float)overlap_width * current_slope;
		if (new_end_depth >= current_end_depth) {
			current->endX = start_x;
			if (current->endX == current->startX)
				current = sw3d_UnlinkSpan(link, current, scan_y);
			else {
				link = &current->next;
				current = current->next;
			}
			continue;
		}

		int crossing_from_right = (int)((new_end_depth - current_end_depth) / (new_slope - current_slope));
		if (crossing_from_right < 0)
			crossing_from_right = 0;
		if (crossing_from_right > overlap_width)
			crossing_from_right = overlap_width;
		const int current_left_width = start_x - current->startX;
		const int new_left_width = overlap_width - crossing_from_right;
		const int new_right_width = end_x - current->endX;

		if (crossing_from_right <= current_left_width && crossing_from_right <= new_left_width &&
			crossing_from_right <= new_right_width) {
			current->endX = start_x;
			if (current->endX == current->startX)
				current = sw3d_UnlinkSpan(link, current, scan_y);
			else {
				link = &current->next;
				current = current->next;
			}
			continue;
		}
		if (crossing_from_right >= current_left_width &&
			(current_left_width > new_left_width || current_left_width > new_right_width)) {
			const int moved_start = current->endX - crossing_from_right;
			if (!sw3d_MoveSpanStart(link, current, moved_start, scan_y))
				break;
			current = *link;
			continue;
		}
		if (new_left_width > current_left_width || new_left_width > crossing_from_right ||
			new_left_width > new_right_width)
			end_x = current->endX - crossing_from_right;
		else
			start_x = current->endX;
		if (start_x >= end_x)
			return;
		link = &current->next;
		current = current->next;
	}
	if (start_x >= end_x)
		return;

	SceneSpanTIE98* span = g_pSceneSpanDataCur++;
	if (g_pSceneSpanDataCur == g_pSceneSpanDataEnd)
		g_pSceneSpanDataCur = g_pSceneSpanDataEnd;
	span->startX = start_x;
	span->endX = end_x;
	span->pFace = face;
	if (face->pScanEdge) {
		span->startLightIntensity = face->pScanEdge->lightIntensity +
									((float)start_x - face->pScanEdge->x) * face->spanLightIntensityDx;
	} else {
		span->startLightIntensity = 0.0f;
	}
	span->dLightIntensityDx = face->spanLightIntensityDx;
	face->pSpans[scan_y - face->yTop] = span;
	*link = span;
	span->next = current;

	link = &span->next;
	current = *link;
	while (current && current->startX < span->endX) {
		SceneFaceTIE98* current_face = current->pFace;
		if (face->maxVertW <= current_face->minVertW) {
			if (current->endX >= span->endX) {
				span->endX = current->startX;
				return;
			}
			link = &current->next;
			current = current->next;
			continue;
		}
		if (face->minVertW >= current_face->maxVertW) {
			if (current->endX <= span->endX) {
				current = sw3d_UnlinkSpan(link, current, scan_y);
				continue;
			}
			if (sw3d_MoveSpanStart(link, current, span->endX, scan_y)) {
				current = *link;
				continue;
			}
			link = &current->next;
			current = current->next;
			continue;
		}

		const int overlap_start = current->startX;
		const float new_depth = face->gradients[6] * (float)overlap_start +
								face->gradients[7] * (float)scan_y + face->gradients[8];
		const float current_depth = current_face->gradients[6] * (float)overlap_start +
									current_face->gradients[7] * (float)scan_y + current_face->gradients[8];
		const float new_slope = face->gradients[6];
		const float current_slope = current_face->gradients[6];
		if (new_depth <= current_depth) {
			if (new_slope <= current_slope) {
				if (current->endX >= span->endX) {
					span->endX = current->startX;
					return;
				}
				link = &current->next;
				current = current->next;
				continue;
			}

			if (current->endX < span->endX) {
				const int overlap_width = current->endX - overlap_start;
				const float new_end_depth = new_depth + (float)overlap_width * new_slope;
				const float current_end_depth = current_depth + (float)overlap_width * current_slope;
				if (new_end_depth <= current_end_depth) {
					link = &current->next;
					current = current->next;
					continue;
				}
				int crossing_from_right =
					(int)((new_end_depth - current_end_depth) / (new_slope - current_slope));
				if (crossing_from_right < 0)
					crossing_from_right = 0;
				current->endX -= crossing_from_right;
				if (current->endX <= current->startX)
					current = sw3d_UnlinkSpan(link, current, scan_y);
				else {
					link = &current->next;
					current = current->next;
				}
				continue;
			}

			const int overlap_width = span->endX - overlap_start;
			const float new_end_depth = new_depth + (float)overlap_width * new_slope;
			const float current_end_depth = current_depth + (float)overlap_width * current_slope;
			if (new_end_depth <= current_end_depth) {
				span->endX = current->startX;
				return;
			}
			int crossing_from_right =
				(int)((new_end_depth - current_end_depth) / (new_slope - current_slope));
			if (crossing_from_right < 0)
				crossing_from_right = 0;
			if (crossing_from_right > overlap_width)
				crossing_from_right = overlap_width;
			const int new_visible_width = overlap_width - crossing_from_right;
			const int current_right_width = current->endX - span->endX;
			if (crossing_from_right < new_visible_width && crossing_from_right < current_right_width) {
				span->endX = current->startX;
				return;
			}
			if (new_visible_width >= current_right_width) {
				current->endX = span->endX - crossing_from_right;
				if (current->endX <= current->startX)
					current = sw3d_UnlinkSpan(link, current, scan_y);
				else {
					link = &current->next;
					current = current->next;
				}
				continue;
			}
			if (sw3d_MoveSpanStart(link, current, span->endX, scan_y)) {
				current = *link;
				continue;
			}
			link = &current->next;
			current = current->next;
			continue;
		}

		if (new_slope >= current_slope) {
			if (current->endX <= span->endX) {
				current = sw3d_UnlinkSpan(link, current, scan_y);
				continue;
			}
			if (sw3d_MoveSpanStart(link, current, span->endX, scan_y)) {
				current = *link;
				continue;
			}
			link = &current->next;
			current = current->next;
			continue;
		}

		if (current->endX > span->endX) {
			const int overlap_width = span->endX - overlap_start;
			const float new_end_depth = new_depth + (float)overlap_width * new_slope;
			const float current_end_depth = current_depth + (float)overlap_width * current_slope;
			int moved_start;
			if (new_end_depth >= current_end_depth) {
				moved_start = span->endX;
			} else {
				int visible_width = (int)((float)overlap_width -
										  (new_end_depth - current_end_depth) / (new_slope - current_slope));
				if (visible_width < 0)
					visible_width = 0;
				if (visible_width > overlap_width)
					visible_width = overlap_width;
				moved_start = current->startX + visible_width;
			}
			if (sw3d_MoveSpanStart(link, current, moved_start, scan_y)) {
				current = *link;
				continue;
			}
			link = &current->next;
			current = current->next;
			continue;
		}

		const int overlap_width = current->endX - overlap_start;
		const float new_end_depth = new_depth + (float)overlap_width * new_slope;
		const float current_end_depth = current_depth + (float)overlap_width * current_slope;
		if (new_end_depth >= current_end_depth) {
			current = sw3d_UnlinkSpan(link, current, scan_y);
			continue;
		}
		int crossing_from_right = (int)((new_end_depth - current_end_depth) / (new_slope - current_slope));
		if (crossing_from_right < 0)
			crossing_from_right = 0;
		int moved_width = current->endX - crossing_from_right - current->startX;
		if (moved_width < 0)
			moved_width = 0;
		const int moved_start = current->startX + moved_width;
		if (moved_start >= current->endX) {
			current = sw3d_UnlinkSpan(link, current, scan_y);
			continue;
		}
		if (sw3d_MoveSpanStart(link, current, moved_start, scan_y)) {
			current = *link;
			continue;
		}
		link = &current->next;
		current = current->next;
	}
}

// FUNCTION: TIE98 0x431F60
static void sw3d_RasterizeMeshFaces(SceneMeshTIE98* mesh) {
	ProjVertexTIE98* vertices = &g_projVertList[mesh->vertBaseIndex];
	SceneFaceTIE98* face = &g_visFaceList[mesh->faceBaseIndex];
	mesh->edgeBaseIndex = g_sceneEdgeCursor;
	mesh->clippedEdgeCount = 0;
	SceneEdgeTIE98* first_edge = &g_sceneEdgeList[g_sceneEdgeCursor];
	SceneEdgeTIE98* output_edge = first_edge;
	for (int edge_index = 0; edge_index < mesh->edgeCount; ++edge_index)
		g_sceneEdgeFlags[edge_index] = -1;

	for (int face_index = 0; face_index < mesh->visFaceCount; ++face_index, ++face) {
		const FaceRecordTIE98* record = &mesh->pFaceGeom[face->faceIndex];
		const int corner_count = record->edgeIdx[3] != -1 ? 4 : 3;
		int output_count = 0;
		if (face->nearClipState == -1) {
			face->nearClipState = (uint16_t)pixelsdeep;
			g_sw3dClipTop = NULL;
			g_sw3dClipBottom = NULL;
			int current_corner = 0;
			for (int previous_corner = corner_count - 1; previous_corner >= 0; --previous_corner) {
				const int source_edge = record->edgeIdx[previous_corner];
				g_sw3dGeneratedClipVertex = NULL;
				const int existing_edge = g_sceneEdgeFlags[source_edge];
				if (existing_edge == -1) {
					if (sw3d_SetupClippedEdge(
							mesh, output_edge, &vertices[g_vertexRemap[record->vertexIdx[previous_corner]]],
							&vertices[g_vertexRemap[record->vertexIdx[current_corner]]]) < 0) {
						if (!g_sw3dGeneratedClipVertex)
							g_sceneEdgeFlags[source_edge] = -2;
					} else {
						face->edges[output_count++] = output_edge;
						output_edge->pClipVert = g_sw3dGeneratedClipVertex;
						g_sceneEdgeFlags[source_edge] = mesh->clippedEdgeCount++;
						++output_edge;
					}
				} else if (existing_edge != -2) {
					SceneEdgeTIE98* edge = &first_edge[existing_edge];
					face->edges[output_count++] = edge;
					if (edge->pClipVert) {
						g_sw3dClipBottom = g_sw3dClipTop;
						g_sw3dClipTop = edge->pClipVert;
					}
				}
				current_corner = previous_corner;
			}
			if (g_sw3dClipBottom &&
				sw3d_SetupClippedEdge(mesh, output_edge, g_sw3dClipTop, g_sw3dClipBottom) >= 0) {
				face->edges[output_count++] = output_edge++;
				++mesh->clippedEdgeCount;
			}
		} else {
			face->nearClipState = (uint16_t)pixelsdeep;
			int current_corner = 0;
			for (int previous_corner = corner_count - 1; previous_corner >= 0; --previous_corner) {
				const int source_edge = record->edgeIdx[previous_corner];
				const int existing_edge = g_sceneEdgeFlags[source_edge];
				if (existing_edge == -1) {
					if (sw3d_SetupEdge(output_edge,
									   &vertices[g_vertexRemap[record->vertexIdx[previous_corner]]],
									   &vertices[g_vertexRemap[record->vertexIdx[current_corner]]]) < 0) {
						g_sceneEdgeFlags[source_edge] = -2;
					} else {
						face->edges[output_count++] = output_edge;
						g_sceneEdgeFlags[source_edge] = mesh->clippedEdgeCount++;
						++output_edge;
					}
				} else if (existing_edge != -2) {
					face->edges[output_count++] = &first_edge[existing_edge];
				}
				current_corner = previous_corner;
			}
		}

		face->edgeCount = output_count;
		if (output_count != 0) {
			sw3d_ScanConvertFace(face);
		} else {
			face->yTop = 0;
			face->yBot = 0;
		}
	}
	g_sceneEdgeCursor += mesh->clippedEdgeCount;
}

// FUNCTION: TIE98 0x432E20
static void RenderScene_DrawSceneMesh(SceneMeshTIE98* mesh) {
	if (g_useHardware3D) {
		RenderScene_DrawMesh(mesh);
	} else {
		g_projVertCount = 0;
		g_sceneEdgeCursor = 0;
		if (g_meshQueueIndex != TIE98_MESH_QUEUE_MAX &&
			g_visFaceCount + mesh->faceCount <= TIE98_SCENE_FACE_MAX &&
			mesh->vertexCount <= g_projVertCapacity && mesh->edgeCount <= g_sceneEdgeCapacity) {
			g_meshQueue[g_meshQueueIndex] = *mesh;
			SceneMeshTIE98* queued = &g_meshQueue[g_meshQueueIndex];
			RenderScene_CullMeshFacesFromView(queued);
			if (queued->visFaceCount != 0) {
				if (g_bBackdropMeshMode)
					sw3d_ProjectMeshVerticesDistant(queued);
				else
					sw3d_ProjectMeshVertices(queued);
				sw3d_RasterizeMeshFaces(queued);
				++g_meshQueueIndex;
			}
		}
	}
}

/* RECOVERY HELPER: source-shaped form of the eight repeated TL-quad emission
 * blocks in TIE98 Hud_DrawBoxOverlayHW. */
static int16_t Hud_EmitBoxOverlayQuadHW(float left, float top, float right, float bottom, float depth,
										uint32_t color, int vertical) {
	const int base_vertex = g_d3dVertexCount;
	D3DTLVERTEX* vertices = &g_flightVertexBuffer[base_vertex];
	vertices[0].sx = g_flightVpOriginX + left;
	vertices[0].sy = g_flightVpOriginY + top;
	if (vertical) {
		vertices[1].sx = g_flightVpOriginX + left;
		vertices[1].sy = g_flightVpOriginY + bottom;
		vertices[2].sx = g_flightVpOriginX + right;
		vertices[2].sy = g_flightVpOriginY + bottom;
		vertices[3].sx = g_flightVpOriginX + right;
		vertices[3].sy = g_flightVpOriginY + top;
	} else {
		vertices[1].sx = g_flightVpOriginX + right;
		vertices[1].sy = g_flightVpOriginY + top;
		vertices[2].sx = g_flightVpOriginX + right;
		vertices[2].sy = g_flightVpOriginY + bottom;
		vertices[3].sx = g_flightVpOriginX + left;
		vertices[3].sy = g_flightVpOriginY + bottom;
	}
	for (int i = 0; i < 4; ++i) {
		vertices[i].sz = depth;
		vertices[i].rhw = depth;
		vertices[i].color = color;
		vertices[i].specular = 0;
		vertices[i].tu = 0.0f;
		vertices[i].tv = 0.0f;
	}
	Std3DRenderTri* triangles = &g_triBuffer[g_d3dIndexCount];
	triangles[0].v0 = base_vertex;
	triangles[0].v1 = base_vertex + 1;
	triangles[0].v2 = base_vertex + 2;
	triangles[0].flags = (Std3DRenderStateFlags)38912;
	triangles[0].texture = NULL;
	triangles[1].v0 = base_vertex;
	triangles[1].v1 = base_vertex + 2;
	triangles[1].v2 = base_vertex + 3;
	triangles[1].flags = (Std3DRenderStateFlags)38912;
	triangles[1].texture = NULL;
	g_d3dIndexCount += 2;
	g_d3dVertexCount += 4;
	return (int16_t)g_d3dVertexCount;
}

// FUNCTION: TIE98 0x453B90
static void FlightMap_DrawObjectBoxSpan(int start_x, int end_x, int y, uint8_t color_index) {
	uint8_t* row = vgapointer + (size_t)g_surfacePitch * (displaycorner_lines + (uint32_t)y);
	start_x += (int)displaycorner_columns;
	end_x += (int)displaycorner_columns;
	if (g_flight16bppBytesPerPixel == 2) {
		uint16_t* pixels = (uint16_t*)row;
		const uint16_t color = g_flightTextPalette[color_index];
		for (int x = start_x; x < end_x; ++x)
			pixels[x] = color;
	} else {
		memset(row + start_x, color_index, (size_t)(end_x - start_x));
	}
}

// FUNCTION: TIE98 0x453C40
void FlightMap_DrawObjectBoxCorners(int x, int y, int width, int height, uint8_t color_index) {
	const int right = x + width;
	const int bottom = y + height;
	if (bottom <= 0 || right <= 0 || x >= pixelswide || y >= pixelsdeep || height <= 0 || width <= 0)
		return;

	int corner_width = width >> 3;
	int corner_height = height >> 3;
	if (corner_width < 3)
		corner_width = 3;
	if (corner_height < 3)
		corner_height = 3;
	if (corner_width > width)
		corner_width = width;
	if (corner_height > height)
		corner_height = height;

	FlightSurface_Lock();

	if (y >= 0) {
		int start = x;
		int end = x + corner_width;
		if (end > 0 && x < pixelswide) {
			if (start < 0)
				start = 0;
			if (end > pixelswide)
				end = pixelswide;
			FlightMap_DrawObjectBoxSpan(start, end, y, color_index);
		}
		start = right - corner_width;
		end = right;
		if (end > 0 && start < pixelswide) {
			if (start < 0)
				start = 0;
			if (end > pixelswide)
				end = pixelswide;
			FlightMap_DrawObjectBoxSpan(start, end, y, color_index);
		}
	}

	if (bottom <= pixelsdeep) {
		int start = x;
		int end = x + corner_width;
		if (end > 0 && x < pixelswide) {
			if (start < 0)
				start = 0;
			if (end > pixelswide)
				end = pixelswide;
			FlightMap_DrawObjectBoxSpan(start, end, bottom - 1, color_index);
		}
		start = right - corner_width;
		end = right;
		if (end > 0 && start < pixelswide) {
			if (start < 0)
				start = 0;
			if (end > pixelswide)
				end = pixelswide;
			FlightMap_DrawObjectBoxSpan(start, end, bottom - 1, color_index);
		}
	}

	for (int row = 1; row < corner_height; ++row) {
		const int screen_y = y + row;
		if (screen_y < 0 || screen_y >= pixelsdeep)
			continue;
		if (x >= 0)
			FlightMap_DrawObjectBoxSpan(x, x + 1, screen_y, color_index);
		if (right <= pixelswide)
			FlightMap_DrawObjectBoxSpan(right - 1, right, screen_y, color_index);
	}
	for (int row = height - corner_height; row < height - 1; ++row) {
		const int screen_y = y + row;
		if (row < corner_height || screen_y < 0 || screen_y >= pixelsdeep)
			continue;
		if (x >= 0)
			FlightMap_DrawObjectBoxSpan(x, x + 1, screen_y, color_index);
		if (right <= pixelswide)
			FlightMap_DrawObjectBoxSpan(right - 1, right, screen_y, color_index);
	}

	FlightSurface_Unlock();
}

// FUNCTION: TIE98 0x42C190
int16_t Hud_DrawBoxOverlayHW(int x, int y, int width, int height, int color_index, int depth) {
	if (depth == 1 && width == 4 && height == 4) {
		int start = x;
		int end = x + 4;
		if (start < 0)
			start = 0;
		if (end > pixelswide)
			end = pixelswide;
		if (start < end) {
			FlightSurface_Lock();
			for (int row = 0; row < 4; ++row) {
				const int screen_y = y + row;
				if (screen_y >= 0 && screen_y < pixelsdeep)
					FlightMap_DrawObjectBoxSpan(start, end, screen_y, color_index);
			}
			FlightSurface_Unlock();
		}
		return 0;
	}

	if (g_d3dVertexCount + 32 > g_maxBatchVerts || g_d3dIndexCount + 16 > g_maxBatchTris) {
		Math_SetFpuExtendedPrecisionMode();
		if (!g_powerVrSceneWorkaround)
			std3D_StartScene();
		std3D_LockExecuteBuffer();
		std3D_AddVertices(g_flightVertexBuffer, g_d3dVertexCount);
		std3D_BeginInstructions();
		std3D_AddTriangles(g_triBuffer, (unsigned int)g_d3dIndexCount);
		std3D_ExecuteBuffer();
		if (!g_powerVrSceneWorkaround)
			std3D_EndScene();
		Math_SetFpuSinglePrecisionMode();
		g_d3dIndexCount = 0;
		g_d3dVertexCount = 0;
	}

	const int right = x + width;
	const int bottom = y + height;
	int corner_width = width >> 3;
	int corner_height = height >> 3;
	if (corner_width < 3)
		corner_width = 3;
	if (corner_height < 3)
		corner_height = 3;
	if (corner_width > width)
		corner_width = width;
	if (corner_height > height)
		corner_height = height;
	const uint8_t* rgb = &rtsvga2_vgapalette[3 * (uint8_t)color_index];
	const uint32_t color =
		0xff000000u | ((uint32_t)rgb[0] << 18) | ((uint32_t)rgb[1] << 10) | ((uint32_t)rgb[2] << 2);
	if (depth < 1)
		depth = 1;
	float screen_depth = 1.0f / ((float)depth * (1.0f / 2048.0f) + 1.0f);
	if (g_std3DZBufferBitDepth == 2)
		screen_depth = 1.0f - screen_depth;

	int16_t result = (int16_t)g_d3dVertexCount;
	if (y >= 0 && y < pixelsdeep) {
		int start = x < 0 ? 0 : x;
		int end = x + corner_width;
		if (end >= pixelswide)
			end = pixelswide - 1;
		if (start < end)
			result = Hud_EmitBoxOverlayQuadHW(start, y, end, y + 1, screen_depth, color, 0);
		start = right - corner_width;
		end = right;
		if (start < 0)
			start = 0;
		if (end >= pixelswide)
			end = pixelswide - 1;
		if (start < end)
			result = Hud_EmitBoxOverlayQuadHW(start, y, end, y + 1, screen_depth, color, 0);
	}
	if (bottom >= 0 && bottom < pixelsdeep) {
		int start = x < 0 ? 0 : x;
		int end = x + corner_width;
		if (end >= pixelswide)
			end = pixelswide - 1;
		if (start < end)
			result = Hud_EmitBoxOverlayQuadHW(start, bottom, end, bottom + 1, screen_depth, color, 0);
		start = right - corner_width;
		end = right + 1;
		if (start < 0)
			start = 0;
		if (end >= pixelswide)
			end = pixelswide - 1;
		if (start < end)
			result = Hud_EmitBoxOverlayQuadHW(start, bottom, end, bottom + 1, screen_depth, color, 0);
	}
	if (x >= 0 && x < pixelswide) {
		int start = y < 0 ? 0 : y;
		int end = y + corner_height;
		if (end >= pixelsdeep)
			end = pixelsdeep - 1;
		if (start < end)
			result = Hud_EmitBoxOverlayQuadHW(x, start, x + 1, end, screen_depth, color, 1);
		start = bottom - corner_height;
		end = bottom;
		if (start < 0)
			start = 0;
		if (end >= pixelsdeep)
			end = pixelsdeep - 1;
		if (start < end)
			result = Hud_EmitBoxOverlayQuadHW(x, start, x + 1, end, screen_depth, color, 1);
	}
	if (right >= 0 && right < pixelswide) {
		int start = y < 0 ? 0 : y;
		int end = y + corner_height;
		if (end >= pixelsdeep)
			end = pixelsdeep - 1;
		if (start < end)
			result = Hud_EmitBoxOverlayQuadHW(right, start, right + 1, end, screen_depth, color, 1);
		start = bottom - corner_height;
		end = bottom;
		if (start < 0)
			start = 0;
		if (end >= pixelsdeep)
			end = pixelsdeep - 1;
		if (start < end)
			result = Hud_EmitBoxOverlayQuadHW(right, start, right + 1, end, screen_depth, color, 1);
	}
	return result;
}

// FUNCTION: TIE98 0x44A4D0
static void sw3d_CopySpanPixels(const uint8_t* source_base, int start_x, int count) {
	if (count <= 0)
		return;
	const int bytes_per_pixel = g_flight16bppBytesPerPixel;
	const uint8_t* source = source_base + bytes_per_pixel * start_x;
	uint8_t* destination = xtrans2_videobaseptr + g_sw3dScanlineByteOffset + bytes_per_pixel * start_x;
	if (bytes_per_pixel == 2) {
		do {
			destination[0] = source[0];
			destination[1] = source[1];
			source += 2;
			destination += 2;
		} while (--count != 0);
	} else {
		do {
			*destination++ = *source++;
		} while (--count != 0);
	}
}

// FUNCTION: TIE98 0x44A090
void sw3d_BlitOccludedSpan(const uint8_t* source, int start_x, int end_x, int scan_y, float depth) {
	const int bytes_per_pixel = g_flight16bppBytesPerPixel;
	const uint8_t* source_base = source - bytes_per_pixel * start_x;
	int draw_x = start_x;
	g_sw3dScanlineByteOffset =
		bytes_per_pixel * displaycorner_columns + (int)g_surfacePitch * (scan_y + displaycorner_lines);

	SceneSpanTIE98* span = g_scanlineSpanHeads[scan_y];
	while (span) {
		const int span_end = span->endX;
		if (span_end <= draw_x) {
			span = span->next;
			continue;
		}
		if (span->startX > draw_x)
			break;

		SceneFaceTIE98* face = span->pFace;
		if (depth <= face->minVertW) {
			draw_x = span_end;
			start_x = draw_x;
			if (span_end >= end_x)
				return;
		} else if (depth < face->maxVertW) {
			float span_depth = (float)scan_y * face->gradients[7] + face->gradients[8];
			span_depth = (float)start_x * face->gradients[6] + span_depth;
			if (depth <= span_depth) {
				if (face->gradients[6] >= 0.0f) {
					draw_x = span_end;
					start_x = draw_x;
					if (span_end >= end_x)
						return;
				} else {
					int limit_x = span_end;
					if (limit_x >= end_x)
						limit_x = end_x;
					const int delta_x = limit_x - draw_x;
					const float limit_depth = (float)delta_x * face->gradients[6] + span_depth;
					if (depth <= limit_depth) {
						if (span_end >= end_x)
							return;
						draw_x = span_end;
						start_x = draw_x;
					} else {
						draw_x += (int)((float)delta_x - (depth - limit_depth) / -face->gradients[6]);
						start_x = draw_x;
						if (draw_x >= end_x)
							return;
					}
				}
			} else if (face->gradients[6] > 0.0f) {
				const int limit_x = span_end > end_x ? end_x : span_end;
				const int delta_x = limit_x - draw_x;
				const float limit_depth = (float)delta_x * face->gradients[6] + span_depth;
				if (depth < limit_depth) {
					end_x = draw_x + (int)((float)delta_x - (depth - limit_depth) / -face->gradients[6]);
					if (draw_x >= end_x)
						return;
				}
			}
		}
		span = span->next;
	}

	while (span) {
		const int span_start = span->startX;
		if (span_start >= end_x)
			break;
		SceneFaceTIE98* face = span->pFace;
		if (depth <= face->minVertW) {
			sw3d_CopySpanPixels(source_base, draw_x, span_start - draw_x);
			draw_x = span->endX;
			start_x = draw_x;
			if (draw_x >= end_x)
				return;
		} else if (depth < face->maxVertW) {
			float span_depth = (float)scan_y * face->gradients[7] + face->gradients[8];
			span_depth = (float)span_start * face->gradients[6] + span_depth;
			if (depth <= span_depth) {
				sw3d_CopySpanPixels(source_base, draw_x, span_start - draw_x);
				draw_x = span->endX;
				if (face->gradients[6] >= 0.0f) {
					start_x = draw_x;
					if (draw_x >= end_x)
						return;
				} else if (draw_x >= end_x) {
					const float limit_depth = (float)(end_x - span_start) * face->gradients[6] + span_depth;
					if (depth <= limit_depth)
						return;
					draw_x = end_x - (int)((depth - limit_depth) / -face->gradients[6]);
					start_x = draw_x;
					if (draw_x >= end_x)
						return;
				} else {
					const float limit_depth = (float)(draw_x - span_start) * face->gradients[6] + span_depth;
					if (depth > limit_depth)
						draw_x -= (int)((depth - limit_depth) / -face->gradients[6]);
					start_x = draw_x;
				}
			} else if (face->gradients[6] > 0.0f) {
				const int span_end = span->endX;
				if (span_end >= end_x) {
					const float limit_depth = (float)(end_x - span_start) * face->gradients[6] + span_depth;
					if (depth < limit_depth) {
						const int count =
							span_start +
							(int)((float)(end_x - span_start) - (depth - limit_depth) / -face->gradients[6]) -
							draw_x;
						sw3d_CopySpanPixels(source_base, draw_x, count);
						return;
					}
				} else {
					const float limit_depth =
						(float)(span_end - span_start) * face->gradients[6] + span_depth;
					if (depth < limit_depth) {
						const int count =
							span_end - (int)((depth - limit_depth) / -face->gradients[6]) - draw_x;
						sw3d_CopySpanPixels(source_base, draw_x, count);
						draw_x = span_end;
						start_x = draw_x;
					}
				}
			}
		}
		span = span->next;
	}
	sw3d_CopySpanPixels(source_base, draw_x, end_x - draw_x);
}

// FUNCTION: TIE98 0x44A550
int16_t Hud_DrawBoxInXTrans(int x, int y, int width, int height, int color_index, int depth) {
	const int bottom = y + height;
	if (bottom <= 0)
		return 0;
	const int right = x + width;
	if (right <= 0 || x >= pixelswide || y >= pixelsdeep || height <= 0 || width <= 0)
		return 0;
	if (g_useHardware3D)
		return Hud_DrawBoxOverlayHW(x, y, width, height, color_index, depth);

	int corner_width = width >> 3;
	int corner_height = height >> 3;
	if (corner_width < 3)
		corner_width = 3;
	if (corner_height < 3)
		corner_height = 3;
	if (corner_width > width)
		corner_width = width;
	if (corner_height > height)
		corner_height = height;

	uint8_t* span = g_panelBoxSpanScratch;
	if (x > 0)
		span += g_flight16bppBytesPerPixel * x;
	if (g_flight16bppBytesPerPixel == 2) {
		const uint16_t color = g_flightTextPalette[(uint8_t)color_index];
		for (int i = 0; i < corner_width; ++i)
			((uint16_t*)span)[i] = color;
	} else {
		memset(span, (uint8_t)color_index, (size_t)corner_width);
	}
	if (depth < 1)
		depth = 1;
	const float span_depth = (float)(uint32_t)perspFactor / (float)depth;
	if (!g_flightSurfaceAlreadyLocked)
		FlightSurface_Lock();

	if (y >= 0) {
		int span_start = x;
		int span_end = x + corner_width;
		if (span_end > 0 && x < pixelswide) {
			if (span_start < 0)
				span_start = 0;
			if (span_end > pixelswide)
				span_end = pixelswide;
			sw3d_BlitOccludedSpan(span, span_start, span_end, y, span_depth);
		}
		span_end = right;
		span_start = span_end - corner_width;
		if (span_end > 0 && span_start < pixelswide) {
			if (span_start < 0)
				span_start = 0;
			if (span_end > pixelswide)
				span_end = pixelswide;
			sw3d_BlitOccludedSpan(span, span_start, span_end, y, span_depth);
		}
	}

	if (bottom <= pixelsdeep) {
		int span_start = x;
		int span_end = x + corner_width;
		if (span_end > 0 && x < pixelswide) {
			if (span_start < 0)
				span_start = 0;
			if (span_end > pixelswide)
				span_end = pixelswide;
			sw3d_BlitOccludedSpan(span, span_start, span_end, bottom - 1, span_depth);
		}
		span_end = right;
		span_start = span_end - corner_width;
		if (span_end > 0 && span_start < pixelswide) {
			if (span_start < 0)
				span_start = 0;
			if (span_end > pixelswide)
				span_end = pixelswide;
			sw3d_BlitOccludedSpan(span, span_start, span_end, bottom - 1, span_depth);
		}
	}

	if (corner_height > 1) {
		int scan_y = y + 1;
		int remaining = corner_height - 1;
		do {
			if (scan_y >= 0 && scan_y < pixelsdeep) {
				if (x >= 0)
					sw3d_BlitOccludedSpan(span, x, x + 1, scan_y, span_depth);
				if (right <= pixelswide)
					sw3d_BlitOccludedSpan(span, right - 1, right, scan_y, span_depth);
			}
			++scan_y;
		} while (--remaining != 0);
	}

	int row_offset = height - corner_height;
	if (row_offset < height - 1) {
		int scan_y = row_offset + y;
		do {
			if (row_offset >= corner_height && scan_y >= 0 && scan_y < pixelsdeep) {
				if (x >= 0)
					sw3d_BlitOccludedSpan(span, x, x + 1, scan_y, span_depth);
				if (right <= pixelswide)
					sw3d_BlitOccludedSpan(span, right - 1, right, scan_y, span_depth);
			}
			++row_offset;
			++scan_y;
		} while (row_offset < height - 1);
	}

	if (!g_flightSurfaceAlreadyLocked)
		FlightSurface_Unlock();
	return (int16_t)g_flightSurfaceAlreadyLocked;
}

// FUNCTION: TIE98 0x42B980
void RenderQuad_DrawRotatedSprite(int angle, int screen_x, int screen_y, uint16_t screen_scale,
								  const uint8_t* texture_level) {
	static const uint32_t explosion_colors[32] = {
		0xffffffff, 0xe0ffffff, 0xf0ffffff, 0xf0ffffff, 0xe0ffffff, 0xd0ffffff, 0xb0ffffff, 0x90ffffff,
		0x70ffffff, 0x50ffffff, 0x30ffffff, 0x30ffffff, 0x30ffffff, 0x30ffffff, 0x30ffffff, 0x30ffffff,
		0x30ffffff, 0x30ffffff, 0x30ffffff, 0x30ffffff, 0x30ffffff, 0x30ffffff, 0x30ffffff, 0x30ffffff,
		0x30ffffff, 0x30ffffff, 0x30ffffff, 0x30ffffff, 0x30ffffff, 0x30ffffff, 0x30ffffff, 0x30ffffff,
	};
	uint32_t color = 0xffffffff;
	if (parentobject < NUM_OBJECTS && objects[parentobject].genus == GENUS_EXPLOSION)
		color = explosion_colors[objects[parentobject].anim_frame & 0x1f];

	float depth;
	if ((uint32_t)objecteyez <= 0x1000000) {
		depth = 1.0f / ((float)objecteyez * (1.0f / 2048.0f) + 1.0f);
		if (g_std3DZBufferBitDepth == 2)
			depth = 1.0f - depth;
	} else {
		color = 0xffffffff;
		depth = 0.00012205541f;
		if (g_std3DZBufferBitDepth == 2)
			depth = 0.99987793f;
	}

	const int source_width = *(const int32_t*)(texture_level + 16);
	const int source_height = *(const int32_t*)(texture_level + 20);
	int width = 2;
	while (width < source_width && width < 256)
		width *= 2;
	int height = 2;
	while (height < source_height && height < 256)
		height *= 2;
	if (g_pStd3DCurDevice->caps.minTextureWidth) {
		if (width > height)
			height = width;
		else if (height > width)
			width = height;
	}

	const float u_max = (float)source_width / (float)width;
	const float v_max = (float)source_height / (float)height;
	const int half_height = (screen_scale * source_height) >> 9;
	const int half_width = (screen_scale * source_width) >> 9;
	const int screen_y_flipped = pixelsdeep - screen_y;
	ProjVertexTIE98 vertices[32] = { 0 };
	vertices[0].sx = (float)(screen_x + trig2_cosinedwordmult(half_width, angle) +
							 trig2_sinedwordmult(half_height, angle));
	vertices[0].sy = (float)(screen_y_flipped + trig2_cosinedwordmult(half_height, angle) -
							 trig2_sinedwordmult(half_width, angle));
	vertices[0].w = depth;
	vertices[1].sx = (float)(screen_x + trig2_cosinedwordmult(-half_width, angle) +
							 trig2_sinedwordmult(half_height, angle));
	vertices[1].sy = (float)(screen_y_flipped + trig2_cosinedwordmult(half_height, angle) -
							 trig2_sinedwordmult(-half_width, angle));
	vertices[1].w = depth;
	vertices[1].tu = u_max;
	vertices[2].sx = (float)(screen_x + trig2_cosinedwordmult(-half_width, angle) +
							 trig2_sinedwordmult(-half_height, angle));
	vertices[2].sy = (float)(screen_y_flipped + trig2_cosinedwordmult(-half_height, angle) -
							 trig2_sinedwordmult(-half_width, angle));
	vertices[2].w = depth;
	vertices[2].tu = u_max;
	vertices[2].tv = v_max;
	vertices[3].sx = (float)(screen_x + trig2_cosinedwordmult(half_width, angle) +
							 trig2_sinedwordmult(-half_height, angle));
	vertices[3].sy = (float)(screen_y_flipped + trig2_cosinedwordmult(-half_height, angle) -
							 trig2_sinedwordmult(half_width, angle));
	vertices[3].w = depth;
	vertices[3].tv = v_max;

	g_clipCountA = 4;
	g_clipVertCursor = 4;
	for (int i = 0; i < 4; ++i)
		g_clipIdxA[i] = i;
	g_clipCountB = 0;
	int previous = g_clipIdxA[g_clipCountA - 1];
	for (int i = 0; i < g_clipCountA; ++i) {
		const int current = g_clipIdxA[i];
		RenderClip_ClipPolyTop(previous, current, vertices);
		previous = current;
	}
	g_clipCountA = 0;
	if (g_clipCountB > 0) {
		previous = g_clipIdxB[g_clipCountB - 1];
		const int count = g_clipCountB;
		for (int i = 0; i < count; ++i) {
			const int current = g_clipIdxB[i];
			RenderClip_ClipPolyBottom(previous, current, vertices);
			previous = current;
		}
	}
	g_clipCountB = 0;
	if (g_clipCountA > 0) {
		previous = g_clipIdxA[g_clipCountA - 1];
		const int count = g_clipCountA;
		for (int i = 0; i < count; ++i) {
			const int current = g_clipIdxA[i];
			RenderClip_ClipPolyLeft(previous, current, vertices);
			previous = current;
		}
	}
	g_clipCountA = 0;
	if (g_clipCountB > 0) {
		previous = g_clipIdxB[g_clipCountB - 1];
		const int count = g_clipCountB;
		for (int i = 0; i < count; ++i) {
			const int current = g_clipIdxB[i];
			RenderClip_ClipPolyRight(previous, current, vertices);
			previous = current;
		}
	}
	if (g_clipCountA < 3)
		return;

	if (g_clipCountA + g_d3dVertexCount > g_maxBatchVerts ||
		g_clipCountA + g_d3dIndexCount > g_maxBatchTris) {
		Math_SetFpuExtendedPrecisionMode();
		if (!g_powerVrSceneWorkaround)
			std3D_StartScene();
		std3D_LockExecuteBuffer();
		std3D_AddVertices(g_flightVertexBuffer, g_d3dVertexCount);
		std3D_BeginInstructions();
		std3D_AddTriangles(g_triBuffer, (unsigned int)g_d3dIndexCount);
		std3D_ExecuteBuffer();
		if (!g_powerVrSceneWorkaround)
			std3D_EndScene();
		Math_SetFpuSinglePrecisionMode();
		g_d3dIndexCount = 0;
		g_d3dVertexCount = 0;
	}
	if (g_capVertexAlpha) {
		color = 0xfeffffff;
		g_capVertexAlpha = 0;
	}
	for (int i = 0; i < g_clipCountA; ++i) {
		const ProjVertexTIE98* source = &vertices[g_clipIdxA[i]];
		D3DTLVERTEX* destination = &g_flightVertexBuffer[g_d3dVertexCount];
		destination->sx = source->sx + g_flightVpOriginX;
		destination->sy = source->sy + g_flightVpOriginY;
		destination->sz = source->w;
		destination->rhw = source->w;
		destination->color = color;
		destination->specular = 0;
		destination->tu = source->tu;
		destination->tv = source->tv;
		g_clipIdxA[i] = g_d3dVertexCount++;
	}
	const uint32_t pixels_offset = *(const uint32_t*)(texture_level + 8);
	const uint32_t palette_offset = *(const uint32_t*)(texture_level + 12);
	const int rle_format = *(const int32_t*)(texture_level + 32);
	Std3DTextureSurface* texture =
		RenderTexture_GetOrCreateBitmap(width, height, (uint16_t*)(texture_level + palette_offset),
										texture_level + pixels_offset + 16, rle_format);
	for (int i = 2; i < g_clipCountA; ++i) {
		Std3DRenderTri* triangle = &g_triBuffer[g_d3dIndexCount++];
		triangle->v0 = g_clipIdxA[0];
		triangle->v1 = g_clipIdxA[i - 1];
		triangle->v2 = g_clipIdxA[i];
		triangle->texture = texture;
		triangle->flags = (Std3DRenderStateFlags)(2066 + (g_bilinearEnabled ? 384 : 0) + 512);
	}
}

// FUNCTION: TIE98 0x43AEC0
static const Tie98OptNode* OptModel_FindNodeByName(const Tie98OptNode* node, const char* name) {
	if (!node)
		return NULL;
	if (node->name && strcasecmp(node->name, name) == 0)
		return node;
	for (int i = 0; i < node->child_count; ++i) {
		const Tie98OptNode* result = OptModel_FindNodeByName(node->children[i], name);
		if (result)
			return result;
	}
	return NULL;
}

// FUNCTION: TIE98 0x43AE80
static const Tie98OptNode* OptModel_FindNodeByNameInModel(const Tie98OptimizedPolyObject* model,
														  const char* name) {
	for (int i = 0; i < model->root_node_count; ++i) {
		const Tie98OptNode* result = OptModel_FindNodeByName(model->root_nodes[i], name);
		if (result)
			return result;
	}
	return NULL;
}

// FUNCTION: TIE98 0x435180
static int FlightModel_TestLightSegmentAgainstFaces(SceneMeshTIE98* mesh, const Vec3f* segment_start,
													const Vec3f* segment_end) {
	for (int face_index = 0; face_index < mesh->faceCount; ++face_index) {
		const FaceRecordTIE98* face = &mesh->pFaceGeom[face_index];
		const int vertex_count = face->vertexIdx[3] == -1 ? 3 : 4;
		float vertices[4][3];
		for (int corner = 0; corner < vertex_count; ++corner) {
			const Vec3f* vertex = &mesh->pModelVerts[face->vertexIdx[corner]];
			vertices[corner][0] = vertex->x;
			vertices[corner][1] = vertex->y;
			vertices[corner][2] = vertex->z;
		}

		const float start[3] = { segment_start->x, segment_start->y, segment_start->z };
		const float end[3] = { segment_end->x, segment_end->y, segment_end->z };
		int separated = 0;
		for (int axis = 0; axis < 3; ++axis) {
			int all_above = 1;
			int all_below = 1;
			for (int corner = 0; corner < vertex_count; ++corner) {
				if (vertices[corner][axis] < start[axis] || vertices[corner][axis] < end[axis])
					all_above = 0;
				if (vertices[corner][axis] > start[axis] || vertices[corner][axis] > end[axis])
					all_below = 0;
			}
			if (all_above || all_below) {
				separated = 1;
				break;
			}
		}
		if (separated)
			continue;

		const Vec3f* normal = &mesh->pFaceNormals[face_index];
		const float start_distance = (start[0] - vertices[0][0]) * normal->x +
									 (start[1] - vertices[0][1]) * normal->y +
									 (start[2] - vertices[0][2]) * normal->z;
		const float direction_dot = (end[0] - start[0]) * normal->x + (end[1] - start[1]) * normal->y +
									(end[2] - start[2]) * normal->z;
		if (start_distance < 0.0f) {
			if (start_distance > -40.0f || direction_dot <= 0.0f)
				continue;
		} else if (start_distance < 40.0f || direction_dot >= 0.0f) {
			continue;
		}

		const float fraction = -start_distance / direction_dot;
		int axis_u;
		int axis_v;
		if (normal->z > normal->x && normal->z > normal->y) {
			axis_u = 0;
			axis_v = 1;
		} else if (normal->y > normal->x && normal->y > normal->z) {
			axis_u = 0;
			axis_v = 2;
		} else {
			axis_u = 1;
			axis_v = 2;
		}
		const float projected_u = start[axis_u] + (end[axis_u] - start[axis_u]) * fraction;
		const float projected_v = start[axis_v] + (end[axis_v] - start[axis_v]) * fraction;
		float first_cross = 0.0f;
		int outside = 0;
		for (int edge = 0; edge < vertex_count; ++edge) {
			const int next = (edge + 1) % vertex_count;
			const float cross =
				(projected_u - vertices[edge][axis_u]) * (vertices[next][axis_v] - vertices[edge][axis_v]) -
				(projected_v - vertices[edge][axis_v]) * (vertices[next][axis_u] - vertices[edge][axis_u]);
			if (edge == 0) {
				first_cross = cross;
			} else if ((first_cross < 0.0f && cross >= 0.0f) || (first_cross >= 0.0f && cross < 0.0f)) {
				outside = 1;
				break;
			}
		}
		if (!outside)
			return 1;
	}
	return 0;
}

// FUNCTION: TIE98 0x434D40
static int FlightModel_TestLightSegmentAgainstNode(const Tie98OptimizedPolyObject* model,
												   const Tie98OptNode* input_node, SceneMeshTIE98* mesh,
												   const Vec3f* segment_start, const Vec3f* segment_end) {
	const Tie98OptNode* node = input_node;
	if (!node)
		return 0;
	while (node->type == TIE98_OPT_NODE_REFERENCE) {
		node = OptModel_FindNodeByNameInModel(model, (const char*)node->param2);
		if (!node)
			return 0;
	}

	if (node->param2) {
		switch (node->type) {
			case TIE98_OPT_NODE_FACE_DATA:
			case TIE98_OPT_NODE_FACE_DATA_15:
			case TIE98_OPT_NODE_FACE_DATA_16:
			case TIE98_OPT_NODE_FACE_DATA_17: {
				const uint8_t* face_data = node->param2;
				mesh->faceCount = (int)node->param1;
				memcpy(&mesh->edgeCount, face_data, sizeof mesh->edgeCount);
				mesh->pFaceGeom = (FaceRecordTIE98*)(face_data + 4);
				mesh->pFaceNormals = (Vec3f*)(mesh->pFaceGeom + mesh->faceCount);
				mesh->pFaceTexturing = (FaceTextureGradientsTIE98*)(mesh->pFaceNormals + mesh->faceCount);
				Vec3f* inline_vertex_normals = (Vec3f*)(mesh->pFaceTexturing + mesh->faceCount);
				if (!mesh->pVertNormals) {
					mesh->pVertNormals = inline_vertex_normals;
					const int blocked =
						FlightModel_TestLightSegmentAgainstFaces(mesh, segment_start, segment_end);
					mesh->pVertNormals = NULL;
					if (blocked)
						return 1;
				} else if (FlightModel_TestLightSegmentAgainstFaces(mesh, segment_start, segment_end)) {
					return 1;
				}
				break;
			}
			case TIE98_OPT_NODE_TRANSFORM: {
				Vec3f* offset = (Vec3f*)node->param2;
				Matrix3x3* transform = (Matrix3x3*)(offset + 1);
				Math3D_MulMatrix3x3(&mesh->viewOrient, transform);
				Math3D_RotateVec3(&mesh->viewPos, transform);
				mesh->viewPos.x += offset->x;
				mesh->viewPos.y += offset->y;
				mesh->viewPos.z += offset->z;
				Math3D_MulMatrix3x3T(&mesh->orient, transform);
				mesh->pos.x -= Math3D_RotateVec3X(offset, &mesh->orient);
				mesh->pos.y -= Math3D_RotateVec3Y(offset, &mesh->orient);
				mesh->pos.z -= Math3D_RotateVec3Z(offset, &mesh->orient);
				break;
			}
			case TIE98_OPT_NODE_TRANSLATE: {
				Vec3f* offset = (Vec3f*)node->param2;
				mesh->viewPos.x += offset->x;
				mesh->viewPos.y += offset->y;
				mesh->viewPos.z += offset->z;
				mesh->pos.x -= Math3D_RotateVec3X(offset, &mesh->orient);
				mesh->pos.y -= Math3D_RotateVec3Y(offset, &mesh->orient);
				mesh->pos.z -= Math3D_RotateVec3Z(offset, &mesh->orient);
				break;
			}
			case TIE98_OPT_NODE_MATRIX:
				Math3D_MulMatrix3x3(&mesh->viewOrient, (Matrix3x3*)node->param2);
				Math3D_RotateVec3(&mesh->viewPos, (Matrix3x3*)node->param2);
				Math3D_MulMatrix3x3T(&mesh->orient, (Matrix3x3*)node->param2);
				break;
			case TIE98_OPT_NODE_SCALE: {
				Vec3f* scale = (Vec3f*)node->param2;
				mesh->viewOrient.m[0] *= scale->x;
				mesh->viewOrient.m[1] *= scale->y;
				mesh->viewOrient.m[2] *= scale->z;
				mesh->viewOrient.m[3] *= scale->x;
				mesh->viewOrient.m[4] *= scale->y;
				mesh->viewOrient.m[5] *= scale->z;
				mesh->viewOrient.m[6] *= scale->x;
				mesh->viewOrient.m[7] *= scale->y;
				mesh->viewOrient.m[8] *= scale->z;
				mesh->viewPos.x *= scale->x;
				mesh->viewPos.y *= scale->y;
				mesh->viewPos.z *= scale->z;
				const float inverse_x = 1.0f / scale->x;
				const float inverse_y = 1.0f / scale->y;
				const float inverse_z = 1.0f / scale->z;
				mesh->orient.m[0] *= inverse_x;
				mesh->orient.m[1] *= inverse_x;
				mesh->orient.m[2] *= inverse_x;
				mesh->orient.m[3] *= inverse_y;
				mesh->orient.m[4] *= inverse_y;
				mesh->orient.m[5] *= inverse_y;
				mesh->orient.m[6] *= inverse_z;
				mesh->orient.m[7] *= inverse_z;
				mesh->orient.m[8] *= inverse_z;
				break;
			}
			case TIE98_OPT_NODE_MESH_VERTICES:
				mesh->vertexCount = (int)node->param1;
				mesh->pModelVerts = (Vec3f*)node->param2;
				break;
			case TIE98_OPT_NODE_VERTEX_NORMALS:
				g_curVertNormals = (intptr_t)node->param2;
				mesh->pVertNormals = (Vec3f*)node->param2;
				break;
			default:
				break;
		}
	}

	if (node->child_count == 0)
		return 0;
	SceneMeshTIE98 child_mesh = *mesh;
	g_modelNodeWalkUnusedScratch0 = 0;
	g_modelNodeWalkUnusedScratch1 = 0;
	g_curVertNormals = 0;
	g_modelNodeWalkUnusedScratch2 = 0;
	g_curMeshFlags = 0;
	g_curVertexCount = 0;
	for (int i = 0; i < node->child_count; ++i) {
		if (FlightModel_TestLightSegmentAgainstNode(model, node->children[i], &child_mesh, segment_start,
													segment_end))
			return 1;
	}
	return 0;
}

// FUNCTION: TIE98 0x434BF0
static int FlightModel_IsLightSegmentBlocked(FlightObject* object, const Vec3f* segment_start,
											 const Vec3f* segment_end) {
	if (!g_modelSelfOcclusionEnabled)
		return 0;
	// PORT: the host supplies the already-adjusted native OPT tree in place of the
	// original handle unlock/lock and pointer-adjustment sequence.
	const Tie98OptimizedPolyObject* model = TieNativeOpt_Acquire(object->ship_idx);
	if (!model)
		return 0;

	SceneMeshTIE98 mesh;
	memset(&mesh, 0, sizeof mesh);
	mesh.pObject = object;
	mesh.viewOrient.m[0] = 1.0f;
	mesh.viewOrient.m[4] = 1.0f;
	mesh.viewOrient.m[8] = 1.0f;
	mesh.orient.m[0] = 1.0f;
	mesh.orient.m[4] = 1.0f;
	mesh.orient.m[8] = 1.0f;
	g_modelNodeWalkUnusedScratch0 = 0;
	g_modelNodeWalkUnusedScratch1 = 0;
	g_curVertNormals = 0;
	g_modelNodeWalkUnusedScratch2 = 0;
	g_curMeshFlags = 0;
	g_curVertexCount = 0;
	for (int i = 0; i < model->root_node_count; ++i) {
		if (FlightModel_TestLightSegmentAgainstNode(model, model->root_nodes[i], &mesh, segment_start,
													segment_end))
			return 1;
	}
	return 0;
}

// FUNCTION: TIE98 0x4333A0
static void FlightModel_Apply_BWing_Bridge_Rotation(const Tie98OptimizedPolyObject* model,
													FlightObject* object, SceneMeshTIE98* mesh,
													int bridge_mesh_index) {
	(void)model;
	float axis_angle[4];
	Matrix3x3 rotation;

	axis_angle[0] = 0.0f;
	axis_angle[1] = -1.0f;
	axis_angle[2] = 0.0f;
	axis_angle[3] = (float)object->craft_ptr->mesh_rotation[bridge_mesh_index] * 0.024543673f;
	Math3D_BuildAxisAngleMatrix(&rotation, axis_angle);
	Math3D_MulMatrix3x3(&mesh->orient, &rotation);
	Math3D_RotateVec3(&mesh->pos, &rotation);
	Math3D_MulMatrix3x3T(&mesh->viewOrient, &rotation);
}

// FUNCTION: TIE98 0x434330
static void FlightModel_Draw_OPT_Node(const Tie98OptimizedPolyObject* model, const Tie98OptNode* input_node,
									  SceneMeshTIE98* mesh) {
	const Tie98OptNode* node = input_node;
	if (!node)
		return;
	while (node->type == TIE98_OPT_NODE_REFERENCE) {
		if (node->param1)
			node = (const Tie98OptNode*)node->param1;
		else
			node = OptModel_FindNodeByNameInModel(model, (const char*)node->param2);
		if (!node)
			return;
	}

	int selected_lod = 0;
	int selected_switch = 0;
	if (node->param2) {
		switch (node->type) {
			case TIE98_OPT_NODE_FACE_DATA:
			case TIE98_OPT_NODE_FACE_DATA_15:
			case TIE98_OPT_NODE_FACE_DATA_16:
			case TIE98_OPT_NODE_FACE_DATA_17: {
				const uint8_t* face_data = node->param2;
				mesh->faceCount = (int)node->param1;
				memcpy(&mesh->edgeCount, face_data, sizeof mesh->edgeCount);
				if (mesh->edgeCount * 4 > g_sceneEdgeCapacity) {
					g_sceneEdgeCapacity = mesh->edgeCount * 4;
					g_sceneEdgeList =
						realloc(g_sceneEdgeList, (size_t)g_sceneEdgeCapacity * sizeof *g_sceneEdgeList);
					g_sceneEdgeFlags =
						realloc(g_sceneEdgeFlags, (size_t)mesh->edgeCount * sizeof *g_sceneEdgeFlags);
				}
				mesh->pFaceGeom = (FaceRecordTIE98*)(face_data + 4);
				mesh->pFaceNormals = (Vec3f*)(mesh->pFaceGeom + mesh->faceCount);
				mesh->pFaceTexturing = (FaceTextureGradientsTIE98*)(mesh->pFaceNormals + mesh->faceCount);
				Vec3f* inline_vertex_normals = (Vec3f*)(mesh->pFaceTexturing + mesh->faceCount);
				if (!mesh->pMaterial)
					FlightModel_BindTextureData(model, mesh, g_curTextureDesc);
				if (mesh->pVertNormals) {
					RenderScene_DrawSceneMesh(mesh);
				} else {
					mesh->pVertNormals = inline_vertex_normals;
					RenderScene_DrawSceneMesh(mesh);
					mesh->pVertNormals = NULL;
				}
				break;
			}
			case TIE98_OPT_NODE_TRANSFORM: {
				Vec3f* offset = (Vec3f*)node->param2;
				Matrix3x3* transform = (Matrix3x3*)(offset + 1);
				Math3D_MulMatrix3x3(&mesh->viewOrient, transform);
				Math3D_RotateVec3(&mesh->viewPos, transform);
				mesh->viewPos.x += offset->x;
				mesh->viewPos.y += offset->y;
				mesh->viewPos.z += offset->z;
				Math3D_MulMatrix3x3T(&mesh->orient, transform);
				mesh->pos.x -= Math3D_RotateVec3X(offset, &mesh->orient);
				mesh->pos.y -= Math3D_RotateVec3Y(offset, &mesh->orient);
				mesh->pos.z -= Math3D_RotateVec3Z(offset, &mesh->orient);
				break;
			}
			case TIE98_OPT_NODE_TRANSLATE: {
				Vec3f* offset = (Vec3f*)node->param2;
				mesh->viewPos.x += offset->x;
				mesh->viewPos.y += offset->y;
				mesh->viewPos.z += offset->z;
				mesh->pos.x -= Math3D_RotateVec3X(offset, &mesh->orient);
				mesh->pos.y -= Math3D_RotateVec3Y(offset, &mesh->orient);
				mesh->pos.z -= Math3D_RotateVec3Z(offset, &mesh->orient);
				break;
			}
			case TIE98_OPT_NODE_MATRIX:
				Math3D_MulMatrix3x3(&mesh->viewOrient, (Matrix3x3*)node->param2);
				Math3D_RotateVec3(&mesh->viewPos, (Matrix3x3*)node->param2);
				Math3D_MulMatrix3x3T(&mesh->orient, (Matrix3x3*)node->param2);
				break;
			case TIE98_OPT_NODE_SCALE: {
				Vec3f* scale = (Vec3f*)node->param2;
				mesh->viewOrient.m[0] *= scale->x;
				mesh->viewOrient.m[1] *= scale->y;
				mesh->viewOrient.m[2] *= scale->z;
				mesh->viewOrient.m[3] *= scale->x;
				mesh->viewOrient.m[4] *= scale->y;
				mesh->viewOrient.m[5] *= scale->z;
				mesh->viewOrient.m[6] *= scale->x;
				mesh->viewOrient.m[7] *= scale->y;
				mesh->viewOrient.m[8] *= scale->z;
				mesh->viewPos.x *= scale->x;
				mesh->viewPos.y *= scale->y;
				mesh->viewPos.z *= scale->z;
				const float inverse_x = 1.0f / scale->x;
				const float inverse_y = 1.0f / scale->y;
				const float inverse_z = 1.0f / scale->z;
				mesh->orient.m[0] *= inverse_x;
				mesh->orient.m[1] *= inverse_x;
				mesh->orient.m[2] *= inverse_x;
				mesh->orient.m[3] *= inverse_y;
				mesh->orient.m[4] *= inverse_y;
				mesh->orient.m[5] *= inverse_y;
				mesh->orient.m[6] *= inverse_z;
				mesh->orient.m[7] *= inverse_z;
				mesh->orient.m[8] *= inverse_z;
				break;
			}
			case TIE98_OPT_NODE_MESH_VERTICES:
				/* PORT: dynamic host storage replaces the original fixed 32-bit
				 * handle-backed projection arrays. */
				if ((int)node->param1 > g_modelVertexCapacity) {
					g_modelVertexCapacity = (int)node->param1;
					g_projVertCapacity = 4 * (int)node->param1;
					g_projVertList =
						realloc(g_projVertList, (size_t)g_projVertCapacity * sizeof *g_projVertList);
					g_vertexRemap = realloc(g_vertexRemap, (size_t)node->param1 * sizeof *g_vertexRemap);
					g_emittedVertexByProjection =
						realloc(g_emittedVertexByProjection,
								(size_t)g_projVertCapacity * sizeof *g_emittedVertexByProjection);
				}
				mesh->vertexCount = (int)node->param1;
				mesh->pModelVerts = (Vec3f*)node->param2;
				break;
			case TIE98_OPT_NODE_VERTEX_NORMALS:
				g_curVertNormals = (intptr_t)node->param2;
				mesh->pVertNormals = (Vec3f*)node->param2;
				break;
			case TIE98_OPT_NODE_TEXTURE_COORDINATES:
				mesh->pUVs = (OptTexCoordTIE98*)node->param2;
				break;
			case TIE98_OPT_NODE_FLAGS:
				memcpy(mesh->nodeFlags, node->param2, 3 * sizeof mesh->nodeFlags[0]);
				break;
			case TIE98_OPT_NODE_TYPE_10:
				if (node->param1 == 7 || node->param1 == 8)
					mesh->field_136 = (int)g_curMeshFlags;
				else if (node->param1 == 5 || node->param1 == 6)
					mesh->field_156 = (int)g_curMeshFlags;
				else
					mesh->nodeFlags[3] = (int)g_curMeshFlags;
				break;
			case TIE98_OPT_NODE_TEXTURE: {
				mesh->textureName = node->name;
				g_curTextureDesc = (OptTextureDataTIE98*)node->param2;
				FlightModel_BindTextureData(model, mesh, g_curTextureDesc);
				break;
			}
			case TIE98_OPT_NODE_FACE_GROUP: {
				selected_lod = g_forcedLodLevel;
				if (objecteyez > 0 && selected_lod == 0) {
					float threshold = 1.0f;
					if (g_lodDistanceScale > 0.0f)
						threshold = 1.0f / ((float)objecteyez * g_lodDistanceScale);
					selected_lod = 1;
					const float* thresholds = node->param2;
					while (selected_lod <= node->child_count && thresholds[selected_lod - 1] > threshold)
						++selected_lod;
					if (selected_lod > node->child_count)
						selected_lod = -1;
				} else if (selected_lod == 0) {
					selected_lod = 1;
				} else if (selected_lod > node->child_count) {
					selected_lod = -1;
				}
				break;
			}
			case TIE98_OPT_NODE_ROTATION_SCALE: {
				if (mesh->rotAngle == 0.0f)
					break;
				Vec3f* pivot = (Vec3f*)node->param2;
				Vec3f* axis = pivot + 1;
				mesh->pos.x -= pivot->x;
				mesh->pos.y -= pivot->y;
				mesh->pos.z -= pivot->z;
				mesh->viewPos.x += Math3D_RotateVec3X(pivot, &mesh->viewOrient);
				mesh->viewPos.y += Math3D_RotateVec3Y(pivot, &mesh->viewOrient);
				mesh->viewPos.z += Math3D_RotateVec3Z(pivot, &mesh->viewOrient);
				float axis_angle[4] = {
					axis->x * 0.000030517578f,
					axis->y * 0.000030517578f,
					axis->z * 0.000030517578f,
					mesh->rotAngle,
				};
				Matrix3x3 rotation;
				Math3D_BuildAxisAngleMatrix(&rotation, axis_angle);
				Math3D_MulMatrix3x3(&mesh->orient, &rotation);
				Math3D_RotateVec3(&mesh->pos, &rotation);
				Math3D_MulMatrix3x3T(&mesh->viewOrient, &rotation);
				mesh->pos.x += pivot->x;
				mesh->pos.y += pivot->y;
				mesh->pos.z += pivot->z;
				mesh->viewPos.x -= Math3D_RotateVec3X(pivot, &mesh->viewOrient);
				mesh->viewPos.y -= Math3D_RotateVec3Y(pivot, &mesh->viewOrient);
				mesh->viewPos.z -= Math3D_RotateVec3Z(pivot, &mesh->viewOrient);
				break;
			}
			case TIE98_OPT_NODE_SWITCH:
				selected_switch = g_nodeSwitchIndex + 1;
				if (selected_switch > node->child_count)
					selected_switch = node->child_count;
				break;
			default:
				break;
		}
	} else {
		if (node->type == TIE98_OPT_NODE_TYPE_10) {
			if (node->param1 == 7 || node->param1 == 8)
				mesh->field_136 = (int)g_curMeshFlags;
			else if (node->param1 == 5 || node->param1 == 6)
				mesh->field_156 = (int)g_curMeshFlags;
			else
				mesh->nodeFlags[3] = (int)g_curMeshFlags;
		} else if (node->type == TIE98_OPT_NODE_SWITCH) {
			selected_switch = g_nodeSwitchIndex + 1;
			if (selected_switch > node->child_count)
				selected_switch = node->child_count;
		}
	}

	if (node->child_count == 0)
		return;
	int selected = selected_switch ? selected_switch : selected_lod;
	if (selected == -1)
		return;
	if (selected != 0) {
		++g_curLayerId;
		FlightModel_Draw_OPT_Node(model, node->children[selected - 1], mesh);
		return;
	}
	SceneMeshTIE98 child_mesh = *mesh;
	g_modelNodeWalkUnusedScratch0 = 0;
	g_modelNodeWalkUnusedScratch1 = 0;
	g_curVertNormals = 0;
	g_modelNodeWalkUnusedScratch2 = 0;
	g_curMeshFlags = 0;
	g_curVertexCount = 0;
	for (int i = 0; i < node->child_count; ++i) {
		++g_curLayerId;
		FlightModel_Draw_OPT_Node(model, node->children[i], &child_mesh);
	}
}

/* RECOVERY HELPER: removes the SceneMesh initialization duplicated by
 * TIE98 FlightModel_Draw_Object and FlightModel_Draw_Object_Mesh. */
static void FlightModel_Init_Object_Mesh(SceneMeshTIE98* mesh, FlightObject* object,
										 int full_width_position) {
	memset(mesh, 0, sizeof *mesh);
	mesh->pObject = object;
	/* PORT: when full_width_position is false, TIE98 reads a compact position
	 * overlay from ObjectRecord. tie_core stores every object in the recovered
	 * TIE95 full-width layout, so both source representations resolve here. */
	(void)full_width_position;
	mesh->viewPos.x = (float)(object->world_x - camera.x);
	mesh->viewPos.y = (float)(object->world_y - camera.y);
	mesh->viewPos.z = (float)(object->world_z - camera.z);
	mesh->viewOrient.m[0] = (float)worldeyeA1 / 32768.0f;
	mesh->viewOrient.m[1] = (float)worldeyeA2 / 32768.0f;
	mesh->viewOrient.m[2] = (float)worldeyeA3 / 32768.0f;
	mesh->viewOrient.m[3] = (float)worldeyeB1 / 32768.0f;
	mesh->viewOrient.m[4] = (float)worldeyeB2 / 32768.0f;
	mesh->viewOrient.m[5] = (float)worldeyeB3 / 32768.0f;
	mesh->viewOrient.m[6] = (float)worldeyeC1 / 32768.0f;
	mesh->viewOrient.m[7] = (float)worldeyeC2 / 32768.0f;
	mesh->viewOrient.m[8] = (float)worldeyeC3 / 32768.0f;
	Math3D_RotateVec3(&mesh->viewPos, &mesh->viewOrient);

	mesh->viewOrient.m[0] = (float)rotworldeyeA1 / 32768.0f;
	mesh->viewOrient.m[1] = (float)rotworldeyeA2 / 32768.0f;
	mesh->viewOrient.m[2] = (float)rotworldeyeA3 / 32768.0f;
	mesh->viewOrient.m[3] = (float)rotworldeyeB1 / 32768.0f;
	mesh->viewOrient.m[4] = (float)rotworldeyeB2 / 32768.0f;
	mesh->viewOrient.m[5] = (float)rotworldeyeB3 / 32768.0f;
	mesh->viewOrient.m[6] = (float)rotworldeyeC1 / 32768.0f;
	mesh->viewOrient.m[7] = (float)rotworldeyeC2 / 32768.0f;
	mesh->viewOrient.m[8] = (float)rotworldeyeC3 / 32768.0f;
	for (int row = 0; row < 3; ++row)
		for (int column = 0; column < 3; ++column)
			mesh->orient.m[row * 3 + column] = mesh->viewOrient.m[column * 3 + row];
	mesh->pos.x = -mesh->viewPos.x;
	mesh->pos.y = -mesh->viewPos.y;
	mesh->pos.z = -mesh->viewPos.z;
	Math3D_RotateVec3(&mesh->pos, &mesh->orient);
	if (!g_defaultTextureInitialized) {
		memset(g_defaultTextureRgb24, 255, sizeof g_defaultTextureRgb24);
		ModelTexture_BuildPalettedShadeTable(g_defaultTextureData, g_defaultTextureRgb24, 8, 8);
		g_defaultTextureInitialized = 1;
	}
}

// FUNCTION: TIE98 0x433430
void FlightModel_Draw_Object(FlightObject* object) {
	const uint16_t model_type = object->ship_idx;
	const int model_has_component_state = tie98_model_variant_enabled[model_type] != 0;
	g_nodeSwitchIndex = model_has_component_state ? object->decal_color : 0;
	const Tie98OptimizedPolyObject* model =
		g_flightModelOverride ? g_flightModelOverride : TieNativeOpt_Acquire(model_type);
	if (!model)
		return;
	SceneMeshTIE98 mesh;
	FlightModel_Init_Object_Mesh(&mesh, object, model_type == 0 || model_has_component_state);
	g_curTextureDesc = &g_defaultMaterial;
	g_modelNodeWalkUnusedScratch0 = 0;
	g_modelNodeWalkUnusedScratch1 = 0;
	g_curVertNormals = 0;
	g_modelNodeWalkUnusedScratch2 = 0;
	g_curMeshFlags = 0;
	g_curVertexCount = 0;
	int mesh_ordinal = 0;
	for (int root = 0; root < model->root_node_count; ++root) {
		const Tie98OptNode* node = model->root_nodes[root];
		mesh.rotAngle = 0.0f;
		int restore_mesh = 0;
		SceneMeshTIE98 saved_mesh;
		if (node && node->type != TIE98_OPT_NODE_TEXTURE) {
			++mesh_ordinal;
			if (object->genus == GENUS_PROJECTILE_PLAYER || object->genus == GENUS_PROJECTILE_NPC) {
				g_nodeSwitchIndex = 0;
			} else if (model_has_component_state) {
				if (object->craft_ptr->mesh_state[mesh_ordinal - 1] != 0)
					continue;
				mesh.rotAngle = object->craft_ptr->mesh_rotation[mesh_ordinal - 1] * 0.024543673f;
			}
			if (model_type == 4) {
				if (g_bwingBridgeMeshIndex == -1)
					g_bwingBridgeMeshIndex = modelmesh_findbridgeindex(model_type);
				if (g_bwingBridgeMeshIndex != -1 &&
					object->craft_ptr->mesh_rotation[g_bwingBridgeMeshIndex] != 0) {
					saved_mesh = mesh;
					restore_mesh = 1;
					FlightModel_Apply_BWing_Bridge_Rotation(model, object, &mesh, g_bwingBridgeMeshIndex);
				}
			}
		}
		++g_curLayerId;
		FlightModel_Draw_OPT_Node(model, node, &mesh);
		if (restore_mesh)
			mesh = saved_mesh;
	}
}

// FUNCTION: TIE98 0x433980
void FlightModel_Draw_Object_Mesh(FlightObject* object, int mesh_index) {
	uint16_t model_type = object->ship_idx;
	if (model_type == 89)
		model_type = object->ship_type_override;
	const Tie98OptimizedPolyObject* model = TieNativeOpt_Acquire(model_type);
	if (!model)
		return;
	g_nodeSwitchIndex = object->decal_color;
	SceneMeshTIE98 mesh;
	FlightModel_Init_Object_Mesh(&mesh, object, 1);
	g_curTextureDesc = &g_defaultMaterial;
	g_modelNodeWalkUnusedScratch0 = 0;
	g_modelNodeWalkUnusedScratch1 = 0;
	g_curVertNormals = 0;
	g_modelNodeWalkUnusedScratch2 = 0;
	g_curMeshFlags = 0;
	g_curVertexCount = 0;
	for (int root = 0; root < model->root_node_count; ++root) {
		const Tie98OptNode* node = model->root_nodes[root];
		if (node && node->type == TIE98_OPT_NODE_TEXTURE) {
			++g_curLayerId;
			FlightModel_Draw_OPT_Node(model, node, &mesh);
			++mesh_index;
		} else if (root == mesh_index) {
			++g_curLayerId;
			FlightModel_Draw_OPT_Node(model, node, &mesh);
		}
	}
}

// FUNCTION: TIE98 0x42B050
static void RenderScene_EffectsPass(void) {
	anim_sort_and_draw_bitmaps_tie98(g_drawSceneEffects != 0);
	numbitmaps = 0;
	if (g_d3dVertexCount != 0 && g_d3dIndexCount != 0) {
		Math_SetFpuExtendedPrecisionMode();
		if (g_powerVrSceneWorkaround) {
			if (g_std3DStartScenePending) {
				std3D_StartScene();
				g_std3DStartScenePending = 0;
			}
		} else {
			std3D_StartScene();
		}
		std3D_LockExecuteBuffer();
		std3D_AddVertices(g_flightVertexBuffer, g_d3dVertexCount);
		std3D_BeginInstructions();
		std3D_AddTriangles(g_triBuffer, (unsigned int)g_d3dIndexCount);
		std3D_ExecuteBuffer();
		if (!g_powerVrSceneWorkaround)
			std3D_EndScene();
		Math_SetFpuSinglePrecisionMode();
	}
	if (g_powerVrSceneWorkaround) {
		Math_SetFpuExtendedPrecisionMode();
		std3D_EndScene();
		g_std3DStartScenePending = 1;
		Math_SetFpuSinglePrecisionMode();
	}
}

// FUNCTION: TIE98 0x41F7D0
static void FlightLight_ResetSoftwareFaceSampleCache(void) {
	g_swFaceLightCachedObject = NULL;
	g_swFaceLightCachedFace = NULL;
}

// FUNCTION: TIE98 0x41F7E0
static float FlightLight_ComputeSoftwareFaceSampleIntensity(SceneFaceTIE98* face, int screen_x, int screen_y,
															float view_z) {
	SceneMeshTIE98* mesh = face->pMesh;
	if (g_swFaceLightCachedObject != mesh->pObject) {
		g_swFaceLightCount = tie_makelocallights_tie98(mesh->pObject);
		g_swFaceLightCachedObject = mesh->pObject;
		for (int light_index = 0; light_index < g_swFaceLightCount; ++light_index) {
			Vec3f light = {
				(float)localLights[light_index].x,
				(float)localLights[light_index].y,
				(float)localLights[light_index].z,
			};
			Math3D_RotateVec3(&light, &mesh->viewOrient);
			light.x += mesh->viewPos.x;
			light.y += mesh->viewPos.y;
			light.z += mesh->viewPos.z;
			g_swFaceLightPositions[light_index] = light;
			g_swFaceLightIntensities[light_index] = (float)localLights[light_index].range;
		}
		g_swFaceDirectionalLight = (Vec3f) {
			(float)rotlightX * (1.0f / 32768.0f),
			(float)rotlightY * (1.0f / 32768.0f),
			(float)rotlightZ * (1.0f / 32768.0f),
		};
		Math3D_RotateVec3(&g_swFaceDirectionalLight, &mesh->viewOrient);
	}

	const float position_z = 1.0f / view_z;
	const float screen_scale = position_z * g_invProjScale;
	const Vec3f position = {
		(float)(screen_x - ((uint16_t)pixelswide >> 1)) * screen_scale,
		(float)(screen_y - ((uint16_t)pixelsdeep >> 1) - transfm2_screenyoffset) * screen_scale,
		position_z,
	};
	if (g_swFaceLightCachedFace != face) {
		g_swFaceLightCachedFace = face;
		g_swFaceLightCachedNormal = mesh->pFaceNormals[face->faceIndex];
		Math3D_RotateVec3(&g_swFaceLightCachedNormal, &mesh->viewOrient);
	}

	float intensity = 0.0f;
	if (g_specularLightingEnabled) {
		const float eye_x = -position.x;
		const float eye_y = -position.y;
		const float eye_z = -position.z;
		const float abs_x = fabsf(eye_x);
		const float abs_y = fabsf(eye_y);
		const float abs_z = fabsf(eye_z);
		float eye_length;
		if (abs_x >= abs_y && abs_x >= abs_z)
			eye_length = abs_x * 0.9264f + (abs_y + abs_z) * 0.3872f;
		else if (abs_y >= abs_x && abs_y >= abs_z)
			eye_length = abs_y * 0.9264f + (abs_x + abs_z) * 0.3872f;
		else
			eye_length = abs_z * 0.9264f + (abs_x + abs_y) * 0.3872f;
		const float inverse_eye_length = 1.0f / eye_length;
		const Vec3f halfway = {
			eye_x * inverse_eye_length + g_swFaceDirectionalLight.x,
			eye_y * inverse_eye_length + g_swFaceDirectionalLight.y,
			eye_z * inverse_eye_length + g_swFaceDirectionalLight.z,
		};
		const float cosine = halfway.x * g_swFaceLightCachedNormal.x +
							 halfway.y * g_swFaceLightCachedNormal.y +
							 halfway.z * g_swFaceLightCachedNormal.z;
		if (cosine > 0.0f) {
			const float half_cosine = cosine * 0.5f;
			const float cosine3 = half_cosine * half_cosine * half_cosine;
			const float cosine6 = cosine3 * cosine3;
			const float cosine12 = cosine6 * cosine6;
			const float cosine24 = cosine12 * cosine12;
			intensity = cosine24 * cosine24 * 0.7f;
			if (intensity >= 1.0f)
				return 1.0f;
		}
	}

	for (int light_index = 0; light_index < g_swFaceLightCount; ++light_index) {
		const Vec3f delta = {
			g_swFaceLightPositions[light_index].x - position.x,
			g_swFaceLightPositions[light_index].y - position.y,
			g_swFaceLightPositions[light_index].z - position.z,
		};
		const float facing = delta.x * g_swFaceLightCachedNormal.x + delta.y * g_swFaceLightCachedNormal.y +
							 delta.z * g_swFaceLightCachedNormal.z;
		if (facing <= 0.0f)
			continue;
		const float abs_x = fabsf(delta.x);
		const float abs_y = fabsf(delta.y);
		const float abs_z = fabsf(delta.z);
		float distance;
		if (abs_x >= abs_y && abs_x >= abs_z)
			distance = abs_x + (abs_y + abs_z) * 0.2941f;
		else if (abs_y >= abs_x && abs_y >= abs_z)
			distance = abs_y + (abs_x + abs_z) * 0.2941f;
		else
			distance = abs_z + (abs_x + abs_y) * 0.2941f;
		float contribution = facing / (distance * distance);
		if (g_specularLightingEnabled) {
			const Vec3f halfway = {
				delta.x - position.x,
				delta.y - position.y,
				delta.z - position.z,
			};
			const float half_x = fabsf(halfway.x);
			const float half_y = fabsf(halfway.y);
			const float half_z = fabsf(halfway.z);
			float half_length;
			if (half_x >= half_y && half_x >= half_z)
				half_length = half_x * 0.4632f + (half_y + half_z) * 0.1936f;
			else if (half_y >= half_x && half_y >= half_z)
				half_length = half_y * 0.4632f + (half_x + half_z) * 0.1936f;
			else
				half_length = half_z * 0.4632f + (half_x + half_y) * 0.1936f;
			const float half_facing =
				(halfway.x * g_swFaceLightCachedNormal.x + halfway.y * g_swFaceLightCachedNormal.y +
				 halfway.z * g_swFaceLightCachedNormal.z) *
				0.5f / half_length;
			if (half_facing >= 0.0f) {
				const float cosine3 = half_facing * half_facing * half_facing;
				const float cosine6 = cosine3 * cosine3;
				const float cosine12 = cosine6 * cosine6;
				const float cosine24 = cosine12 * cosine12;
				contribution += cosine24 * cosine24 / half_length;
			}
		}
		if (contribution > 0.0f) {
			intensity += contribution * g_swFaceLightIntensities[light_index];
			if (intensity >= 1.0f)
				return 1.0f;
		}
	}
	return intensity;
}

/* RECOVERY HELPER: reproduces the original texture-size-specialized wrapping
 * without duplicating its 8-bit and 16-bit span kernels. */
static void sw3d_DrawTexturedShadeSpanKernel(void) {
	uint8_t* pixel =
		xtrans2_videobaseptr + g_sw3dScanlineByteOffset + g_flight16bppBytesPerPixel * g_sw3dSpanStartX;
	const int width_shift = g_sw3dSpanTextureWidthShift;
	const int height_shift = g_sw3dSpanTextureHeightShift;
	const bool use_specialized_wrap =
		width_shift >= 3 && width_shift <= 8 && height_shift >= 3 && height_shift <= 8;
	const uint32_t u_mask = use_specialized_wrap ? (1u << width_shift) - 1u : 0;
	const uint32_t v_mask = use_specialized_wrap ? (1u << height_shift) - 1u : 0;
	for (int index = 0; index < g_sw3dSpanLength; ++index) {
		const uint32_t integer_u = (uint32_t)g_sw3dSpanUQ8 >> 8;
		const uint32_t integer_v = (uint32_t)g_sw3dSpanVQ8 >> 8;
		uint32_t texel_index;
		if (use_specialized_wrap) {
			texel_index = ((integer_v & v_mask) << width_shift) | (integer_u & u_mask);
		} else {
			texel_index = ((integer_v << width_shift) + integer_u) & (uint32_t)g_sw3dSpanTexelMask;
		}
		const uint8_t texel = g_sw3dSpanTexels[texel_index];
		const unsigned shade = (unsigned)(g_sw3dSpanShadeDitherAccum + g_sw3dSpanShadeQ8);
		g_sw3dSpanShadeDitherAccum = (uint8_t)shade;
		if (g_flight16bppBytesPerPixel == 2) {
			const uint16_t* colors = (const uint16_t*)(g_sw3dSpanShadeTable + 4096);
			((uint16_t*)pixel)[0] = colors[((shade >> 8) & 15) * 256 + texel];
			pixel += 2;
		} else {
			*pixel++ = g_sw3dSpanShadeTable[((shade >> 8) & 15) * 256 + texel];
		}
		g_sw3dSpanShadeQ8 += g_sw3dSpanShadeStepQ8;
		g_sw3dSpanUQ8 += g_sw3dSpanStepUQ8;
		g_sw3dSpanVQ8 += g_sw3dSpanStepVQ8;
	}
}

// FUNCTION: TIE98 0x43F260
static void sw3d_DrawTexturedShadeSpan(int start_x, int end_x, float start_view_z) {
	SceneFaceTIE98* face = g_sw3dCurrentFace;
	SoftwareLightSampleTIE98* samples = face->pPhongData;
	const float scan_y = (float)(uint32_t)g_sw3dCurrentScanY;
	const float view_z_at_y = scan_y * face->gradients[7] + face->gradients[8];
	const float u_at_y = scan_y * face->gradients[1] + face->gradients[2];
	const float v_at_y = scan_y * face->gradients[4] + face->gradients[5];
	float u_numerator = (float)start_x * face->gradients[0] + u_at_y;
	float v_numerator = (float)start_x * face->gradients[3] + v_at_y;
	float inverse_view_z = 1.0f / start_view_z;
	float u = inverse_view_z * u_numerator;
	float v = inverse_view_z * v_numerator;
	float light_intensity =
		face->pScanEdge->lightIntensity + ((float)start_x - face->pScanEdge->x) * face->spanLightIntensityDx;

	g_sw3dSpanShadeDitherAccum = (g_sw3dCurrentScanY & 1) ? 128 : 0;
	const int start_block = start_x >> g_sw3dLightSampleBlockShift;
	const int end_block = (end_x - 1) >> g_sw3dLightSampleBlockShift;
	g_sw3dSpanStartX = start_x;
	const int within_block_x = start_x & g_sw3dLightSampleBlockMask;
	const int within_block_y = g_sw3dCurrentScanY & g_sw3dLightSampleBlockMask;
	const int block_start_x = start_x - within_block_x;
	const int block_start_y = g_sw3dCurrentScanY - within_block_y;
	SoftwareLightSampleTIE98* left_sample = &samples[start_block];
	int stamp_delta = g_sw3dCurrentLightSampleCacheStamp - left_sample->stamp;
	if (stamp_delta != 0) {
		if (stamp_delta != 1) {
			left_sample->stamp = g_sw3dCurrentLightSampleCacheStamp;
			left_sample->intensity = FlightLight_ComputeSoftwareFaceSampleIntensity(
				face, block_start_x, block_start_y,
				start_view_z - (float)within_block_x * face->gradients[6] -
					g_sw3dLightSampleSubrowFloat * face->gradients[7]);
		} else {
			++left_sample->stamp;
			left_sample->intensity += left_sample->rowDelta;
		}
		left_sample->rowDelta = FlightLight_ComputeSoftwareFaceSampleIntensity(
									face, block_start_x, block_start_y + g_sw3dLightSampleBlockSize,
									start_view_z - (float)within_block_x * face->gradients[6] +
										g_sw3dLightSampleRowsToNextBlockFloat * face->gradients[7]) -
								left_sample->intensity;
	}
	float left_light = left_sample->intensity + g_sw3dLightSampleSubrowLerpT * left_sample->rowDelta;

	int boundary_x = (start_block + 1) << g_sw3dLightSampleBlockShift;
	g_sw3dSpanLength = boundary_x - g_sw3dSpanStartX;
	u_numerator = (float)boundary_x * face->gradients[0] + u_at_y;
	v_numerator = (float)boundary_x * face->gradients[3] + v_at_y;
	float boundary_view_z = (float)boundary_x * face->gradients[6] + view_z_at_y;
	int block = start_block + 1;
	inverse_view_z = 1.0f / boundary_view_z;
	SoftwareLightSampleTIE98* right_sample = &samples[block];
	stamp_delta = g_sw3dCurrentLightSampleCacheStamp - right_sample->stamp;
	if (stamp_delta != 0) {
		if (stamp_delta != 1) {
			right_sample->stamp = g_sw3dCurrentLightSampleCacheStamp;
			right_sample->intensity = FlightLight_ComputeSoftwareFaceSampleIntensity(
				face, boundary_x, block_start_y, boundary_view_z);
		} else {
			++right_sample->stamp;
			right_sample->intensity += right_sample->rowDelta;
		}
		right_sample->rowDelta =
			FlightLight_ComputeSoftwareFaceSampleIntensity(
				face, boundary_x, block_start_y + g_sw3dLightSampleBlockSize, boundary_view_z) -
			right_sample->intensity;
	}
	float right_light = right_sample->intensity + g_sw3dLightSampleSubrowLerpT * right_sample->rowDelta;
	left_light += (right_light - left_light) * ((float)within_block_x / (float)g_sw3dLightSampleBlockSize);
	float right_u = inverse_view_z * u_numerator;
	float right_v = inverse_view_z * v_numerator;
	g_sw3dSpanStepUQ8 = (int)lrintf((right_u - u) / (float)g_sw3dSpanLength *
									(float)(1 << (g_sw3dSpanTextureWidthShift + 8)));
	g_sw3dSpanStepVQ8 = (int)lrintf((right_v - v) / (float)g_sw3dSpanLength *
									(float)(1 << (g_sw3dSpanTextureHeightShift + 8)));

	const float u_numerator_step = face->gradients[0] * 16.0f;
	const float v_numerator_step = face->gradients[3] * 16.0f;
	const float view_z_step = face->gradients[6] * 16.0f;
	if (block > end_block)
		g_sw3dSpanLength = end_x - g_sw3dSpanStartX;
	float light_at_end = (float)g_sw3dSpanLength * face->spanLightIntensityDx + light_intensity;
	const float light_block_step = face->spanLightIntensityDx * 16.0f;
	g_sw3dSpanShadeQ8 = (int)lrintf((left_light + light_intensity) * 15.0f * 256.0f);
	if (g_sw3dSpanShadeQ8 < 0)
		g_sw3dSpanShadeQ8 = 0;
	if (g_sw3dSpanShadeQ8 > 0xEFF)
		g_sw3dSpanShadeQ8 = 0xEFF;
	int end_shade = (int)lrintf((right_light + light_at_end) * 15.0f * 256.0f);
	if (end_shade < 0)
		end_shade = 0;
	if (end_shade > 0xEFF)
		end_shade = 0xEFF;
	int shade_delta = end_shade - g_sw3dSpanShadeQ8;
	if (shade_delta < 0)
		shade_delta += g_sw3dLightSampleBlockSize;
	g_sw3dSpanShadeStepQ8 = (int)lrintf((float)shade_delta / (float)g_sw3dSpanLength);
	g_sw3dSpanUQ8 = (int)lrintf(u * (float)(1 << (g_sw3dSpanTextureWidthShift + 8)));
	g_sw3dSpanVQ8 = (int)lrintf(v * (float)(1 << (g_sw3dSpanTextureHeightShift + 8)));
	int next_u = (int)lrintf(right_u * (float)(1 << (g_sw3dSpanTextureWidthShift + 8)));
	int next_v = (int)lrintf(right_v * (float)(1 << (g_sw3dSpanTextureHeightShift + 8)));

	while (1) {
		if (block <= end_block) {
			boundary_view_z += view_z_step;
			inverse_view_z = 1.0f / boundary_view_z;
		}
		sw3d_DrawTexturedShadeSpanKernel();
		if (block > end_block)
			break;

		u_numerator += u_numerator_step;
		v_numerator += v_numerator_step;
		g_sw3dSpanStartX += g_sw3dSpanLength;
		boundary_x += g_sw3dLightSampleBlockSize;
		if (block == end_block)
			g_sw3dSpanLength = end_x - g_sw3dSpanStartX;
		else
			g_sw3dSpanLength = g_sw3dLightSampleBlockSize;
		++block;
		++right_sample;
		stamp_delta = g_sw3dCurrentLightSampleCacheStamp - right_sample->stamp;
		if (stamp_delta != 0) {
			if (stamp_delta != 1) {
				right_sample->stamp = g_sw3dCurrentLightSampleCacheStamp;
				right_sample->intensity = FlightLight_ComputeSoftwareFaceSampleIntensity(
					face, boundary_x, block_start_y, boundary_view_z);
			} else {
				++right_sample->stamp;
				right_sample->intensity += right_sample->rowDelta;
			}
			right_sample->rowDelta =
				FlightLight_ComputeSoftwareFaceSampleIntensity(
					face, boundary_x, block_start_y + g_sw3dLightSampleBlockSize, boundary_view_z) -
				right_sample->intensity;
		}
		right_u = inverse_view_z * u_numerator;
		right_v = inverse_view_z * v_numerator;
		right_light = right_sample->intensity + g_sw3dLightSampleSubrowLerpT * right_sample->rowDelta;
		light_at_end += light_block_step;
		g_sw3dSpanShadeQ8 = end_shade;
		end_shade = (int)lrintf((right_light + light_at_end) * 15.0f * 256.0f);
		if (end_shade < 0)
			end_shade = 0;
		if (end_shade > 0xEFF)
			end_shade = 0xEFF;
		shade_delta = end_shade - g_sw3dSpanShadeQ8;
		if (shade_delta < 0)
			shade_delta += g_sw3dLightSampleBlockSize;
		g_sw3dSpanShadeStepQ8 = shade_delta >> g_sw3dLightSampleBlockShift;
		g_sw3dSpanUQ8 = next_u;
		g_sw3dSpanVQ8 = next_v;
		next_u = (int)lrintf(right_u * (float)(1 << (g_sw3dSpanTextureWidthShift + 8)));
		next_v = (int)lrintf(right_v * (float)(1 << (g_sw3dSpanTextureHeightShift + 8)));
		g_sw3dSpanStepUQ8 = (next_u - g_sw3dSpanUQ8) >> g_sw3dLightSampleBlockShift;
		g_sw3dSpanStepVQ8 = (next_v - g_sw3dSpanVQ8) >> g_sw3dLightSampleBlockShift;
	}
}

// FUNCTION: TIE98 0x43DF20
void RenderScene_DrawVisibleFaces(void) {
	if (g_useHardware3D) {
		RenderScene_EffectsPass();
		return;
	}
	FlightLight_ResetSoftwareFaceSampleCache();
	if (!g_flightSurfaceAlreadyLocked)
		FlightSurface_Lock();
	g_sw3dSpanSceneMesh = NULL;
	for (int face_index = g_visFaceDrawStartIndex; face_index < g_visFaceCount; ++face_index) {
		int mip_offset = 0;
		g_sw3dCurrentFace = &g_visFaceList[face_index];
		g_sw3dCurrentScanY = g_sw3dCurrentFace->yTop;
		SceneEdgeTIE98 scan_edge;
		g_sw3dCurrentFace->pScanEdge = &scan_edge;
		SceneFaceTIE98* face = g_sw3dCurrentFace;
		int texture_width = face->pMesh->pMaterial->width;
		int texture_height = face->pMesh->pMaterial->height;
		if (g_sw3dMipmapEnabled && texture_width * texture_height == face->pMesh->pMaterial->textureSize) {
			int lod = (int)((float)face->mipLevel * g_mipLodScale);
			while (lod > 256 && texture_width != 8 && texture_height != 8) {
				lod >>= 2;
				mip_offset += texture_width * texture_height;
				texture_width >>= 1;
				texture_height >>= 1;
			}
		}
		g_sw3dSpanTextureWidthFloat = (float)texture_width;
		g_sw3dSpanTextureHeightFloat = (float)texture_height;
		g_sw3dSpanTextureWidthShift = g_sw3dTextureShiftBySizeDiv16[texture_width >> 4];
		g_sw3dSpanTextureHeightShift = g_sw3dTextureShiftBySizeDiv16[texture_height >> 4];
		g_sw3dSpanTexelMask = texture_width * texture_height - 1;
		g_sw3dSpanShadeTable = face->pMesh->pPalette0;
		g_sw3dSpanTexels = face->pMesh->pTexels + mip_offset;
		g_sw3dSpanSceneMesh = face->pMesh;
		float scanline_view_z = (float)(uint32_t)g_sw3dCurrentScanY * face->gradients[7] + face->gradients[8];
		g_sw3dScanlineByteOffset = g_flight16bppBytesPerPixel * displaycorner_columns +
								   (int)g_surfacePitch * (g_sw3dCurrentScanY + displaycorner_lines);

		for (int scanline = 0; (uint32_t)g_sw3dCurrentScanY < (uint32_t)face->yBot;
			 ++scanline, ++g_sw3dCurrentScanY) {
			SceneSpanTIE98* span = face->pSpans[scanline];
			if (span) {
				const int sample_subrow = g_sw3dCurrentScanY & g_sw3dLightSampleBlockMask;
				if (sample_subrow) {
					g_sw3dLightSampleSubrowFloat = (float)sample_subrow;
					g_sw3dLightSampleRowsToNextBlockFloat =
						(float)(g_sw3dLightSampleBlockSize - sample_subrow);
					g_sw3dLightSampleSubrowLerpT = (float)sample_subrow / (float)g_sw3dLightSampleBlockSize;
				} else {
					g_sw3dLightSampleSubrowLerpT = 0.0f;
					g_sw3dLightSampleSubrowFloat = 0.0f;
					g_sw3dLightSampleRowsToNextBlockFloat = (float)g_sw3dLightSampleBlockSize;
				}
				g_sw3dCurrentLightSampleCacheStamp = g_sw3dLightSampleCacheSceneStampBase +
													 (g_sw3dCurrentScanY >> g_sw3dLightSampleBlockShift);
				int start_x = span->startX;
				const int end_x = span->endX;
				int draw_start_x = start_x;
				scan_edge.x = (float)draw_start_x;
				scan_edge.lightIntensity = span->startLightIntensity;
				face->spanLightIntensityDx = span->dLightIntensityDx;
				SceneSpanTIE98* clip_span = span->next;
				while (clip_span && clip_span->startX < end_x) {
					if (clip_span->startX > start_x) {
						sw3d_DrawTexturedShadeSpan(start_x, clip_span->startX,
												   (float)draw_start_x * face->gradients[6] +
													   scanline_view_z);
					}
					if (start_x < clip_span->endX) {
						start_x = clip_span->endX;
						draw_start_x = start_x;
						if (start_x >= end_x)
							break;
					}
					clip_span = clip_span->next;
				}
				if (start_x < end_x) {
					sw3d_DrawTexturedShadeSpan(start_x, end_x,
											   (float)draw_start_x * face->gradients[6] + scanline_view_z);
				}
			}
			scanline_view_z += face->gradients[7];
			g_sw3dScanlineByteOffset += (int)g_surfacePitch;
		}
	}
	if (!g_flightSurfaceAlreadyLocked)
		FlightSurface_Unlock();
}
