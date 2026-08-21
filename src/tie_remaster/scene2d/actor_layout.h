#ifndef TIE_SCENE2D_ACTORS_H
#define TIE_SCENE2D_ACTORS_H

/* Resolve snapshot actors and manifest overrides into backend-independent
 * draw descriptors, or record their flat-quad representation directly. */

#include "tie_remaster/scene2d/canvas.h"
#include "tie_remaster/scene2d/manifest.h"          /* TieScene2dActorView */
#include "tie_remaster/scene2d/manifest_internal.h" /* TieScene2dActorEntry */
#include "tie_remaster/scene2d/types.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Caller fills this from the texture cache hit. `atlas_*_authored`
 * is the atlas's pixel dim BEFORE any LRU-cache downsample —
 * `atlas_frame_*` is in those authored coords. The compose layer
 * rescales the frame rect to the loaded texture's actual dims when
 * the texture was downsampled. Set use_atlas=false (and ignore the
 * authored dims / frame fields) for sprite / frames-dir cases. */
typedef struct TieScene2dActorTexture {
	AeronTexture* texture;
	int tex_w, tex_h;
	bool use_atlas;
	int atlas_w_authored;
	int atlas_h_authored;
	float atlas_frame_x, atlas_frame_y;
	float atlas_frame_w, atlas_frame_h;
} TieScene2dActorTexture;

/* Flat-quad consumers expand tiling when recording. Retained consumers use
 * the actor identity and may implement tiling in their material. */
typedef struct TieScene2dResolvedActorDraw {
	/* Identity. Ignored by flat-quad backends. */
	char res_name[8];
	int16_t film_entry_index;
	int16_t _pad_id;

	/* Texture binding. The opaque handle the caller put in
	 * TieScene2dActorTexture.texture is passed through unchanged. */
	AeronTexture* texture;
	int tex_w, tex_h; /* loaded (post-downsample) dims */

	/* Destination rect in viewport pixels, ALL overrides applied
	 * (default, fit:extend anchor, dst:{} expression, classic-clip
	 * narrowing). For tile mode this is the outer clip area. */
	TieScene2dRect dst;

	/* Source rect, normalised UV. Single-quad case: the exact slice
	 * to sample. Tile-mode case: the base rect to repeat (atlas
	 * sub-rect for atlas, full texture (0..1) otherwise). */
	TieScene2dUvRect src;

	/* Per-actor color modulation. tint multiplies sampled texel,
	 * bias adds (alpha-weighted) for fade-to-target effects. */
	TieScene2dRgba tint;
	TieScene2dRgba bias;

	uint8_t flip_flags; /* TieScene2dFlip bitmask */
	AeronBlit2DFilter filter;
	AeronBlit2DBlend blend;
	uint8_t _pad_flags;

	/* Tile mode. Both flags zero = single-quad descriptor. When set,
	 * the renderer should repeat the texture across `dst` at intervals
	 * of (tile_w, tile_h) viewport pixels, shifted by
	 * (tile_offset_x, tile_offset_y). TieScene2dActors_Emit's flat path
	 * expands this into a scissor-clipped grid; retained-mode
	 * backends can lower it to a tiling material instead. */
	bool tile_x, tile_y;
	float tile_w, tile_h;
	float tile_offset_x, tile_offset_y;
} TieScene2dResolvedActorDraw;

/* Compute the per-actor draw descriptor. Returns true when `out` is
 * populated and the actor produces visible output; false for hidden
 * actors, missing/zero-size textures, or geometry collapsed by the
 * classic clip-rect intersection. Caller passes the live viewport
 * dims (the same the consuming backend renders into). */
bool TieScene2dActors_Resolve(const TieScene2dActorView* a, const TieScene2dActorEntry* manifest_entry,
							  const TieScene2dActorTexture* tex, int viewport_w, int viewport_h, int cur_cel,
							  TieScene2dResolvedActorDraw* out);

bool TieScene2dActors_ResolveInSource(const TieScene2dActorView* a,
									  const TieScene2dActorEntry* manifest_entry,
									  const TieScene2dActorTexture* tex, int viewport_w, int viewport_h,
									  int source_w, int source_h, int cur_cel,
									  TieScene2dResolvedActorDraw* out);

bool TieScene2dActors_ResolveInSourceAspect(const TieScene2dActorView* a,
											const TieScene2dActorEntry* manifest_entry,
											const TieScene2dActorTexture* tex, int viewport_w, int viewport_h,
											int source_w, int source_h, uint8_t source_pixel_aspect,
											int cur_cel, TieScene2dResolvedActorDraw* out);

/* Flat-quad path: resolve + push quads to `frame`. Each call appends
 * one batch (no-scissor for the simple case, dst-clipped for tile
 * mode). No-op when resolve returns false. */
void TieScene2dActors_Record(TieScene2dCanvas* canvas, const TieScene2dActorView* a,
							 const TieScene2dActorEntry* manifest_entry, const TieScene2dActorTexture* tex,
							 int cur_cel);

#ifdef __cplusplus
}
#endif

#endif
