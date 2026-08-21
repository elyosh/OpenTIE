#ifndef TIE_REMASTER_SCENE2D_CUTSCENE_H
#define TIE_REMASTER_SCENE2D_CUTSCENE_H

/*
 * Cutscene compositor (Aeron port).
 *
 * Wraps the manifest-discovery state from cutscene.c (TieScene2dManifest)
 * plus a GPU texture cache and provides the per-frame
 * draw entry points consumed by both the game application (`tie`) and
 * filmview's preview path.
 */

#include "tie_remaster/scene2d/manifest.h"

#include "aeron/render.h"
#include "aeron/scene/draw_list2d.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct TieScene2dCutscene TieScene2dCutscene;

/* Open the manifests under remaster_dir and create a GPU-side asset
 * cache. Returns NULL on init failure (no manifests, directory does not
 * exist, etc.); the caller proceeds without cutscene compositing. */
TieScene2dCutscene* TieScene2dCutscene_Init(const char* remaster_dir, const char* frontend_profile_id);

void TieScene2dCutscene_Shutdown(TieScene2dCutscene* g);

/* Borrowed accessors for sibling modules that need to share the
 * texture cache, asset root, and manifest data (e.g. map walks
 * the active bundle for the brief-map's manifest-driven asset
 * lookups). Pointers are owned by the TieScene2dCutscene and valid for its
 * lifetime. find_bundle returns the `(lfd, film)` bundle entry from
 * the manifest, or NULL when no such bundle exists; sibling modules
 * use the result with TieScene2dManifest_FindActor() etc. */
struct AeronImageCache* TieScene2dCutscene_Assets(TieScene2dCutscene* g);
const char* TieScene2dCutscene_RemasterDir(const TieScene2dCutscene* g);
const struct TieScene2dFilmBundle*
TieScene2dCutscene_FindBundle(const TieScene2dCutscene* g, const char* lfd_basename, const char* film_name);

bool TieScene2dCutscene_HasCompleteBundle(const TieScene2dCutscene* g, const char* lfd_basename,
										  const char* film_name);

bool TieScene2dCutscene_BundleMatchesSource(const TieScene2dCutscene* g, const char* lfd_basename,
											const char* film_name, int source_w, int source_h,
											uint8_t source_pixel_aspect);

/* True when the bundle for (lfd, film) has `opaque_background: true`
 * in its manifest — the application uses this to switch the cutscene RT
 * clear color from transparent to opaque black, suppressing classic
 * FB bleed-through where HD glyphs don't pixel-match the procedural
 * classic render (SCENE_TITLE perspective crawl). */
bool TieScene2dCutscene_BundleIsOpaque(const TieScene2dCutscene* g, const char* lfd_basename,
									   const char* film_name);

/* True when the complete bundle contains at least one visible actor whose
 * resolved manifest policy enables sub-cel interpolation. The application
 * uses this to request intermediate presentation frames only when they can
 * change the rendered result. */
bool TieScene2dCutscene_BundleHasInterpolation(const TieScene2dCutscene* g, const char* lfd_basename,
											   const char* film_name);

/* Borrowed mutable handle to the underlying TieScene2dManifest. Lets
 * tooling (filmview's atlas-rect editor) call the manifest-side
 * editing accessors (TieScene2dManifest_AtlasGet/_set/_save) without
 * pulling manifest_internal.h. Pointer is owned by the TieScene2dCutscene and
 * valid for its lifetime. */
struct TieScene2dManifest* TieScene2dCutscene_ManifestMut(TieScene2dCutscene* g);

/* Drop the cached KTX2 entry for `full_path`, forcing the next compose
 * pass to load fresh bytes from disk. */
void TieScene2dCutscene_TextureInvalidate(TieScene2dCutscene* g, const char* full_path);

/* Install or remove a borrowed in-memory texture for an asset path. Tooling
 * uses this to preview unsaved atlas edits through the production renderer. */
void TieScene2dCutscene_TextureOverride(TieScene2dCutscene* g, const char* full_path, AeronTexture* texture,
										int width, int height);
void TieScene2dCutscene_TextureUnoverride(TieScene2dCutscene* g, const char* full_path);
void TieScene2dCutscene_TextureUnoverrideAll(TieScene2dCutscene* g);

/* Manifest-presence query: true when a complete bundle is loaded for
 * (lfd, film) AND a manifest entry exists for (res_name,
 * entry_index). Filmview uses this to flag actor rows in its
 * inspector that have no manifest mapping. */
bool TieScene2dCutscene_ActorHasManifest(const TieScene2dCutscene* g, const char* lfd_basename,
										 const char* film_name, const char* res_name, int16_t entry_index);

/* Pre-render-pass: walks the actor list and ensures every needed
 * texture is loaded + uploaded. Issues copy passes on `cmd`. The
 * caller MUST NOT have an active render or copy pass at call time.
 * No-op when the (lfd, film) tuple has no complete bundle. */
void TieScene2dCutscene_PrepUploads(TieScene2dCutscene* g, AeronCommandBuffer* cmd, const char* lfd_basename,
									const char* film_name, int cur_cel, const TieScene2dActorView* actors,
									int actor_count);

/* In-render-pass: emits the per-actor blits. `viewport_w/h` are the
 * pixel dim of the bound color target's viewport (typically the 4K
 * cutscene RT). Caller is responsible for clearing the RT in the
 * render pass's load_op. */
void TieScene2dCutscene_RecordActors(TieScene2dCutscene* g, AeronDrawList2D* list, int viewport_w,
									 int viewport_h, const char* lfd_basename, const char* film_name,
									 int cur_cel, const TieScene2dActorView* actors, int actor_count);

void TieScene2dCutscene_RecordActorsInSource(TieScene2dCutscene* g, AeronDrawList2D* list, int viewport_w,
											 int viewport_h, int source_w, int source_h,
											 uint8_t source_pixel_aspect, const char* lfd_basename,
											 const char* film_name, int cur_cel,
											 const TieScene2dActorView* actors, int actor_count);

#ifdef __cplusplus
}
#endif

#endif
