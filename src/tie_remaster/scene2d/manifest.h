#ifndef TIE_SCENE2D_MANIFEST_H
#define TIE_SCENE2D_MANIFEST_H

/* Reads `<remaster_dir>/<LFD>/<film>/manifest.yaml` asset bundles. */

#include "aeron/vfs.h"
#include "tie_remaster/scene2d/types.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct TieScene2dManifest TieScene2dManifest;

enum {
	ACTOR2D_VISIBLE = 0x0001,
	ACTOR2D_HFLIP = 0x0100,
	ACTOR2D_VFLIP = 0x0200,
	/* Mirrors AF_REMAP_COLOR. Compose layer renders the cel as a flat
	 * silhouette using `remap_rgba` as the fill color (alpha-weighted
	 * by the cel's sample.a, AF_REMAP_COLOR equivalent). The application
	 * pre-resolves palette[fore_color] → RGB at build_views time so
	 * the compose layer stays palette-independent. */
	ACTOR2D_REMAP_COLOR = 0x0400,
};
typedef struct TieScene2dActorView {
	uint32_t res_type; /* FOURCC (DELT/ANIM/RAW/CUST) */
	char res_name[8];
	int16_t film_entry_index;
	/* Pre-scale, pre-recentering position and dimensions in classic
	 * coords — see TieActor2DState. The compositor applies xscale/
	 * yscale on demand (in float for sub-pixel-precise dst rects). */
	int16_t x, y, w, h;
	int16_t xscale, yscale; /* Watcom Q8 (256 = identity). */
	int16_t zplane;
	int16_t state;
	uint16_t flags;
	int16_t clip_left, clip_top, clip_right, clip_bottom;
	/* Cross-channel emit z-order (shared with TieUIText / TiePaintCmd
	 * via the snapshot's monotonic counter). The application's RT pass
	 * merge-dispatches the three channels by ascending z_order so
	 * paint primitives, sprites, and text layer in true engine
	 * emit order. -1 when the view didn't come from the snapshot
	 * (filmview's preview adapter). */
	int16_t z_order;

	/* Sub-cel smoothing inputs (mirror TieActor2DState.prev_x/y +
	 * prev_xv/yv + xv/yv/xvf/yvf). The compositor lerps
	 * (prev_x, prev_y) → (x, y) by `frame_progress` when the
	 * manifest's interpolate flag is set AND the cel-to-cel delta
	 * matches the velocity that produced it (prev_xv/yv — captured
	 * at the start of Move_Actor, before the FILM script potentially
	 * rewrites xv for the next cel). Default-initialised to (x, y)
	 * by the filmview preview adapter so its no-frame-progress path
	 * renders exactly the current cel commit with no slide. */
	int16_t prev_x, prev_y;
	int16_t prev_xv, prev_yv;
	int16_t xv, yv;
	int16_t xvf, yvf;
	/* Cel-start scale (Q8, 256 = identity) — lerp target alongside
	 * prev_x/y when interpolation is on. Pin to current xscale/yscale
	 * for adapters that don't animate scale (filmview preview, INCREMENTAL
	 * UI draws_2d) so the lerp degenerates to a snap. */
	int16_t prev_xscale, prev_yscale;
	/* 0..1 fractional progress within the current cel budget at
	 * snapshot emit time. Set by the application from
	 * snap->scene_clock.frame_progress; 0.0 disables smoothing. */
	float frame_progress;
	/* Cel period in microseconds — needed by the compositor's
	 * frame_progress quantizer when the manifest's interp_rate_hz
	 * is set. Set by the application from snap->scene_clock.cel_period_us;
	 * 0 leaves the lerp at host frame rate (no quantization). */
	uint32_t cel_period_us;

	/* Pre-resolved silhouette color for ACTOR2D_REMAP_COLOR (mirrors
	 * the engine's AF_REMAP_COLOR + foreColor pair). Filled by the
	 * application when the flag is set; ignored otherwise. */
	TieScene2dRgba remap_rgba;

	/* Routing tag mirroring TieDraw2D::target (TieEmitTarget enum:
	 * 0 = cutscene RT, 1 = brief-map source RT). The application uses this
	 * to direct the brief-widget poly-path emits onto the source RT
	 * pass while the standard emits flow through the cutscene RT pass.
	 * 0 for views that didn't come from a snapshot (filmview). */
	uint8_t target;
} TieScene2dActorView;

/* Open a cutscene compositor rooted at the host directory `remaster_dir`.
 * Manifest documents are read from the borrowed VFS at
 * `<vfs_prefix>/<LFD>/films/<film>/manifest.yaml`; an empty prefix means the
 * selected VFS root is the remaster directory itself. The host directory is
 * used for discovery and asset paths. Returns NULL when the inputs are
 * invalid, discovery fails, or no manifests exist. The VFS remains caller-
 * owned and must outlive the manifest. Caller frees via
 * TieScene2dManifest_Close.
 *
 * The compositor itself doesn't talk to a GPU — `TieScene2dManifest` is
 * pure manifest data. Pair with TieScene2dCutscene_Init to attach GPU-side
 * resources (texture cache + draw entry points). */
TieScene2dManifest* TieScene2dManifest_Open(AeronVfs* vfs, AeronVfsRoot root, const char* vfs_prefix,
											const char* remaster_dir, const char* frontend_profile_id);

/* Free all manifest state. Safe to call with NULL. */
void TieScene2dManifest_Close(TieScene2dManifest* cs);

/* True when a complete bundle exists for an explicit (lfd, film) tuple. */
bool TieScene2dManifest_HasCompleteBundle(const TieScene2dManifest* cs, const char* lfd_basename,
										  const char* film_name);

/* True when a manifest bundle for (lfd, film) is loaded AND the
 * given (res_name, entry_index) resolves to a manifest entry —
 * either a `<res_name>#<N>` instance-specific entry or the bare
 * `<res_name>` default. False when no manifest will match the
 * actor at draw time. Useful for asset-team tooling that wants to
 * surface "manifest hit" status per actor row. */
bool TieScene2dManifest_ActorMatches(const TieScene2dManifest* cs, const char* lfd_basename,
									 const char* film_name, const char* res_name, int16_t entry_index);

/* ------------------- atlas-rect editing API -------------------
 *
 * filmview's atlas-layout editor mutates the in-memory AeronSpriteAtlas of
 * the TieScene2dAssetVariant resolved at `cur_cel`, and writes the variant's
 * YAML back on demand. The compose path reads `av->atlas.frames[state]`
 * each draw, so a `_set` call is visible on the next frame without any
 * GPU cache invalidation.
 *
 * All three accessors require the (lfd, film, res_name, entry_index,
 * cur_cel) tuple to resolve a manifest entry of kind ATLAS with a
 * loaded layout. Otherwise they return false (and `_get` leaves out
 * params untouched). entry_index < 0 falls back to the bare-name entry
 * exactly like the compose path's resolver.
 */

/* Read frame[frame_idx] of the atlas resolved for (lfd, film, res_name,
 * entry_index) at cur_cel. All out pointers are optional (may be
 * NULL). Pointers returned via `out_yaml_path` and `out_asset_path`
 * are borrowed for the manifest's lifetime; `out_asset_path` is the
 * resolved on-disk KTX2 path the compositor samples (filmview's
 * atlas-bake mode keys its texture overrides off this exact string).
 * Returns false when no atlas resolves or `frame_idx` is out of range. */
bool TieScene2dManifest_AtlasGet(const TieScene2dManifest* cs, const char* lfd_basename,
								 const char* film_name, const char* res_name, int16_t entry_index,
								 int cur_cel, int frame_idx, TieScene2dRect* out_rect, int* out_atlas_w,
								 int* out_atlas_h, int* out_frame_count, const char** out_yaml_path,
								 const char** out_asset_path);

/* Mutate frame[frame_idx] of the same atlas. The compose path picks it
 * up on the next draw. Returns false when no atlas resolves or
 * `frame_idx` is out of range. */
bool TieScene2dManifest_AtlasSet(TieScene2dManifest* cs, const char* lfd_basename, const char* film_name,
								 const char* res_name, int16_t entry_index, int cur_cel, int frame_idx,
								 TieScene2dRect new_rect);

/* Persist the atlas's current in-memory layout to its YAML path via
 * Aeron_SpriteAtlasSave. On failure writes a NUL-terminated reason into
 * `err` (NULL/0 disables). Returns false when no atlas resolves. */
bool TieScene2dManifest_AtlasSave(TieScene2dManifest* cs, const char* lfd_basename, const char* film_name,
								  const char* res_name, int16_t entry_index, int cur_cel, char* err,
								  size_t errsz);

/* Re-parse the on-disk YAML for the resolved atlas variant, replacing
 * the in-memory AeronSpriteAtlas with what's now on disk. Used after
 * tooling has rewritten the YAML out-of-band (e.g. filmview's
 * regenerate-from-source-ANIM button). Returns false when the atlas
 * doesn't resolve or Aeron_SpriteAtlasLoad fails. */
bool TieScene2dManifest_AtlasReload(TieScene2dManifest* cs, const char* lfd_basename, const char* film_name,
									const char* res_name, int16_t entry_index, int cur_cel);

/* Resolve the SPRITE-kind variant for (lfd, film, res_name, entry_index)
 * at `cur_cel`. On success, *out_asset_path is set to the on-disk KTX2
 * path the compositor samples (same string the texture-override layer
 * keys on); the pointer is borrowed for the manifest's lifetime. Used
 * by filmview's bake-from-reference path to target DELT/RAW sprites
 * with the same override + sibling-PNG pipeline the atlas bake uses.
 * Returns false when no SPRITE variant resolves at the given cel. */
bool TieScene2dManifest_SpriteGet(const TieScene2dManifest* cs, const char* lfd_basename,
								  const char* film_name, const char* res_name, int16_t entry_index,
								  int cur_cel, const char** out_asset_path);

#endif
