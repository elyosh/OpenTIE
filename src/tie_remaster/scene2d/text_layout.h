#ifndef TIE_SCENE2D_TEXT_H
#define TIE_SCENE2D_TEXT_H

/* Bitmap-font layout for UI and subtitle draw lists. */

#include "tie_remaster/scene2d/canvas.h"
#include "tie_remaster/scene2d/types.h"
#include "tie_runtime/snapshot/snapshot_types.h" /* TieUIText */

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Per-glyph atlas metrics. Pixel coords inside the atlas. The
 * compose layer doesn't care how the atlas got built — it only
 * reads (atlas_x, atlas_y, atlas_w, atlas_h, advance) and writes
 * one quad per non-empty cell. */
typedef struct TieScene2dGlyphMetric {
	uint16_t atlas_x, atlas_y;
	uint16_t atlas_w, atlas_h;
	uint16_t advance;
} TieScene2dGlyphMetric;

/* `cell_h` is the uniform font-cell height used for background fills;
 * individual glyph rectangles may differ because of atlas scaling. */
typedef struct TieScene2dFontAtlas {
	AeronTexture* texture;
	int atlas_w, atlas_h;
	uint16_t cell_w, cell_h;
	uint16_t first_char;
	uint16_t num_chars;
	const TieScene2dGlyphMetric* glyphs;
	bool loaded;
} TieScene2dFontAtlas;

/* Selects whether the classic coordinate frame is letterboxed or fills the viewport. */
typedef enum {
	TIE_SCENE2D_TEXT_FIT_LETTERBOX_4_3 = 0,
	TIE_SCENE2D_TEXT_FIT_FILL = 1,
} TieScene2dTextFit;

/* Describes the source coordinate frame, atlas-to-source scale,
 * inter-glyph spacing, and viewport fit for a batch. */
typedef struct TieScene2dTextSpace {
	int classic_w, classic_h;
	float atlas_scale_x, atlas_scale_y;
	float space_between_classic;
	TieScene2dTextFit fit;
} TieScene2dTextSpace;

/* Canonical 320×200 VGA cutscene/UI coordinate space. */
extern const TieScene2dTextSpace TIE_SCENE2D_TEXT_SPACE_CLASSIC_VGA;

/* Derived transforms for atlas pixels, source coordinates, shadows,
 * and exact clip-edge mapping into one output frame. */
typedef struct TieScene2dTextLayout {
	float ratio_x, ratio_y;
	float space_between;
	float shadow_dx, shadow_dy;
	float region_x, region_y;
	float scale_x, scale_y;
	int region_ix, region_iy;
	int region_w, region_h;
	int classic_w, classic_h;
} TieScene2dTextLayout;

/* Initialise a layout for the given coord space inside the bound
 * viewport. */
void TieScene2dText_LayoutInit(TieScene2dTextLayout* out, int viewport_w, int viewport_h,
							   const TieScene2dTextSpace* space);

/* Emit one NUL-terminated text run starting at (pen_x, pen_y) in the
 * caller's coord system. Bytes 0x01 / 0x02 toggle between main_tint
 * (default / after 0x01) and bold_tint (after 0x02), matching
 * landru/font.c lfont_Draw_Font_Text. start_bold sets the initial
 * color (true == bold_tint). Pass bold_tint == main_tint to disable
 * toggle behaviour. with_shadow stamps shadow_tint at (shadow_dx, 0)
 * and (0, shadow_dy) — mirrors lfont_Draw_Font_Shadow_NN's right +
 * below neighbour writes. Sub-first_char bytes other than the toggle
 * codes are silently skipped without advancing the pen, matching
 * classic. Returns the final pen X. */
float TieScene2dText_RecordRun(TieScene2dCanvas* canvas, const TieScene2dFontAtlas* fa,
							   const TieScene2dTextLayout* layout, float pen_x, float pen_y,
							   TieScene2dRgba main_tint, TieScene2dRgba bold_tint, TieScene2dRgba shadow_tint,
							   bool start_bold, bool with_shadow, const char* text);

/* Record Landru text, including per-record clipping, colors, shadows, and
 * 0x01/0x02 foreground toggles. Missing font slots fall back to slot zero. */
void TieScene2dText_RecordUiText(TieScene2dCanvas* canvas, const TieScene2dFontAtlas* fonts, int font_count,
								 const TieScene2dTextSpace* space, const TieUIText* texts, int text_count,
								 const uint32_t* palette);

/* Record festring/rtsvga2 text. '[' and ']' adjust the remapped palette
 * index within 0xD3..0xD4; 0xFE consumes the next byte as a new index.
 * This path ignores Landru bold and shadow fields. */
void TieScene2dText_RecordFestringUiText(TieScene2dCanvas* canvas, const TieScene2dFontAtlas* fonts,
										 int font_count, const TieScene2dTextSpace* space,
										 const TieUIText* texts, int text_count, const uint32_t* palette);

/* Measure a NUL-terminated festring text run, returning width in
 * classic-coord pixels. Uses the same per-glyph advance and `[` / `]`
 * / 0xFE skip rules as TieScene2dText_RecordFestringRun, so the result
 * matches what TieScene2dText_RecordFestringUiText will draw. Returns 0
 * on any invalid argument or empty text. Lets cockpit/HUD builders
 * compute right-alignment + centering against the engine's
 * sys2_calclength semantics. */
float TieScene2dText_MeasureFestringClassic(const TieScene2dFontAtlas* fa, const TieScene2dTextSpace* space,
											const char* text);

#ifdef __cplusplus
}
#endif

#endif
