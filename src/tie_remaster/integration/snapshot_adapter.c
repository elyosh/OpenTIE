/*
 * cutscene compositor — tie_core-coupled glue for the game application.
 *
 * Marshals data from TieSnapshot_Current() into the snapshot-free
 * TieScene2dActorView[] form the cutscene compositor expects, and exposes the
 * bundle-presence predicate the application uses to gate the crossfade
 * machinery. Both functions take TieScene2dCutscene* explicitly so this
 * file has no static / extern coupling to the application main.
 *
 * Filmview drives the cutscene compositor directly from its own PlayerObject
 * adapter — this file is built only into the `tie` executable.
 */

#include "tie_remaster/integration/snapshot_adapter.h"

#include "tie_remaster/scene2d/srgb_math.h"
#include "tie_runtime/snapshot/snapshot.h" /* TieSnapshot_Current, TieActor2DState */

#include <stdint.h>
#include <string.h>

/* Pull (lfd, film) from the snapshot. Returns false when the engine
 * has no scene tagged (no active_film set) or the names are empty.
 *
 * OVERLAY-mode UI scenes set
 * active_film for their own remaster bundle just like PLAY1 cutscenes.
 * Flight owns a separate compositor, so it must not reuse a stale application-film
 * tag left by the scene that launched it. */
static bool TieRemasterSnapshot_Scene2dTuple(const char** out_lfd, const char** out_film) {
	const TieSnapshot* snap = TieSnapshot_Current();
	if (!snap)
		return false;
	if (snap->scene_kind == TIE_SCENE_FLIGHT)
		return false;
	if (snap->current_film_name[0] == '\0' || snap->current_lfd_basename[0] == '\0')
		return false;
	*out_lfd = snap->current_lfd_basename;
	*out_film = snap->current_film_name;
	return true;
}

bool TieRemasterSnapshot_HasScene2dBundle(TieScene2dCutscene* g) {
	if (!g)
		return false;
	const char *lfd, *film;
	if (!TieRemasterSnapshot_Scene2dTuple(&lfd, &film)) {
		return false;
	}
	const TieSnapshot* snapshot = TieSnapshot_Current();
	const int source_w = snapshot && snapshot->landru_coord_w ? snapshot->landru_coord_w : 320;
	const int source_h = snapshot && snapshot->landru_coord_h ? snapshot->landru_coord_h : 200;
	const uint8_t aspect = snapshot ? snapshot->landru_pixel_aspect : TIE_SOURCE_PIXEL_ASPECT_VGA_4_3;
	return TieScene2dCutscene_BundleMatchesSource(g, lfd, film, source_w, source_h, aspect);
}

bool TieRemasterSnapshot_HasScene2dInterpolation(TieScene2dCutscene* g) {
	const char *lfd, *film;
	if (!g || !TieRemasterSnapshot_Scene2dTuple(&lfd, &film) || !TieRemasterSnapshot_HasScene2dBundle(g))
		return false;
	return TieScene2dCutscene_BundleHasInterpolation(g, lfd, film);
}

int TieRemasterSnapshot_BuildActorViews(TieScene2dActorView* out, int cap, int* out_cur_cel,
										const char** out_lfd, const char** out_film) {
	if (out_cur_cel)
		*out_cur_cel = 0;
	if (out_lfd)
		*out_lfd = NULL;
	if (out_film)
		*out_film = NULL;

	const char *lfd, *film;
	if (!TieRemasterSnapshot_Scene2dTuple(&lfd, &film))
		return 0;
	if (out_lfd)
		*out_lfd = lfd;
	if (out_film)
		*out_film = film;

	const TieSnapshot* snap = TieSnapshot_Current();
	int n = 0;
	/* INCREMENTAL scenes (UI) walk draws_2D — the per-frame draw-list
	 * is the source of truth, since actors_2D's one-record-per-Actor
	 * model can't represent dialog code that draws the same Actor*
	 * multiple times per frame at different positions/states.
	 * FULL_FRAME scenes (cutscenes) keep using actors_2D — the FILM-
	 * driven actor pass produces a stable per-actor record with
	 * z-sorted ordering, and the RT is cleared every frame so the
	 * full set is rebuilt from scratch. */
	/* Sub-cel smoothing time signal — shared by every view we
	 * build for this frame. Live for FILM scenes via lfilm_emit_
	 * snapshot; 0.0 elsewhere (no-op). cel_period_us rides along
	 * for the manifest's optional interp_rate_hz quantizer. */
	const float frame_progress = snap->scene_clock.frame_progress;
	const uint32_t cel_period_us = snap->scene_clock.cel_period_us;

	/* Resolve a fore_color palette index → RGB for ACTOR2D_REMAP_COLOR.
	 * Engine emits the palette index on each silhouette draw_2D /
	 * actor_2D record; we expand to RGB here so the compose layer can
	 * stay palette-independent (matches how subtitle/UI text resolves
	 * color_index against snap->palette before emit). */
	const uint32_t* pal = snap->palette;

	if (snap->redraw_model == TIE_REDRAW_INCREMENTAL) {
		for (uint16_t i = 0; i < snap->draw_2D_count && n < cap; i++) {
			const TieDraw2D* d = &snap->draws_2D[i];
			TieScene2dActorView* v = &out[n++];
			v->res_type = d->res_type;
			memcpy(v->res_name, d->res_name, 8);
			v->film_entry_index = d->film_entry_index;
			v->x = d->x;
			v->y = d->y;
			v->w = d->w;
			v->h = d->h;
			/* Imperative draws_2D come from dialog/UI code that
			 * doesn't go through actor scaling; identity. */
			v->xscale = 256;
			v->yscale = 256;
			v->zplane = 0; /* iteration order = z order */
			v->state = d->state;
			/* Engine emits draws_2D only for actually-drawn calls
			 * (refresh != 0, data != NULL), so visibility is implicit. */
			v->flags = (uint16_t)(d->flags | ACTOR2D_VISIBLE);
			v->clip_left = d->clip_left;
			v->clip_top = d->clip_top;
			v->clip_right = d->clip_right;
			v->clip_bottom = d->clip_bottom;
			v->z_order = d->z_order;
			/* Imperative UI draws have no engine velocity; pin
			 * prev to current and zero the velocity so the
			 * compositor's lerp degenerates to a snap. */
			v->prev_x = d->x;
			v->prev_y = d->y;
			v->prev_xv = 0;
			v->prev_yv = 0;
			v->xv = 0;
			v->yv = 0;
			v->xvf = 0;
			v->yvf = 0;
			v->prev_xscale = v->xscale;
			v->prev_yscale = v->yscale;
			v->frame_progress = 0.0f;
			v->cel_period_us = 0;
			v->remap_rgba = (TieScene2dRgba) { 0 };
			if (d->flags & ACTOR2D_REMAP_COLOR) {
				uint32_t argb = pal[(uint8_t)d->fore_color];
				TieScene2dSrgb_PalToLinearRgb(argb, &v->remap_rgba.r, &v->remap_rgba.g, &v->remap_rgba.b);
				v->remap_rgba.a = 1.0f;
			}
			v->target = d->target;
		}
	} else {
		for (uint16_t i = 0; i < snap->actor_2D_count && n < cap; i++) {
			const TieActor2DState* a = &snap->actors_2D[i];
			TieScene2dActorView* v = &out[n++];
			v->res_type = a->res_type;
			memcpy(v->res_name, a->res_name, 8);
			v->film_entry_index = a->film_entry_index;
			v->x = a->x;
			v->y = a->y;
			v->w = a->w;
			v->h = a->h;
			v->xscale = a->xscale;
			v->yscale = a->yscale;
			v->zplane = a->zplane;
			v->state = a->state;
			v->flags = a->flags;
			v->clip_left = a->clip_left;
			v->clip_top = a->clip_top;
			v->clip_right = a->clip_right;
			v->clip_bottom = a->clip_bottom;
			/* Cutscenes don't merge-dispatch (REPLACE mode draws all
			 * actors first, subtitles on top); -1 = inert. */
			v->z_order = -1;
			/* Sub-cel motion fields — see TieScene2dActorView. The
			 * compositor decides whether to lerp based on the
			 * manifest's interpolate flag and a teleport check. */
			v->prev_x = a->prev_x;
			v->prev_y = a->prev_y;
			v->prev_xv = a->prev_xv;
			v->prev_yv = a->prev_yv;
			v->xv = a->xv;
			v->yv = a->yv;
			v->xvf = a->xvf;
			v->yvf = a->yvf;
			v->prev_xscale = a->prev_xscale;
			v->prev_yscale = a->prev_yscale;
			v->frame_progress = frame_progress;
			v->cel_period_us = cel_period_us;
			v->remap_rgba = (TieScene2dRgba) { 0 };
			if (a->flags & ACTOR2D_REMAP_COLOR) {
				uint32_t argb = pal[(uint8_t)a->fore_color];
				TieScene2dSrgb_PalToLinearRgb(argb, &v->remap_rgba.r, &v->remap_rgba.g, &v->remap_rgba.b);
				v->remap_rgba.a = 1.0f;
			}
			/* FULL_FRAME actors_2D don't go through the brief-widget
			 * scratch path. */
			v->target = 0;
		}
	}
	if (out_cur_cel) {
		bool resolved = false;
		for (uint16_t i = 0; i < snap->film_2D_count; i++) {
			const TieFilm2DState* f = &snap->films_2D[i];
			if (memcmp(f->res_name, film, 8) == 0) {
				*out_cur_cel = (int)f->cur_cel;
				resolved = true;
				break;
			}
		}
		/* Non-FILM scene fallback: when no FILM matches the active film
		 * tuple (e.g. SCENE_TITLE — no FILM resource is loaded), the
		 * engine-driven scene clock (TieSceneClock, set via
		 * TieSnapshotBuilder_SetSceneClock) carries the per-frame cursor
		 * the manifest's cel-based fields need. For SCENE_TITLE this
		 * matches title.c's `film_time` on fast systems. Outside any
		 * scene-clock-emitting scene the setter doesn't fire, so
		 * engine_frame is 0 → no-op fallback for FILM scenes. */
		if (!resolved && snap->scene_clock.engine_frame > 0)
			*out_cur_cel = (int)snap->scene_clock.engine_frame;
	}
	return n;
}
