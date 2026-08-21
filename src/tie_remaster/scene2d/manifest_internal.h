#ifndef TIE_SCENE2D_MANIFEST_INTERNAL_H
#define TIE_SCENE2D_MANIFEST_INTERNAL_H

/* Private manifest and scene compositor types. */

#include "aeron/scene/sprite_atlas.h"
#include "tie_remaster/scene2d/manifest.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
	ASSET_KIND_NONE = 0,
	ASSET_KIND_SPRITE,
	ASSET_KIND_FRAMES,
	ASSET_KIND_ATLAS,
} TieScene2dAssetKind;

/* FIT_DEFAULT uses the scaled classic bbox. FIT_EXTEND uses the texture's
 * canonical-4K size and anchors it to that bbox. */
typedef enum {
	FIT_DEFAULT = 0,
	FIT_EXTEND,
} TieScene2dFitMode;

/* 9-point anchor grid. Names are <vertical>_<horizontal>; the central
 * value is `ANCHOR_CENTER`. Used by `fit: extend` to position the
 * texture-driven dst rect relative to the engine's classic bbox. */
typedef enum {
	ANCHOR_CENTER = 0,
	ANCHOR_TOP_LEFT,
	ANCHOR_TOP_CENTER,
	ANCHOR_TOP_RIGHT,
	ANCHOR_CENTER_LEFT,
	ANCHOR_CENTER_RIGHT,
	ANCHOR_BOTTOM_LEFT,
	ANCHOR_BOTTOM_CENTER,
	ANCHOR_BOTTOM_RIGHT,
} TieScene2dAnchor;

typedef struct TieScene2dAssetVariant {
	int from_cel;
	char name[64];
	char layout_name[64];
	/* Resolved on-disk path. Either a `.ktx2` file (for SPRITE and
	 * ATLAS kinds) or a directory containing `frame_NN.ktx2` files
	 * (FRAMES kind). Empty when the variant didn't resolve in the
	 * LFD chain. */
	char asset_path[1024];
	char yaml_path[1024]; /* atlas only */
	AeronSpriteAtlas atlas;
	bool atlas_loaded;
} TieScene2dAssetVariant;

typedef struct TieScene2dActorEntry {
	char res_name[16];
	int16_t entry_index;

	TieScene2dAssetKind kind;
	TieScene2dAssetVariant* variants;
	int variant_count;
	int variant_cap;

	/* dst rect override expressions — each component independent.
	 * Empty string = component not overridden (use the classic
	 * 4:3 anchor / fit:extend default for that component). */
	char dst_x_expr[64];
	char dst_y_expr[64];
	char dst_w_expr[64];
	char dst_h_expr[64];
	char tile_x_expr[64];
	char tile_y_expr[64];
	char tile_w_expr[64];
	char tile_h_expr[64];
	/* Parsed clip-rect expressions, currently unused by composition. */
	char clip_x_expr[64];
	char clip_y_expr[64];
	char clip_w_expr[64];
	char clip_h_expr[64];
	char replace_with[16];
	bool hide;
	bool filter_linear;
	TieScene2dFitMode fit;
	TieScene2dAnchor anchor;

	/* Sub-cel position smoothing — resolved after manifest load.
	 * In the parser, -1 means "not explicitly set" (inherit from
	 * the bundle, then auto-enable if interp_rate_hz is set);
	 * after manifest_resolve_inheritance() runs every entry holds
	 * 0 (off) or 1 (on). Compose reads this directly.
	 *
	 * Opt in for velocity-driven movers (sliding sprites,
	 * scrolling backdrops, weaving fighters). Leave off for
	 * actors with intentionally choppy or keyframed motion
	 * (e.g. CITY's `tie07` cels 14→15) or whose position is
	 * driven by an actor->user callback that doesn't go through
	 * Move_Actor. The compositor's per-cel teleport detection
	 * (|dx| > |prev_xv| + 1) catches in-flight ACTOR_POS snaps
	 * within an otherwise-smooth animation. */
	int8_t interpolate;

	/* Optional target rate (Hz) for the rendered position.
	 * Parser sentinel: -1 = "not explicitly set" (inherit from
	 * the bundle); after resolution, 0 = host frame rate, > 0 =
	 * quantize frame_progress to ceil(cel_period × rate / 1e6)
	 * buckets per cel so position updates roll at `rate` Hz
	 * regardless of host refresh.
	 *
	 * Setting interp_rate at any level (actor or bundle) auto-
	 * enables interpolate when the latter wasn't explicitly set
	 * — saves authoring `interpolate: true` alongside every
	 * `interp_rate:`. Ignored when `interpolate` resolves to 0. */
	int32_t interp_rate_hz;

	/* Linear color fade between two cel values. Inactive when from == to.
	 * Outside [from, to] the actor draws at its full sampled color (cel
	 * < from) or fully tinted toward to_color (cel >= to). The classic
	 * partial palette fade (e.g. SCENE_TITLE's PAL_TO_PAL on indices
	 * 81-96 only) doesn't propagate to the HD layer, so manifest-driven
	 * fades are how the asset team replays those effects on remastered
	 * textures. */
	bool fade_active;
	int32_t fade_from_cel;
	int32_t fade_to_cel;
	uint8_t fade_to_r, fade_to_g, fade_to_b; /* default 0,0,0 = black */
} TieScene2dActorEntry;

#define CUTSCENE_MAX_EXTRAS 8

typedef struct TieScene2dFilmBundle {
	char lfd_basename[16];
	char film_name[12];
	bool complete;
	uint16_t source_width;
	uint16_t source_height;
	uint8_t source_pixel_aspect;
	bool source_space_explicit;
	bool source_mismatch_warned;

	/* When true (the default — see TieScene2dManifest_AllocBundle), the cutscene RT
	 * clears opaque (alpha=1) so HD-uncovered regions show black
	 * rather than the classic FB. Set false to opt into HD-overlay-
	 * on-classic semantics (FMV-stream cutscenes, scenes where the
	 * classic FB is meant to show through partial HD coverage).
	 *
	 * The default is true because every authored UI scene wants HD
	 * to fully replace classic — HD-uncovered strips otherwise show
	 * un-remastered classic art (and during scene-transition
	 * palette fades, that art briefly shows in the wrong palette,
	 * reading as a flash). Cutscenes typically have full HD
	 * coverage so the default is also correct for them; the FMV-
	 * defer / partial-bundle cases need to opt out explicitly. */
	bool opaque_background;

	/* Film-level defaults for sub-cel smoothing. Authored at the
	 * top of the manifest with `interpolate:` and `interp_rate:`;
	 * each actor inherits unless it sets the same key. Parser
	 * sentinel -1 = "not specified at the bundle level". After
	 * manifest_resolve_inheritance(), interpolate ∈ {0, 1} and
	 * interp_rate_hz ≥ 0. */
	int8_t interpolate;
	int32_t interp_rate_hz;
	/* Derived after inheritance resolution for the presentation scheduler. */
	bool has_interpolation;

	char extras[CUTSCENE_MAX_EXTRAS][16];
	int extras_count;

	char classic_region[64];

	TieScene2dActorEntry* actors;
	int actor_count;
	int actor_cap;

	struct TieScene2dFilmBundle* next;
} TieScene2dFilmBundle;

struct TieScene2dManifest {
	AeronVfs* vfs;
	AeronVfsRoot vfs_root;
	char vfs_prefix[512];
	char root[512];
	char profile_id[16];
	TieScene2dFilmBundle* bundles;
	int bundle_count;
};

/* TieScene2dViewportTransform + TieScene2dViewport_ComputeXform live in viewport.{c,h} — a
 * separate file so layout modules can include just that without
 * pulling the manifest data types. Re-exported here for the SDL
 * backend's resolve_actor / draw_actors path which also needs the
 * manifest types in the same translation unit. */
#include "tie_remaster/scene2d/viewport.h"

/* Manifest accessors — implemented in manifest.c. */
const TieScene2dFilmBundle* TieScene2dManifest_FindBundle(const TieScene2dManifest* cs, const char* lfd,
														  const char* film);
const TieScene2dActorEntry* TieScene2dManifest_FindActor(const TieScene2dFilmBundle* b, const char* res_name,
														 int16_t entry_index);
int TieScene2dManifest_VariantAtCel(const TieScene2dAssetVariant* vs, int n, int cel);

/* Combined entry+variant lookup — finds the actor entry by (res_name,
 * entry_index) and selects its variant for cur_cel. Returns false when
 * the entry is missing, hidden, or has no variants. The two output
 * pointers are borrowed for as long as the bundle stays loaded. Used
 * by cutscene actor and map backdrop resolution to share the manifest walk. */
bool TieScene2dManifest_ResolveVariant(const TieScene2dFilmBundle* b, const char* res_name,
									   int16_t entry_index, int cur_cel,
									   const TieScene2dActorEntry** out_entry,
									   const TieScene2dAssetVariant** out_variant);

bool TieScene2dManifest_BundleMatchesSource(const TieScene2dFilmBundle* b, int source_w, int source_h,
											uint8_t source_pixel_aspect);

#ifdef __cplusplus
}
#endif

#endif
