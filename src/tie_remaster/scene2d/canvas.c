#include "tie_remaster/scene2d/canvas.h"

#include "tie_remaster/scene2d/viewport.h"

#include <math.h>
#include <string.h>

static AeronRectI TieScene2dCanvas_TransformScissor(const TieScene2dCanvas* canvas, AeronRectI source) {
	if (source.width <= 0 || source.height <= 0)
		return (AeronRectI) { 0 };
	int x0 = (int)floorf(canvas->offset_x + source.x * canvas->scale_x);
	int y0 = (int)floorf(canvas->offset_y + source.y * canvas->scale_y);
	int x1 = (int)ceilf(canvas->offset_x + (source.x + source.width) * canvas->scale_x);
	int y1 = (int)ceilf(canvas->offset_y + (source.y + source.height) * canvas->scale_y);
	if (x0 < 0)
		x0 = 0;
	if (y0 < 0)
		y0 = 0;
	if (x1 > canvas->target_w)
		x1 = canvas->target_w;
	if (y1 > canvas->target_h)
		y1 = canvas->target_h;
	if (x1 <= x0 || y1 <= y0)
		return (AeronRectI) { .width = -1 };
	return (AeronRectI) { x0, y0, x1 - x0, y1 - y0 };
}

void TieScene2dCanvas_Begin(TieScene2dCanvas* canvas, AeronDrawList2D* list, int viewport_w, int viewport_h) {
	memset(canvas, 0, sizeof *canvas);
	canvas->list = list;
	canvas->viewport_w = viewport_w;
	canvas->viewport_h = viewport_h;
	canvas->source_w = CLASSIC_FB_W;
	canvas->source_h = CLASSIC_FB_H;
	canvas->source_pixel_aspect = TIE_SCENE2D_VIEWPORT_PIXEL_ASPECT_VGA_4_3;
	canvas->scale_x = 1.0f;
	canvas->scale_y = 1.0f;
	canvas->target_w = viewport_w;
	canvas->target_h = viewport_h;
}

void TieScene2dCanvas_SetSourceAspect(TieScene2dCanvas* canvas, int source_w, int source_h,
									  uint8_t source_pixel_aspect) {
	if (!canvas || source_w <= 0 || source_h <= 0)
		return;
	canvas->source_w = source_w;
	canvas->source_h = source_h;
	canvas->source_pixel_aspect = source_pixel_aspect;
}

void TieScene2dCanvas_SetOutputTransform(TieScene2dCanvas* canvas, float offset_x, float offset_y,
										 float scale_x, float scale_y, int target_w, int target_h) {
	if (!canvas || scale_x <= 0.0f || scale_y <= 0.0f || target_w <= 0 || target_h <= 0)
		return;
	canvas->offset_x = offset_x;
	canvas->offset_y = offset_y;
	canvas->scale_x = scale_x;
	canvas->scale_y = scale_y;
	canvas->target_w = target_w;
	canvas->target_h = target_h;
}

void TieScene2dCanvas_SetScissor(TieScene2dCanvas* canvas, AeronRectI scissor) {
	if (canvas)
		canvas->scissor = scissor;
}

void TieScene2dCanvas_AddSprite(TieScene2dCanvas* canvas, const AeronDrawList2DSprite* sprite) {
	if (!canvas || !canvas->list || !sprite || sprite->dst_w <= 0.0f || sprite->dst_h <= 0.0f)
		return;
	AeronDrawList2DSprite transformed = *sprite;
	transformed.dst_x = canvas->offset_x + sprite->dst_x * canvas->scale_x;
	transformed.dst_y = canvas->offset_y + sprite->dst_y * canvas->scale_y;
	transformed.dst_w = sprite->dst_w * canvas->scale_x;
	transformed.dst_h = sprite->dst_h * canvas->scale_y;
	transformed.trap_top_dx_left_px *= canvas->scale_x;
	transformed.trap_top_dx_right_px *= canvas->scale_x;
	transformed.scissor = TieScene2dCanvas_TransformScissor(canvas, canvas->scissor);
	if (transformed.scissor.width < 0)
		return;
	AeronDrawList_AddSprite(canvas->list, &transformed);
}

void TieScene2dCanvas_AddQuad4(TieScene2dCanvas* canvas, const AeronDrawList2DQuad4* quad) {
	if (!canvas || !canvas->list || !quad || !quad->texture)
		return;
	AeronDrawList2DQuad4 transformed = *quad;
	for (int corner = 0; corner < 4; ++corner) {
		transformed.corners[corner][0] = canvas->offset_x + quad->corners[corner][0] * canvas->scale_x;
		transformed.corners[corner][1] = canvas->offset_y + quad->corners[corner][1] * canvas->scale_y;
	}
	transformed.scissor = TieScene2dCanvas_TransformScissor(canvas, canvas->scissor);
	if (transformed.scissor.width < 0)
		return;
	AeronDrawList_AddQuad4(canvas->list, &transformed);
}

void TieScene2dCanvas_AddFill(TieScene2dCanvas* canvas, float x, float y, float w, float h,
							  const float rgba[4], AeronBlit2DBlend blend) {
	if (!canvas || !canvas->list || !rgba || w <= 0.0f || h <= 0.0f)
		return;
	AeronRectI scissor = TieScene2dCanvas_TransformScissor(canvas, canvas->scissor);
	if (scissor.width < 0)
		return;
	AeronDrawList_AddFill(canvas->list, canvas->offset_x + x * canvas->scale_x,
						  canvas->offset_y + y * canvas->scale_y, w * canvas->scale_x, h * canvas->scale_y,
						  rgba, blend, &scissor);
}
