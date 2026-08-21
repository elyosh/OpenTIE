/*
 * Text layout and recording. Every text batch is interpreted in
 * an explicit TieScene2dTextSpace describing
 *   - the classic coord frame TieUIText.x/y are in (320×200 VGA,
 *     640×480 SVGA cockpit, or an authored HD frame);
 *   - the atlas-px-per-classic-px scale the font was extracted at
 *     (9×/10.8× for VGA font_extract, 4.5× for SVGA cockpit_font_extract);
 *   - the inter-glyph spacer in classic-px (1 for landru lfont,
 *     0 for cockpit engine fonts);
 *   - how the classic frame maps into the bound viewport
 *     (4:3 letterbox vs fill).
 */

#include "tie_remaster/scene2d/text_layout.h"

#include "tie_remaster/scene2d/srgb_math.h"
#include "tie_remaster/scene2d/viewport.h" /* TieScene2dViewportTransform + TieScene2dViewport_ComputeXform */

#include <math.h>
#include <string.h>

const TieScene2dTextSpace TIE_SCENE2D_TEXT_SPACE_CLASSIC_VGA = {
	/* The legacy cutscene/UI space — 320×200 VGA records, 9×/10.8× VGA→4K
	 * atlas (font_extract / fontbake output), landru's 1-classic-px
	 * inter-glyph spacer, 4:3 letterbox inside any non-4:3 viewport. */
	.classic_w = 320,
	.classic_h = 200,
	.atlas_scale_x = 9.0f,
	.atlas_scale_y = 10.8f,
	.space_between_classic = 1.0f,
	.fit = TIE_SCENE2D_TEXT_FIT_LETTERBOX_4_3,
};

/* Compute the region (output-pixel origin + classic→output scale)
 * inside a viewport_w×viewport_h-sized bound viewport for the given
 * space. FIT_LETTERBOX_4_3 reuses TieScene2dViewport_ComputeXform's 4:3 region
 * (with a configurable classic frame, not hardcoded 320×200); FIT_FILL
 * skips the letterbox and maps the classic frame straight to the
 * viewport bounds. */
static void TieScene2dText_ComputeSpaceXform(int viewport_w, int viewport_h, const TieScene2dTextSpace* space,
											 TieScene2dViewportTransform* out) {
	memset(out, 0, sizeof *out);
	if (viewport_w <= 0 || viewport_h <= 0) {
		return;
	}
	out->viewport_w = viewport_w;
	out->viewport_h = viewport_h;

	if (space->fit == TIE_SCENE2D_TEXT_FIT_FILL || space->classic_w <= 0 || space->classic_h <= 0) {
		out->region_x = 0;
		out->region_y = 0;
		out->region_w = viewport_w;
		out->region_h = viewport_h;
	} else {
		TieScene2dViewportRect region;
		if (!TieScene2dViewport_ComputeFullHeightRect(viewport_w, viewport_h, 4, 3, &region)) {
			return;
		}
		out->region_w = region.w;
		out->region_h = region.h;
		out->region_x = region.x;
		out->region_y = region.y;
	}
	out->scale_x = (float)out->region_w / (float)space->classic_w;
	out->scale_y = (float)out->region_h / (float)space->classic_h;
}

static inline TieScene2dRgba TieScene2dText_PaletteRgba(const uint32_t* palette, uint8_t idx, float a) {
	float r, g, b;
	uint32_t argb = palette[idx];
	TieScene2dSrgb_PalToLinearRgb(argb, &r, &g, &b);
	/* Atlas is PMA: tint = (R*A, G*A, B*A, A) preserves the PMA
	 * invariant on the blit shader's `sample * tint` op. */
	return (TieScene2dRgba) { r * a, g * a, b * a, a };
}

/* Pick the loaded font slot for `font_id`, falling back to slot 0
 * when the requested id isn't loaded. NULL only if no fonts loaded. */
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

static const TieScene2dFontAtlas* TieScene2dText_PickFont(const TieScene2dFontAtlas* fonts, int font_count,
														  uint8_t domain, uint8_t font_id) {
	int slot = TieScene2dText_FontSlot(domain, font_id);
	if (slot >= 0 && slot < font_count && fonts[slot].loaded)
		return &fonts[slot];
	if (font_count > 0 && fonts[0].loaded)
		return &fonts[0];
	return NULL;
}

/* Append one glyph quad. Caller has already filtered out zero-size
 * cells. UV is normalised against the atlas dims. */
static void TieScene2dText_RecordGlyph(TieScene2dCanvas* canvas, const TieScene2dFontAtlas* fa, float dx,
									   float dy, float dw, float dh, const TieScene2dGlyphMetric* gm,
									   TieScene2dRgba tint) {
	AeronDrawList2DSprite sprite = {
		.texture = fa->texture,
		.src_u0 = (float)gm->atlas_x / (float)fa->atlas_w,
		.src_v0 = (float)gm->atlas_y / (float)fa->atlas_h,
		.src_u1 = (float)(gm->atlas_x + gm->atlas_w) / (float)fa->atlas_w,
		.src_v1 = (float)(gm->atlas_y + gm->atlas_h) / (float)fa->atlas_h,
		.dst_x = dx,
		.dst_y = dy,
		.dst_w = dw,
		.dst_h = dh,
		.tint = { tint.r, tint.g, tint.b, tint.a },
		.filter = AERON_BLIT2D_FILTER_LINEAR,
		.blend = AERON_BLIT2D_BLEND_PMA,
	};
	TieScene2dCanvas_AddSprite(canvas, &sprite);
}

float TieScene2dText_RecordRun(TieScene2dCanvas* f, const TieScene2dFontAtlas* fa,
							   const TieScene2dTextLayout* L, float pen_x, float pen_y,
							   TieScene2dRgba main_tint, TieScene2dRgba bold_tint, TieScene2dRgba shadow_tint,
							   bool start_bold, bool with_shadow, const char* text) {
	bool cur_bold = start_bold;
	for (const char* p = text; *p; p++) {
		unsigned ch = (unsigned char)*p;
		/* Bracket-toggle bytes — change current color; no advance,
		 * no glyph emit. Mirrors landru/font.c case 1 / case 2. */
		if (ch == 0x01) {
			cur_bold = false;
			continue;
		}
		if (ch == 0x02) {
			cur_bold = true;
			continue;
		}
		if (ch < fa->first_char || ch >= fa->first_char + fa->num_chars)
			continue;
		const TieScene2dGlyphMetric* gm = &fa->glyphs[ch - fa->first_char];
		if (gm->atlas_w == 0 || gm->atlas_h == 0 || gm->advance == 0) {
			pen_x += (float)gm->advance * L->ratio_x;
			continue;
		}
		float dw = (float)gm->atlas_w * L->ratio_x;
		float dh = (float)gm->atlas_h * L->ratio_y;
		if (with_shadow) {
			/* Two shadow passes per glyph — right neighbour AND below
			 * neighbour, NOT the diagonal. Mirrors landru/font.c
			 * lfont_Draw_Font_Shadow_NN, which writes shadow at
			 * dst[col+1] (same row) AND dst[lineStride+col] (same
			 * column, row +1) for each FG pixel. The foreground pass
			 * below paints over any shadow that lands on FG cells. */
			TieScene2dText_RecordGlyph(f, fa, pen_x + L->shadow_dx, pen_y, dw, dh, gm, shadow_tint);
			TieScene2dText_RecordGlyph(f, fa, pen_x, pen_y + L->shadow_dy, dw, dh, gm, shadow_tint);
		}
		TieScene2dText_RecordGlyph(f, fa, pen_x, pen_y, dw, dh, gm, cur_bold ? bold_tint : main_tint);
		pen_x += (float)gm->advance * L->ratio_x + L->space_between;
	}
	return pen_x;
}

void TieScene2dText_LayoutInit(TieScene2dTextLayout* out, int viewport_w, int viewport_h,
							   const TieScene2dTextSpace* space) {
	if (!space)
		space = &TIE_SCENE2D_TEXT_SPACE_CLASSIC_VGA;
	TieScene2dViewportTransform vx;
	TieScene2dText_ComputeSpaceXform(viewport_w, viewport_h, space, &vx);
	/* atlas-px → output-px = (atlas-px / atlas_scale) (classic-px)
	 *                         × scale_? (output-px-per-classic-px). */
	out->ratio_x = vx.scale_x / space->atlas_scale_x;
	out->ratio_y = vx.scale_y / space->atlas_scale_y;
	out->space_between = space->space_between_classic * vx.scale_x;
	/* Shadow offset = 1 classic-px in output-px, snapped to integers
	 * so linear-filter UV doesn't bleed at the cell edge. */
	out->shadow_dx = floorf(vx.scale_x + 0.5f);
	out->shadow_dy = floorf(vx.scale_y + 0.5f);
	out->region_x = (float)vx.region_x;
	out->region_y = (float)vx.region_y;
	out->scale_x = vx.scale_x;
	out->scale_y = vx.scale_y;
	out->region_ix = vx.region_x;
	out->region_iy = vx.region_y;
	out->region_w = vx.region_w;
	out->region_h = vx.region_h;
	out->classic_w = space->classic_w;
	out->classic_h = space->classic_h;
}

static int TieScene2dText_ScissorFromClip(const TieScene2dTextLayout* layout, int left, int top, int right,
										  int bottom, AeronRectI* out) {
	int x0, y0, x1, y1;

	if (!TieScene2dViewport_MapEdge(layout->region_ix, layout->region_w, layout->classic_w, left, &x0) ||
		!TieScene2dViewport_MapEdge(layout->region_iy, layout->region_h, layout->classic_h, top, &y0) ||
		!TieScene2dViewport_MapEdge(layout->region_ix, layout->region_w, layout->classic_w, right, &x1) ||
		!TieScene2dViewport_MapEdge(layout->region_iy, layout->region_h, layout->classic_h, bottom, &y1))
		return 0;
	*out = (AeronRectI) { x0, y0, x1 - x0, y1 - y0 };
	return 1;
}

/* Sum the (advance + spacer) cost of a NUL-terminated text run, in
 * atlas-px. Skips control bytes 0x01/0x02 (landru/font.c color
 * toggles) and codepoints outside the font's [first_char,
 * first_char+num_chars) range. Returns 0 when the run is empty.
 *
 * Counterpart for the festring/rtsvga2 text path is below
 * (`TieScene2dText_RunWidthAtlasFestring`); it skips `[`/`]` color nudges and
 * `0xFE <idx>` color escapes instead. The two paths share no
 * opcodes — see comment block above TieScene2dText_RecordFestringRun. */
static float TieScene2dText_RunWidthAtlas(const TieScene2dFontAtlas* fa, float space_between_atlas,
										  const char* text) {
	if (!fa || !text || !text[0])
		return 0.0f;
	float width = 0.0f;
	int emitted = 0;
	for (const char* p = text; *p; p++) {
		unsigned ch = (unsigned char)*p;
		if (ch == 0x01 || ch == 0x02)
			continue;
		if (ch < fa->first_char || ch >= fa->first_char + fa->num_chars)
			continue;
		const TieScene2dGlyphMetric* gm = &fa->glyphs[ch - fa->first_char];
		if (emitted > 0)
			width += space_between_atlas;
		width += (float)gm->advance;
		emitted++;
	}
	return width;
}

void TieScene2dText_RecordUiText(TieScene2dCanvas* canvas, const TieScene2dFontAtlas* fonts, int font_count,
								 const TieScene2dTextSpace* space, const TieUIText* texts, int text_count,
								 const uint32_t* palette) {
	if (!canvas || !fonts || font_count <= 0 || !texts || text_count <= 0 || !palette)
		return;
	if (!space)
		space = &TIE_SCENE2D_TEXT_SPACE_CLASSIC_VGA;

	TieScene2dTextLayout layout;
	TieScene2dText_LayoutInit(&layout, canvas->viewport_w, canvas->viewport_h, space);

	/* One batch per text record — each carries its captured canvas-
	 * clip as a scissor so glyphs that classic dropped via
	 * lcanvas_Clip_Rect_To_Canvas don't bleed past the engine clip
	 * (e.g. brief-widget readout text near map-area edges). */
	for (int ti = 0; ti < text_count; ti++) {
		const TieUIText* t = &texts[ti];
		if (t->text[0] == '\0')
			continue;

		const TieScene2dFontAtlas* fa =
			TieScene2dText_PickFont(fonts, font_count, t->font_domain, t->font_id);
		if (!fa)
			continue;

		AeronRectI sc;
		if (t->clip_right > t->clip_left && t->clip_bottom > t->clip_top) {
			if (!TieScene2dText_ScissorFromClip(&layout, t->clip_left, t->clip_top, t->clip_right,
												t->clip_bottom, &sc))
				return;
		} else {
			sc = (AeronRectI) { 0 };
		}
		TieScene2dCanvas_SetScissor(canvas, sc);

		/* Pen origin in viewport pixels — y0 snapped to nearest
		 * integer so fractional scale_y doesn't cause UV bleed at the
		 * cell edge under linear filtering. */
		float pen_x = layout.region_x + (float)t->x * layout.scale_x;
		float pen_y = floorf(layout.region_y + (float)t->y * layout.scale_y + 0.5f);

		/* `background` mirrors festring_setbackcolor's effect on the
		 * engine outchar loop — every non-ink pixel of each glyph cell
		 * gets painted with backcolor. We emit one solid strip
		 * covering the full run's extent (advance_total × cell_h_out),
		 * then composite ink on top via the glyph pass. Skipped when
		 * the caller passed NULL `white_texture` (e.g. cutscene
		 * subtitles which never set the background flag anyway). */
		if (t->background && fa->cell_h > 0) {
			float space_between_atlas =
				(layout.ratio_x > 0.0f) ? layout.space_between / layout.ratio_x : 0.0f;
			float run_w_atlas = TieScene2dText_RunWidthAtlas(fa, space_between_atlas, t->text);
			if (run_w_atlas > 0.0f) {
				TieScene2dRgba color =
					TieScene2dText_PaletteRgba(palette, (uint8_t)t->background_color_index, 1.0f);
				const float rgba[4] = { color.r, color.g, color.b, color.a };
				TieScene2dCanvas_AddFill(canvas, pen_x, pen_y, run_w_atlas * layout.ratio_x,
										 (float)fa->cell_h * layout.ratio_y, rgba, AERON_BLIT2D_BLEND_PMA);
			}
		}

		/* Three palette-indexed tints captured from s_cur_font at
		 * draw time — see the TieUIText comment in snapshot.h. */
		TieScene2dRgba main_tint = TieScene2dText_PaletteRgba(palette, (uint8_t)t->color_index, 1.0f);
		TieScene2dRgba bold_tint = TieScene2dText_PaletteRgba(palette, (uint8_t)t->bold_color_index, 1.0f);
		TieScene2dRgba shadow_tint =
			TieScene2dText_PaletteRgba(palette, (uint8_t)t->shadow_color_index, 1.0f);

		TieScene2dText_RecordRun(canvas, fa, &layout, pen_x, pen_y, main_tint, bold_tint, shadow_tint,
								 /*start_bold=*/false,
								 /*with_shadow=*/(t->shadow != 0), t->text);
	}
}

/* Festring uses standalone cockpit fonts and its own color opcodes.
 * It does not interpret Landru's 0x01/0x02 bold toggles. The recovered
 * drop color currently equals the background, so no shadow is emitted. */

/* Sum the printable-glyph atlas-px width of a NUL-terminated text
 * run on the festring path. Skips `[` and `]` (color nudges) and the
 * 0xFE + 1-byte color-escape pair. Out-of-range codepoints emit no
 * width. */
static float TieScene2dText_RunWidthAtlasFestring(const TieScene2dFontAtlas* fa, float space_between_atlas,
												  const char* text) {
	if (!fa || !text || !text[0])
		return 0.0f;
	float width = 0.0f;
	int emitted = 0;
	for (const char* p = text; *p; p++) {
		unsigned ch = (unsigned char)*p;
		if (ch == '[' || ch == ']')
			continue;
		if (ch == 0xFE) {
			/* Skip the trailing color-index byte (or stop at EOS). */
			if (*(p + 1))
				p++;
			continue;
		}
		if (ch < fa->first_char || ch >= fa->first_char + fa->num_chars)
			continue;
		const TieScene2dGlyphMetric* gm = &fa->glyphs[ch - fa->first_char];
		if (emitted > 0)
			width += space_between_atlas;
		width += (float)gm->advance;
		emitted++;
	}
	return width;
}

/* Per-record per-char emit for the festring path. The caller fixes
 * the base tint via `base_color_idx`; this function tracks the
 * "current" post-remap palette index in `cur_color` and updates the
 * active tint on each color event:
 *
 *   '['  : cur_color = (cur == 0xD4) ? 0xD3 : cur + 1
 *   ']'  : cur_color = (cur == 0xD3) ? 0xD4 : cur - 1
 *   0xFE : cur_color = *(p+1)            (skip both bytes)
 *
 * Out-of-range codepoints (including 0x01/0x02 — those are landru-
 * only opcodes the festring renderer treats as nothing) and stray
 * bytes are silently skipped without advancing the pen. */
static float TieScene2dText_RecordFestringRun(TieScene2dCanvas* f, const TieScene2dFontAtlas* fa,
											  const TieScene2dTextLayout* L, float pen_x, float pen_y,
											  uint8_t base_color_idx, const uint32_t* palette,
											  const char* text) {
	uint8_t cur_color = base_color_idx;
	TieScene2dRgba cur_tint = TieScene2dText_PaletteRgba(palette, cur_color, 1.0f);
	for (const char* p = text; *p; p++) {
		unsigned ch = (unsigned char)*p;
		if (ch == '[') {
			cur_color = (cur_color == 0xD4u) ? (uint8_t)(cur_color - 1) : (uint8_t)(cur_color + 1);
			cur_tint = TieScene2dText_PaletteRgba(palette, cur_color, 1.0f);
			continue;
		}
		if (ch == ']') {
			cur_color = (cur_color == 0xD3u) ? (uint8_t)(cur_color + 1) : (uint8_t)(cur_color - 1);
			cur_tint = TieScene2dText_PaletteRgba(palette, cur_color, 1.0f);
			continue;
		}
		if (ch == 0xFE) {
			const uint8_t next = (uint8_t)*(p + 1);
			if (next) {
				cur_color = next;
				cur_tint = TieScene2dText_PaletteRgba(palette, cur_color, 1.0f);
				p++;
			}
			continue;
		}
		if (ch < fa->first_char || ch >= fa->first_char + fa->num_chars)
			continue;
		const TieScene2dGlyphMetric* gm = &fa->glyphs[ch - fa->first_char];
		if (gm->atlas_w == 0 || gm->atlas_h == 0 || gm->advance == 0) {
			pen_x += (float)gm->advance * L->ratio_x;
			continue;
		}
		const float dw = (float)gm->atlas_w * L->ratio_x;
		const float dh = (float)gm->atlas_h * L->ratio_y;
		TieScene2dText_RecordGlyph(f, fa, pen_x, pen_y, dw, dh, gm, cur_tint);
		pen_x += (float)gm->advance * L->ratio_x + L->space_between;
	}
	return pen_x;
}

void TieScene2dText_RecordFestringUiText(TieScene2dCanvas* canvas, const TieScene2dFontAtlas* fonts,
										 int font_count, const TieScene2dTextSpace* space,
										 const TieUIText* texts, int text_count, const uint32_t* palette) {
	if (!canvas || !fonts || font_count <= 0 || !texts || text_count <= 0 || !palette)
		return;
	if (!space)
		space = &TIE_SCENE2D_TEXT_SPACE_CLASSIC_VGA;

	TieScene2dTextLayout layout;
	TieScene2dText_LayoutInit(&layout, canvas->viewport_w, canvas->viewport_h, space);

	/* Two-pass emit so retained-list state coalescence does not get broken by the
	 * per-record bg-then-glyphs ordering.
	 *
	 * Before: record A bg, record A glyphs, record B bg, record B
	 * glyphs, ... — every record swaps texture twice (solid → font).
	 * After:  ALL record bgs, then ALL record glyphs — the retained renderer
	 * collapses the background strip to a single instanced draw and the
	 * glyphs to another. Visually identical for non-overlapping
	 * records (the dominant case for cockpit HUD readouts).
	 *
	 * Records with their own scissor still get their own batch
	 * (preserves the clip rect) — those don't merge with neighbours
	 * because the scissor change forces a flush. Records with no clip
	 * (the common case) all coalesce in each phase. */

	/* Pass 1 — backgrounds only. */
	for (int ti = 0; ti < text_count; ti++) {
		const TieUIText* t = &texts[ti];
		if (t->text[0] == '\0')
			continue;
		const TieScene2dFontAtlas* fa =
			TieScene2dText_PickFont(fonts, font_count, t->font_domain, t->font_id);
		if (!fa)
			continue;
		if (!(t->background && fa->cell_h > 0))
			continue;

		AeronRectI sc;
		if (t->clip_right > t->clip_left && t->clip_bottom > t->clip_top) {
			if (!TieScene2dText_ScissorFromClip(&layout, t->clip_left, t->clip_top, t->clip_right,
												t->clip_bottom, &sc))
				return;
		} else {
			sc = (AeronRectI) { 0 };
		}

		float space_between_atlas = (layout.ratio_x > 0.0f) ? layout.space_between / layout.ratio_x : 0.0f;
		float run_w_atlas = TieScene2dText_RunWidthAtlasFestring(fa, space_between_atlas, t->text);
		if (run_w_atlas <= 0.0f)
			continue;

		TieScene2dCanvas_SetScissor(canvas, sc);

		float pen_x = layout.region_x + (float)t->x * layout.scale_x;
		float pen_y = floorf(layout.region_y + (float)t->y * layout.scale_y + 0.5f);

		TieScene2dRgba color = TieScene2dText_PaletteRgba(palette, (uint8_t)t->background_color_index, 1.0f);
		const float rgba[4] = { color.r, color.g, color.b, color.a };
		TieScene2dCanvas_AddFill(canvas, pen_x, pen_y, run_w_atlas * layout.ratio_x,
								 (float)fa->cell_h * layout.ratio_y, rgba, AERON_BLIT2D_BLEND_PMA);
	}

	/* Pass 2 — glyphs only. */
	for (int ti = 0; ti < text_count; ti++) {
		const TieUIText* t = &texts[ti];
		if (t->text[0] == '\0')
			continue;
		const TieScene2dFontAtlas* fa =
			TieScene2dText_PickFont(fonts, font_count, t->font_domain, t->font_id);
		if (!fa)
			continue;

		AeronRectI sc;
		if (t->clip_right > t->clip_left && t->clip_bottom > t->clip_top) {
			if (!TieScene2dText_ScissorFromClip(&layout, t->clip_left, t->clip_top, t->clip_right,
												t->clip_bottom, &sc))
				return;
		} else {
			sc = (AeronRectI) { 0 };
		}
		TieScene2dCanvas_SetScissor(canvas, sc);

		float pen_x = layout.region_x + (float)t->x * layout.scale_x;
		float pen_y = floorf(layout.region_y + (float)t->y * layout.scale_y + 0.5f);

		TieScene2dText_RecordFestringRun(canvas, fa, &layout, pen_x, pen_y, (uint8_t)t->color_index, palette,
										 t->text);
	}
}

float TieScene2dText_MeasureFestringClassic(const TieScene2dFontAtlas* fa, const TieScene2dTextSpace* space,
											const char* text) {
	if (!fa || !text || !text[0])
		return 0.0f;
	if (!space)
		space = &TIE_SCENE2D_TEXT_SPACE_CLASSIC_VGA;
	if (space->atlas_scale_x <= 0.0f)
		return 0.0f;

	/* TieScene2dText_RunWidthAtlasFestring returns atlas-px width including the
	 * inter-glyph spacer it's given. Pass the classic-space spacer
	 * converted to atlas-px (space_between_classic × atlas_scale_x);
	 * dividing the result by atlas_scale_x yields classic-px width,
	 * which is what cockpit/HUD callers express their layout in. */
	const float space_atlas = space->space_between_classic * space->atlas_scale_x;
	const float w_atlas = TieScene2dText_RunWidthAtlasFestring(fa, space_atlas, text);
	return w_atlas / space->atlas_scale_x;
}
