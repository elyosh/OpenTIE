#include "backends/imgui_impl_sdl3.h"
#include "backends/imgui_impl_sdlgpu3.h"
#include "imgui.h"

#include <SDL3/SDL.h>

extern "C" {
#include "aeron/config_file.h"
#include "font_atlas.h"
#include "font_atlas_load.h"
}

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

/* Per-TTF state: source bytes, current parameters, last build result,
 * GPU sample-text texture. `params_prev` lets us detect slider changes
 * cheaply via memcmp. */
struct TieFontTuneTtfEntry {
    /* Stable identifier used by file-dialog callbacks to find the
     * entry again after the dialog returns. We can't capture a raw
     * vector index because Add/Remove can shift indices, and we
     * can't capture a pointer because std::vector resize invalidates
     * pointers. Assigned at construction from a monotonic counter. */
    int                     id            = 0;
    std::string             path;
    std::vector<uint8_t>    ttf_data;
    FontAtlasParams         params{};
    FontAtlasParams         params_prev{};
    int                     descender_lift_atlas = 0;
    int                     descender_lift_prev  = 0;
    FontAtlasResult         result{};
    bool                    result_valid  = false;
    std::string             err;
    SDL_GPUTexture         *sample_tex    = nullptr;
    int                     sample_w      = 0;
    int                     sample_h      = 0;
    /* Full-atlas texture for the per-glyph picker grid. Rebuilt every
     * time the atlas itself does. */
    SDL_GPUTexture         *atlas_tex     = nullptr;
    /* Codepoint currently selected in the picker (-1 = none). The
     * picker grid highlights this cell and the per-glyph slider edits
     * compression_glyph_delta_pct[selected_cp]. */
    int                     selected_cp   = -1;
    /* Save UI state. `save_basename` is the output prefix; `save_msg`
     * shows the last save attempt's outcome. */
    std::string             save_basename;
    std::string             save_msg;
    bool                    save_ok       = false;
};

struct TieFontTuneApp {
    SDL_Window           *window     = nullptr;
    SDL_GPUDevice        *device     = nullptr;
    SDL_GPUTextureFormat  swap_fmt   = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    float                 dpi_scale  = 1.0f;

    bool                  ref_loaded = false;
    std::string           ref_basename;
    FontAtlasResult       ref_atlas{};
    SDL_GPUTexture       *ref_sample_tex = nullptr;
    int                   ref_sample_w   = 0;
    int                   ref_sample_h   = 0;

    std::vector<TieFontTuneTtfEntry> ttfs;
    /* Default sample text. Includes a literal underscore plus the ™
     * UTF-8 sequence so non-ASCII slot resolution gets exercised
     * out of the box. ImGui's default font has no glyph for ™ and
     * shows a fallback '?' inside the input field — that's a quirk
     * of the editor widget; the underlying buffer bytes are intact
     * and our compose_text resolves them via codepoint_remap. */
    char                  sample_text[256] =
        "The quick brown fox _ 0123456789 !? \xE2\x84\xA2";
    bool                  sample_dirty     = true;
	bool                  tie_classic      = false;
	int                   preview_sizes[3] = {16, 20, 22};

    /* TIE-classic preview scaling. Generic mode instead displays the
     * sample at each logical pixel size in preview_sizes. */
    float                 display_scale = 0.5f;

    /* Only TIE-classic mode has an implicit two-pass text shadow. */
    bool                  show_shadow   = false;

    /* Overlay the baseline as a thin colored line under each text row.
     * On by default — the whole point of the visual is to show where
     * glyphs sit. */
    bool                  show_baseline = true;

    /* Overlay a thin vertical line at every inter-glyph boundary in
     * the sample-text strip. Off by default — useful when checking
     * that auto-match advances really do put glyph cells where the
     * reference's cells are, but visually noisy otherwise. */
    bool                  show_cell_separators = false;

    /* Monotonic id counter for TieFontTuneTtfEntry. */
    int                   next_ttf_id   = 1;
};

/* Read entire file into a byte vector. Returns empty + sets `err` on
 * failure. Used for TTF loading; small files (≤ a few MB). */
bool TieFontTune_SlurpFile(const char *path, std::vector<uint8_t> &out, std::string &err)
{
    FILE *fp = std::fopen(path, "rb");
    if (!fp) { err = std::string("open: ") + path; return false; }
    if (std::fseek(fp, 0, SEEK_END) != 0) { std::fclose(fp); err = "seek"; return false; }
    long sz = std::ftell(fp);
    if (sz <= 0) { std::fclose(fp); err = "empty"; return false; }
    std::rewind(fp);
    out.resize((size_t)sz);
    if (std::fread(out.data(), 1, (size_t)sz, fp) != (size_t)sz) {
        std::fclose(fp); err = "read"; return false;
    }
    std::fclose(fp);
    return true;
}

/* Number of stacked sample-text lines per font card. Two is enough to
 * show in-game line spacing (= cell_h between lines) at a glance. */
constexpr int PREVIEW_LINES = 2;

constexpr int TIE_CLASSIC_TRACKING_ATLAS = 9;

/* Drop-shadow offsets in atlas px, matching the runtime
 * (src/tie_remaster/scene2d/text_layout.c::TieScene2dText_LayoutInit):
 *   shadow_dx = 1 classic-px → 9 atlas-px   (= TIE_SCENE2D_TEXT_ATLAS_SCALE_X)
 *   shadow_dy = 1 classic-px → ~11 atlas-px (= round(TIE_SCENE2D_TEXT_ATLAS_SCALE_Y=10.8))
 * Right + below, NOT diagonal — mirrors landru/font.c
 * lfont_Draw_Font_Shadow_NN's two-pass write pattern. */
constexpr int TIE_SHADOW_DX_ATLAS = 9;
constexpr int TIE_SHADOW_DY_ATLAS = 11;

int TieFontTune_PreviewTrackingAtlas(const TieFontTuneApp &app) {
	return app.tie_classic ? TIE_CLASSIC_TRACKING_ATLAS : 0;
}

int TieFontTune_PreviewShadowDxAtlas(const TieFontTuneApp &app) {
	return app.tie_classic ? TIE_SHADOW_DX_ATLAS : 0;
}

int TieFontTune_PreviewShadowDyAtlas(const TieFontTuneApp &app) {
	return app.tie_classic ? TIE_SHADOW_DY_ATLAS : 0;
}

/* Zero means "no override" to font_atlas_build, so explicit classic
 * strides that collapse to zero temporarily use 1 and are fixed after
 * rasterization. */
void TieFontTune_ApplyTieClassicBuildMetrics(FontAtlasParams &params) {
    params.tracking_atlas -= TIE_CLASSIC_TRACKING_ATLAS;
    if (params.space_advance_atlas > 0) {
        params.space_advance_atlas -= TIE_CLASSIC_TRACKING_ATLAS;
        if (params.space_advance_atlas <= 0)
            params.space_advance_atlas = 1;
    }
    for (int cp = 0; cp < 256; ++cp) {
        int &advance = params.compression_glyph_advance_atlas[cp];
        if (advance <= 0) continue;
        advance -= TIE_CLASSIC_TRACKING_ATLAS;
        if (advance <= 0) advance = 1;
    }
}

void TieFontTune_FinalizeTieClassicZeroAdvances(const FontAtlasParams &source,
                                        FontAtlasResult &result) {
    if (source.space_advance_atlas > 0 &&
        source.space_advance_atlas <= TIE_CLASSIC_TRACKING_ATLAS) {
        int glyph = ' ' - result.first_char;
        if (glyph >= 0 && glyph < result.num_chars)
            result.glyphs[glyph].advance = 0;
    }
    for (int cp = 0; cp < 256; ++cp) {
        int advance = source.compression_glyph_advance_atlas[cp];
        if (advance <= 0 || advance > TIE_CLASSIC_TRACKING_ATLAS)
            continue;
        int glyph = cp - result.first_char;
        if (glyph >= 0 && glyph < result.num_chars)
            result.glyphs[glyph].advance = 0;
    }
}

/* Blit one glyph from `r->rgba` into `buf` at (dst_x, dst_y), tinted.
 * Source is PMA (a, a, a, a); after tinting, output PMA is
 * (tint_r*a/255, tint_g*a/255, tint_b*a/255, a). Composites over
 * existing buf contents using the standard PMA-over-PMA formula. */
void TieFontTune_BlitGlyph(uint8_t *buf, int buf_w, int buf_h,
                const FontAtlasResult *r, const FontAtlasGlyph *g,
                int dst_x, int dst_y,
                uint8_t tint_r, uint8_t tint_g, uint8_t tint_b)
{
    int gw = (int)g->atlas_w, gh = (int)g->atlas_h;
    for (int y = 0; y < gh; y++) {
        int dy = dst_y + y;
        if (dy < 0 || dy >= buf_h) continue;
        const uint8_t *src = r->rgba +
            (((size_t)g->atlas_y + y) * (size_t)r->atlas_w
             + (size_t)g->atlas_x) * 4u;
        uint8_t *dst = buf + ((size_t)dy * (size_t)buf_w + (size_t)dst_x) * 4u;
        int x = 0;
        if (dst_x < 0) { src += -dst_x * 4; dst += -dst_x * 4; x = -dst_x; }
        for (; x < gw; x++) {
            int dx = dst_x + x;
            if (dx >= buf_w) break;
            uint8_t sa = src[3];
            if (sa > 0) {
                /* Source RGB is (sa, sa, sa) — atlas is white-on-clear PMA.
                 * Tint: replace with (tint*sa/255). For white tint
                 * (255,255,255) this is identity. */
                uint8_t sr = (uint8_t)(((int)tint_r * sa) / 255);
                uint8_t sg = (uint8_t)(((int)tint_g * sa) / 255);
                uint8_t sb = (uint8_t)(((int)tint_b * sa) / 255);
                int inv = 255 - sa;
                dst[0] = (uint8_t)(sr + (dst[0] * inv) / 255);
                dst[1] = (uint8_t)(sg + (dst[1] * inv) / 255);
                dst[2] = (uint8_t)(sb + (dst[2] * inv) / 255);
                dst[3] = (uint8_t)(sa + (dst[3] * inv) / 255);
            }
				src += 4;
				dst += 4;
        }
    }
}

/* Compose `num_lines` copies of `text` at atlas resolution. Generic mode
 * passes zero tracking and no shadow; TIE-classic mode supplies its legacy
 * spacer and two-pass shadow offsets. */
/* Decode one UTF-8 codepoint from `s`. Returns bytes consumed (1..4)
 * and writes the codepoint to *out; returns 0 on a malformed lead /
 * truncated continuation. ASCII (< 0x80) is the trivial 1-byte case. */
	int TieFontTune_DecodeUtf8(const unsigned char *s, unsigned *out) {
    unsigned char b0 = s[0];
		if (b0 < 0x80) {
			*out = b0;
			return 1;
		}
    if ((b0 & 0xE0) == 0xC0) {
        if ((s[1] & 0xC0) != 0x80) return 0;
        *out = ((unsigned)(b0 & 0x1F) << 6) | (unsigned)(s[1] & 0x3F);
        return 2;
    }
    if ((b0 & 0xF0) == 0xE0) {
        if ((s[1] & 0xC0) != 0x80 || (s[2] & 0xC0) != 0x80) return 0;
        *out = ((unsigned)(b0 & 0x0F) << 12) |
               ((unsigned)(s[1] & 0x3F) << 6) |
                (unsigned)(s[2] & 0x3F);
        return 3;
    }
    if ((b0 & 0xF8) == 0xF0) {
        if ((s[1] & 0xC0) != 0x80 || (s[2] & 0xC0) != 0x80 ||
            (s[3] & 0xC0) != 0x80) return 0;
        *out = ((unsigned)(b0 & 0x07) << 18) |
               ((unsigned)(s[1] & 0x3F) << 12) |
               ((unsigned)(s[2] & 0x3F) << 6) |
                (unsigned)(s[3] & 0x3F);
        return 4;
    }
    return 0;
}

/* Resolve a Unicode codepoint to a glyph slot in atlas `r`. Walks
 * every slot, computing its EFFECTIVE codepoint:
 *   eff_cp(slot) = codepoint_remap[slot_cp]  if remap set & non-zero
 *                = slot_cp                    otherwise
 * and returns the slot whose eff_cp equals `cp`. -1 on miss.
 *
 * The full scan is necessary because the natural-ASCII-first shortcut
 * shadows user remappings: with remap[126]=8482 (slot 126 now holds
 * ™) and remap[127]=126 (slot 127 now holds ~), a typed '~' (cp 126)
 * must NOT resolve to its natural slot 94 (which now holds ™); it has
 * to find slot 95, where the user moved '~'. Walking all 96 slots is
 * O(num_chars) per character — trivial for sample-text strings. */
int TieFontTune_ResolveTextSlot(unsigned cp, const FontAtlasResult *r,
						  const int *codepoint_remap_or_null) {
    for (int slot = 0; slot < r->num_chars; slot++) {
        int slot_cp = r->first_char + slot;
        int eff_cp  = slot_cp;
			if (codepoint_remap_or_null && slot_cp >= 0 && slot_cp < 256 && codepoint_remap_or_null[slot_cp] > 0)
            eff_cp = codepoint_remap_or_null[slot_cp];
        if (eff_cp == (int)cp) return slot;
    }
    return -1;
}

uint8_t *TieFontTune_ComposeText(const FontAtlasResult *r,
                      const int *codepoint_remap_or_null,
                      const char *text,
                      int num_lines, int tracking_atlas,
					  bool skip_tie_controls, bool with_shadow,
					  int shadow_dx, int shadow_dy,
						  int *out_w, int *out_h) {
		*out_w = 0;
		*out_h = 0;
    if (!r || !r->rgba || !r->glyphs || !text ||
        r->cell_h <= 0 || num_lines <= 0)
        return nullptr;

    /* Width pre-pass: sum advances + (n-1) inter-glyph spacers, then
     * pad for last glyph's right-extending ink + right shadow. */
    int width = 0;
    int glyph_count = 0;
    for (const unsigned char *p = (const unsigned char *)text; *p; ) {
        unsigned cp;
        int len = TieFontTune_DecodeUtf8(p, &cp);
			if (len <= 0) {
				p++;
				continue;
			}
        p += len;
		if (skip_tie_controls && (cp == 0x01 || cp == 0x02)) continue;
        int slot = TieFontTune_ResolveTextSlot(cp, r, codepoint_remap_or_null);
        if (slot < 0) continue;
        width += r->glyphs[slot].advance;
        glyph_count++;
    }
    if (glyph_count > 1)
        width += (glyph_count - 1) * tracking_atlas;
    width += r->cell_w;
    if (with_shadow) width += shadow_dx;

    int height = r->cell_h * num_lines;
    if (with_shadow) height += shadow_dy;
    if (width <= 0 || height <= 0) return nullptr;

    uint8_t *buf = (uint8_t *)std::calloc((size_t)width * (size_t)height * 4u, 1);
    if (!buf) return nullptr;

    for (int line = 0; line < num_lines; line++) {
        int line_y = line * r->cell_h;
        int pen = 0;
        bool first_glyph = true;
        for (const unsigned char *p = (const unsigned char *)text; *p; ) {
            unsigned cp;
            int len = TieFontTune_DecodeUtf8(p, &cp);
				if (len <= 0) {
					p++;
					continue;
				}
            p += len;
			if (skip_tie_controls && (cp == 0x01 || cp == 0x02)) continue;
            int slot = TieFontTune_ResolveTextSlot(cp, r, codepoint_remap_or_null);
            if (slot < 0) continue;

            if (!first_glyph) pen += tracking_atlas;
            first_glyph = false;

            const FontAtlasGlyph &g = r->glyphs[slot];
            /* Shadows first so foreground writes over shadow at glyph
             * cells — same emit order the runtime uses. */
            if (with_shadow) {
                TieFontTune_BlitGlyph(buf, width, height, r, &g,
                           pen + shadow_dx, line_y, 0, 0, 0);
                TieFontTune_BlitGlyph(buf, width, height, r, &g,
                           pen, line_y + shadow_dy, 0, 0, 0);
            }
            TieFontTune_BlitGlyph(buf, width, height, r, &g, pen, line_y, 255, 255, 255);
            pen += g.advance;
        }
    }

    *out_w = width;
    *out_h = height;
    return buf;
}

/* Upload an RGBA8 buffer as a sampled texture in a one-shot copy pass. */
SDL_GPUTexture *TieFontTune_UploadRgba(SDL_GPUDevice *dev, const uint8_t *rgba,
								int w, int h) {
    if (!dev || !rgba || w <= 0 || h <= 0) return nullptr;

    SDL_GPUTextureCreateInfo ti{};
    ti.type                 = SDL_GPU_TEXTURETYPE_2D;
    ti.format               = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    ti.usage                = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    ti.width                = (Uint32)w;
    ti.height               = (Uint32)h;
    ti.layer_count_or_depth = 1;
    ti.num_levels           = 1;
    ti.sample_count         = SDL_GPU_SAMPLECOUNT_1;
    SDL_GPUTexture *tex = SDL_CreateGPUTexture(dev, &ti);
    if (!tex) return nullptr;

    SDL_GPUTransferBufferCreateInfo xi{};
    xi.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    xi.size  = (Uint32)((size_t)w * (size_t)h * 4u);
    SDL_GPUTransferBuffer *xfer = SDL_CreateGPUTransferBuffer(dev, &xi);
		if (!xfer) {
			SDL_ReleaseGPUTexture(dev, tex);
			return nullptr;
		}
    void *mapped = SDL_MapGPUTransferBuffer(dev, xfer, false);
    if (!mapped) {
        SDL_ReleaseGPUTransferBuffer(dev, xfer);
        SDL_ReleaseGPUTexture(dev, tex);
        return nullptr;
    }
    std::memcpy(mapped, rgba, (size_t)w * (size_t)h * 4u);
    SDL_UnmapGPUTransferBuffer(dev, xfer);

    SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(dev);
    if (!cmd) {
        SDL_ReleaseGPUTransferBuffer(dev, xfer);
        SDL_ReleaseGPUTexture(dev, tex);
        return nullptr;
    }
    SDL_GPUCopyPass *cp = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTextureTransferInfo src{};
    src.transfer_buffer = xfer;
    src.pixels_per_row  = (Uint32)w;
    src.rows_per_layer  = (Uint32)h;
    SDL_GPUTextureRegion dst{};
    dst.texture = tex;
		dst.w = (Uint32) w;
		dst.h = (Uint32) h;
		dst.d = 1;
    SDL_UploadToGPUTexture(cp, &src, &dst, false);
    SDL_EndGPUCopyPass(cp);
    SDL_SubmitGPUCommandBuffer(cmd);
    SDL_ReleaseGPUTransferBuffer(dev, xfer);
    return tex;
}

/* Re-compose the sample-text strip for the given result + replace the
 * GPU texture. Called whenever sample text or atlas changes. The
 * `codepoint_remap_or_null` argument lets the user type non-ASCII
 * characters (™, ©, etc.) and have them resolve to whichever atlas
 * slot the user remapped to that codepoint. */
void TieFontTune_RecomposeSample(TieFontTuneApp &app, const FontAtlasResult *r,
                      const int *codepoint_remap_or_null,
						  SDL_GPUTexture *&tex, int &out_w, int &out_h) {
    if (tex) {
        SDL_ReleaseGPUTexture(app.device, tex);
        tex = nullptr;
    }
    out_w = out_h = 0;
    int w = 0, h = 0;
    uint8_t *buf = TieFontTune_ComposeText(r, codepoint_remap_or_null,
                                app.sample_text, PREVIEW_LINES,
								TieFontTune_PreviewTrackingAtlas(app),
								app.tie_classic,
								app.tie_classic && app.show_shadow,
								TieFontTune_PreviewShadowDxAtlas(app),
								TieFontTune_PreviewShadowDyAtlas(app), &w, &h);
    if (!buf) return;
    tex = TieFontTune_UploadRgba(app.device, buf, w, h);
    out_w = w; out_h = h;
    std::free(buf);
}

void TieFontTune_DrawPreviewBaselines(const TieFontTuneApp &app, int cell_h, int baseline,
							float scale) {
	if (!app.show_baseline) return;
	const ImVec2 p0 = ImGui::GetItemRectMin();
	const ImVec2 p1 = ImGui::GetItemRectMax();
	ImDrawList *draw = ImGui::GetWindowDrawList();
	const ImU32 color = IM_COL32(80, 200, 255, 200);
	for (int line = 0; line < PREVIEW_LINES; ++line) {
		const float y = p0.y + (float)(line * cell_h + baseline) * scale;
		if (y >= p0.y && y <= p1.y)
			draw->AddLine(ImVec2(p0.x, y), ImVec2(p1.x, y), color, 1.0f);
	}
}

void TieFontTune_DrawPreviewCellSeparators(const TieFontTuneApp &app, const FontAtlasResult *r,
								  const int *remap, float scale) {
	if (!app.show_cell_separators || !r || !r->glyphs) return;
	const ImVec2 p0 = ImGui::GetItemRectMin();
	const ImVec2 p1 = ImGui::GetItemRectMax();
	ImDrawList *draw = ImGui::GetWindowDrawList();
	const ImU32 color = IM_COL32(80, 200, 255, 200);
	auto line_at = [&](int pen) {
		const float x = p0.x + (float)pen * scale;
		if (x >= p0.x && x <= p1.x)
			draw->AddLine(ImVec2(x, p0.y), ImVec2(x, p1.y), color, 1.0f);
	};
	int pen = 0;
	bool first_glyph = true;
	bool any_glyph = false;
	for (const unsigned char *s = (const unsigned char *)app.sample_text; *s;) {
		unsigned cp;
		const int len = TieFontTune_DecodeUtf8(s, &cp);
		if (len <= 0) {
			++s;
			continue;
		}
		s += len;
		if (app.tie_classic && (cp == 0x01 || cp == 0x02)) continue;
		const int slot = TieFontTune_ResolveTextSlot(cp, r, remap);
		if (slot < 0) continue;
		if (first_glyph)
			line_at(0);
		else {
			pen += TieFontTune_PreviewTrackingAtlas(app);
			line_at(pen);
		}
		first_glyph = false;
		any_glyph = true;
		pen += r->glyphs[slot].advance;
	}
	if (any_glyph) line_at(pen);
}

void TieFontTune_DrawSamplePreviews(const TieFontTuneApp &app, SDL_GPUTexture *texture,
						  int width, int height, const FontAtlasResult *r,
						  const int *remap) {
	if (!texture || !r || r->cell_h <= 0) return;
	const int count = app.tie_classic ? 1 : 3;
	for (int index = 0; index < count; ++index) {
		const float scale = app.tie_classic
			? app.display_scale
			: (float)app.preview_sizes[index] / (float)r->cell_h;
		if (!app.tie_classic)
			ImGui::Text("%d px", app.preview_sizes[index]);
		ImGui::Image((ImTextureID)(uintptr_t)texture,
					 ImVec2((float)width * scale, (float)height * scale));
		TieFontTune_DrawPreviewBaselines(app, r->cell_h, r->baseline, scale);
		TieFontTune_DrawPreviewCellSeparators(app, r, remap, scale);
	}
}

/* Rebuild the TTF atlas from current params, then recompose its
 * sample-text strip and re-upload the full atlas as a picker texture.
 * Cheap (sub-ms) so safe per-frame on slider drag. */
void TieFontTune_RebuildTtf(TieFontTuneApp &app, TieFontTuneTtfEntry &e)
{
    char err[512] = {0};
    FontAtlasParams build_params = e.params;
    if (e.descender_lift_atlas > 0) {
        static constexpr int descender_codepoints[] = {
            'g', 'j', 'p', 'q', 'y', ',', ';'
        };
        for (int cp : descender_codepoints)
            build_params.compression_glyph_y_offset_atlas[cp] +=
                e.descender_lift_atlas;
    }
    if (app.tie_classic)
        TieFontTune_ApplyTieClassicBuildMetrics(build_params);
    font_atlas_free(&e.result);
    e.result_valid = font_atlas_build(e.ttf_data.data(), e.ttf_data.size(),
                                      &build_params, &e.result, err, sizeof err);
    if (e.atlas_tex) {
        SDL_ReleaseGPUTexture(app.device, e.atlas_tex);
        e.atlas_tex = nullptr;
    }
    if (!e.result_valid) {
        e.err = err;
        if (e.sample_tex) {
            SDL_ReleaseGPUTexture(app.device, e.sample_tex);
            e.sample_tex = nullptr;
        }
        e.sample_w = e.sample_h = 0;
        e.params_prev = e.params;
        e.descender_lift_prev = e.descender_lift_atlas;
        return;
    }
    if (app.tie_classic)
        TieFontTune_FinalizeTieClassicZeroAdvances(e.params, e.result);
    e.err.clear();
    TieFontTune_RecomposeSample(app, &e.result, e.params.codepoint_remap,
                     e.sample_tex, e.sample_w, e.sample_h);
    e.atlas_tex = TieFontTune_UploadRgba(app.device, e.result.rgba,
                              e.result.atlas_w, e.result.atlas_h);
    e.params_prev = e.params;
    e.descender_lift_prev = e.descender_lift_atlas;
    /* The reference's sample-text strip borrows the FIRST TTF's
     * codepoint_remap to resolve typed characters into atlas slots,
     * so any change to a TTF's params (and especially its remap) may
     * change which slot the reference should display. Flag a global
     * recompose so the reference picks it up next frame. The cost is
     * one extra compose_text per TTF param edit, which is sub-ms. */
    app.sample_dirty = true;
}

/* ===== YAML save/load of tuning state =====
 *
 * Schema (version 1):
 *   font_tune_version: 1
 *   ttf_path: /abs/or/rel/path/to/font.ttf
 *   save_basename: my_font
 *   params:
 *     <editable scalar fields>
 *     glyph_deltas:    { <cp>: <delta_pp>,        ... }
 *     glyph_advances:  { <cp>: <atlas_px_stride>, ... }
 *     glyph_lsbs:      { <cp>: <atlas_px_lsb>,    ... }
 *
 * Sparse maps for the per-glyph arrays — only non-zero entries are
 * emitted, keys are decimal codepoints. Reload zero-fills the array
 * first, so unspecified codepoints reset to natural. */

bool TieFontTune_SaveTuningYaml(const char *path, const std::string &ttf_path,
                      const std::string &save_basename,
						  const FontAtlasParams &p, int descender_lift_atlas,
						  std::string &err) {
    char tmp[2048];
    int n = std::snprintf(tmp, sizeof tmp, "%s.tmp", path);
		if (n < 0 || n >= (int) sizeof tmp) {
			err = "path too long";
			return false;
		}
    FILE *fp = std::fopen(tmp, "wb");
		if (!fp) {
			err = std::string("open: ") + tmp;
			return false;
		}

    auto emit_scalar = [&](const char *key, int v) {
        std::fprintf(fp, "  %s: %d\n", key, v);
    };
    /* Inline comment for printable ASCII codepoints to keep the file
     * legible when hand-editing. Not parsed back. `skip_if_eq` is the
     * "no override" sentinel for that array (0 for delta/advance, -1
     * for LSB) — entries equal to it aren't emitted. */
    auto emit_glyph_map = [&](const char *key, const int *arr,
                              int skip_if_eq) {
        bool any = false;
			for (int i = 0; i < 256; i++)
				if (arr[i] != skip_if_eq) {
					any = true;
					break;
				}
        if (!any) return;
        std::fprintf(fp, "  %s:\n", key);
        for (int i = 0; i < 256; i++) {
            if (arr[i] == skip_if_eq) continue;
            if (i >= 33 && i < 127)
                std::fprintf(fp, "    %d: %d  # '%c'\n", i, arr[i], (char)i);
            else
                std::fprintf(fp, "    %d: %d\n", i, arr[i]);
        }
    };

    std::fprintf(fp, "font_tune_version: 1\n");
    std::fprintf(fp, "ttf_path: %s\n", ttf_path.c_str());
    std::fprintf(fp, "save_basename: %s\n", save_basename.c_str());
    std::fprintf(fp, "params:\n");
    emit_scalar("first_char",            p.first_char);
    emit_scalar("num_chars",             p.num_chars);
    emit_scalar("cell_h",                p.cell_h);
    emit_scalar("cap_height",            p.cap_height);
    emit_scalar("baseline",              p.baseline);
    emit_scalar("cell_w",                p.cell_w);
    emit_scalar("font_index",            p.font_index);
    emit_scalar("descender_lift_atlas",  descender_lift_atlas);
    emit_scalar("tracking_atlas",        p.tracking_atlas);
    emit_scalar("space_advance_atlas",   p.space_advance_atlas);
    emit_scalar("compression_upper_pct", p.compression_upper_pct);
    emit_scalar("compression_lower_pct", p.compression_lower_pct);
    emit_scalar("compression_other_pct", p.compression_other_pct);
    emit_glyph_map("glyph_deltas",          p.compression_glyph_delta_pct,        0);
    emit_glyph_map("glyph_advances",        p.compression_glyph_advance_atlas,    0);
    emit_glyph_map("glyph_lsbs",            p.compression_glyph_lsb_atlas,       -1);
    emit_glyph_map("glyph_boldnesses",      p.compression_glyph_boldness_atlas,   0);
    emit_glyph_map("glyph_codepoint_remap", p.codepoint_remap,                    0);
    emit_glyph_map("glyph_y_offsets",       p.compression_glyph_y_offset_atlas,   0);

    if (std::fflush(fp) != 0 || std::fclose(fp) != 0) {
        err = "write failed";
        std::remove(tmp);
        return false;
    }
    if (std::rename(tmp, path) != 0) {
        err = std::string("rename: ") + path;
        std::remove(tmp);
        return false;
    }
    return true;
}

	void TieFontTune_LoadIntField(const AeronConfigNode *map, const char *key, int *dst) {
		const AeronConfigNode *node = AeronConfigNode_MapGet(map, key);
		if (AeronConfigNode_Type(node) == AERON_CONFIG_INT)
			*dst = static_cast<int>(AeronConfigNode_Int(node, *dst));
}

/* Walk the named mapping and assign listed codepoints into arr256.
 * Caller is responsible for pre-initialising the array to its
 * "no-override" baseline before calling — 0 for delta/advance arrays,
 * -1 for the LSB array. We don't memset here because the LSB sentinel
 * differs from the delta/advance one. */
	void TieFontTune_LoadGlyphMap(const AeronConfigNode *map, const char *key,
						int *arr256) {
		const AeronConfigNode *glyphs = AeronConfigNode_MapGet(map, key);
		if (AeronConfigNode_Type(glyphs) != AERON_CONFIG_MAP) return;
		for (size_t index = 0; index < AeronConfigNode_MapCount(glyphs); ++index) {
			int cp = std::atoi(AeronConfigNode_MapKeyAt(glyphs, index));
			const AeronConfigNode *value = AeronConfigNode_MapValueAt(glyphs, index);
			if (AeronConfigNode_Type(value) != AERON_CONFIG_INT) continue;
			int v = static_cast<int>(AeronConfigNode_Int(value, 0));
			if (cp < 0 || cp >= 256) continue;
			arr256[cp] = v;
		}
	}

	AeronVfs *TieFontTune_TuningVfs(const char *path, char *file_name, size_t capacity) {
		if (!path || !path[0]) return nullptr;
		const char *slash = std::strrchr(path, '/');
		const char *backslash = std::strrchr(path, '\\');
		const char *separator = !slash ? backslash : !backslash  ? slash
											 : slash > backslash ? slash
																 : backslash;
		char directory[2048];
		AeronVfsConfig config{};
		if (!separator) {
			std::snprintf(directory, sizeof directory, "%s", ".");
			if (std::strlen(path) >= capacity) return nullptr;
			std::memcpy(file_name, path, std::strlen(path) + 1);
		} else {
			size_t length = static_cast<size_t>(separator - path);
			if (length == 0) length = 1;
			if (length >= sizeof directory) return nullptr;
			std::memcpy(directory, path, length);
			directory[length] = '\0';
			if (std::strlen(separator + 1) >= capacity) return nullptr;
			std::memcpy(file_name, separator + 1,
						std::strlen(separator + 1) + 1);
		}
		config.asset_root = directory;
		config.resource_root = directory;
		config.user_root = directory;
		config.temp_root = directory;
		return AeronVfs_Create(&config);
	}

	bool TieFontTune_LoadTuningYaml(const char *path, std::string &ttf_path_out,
						  std::string &save_basename_out,
						  FontAtlasParams &p_out,
						  int &descender_lift_atlas_out,
						  std::string &err) {
		char file_name[1024];
		AeronVfs *vfs = TieFontTune_TuningVfs(path, file_name, sizeof file_name);
		AeronConfigFile *document = nullptr;
		AeronConfigError config_error{};
		if (!vfs || !AeronConfigFile_LoadYamlEx(vfs, AERON_VFS_ROOT_RESOURCE,
												file_name, &document, &config_error)) {
			err = vfs ? config_error.message : "invalid tuning file path";
			AeronVfs_Destroy(vfs);
			return false;
		}
		const AeronConfigNode *root = AeronConfigFile_Root(document);
		if (AeronConfigNode_Type(root) != AERON_CONFIG_MAP) {
			err = "root is not a mapping";
			AeronConfigFile_Destroy(document);
			AeronVfs_Destroy(vfs);
			return false;
		}

		/* Missing version keys mean version 1. */
		const AeronConfigNode *version = AeronConfigNode_MapGet(root, "font_tune_version");
		if (version && AeronConfigNode_Int(version, -1) != 1) {
			err = "unsupported font_tune_version";
			AeronConfigFile_Destroy(document);
			AeronVfs_Destroy(vfs);
			return false;
		}

		const char *t = AeronConfigNode_String(AeronConfigNode_MapGet(root, "ttf_path"), nullptr);
		ttf_path_out = t ? t : "";
		const char *b = AeronConfigNode_String(AeronConfigNode_MapGet(root, "save_basename"), nullptr);
		save_basename_out = b ? b : "";

		std::memset(&p_out, 0, sizeof p_out);
		descender_lift_atlas_out = 0;
		/* Use -1 as the LSB sentinel so unspecified glyphs retain their
		 * natural left-side bearing. */
		std::memset(p_out.compression_glyph_lsb_atlas, 0xFF,
					sizeof p_out.compression_glyph_lsb_atlas);
		const AeronConfigNode *params = AeronConfigNode_MapGet(root, "params");
		if (params) {
			TieFontTune_LoadIntField(params, "first_char", &p_out.first_char);
			TieFontTune_LoadIntField(params, "num_chars", &p_out.num_chars);
			TieFontTune_LoadIntField(params, "cell_h", &p_out.cell_h);
			TieFontTune_LoadIntField(params, "cap_height", &p_out.cap_height);
			TieFontTune_LoadIntField(params, "baseline", &p_out.baseline);
			TieFontTune_LoadIntField(params, "cell_w", &p_out.cell_w);
			TieFontTune_LoadIntField(params, "font_index", &p_out.font_index);
			TieFontTune_LoadIntField(params, "descender_lift_atlas", &descender_lift_atlas_out);
			TieFontTune_LoadIntField(params, "tracking_atlas", &p_out.tracking_atlas);
			TieFontTune_LoadIntField(params, "space_advance_atlas", &p_out.space_advance_atlas);
			TieFontTune_LoadIntField(params, "compression_upper_pct", &p_out.compression_upper_pct);
			TieFontTune_LoadIntField(params, "compression_lower_pct", &p_out.compression_lower_pct);
			TieFontTune_LoadIntField(params, "compression_other_pct", &p_out.compression_other_pct);
			TieFontTune_LoadGlyphMap(params, "glyph_deltas", p_out.compression_glyph_delta_pct);
			TieFontTune_LoadGlyphMap(params, "glyph_advances", p_out.compression_glyph_advance_atlas);
			TieFontTune_LoadGlyphMap(params, "glyph_lsbs", p_out.compression_glyph_lsb_atlas);
			TieFontTune_LoadGlyphMap(params, "glyph_boldnesses", p_out.compression_glyph_boldness_atlas);
			TieFontTune_LoadGlyphMap(params, "glyph_codepoint_remap", p_out.codepoint_remap);
			TieFontTune_LoadGlyphMap(params, "glyph_y_offsets", p_out.compression_glyph_y_offset_atlas);
		}

		AeronConfigFile_Destroy(document);
		AeronVfs_Destroy(vfs);
		return true;
	}
/* Add a TTF entry from a file path. Defaults its params to match the
 * loaded reference's cell_h / baseline so a freshly-added TTF lines up
 * with the reference visually before any slider tweaks. */
void TieFontTune_AddTtfFromPath(TieFontTuneApp &app, const std::string &path)
{
    TieFontTuneTtfEntry e;
    std::string err;
    if (!TieFontTune_SlurpFile(path.c_str(), e.ttf_data, err)) {
        std::fprintf(stderr, "font_tune: %s\n", err.c_str());
        return;
    }
    e.id   = app.next_ttf_id++;
    e.path = path;
    if (app.ref_loaded) {
        e.params.first_char = app.ref_atlas.first_char;
        e.params.num_chars  = app.ref_atlas.num_chars;
        e.params.cell_h     = app.ref_atlas.cell_h;
        e.params.baseline   = app.ref_atlas.baseline;
    } else {
        e.params.first_char = 32;
		e.params.num_chars  = app.tie_classic ? 96 : 95;
		e.params.cell_h     = app.tie_classic ? 86 : 72;
		e.params.baseline   = app.tie_classic ? 65 : -1;
    }
    e.params.cap_height = 0;   /* auto */
    e.params.cell_w     = 0;
    e.params.font_index = 0;
    /* Default compression to 100% so freshly-added TTFs render at their
     * natural width; sliders also start at the neutral midpoint. */
    e.params.compression_upper_pct = 100;
    e.params.compression_lower_pct = 100;
    e.params.compression_other_pct = 100;
    /* LSB override sentinel: <0 means "natural LSB". memset to 0xFF
     * fills every byte with 0xFF, producing -1 for each int regardless
     * of width. Without this, TieFontTuneTtfEntry's default-init leaves the array
     * full of zeros, which would render every glyph flush-to-cell-edge. */
    std::memset(e.params.compression_glyph_lsb_atlas, 0xFF,
                sizeof e.params.compression_glyph_lsb_atlas);
    /* Default save basename: derived from TTF filename, no extension. */
    auto slash = path.find_last_of("/\\");
    auto base  = (slash == std::string::npos) ? path : path.substr(slash + 1);
    auto dot   = base.find_last_of('.');
    e.save_basename = (dot == std::string::npos) ? base : base.substr(0, dot);
    /* Build now so the user sees something immediately. */
    TieFontTune_RebuildTtf(app, e);
    app.ttfs.push_back(std::move(e));
}

/* SDL3 file-dialog callback; userdata is the TieFontTuneApp. */
	void SDLCALL TieFontTune_TtfDialogCb(void *userdata, const char *const *filelist, int /*filter*/) {
    if (!filelist) return;   /* error */
    TieFontTuneApp *app = (TieFontTuneApp *)userdata;
    for (const char *const *p = filelist; *p; ++p)
        TieFontTune_AddTtfFromPath(*app, *p);
}

/* Add a TTF entry with caller-supplied params + save_basename — used
 * by the "Add tuning (YAML)..." path that already parsed both. */
void TieFontTune_AddTtfWithParams(TieFontTuneApp &app, const std::string &ttf_path,
                         const FontAtlasParams &params,
						 int descender_lift_atlas,
							 const std::string &save_basename) {
    TieFontTuneTtfEntry e;
    std::string err;
    if (!TieFontTune_SlurpFile(ttf_path.c_str(), e.ttf_data, err)) {
        std::fprintf(stderr, "font_tune: %s\n", err.c_str());
        return;
    }
    e.id            = app.next_ttf_id++;
    e.path          = ttf_path;
    e.params        = params;
    e.descender_lift_atlas = descender_lift_atlas;
    e.save_basename = save_basename;
    TieFontTune_RebuildTtf(app, e);
    app.ttfs.push_back(std::move(e));
}

/* Lookup a TTF entry by stable id. NULL when removed since the
 * dialog opened. */
	TieFontTuneTtfEntry *TieFontTune_FindTtf(TieFontTuneApp &app, int id) {
		for (auto &e: app.ttfs)
			if (e.id == id) return &e;
    return nullptr;
}

/* Userdata for the per-TTF tuning dialogs. Allocated with `new` when
 * the dialog is shown; freed in the callback regardless of outcome.
 * `entry_id` -1 means "no specific entry — load YAML and add as a
 * new TTF". */
struct TieFontTuneDialogContext {
    TieFontTuneApp *app;
    int  entry_id;
};

	void SDLCALL TieFontTune_SaveTuningCb(void *userdata, const char *const *filelist, int /*filter*/) {
    auto *ctx = (TieFontTuneDialogContext *)userdata;
    if (filelist && filelist[0]) {
        TieFontTuneTtfEntry *e = TieFontTune_FindTtf(*ctx->app, ctx->entry_id);
        if (e) {
            std::string err;
            if (TieFontTune_SaveTuningYaml(filelist[0], e->path, e->save_basename,
                                 e->params, e->descender_lift_atlas, err)) {
                e->save_msg = std::string("saved tuning ") + filelist[0];
                e->save_ok  = true;
            } else {
                e->save_msg = std::string("save tuning failed: ") + err;
                e->save_ok  = false;
            }
        }
    }
    delete ctx;
}

void SDLCALL TieFontTune_LoadTuningIntoExistingCb(void *userdata,
                                          const char *const *filelist,
											  int /*filter*/) {
    auto *ctx = (TieFontTuneDialogContext *)userdata;
    if (filelist && filelist[0]) {
        TieFontTuneTtfEntry *e = TieFontTune_FindTtf(*ctx->app, ctx->entry_id);
        if (e) {
            std::string ttf_path, save_basename, err;
            FontAtlasParams params;
            int descender_lift_atlas;
			if (TieFontTune_LoadTuningYaml(filelist[0], ttf_path, save_basename,
								 params, descender_lift_atlas, err)) {
                e->params = params;
                e->descender_lift_atlas = descender_lift_atlas;
                if (!save_basename.empty()) e->save_basename = save_basename;
                e->save_msg = std::string("loaded tuning ") + filelist[0];
                e->save_ok  = true;
                /* The main loop's params memcmp picks up the changes
                 * and triggers a rebuild on the next frame. */
            } else {
                e->save_msg = std::string("load tuning failed: ") + err;
                e->save_ok  = false;
            }
        }
    }
    delete ctx;
}

void SDLCALL TieFontTune_LoadTuningAsNewCb(void *userdata,
                                   const char *const *filelist,
									   int /*filter*/) {
    auto *ctx = (TieFontTuneDialogContext *)userdata;
    if (filelist && filelist[0]) {
        std::string ttf_path, save_basename, err;
        FontAtlasParams params;
        int descender_lift_atlas;
		if (TieFontTune_LoadTuningYaml(filelist[0], ttf_path, save_basename,
							 params, descender_lift_atlas, err)) {
            if (ttf_path.empty())
                std::fprintf(stderr,
                    "font_tune: %s has no ttf_path\n", filelist[0]);
            else
                TieFontTune_AddTtfWithParams(*ctx->app, ttf_path,
                                    params, descender_lift_atlas,
                                    save_basename);
        } else {
            std::fprintf(stderr, "font_tune: %s\n", err.c_str());
        }
    }
    delete ctx;
}

	void TieFontTune_Usage(const char *argv0) {
    std::fprintf(stderr,
        "Usage: %s [--tie-classic] [--reference <atlas_basename>]\n"
		"          [--ttf <font.ttf>]...\n"
        "\n"
		"  --tie-classic  enable TIE's classic 9-px spacer and shadow\n"
		"  --reference    optional .png+.fnt basename for comparison\n"
        "  --ttf        TTF to load on startup (repeatable). Additional TTFs\n"
		"               can be added through the GUI. At least one --ttf or\n"
		"               --reference is required.\n",
        argv0);
}

}// namespace

int main(int argc, char **argv) {
    const char *ref_basename = nullptr;
    bool tie_classic = false;
    std::vector<const char *> initial_ttfs;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        const char *v = (i + 1 < argc) ? argv[i + 1] : nullptr;
		if (std::strcmp(a, "--reference") == 0 && v) {
			ref_basename = v;
			i++;
		} else if (std::strcmp(a, "--tie-classic") == 0) {
			tie_classic = true;
		} else if (std::strcmp(a, "--ttf") == 0 && v) {
			initial_ttfs.push_back(v);
			i++;
		} else if (std::strcmp(a, "-h") == 0 || std::strcmp(a, "--help") == 0) {
			TieFontTune_Usage(argv[0]);
			return 0;
		} else {
			std::fprintf(stderr, "unknown arg: %s\n", a);
			TieFontTune_Usage(argv[0]);
			return 2;
        }
    }
	if (!ref_basename && initial_ttfs.empty()) {
		TieFontTune_Usage(argv[0]);
		return 2;
	}

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }

    TieFontTuneApp app;
	app.tie_classic = tie_classic;
	app.show_shadow = tie_classic;
    app.dpi_scale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
    if (app.dpi_scale <= 0.0f) app.dpi_scale = 1.0f;

    int win_w = (int)(1280 * app.dpi_scale);
    int win_h = (int)(800  * app.dpi_scale);
    app.window = SDL_CreateWindow("font_tune", win_w, win_h,
                                  SDL_WINDOW_RESIZABLE |
                                  SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!app.window) {
        std::fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
		SDL_Quit();
		return 1;
    }

    app.device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV |
                                     SDL_GPU_SHADERFORMAT_DXIL  |
                                     SDL_GPU_SHADERFORMAT_MSL,
                                     /*debug=*/false, nullptr);
    if (!app.device) {
        std::fprintf(stderr, "SDL_CreateGPUDevice: %s\n", SDL_GetError());
		SDL_DestroyWindow(app.window);
		SDL_Quit();
		return 1;
    }
    if (!SDL_ClaimWindowForGPUDevice(app.device, app.window)) {
        std::fprintf(stderr, "ClaimWindow: %s\n", SDL_GetError());
        SDL_DestroyGPUDevice(app.device);
		SDL_DestroyWindow(app.window);
		SDL_Quit();
		return 1;
    }
    app.swap_fmt = SDL_GetGPUSwapchainTextureFormat(app.device, app.window);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    {
        ImGuiStyle &style = ImGui::GetStyle();
        style.ScaleAllSizes(app.dpi_scale);
        style.FontScaleDpi = app.dpi_scale;
    }
    ImGui_ImplSDL3_InitForSDLGPU(app.window);
    ImGui_ImplSDLGPU3_InitInfo imgui_info{};
    imgui_info.Device            = app.device;
    imgui_info.ColorTargetFormat = app.swap_fmt;
    imgui_info.MSAASamples       = SDL_GPU_SAMPLECOUNT_1;
    ImGui_ImplSDLGPU3_Init(&imgui_info);

    /* Reference atlas is optional. Keep going on failure so the user can
     * still tune startup TTFs without it. */
    if (ref_basename) {
        char err[512] = {0};
        if (font_atlas_load(ref_basename, &app.ref_atlas, err, sizeof err)) {
            app.ref_loaded   = true;
            app.ref_basename = ref_basename;
        } else {
            std::fprintf(stderr, "font_tune: --reference '%s': %s\n",
                         ref_basename, err);
        }
    }

    /* Initial TTFs. Each rebuilds its atlas + sample text immediately. */
    for (auto p : initial_ttfs) TieFontTune_AddTtfFromPath(app, p);

    bool running = true;
    while (running) {
        /* Idle pacing. The original loop called SDL_PollEvent (non-blocking)
         * and re-rendered every iteration — at vsync that's ~60 ImGui
         * rebuilds + GPU submits per second with zero input, burning CPU.
         *
         * Block up to 100 ms in the kernel for the next event. While the
         * user interacts (mouse moves, slider drag, text input) events
         * arrive continuously and the wait returns immediately, so the UI
         * stays as responsive as before. With nothing happening, idle drops
         * to ~10 frames/sec — enough to keep ImGui's time-driven animations
         * (text-cursor blink, hover fades) ticking, but two orders of
         * magnitude less work than vsync-bounded busy redraw. */
        SDL_Event ev;
        bool got_event = SDL_WaitEventTimeout(&ev, 100);
        if (got_event) {
            do {
                ImGui_ImplSDL3_ProcessEvent(&ev);
                if (ev.type == SDL_EVENT_QUIT) running = false;
                else if (ev.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
                         ev.window.windowID == SDL_GetWindowID(app.window))
                    running = false;
            } while (SDL_PollEvent(&ev));
        }

        /* Sample text edited → re-compose every strip (atlases stay
         * valid since glyph metrics didn't change). The reference has
         * no params of its own; reuse the first TTF's codepoint_remap
         * so a typed character like '™' resolves to whichever atlas
         * slot the user remapped it to in their tuning. */
        if (app.sample_dirty) {
            app.sample_dirty = false;
            const int *ref_remap = app.ttfs.empty()
                ? nullptr
                : app.ttfs.front().params.codepoint_remap;
            if (app.ref_loaded)
                TieFontTune_RecomposeSample(app, &app.ref_atlas, ref_remap,
                                 app.ref_sample_tex,
                                 app.ref_sample_w, app.ref_sample_h);
            for (auto &e : app.ttfs)
                if (e.result_valid)
                    TieFontTune_RecomposeSample(app, &e.result,
                                     e.params.codepoint_remap,
                                     e.sample_tex,
                                     e.sample_w, e.sample_h);
        }

        /* Rebuild any TTF whose tuning changed. */
        for (auto &e : app.ttfs) {
            if (std::memcmp(&e.params, &e.params_prev, sizeof e.params) != 0 ||
                e.descender_lift_atlas != e.descender_lift_prev)
                TieFontTune_RebuildTtf(app, e);
        }

        ImGui_ImplSDLGPU3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        const ImGuiViewport *vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(vp->Pos);
        ImGui::SetNextWindowSize(vp->Size);
        ImGui::Begin("font_tune", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoMove     | ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_NoBringToFrontOnFocus);

        ImGui::Text("Mode: %s", app.tie_classic ? "TIE classic" : "generic");
        ImGui::Text("Sample text");
        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputText("##sample", app.sample_text,
                             sizeof app.sample_text))
            app.sample_dirty = true;
		if (app.tie_classic) {
			ImGui::SliderFloat("Display scale", &app.display_scale,
							   0.1f, 2.0f, "%.2f");
			if (ImGui::Checkbox("Classic text shadow", &app.show_shadow))
				app.sample_dirty = true;
		} else {
			ImGui::Text("Preview sizes (logical px)");
			for (int index = 0; index < 3; ++index) {
				ImGui::PushID(index);
				ImGui::SetNextItemWidth(100.0f * app.dpi_scale);
				if (ImGui::InputInt("##preview_size", &app.preview_sizes[index],
								1, 4)) {
					if (app.preview_sizes[index] < 1) app.preview_sizes[index] = 1;
					if (app.preview_sizes[index] > 512) app.preview_sizes[index] = 512;
				}
				ImGui::PopID();
				if (index < 2) ImGui::SameLine();
			}
		}
        ImGui::SameLine();
        ImGui::Checkbox("Show baseline", &app.show_baseline);
        ImGui::SameLine();
        ImGui::Checkbox("Show cell separators", &app.show_cell_separators);
        ImGui::Separator();

        /* Reference card — header + metrics + sample image. No tunables;
         * the bitmap atlas is the truth we're measuring against. */
        if (app.ref_loaded) {
            ImGui::Text("REFERENCE  %s", app.ref_basename.c_str());
            ImGui::Text("cell=%dx%d  baseline=%d  max_advance=%d",
                        app.ref_atlas.cell_w, app.ref_atlas.cell_h,
                        app.ref_atlas.baseline, app.ref_atlas.max_advance_atlas);
            /* Show the reference's final visible space stride under the
             * active preview convention. */
            int ref_space_idx = ' ' - app.ref_atlas.first_char;
            if (ref_space_idx >= 0 && ref_space_idx < app.ref_atlas.num_chars) {
                int baked = (int)app.ref_atlas.glyphs[ref_space_idx].advance;
				int stride = baked + TieFontTune_PreviewTrackingAtlas(app);
				if (app.tie_classic)
					ImGui::Text("space stride: %d atlas-px (= %.1f classic-px)",
								stride, stride / 9.0f);
				else
					ImGui::Text("space advance: %d atlas-px", stride);
            }
            if (app.ref_sample_tex) {
                /* Reuse the first TTF's codepoint_remap for the
                 * reference's separators so positions match what
                 * compose_text computed for the text strip. */
                const int *ref_remap = app.ttfs.empty()
                    ? nullptr
                    : app.ttfs.front().params.codepoint_remap;
				TieFontTune_DrawSamplePreviews(app, app.ref_sample_tex,
								 app.ref_sample_w, app.ref_sample_h,
								 &app.ref_atlas, ref_remap);
            }
            ImGui::Separator();
        }

        /* Per-TTF cards. */
        int delete_idx = -1;
        for (size_t i = 0; i < app.ttfs.size(); i++) {
            ImGui::PushID((int)i);
            TieFontTuneTtfEntry &e = app.ttfs[i];
            ImGui::Text("TTF  %s", e.path.c_str());

            /* Slider ranges sized for the typical bitmap-replacement
             * use case (cell_h 40..200). cell_h doubles as the upper
             * bound on cap_height / baseline so they can't drift past
             * the cell. Sliders are laid out in 3 columns to keep the
             * per-TTF card compact:
             *   col 1: vertical metrics (cell_h, cap_height, baseline)
             *   col 2: spacing         (descender lift, tracking, space)
             *   col 3: compression     (upper, lower, other) */
            int cell_max = std::max(e.params.cell_h * 2, 256);
            if (ImGui::BeginTable("ttf_sliders", 3,
                                  ImGuiTableFlags_SizingStretchSame |
                                  ImGuiTableFlags_NoSavedSettings)) {
                /* Row 1 */
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::SetNextItemWidth(-FLT_MIN);
                ImGui::SliderInt("##cell_h", &e.params.cell_h,
                                 20, cell_max, "cell_h %d");
                ImGui::TableSetColumnIndex(1);
                ImGui::SetNextItemWidth(-FLT_MIN);
                /* Descender lift shifts common descender-bearing glyphs
                 * upward together. Per-glyph y-offsets handle exceptions. */
                ImGui::SliderInt("##descender_lift",
                                 &e.descender_lift_atlas, 0, 30,
                                 "desc lift %d");
                ImGui::TableSetColumnIndex(2);
                ImGui::SetNextItemWidth(-FLT_MIN);
                ImGui::SliderInt("##compr_upper",
                                 &e.params.compression_upper_pct,
                                 50, 150, "upper %d%%");

                /* Row 2 */
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::SetNextItemWidth(-FLT_MIN);
                ImGui::SliderInt("##cap_height", &e.params.cap_height,
                                 0, e.params.cell_h, "cap %d (0=auto)");
                ImGui::TableSetColumnIndex(1);
                ImGui::SetNextItemWidth(-FLT_MIN);
                /* Tracking — uniform per-glyph stride offset. Negative
                 * tightens letter-spacing, positive loosens. */
                ImGui::SliderInt("##tracking", &e.params.tracking_atlas,
                                 -20, 20, "tracking %+d");
                ImGui::TableSetColumnIndex(2);
                ImGui::SetNextItemWidth(-FLT_MIN);
                ImGui::SliderInt("##compr_lower",
                                 &e.params.compression_lower_pct,
                                 50, 150, "lower %d%%");

                /* Row 3 */
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::SetNextItemWidth(-FLT_MIN);
                ImGui::SliderInt("##baseline", &e.params.baseline,
                                 0, e.params.cell_h, "baseline %d");
                ImGui::TableSetColumnIndex(1);
                ImGui::SetNextItemWidth(-FLT_MIN);
                /* Optional final visible space stride override. */
                ImGui::SliderInt("##space_stride",
                                 &e.params.space_advance_atlas, 0, 120,
                                 "space %d (0=natural)");
                ImGui::TableSetColumnIndex(2);
                ImGui::SetNextItemWidth(-FLT_MIN);
                ImGui::SliderInt("##compr_other",
                                 &e.params.compression_other_pct,
                                 50, 150, "digits/punct %d%%");

                ImGui::EndTable();
            }

            if (e.result_valid) {
                ImGui::Text("ascent=%d  cap_height=%d  descent=%d  max_advance=%d",
                            e.result.ascent_atlas, e.result.cap_height_atlas,
                            e.result.descent_atlas, e.result.max_advance_atlas);
                if (e.result.ascender_clip_px || e.result.descender_clip_px) {
                    ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f),
                        "WARNING ascender clip=%d  descender clip=%d",
                        e.result.ascender_clip_px, e.result.descender_clip_px);
                }
				if (e.result.neg_lsb_clip_px || e.result.oversize_x ||
					e.result.oversize_y) {
					ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.2f, 1.0f),
						"WARNING bitmap clip: left=%d right=%d bottom=%d",
						e.result.neg_lsb_clip_px, e.result.oversize_x,
						e.result.oversize_y);
				}
                if (e.sample_tex) {
					TieFontTune_DrawSamplePreviews(app, e.sample_tex, e.sample_w,
									 e.sample_h, &e.result,
									 e.params.codepoint_remap);
                }

                /* Per-glyph adjustments are collapsed by default. The
                 * atlas picker exposes sizing, positioning, boldness and
                 * codepoint remapping for exceptional glyphs. */
				if (ImGui::CollapsingHeader("Per-glyph adjustments")) {
                    /* Auto-match + reset row. Auto-match needs a
                     * reference atlas to target; greyed out otherwise. */
                    bool can_auto = app.ref_loaded && e.result_valid;
                    ImGui::BeginDisabled(!can_auto);
                    if (ImGui::Button("Auto-match advances to reference")) {
                        /* Ink-aware auto-match (Option 3): treat
                         * compression and advance as independent.
                         *   compression  ← match ink width
                         *                  (so 'i'/'l'/'!' don't get
                         *                  squished — their stem stays
                         *                  the same width; only glyphs
                         *                  whose ink should genuinely
                         *                  shrink do)
                         *   advance      ← match runtime stride
                         *                  via per-glyph advance
                         *                  override (decoupled from
                         *                  compression, so bearings
                         *                  shrink even when the stem
                         *                  doesn't).
                         * For glyphs without measurable ink (e.g. ' '
                         * itself, or font-missing codepoints), only
                         * advance is matched. */
                        int first = std::max(app.ref_atlas.first_char,
                                             e.result.first_char);
						int last = std::min(app.ref_atlas.first_char + app.ref_atlas.num_chars,
											e.result.first_char + e.result.num_chars);
                        for (int cp = first; cp < last; cp++) {
                            if (cp < 0 || cp >= 256) continue;
                            int gr = cp - app.ref_atlas.first_char;
                            int gc = cp - e.result.first_char;
                            int ref_baked = app.ref_atlas.glyphs[gr].advance;
							int target_stride =
								ref_baked + TieFontTune_PreviewTrackingAtlas(app);

                            if (cp == ' ') {
                                /* Space — drive the dedicated space
                                 * override; ink would always be 0 here. */
                                e.params.space_advance_atlas = target_stride;
                                e.params.compression_glyph_delta_pct[cp] = 0;
                                e.params.compression_glyph_advance_atlas[cp] = 0;
                                e.params.compression_glyph_lsb_atlas[cp] = -1;
                                continue;
                            }

                            /* Advance: per-glyph override in atlas-px. */
                            e.params.compression_glyph_advance_atlas[cp] =
                                target_stride;

                            /* LSB: align ink position with the reference's,
                             * so candidate ink and reference ink land at
                             * the same offset within their (now-equal)
                             * stride. Without this step, advance match
                             * alone produces uneven spacing because the
                             * candidate's natural LSB differs from the
                             * reference's. Apply when the reference has
                             * measurable ink (use ink_widths[gr] > 0 as
                             * the signal — ref_lsb itself can legitimately
                             * be 0 for glyphs whose ink starts flush at
                             * the cell edge). Otherwise leave natural. */
							if (app.ref_atlas.ink_lsbs && app.ref_atlas.ink_widths && app.ref_atlas.ink_widths[gr] > 0) {
                                e.params.compression_glyph_lsb_atlas[cp] =
                                    app.ref_atlas.ink_lsbs[gr];
                            } else {
                                e.params.compression_glyph_lsb_atlas[cp] = -1;
                            }

                            /* Compression: match ink width. The current
                             * candidate ink_widths[gc] is at the current
                             * total_pct; same ratio trick as the advance
                             * version (cur_total * target / current). */
                            int ref_ink = app.ref_atlas.ink_widths
												  ? app.ref_atlas.ink_widths[gr]
												  : 0;
                            int can_ink = e.result.ink_widths
												  ? e.result.ink_widths[gc]
												  : 0;
                            if (ref_ink <= 0 || can_ink <= 0) {
                                /* Fallback: no usable ink measurement
                                 * (font missing the codepoint, or all-
                                 * whitespace bitmap). Leave compression
                                 * alone; the advance override above is
                                 * still effective. */
                                continue;
                            }
                            int class_pct;
                            if      (cp >= 'A' && cp <= 'Z') class_pct = e.params.compression_upper_pct > 0 ? e.params.compression_upper_pct : 100;
							else if (cp >= 'a' && cp <= 'z')
								class_pct = e.params.compression_lower_pct > 0 ? e.params.compression_lower_pct : 100;
							else
								class_pct = e.params.compression_other_pct > 0 ? e.params.compression_other_pct : 100;
							int cur_total = class_pct + e.params.compression_glyph_delta_pct[cp];
                            if (cur_total < 10)  cur_total = 10;
                            if (cur_total > 200) cur_total = 200;
							int new_total = (int) ((float) cur_total * (float) ref_ink / (float) can_ink + 0.5f);
                            if (new_total < 10)  new_total = 10;
                            if (new_total > 200) new_total = 200;
                            e.params.compression_glyph_delta_pct[cp] =
                                new_total - class_pct;
                        }
                    }
                    ImGui::EndDisabled();
                    ImGui::SameLine();
                    if (ImGui::Button("Reset all deltas")) {
                        std::memset(e.params.compression_glyph_delta_pct, 0,
                                    sizeof e.params.compression_glyph_delta_pct);
                        std::memset(e.params.compression_glyph_advance_atlas, 0,
                                    sizeof e.params.compression_glyph_advance_atlas);
                        /* LSB sentinel for "natural" is -1, not 0 — fill
                         * with 0xFF so each int reads back as -1. */
                        std::memset(e.params.compression_glyph_lsb_atlas, 0xFF,
                                    sizeof e.params.compression_glyph_lsb_atlas);
                        std::memset(e.params.compression_glyph_boldness_atlas, 0,
                                    sizeof e.params.compression_glyph_boldness_atlas);
                        std::memset(e.params.codepoint_remap, 0,
                                    sizeof e.params.codepoint_remap);
                        std::memset(e.params.compression_glyph_y_offset_atlas, 0,
                                    sizeof e.params.compression_glyph_y_offset_atlas);
                        e.selected_cp = -1;
                    }

                    /* Picker grid. Aim for ~36 atlas-px-tall cells; the
                     * scale is shared with the highlight overlay below. */
                    float picker_scale = 36.0f / (float)e.result.cell_h;
                    if (picker_scale < 0.15f) picker_scale = 0.15f;
                    if (picker_scale > 1.00f) picker_scale = 1.00f;
                    if (e.atlas_tex) {
                        ImVec2 grid_size((float)e.result.atlas_w * picker_scale,
                                         (float)e.result.atlas_h * picker_scale);
                        ImGui::Image((ImTextureID)(uintptr_t)e.atlas_tex,
                                     grid_size);
                        ImVec2 grid_p0 = ImGui::GetItemRectMin();
                        /* Click maps mouse pos within the image to
                         * (col, row) → glyph index. */
                        if (ImGui::IsItemHovered() &&
                            ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                            ImVec2 mp = ImGui::GetMousePos();
                            float lx = mp.x - grid_p0.x;
                            float ly = mp.y - grid_p0.y;
                            int rs_h = e.result.cell_h + 1;   /* ROW_GUTTER */
                            int col = (int)(lx / (e.result.cell_w * picker_scale));
                            int row = (int)(ly / (rs_h * picker_scale));
                            if (col >= 0 && col < 16 && row >= 0) {
                                int gi = row * 16 + col;
                                if (gi >= 0 && gi < e.result.num_chars)
                                    e.selected_cp = e.result.first_char + gi;
                            }
                        }
                        /* Selection highlight (drawn via window draw list
                         * so it overlays the image without affecting layout). */
                        if (e.selected_cp >= e.result.first_char &&
                            e.selected_cp < e.result.first_char + e.result.num_chars) {
                            int gi = e.selected_cp - e.result.first_char;
                            int col = gi % 16;
                            int row = gi / 16;
                            int rs_h = e.result.cell_h + 1;
                            ImVec2 r0(grid_p0.x + col * e.result.cell_w * picker_scale,
                                      grid_p0.y + row * rs_h * picker_scale);
                            ImVec2 r1(r0.x + e.result.cell_w * picker_scale,
                                      r0.y + e.result.cell_h * picker_scale);
                            ImGui::GetWindowDrawList()->AddRect(r0, r1,
                                IM_COL32(255, 220, 0, 255), 0.0f, 0, 2.0f);
                        }
                    }

                    /* Selected-glyph editor. Greyed-out placeholder when
                     * nothing's selected to keep the layout stable. */
                    int cp = e.selected_cp;
                    bool sel_valid = cp >= e.result.first_char &&
                                     cp < e.result.first_char + e.result.num_chars &&
                                     cp >= 0 && cp < 256;
                    if (sel_valid) {
                        int gi = cp - e.result.first_char;
                        int class_pct;
                        const char *cls;
                        if      (cp >= 'A' && cp <= 'Z') {
                            cls = "upper";
                            class_pct = e.params.compression_upper_pct > 0
												? e.params.compression_upper_pct
												: 100;
                        } else if (cp >= 'a' && cp <= 'z') {
                            cls = "lower";
                            class_pct = e.params.compression_lower_pct > 0
												? e.params.compression_lower_pct
												: 100;
                        } else {
                            cls = "other";
                            class_pct = e.params.compression_other_pct > 0
												? e.params.compression_other_pct
												: 100;
                        }
						int total = class_pct + e.params.compression_glyph_delta_pct[cp];
                        if (total < 10)  total = 10;
                        if (total > 200) total = 200;
                        int can_baked = e.result.glyphs[gi].advance;
                        int can_ink   = e.result.ink_widths
											  ? e.result.ink_widths[gi]
											  : 0;
                        int can_lsb   = e.result.ink_lsbs
											  ? e.result.ink_lsbs[gi]
											  : 0;
                        int ref_baked = -1, ref_ink = -1, ref_lsb = -1;
						if (app.ref_loaded && cp >= app.ref_atlas.first_char && cp < app.ref_atlas.first_char + app.ref_atlas.num_chars) {
                            int gref = cp - app.ref_atlas.first_char;
                            ref_baked = app.ref_atlas.glyphs[gref].advance;
                            if (app.ref_atlas.ink_widths)
                                ref_ink = app.ref_atlas.ink_widths[gref];
                            if (app.ref_atlas.ink_lsbs)
                                ref_lsb = app.ref_atlas.ink_lsbs[gref];
                        }

                        /* Printable codepoints: show as 'X'; otherwise hex. */
                        char tag[16];
                        if (cp >= 33 && cp < 127)
                            std::snprintf(tag, sizeof tag, "'%c'", (char)cp);
                        else
                            std::snprintf(tag, sizeof tag, "U+%04X", cp);

                        if (ref_baked >= 0)
                            ImGui::Text("Selected: %s (cp=%d)  class=%s@%d%%"
                                        "  total=%d%%  advance: cand=%d ref=%d (%+d)"
                                        "  ink: cand=%d ref=%d  lsb: cand=%d ref=%d",
                                        tag, cp, cls, class_pct, total,
                                        can_baked, ref_baked, can_baked - ref_baked,
                                        can_ink, ref_ink, can_lsb, ref_lsb);
                        else
                            ImGui::Text("Selected: %s (cp=%d)  class=%s@%d%%"
                                        "  total=%d%%  advance=%d  ink=%d  lsb=%d",
                                        tag, cp, cls, class_pct, total,
                                        can_baked, can_ink, can_lsb);

                        /* Reset on its own line above the slider table
                         * so the table cells can give all six controls
                         * equal width. */
                        if (ImGui::Button("Reset selected")) {
                            e.params.compression_glyph_delta_pct[cp] = 0;
                            e.params.compression_glyph_advance_atlas[cp] = 0;
                            e.params.compression_glyph_lsb_atlas[cp] = -1;
                            e.params.compression_glyph_boldness_atlas[cp] = 0;
                            e.params.codepoint_remap[cp] = 0;
                            e.params.compression_glyph_y_offset_atlas[cp] = 0;
                        }

                        /* Six per-glyph controls in a 3 x 2 table.
                         *   col 1 (sizing):     delta, advance
                         *   col 2 (positioning): lsb, y-offset
                         *   col 3 (other):      boldness, TTF cp remap */
                        if (ImGui::BeginTable("glyph_sliders", 3,
                                              ImGuiTableFlags_SizingStretchSame |
                                              ImGuiTableFlags_NoSavedSettings)) {
                            /* Row 1 */
                            ImGui::TableNextRow();

                            ImGui::TableSetColumnIndex(0);
                            ImGui::SetNextItemWidth(-FLT_MIN);
                            ImGui::SliderInt("##glyph_delta",
                                &e.params.compression_glyph_delta_pct[cp],
                                -50, 50, "delta %+d pp");

                            ImGui::TableSetColumnIndex(1);
                            ImGui::SetNextItemWidth(-FLT_MIN);
                            const char *lsb_fmt =
                                (e.params.compression_glyph_lsb_atlas[cp] < 0)
                                ? "lsb natural"
                                : "lsb %d";
                            ImGui::SliderInt("##glyph_lsb",
                                &e.params.compression_glyph_lsb_atlas[cp],
                                -1, 60, lsb_fmt);

                            ImGui::TableSetColumnIndex(2);
                            ImGui::SetNextItemWidth(-FLT_MIN);
                            /* Boldness: tenths-of-iteration, ±5.0 in
                             * 0.1 steps; rasterizer clamps at ±10.0. */
                            float bf =
									e.params.compression_glyph_boldness_atlas[cp] / 10.0f;
                            if (ImGui::SliderFloat("##glyph_boldness", &bf,
                                                   -5.0f, 5.0f,
                                                   "boldness %+.1f")) {
                                int rounded = (int)std::lround(bf * 10.0f);
                                if (rounded < -100) rounded = -100;
                                if (rounded >  100) rounded =  100;
                                e.params.compression_glyph_boldness_atlas[cp] =
                                    rounded;
                            }

                            /* Row 2 */
                            ImGui::TableNextRow();

                            ImGui::TableSetColumnIndex(0);
                            ImGui::SetNextItemWidth(-FLT_MIN);
                            ImGui::SliderInt("##glyph_advance",
                                &e.params.compression_glyph_advance_atlas[cp],
                                0, 200, "advance %d (0=natural)");

                            ImGui::TableSetColumnIndex(1);
                            ImGui::SetNextItemWidth(-FLT_MIN);
                            /* Y-offset: positive = shift glyph up.
                             * Range covers a typical descender area
                             * generously; users rarely need more than
                             * ±20 atlas-px. */
                            ImGui::SliderInt("##glyph_y_offset",
                                &e.params.compression_glyph_y_offset_atlas[cp],
                                -30, 30, "y-offset %+d");

                            ImGui::TableSetColumnIndex(2);
                            ImGui::SetNextItemWidth(-FLT_MIN);
                            /* TTF codepoint remap: 0 = no remap; >0 = fetch
                             * that codepoint's glyph from the TTF instead.
                             * step_fast=16 so +/- buttons jump by hex digit. */
                            if (ImGui::InputInt("##cp_remap",
                                                &e.params.codepoint_remap[cp],
                                                1, 16)) {
                                int v = e.params.codepoint_remap[cp];
                                if (v < 0) v = 0;
                                if (v > 0x10FFFF) v = 0x10FFFF;
                                e.params.codepoint_remap[cp] = v;
                            }
                            int remap = e.params.codepoint_remap[cp];
                            if (remap == 0) {
                                ImGui::TextDisabled("TTF cp (0=natural)");
                            } else if (remap >= 33 && remap < 0x80) {
                                ImGui::Text("TTF cp = '%c' (U+%04X)",
                                            (char)remap, remap);
                            } else {
                                ImGui::Text("TTF cp = U+%04X", remap);
                            }

                            ImGui::EndTable();
                        }
                    } else {
                        ImGui::TextDisabled(
                            "(no glyph selected — click a cell above)");
                    }
                }
            } else {
                ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f),
                                   "BUILD FAILED: %s", e.err.c_str());
            }

            /* Save row: output basename + Save button + status. */
            char save_buf[512];
            std::snprintf(save_buf, sizeof save_buf, "%s",
                          e.save_basename.c_str());
            ImGui::SetNextItemWidth(400 * app.dpi_scale);
            if (ImGui::InputText("output basename", save_buf, sizeof save_buf))
                e.save_basename = save_buf;
            ImGui::SameLine();
            if (ImGui::Button("Save") && e.result_valid) {
                char err[512] = {0};
                if (font_atlas_write(&e.result, e.save_basename.c_str(),
                                     err, sizeof err)) {
                    e.save_msg = "saved " + e.save_basename + ".png + .fnt";
                    e.save_ok  = true;
                } else {
                    e.save_msg = std::string("save failed: ") + err;
                    e.save_ok  = false;
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Save tuning...")) {
                /* Suggest <basename>.tune.yaml in the dialog. SDL3
                 * uses the suggestion as the initial filename + dir. */
                std::string suggested = (!e.save_basename.empty()
												 ? e.save_basename
												 : std::string("font")) +
										".tune.yaml";
                SDL_DialogFileFilter filters[] = {
                    { "Tuning YAML", "yaml;yml" },
                    { "All files",   "*" },
                };
                auto *ctx = new TieFontTuneDialogContext{ &app, e.id };
                SDL_ShowSaveFileDialog(TieFontTune_SaveTuningCb, ctx, app.window,
                                       filters, 2, suggested.c_str());
            }
            ImGui::SameLine();
            if (ImGui::Button("Load tuning...")) {
                SDL_DialogFileFilter filters[] = {
                    { "Tuning YAML", "yaml;yml" },
                    { "All files",   "*" },
                };
                auto *ctx = new TieFontTuneDialogContext{ &app, e.id };
                SDL_ShowOpenFileDialog(TieFontTune_LoadTuningIntoExistingCb, ctx,
                                       app.window, filters, 2, nullptr,
                                       /*allow_many=*/false);
            }
            ImGui::SameLine();
            if (ImGui::Button("Remove")) delete_idx = (int)i;
            if (!e.save_msg.empty()) {
                ImGui::TextColored(e.save_ok
                    ? ImVec4(0.4f, 1.0f, 0.4f, 1.0f)
                    : ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
                    "%s", e.save_msg.c_str());
            }
            ImGui::Separator();
            ImGui::PopID();
        }
        if (delete_idx >= 0) {
            TieFontTuneTtfEntry &e = app.ttfs[delete_idx];
            if (e.sample_tex) SDL_ReleaseGPUTexture(app.device, e.sample_tex);
            if (e.atlas_tex)  SDL_ReleaseGPUTexture(app.device, e.atlas_tex);
            font_atlas_free(&e.result);
            app.ttfs.erase(app.ttfs.begin() + delete_idx);
        }

        if (ImGui::Button("Add TTF...")) {
            SDL_DialogFileFilter filters[] = {
                { "TrueType / OpenType", "ttf;otf" },
                { "All files", "*" },
            };
            SDL_ShowOpenFileDialog(TieFontTune_TtfDialogCb, &app, app.window,
                                   filters, 2, nullptr, /*allow_many=*/true);
        }
        ImGui::SameLine();
        if (ImGui::Button("Add tuning (YAML)...")) {
            /* Loads a previously saved tuning file. The YAML carries
             * the source TTF path, which we slurp + apply to a brand-
             * new TieFontTuneTtfEntry so the user can resume tuning where they
             * left off. */
            SDL_DialogFileFilter filters[] = {
                { "Tuning YAML", "yaml;yml" },
                { "All files",   "*" },
            };
            auto *ctx = new TieFontTuneDialogContext{ &app, /*entry_id=*/-1 };
            SDL_ShowOpenFileDialog(TieFontTune_LoadTuningAsNewCb, ctx, app.window,
                                   filters, 2, nullptr, /*allow_many=*/false);
        }

        ImGui::End();
        ImGui::Render();
        ImDrawData *draw_data = ImGui::GetDrawData();

        SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(app.device);
        if (!cmd) continue;
        SDL_GPUTexture *swap_tex = nullptr;
        Uint32 sw = 0, sh = 0;
        if (!SDL_WaitAndAcquireGPUSwapchainTexture(cmd, app.window,
												   &swap_tex, &sw, &sh) ||
			!swap_tex) {
            SDL_SubmitGPUCommandBuffer(cmd);
            continue;
        }
        ImGui_ImplSDLGPU3_PrepareDrawData(draw_data, cmd);
        SDL_GPUColorTargetInfo cti{};
        cti.texture     = swap_tex;
        cti.clear_color = SDL_FColor{ 0.10f, 0.10f, 0.12f, 1.0f };
        cti.load_op     = SDL_GPU_LOADOP_CLEAR;
        cti.store_op    = SDL_GPU_STOREOP_STORE;
        SDL_GPURenderPass *pass = SDL_BeginGPURenderPass(cmd, &cti, 1, nullptr);
        ImGui_ImplSDLGPU3_RenderDrawData(draw_data, cmd, pass);
        SDL_EndGPURenderPass(pass);
        SDL_SubmitGPUCommandBuffer(cmd);
    }

    /* Cleanup. SDL_GPU resources tear down before ImGui's GPU backend
     * because the backend uses the device for its own resources. */
    for (auto &e : app.ttfs) {
        if (e.sample_tex) SDL_ReleaseGPUTexture(app.device, e.sample_tex);
        if (e.atlas_tex)  SDL_ReleaseGPUTexture(app.device, e.atlas_tex);
        font_atlas_free(&e.result);
    }
    if (app.ref_sample_tex) SDL_ReleaseGPUTexture(app.device, app.ref_sample_tex);
    if (app.ref_loaded) font_atlas_free(&app.ref_atlas);

    ImGui_ImplSDLGPU3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();

    SDL_ReleaseWindowFromGPUDevice(app.device, app.window);
    SDL_DestroyGPUDevice(app.device);
    SDL_DestroyWindow(app.window);
    SDL_Quit();
    return 0;
}
