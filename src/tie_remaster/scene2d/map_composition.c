/*
 * Brief-map composition.
 *
 * Tiny: emits the HD backdrop sprite. Everything else (grid, lock-
 * box, strip-bg paints, ship-icon cels, reticle silhouettes via
 * AF_REMAP_COLOR-equivalent, readout / paragraph text) flows through
 * the standard engine snapshot channels — paint_cmds / draws_2d /
 * ui_texts — and the application dispatches them through the same
 * compositors every other HD scene uses.
 *
 * Backdrop is the only piece classic doesn't push to the snapshot:
 * it's a one-shot scratch-buffer prep at scene init followed by a
 * pure memcpy each tick (player_Stars_To_Back). Re-creating that
 * in HD requires a textured sprite quad, which this composer emits.
 */

#include "tie_remaster/scene2d/map_composition.h"
#include "tie_remaster/scene2d/viewport.h"

#include <stdint.h>

/* Polygon-warp UV inset (classic-px). Mirrors classic's
 * lrect_Inset_Rect(&src_rect, 32, 16) at the stub_Map_Clipped_Image
 * call in player.c::idraw_Map — the polygon samples the brief widget
 * with these margins shaved off so the rounded console-screen frame
 * doesn't expose the widget's hard rectangular edges. */
#define MAP_POLY_UV_INSET_X 32
#define MAP_POLY_UV_INSET_Y 16

static void TieScene2dMap_ComputeProjectiveQ(const AeronDrawList2DQuad4* quad, float out[4]) {
	const float ax = quad->corners[0][0], ay = quad->corners[0][1];
	const float bx = quad->corners[1][0], by = quad->corners[1][1];
	const float cx = quad->corners[3][0], cy = quad->corners[3][1];
	const float dx = quad->corners[2][0], dy = quad->corners[2][1];
	const float ex = cx - ax, ey = cy - ay;
	const float fx = dx - bx, fy = dy - by;
	const float gx = bx - ax, gy = by - ay;
	const float det = fx * ey - ex * fy;
	const float eps = 1e-6f;
	out[0] = out[1] = out[2] = out[3] = 1.0f;
	if (det > -eps && det < eps)
		return;
	const float t = (fx * gy - gx * fy) / det;
	const float s = (ex * gy - ey * gx) / det;
	if (t <= eps || t >= 1.0f - eps || s <= eps || s >= 1.0f - eps)
		return;
	out[0] = 1.0f - t;
	out[1] = 1.0f - s;
	out[2] = s;
	out[3] = t;
}

/* Map TIE_MAP_BG_* → the manifest's backdrop res_name. Bundle's
 * extras chain (PLAYER/EMPIRE → MAP) auto-resolves the name to a
 * concrete KTX2 path. */
static const char* TieScene2dMap_BackdropResourceName(uint8_t bg_kind) {
	switch (bg_kind) {
		case TIE_MAP_BG_STARS:
			return "stars";
		case TIE_MAP_BG_COMBAT:
		case TIE_MAP_BG_COMBAT_DEBRIEF:
			return "cmbtmap2";
		case TIE_MAP_BG_TRAINING:
			return "trnmap2";
		default:
			return NULL;
	}
}

/* Sample palette[0] (engine clear-to-black) for the wipe color, with
 * a dim red bias for combat-A mirroring the engine's
 * lpal_Set_Dest_Pal_Color tint. */
static TieScene2dRgba TieScene2dMap_WipeColor(const TieSnapshot* snap, uint8_t bg_kind) {
	uint32_t argb = snap->palette[0];
	float r = (float)((argb >> 16) & 0xFFu) / 255.0f;
	float g = (float)((argb >> 8) & 0xFFu) / 255.0f;
	float b = (float)(argb & 0xFFu) / 255.0f;
	if (bg_kind == TIE_MAP_BG_COMBAT)
		r *= 0.4f;
	return (TieScene2dRgba) { r, g, b, 1.0f };
}

/* Opaque replacement of the map area clears persistent-target contents and
 * alpha before the PMA backdrop overlay. Dimensions and offsets use classic
 * coordinates and are mapped to either the source or cutscene viewport. */
static void TieScene2dMap_RecordBackdrop(TieScene2dCanvas* canvas, const TieSnapshot* snap,
										 const TieScene2dMapAssets* assets, int classic_w, int classic_h,
										 int offset_x, int offset_y) {
	if (!canvas || !snap || !assets)
		return;
	if (classic_w <= 0 || classic_h <= 0)
		return;
	const TieMapHeader* h = &snap->map;

	TieScene2dViewportTransform vx;
	int x0, y0, x1, y1;
	if (!TieScene2dViewport_ComputeXform(canvas->viewport_w, canvas->viewport_h, &vx) ||
		!TieScene2dViewport_MapEdge(vx.region_x, vx.region_w, CLASSIC_FB_W, offset_x, &x0) ||
		!TieScene2dViewport_MapEdge(vx.region_y, vx.region_h, CLASSIC_FB_H, offset_y, &y0) ||
		!TieScene2dViewport_MapEdge(vx.region_x, vx.region_w, CLASSIC_FB_W, offset_x + classic_w, &x1) ||
		!TieScene2dViewport_MapEdge(vx.region_y, vx.region_h, CLASSIC_FB_H, offset_y + classic_h, &y1))
		return;

	float dx = (float)x0;
	float dy = (float)y0;
	float dw = (float)(x1 - x0);
	float dh = (float)(y1 - y0);

	TieScene2dCanvas_SetScissor(canvas, (AeronRectI) { 0 });

	/* 1. Opaque wipe — covers the whole brief-widget rect. */
	{
		TieScene2dRgba color = TieScene2dMap_WipeColor(snap, h->bg_kind);
		const float rgba[4] = { color.r, color.g, color.b, color.a };
		TieScene2dCanvas_AddFill(canvas, dx, dy, dw, dh, rgba, AERON_BLIT2D_BLEND_NONE);
	}

	/* 2. Backdrop sprite — overlay-style PMA. */
	const char* bg_name = TieScene2dMap_BackdropResourceName(h->bg_kind);
	TieScene2dActorTexture bg_tex = { 0 };
	if (bg_name && assets->resolve_sprite &&
		assets->resolve_sprite(assets->resolve_userdata, bg_name, 0, &bg_tex) && bg_tex.texture) {
		AeronDrawList2DSprite sprite = {
			.texture = bg_tex.texture,
			.src_u1 = 1.0f,
			.src_v1 = 1.0f,
			.dst_x = dx,
			.dst_y = dy,
			.dst_w = dw,
			.dst_h = dh,
			.tint = { 1.0f, 1.0f, 1.0f, 1.0f },
			.filter = AERON_BLIT2D_FILTER_LINEAR,
			.blend = AERON_BLIT2D_BLEND_PMA,
		};
		TieScene2dCanvas_AddSprite(canvas, &sprite);
	}
}

void TieScene2dMapComposition_RecordSource(TieScene2dCanvas* canvas, const TieSnapshot* snap,
										   const TieScene2dMapAssets* assets) {
	if (!canvas || !snap || !assets)
		return;
	const TieMapHeader* h = &snap->map;
	if (!h->active || !h->has_polygon)
		return;
	if (h->src_rect_w <= 0 || h->src_rect_h <= 0)
		return;
	/* Source RT is sized 4:3 (CLASSIC_FB_W*hd × CLASSIC_DISPLAY_H*hd)
	 * so TieScene2dViewport_ComputeXform's letterbox fills the full RT. The
	 * brief widget lives at (0,0)..(src_rect_w, src_rect_h) of the
	 * classic frame; offset=0. */
	TieScene2dMap_RecordBackdrop(canvas, snap, assets, h->src_rect_w, h->src_rect_h, 0, 0);
}

void TieScene2dMapComposition_RecordOverlay(TieScene2dCanvas* canvas, const TieSnapshot* snap,
											const TieScene2dMapAssets* assets, AeronTexture* source_rt_tex,
											int source_rt_w, int source_rt_h) {
	if (!canvas || !snap)
		return;
	const TieMapHeader* h = &snap->map;
	if (!h->active)
		return;

	TieScene2dViewportTransform vx;
	if (!TieScene2dViewport_ComputeXform(canvas->viewport_w, canvas->viewport_h, &vx))
		return;

	if (h->has_polygon) {
		if (!source_rt_tex || source_rt_w <= 0 || source_rt_h <= 0)
			return;
		/* 4-corner free-quad warp. The source RT was prepped (by
		 * map preparation) with the backdrop sprite plus all brief-
		 * widget snapshot records — paint_cmds / draws_2d / ui_texts
		 * tagged TIE_EMIT_TARGET_BRIEF_SOURCE — z-merged in engine
		 * emit order. UV samples the brief-widget area of the source
		 * RT inset by MAP_POLY_UV_INSET_X/Y on each side, mirroring
		 * classic's lrect_Inset_Rect(&src_rect, 32, 16) feeding
		 * stub_Map_Clipped_Image. */
		AeronDrawList2DQuad4 quad = {
			.texture = source_rt_tex,
			.tint = { 1.0f, 1.0f, 1.0f, 1.0f },
			.filter = AERON_BLIT2D_FILTER_LINEAR,
			.blend = AERON_BLIT2D_BLEND_PMA,
		};
		static const int aeron_corner[4] = { 0, 1, 3, 2 };
		for (int i = 0; i < 4; i++) {
			int corner = aeron_corner[i];
			quad.corners[corner][0] = (float)vx.region_x + (float)h->dst_poly_x[i] * vx.scale_x;
			quad.corners[corner][1] = (float)vx.region_y + (float)h->dst_poly_y[i] * vx.scale_y;
		}
		float u0 = (float)MAP_POLY_UV_INSET_X / (float)CLASSIC_FB_W;
		float u1 = (float)(h->src_rect_w - MAP_POLY_UV_INSET_X) / (float)CLASSIC_FB_W;
		float v0 = (float)MAP_POLY_UV_INSET_Y / (float)CLASSIC_FB_H;
		float v1 = (float)(h->src_rect_h - MAP_POLY_UV_INSET_Y) / (float)CLASSIC_FB_H;
		quad.corners[0][2] = u0;
		quad.corners[0][3] = v0;
		quad.corners[1][2] = u1;
		quad.corners[1][3] = v0;
		quad.corners[2][2] = u0;
		quad.corners[2][3] = v1;
		quad.corners[3][2] = u1;
		quad.corners[3][3] = v1;
		TieScene2dMap_ComputeProjectiveQ(&quad, quad.q);
		TieScene2dCanvas_SetScissor(canvas, (AeronRectI) { 0 });
		TieScene2dCanvas_AddQuad4(canvas, &quad);
		return;
	}

	/* Rect path: emit backdrop directly into the cutscene RT at the
	 * dst rect. Engine's brief-widget paint_cmds / draws_2d /
	 * ui_texts merge-dispatch on top via the existing channels. */
	if (!assets)
		return;
	if (h->src_rect_w <= 0 || h->src_rect_h <= 0)
		return;
	TieScene2dMap_RecordBackdrop(canvas, snap, assets, h->src_rect_w, h->src_rect_h, h->dst_rect_x,
								 h->dst_rect_y);
}
