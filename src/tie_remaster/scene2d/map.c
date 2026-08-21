/* Brief-map renderer. Polygon maps render their backdrop and widget records
 * into an intermediate 4:3 target, then warp that target into the cutscene.
 * Rectangular maps record directly into the cutscene target. */

#include "tie_remaster/scene2d/map.h"
#include "aeron/scene/image_cache.h"
#include "tie_remaster/gpu_debug.h"
#include "tie_remaster/scene2d/cutscene.h"
#include "tie_remaster/scene2d/map_composition.h"
#include "tie_remaster/scene2d/snapshot_merge.h"
#include "tie_remaster/scene2d/text.h"
#include "tie_remaster/scene2d/viewport.h" /* CLASSIC_FB_W / CLASSIC_DISPLAY_H */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct TieScene2dMapRenderer {
	AeronTextureFormat source_format; /* source RT format */
	AeronRenderTarget* source_rt;
	int source_w, source_h;
	bool source_valid; /* cleared between scenes */
	AeronDrawList2D* source_list;

	/* Borrowed handles — cutscene supplies the manifest, asset cache
	 * and remaster_dir for the backdrop sprite resolve; text_renderer
	 * drives the brief-widget text dispatched onto the source RT (poly
	 * path only). Both are NULL-safe. */
	TieScene2dCutscene* cutscene;
	TieScene2dTextRenderer* text_renderer;

	/* Active bundle for the current snapshot's (lfd, film) tag.
	 * Resolved by TieScene2dMap_Prep, consumed by TieScene2dMap_RecordOverlay
	 * later in the same frame so the rect path's overlay emit can
	 * reach the manifest without redoing the lookup. NULL when no
	 * bundle is active (composer is then a no-op). */
	const TieScene2dFilmBundle* frame_bundle;
};

/* Resolver context passed via assets->resolve_userdata for one compose
 * call. Carries the per-call AeronCommandBuffer (which differs
 * between TieScene2dMap_Prep and TieScene2dMap_RecordOverlay) without stashing it
 * on the long-lived TieScene2dMapRenderer struct. The bundle ref lives on TieScene2dMapRenderer so
 * draw_overlay can reuse prep's lookup. */
typedef struct TieScene2dMapResolveContext {
	TieScene2dMapRenderer* g;
	AeronCommandBuffer* cmd;
} TieScene2dMapResolveContext;

/* ===== Helpers ===== */

/* (Re)create the source RT to (w, h). Returns false on failure. */
static bool TieScene2dMap_ResizeSourceRt(TieScene2dMapRenderer* g, int w, int h) {
	if (g->source_rt && g->source_w == w && g->source_h == h)
		return true;
	if (g->source_rt) {
		Aeron_DestroyRenderTarget(g->source_rt);
		g->source_rt = NULL;
		g->source_valid = false;
	}
	g->source_rt = Aeron_CreateRenderTarget(&(AeronRenderTargetDesc) {
		.width = w,
		.height = h,
		.format = g->source_format,
		.sample_count = AERON_SAMPLE_COUNT_1,
	});
	if (!g->source_rt) {
		Aeron_RequestFatalRendererError("briefing map target creation");
		return false;
	}
	g->source_w = w;
	g->source_h = h;
	return true;
}

/* ===== Public API ===== */

TieScene2dMapRenderer* TieScene2dMap_Init(AeronCommandBuffer* cmd, AeronTextureFormat target_format,
										  TieScene2dCutscene* cutscene,
										  TieScene2dTextRenderer* text_renderer) {
	if (!cmd)
		return NULL;
	TieScene2dMapRenderer* g = (TieScene2dMapRenderer*)calloc(1, sizeof *g);
	if (!g) {
		Aeron_RequestFatalRendererError("map renderer allocation");
		return NULL;
	}
	/* Keep the intermediate in the same color space as the cutscene target. */
	g->source_format = target_format;
	g->source_list =
		AeronDrawList_Create(3 * TIE_MAX_UI_TEXTS * TIE_UI_TEXT_MAX_CHARS + 9 * TIE_MAX_PAINT_CMDS +
							 2 * TIE_MAX_DRAWS_2D + TIE_MAX_TITLE_CRAWL_LINES);
	if (!g->source_list) {
		Aeron_RequestFatalRendererError("map renderer resource creation");
		TieScene2dMap_Shutdown(g);
		return NULL;
	}
	g->cutscene = cutscene;
	g->text_renderer = text_renderer;
	return g;
}

void TieScene2dMap_Shutdown(TieScene2dMapRenderer* g) {
	if (!g)
		return;
	if (g->source_rt)
		Aeron_DestroyRenderTarget(g->source_rt);
	AeronDrawList_Destroy(g->source_list);
	free(g);
}

/* Backdrop-sprite resolver passed to map composition. Resolves the bg
 * res_name (stars / cmbtmap2 / trnmap2) through the active bundle's
 * manifest + extras chain to a loaded GPU texture. Sprite-only — the
 * composer samples the full texture, no atlas frame. */
static bool TieScene2dMap_ResolveBackdropSprite(void* userdata, const char* res_name, int cur_cel,
												TieScene2dActorTexture* out_tex) {
	TieScene2dMapResolveContext* ctx = (TieScene2dMapResolveContext*)userdata;
	if (!ctx || !ctx->g || !res_name || !out_tex)
		return false;
	if (!ctx->g->frame_bundle || !ctx->g->cutscene)
		return false;

	const TieScene2dActorEntry* e;
	const TieScene2dAssetVariant* av;
	if (!TieScene2dManifest_ResolveVariant(ctx->g->frame_bundle, res_name, -1, cur_cel, &e, &av))
		return false;
	if (!av->asset_path[0])
		return false;

	AeronImageCache* cache = TieScene2dCutscene_Assets(ctx->g->cutscene);
	if (!cache)
		return false;
	const AeronImageCacheEntry* t = Aeron_ImageCacheLoad(cache, ctx->cmd, av->asset_path);
	if (!t || !t->tex)
		return false;

	*out_tex = (TieScene2dActorTexture) {
		.texture = t->tex,
		.tex_w = t->w,
		.tex_h = t->h,
	};
	return true;
}

/* Pre-touch every actor variant declared by the active bundle so the
 * texture cache is primed before any render pass opens. Brief-widget
 * actors (icons in unused side colors, e.g. iconsred when the player
 * is empire) aren't necessarily in this frame's views[], and an in-
 * pass cache miss can't issue an upload (no copy passes inside a
 * render pass) — those records would silently drop. Iterating the
 * full bundle is cheap: most entries are already cached from
 * TieScene2dCutscene_PrepUploads' views walk. */
static void TieScene2dMap_PrefetchBundleAssets(TieScene2dMapRenderer* g, AeronCommandBuffer* cmd) {
	if (!g || !g->frame_bundle || !g->cutscene)
		return;
	AeronImageCache* cache = TieScene2dCutscene_Assets(g->cutscene);
	if (!cache)
		return;
	for (int i = 0; i < g->frame_bundle->actor_count; i++) {
		const TieScene2dActorEntry* e = &g->frame_bundle->actors[i];
		if (e->hide)
			continue;
		for (int v = 0; v < e->variant_count; v++) {
			const TieScene2dAssetVariant* av = &e->variants[v];
			if (!av->asset_path[0])
				continue;
			(void)Aeron_ImageCacheLoad(cache, cmd, av->asset_path);
		}
	}
}

/* Build the TieScene2dMapAssets descriptor for one compose call. The
 * resolve_userdata pointer points at a stack TieScene2dMapResolveContext the caller
 * scopes around the TieScene2dMapComposition_Record* invocation. */
static void TieScene2dMap_BuildComposeAssets(TieScene2dMapResolveContext* ctx, TieScene2dMapAssets* out) {
	memset(out, 0, sizeof *out);
	out->resolve_sprite = TieScene2dMap_ResolveBackdropSprite;
	out->resolve_userdata = ctx;
}

bool TieScene2dMap_Prep(TieScene2dMapRenderer* g, AeronCommandBuffer* cmd, const TieSnapshot* snap,
						const TieScene2dActorView* views, int nviews, const char* lfd, const char* film,
						int cur_cel) {
	if (!g || !cmd || !snap)
		return false;
	g->source_valid = false;
	g->frame_bundle = NULL;
	const TieMapHeader* h = &snap->map;
	if (!h->active)
		return true;
	if (h->src_rect_w <= 0 || h->src_rect_h <= 0)
		return true;

	/* Resolve the active manifest bundle for this snapshot tag —
	 * stashed on TieScene2dMapRenderer so TieScene2dMap_RecordOverlay can reuse the lookup
	 * (rect path's overlay backdrop emit needs it later this frame).
	 * NULL → composer is a no-op. */
	if (g->cutscene)
		g->frame_bundle =
			TieScene2dCutscene_FindBundle(g->cutscene, snap->current_lfd_basename, snap->current_film_name);

	/* Pre-touch every bundle asset so the texture cache is primed
	 * before any render pass opens. Brief widget icons in side
	 * colors not yet drawn (e.g. iconsred while iconsgrn renders)
	 * would silently drop on an in-pass cache miss — copy passes
	 * can't run inside an active render pass. */
	TieScene2dMap_PrefetchBundleAssets(g, cmd);

	/* Source-RT prep is only needed for the polygon-warp path
	 * (SCENE_BRIEF). Non-poly scenes render content directly into the
	 * cutscene RT during the overlay pass — no intermediate texture,
	 * native cutscene-RT resolution. */
	if (!h->has_polygon)
		return true;

	/* Source RT sized 4:3 so TieScene2dViewport_ComputeXform's 4:3 letterbox
	 * fills the full RT (no pillarbox). With a 16:10 RT paint and actor
	 * composition would pillarbox content at the X edges, but the
	 * polygon-warp UV samples 0..1 of the texture as if it were a
	 * direct CLASSIC_FB_W×CLASSIC_FB_H framebuffer — which over-
	 * samples the letterbox columns and visually stretches the brief-
	 * buffer's X axis. Using CLASSIC_FB_W×hd × CLASSIC_DISPLAY_H×hd
	 * (= 4:3) places region_w = full RT width: classic-x maps to
	 * HD-x = N × hd directly, so the polygon's `u = classic_x /
	 * CLASSIC_FB_W` UV maps to the right texels.
	 *
	 * Vertical scale becomes hd × 1.2, matching the classic 320×200 →
	 * 4:3 display aspect correction (square-pixel monitors stretch
	 * 200 lines to 240). */
	int hd = MAP_HD_MULT_DEFAULT;
	int hd_w = CLASSIC_FB_W * hd;
	int hd_h = CLASSIC_DISPLAY_H * hd;
	if (!TieScene2dMap_ResizeSourceRt(g, hd_w, hd_h))
		return false;

	TieScene2dMapResolveContext ctx = { g, cmd };
	TieScene2dMapAssets assets;
	TieScene2dMap_BuildComposeAssets(&ctx, &assets);
	const float clear[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
	AeronDrawList_Begin(g->source_list, g->source_rt, hd_w, hd_h, AERON_DRAWLIST2D_CLEAR, clear);

	/* 1. Backdrop sprite — drained first so the engine emits stamp
	 *    on top in the natural Painter's-algorithm order. */
	TieScene2dCanvas source_canvas;
	TieScene2dCanvas_Begin(&source_canvas, g->source_list, hd_w, hd_h);
	TieScene2dMapComposition_RecordSource(&source_canvas, snap, &assets);

	/* 2. Engine emits stamped TIE_EMIT_TARGET_BRIEF_SOURCE — the
	 *    classic engine drew these into the brief_buffer scratch
	 *    canvas so the stub_Map_Clipped_Image warp could read them
	 *    back; HD draws them onto the same source RT so the polygon-
	 *    warp quad later samples a single composited image. The
	 *    merge dispatcher walks the three channels in cross-channel
	 *    z order so paint highlight rects layer underneath the icons
	 *    that follow them (target reticles in player_Draw_Display_
	 *    Ship), with same-channel batching keeping draw-call counts
	 *    low. No map-quad slot here — the source RT IS the brief-map
	 *    source. */
	TieScene2dSnapshotMergeDispatch cfg = (TieScene2dSnapshotMergeDispatch) {
		.views = views,
		.view_count = nviews,
		.ui_texts = snap->ui_texts,
		.ui_text_count = (int)snap->ui_text_count,
		.paint_cmds = snap->paint_cmds,
		.paint_cmd_count = (int)snap->paint_cmd_count,
		.palette = snap->palette,
		.source_w = CLASSIC_FB_W,
		.source_h = CLASSIC_FB_H,
		.accept_target = TIE_EMIT_TARGET_BRIEF_SOURCE,
		.cutscene = g->cutscene,
		.text_renderer = g->text_renderer,
		.lfd = lfd,
		.film = film,
		.cur_cel = cur_cel,
		.emit_map_quad = NULL,
	};
	TieScene2dSnapshotDispatch_Run(g->source_list, hd_w, hd_h, &cfg);
	if (!AeronDrawList_Prepare(g->source_list, cmd)) {
		Aeron_RequestFatalRendererError("briefing map draw-list preparation");
		return false;
	}
	AeronDrawList_Render(g->source_list, cmd);

	g->source_valid = true;
	return true;
}

void TieScene2dMap_RecordOverlay(TieScene2dMapRenderer* g, AeronDrawList2D* list, int viewport_w,
								 int viewport_h, const TieSnapshot* snap) {
	if (!g || !list || !snap)
		return;
	if (!snap->map.active)
		return;
	if (viewport_w <= 0 || viewport_h <= 0)
		return;

	/* Polygon path requires the source RT prepped by TieScene2dMap_Prep;
	 * rect path skips the source RT and emits content directly. */
	bool poly = snap->map.has_polygon != 0;
	if (poly && (!g->source_valid || !g->source_rt))
		return;

	/* `cmd` here is inside an active render pass: Aeron_ImageCache
	 * load can't issue a copy pass — first-touch cache misses silently
	 * drop. TieScene2dMap_Prep ran earlier this frame (before any render
	 * pass opened) and prefetched the bundle, so steady-state hits
	 * succeed. The bundle ref was stashed on TieScene2dMapRenderer by prep too — no
	 * need to re-resolve here. */

	TieScene2dMapResolveContext ctx = { g, NULL };
	TieScene2dMapAssets assets;
	TieScene2dMap_BuildComposeAssets(&ctx, &assets);
	TieScene2dCanvas overlay_canvas;
	TieScene2dCanvas_Begin(&overlay_canvas, list, viewport_w, viewport_h);
	TieScene2dMapComposition_RecordOverlay(&overlay_canvas, snap, &assets,
										   poly ? Aeron_RenderTargetGetTexture(g->source_rt) : NULL,
										   poly ? g->source_w : 0, poly ? g->source_h : 0);
}
