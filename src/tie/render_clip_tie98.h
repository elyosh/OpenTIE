#ifndef TIE_RENDER_CLIP_TIE98_H
#define TIE_RENDER_CLIP_TIE98_H

typedef struct ProjVertexTIE98 {
	float sx;
	float sy;
	float w;
	float lightIntensity;
	float tu;
	float tv;
} ProjVertexTIE98;

extern int g_clipIdxA[32];
extern int g_clipIdxB[32];
extern int g_clipCountA;
extern int g_clipCountB;
extern int g_clipVertCursor;
extern int g_clipOccurred;
extern float g_invProjScale;

void RenderClip_ClipPolyTop(int prev_vert, int cur_vert, ProjVertexTIE98* vert_buf);
void RenderClip_ClipPolyBottom(int prev_vert, int cur_vert, ProjVertexTIE98* vert_buf);
void RenderClip_ClipPolyLeft(int prev_vert, int cur_vert, ProjVertexTIE98* vert_buf);
void RenderClip_ClipPolyRight(int prev_vert, int cur_vert, ProjVertexTIE98* vert_buf);
void RenderClip_ClipPolyNear(int prev_vert, int cur_vert, ProjVertexTIE98* vert_buf);

#endif
