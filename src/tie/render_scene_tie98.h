#ifndef TIE_RENDER_SCENE_TIE98_H
#define TIE_RENDER_SCENE_TIE98_H

#include "tie/math3d_tie98.h"
#include "tie/render_clip_tie98.h"
#include "tie/std3d_tie98.h"
#include "tie_runtime/flight_assets/native_opt.h"

#include <stdint.h>

typedef struct FlightObject FlightObject;

typedef struct OptTexCoordTIE98 {
	float u;
	float v;
} OptTexCoordTIE98;

typedef struct FaceRecordTIE98 {
	int32_t vertexIdx[4];
	int32_t edgeIdx[4];
	int32_t uvIdx[4];
	int32_t normalIdx[4];
} FaceRecordTIE98;

typedef struct FaceTextureGradientsTIE98 {
	Vec3f gradient0;
	Vec3f gradient1;
} FaceTextureGradientsTIE98;

typedef struct OptTextureDataTIE98 {
	uint32_t paletteAddress;
	int32_t paletteType;
	int32_t textureSize;
	int32_t dataSize;
	int32_t width;
	int32_t height;
} OptTextureDataTIE98;

struct SceneMeshTIE98;
struct SceneFaceTIE98;

typedef struct SceneEdgeTIE98 {
	int yEnd;
	int yStart;
	float x;
	float lightIntensity;
	float dxdy;
	float dLightIntensityDy;
	void* pClipVert;
} SceneEdgeTIE98;

typedef struct SceneSpanTIE98 {
	struct SceneSpanTIE98* next;
	int startX;
	int endX;
	float startLightIntensity;
	float dLightIntensityDx;
	struct SceneFaceTIE98* pFace;
} SceneSpanTIE98;

typedef struct SceneFaceTIE98 {
	int faceIndex;
	struct SceneMeshTIE98* pMesh;
	int packed;
	int nearClipState;
	float gradients[9];
	float spanLightIntensityDx;
	SceneEdgeTIE98* pScanEdge;
	void* pPhongData;
	int yTop;
	int yBot;
	float maxVertW;
	float minVertW;
	SceneEdgeTIE98* edges[5];
	int edgeCount;
	SceneSpanTIE98** pSpans;
	int mipLevel;
} SceneFaceTIE98;

typedef struct SceneMeshTIE98 {
	FlightObject* pObject;
	float rotAngle;
	Vec3f viewPos;
	Matrix3x3 viewOrient;
	Vec3f pos;
	Matrix3x3 orient;
	int nodeFlags[4];
	int vertexCount;
	Vec3f* pModelVerts;
	OptTexCoordTIE98* pUVs;
	Vec3f* pVertNormals;
	int field_136;
	int faceCount;
	int edgeCount;
	Vec3f* pFaceNormals;
	FaceTextureGradientsTIE98* pFaceTexturing;
	int field_156;
	FaceRecordTIE98* pFaceGeom;
	const char* textureName;
	OptTextureDataTIE98* pMaterial;
	uint8_t* pTexels;
	uint8_t* pPalette0;
	uint8_t* pPalette1;
	int faceBaseIndex;
	int vertBaseIndex;
	int edgeBaseIndex;
	int visFaceCount;
	int projVertCursor;
	int clippedEdgeCount;
} SceneMeshTIE98;

void RenderScene_Initialize_tie98(int reset_flag);
void RenderScene_UnlockSceneBuffers_tie98(void);
void RenderScene_DrawVisibleFaces(void);
void sw3d_BlitOccludedSpan(const uint8_t* source, int start_x, int end_x, int scan_y, float depth);
void FlightModel_Draw_Object(FlightObject* object);
void FlightModel_Draw_Object_Mesh(FlightObject* object, int mesh_index);
void RenderQuad_DrawRotatedSprite(int angle, int screen_x, int screen_y, uint16_t screen_scale,
								  const uint8_t* texture_level);
int16_t Hud_DrawBoxOverlayHW(int x, int y, int width, int height, int color_index, int depth);
int16_t Hud_DrawBoxInXTrans(int x, int y, int width, int height, int color_index, int depth);
void FlightMap_DrawObjectBoxCorners(int x, int y, int width, int height, uint8_t color_index);

extern int g_drawSceneEffects;
extern int g_useHardware3D;
extern int g_powerVrSceneWorkaround;
extern int g_bilinearEnabled;
extern int g_flightSurfaceAlreadyLocked;
extern const Tie98OptimizedPolyObject* g_flightModelOverride;

#endif
