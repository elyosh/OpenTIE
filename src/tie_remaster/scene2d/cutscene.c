/*
 * Cutscene compositor — implementation (Aeron port).
 *
 * The portable per-actor geometry (Q8 scale, classic clip, fit/anchor,
 * dst-expr override, atlas downsample-rescale, tile mode, fade tint)
 * lives in actor_layout.c. This file owns the GPU-side resources:
 *   - The TieScene2dManifest (manifest data) lifecycle.
 *   - The KTX2 texture cache lifecycle and
 *     per-frame upload prep.
 *   - Resolving each actor's manifest entry + variant + texture-cache
 *     hit into a TieScene2dActorTexture, then recording it into the
 *     caller's Aeron draw list.
 */

#include "tie_remaster/scene2d/cutscene.h"
#include "aeron/aeron.h"
#include "aeron/log.h"
#include "aeron/scene/image_cache.h"
#include "aeron/scene/sprite_atlas.h"
#include "tie_remaster/scene2d/actor_layout.h"
#include "tie_remaster/scene2d/manifest_internal.h"
#include "tie_remaster/scene2d/viewport.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct TieScene2dCutscene {
	TieScene2dManifest* cs;  /* manifest data — owned */
	AeronImageCache* assets; /* GPU-side texture cache */
	char remaster_dir[512];  /* asset-root copy for sibling modules */
};

TieScene2dCutscene* TieScene2dCutscene_Init(const char* remaster_dir, const char* frontend_profile_id) {
	TieScene2dManifest* cs = TieScene2dManifest_Open(Aeron_GetVfs(), AERON_VFS_ROOT_RESOURCE, "",
													 remaster_dir, frontend_profile_id);
	if (!cs)
		return NULL;

	TieScene2dCutscene* g = (TieScene2dCutscene*)calloc(1, sizeof *g);
	if (!g) {
		TieScene2dManifest_Close(cs);
		Aeron_RequestFatalRendererError("cutscene renderer allocation");
		return NULL;
	}
	g->cs = cs;
	g->assets = Aeron_ImageCacheCreate();
	if (!g->assets) {
		TieScene2dManifest_Close(cs);
		free(g);
		Aeron_RequestFatalRendererError("cutscene image-cache creation");
		return NULL;
	}
	/* Stash the asset-root path so sibling modules such as map
	 * can compose absolute KTX2 paths against it without re-walking
	 * the manifest. snprintf null-terminates and silently truncates on
	 * overrun — the caller's path is rarely > 256 chars. */
	if (remaster_dir) {
		snprintf(g->remaster_dir, sizeof g->remaster_dir, "%s", remaster_dir);
	}
	return g;
}

AeronImageCache* TieScene2dCutscene_Assets(TieScene2dCutscene* g) { return g ? g->assets : NULL; }

const char* TieScene2dCutscene_RemasterDir(const TieScene2dCutscene* g) { return g ? g->remaster_dir : NULL; }

const struct TieScene2dFilmBundle*
TieScene2dCutscene_FindBundle(const TieScene2dCutscene* g, const char* lfd_basename, const char* film_name) {
	if (!g)
		return NULL;
	return TieScene2dManifest_FindBundle(g->cs, lfd_basename, film_name);
}

struct TieScene2dManifest* TieScene2dCutscene_ManifestMut(TieScene2dCutscene* g) { return g ? g->cs : NULL; }

void TieScene2dCutscene_TextureInvalidate(TieScene2dCutscene* g, const char* full_path) {
	if (!g)
		return;
	Aeron_ImageCacheInvalidate(g->assets, full_path);
}

void TieScene2dCutscene_TextureOverride(TieScene2dCutscene* g, const char* full_path, AeronTexture* texture,
										int width, int height) {
	if (!g || !full_path || !texture)
		return;
	Aeron_ImageCacheSetOverride(g->assets, full_path, texture, width, height);
}

void TieScene2dCutscene_TextureUnoverride(TieScene2dCutscene* g, const char* full_path) {
	if (!g || !full_path)
		return;
	Aeron_ImageCacheClearOverride(g->assets, full_path);
}

void TieScene2dCutscene_TextureUnoverrideAll(TieScene2dCutscene* g) {
	if (!g)
		return;
	Aeron_ImageCacheClearOverrides(g->assets);
}

void TieScene2dCutscene_Shutdown(TieScene2dCutscene* g) {
	if (!g)
		return;
	if (g->assets)
		Aeron_ImageCacheDestroy(g->assets);
	if (g->cs)
		TieScene2dManifest_Close(g->cs);
	free(g);
}

bool TieScene2dCutscene_HasCompleteBundle(const TieScene2dCutscene* g, const char* lfd_basename,
										  const char* film_name) {
	return g && TieScene2dManifest_HasCompleteBundle(g->cs, lfd_basename, film_name);
}

bool TieScene2dCutscene_BundleMatchesSource(const TieScene2dCutscene* g, const char* lfd_basename,
											const char* film_name, int source_w, int source_h,
											uint8_t source_pixel_aspect) {
	if (!g)
		return false;
	const TieScene2dFilmBundle* bundle = TieScene2dManifest_FindBundle(g->cs, lfd_basename, film_name);
	return bundle && bundle->complete &&
		   TieScene2dManifest_BundleMatchesSource(bundle, source_w, source_h, source_pixel_aspect);
}

bool TieScene2dCutscene_BundleIsOpaque(const TieScene2dCutscene* g, const char* lfd_basename,
									   const char* film_name) {
	if (!g)
		return false;
	const TieScene2dFilmBundle* b = TieScene2dManifest_FindBundle(g->cs, lfd_basename, film_name);
	return b && b->complete && b->opaque_background;
}

bool TieScene2dCutscene_BundleHasInterpolation(const TieScene2dCutscene* g, const char* lfd_basename,
											   const char* film_name) {
	if (!g)
		return false;
	const TieScene2dFilmBundle* bundle = TieScene2dManifest_FindBundle(g->cs, lfd_basename, film_name);
	return bundle && bundle->complete && bundle->has_interpolation;
}

bool TieScene2dCutscene_ActorHasManifest(const TieScene2dCutscene* g, const char* lfd_basename,
										 const char* film_name, const char* res_name, int16_t entry_index) {
	return g && TieScene2dManifest_ActorMatches(g->cs, lfd_basename, film_name, res_name, entry_index);
}

/* ---------- per-actor manifest+variant resolution ----------
 *
 * Collapses the (manifest entry, variant timeline, asset kind) lookup
 * + the on-disk path computation into a single record. Used by both
 * the upload prep and the in-pass draw so we walk the manifest once
 * per actor per frame. */
typedef struct TieScene2dCutsceneResolvedActor {
	const TieScene2dActorEntry* entry;
	const TieScene2dAssetVariant* variant;
	char full_path[1024];
	bool use_atlas;
	TieScene2dRect atlas_frame; /* atlas only */
} TieScene2dCutsceneResolvedActor;

/* One-shot warn for actors_2d entries that don't resolve to a manifest
 * binding. Without this, a missing actors:` entry in a bundle's
 * manifest produces zero pixels and zero diagnostics — which is exactly
 * the failure mode that took several iterations to track down for
 * TIELOGO's fighter / fighter2. We keep a small dedup set keyed on
 * (bundle, res_name, entry_index) so we warn once per unique miss
 * rather than every frame, every actor. The cap is generous: a
 * cumulative ~64 unique misses across a session is far more than any
 * real bundle would legitimately produce. */
typedef struct TieScene2dCutsceneUnresolvedKey {
	const TieScene2dFilmBundle* bundle;
	char res_name[8];
	int16_t entry_index;
} TieScene2dCutsceneUnresolvedKey;

#define UNRESOLVED_WARN_CAP 64
static TieScene2dCutsceneUnresolvedKey s_unresolved_warned[UNRESOLVED_WARN_CAP];
static int s_unresolved_warned_count;

static void TieScene2dCutscene_WarnUnresolvedActor(const TieScene2dFilmBundle* b, const char* res_name,
												   int16_t entry_index) {
	/* Already warned? */
	for (int i = 0; i < s_unresolved_warned_count; i++) {
		const TieScene2dCutsceneUnresolvedKey* k = &s_unresolved_warned[i];
		if (k->bundle == b && k->entry_index == entry_index && memcmp(k->res_name, res_name, 8) == 0)
			return;
	}
	/* Cap reached — emit one final summary line, then go silent. */
	if (s_unresolved_warned_count >= UNRESOLVED_WARN_CAP) {
		static bool s_warned_cap = false;
		if (!s_warned_cap) {
			s_warned_cap = true;
			Aeron_LogWarn("tie.cutscene", "manifest-miss dedup cap (%d) reached; further warnings suppressed",
						  UNRESOLVED_WARN_CAP);
		}
		return;
	}
	TieScene2dCutsceneUnresolvedKey* k = &s_unresolved_warned[s_unresolved_warned_count++];
	k->bundle = b;
	k->entry_index = entry_index;
	memcpy(k->res_name, res_name, 8);

	char name8[9];
	memcpy(name8, res_name, 8);
	name8[8] = '\0';
	Aeron_LogWarn("tie.assets", "manifest miss: bundle=%s/%s actor=%s entry=%d", b->lfd_basename,
				  b->film_name, name8, (int)entry_index);
}

static bool TieScene2dCutscene_ResolveActor(const TieScene2dFilmBundle* b, const TieScene2dActorView* a,
											int cur_cel, TieScene2dCutsceneResolvedActor* out) {
	memset(out, 0, sizeof *out);
	const TieScene2dActorEntry* e;
	const TieScene2dAssetVariant* av;
	if (!TieScene2dManifest_ResolveVariant(b, a->res_name, a->film_entry_index, cur_cel, &e, &av))
		return false;
	out->entry = e;
	out->variant = av;

	if (e->kind == ASSET_KIND_SPRITE) {
		if (!av->asset_path[0])
			return false;
		snprintf(out->full_path, sizeof out->full_path, "%s", av->asset_path);
	} else if (e->kind == ASSET_KIND_FRAMES) {
		if (!av->asset_path[0])
			return false;
		int state = a->state;
		if (state < 0)
			state = 0;
		snprintf(out->full_path, sizeof out->full_path, "%s/frame_%02d.ktx2", av->asset_path, state);
	} else if (e->kind == ASSET_KIND_ATLAS && av->atlas_loaded) {
		if (!av->asset_path[0])
			return false;
		snprintf(out->full_path, sizeof out->full_path, "%s", av->asset_path);
		out->use_atlas = true;
		int state = a->state;
		if (state < 0)
			state = 0;
		if (state >= av->atlas.frame_count)
			state = av->atlas.frame_count - 1;
		out->atlas_frame = av->atlas.frames[state];
	} else {
		return false;
	}
	return true;
}

void TieScene2dCutscene_PrepUploads(TieScene2dCutscene* g, AeronCommandBuffer* cmd, const char* lfd_basename,
									const char* film_name, int cur_cel, const TieScene2dActorView* actors,
									int actor_count) {
	if (!g || !cmd || !lfd_basename || !film_name)
		return;
	const TieScene2dFilmBundle* b = TieScene2dManifest_FindBundle(g->cs, lfd_basename, film_name);
	if (!b || !b->complete)
		return;

	for (int i = 0; i < actor_count; i++) {
		const TieScene2dActorView* a = &actors[i];
		if (!(a->flags & ACTOR2D_VISIBLE))
			continue;
		if (a->w <= 0 || a->h <= 0)
			continue;
		TieScene2dCutsceneResolvedActor r;
		if (!TieScene2dCutscene_ResolveActor(b, a, cur_cel, &r)) {
			/* prep_uploads runs before draw_actors each frame, so this
			 * is the single point that catches every visible-but-
			 * unresolved actor. The dedup set inside the helper stops
			 * us from spamming stderr — one line per unique miss. */
			TieScene2dCutscene_WarnUnresolvedActor(b, a->res_name, a->film_entry_index);
			continue;
		}
		/* Side effect: upload the texture if it's not in the cache.
		 * Return value not needed here — the draw phase looks it up
		 * again, by which point the cache hit guarantees no upload. */
		(void)Aeron_ImageCacheLoad(g->assets, cmd, r.full_path);
	}
}

void TieScene2dCutscene_RecordActorsInSource(TieScene2dCutscene* g, AeronDrawList2D* list, int viewport_w,
											 int viewport_h, int source_w, int source_h,
											 uint8_t source_pixel_aspect, const char* lfd_basename,
											 const char* film_name, int cur_cel,
											 const TieScene2dActorView* actors, int actor_count) {
	if (!g || !list || !lfd_basename || !film_name)
		return;
	if (viewport_w <= 0 || viewport_h <= 0)
		return;
	const TieScene2dFilmBundle* b = TieScene2dManifest_FindBundle(g->cs, lfd_basename, film_name);
	if (!b || !b->complete ||
		!TieScene2dManifest_BundleMatchesSource(b, source_w, source_h, source_pixel_aspect))
		return;

	TieScene2dCanvas canvas;
	TieScene2dCanvas_Begin(&canvas, list, viewport_w, viewport_h);
	TieScene2dCanvas_SetSourceAspect(&canvas, source_w, source_h, source_pixel_aspect);

	for (int i = 0; i < actor_count; i++) {
		const TieScene2dActorView* a = &actors[i];
		if (!(a->flags & ACTOR2D_VISIBLE))
			continue;
		if (a->w <= 0 || a->h <= 0)
			continue;
		TieScene2dCutsceneResolvedActor r;
		if (!TieScene2dCutscene_ResolveActor(b, a, cur_cel, &r))
			continue;

		/* Late-binding edge case: prep_uploads is expected to have
		 * populated the cache, but we pass NULL cmd here so the load
		 * function skips upload (a copy pass would fail inside an
		 * active render pass). Cache miss = silently drop the actor. */
		const AeronImageCacheEntry* t = Aeron_ImageCacheLoad(g->assets, NULL, r.full_path);
		if (!t || !t->tex)
			continue;

		TieScene2dActorTexture tex = {
			.texture = t->tex,
			.tex_w = t->w,
			.tex_h = t->h,
			.use_atlas = r.use_atlas,
			.atlas_w_authored = r.use_atlas ? r.variant->atlas.atlas_w : 0,
			.atlas_h_authored = r.use_atlas ? r.variant->atlas.atlas_h : 0,
			.atlas_frame_x = r.atlas_frame.x,
			.atlas_frame_y = r.atlas_frame.y,
			.atlas_frame_w = r.atlas_frame.w,
			.atlas_frame_h = r.atlas_frame.h,
		};
		TieScene2dActors_Record(&canvas, a, r.entry, &tex, cur_cel);
	}
}

void TieScene2dCutscene_RecordActors(TieScene2dCutscene* g, AeronDrawList2D* list, int viewport_w,
									 int viewport_h, const char* lfd_basename, const char* film_name,
									 int cur_cel, const TieScene2dActorView* actors, int actor_count) {
	TieScene2dCutscene_RecordActorsInSource(g, list, viewport_w, viewport_h, CLASSIC_FB_W, CLASSIC_FB_H,
											TIE_SCENE2D_VIEWPORT_PIXEL_ASPECT_VGA_4_3, lfd_basename,
											film_name, cur_cel, actors, actor_count);
}
