/*
 * Aeron text renderer.
 *
 * Storage: one sampled AeronTexture for the atlas, plus an in-memory
 * copy of the .fnt header + per-glyph records keyed by `first_char`.
 *
 * Draw: per active text record, walk the string, emit retained sprites
 * per glyph (and right + below shadow blits when the record has
 * shadow on) using Aeron's PMA sprite pipeline. No CPU-side glyph layout cache — typical UI
 * frame stays well under the GPU draw-call budget.
 */

#include "tie_remaster/scene2d/text.h"

#include "tie_remaster/scene2d/manifest_internal.h" /* TieScene2dViewportTransform + TieScene2dViewport_ComputeXform */
#include "tie_remaster/scene2d/srgb_math.h"
#include "tie_remaster/scene2d/text_layout.h"

#include "aeron/scene/font_atlas.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* One font's data — texture + per-glyph metrics. Loading (TFNT v2 +
 * PNG → sampled texture + metrics) moved to aeron_scene (C3b). */
typedef AeronFontGlyph TieScene2dTextGlyphMetric;
typedef AeronFontAtlas TieScene2dTextFontAtlas;

#define MAX_FONTS 7 /* Landru 0..3, title 4, cockpit 5..6. */

/* Title-crawl per-line flat-render slot. Each visible crawl line gets
 * one slot in the offscreen text RT (`title_rt`). Glyphs render at
 * atlas-native size into the slot during the prep pass; the draw pass
 * blits each slot as a trapezoidal quad with projective UV, sampling
 * the slot's sub-rect through the trap shader. */
/* Title-crawl per-line slot in the offscreen text RT.
 * Captured once at scene start by prep_title_crawl; consumed every
 * frame by draw_title_crawl. The slot's `initial_y` is the line's
 * classic-coord starting position from the snapshot (constant for
 * the entire scene); current y at any time = initial_y - elapsed
 * engine frames since `crawl_start_us`. */
typedef struct TieScene2dTextTitleCrawlSlot {
	float initial_y;   /* line's start y in classic px (constant) */
	int pen_atlas_w;   /* used horizontal extent in atlas px */
	int strip_atlas_h; /* slot height in atlas px (== font cell_h) */
	int slot_y_atlas;  /* top of slot in offscreen RT (atlas px) */
	uint8_t font_id;
	uint8_t _pad[3];
} TieScene2dTextTitleCrawlSlot;

/* Sized for the worst-case crawl line: ~320 classic-px wide × 9 atlas
 * scale = 2880 atlas px + a cell-width tail margin (last glyph's cell
 * extends past its advance). 4096 covers all retail title paragraphs.
 * Vertical: 18 lines × ~150 atlas px (font8 cell_h) = 2700. */
#define TITLE_RT_W 4096
#define TITLE_RT_H 4096
#define TITLE_MAX_SLOTS TIE_MAX_TITLE_CRAWL_LINES
/* SCENE_TITLE always runs at frame_rate=20 (PIT ticks/frame, 4 ms
 * each → 80 ms per engine frame); the crawl scrolls 1 classic px
 * per engine frame. The application drives this from its own wall clock
 * for smooth motion regardless of host framerate. */
#define TITLE_FRAME_BUDGET_US 80000ull

struct TieScene2dTextRenderer {
	AeronTextureFormat target_format;
	TieScene2dTextFontAtlas fonts[MAX_FONTS];

	/* Offscreen text RT for the title crawl. Lazily created on
	 * first prep call. Format matches `target_format` so the same
	 * blit pipeline can sample it. */
	AeronRenderTarget* title_rt;
	AeronDrawList2D* title_bake_list;
	int title_rt_w;
	int title_rt_h;

	/* Cached scene state. The application tracks the scene_tag of the
	 * last prepped scene; when prep is called with a different tag
	 * (or the snap has no crawl), slots are re-rendered (or cleared)
	 * and the start timestamp captured. Between transitions, prep
	 * is a no-op. */
	char cached_scene_tag[64];
	uint64_t crawl_start_us;
	TieScene2dTextTitleCrawlSlot slots[TITLE_MAX_SLOTS];
	int slot_count;

	/* Reused across submit() calls — the array lets the composition
	 * layer treat fonts as a flat TieScene2dFontAtlas[]. */
	TieScene2dFontAtlas compose_fonts[MAX_FONTS];

	/* Optional override for the title-crawl tint. When active, the
	 * binary's per-line palette ramp is bypassed and each line is
	 * tinted with (crawl_tint_r/g/b) scaled by a depth-based
	 * brightness factor that keeps the near-bright / far-dim
	 * perspective cue. Channel values are linear-RGB in [0, 1]. */
	bool crawl_tint_override;
	float crawl_tint_r;
	float crawl_tint_g;
	float crawl_tint_b;
};

/* Returns the loaded atlas for `font_id`, falling back to font 0 if
 * the requested id isn't loaded. NULL only if no fonts are loaded. */
static int TieScene2dText_FontSlot(uint8_t domain, uint8_t font_id) {
	switch ((TieFontDomain)domain) {
		case TIE_FONT_DOMAIN_TITLE:
			return 4;
		case TIE_FONT_DOMAIN_COCKPIT:
			return font_id == 4 ? 6 : 5;
		case TIE_FONT_DOMAIN_LANDRU:
		default:
			return font_id;
	}
}

static const TieScene2dTextFontAtlas* TieScene2dTextRenderer_PickFont(const TieScene2dTextRenderer* g,
																	  uint8_t domain, uint8_t font_id) {
	int slot = TieScene2dText_FontSlot(domain, font_id);
	if (slot >= 0 && slot < MAX_FONTS && g->fonts[slot].loaded)
		return &g->fonts[slot];
	if (g->fonts[0].loaded)
		return &g->fonts[0];
	return NULL;
}

TieScene2dTextRenderer* TieScene2dTextRenderer_Init(AeronCommandBuffer* cmd, AeronTextureFormat target_format,
													const char* atlas_basename) {
	if (!cmd)
		return NULL;
	TieScene2dTextRenderer* g = (TieScene2dTextRenderer*)calloc(1, sizeof(TieScene2dTextRenderer));
	if (!g) {
		Aeron_RequestFatalRendererError("text renderer allocation");
		return NULL;
	}
	g->target_format = target_format;

	if (atlas_basename && !AeronFontAtlas_Load(&g->fonts[0], cmd, atlas_basename)) {
		free(g);
		return NULL;
	}

	g->title_bake_list = AeronDrawList_Create(TIE_MAX_TITLE_CRAWL_LINES * TIE_TITLE_CRAWL_MAX_CHARS);
	if (!g->title_bake_list) {
		Aeron_RequestFatalRendererError("text renderer resource creation");
		TieScene2dTextRenderer_Shutdown(g);
		return NULL;
	}
	return g;
}

bool TieScene2dTextRenderer_LoadFont(TieScene2dTextRenderer* g, AeronCommandBuffer* cmd, uint8_t font_id,
									 const char* atlas_basename) {
	if (!g || !cmd || font_id >= MAX_FONTS)
		return false;
	/* Caller already supplied a primary in init; if they re-supply
	 * the same slot here, free the previous load first. */
	TieScene2dTextFontAtlas* slot = &g->fonts[font_id];
	if (slot->loaded)
		AeronFontAtlas_Release(slot);
	return AeronFontAtlas_Load(slot, cmd, atlas_basename) != 0;
}

bool TieScene2dTextRenderer_LoadFontRgba8(TieScene2dTextRenderer* g, AeronCommandBuffer* cmd, uint8_t font_id,
										  const AeronFontAtlasRgba8Desc* desc) {
	if (!g || !cmd || font_id >= MAX_FONTS || !desc)
		return false;
	TieScene2dTextFontAtlas* slot = &g->fonts[font_id];
	if (slot->loaded)
		AeronFontAtlas_Release(slot);
	return AeronFontAtlas_InitRgba8(slot, cmd, desc);
}

bool TieScene2dTextRenderer_LoadFontVfs(TieScene2dTextRenderer* g, AeronCommandBuffer* cmd, uint8_t font_id,
										AeronVfs* vfs, AeronVfsRoot root, const char* atlas_basename,
										size_t maximum_file_size) {
	if (!g || !cmd || font_id >= MAX_FONTS || !vfs || !atlas_basename)
		return false;
	TieScene2dTextFontAtlas* slot = &g->fonts[font_id];
	if (slot->loaded)
		AeronFontAtlas_Release(slot);
	return AeronFontAtlas_LoadVfs(slot, cmd, vfs, root, atlas_basename, maximum_file_size) != 0;
}

void TieScene2dTextRenderer_SetCrawlTint(TieScene2dTextRenderer* g, bool active, float r, float green,
										 float b) {
	if (!g)
		return;
	g->crawl_tint_override = active;
	g->crawl_tint_r = r;
	g->crawl_tint_g = green;
	g->crawl_tint_b = b;
}

const TieScene2dFontAtlas* TieScene2dTextRenderer_ComposeFonts(const TieScene2dTextRenderer* g,
															   int* out_count) {
	if (out_count)
		*out_count = g ? MAX_FONTS : 0;
	return g ? g->compose_fonts : NULL;
}

static void TieScene2dText_PopulateComposeFonts(TieScene2dTextRenderer* g);

float TieScene2dTextRenderer_MeasureFestringClassic(TieScene2dTextRenderer* g, uint8_t font_domain,
													uint8_t font_id, const TieScene2dTextSpace* space,
													const char* text) {
	if (!g || !text || !text[0])
		return 0.0f;
	/* Refresh the compose view of the loaded fonts — populate is
	 * the same call sites use before emitting; cheap. */
	TieScene2dText_PopulateComposeFonts(g);
	const int slot = TieScene2dText_FontSlot(font_domain, font_id);
	if (slot < 0 || slot >= MAX_FONTS)
		return 0.0f;
	const TieScene2dFontAtlas* fa = &g->compose_fonts[slot];
	if (!fa->loaded)
		return 0.0f;
	return TieScene2dText_MeasureFestringClassic(fa, space, text);
}

void TieScene2dTextRenderer_Shutdown(TieScene2dTextRenderer* g) {
	if (!g)
		return;
	for (int i = 0; i < MAX_FONTS; i++)
		AeronFontAtlas_Release(&g->fonts[i]);
	if (g->title_rt)
		Aeron_DestroyRenderTarget(g->title_rt);
	AeronDrawList_Destroy(g->title_bake_list);
	free(g);
}

/* Build the compose-layer view of the loaded font slots. TieScene2dTextGlyphMetric
 * is layout-equivalent to TieScene2dGlyphMetric (same uint16_t fields in
 * the same order), so we can share the underlying array via a cast.
 * Cheap to rebuild every call; ~32 store instructions for 4 slots. */
static void TieScene2dText_PopulateComposeFonts(TieScene2dTextRenderer* g) {
	for (int i = 0; i < MAX_FONTS; i++) {
		const TieScene2dTextFontAtlas* src = &g->fonts[i];
		TieScene2dFontAtlas* dst = &g->compose_fonts[i];
		dst->texture = src->texture;
		dst->atlas_w = src->atlas_w;
		dst->atlas_h = src->atlas_h;
		dst->cell_w = src->cell_w;
		dst->cell_h = src->cell_h;
		dst->first_char = src->first_char;
		dst->num_chars = src->num_chars;
		dst->glyphs = (const TieScene2dGlyphMetric*)src->glyphs;
		dst->loaded = src->loaded;
	}
}

/* ---------- Title crawl (Option B: flat-text RT + projective blit) ----------
 *
 * Each visible line is rendered FLAT into a slot of the offscreen
 * `title_rt` (one row of glyphs, no perspective, atlas-native size).
 * The cutscene RT pass then blits each slot as a single trapezoidal
 * quad with projective `position.w`, so the GPU's perspective-correct
 * varying interpolation produces hyperbolic UV sampling — matching
 * exactly what a tilted 3D plane projection would give.
 *
 * Single bilinear sample per fragment of fully-antialiased text =
 * cleaner than per-glyph trap from atlas. */
#define TITLE_CRAWL_STRIP_H_CLASSIC 20.0f

/* Allocate the offscreen text RT on demand. Format matches the host
 * cutscene RT format so the same blit pipeline can sample it. */
static bool TieScene2dText_EnsureTitleRt(TieScene2dTextRenderer* g) {
	if (g->title_rt)
		return true;
	g->title_rt = Aeron_CreateRenderTarget(&(AeronRenderTargetDesc) {
		.width = TITLE_RT_W,
		.height = TITLE_RT_H,
		.format = g->target_format,
		.sample_count = AERON_SAMPLE_COUNT_1,
	});
	if (!g->title_rt) {
		Aeron_RequestFatalRendererError("title crawl target creation");
		return false;
	}
	g->title_rt_w = TITLE_RT_W;
	g->title_rt_h = TITLE_RT_H;
	return true;
}

/* Scene-tag-driven one-shot prep. Re-rendering only happens on a
 * scene_tag change (or initial entry into a title scene); steady-
 * state calls are no-ops. Empty crawl on the snapshot clears the
 * cached state. */
bool TieScene2dTextRenderer_PrepTitleCrawl(TieScene2dTextRenderer* g, AeronCommandBuffer* cmd,
										   const TieSnapshot* snap) {
	if (!g || !cmd)
		return false;

	/* No crawl on snapshot → leave the texture untouched but mark
	 * inactive so draw_title_crawl skips and the next entry into a
	 * title scene re-renders. */
	if (!snap || snap->title_crawl_count == 0) {
		g->slot_count = 0;
		g->cached_scene_tag[0] = '\0';
		return true;
	}

	/* Steady state — same scene as last prep. */
	if (g->cached_scene_tag[0] != '\0' &&
		strncmp(g->cached_scene_tag, snap->scene_tag, sizeof g->cached_scene_tag) == 0)
		return true;

	/* Scene transition: re-render. */
	g->slot_count = 0;
	if (!TieScene2dText_EnsureTitleRt(g))
		return false;
	const TieTitleCrawlLine* lines = snap->title_crawl_lines;
	int line_count = (int)snap->title_crawl_count;

	const float clear[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	AeronDrawList_Begin(g->title_bake_list, g->title_rt, g->title_rt_w, g->title_rt_h, AERON_DRAWLIST2D_CLEAR,
						clear);

	const float atlas_scale_x = 9.0f;
	const float space_between_atlas = atlas_scale_x;

	int slot = 0;
	int next_slot_y = 0; /* running cursor — slots vary in height per font */
	for (int li = 0; li < line_count && slot < TITLE_MAX_SLOTS; li++) {
		const TieTitleCrawlLine* L = &lines[li];
		if (L->text[0] == '\0')
			continue;

		const TieScene2dTextFontAtlas* fa = TieScene2dTextRenderer_PickFont(g, L->font_domain, L->font_id);
		if (!fa || fa->cell_h == 0)
			continue;

		/* Slot height matches the font's cell_h exactly so the rendered
		 * glyphs fill the slot vertically. The blit pass scales this
		 * cell_h to the strip's classic-coord 20 px height — which
		 * effectively magnifies font8 to the apparent height of the
		 * binary's helv-20, matching the original line density. */
		int strip_atlas_h = (int)fa->cell_h;
		if (next_slot_y + strip_atlas_h > g->title_rt_h)
			break; /* RT vertical exhausted */

		/* Render glyphs left-to-right at atlas-native size.
		 *
		 * Atlas glyph cells (atlas_w == cell_w_out from font_extract)
		 * are uniform full-cell width and trail transparent padding
		 * past the ink. The binary-equivalent stride is `advance` (=
		 * widthArray*9), which is what lfont_Get_String_Width uses.
		 * Track right_edge_atlas as the position AFTER each glyph's
		 * advance — matches sum(widthArray)+(n-1)*spaceBetween of the
		 * FB path. Using cell_right (dx + atlas_w) instead would
		 * include the last glyph's trailing transparent pad and
		 * shift the centered trapezoid LEFT by half that pad. */
		float pen_x_atlas = 0.0f;
		float right_edge_atlas = 0.0f;
		for (const char* p = L->text; *p; p++) {
			unsigned ch = (unsigned char)*p;
			if (ch < fa->first_char || ch >= fa->first_char + fa->num_chars)
				continue;
			const TieScene2dTextGlyphMetric* gm = &fa->glyphs[ch - fa->first_char];
			if (gm->advance == 0) {
				pen_x_atlas += space_between_atlas;
				continue;
			}
			if (gm->atlas_w == 0 || gm->atlas_h == 0) {
				pen_x_atlas += (float)gm->advance;
				right_edge_atlas = pen_x_atlas;
				pen_x_atlas += space_between_atlas;
				continue;
			}
			float dx = pen_x_atlas;
			float dy = (float)next_slot_y;
			float dw = (float)gm->atlas_w;
			float dh = (float)gm->atlas_h;

			/* Skip glyphs whose cell would overflow the RT — better to
			 * truncate the line than spill into the next slot. */
			if (dx + dw > (float)g->title_rt_w)
				break;

			AeronDrawList2DSprite sprite = { 0 };
			sprite.texture = fa->texture;
			sprite.dst_x = dx;
			sprite.dst_y = dy;
			sprite.dst_w = dw;
			sprite.dst_h = dh;
			sprite.src_u0 = (float)gm->atlas_x / (float)fa->atlas_w;
			sprite.src_v0 = (float)gm->atlas_y / (float)fa->atlas_h;
			sprite.src_u1 = (float)(gm->atlas_x + gm->atlas_w) / (float)fa->atlas_w;
			sprite.src_v1 = (float)(gm->atlas_y + gm->atlas_h) / (float)fa->atlas_h;
			sprite.tint[0] = sprite.tint[1] = sprite.tint[2] = sprite.tint[3] = 1.0f;
			sprite.blend = AERON_BLIT2D_BLEND_PMA;
			sprite.filter = AERON_BLIT2D_FILTER_LINEAR;
			AeronDrawList_AddSprite(g->title_bake_list, &sprite);

			pen_x_atlas += (float)gm->advance;
			right_edge_atlas = pen_x_atlas;
			pen_x_atlas += space_between_atlas;
		}

		if (right_edge_atlas <= 0.0f)
			continue; /* no glyphs rendered */

		TieScene2dTextTitleCrawlSlot* s = &g->slots[slot];
		s->initial_y = L->initial_y;
		s->pen_atlas_w = (int)(right_edge_atlas + 0.5f);
		if (s->pen_atlas_w > g->title_rt_w)
			s->pen_atlas_w = g->title_rt_w;
		s->strip_atlas_h = strip_atlas_h;
		s->slot_y_atlas = next_slot_y;
		s->font_id = L->font_id;
		s->_pad[0] = s->_pad[1] = s->_pad[2] = 0;
		next_slot_y += strip_atlas_h;
		slot++;
	}
	g->slot_count = slot;

	if (!AeronDrawList_Prepare(g->title_bake_list, cmd)) {
		Aeron_RequestFatalRendererError("title crawl draw-list preparation");
		return false;
	}
	AeronDrawList_Render(g->title_bake_list, cmd);

	/* Capture scene identity + start time. From now on the application
	 * scrolls independently of tie_core. */
	size_t tag_len = 0;
	while (tag_len < sizeof g->cached_scene_tag - 1 && snap->scene_tag[tag_len])
		++tag_len;
	memcpy(g->cached_scene_tag, snap->scene_tag, tag_len);
	g->cached_scene_tag[tag_len] = '\0';
	g->crawl_start_us = snap->sim_time_us;
	return true;
}

void TieScene2dTextRenderer_RecordTitleCrawl(TieScene2dTextRenderer* g, AeronDrawList2D* list, int viewport_w,
											 int viewport_h, const TieSnapshot* snap,
											 const uint32_t* palette) {
	/* Crawl prep already rendered all 18 lines into `title_rt` slots
	 * at scene start. Each frame this call computes per-line current
	 * y from elapsed wall time (independent of tie_core's per-frame
	 * state) and blits each visible slot as a single trapezoidal
	 * quad with projective `position.w`. */
	if (!g || !list || !palette || !snap || !g->title_rt || g->slot_count <= 0)
		return;
	if (viewport_w <= 0 || viewport_h <= 0)
		return;

	/* Engine-frame scroll clock — driven by the engine's own logical
	 * time (view_gbl->time + frame_progress) instead of wall time.
	 * The engine commits at slightly more than 80 ms per frame on a
	 * 60-fps host because of host quantization (commits happen at the
	 * first host tick where elapsed >= budget, not exactly at budget),
	 * so wall-time-derived elapsed_frames was running ~4% ahead of
	 * the engine's actual rate. Using the engine clock makes the
	 * scroll exactly track the binary's pacing. */
	float elapsed_frames = (float)snap->scene_clock.engine_frame + snap->scene_clock.frame_progress;
	/* crawl_start_us is captured at the FIRST emit after scene
	 * transition; that already corresponded to engine_frame=0 for
	 * the new scene because lview_Get_View_Time resets per scene. */
	(void)g->crawl_start_us; /* still tracked but not consulted */

	TieScene2dViewportTransform vx;
	TieScene2dViewport_ComputeXform(viewport_w, viewport_h, &vx);

	const float atlas_scale_x = 9.0f;
	const float inv_rt_w = 1.0f / (float)g->title_rt_w;
	const float inv_rt_h = 1.0f / (float)g->title_rt_h;

	/* Three-phase scroll. Phases 1 and 2a mirror the binary's
	 * user_Title (title.c): linear scroll at 1 classic px / engine
	 * frame while off-screen below, then linear deceleration
	 *   v(t2) = 1 - t2/256,  y(t2) = 200 - t2 + t2²/512
	 * once past y=200 (line_yvf decays from 4080 in steps of 16).
	 *
	 * The binary stops the line at t2=256 (y=72, v=0) and clears
	 * line_used. At VGA the strip is tiny by then so the dead-stop
	 * is invisible, but at HD scale the line visibly freezes near
	 * the top of the screen for ~30 frames before the scene-end
	 * fade. To preserve the binary's apparent speed everywhere it's
	 * actually visible while avoiding that frozen tail, hand off at
	 * t2=192 to an exponential approach toward the y=40 vanishing
	 * point. The handoff is C¹ smooth — at t2=192 the linear-decel
	 * curve has y=80 and v=-0.25, which is exactly what -(y-40)/160
	 * gives, so neither position nor velocity jumps at the seam. */
	const float kPhase2bStart_t2 = 192.0f;
	const float kPhase2bStart_y = 80.0f;
	const float kVanishingY = 40.0f;
	const float kInvAccel = 1.0f / 512.0f;     /* phase-2a: 0.5 / 256 */
	const float kPhase2bDecay = 1.0f / 160.0f; /* phase-2b: v=-(y-40)/160 */

	for (int si = 0; si < g->slot_count; si++) {
		const TieScene2dTextTitleCrawlSlot* s = &g->slots[si];
		if (s->pen_atlas_w <= 0)
			continue;

		/* Phase 1 (line_y >= 200): linear scroll at 1 classic px per
		 * engine frame. Consecutive lines enter the visible region
		 * 28 frames apart, matching the binary's `28*i` initial offset. */
		float entry_t = s->initial_y - 200.0f;
		float top_y_c;
		if (elapsed_frames < entry_t) {
			top_y_c = s->initial_y - elapsed_frames;
		} else {
			float t2 = elapsed_frames - entry_t;
			if (t2 < kPhase2bStart_t2) {
				top_y_c = 200.0f - t2 + t2 * t2 * kInvAccel;
			} else {
				float dt = t2 - kPhase2bStart_t2;
				top_y_c = kVanishingY + (kPhase2bStart_y - kVanishingY) * expf(-dt * kPhase2bDecay);
			}
		}
		if (top_y_c >= 200.0f)
			continue; /* still off-screen below */

		/* Perspective stretch matches the binary's draw_Title:
		 *   stretch(y) = (y - 40) / 160; vanishing point at y=40,
		 *   full natural scale at y=200. */
		float top_stretch = (top_y_c - 40.0f) / 160.0f;
		if (top_stretch <= 0.0f)
			continue; /* past the vanishing point */
		if (top_stretch > 1.0f)
			top_stretch = 1.0f;

		/* Strip vertical foreshortening: same factor as horizontal,
		 * so the line projects as if from a uniformly-tilted plane.
		 * The strip's classic-coord height tracks the binary's helv-20
		 * cell (20 classic px) — this magnifies the font8 atlas (which
		 * has a smaller native cell) up to helv-20 apparent size and
		 * keeps line spacing density visually equivalent.
		 *
		 * No clamp on bot_stretch: when the trapezoid's bottom edge
		 * extends below the screen (bot_y_c > 200, common for the few
		 * frames right after a line enters from the bottom), the
		 * unclamped value keeps top_w = bot_stretch/top_stretch fixed
		 * at 9/8. Clamping bot_stretch to 1.0 would degenerate the
		 * trapezoid to a rectangle at entry then "snap" back to the
		 * tilted shape as the line moves up — visible as a perspective
		 * jump. The off-screen part of the quad is GPU-clipped. */
		float strip_h_c = TITLE_CRAWL_STRIP_H_CLASSIC * top_stretch;
		float bot_y_c = top_y_c + strip_h_c;
		float bot_stretch = (bot_y_c - 40.0f) / 160.0f;

		/* Natural classic-coord text width: convert the slot's used
		 * atlas extent through atlas_scale_x and apply the same
		 * cell_h→strip-h vertical magnification factor so X and Y
		 * scale uniformly. The font appears at strip_h_c / cell_h_c
		 * times its native width. */
		float cell_h_c = (float)s->strip_atlas_h / 10.8f; /* atlas_scale_y */
		float font_magnify = (cell_h_c > 0.0f) ? TITLE_CRAWL_STRIP_H_CLASSIC / cell_h_c : 1.0f;
		float natural_w_c = ((float)s->pen_atlas_w / atlas_scale_x) * font_magnify;
		float text_top_w_c = natural_w_c * top_stretch;
		float text_bot_w_c = natural_w_c * bot_stretch;

		/* Trapezoid corners in classic px, centered on x=160. */
		float top_left_c = 160.0f - 0.5f * text_top_w_c;
		float top_right_c = 160.0f + 0.5f * text_top_w_c;
		float bot_left_c = 160.0f - 0.5f * text_bot_w_c;
		float bot_right_c = 160.0f + 0.5f * text_bot_w_c;

		/* Convert to viewport px. */
		float top_left_vp = (float)vx.region_x + top_left_c * vx.scale_x;
		float top_right_vp = (float)vx.region_x + top_right_c * vx.scale_x;
		float bot_left_vp = (float)vx.region_x + bot_left_c * vx.scale_x;
		float bot_right_vp = (float)vx.region_x + bot_right_c * vx.scale_x;
		float top_y_vp = (float)vx.region_y + top_y_c * vx.scale_y;
		float strip_h_vp = strip_h_c * vx.scale_y;

		/* Source UV — the line's slot in title_rt. (u0, v0) = top-left
		 * of the rendered text region; (u1, v1) = bottom-right. */
		float u0 = 0.0f;
		float v0 = (float)s->slot_y_atlas * inv_rt_h;
		float u1 = (float)s->pen_atlas_w * inv_rt_w;
		float v1 = (float)(s->slot_y_atlas + s->strip_atlas_h) * inv_rt_h;

		/* Projective `w` ratio: bot_stretch / top_stretch ≥ 1. The
		 * GPU rasterizer uses this for hyperbolic varying interpolation,
		 * giving proper perspective text sampling across the trap. */
		float top_w = bot_stretch / top_stretch;

		/* Build the trap blit uniforms. dst_rect describes the BOTTOM
		 * rectangle (full bottom width × strip height); top corners
		 * shift inward via trap. */
		AeronDrawList2DSprite sprite = { 0 };
		sprite.texture = Aeron_RenderTargetGetTexture(g->title_rt);
		sprite.dst_x = bot_left_vp;
		sprite.dst_y = top_y_vp;
		sprite.dst_w = bot_right_vp - bot_left_vp;
		sprite.dst_h = strip_h_vp;
		sprite.src_u0 = u0;
		sprite.src_v0 = v0;
		sprite.src_u1 = u1;
		sprite.src_v1 = v1;

		/* Per-line tint. Default path replicates the binary's palette
		 * ramp at the strip mid-row: color_index = ((mid_y-40)>>1)+96,
		 * clamped to >= 96. base_color (the engine's end-of-scene
		 * fade) isn't tracked here. Override path uses the
		 * application-supplied (r,g,b) scaled by the same depth ramp so
		 * lines closer to the camera stay brighter. */
		float mid_y_c = top_y_c + 0.5f * strip_h_c;
		float tr, tg, tb;
		float ta = 1.0f;
		if (g->crawl_tint_override) {
			/* Linear depth fade: fully invisible at the y=40
			 * vanishing point, full override color at y=200. Matches
			 * the binary's palette ramp behaviour, where index 96
			 * (the floor of ((mid_y-40)>>1)+96) is near-black in the
			 * title palette. */
			float k = (mid_y_c - 40.0f) / 160.0f;
			if (k < 0.0f)
				k = 0.0f;
			if (k > 1.0f)
				k = 1.0f;
			tr = g->crawl_tint_r * k;
			tg = g->crawl_tint_g * k;
			tb = g->crawl_tint_b * k;
		} else {
			int color = ((int)(mid_y_c - 40.0f) >> 1) + 96;
			if (color < 96)
				color = 96;
			if (color > 255)
				color = 255;
			uint32_t argb = palette[(uint8_t)color];
			TieScene2dSrgb_PalToLinearRgb(argb, &tr, &tg, &tb);
		}
		sprite.tint[0] = tr * ta;
		sprite.tint[1] = tg * ta;
		sprite.tint[2] = tb * ta;
		sprite.tint[3] = ta;
		sprite.blend = AERON_BLIT2D_BLEND_PMA;
		sprite.filter = AERON_BLIT2D_FILTER_LINEAR;
		sprite.trap_top_dx_left_px = top_left_vp - bot_left_vp;
		sprite.trap_top_dx_right_px = top_right_vp - bot_right_vp;
		sprite.trap_top_w = top_w;
		AeronDrawList_AddSprite(list, &sprite);
	}
}

void TieScene2dTextRenderer_RecordInSpace(TieScene2dTextRenderer* g, AeronDrawList2D* list, int viewport_w,
										  int viewport_h, const TieScene2dTextSpace* space,
										  const TieUIText* texts, int text_count, const uint32_t* palette) {
	if (!g || !list || !texts || text_count <= 0 || !palette)
		return;
	if (viewport_w <= 0 || viewport_h <= 0)
		return;
	if (!space)
		space = &TIE_SCENE2D_TEXT_SPACE_CLASSIC_VGA;

	TieScene2dText_PopulateComposeFonts(g);
	TieScene2dCanvas canvas;
	TieScene2dCanvas_Begin(&canvas, list, viewport_w, viewport_h);
	TieScene2dText_RecordUiText(&canvas, g->compose_fonts, MAX_FONTS, space, texts, text_count, palette);
}

void TieScene2dTextRenderer_RecordFestringInSpace(TieScene2dTextRenderer* g, AeronDrawList2D* list,
												  int viewport_w, int viewport_h,
												  const TieScene2dTextSpace* space, const TieUIText* texts,
												  int text_count, const uint32_t* palette) {
	/* Flight-sim text path — see text_layout.h above
	 * `TieScene2dText_RecordFestringUiText`. Routes through the
	 * festring/rtsvga2 emit (handles `[`/`]`/0xFE color escapes,
	 * ignores 0x01/0x02 landru toggles, no neighbour-pair shadow).
	 *
	 * `pass_depth_format` selects the depth-aware pipeline variant
	 * — required when this draws inside the flight-main pass (which
	 * has D32_FLOAT depth bound) instead of cockpit's own no-depth
	 * pass. INVALID for the legacy no-depth case. */
	if (!g || !list || !texts || text_count <= 0 || !palette)
		return;
	if (viewport_w <= 0 || viewport_h <= 0)
		return;
	if (!space)
		space = &TIE_SCENE2D_TEXT_SPACE_CLASSIC_VGA;

	TieScene2dText_PopulateComposeFonts(g);
	TieScene2dCanvas canvas;
	TieScene2dCanvas_Begin(&canvas, list, viewport_w, viewport_h);
	TieScene2dText_RecordFestringUiText(&canvas, g->compose_fonts, MAX_FONTS, space, texts, text_count,
										palette);
}

void TieScene2dTextRenderer_RecordFestringScaled(TieScene2dTextRenderer* g, AeronDrawList2D* list,
												 int viewport_w, int viewport_h,
												 const TieScene2dTextSpace* space, const TieUIText* texts,
												 int text_count, const uint32_t* palette, float offset_x,
												 float offset_y, float scale_x, float scale_y, int target_w,
												 int target_h) {
	if (!g || !list || !texts || text_count <= 0 || !palette || viewport_w <= 0 || viewport_h <= 0)
		return;
	if (!space)
		space = &TIE_SCENE2D_TEXT_SPACE_CLASSIC_VGA;

	TieScene2dText_PopulateComposeFonts(g);
	TieScene2dCanvas canvas;
	TieScene2dCanvas_Begin(&canvas, list, viewport_w, viewport_h);
	TieScene2dCanvas_SetOutputTransform(&canvas, offset_x, offset_y, scale_x, scale_y, target_w, target_h);
	TieScene2dText_RecordFestringUiText(&canvas, g->compose_fonts, MAX_FONTS, space, texts, text_count,
										palette);
}

void TieScene2dTextRenderer_Record(TieScene2dTextRenderer* g, AeronDrawList2D* list, int viewport_w,
								   int viewport_h, const TieUIText* texts, int text_count,
								   const uint32_t* palette) {
	/* Canonical VGA layout. */
	TieScene2dTextRenderer_RecordInSpace(g, list, viewport_w, viewport_h, &TIE_SCENE2D_TEXT_SPACE_CLASSIC_VGA,
										 texts, text_count, palette);
}
