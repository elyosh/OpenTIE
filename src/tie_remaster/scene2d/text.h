/* Records snapshot text and the title crawl into Aeron draw lists. */
#ifndef TIE_REMASTER_SCENE2D_TEXT_H
#define TIE_REMASTER_SCENE2D_TEXT_H

#include "tie_remaster/scene2d/text_layout.h"    /* TieScene2dFontAtlas */
#include "tie_runtime/snapshot/snapshot_types.h" /* TieUIText */

#include "aeron/render.h"
#include "aeron/scene/draw_list2d.h"
#include "aeron/scene/font_atlas.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct TieScene2dTextRenderer TieScene2dTextRenderer;

/* Physical slots reserved for the two flight-simulator fonts. Logical
 * festring font IDs are mapped to these slots by the cockpit domain. */
#define TIE_SCENE2D_TEXT_RENDERER_FONT_SLOT_COCKPIT_MICRO 5
#define TIE_SCENE2D_TEXT_RENDERER_FONT_SLOT_COCKPIT_TINY 6

/* Loads `<atlas_basename>.png` and `.fnt` and uploads the texture. */
TieScene2dTextRenderer* TieScene2dTextRenderer_Init(AeronCommandBuffer* cmd, AeronTextureFormat target_format,
													const char* atlas_basename);

void TieScene2dTextRenderer_Shutdown(TieScene2dTextRenderer* g);

/* Load an additional font atlas into an internal domain-qualified slot.
 * Slots 0..3 are recovered Landru IDs, 4 is title, and 5..6 are cockpit.
 * Returns false on file/upload failure — the slot stays empty
 * and rendering for that font_id falls back to font 0. */
bool TieScene2dTextRenderer_LoadFont(TieScene2dTextRenderer* g, AeronCommandBuffer* cmd, uint8_t font_id,
									 const char* atlas_basename);
bool TieScene2dTextRenderer_LoadFontRgba8(TieScene2dTextRenderer* g, AeronCommandBuffer* cmd, uint8_t font_id,
										  const AeronFontAtlasRgba8Desc* desc);
bool TieScene2dTextRenderer_LoadFontVfs(TieScene2dTextRenderer* g, AeronCommandBuffer* cmd, uint8_t font_id,
										AeronVfs* vfs, AeronVfsRoot root, const char* atlas_basename,
										size_t maximum_file_size);

/* Records Landru text in the canonical VGA coordinate space. */
void TieScene2dTextRenderer_Record(TieScene2dTextRenderer* g, AeronDrawList2D* list, int viewport_w,
								   int viewport_h, const TieUIText* texts, int text_count,
								   const uint32_t* palette);

void TieScene2dTextRenderer_RecordInSpace(TieScene2dTextRenderer* g, AeronDrawList2D* list, int viewport_w,
										  int viewport_h, const TieScene2dTextSpace* space,
										  const TieUIText* texts, int text_count, const uint32_t* palette);

/* Records cockpit/HUD festring text using `[`, `]`, and `0xFE` color opcodes. */
void TieScene2dTextRenderer_RecordFestringInSpace(TieScene2dTextRenderer* g, AeronDrawList2D* list,
												  int viewport_w, int viewport_h,
												  const TieScene2dTextSpace* space, const TieUIText* texts,
												  int text_count, const uint32_t* palette);

void TieScene2dTextRenderer_RecordFestringScaled(TieScene2dTextRenderer* g, AeronDrawList2D* list,
												 int viewport_w, int viewport_h,
												 const TieScene2dTextSpace* space, const TieUIText* texts,
												 int text_count, const uint32_t* palette, float offset_x,
												 float offset_y, float scale_x, float scale_y, int target_w,
												 int target_h);

/* Prep caches title-crawl text outside a render pass. Record projects
 * and scrolls that cache using the snapshot scene tag and simulation time. */
bool TieScene2dTextRenderer_PrepTitleCrawl(TieScene2dTextRenderer* g, AeronCommandBuffer* cmd,
										   const TieSnapshot* snap);

void TieScene2dTextRenderer_RecordTitleCrawl(TieScene2dTextRenderer* g, AeronDrawList2D* list, int viewport_w,
											 int viewport_h, const TieSnapshot* snap,
											 const uint32_t* palette);

/* Override the title-crawl tint. When `active` is true the binary's
 * per-line palette ramp is bypassed and each line is tinted with
 * (r, g, b), scaled by a depth-based brightness factor that keeps the
 * binary's near-bright / far-dim perspective cue. Channel values are
 * linear-RGB in [0, 1]. Pass `active=false` to fall back to the
 * palette ramp; the (r, g, b) values are ignored in that case. */
void TieScene2dTextRenderer_SetCrawlTint(TieScene2dTextRenderer* g, bool active, float r, float green,
										 float b);

/* Borrowed font array, stable for the renderer's lifetime. */
const TieScene2dFontAtlas* TieScene2dTextRenderer_ComposeFonts(const TieScene2dTextRenderer* g,
															   int* out_count);

/* Measure a festring text run (with `[`/`]`/0xFE color-escape skip
 * semantics) and return its width in classic-coord pixels for the
 * given domain-qualified font ID and space descriptor. Used by cockpit/HUD builders
 * to right-align or center text without hand-rolling sys2_calclength.
 * Returns 0 on any invalid argument, an unloaded slot, or empty text. */
float TieScene2dTextRenderer_MeasureFestringClassic(TieScene2dTextRenderer* g, uint8_t font_domain,
													uint8_t font_id, const TieScene2dTextSpace* space,
													const char* text);

#ifdef __cplusplus
}
#endif

#endif
