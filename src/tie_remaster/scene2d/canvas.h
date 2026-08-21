#ifndef TIE_REMASTER_SCENE2D_CANVAS_H
#define TIE_REMASTER_SCENE2D_CANVAS_H

#include "aeron/scene/draw_list2d.h"

#include <stdint.h>

typedef struct TieScene2dCanvas {
	AeronDrawList2D* list;
	int viewport_w;
	int viewport_h;
	int source_w;
	int source_h;
	uint8_t source_pixel_aspect;
	AeronRectI scissor;
	float offset_x;
	float offset_y;
	float scale_x;
	float scale_y;
	int target_w;
	int target_h;
} TieScene2dCanvas;

void TieScene2dCanvas_Begin(TieScene2dCanvas* canvas, AeronDrawList2D* list, int viewport_w, int viewport_h);
void TieScene2dCanvas_SetSourceAspect(TieScene2dCanvas* canvas, int source_w, int source_h,
									  uint8_t source_pixel_aspect);
void TieScene2dCanvas_SetOutputTransform(TieScene2dCanvas* canvas, float offset_x, float offset_y,
										 float scale_x, float scale_y, int target_w, int target_h);
void TieScene2dCanvas_SetScissor(TieScene2dCanvas* canvas, AeronRectI scissor);
void TieScene2dCanvas_AddSprite(TieScene2dCanvas* canvas, const AeronDrawList2DSprite* sprite);
void TieScene2dCanvas_AddQuad4(TieScene2dCanvas* canvas, const AeronDrawList2DQuad4* quad);
void TieScene2dCanvas_AddFill(TieScene2dCanvas* canvas, float x, float y, float w, float h,
							  const float rgba[4], AeronBlit2DBlend blend);

#endif
