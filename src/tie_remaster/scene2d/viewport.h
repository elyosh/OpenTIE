#ifndef TIE_SCENE2D_VIEWPORT_H
#define TIE_SCENE2D_VIEWPORT_H

#include <stdint.h>

/*
 * Full-height aspect transform inside an arbitrary viewport. Shared by
 * every scene composition module so they all produce destination rects in the same
 * coordinate space. No SDL / no tie_core dependency.
 */

#ifdef __cplusplus
extern "C" {
#endif

/* Classic-frame dimensions. The engine renders into a 320×200 VGA
 * mode-13h framebuffer (16:10 in square pixels, 4:3 on a CRT after the
 * 1.2× vertical pixel-aspect stretch). The application exposes the same
 * coordinate space to the scene composition modules —
 * positions and sizes flow through as classic-px and the viewport
 * transform applies the scale. */
#define CLASSIC_FB_W 320
#define CLASSIC_FB_H 200
/* Aspect-corrected display height — the source-RT pre-warp surface and
 * other 4:3-letterbox-fills-RT contexts size their height as
 * `CLASSIC_DISPLAY_H * hd_mult` so region_w == full RT width and the
 * polygon-warp UV `u = classic_x / CLASSIC_FB_W` lands on the right
 * texels (no pillarbox). */
#define CLASSIC_DISPLAY_H 240

typedef struct TieScene2dViewportRect {
	int x, y;
	int w, h;
} TieScene2dViewportRect;

typedef enum TieScene2dViewportSourcePixelAspect {
	TIE_SCENE2D_VIEWPORT_PIXEL_ASPECT_SQUARE = 0,
	TIE_SCENE2D_VIEWPORT_PIXEL_ASPECT_VGA_4_3 = 1,
} TieScene2dViewportSourcePixelAspect;

typedef struct TieScene2dViewportTransform {
	int viewport_w;
	int viewport_h;
	int source_w, source_h;
	uint8_t source_pixel_aspect;
	int region_x, region_y;
	int region_w, region_h;
	float scale_x, scale_y;
} TieScene2dViewportTransform;

/* Computes a centered rectangle with the requested aspect and the complete
 * bounds height. The width may exceed the bounds and x may be negative. */
int TieScene2dViewport_ComputeFullHeightRect(int bounds_w, int bounds_h, int aspect_w, int aspect_h,
											 TieScene2dViewportRect* out);

/* Compute the full-height 4:3 region of (window_w × window_h), plus
 * the classic→viewport pixel scale.
 *
 * scale_x = region_w / CLASSIC_FB_W, scale_y = region_h / CLASSIC_FB_H;
 * for the canonical 4K target (3840 × 2160 → region 2880 × 2160) this
 * gives (9, 10.8) — the same factor every authored remaster asset
 * assumes.
 *
 * Returns zero for invalid dimensions. */
int TieScene2dViewport_ComputeXform(int window_w, int window_h, TieScene2dViewportTransform* vx);

/* Source-size-aware form used by Landru presentation. VGA (320x200) and
 * SVGA (640x480) both fill the same 4:3 display region without an
 * intermediate compatibility surface. */
int TieScene2dViewport_ComputeXformForSource(int window_w, int window_h, int source_w, int source_h,
											 TieScene2dViewportTransform* vx);

int TieScene2dViewport_ComputeXformForSourceAspect(int window_w, int window_h, int source_w, int source_h,
												   uint8_t source_pixel_aspect,
												   TieScene2dViewportTransform* vx);

/* Map one integer source-space edge into a target region. Rounding is
 * performed with 64-bit integer arithmetic so independently emitted
 * rectangles and scissors resolve a shared edge to the same target pixel.
 * Returns zero for invalid extents or an overflowing result. */
int TieScene2dViewport_MapEdge(int region_origin, int region_extent, int source_extent, int source_edge,
							   int* target_edge);

#ifdef __cplusplus
}
#endif

#endif
