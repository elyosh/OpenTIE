/*
 * 4:3 letterbox transform — see viewport.h. Extracted from
 * manifest.c so layout modules can call it without pulling the whole
 * manifest module and its directory-scanning dependency chain.
 */

#include "tie_remaster/scene2d/viewport.h"

#include <limits.h>
#include <stdint.h>
#include <string.h>

int TieScene2dViewport_ComputeFullHeightRect(int bounds_w, int bounds_h, int aspect_w, int aspect_h,
											 TieScene2dViewportRect* out) {
	int64_t width;

	if (!out || bounds_w <= 0 || bounds_h <= 0 || aspect_w <= 0 || aspect_h <= 0)
		return 0;
	width = ((int64_t)bounds_h * aspect_w + aspect_h / 2) / aspect_h;
	if (width <= 0 || width > INT_MAX) {
		memset(out, 0, sizeof *out);
		return 0;
	}
	out->x = (bounds_w - (int)width) / 2;
	out->y = 0;
	out->w = (int)width;
	out->h = bounds_h;
	return 1;
}

int TieScene2dViewport_ComputeXformForSourceAspect(int window_w, int window_h, int source_w, int source_h,
												   uint8_t source_pixel_aspect,
												   TieScene2dViewportTransform* vx) {
	TieScene2dViewportRect region;

	if (!vx)
		return 0;
	memset(vx, 0, sizeof *vx);
	if (source_w <= 0 || source_h <= 0)
		return 0;
	if (!TieScene2dViewport_ComputeFullHeightRect(window_w, window_h, 4, 3, &region))
		return 0;
	vx->viewport_w = window_w;
	vx->viewport_h = window_h;
	vx->source_w = source_w;
	vx->source_h = source_h;
	vx->source_pixel_aspect = source_pixel_aspect;
	vx->region_w = region.w;
	vx->region_h = region.h;
	vx->region_x = region.x;
	vx->region_y = region.y;
	vx->scale_x = (float)region.w / (float)source_w;
	vx->scale_y = (float)region.h / (float)source_h;
	return 1;
}

int TieScene2dViewport_ComputeXformForSource(int window_w, int window_h, int source_w, int source_h,
											 TieScene2dViewportTransform* vx) {
	const uint8_t aspect = source_w == CLASSIC_FB_W && source_h == CLASSIC_FB_H
							   ? TIE_SCENE2D_VIEWPORT_PIXEL_ASPECT_VGA_4_3
							   : TIE_SCENE2D_VIEWPORT_PIXEL_ASPECT_SQUARE;
	return TieScene2dViewport_ComputeXformForSourceAspect(window_w, window_h, source_w, source_h, aspect, vx);
}

int TieScene2dViewport_ComputeXform(int window_w, int window_h, TieScene2dViewportTransform* vx) {
	return TieScene2dViewport_ComputeXformForSource(window_w, window_h, CLASSIC_FB_W, CLASSIC_FB_H, vx);
}

int TieScene2dViewport_MapEdge(int region_origin, int region_extent, int source_extent, int source_edge,
							   int* target_edge) {
	int64_t scaled;
	int64_t rounded;
	int64_t result;

	if (!target_edge || region_extent <= 0 || source_extent <= 0)
		return 0;

	scaled = (int64_t)source_edge * region_extent;
	if (scaled < 0)
		rounded = -((-scaled + source_extent / 2) / source_extent);
	else
		rounded = (scaled + source_extent / 2) / source_extent;
	result = (int64_t)region_origin + rounded;
	if (result < INT_MIN || result > INT_MAX)
		return 0;

	*target_edge = (int)result;
	return 1;
}
