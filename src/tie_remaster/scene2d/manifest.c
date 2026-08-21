/*
 * Cutscene compositor — manifest discovery + bundle map.
 *
 * Manifests are loaded through Aeron's configuration API.
 *
 * Schema is documented in docs/cutscene-asset-format.md. Briefly:
 *   - top-level keys: complete, opaque_background, extras, viewport,
 *     actors.
 *   - viewport's classic_region: opaque expression captured raw
 *     (reserved; not currently consumed).
 *   - actors map → per-actor entries with sprite/frames/atlas/layout
 *     plus override expressions (dst, tile_xywh, clip, fade), enums
 *     (fit, anchor, filter), and flags (hide, replace_with). Override
 *     expressions are kept as raw scalar strings; manifest_expr.c
 *     evaluates them per-frame against a ManifestEvalCtx.
 */

#include "tie_remaster/scene2d/manifest.h"
#include "aeron/config_file.h"
#include "aeron/log.h"
#include "aeron/numeric.h"
#include "aeron/scene/sprite_atlas.h"
#include "tie_remaster/scene2d/manifest_internal.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

/* Manifest type definitions live in cutscene_internal.h so the
 * Aeron draw path can read the same TieScene2dFilmBundle /
 * TieScene2dActorEntry structures this manifest parser populates. */

/* ---------- string utilities ---------- */

/* Copy `src` into a fixed-cap field with NUL termination, optionally
 * upper-casing as it goes (for LFD basenames). Used for the short
 * fields in TieScene2dFilmBundle/TieScene2dActorEntry (res_name, lfd_basename, etc.). */
static void TieScene2dManifest_StoreShort(char* dst, size_t cap, const char* src, int upper) {
	memset(dst, 0, cap);
	if (!src)
		return;
	size_t i = 0;
	for (; i + 1 < cap && src[i]; ++i)
		dst[i] = upper ? (char)toupper((unsigned char)src[i]) : src[i];
}
/* ---------- bundle list ---------- */

static TieScene2dActorEntry* TieScene2dManifest_AllocBundleActor(TieScene2dFilmBundle* b) {
	if (b->actor_count == b->actor_cap) {
		int new_cap = b->actor_cap ? b->actor_cap * 2 : 8;
		TieScene2dActorEntry* p =
			(TieScene2dActorEntry*)realloc(b->actors, sizeof(TieScene2dActorEntry) * (size_t)new_cap);
		if (!p)
			return NULL;
		b->actors = p;
		b->actor_cap = new_cap;
	}
	TieScene2dActorEntry* e = &b->actors[b->actor_count++];
	memset(e, 0, sizeof *e);
	e->entry_index = -1;
	/* Sub-cel smoothing parser sentinel — "not explicitly set on
	 * this actor". Resolved after the manifest is fully parsed
	 * via manifest_resolve_interp(): inherits the bundle-level
	 * default when set, else auto-enables when interp_rate_hz > 0,
	 * else stays off. After resolution every actor holds
	 * interpolate ∈ {0, 1} and interp_rate_hz ≥ 0. */
	e->interpolate = -1;
	e->interp_rate_hz = -1;
	return e;
}

static void TieScene2dManifest_BundleFree(TieScene2dFilmBundle* b) {
	for (int i = 0; i < b->actor_count; i++) {
		TieScene2dActorEntry* e = &b->actors[i];
		for (int v = 0; v < e->variant_count; v++)
			Aeron_SpriteAtlasFree(&e->variants[v].atlas);
		free(e->variants);
	}
	free(b->actors);
	free(b);
}

/* Append a fresh TieScene2dAssetVariant slot. Caller fills name/from_cel/etc. */
static TieScene2dAssetVariant* TieScene2dManifest_AllocActorVariant(TieScene2dActorEntry* e) {
	if (e->variant_count == e->variant_cap) {
		int new_cap = e->variant_cap ? e->variant_cap * 2 : 2;
		TieScene2dAssetVariant* p =
			(TieScene2dAssetVariant*)realloc(e->variants, sizeof(TieScene2dAssetVariant) * (size_t)new_cap);
		if (!p)
			return NULL;
		e->variants = p;
		e->variant_cap = new_cap;
	}
	TieScene2dAssetVariant* v = &e->variants[e->variant_count++];
	memset(v, 0, sizeof *v);
	return v;
}

/* Set the actor's asset kind (sprite/frames/atlas). Subsequent variant
 * additions must agree with `kind`; mixing kinds within one actor is
 * a manifest error and is rejected at parse time. */
static bool TieScene2dManifest_ActorSetKind(TieScene2dActorEntry* e, TieScene2dAssetKind kind) {
	if (e->kind == ASSET_KIND_NONE) {
		e->kind = kind;
		return true;
	}
	return e->kind == kind;
}

/* Scalar shorthand: a single variant active from cel 0. The common
 * case (most actors don't have palette-timeline transitions). */
static void TieScene2dManifest_ActorAddScalarVariant(TieScene2dActorEntry* e, TieScene2dAssetKind kind,
													 const char* name) {
	if (!name || !*name)
		return;
	if (!TieScene2dManifest_ActorSetKind(e, kind))
		return;
	TieScene2dAssetVariant* v = TieScene2dManifest_AllocActorVariant(e);
	if (!v)
		return;
	v->from_cel = 0;
	TieScene2dManifest_StoreShort(v->name, sizeof v->name, name, 0);
}

/* Atlas-only: explicit `layout: <name>` overrides the default-from-name
 * layout sibling. Applies to the first variant only (the scalar form);
 * sequence form encodes layout per variant via the sibling-name
 * implicit rule. */
static void TieScene2dManifest_ActorSetAtlasLayout(TieScene2dActorEntry* e, const char* layout) {
	if (!layout || !*layout)
		return;
	if (e->variant_count == 0)
		return;
	TieScene2dManifest_StoreShort(e->variants[0].layout_name, sizeof e->variants[0].layout_name, layout, 0);
}

static TieScene2dFilmBundle* TieScene2dManifest_AllocBundle(TieScene2dManifest* cs) {
	TieScene2dFilmBundle* b = (TieScene2dFilmBundle*)calloc(1, sizeof *b);
	if (!b)
		return NULL;
	/* Default: HD bundle fully replaces classic FB. Manifests that
	 * want HD-overlay-on-classic semantics (rare — FMV-stream
	 * cutscenes, scenes where classic intentionally bleeds through
	 * partial HD coverage) opt out via `opaque_background: false`.
	 * The HD-replaces-classic intent matches every authored UI
	 * scene so far and avoids the un-remastered-classic-bleed-
	 * through transient that bare-default bundles otherwise show
	 * during scene-transition palette fades. */
	b->opaque_background = true;
	/* Sub-cel smoothing defaults — parser sentinel "not set". Resolved
	 * after the actors mapping is parsed by manifest_resolve_interp()
	 * into concrete {0,1} / [0..1000] values per actor. */
	b->interpolate = -1;
	b->interp_rate_hz = -1;
	b->next = cs->bundles;
	cs->bundles = b;
	cs->bundle_count++;
	return b;
}

/* ---------- Aeron configuration walkers ---------- */

static const char* TieScene2dManifest_NodeScalar(const AeronConfigNode* node) {
	return AeronConfigNode_String(node, NULL);
}

static bool TieScene2dManifest_NodeTruthy(const AeronConfigNode* node) {
	if (AeronConfigNode_Type(node) == AERON_CONFIG_BOOL)
		return AeronConfigNode_Bool(node, 0) != 0;
	if (AeronConfigNode_Type(node) == AERON_CONFIG_INT)
		return AeronConfigNode_Int(node, 0) != 0;
	return false;
}

static const AeronConfigNode* TieScene2dManifest_MappingLookup(const AeronConfigNode* map, const char* key) {
	return AeronConfigNode_MapGet(map, key);
}

static const char* TieScene2dManifest_MappingLookupScalar(const AeronConfigNode* map, const char* key) {
	return TieScene2dManifest_NodeScalar(TieScene2dManifest_MappingLookup(map, key));
}

static int TieScene2dManifest_MappingLookupInt(const AeronConfigNode* map, const char* key, int fallback) {
	return (int)AeronConfigNode_Int(TieScene2dManifest_MappingLookup(map, key), fallback);
}

static void TieScene2dManifest_StoreNodeValue(char* dst, size_t cap, const AeronConfigNode* node) {
	char text[64];
	const char* value = AeronConfigNode_String(node, NULL);
	if (value) {
		TieScene2dManifest_StoreShort(dst, cap, value, 0);
	} else if (AeronConfigNode_Type(node) == AERON_CONFIG_INT) {
		snprintf(text, sizeof text, "%lld", (long long)AeronConfigNode_Int(node, 0));
		TieScene2dManifest_StoreShort(dst, cap, text, 0);
	} else if (AeronConfigNode_Type(node) == AERON_CONFIG_FLOAT &&
			   Aeron_FormatAsciiDouble(text, sizeof text, AeronConfigNode_Float(node, 0.0), 17)) {
		TieScene2dManifest_StoreShort(dst, cap, text, 0);
	} else {
		TieScene2dManifest_StoreShort(dst, cap, NULL, 0);
	}
}

/* Parse `fade: { from_cel: N, to_cel: M, to_color: { r, g, b } }`
 * (mapping or null). Updates `e->fade_*` in place. */
static void TieScene2dManifest_ParseFadeNode(const AeronConfigNode* m, TieScene2dActorEntry* e,
											 const char* path) {
	if (AeronConfigNode_Type(m) != AERON_CONFIG_MAP) {
		Aeron_LogWarn("tie.cutscene", "[cutscene] %s: actor '%s': fade must be an inline map", path,
					  e->res_name);
		return;
	}
	const AeronConfigNode* from = TieScene2dManifest_MappingLookup(m, "from_cel");
	const AeronConfigNode* to = TieScene2dManifest_MappingLookup(m, "to_cel");
	if (!from || !to) {
		Aeron_LogWarn("tie.cutscene",
					  "[cutscene] %s: actor '%s': fade missing from_cel "
					  "and/or to_cel",
					  path, e->res_name);
		return;
	}
	e->fade_from_cel = (int32_t)AeronConfigNode_Int(from, 0);
	e->fade_to_cel = (int32_t)AeronConfigNode_Int(to, 0);

	/* Optional to_color (defaults to black). */
	const AeronConfigNode* color = TieScene2dManifest_MappingLookup(m, "to_color");
	if (AeronConfigNode_Type(color) == AERON_CONFIG_MAP) {
		long rv = TieScene2dManifest_MappingLookupInt(color, "r", 0);
		long gv = TieScene2dManifest_MappingLookupInt(color, "g", 0);
		long bv = TieScene2dManifest_MappingLookupInt(color, "b", 0);
		if (rv < 0)
			rv = 0;
		if (rv > 255)
			rv = 255;
		if (gv < 0)
			gv = 0;
		if (gv > 255)
			gv = 255;
		if (bv < 0)
			bv = 0;
		if (bv > 255)
			bv = 255;
		e->fade_to_r = (uint8_t)rv;
		e->fade_to_g = (uint8_t)gv;
		e->fade_to_b = (uint8_t)bv;
	}

	e->fade_active = (e->fade_to_cel != e->fade_from_cel);
	if (!e->fade_active)
		Aeron_LogWarn("tie.cutscene",
					  "[cutscene] %s: actor '%s': fade with "
					  "from_cel == to_cel ignored",
					  path, e->res_name);
}

static void TieScene2dManifest_ParseSourceSpaceNode(const AeronConfigNode* node, TieScene2dFilmBundle* bundle,
													const char* path) {
	if (AeronConfigNode_Type(node) != AERON_CONFIG_MAP) {
		Aeron_LogWarn("tie.cutscene", "[cutscene] %s: source_space must be a mapping", path);
		return;
	}
	const int width = TieScene2dManifest_MappingLookupInt(node, "width", 0);
	const int height = TieScene2dManifest_MappingLookupInt(node, "height", 0);
	const char* aspect = TieScene2dManifest_MappingLookupScalar(node, "pixel_aspect");
	if (width <= 0 || width > UINT16_MAX || height <= 0 || height > UINT16_MAX || !aspect ||
		(strcmp(aspect, "square") != 0 && strcmp(aspect, "vga_4_3") != 0)) {
		Aeron_LogWarn("tie.cutscene", "[cutscene] %s: invalid source_space", path);
		return;
	}
	bundle->source_width = (uint16_t)width;
	bundle->source_height = (uint16_t)height;
	bundle->source_pixel_aspect = strcmp(aspect, "vga_4_3") == 0 ? TIE_SCENE2D_VIEWPORT_PIXEL_ASPECT_VGA_4_3
																 : TIE_SCENE2D_VIEWPORT_PIXEL_ASPECT_SQUARE;
	bundle->source_space_explicit = true;
}

/* Parse `dst:` / `clip:` mapping into 4 separate per-component
 * expression strings. Missing components leave the destination empty
 * (= "no override on this component"); the compositor blends defaults
 * with overrides per-component. */
static void TieScene2dManifest_ParseRectNode(const AeronConfigNode* m, char* out_x, size_t cx, char* out_y,
											 size_t cy, char* out_w, size_t cw, char* out_h, size_t ch) {
	if (AeronConfigNode_Type(m) != AERON_CONFIG_MAP)
		return;
	TieScene2dManifest_StoreNodeValue(out_x, cx, TieScene2dManifest_MappingLookup(m, "x"));
	TieScene2dManifest_StoreNodeValue(out_y, cy, TieScene2dManifest_MappingLookup(m, "y"));
	TieScene2dManifest_StoreNodeValue(out_w, cw, TieScene2dManifest_MappingLookup(m, "w"));
	TieScene2dManifest_StoreNodeValue(out_h, ch, TieScene2dManifest_MappingLookup(m, "h"));
}

/* `fit:` accepts the string forms `default` and `extend`. Anything else
 * logs a warning and leaves the mode at FIT_DEFAULT (no-op). */
static bool TieScene2dManifest_ParseFitMode(const char* s, TieScene2dFitMode* out) {
	if (!s)
		return false;
	if (strcmp(s, "default") == 0) {
		*out = FIT_DEFAULT;
		return true;
	}
	if (strcmp(s, "extend") == 0) {
		*out = FIT_EXTEND;
		return true;
	}
	return false;
}

/* `anchor:` accepts the 9-point grid plus the shorthand alias
 * `top`/`bottom`/`left`/`right` for the corresponding edge centers.
 * Returns false (and leaves *out untouched) for unknown values. */
static bool TieScene2dManifest_ParseAnchor(const char* s, TieScene2dAnchor* out) {
	if (!s)
		return false;
	if (strcmp(s, "center") == 0) {
		*out = ANCHOR_CENTER;
		return true;
	}
	if (strcmp(s, "top_left") == 0) {
		*out = ANCHOR_TOP_LEFT;
		return true;
	}
	if (strcmp(s, "top_center") == 0 || strcmp(s, "top") == 0) {
		*out = ANCHOR_TOP_CENTER;
		return true;
	}
	if (strcmp(s, "top_right") == 0) {
		*out = ANCHOR_TOP_RIGHT;
		return true;
	}
	if (strcmp(s, "center_left") == 0 || strcmp(s, "left") == 0) {
		*out = ANCHOR_CENTER_LEFT;
		return true;
	}
	if (strcmp(s, "center_right") == 0 || strcmp(s, "right") == 0) {
		*out = ANCHOR_CENTER_RIGHT;
		return true;
	}
	if (strcmp(s, "bottom_left") == 0) {
		*out = ANCHOR_BOTTOM_LEFT;
		return true;
	}
	if (strcmp(s, "bottom_center") == 0 || strcmp(s, "bottom") == 0) {
		*out = ANCHOR_BOTTOM_CENTER;
		return true;
	}
	if (strcmp(s, "bottom_right") == 0) {
		*out = ANCHOR_BOTTOM_RIGHT;
		return true;
	}
	return false;
}

/* Parse one of (sprite, frames, atlas). Three forms accepted:
 *   - scalar:         single variant, from_cel=0
 *   - empty value:    not allowed (caller must pass a real node)
 *   - sequence of {from_cel, name}: palette-timeline variants
 */
static void TieScene2dManifest_ParseVariantField(const AeronConfigNode* v, TieScene2dActorEntry* e,
												 TieScene2dAssetKind kind, const char* path) {
	if (!v)
		return;
	if (!TieScene2dManifest_ActorSetKind(e, kind)) {
		Aeron_LogWarn("tie.cutscene",
					  "[cutscene] %s: actor '%s' mixes asset kinds; "
					  "ignoring later kind",
					  path, e->res_name);
		return;
	}
	if (AeronConfigNode_Type(v) == AERON_CONFIG_STRING) {
		TieScene2dManifest_ActorAddScalarVariant(e, kind, AeronConfigNode_String(v, NULL));
		return;
	}
	if (AeronConfigNode_Type(v) == AERON_CONFIG_SEQUENCE) {
		for (size_t index = 0; index < AeronConfigNode_SequenceCount(v); ++index) {
			const AeronConfigNode* item = AeronConfigNode_SequenceGet(v, index);
			if (AeronConfigNode_Type(item) != AERON_CONFIG_MAP) {
				Aeron_LogWarn("tie.cutscene", "[cutscene] %s: actor '%s' variant entry not a map", path,
							  e->res_name);
				continue;
			}
			const char* name = TieScene2dManifest_MappingLookupScalar(item, "name");
			if (!name) {
				Aeron_LogWarn("tie.cutscene", "[cutscene] %s: actor '%s' variant missing name", path,
							  e->res_name);
				continue;
			}
			TieScene2dAssetVariant* av = TieScene2dManifest_AllocActorVariant(e);
			if (!av)
				continue;
			av->from_cel = TieScene2dManifest_MappingLookupInt(item, "from_cel", 0);
			TieScene2dManifest_StoreShort(av->name, sizeof av->name, name, 0);
		}
		return;
	}
	Aeron_LogWarn("tie.cutscene", "[cutscene] %s: actor '%s' asset field has unexpected type", path,
				  e->res_name);
}

/* Parse a single actor entry. The mapping key (e.g. `mothma02#12`)
 * has already been split off; the value node is the actor's
 * mapping body. */
static void TieScene2dManifest_ParseActor(const char* res_key, const AeronConfigNode* body,
										  TieScene2dFilmBundle* b, const char* path) {
	if (AeronConfigNode_Type(body) != AERON_CONFIG_MAP) {
		Aeron_LogWarn("tie.cutscene", "[cutscene] %s: actor '%s': expected mapping body", path, res_key);
		return;
	}
	TieScene2dActorEntry* e = TieScene2dManifest_AllocBundleActor(b);
	if (!e)
		return;

	/* Split `res_name#N` → res_name + entry_index. The bare form
	 * leaves entry_index at -1 (matches any instance). The `#N`
	 * form binds the entry to one specific FilmObject array index. */
	char res_name[16] = { 0 };
	size_t i = 0;
	for (; i + 1 < sizeof res_name && res_key[i] && res_key[i] != '#'; i++)
		res_name[i] = res_key[i];
	if (res_key[i] == '#')
		e->entry_index = (int16_t)strtol(res_key + i + 1, NULL, 10);
	TieScene2dManifest_StoreShort(e->res_name, sizeof e->res_name, res_name, 0);

	/* Walk all fields. Order is unimportant — every field dispatches
	 * to its own handler. Asset fields (sprite/frames/atlas) update
	 * e->kind; conflicting kinds within one actor are warned. */
	for (size_t field = 0; field < AeronConfigNode_MapCount(body); ++field) {
		const char* k = AeronConfigNode_MapKeyAt(body, field);
		const AeronConfigNode* v = AeronConfigNode_MapValueAt(body, field);
		if (!k || !v)
			continue;
		const char* vs = TieScene2dManifest_NodeScalar(v);

		if (strcmp(k, "sprite") == 0) {
			TieScene2dManifest_ParseVariantField(v, e, ASSET_KIND_SPRITE, path);
		} else if (strcmp(k, "frames") == 0) {
			TieScene2dManifest_ParseVariantField(v, e, ASSET_KIND_FRAMES, path);
		} else if (strcmp(k, "atlas") == 0) {
			TieScene2dManifest_ParseVariantField(v, e, ASSET_KIND_ATLAS, path);
		} else if (strcmp(k, "layout") == 0 && vs) {
			TieScene2dManifest_ActorSetAtlasLayout(e, vs);
		} else if (strcmp(k, "dst") == 0) {
			TieScene2dManifest_ParseRectNode(v, e->dst_x_expr, sizeof e->dst_x_expr, e->dst_y_expr,
											 sizeof e->dst_y_expr, e->dst_w_expr, sizeof e->dst_w_expr,
											 e->dst_h_expr, sizeof e->dst_h_expr);
		} else if (strcmp(k, "clip") == 0) {
			TieScene2dManifest_ParseRectNode(v, e->clip_x_expr, sizeof e->clip_x_expr, e->clip_y_expr,
											 sizeof e->clip_y_expr, e->clip_w_expr, sizeof e->clip_w_expr,
											 e->clip_h_expr, sizeof e->clip_h_expr);
		} else if (strcmp(k, "tile_x") == 0) {
			TieScene2dManifest_StoreNodeValue(e->tile_x_expr, sizeof e->tile_x_expr, v);
		} else if (strcmp(k, "tile_y") == 0) {
			TieScene2dManifest_StoreNodeValue(e->tile_y_expr, sizeof e->tile_y_expr, v);
		} else if (strcmp(k, "tile_w") == 0) {
			TieScene2dManifest_StoreNodeValue(e->tile_w_expr, sizeof e->tile_w_expr, v);
		} else if (strcmp(k, "tile_h") == 0) {
			TieScene2dManifest_StoreNodeValue(e->tile_h_expr, sizeof e->tile_h_expr, v);
		} else if (strcmp(k, "hide") == 0) {
			e->hide = TieScene2dManifest_NodeTruthy(v);
		} else if (strcmp(k, "replace_with") == 0 && vs) {
			TieScene2dManifest_StoreShort(e->replace_with, sizeof e->replace_with, vs, 0);
		} else if (strcmp(k, "filter") == 0 && vs) {
			e->filter_linear = (strcmp(vs, "linear") == 0);
		} else if (strcmp(k, "fit") == 0 && vs) {
			if (!TieScene2dManifest_ParseFitMode(vs, &e->fit))
				Aeron_LogWarn("tie.cutscene", "[cutscene] %s: actor '%s': unknown fit mode '%s'", path,
							  e->res_name, vs);
		} else if (strcmp(k, "anchor") == 0 && vs) {
			if (!TieScene2dManifest_ParseAnchor(vs, &e->anchor))
				Aeron_LogWarn("tie.cutscene", "[cutscene] %s: actor '%s': unknown anchor '%s'", path,
							  e->res_name, vs);
		} else if (strcmp(k, "fade") == 0) {
			TieScene2dManifest_ParseFadeNode(v, e, path);
		} else if (strcmp(k, "interpolate") == 0) {
			e->interpolate = TieScene2dManifest_NodeTruthy(v) ? 1 : 0;
		} else if (strcmp(k, "interp_rate") == 0) {
			int hz = (int)AeronConfigNode_Int(v, 0);
			if (hz < 0)
				hz = 0;
			else if (hz > 1000)
				hz = 1000;
			e->interp_rate_hz = hz;
		}
		/* `cel_overrides` is reserved for v2 — ignored entirely now. */
	}
}

static void TieScene2dManifest_ParseActorsNode(const AeronConfigNode* actors_node, TieScene2dFilmBundle* b,
											   const char* path) {
	/* `actors:` with no body is a valid placeholder manifest emitted for
	 * films whose remastered assets are not authored yet. */
	if (!actors_node)
		return;
	if (AeronConfigNode_Type(actors_node) == AERON_CONFIG_STRING) {
		const char* s = AeronConfigNode_String(actors_node, NULL);
		if (!s || !*s)
			return;
	}
	if (AeronConfigNode_Type(actors_node) != AERON_CONFIG_MAP) {
		Aeron_LogWarn("tie.cutscene", "[cutscene] %s: `actors` must be a mapping", path);
		return;
	}
	for (size_t index = 0; index < AeronConfigNode_MapCount(actors_node); ++index) {
		const char* res_key = AeronConfigNode_MapKeyAt(actors_node, index);
		const AeronConfigNode* body = AeronConfigNode_MapValueAt(actors_node, index);
		if (!res_key || !body)
			continue;
		TieScene2dManifest_ParseActor(res_key, body, b, path);
	}
}

static void TieScene2dManifest_ParseExtrasNode(const AeronConfigNode* seq, TieScene2dFilmBundle* b) {
	if (AeronConfigNode_Type(seq) != AERON_CONFIG_SEQUENCE)
		return;
	for (size_t index = 0;
		 index < AeronConfigNode_SequenceCount(seq) && b->extras_count < CUTSCENE_MAX_EXTRAS; ++index) {
		const char* s = TieScene2dManifest_NodeScalar(AeronConfigNode_SequenceGet(seq, index));
		if (!s || !*s)
			continue;
		TieScene2dManifest_StoreShort(b->extras[b->extras_count++], sizeof b->extras[0], s, 1);
	}
}

static void TieScene2dManifest_ParseViewportNode(const AeronConfigNode* vp, TieScene2dFilmBundle* b) {
	if (AeronConfigNode_Type(vp) != AERON_CONFIG_MAP)
		return;
	/* Preserve the inline mapping as a comma-separated string for the
	 * existing expression evaluator. */
	const AeronConfigNode* region = TieScene2dManifest_MappingLookup(vp, "classic_region");
	if (AeronConfigNode_Type(region) != AERON_CONFIG_MAP)
		return;
	/* Build "x: <expr>, y: <expr>, w: <expr>, h: <expr>" verbatim. */
	char buf[64];
	size_t pos = 0;
	static const char* keys[] = { "x", "y", "w", "h" };
	for (size_t i = 0; i < 4 && pos + 1 < sizeof buf; i++) {
		char value[32];
		TieScene2dManifest_StoreNodeValue(value, sizeof value,
										  TieScene2dManifest_MappingLookup(region, keys[i]));
		if (!value[0])
			continue;
		int n = snprintf(buf + pos, sizeof buf - pos, "%s%s: %s", pos ? ", " : "", keys[i], value);
		if (n < 0)
			break;
		pos += (size_t)n;
		if (pos >= sizeof buf) {
			pos = sizeof buf - 1;
			break;
		}
	}
	TieScene2dManifest_StoreShort(b->classic_region, sizeof b->classic_region, buf, 0);
}

static bool TieScene2dManifest_ParseManifest(TieScene2dManifest* cs, const char* path,
											 const char* lfd_basename, const char* film_name) {
	AeronConfigFile* document = NULL;
	AeronConfigError error = { 0 };
	const AeronConfigNode* root;
	if (!AeronConfigFile_LoadYamlEx(cs->vfs, cs->vfs_root, path, &document, &error)) {
		Aeron_LogWarn("tie.cutscene", "[cutscene] cannot load %s: %s", path, error.message);
		return false;
	}

	TieScene2dFilmBundle* b = TieScene2dManifest_AllocBundle(cs);
	if (!b) {
		AeronConfigFile_Destroy(document);
		return false;
	}
	TieScene2dManifest_StoreShort(b->lfd_basename, sizeof b->lfd_basename, lfd_basename, 1);
	TieScene2dManifest_StoreShort(b->film_name, sizeof b->film_name, film_name, 0);

	root = AeronConfigFile_Root(document);
	if (AeronConfigNode_Type(root) != AERON_CONFIG_MAP) {
		Aeron_LogWarn("tie.cutscene", "[cutscene] %s: top-level node is not a mapping", path);
		AeronConfigFile_Destroy(document);
		return true; /* bundle remains in list (empty) — same as before */
	}

	/* Top-level keys: complete, opaque_background, extras, viewport,
	 * actors. Walk once; missing keys leave defaults in place. */
	for (size_t field = 0; field < AeronConfigNode_MapCount(root); ++field) {
		const char* k = AeronConfigNode_MapKeyAt(root, field);
		const AeronConfigNode* v = AeronConfigNode_MapValueAt(root, field);
		if (!k || !v)
			continue;

		if (strcmp(k, "complete") == 0) {
			b->complete = TieScene2dManifest_NodeTruthy(v);
		} else if (strcmp(k, "opaque_background") == 0) {
			b->opaque_background = TieScene2dManifest_NodeTruthy(v);
		} else if (strcmp(k, "extras") == 0) {
			TieScene2dManifest_ParseExtrasNode(v, b);
		} else if (strcmp(k, "viewport") == 0) {
			TieScene2dManifest_ParseViewportNode(v, b);
		} else if (strcmp(k, "source_space") == 0) {
			TieScene2dManifest_ParseSourceSpaceNode(v, b, path);
		} else if (strcmp(k, "actors") == 0) {
			TieScene2dManifest_ParseActorsNode(v, b, path);
		} else if (strcmp(k, "interpolate") == 0) {
			b->interpolate = TieScene2dManifest_NodeTruthy(v) ? 1 : 0;
		} else if (strcmp(k, "interp_rate") == 0) {
			int hz = (int)AeronConfigNode_Int(v, 0);
			if (hz < 0)
				hz = 0;
			else if (hz > 1000)
				hz = 1000;
			b->interp_rate_hz = hz;
		}
	}

	/* Inheritance resolution.
	 *
	 * Bundle-level `interpolate:` and `interp_rate:` set the film
	 * default; per-actor keys override. After this pass every
	 * TieScene2dActorEntry holds final values (interpolate ∈ {0, 1},
	 * interp_rate_hz ∈ [0, 1000]) so TieScene2dActors_Emit can read
	 * them directly without walking back to the bundle.
	 *
	 * Order matters: resolve interp_rate_hz first, then use it to
	 * auto-enable interpolate when neither level explicitly set
	 * the latter — "specifying a rate implies you want it on" is
	 * the asset-team UX shortcut so manifests don't need to
	 * repeat `interpolate: true` next to every `interp_rate:`. */
	for (int i = 0; i < b->actor_count; i++) {
		TieScene2dActorEntry* e = &b->actors[i];

		int32_t rate = e->interp_rate_hz;
		if (rate < 0)
			rate = b->interp_rate_hz;
		if (rate < 0)
			rate = 0;
		e->interp_rate_hz = rate;

		int8_t interp = e->interpolate;
		if (interp < 0)
			interp = b->interpolate;
		if (interp < 0)
			interp = (rate > 0) ? 1 : 0;
		e->interpolate = interp;
		if (!e->hide && interp)
			b->has_interpolation = true;
	}

	AeronConfigFile_Destroy(document);
	return true;
}

/* ---------- directory walk ---------- */

static bool TieScene2dManifest_IsDir(const char* path) {
	struct stat st;
	return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static bool TieScene2dManifest_IsFile(const char* path) {
	struct stat st;
	return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

/* Each LFD directory owns its sprites and atlases, with film manifests below
 * films/. Actor assets are runtime KTX2 files; manifests may resolve short
 * names through the LFD dependency chain. */
static void TieScene2dManifest_ScanLfdDir(TieScene2dManifest* cs, const char* root, const char* lfd_name) {
	char films_path[1024];
	snprintf(films_path, sizeof films_path, "%s/%s/films", root, lfd_name);
	DIR* d = opendir(films_path);
	if (!d)
		return; /* asset-only LFD (e.g. EMPIRE) — no films, that's fine */
	struct dirent* e;
	while ((e = readdir(d)) != NULL) {
		if (e->d_name[0] == '.')
			continue;
		char manifest[1024];
		char relative[1024];
		snprintf(manifest, sizeof manifest, "%s/%s/manifest.yaml", films_path, e->d_name);
		if (!TieScene2dManifest_IsFile(manifest))
			continue;
		snprintf(relative, sizeof relative, "%s%s%s/films/%s/manifest.yaml", cs->vfs_prefix,
				 cs->vfs_prefix[0] ? "/" : "", lfd_name, e->d_name);
		TieScene2dManifest_ParseManifest(cs, relative, lfd_name, e->d_name);
	}
	closedir(d);
}

static void TieScene2dManifest_ScanRoot(TieScene2dManifest* cs) {
	DIR* d = opendir(cs->root);
	if (!d) {
		Aeron_LogWarn("tie.cutscene", "[cutscene] opendir(%s) failed: %s", cs->root, strerror(errno));
		return;
	}
	struct dirent* e;
	while ((e = readdir(d)) != NULL) {
		if (e->d_name[0] == '.')
			continue;
		char sub[1024];
		snprintf(sub, sizeof sub, "%s/%s", cs->root, e->d_name);
		if (TieScene2dManifest_IsDir(sub))
			TieScene2dManifest_ScanLfdDir(cs, cs->root, e->d_name);
	}
	closedir(d);
}

/* Resolve explicit paths beneath the remaster root. Bare names search the
 * owning LFD followed by its extras; non-empty extensions identify files,
 * while an empty extension identifies a frames directory. */
static bool TieScene2dManifest_ResolveAsset(const TieScene2dManifest* cs, const TieScene2dFilmBundle* b,
											const char* kind, const char* ext, const char* name, char* out,
											size_t out_cap) {
	if (!name || !name[0])
		return false;
	/* Form 1: contains a slash → explicit path under the root. */
	if (strchr(name, '/')) {
		snprintf(out, out_cap, "%s/%s", cs->root, name);
		return true;
	}
	/* Form 2: bare name → walk the chain. */
	int n_chain = 1 + b->extras_count;
	for (int i = 0; i < n_chain; i++) {
		const char* lfd = (i == 0) ? b->lfd_basename : b->extras[i - 1];
		if (ext && ext[0])
			snprintf(out, out_cap, "%s/%s/%s/%s.%s", cs->root, lfd, kind, name, ext);
		else
			snprintf(out, out_cap, "%s/%s/%s/%s", cs->root, lfd, kind, name);
		if (ext && ext[0]) {
			if (TieScene2dManifest_IsFile(out))
				return true;
		} else {
			if (TieScene2dManifest_IsDir(out))
				return true;
		}
	}
	return false;
}

/* ---------- public API ---------- */

TieScene2dManifest* TieScene2dManifest_Open(AeronVfs* vfs, AeronVfsRoot root, const char* vfs_prefix,
											const char* remaster_dir, const char* frontend_profile_id) {
	if (!vfs || !remaster_dir || !remaster_dir[0])
		return NULL;
	if (!TieScene2dManifest_IsDir(remaster_dir)) {
		Aeron_LogWarn("tie.cutscene", "[cutscene] resource root '%s' is not a directory", remaster_dir);
		return NULL;
	}
	TieScene2dManifest* cs = (TieScene2dManifest*)calloc(1, sizeof *cs);
	if (!cs)
		return NULL;
	cs->vfs = vfs;
	cs->vfs_root = root;
	TieScene2dManifest_StoreShort(cs->vfs_prefix, sizeof cs->vfs_prefix, vfs_prefix ? vfs_prefix : "", 0);
	TieScene2dManifest_StoreShort(cs->root, sizeof cs->root, remaster_dir, 0);
	TieScene2dManifest_StoreShort(cs->profile_id, sizeof cs->profile_id,
								  frontend_profile_id ? frontend_profile_id : "", 0);

	/* Prefer an edition-qualified manifest tree when present. Existing
	 * single-edition trees remain valid and are used as a compatibility
	 * fallback. All bundles and asset paths in one manifest instance then
	 * belong to exactly one frontend profile. */
	if (cs->profile_id[0]) {
		char qualified_root[sizeof cs->root];
		int length = snprintf(qualified_root, sizeof qualified_root, "%s/%s", remaster_dir, cs->profile_id);
		if (length > 0 && (size_t)length < sizeof qualified_root && TieScene2dManifest_IsDir(qualified_root))
			TieScene2dManifest_StoreShort(cs->root, sizeof cs->root, qualified_root, 0);
	}

	TieScene2dManifest_ScanRoot(cs);

	if (cs->bundle_count == 0) {
		Aeron_LogWarn("tie.cutscene", "[cutscene] no manifests found under %s — disabling", cs->root);
		TieScene2dManifest_Close(cs);
		return NULL;
	}

	/* Resolve asset paths and parse atlas YAML siblings up front.
	 * Texture decode + GPU upload is deferred to first draw — the
	 * texture cache (cutscene_assets_gpu) is LRU-evicted with a
	 * memory budget, so loading lazily keeps startup fast and bounds
	 * VRAM by working-set size instead of total-bundle size.
	 *
	 * What still happens here:
	 *  - TieScene2dManifest_ResolveAsset walks the LFD chain to bind each variant to a
	 *    concrete file path (cheap — directory stat-ish). Missing
	 *    paths leave the variant unresolved; the draw path skips it.
	 *  - atlas YAML layouts are parsed up front
	 *    so the very first draw of an atlas actor knows its sub-rect
	 *    geometry without paying any decode cost on the first frame.
	 */
	int total_actors = 0;
	for (TieScene2dFilmBundle* b = cs->bundles; b; b = b->next) {
		total_actors += b->actor_count;
		for (int i = 0; i < b->actor_count; i++) {
			TieScene2dActorEntry* e = &b->actors[i];
			/* Skip resolution for hidden actors — resolve_actor short-
			 * circuits on e->hide so the path / layout would never be
			 * consulted at draw time. The remaster bundle then doesn't
			 * need to ship pixels for entries the artist explicitly
			 * suppressed. */
			if (e->hide)
				continue;
			for (int v = 0; v < e->variant_count; v++) {
				TieScene2dAssetVariant* av = &e->variants[v];
				if (e->kind == ASSET_KIND_SPRITE) {
					TieScene2dManifest_ResolveAsset(cs, b, "sprites", "ktx2", av->name, av->asset_path,
													sizeof av->asset_path);
				} else if (e->kind == ASSET_KIND_ATLAS) {
					TieScene2dManifest_ResolveAsset(cs, b, "atlas", "ktx2", av->name, av->asset_path,
													sizeof av->asset_path);
					/* Sibling layout YAML — required to map an ANIM
					 * state index to a sub-rect within the atlas.
					 * Defaults to the variant name (extract emits the
					 * `<name>.yaml` sibling), overridable via the
					 * scalar form's `layout:` field. */
					const char* layout_ref = av->layout_name[0] ? av->layout_name : av->name;
					if (TieScene2dManifest_ResolveAsset(cs, b, "atlas", "yaml", layout_ref, av->yaml_path,
														sizeof av->yaml_path))
						av->atlas_loaded = Aeron_SpriteAtlasLoad(&av->atlas, av->yaml_path);
				} else if (e->kind == ASSET_KIND_FRAMES) {
					/* frames-dir resolves to a directory; the draw
					 * path appends `frame_NN.ktx2` per actor state. */
					TieScene2dManifest_ResolveAsset(cs, b, "atlas", "", av->name, av->asset_path,
													sizeof av->asset_path);
				}
			}
		}
	}

	Aeron_LogInfo("tie.cutscene",
				  "loaded %d bundle%s, %d actor entr%s from %s "
				  "(textures load lazily on the GPU side)\n",
				  cs->bundle_count, cs->bundle_count == 1 ? "" : "s", total_actors,
				  total_actors == 1 ? "y" : "ies", cs->root);
	return cs;
}

void TieScene2dManifest_Close(TieScene2dManifest* cs) {
	if (!cs)
		return;
	TieScene2dFilmBundle* b = cs->bundles;
	while (b) {
		TieScene2dFilmBundle* next = b->next;
		TieScene2dManifest_BundleFree(b);
		b = next;
	}
	free(cs);
}

/* ---------- lookup helpers ---------- */

const TieScene2dFilmBundle* TieScene2dManifest_FindBundle(const TieScene2dManifest* cs, const char* lfd,
														  const char* film) {
	if (!cs)
		return NULL;
	for (TieScene2dFilmBundle* b = cs->bundles; b; b = b->next)
		if (strcmp(b->lfd_basename, lfd) == 0 && strcmp(b->film_name, film) == 0)
			return b;
	return NULL;
}

const TieScene2dActorEntry* TieScene2dManifest_FindActor(const TieScene2dFilmBundle* b, const char* res_name,
														 int16_t entry_index) {
	/* res_name is either a snapshot's `char[8]` (NUL-padded, no
	 * terminator) or a shorter C string literal — strncpy handles
	 * both without over-reading short literals. Two-pass lookup:
	 * an entry_index match wins over a bare `res_name` entry. */
	char key[9] = { 0 };
	strncpy(key, res_name, 8);
	if (entry_index >= 0) {
		for (int i = 0; i < b->actor_count; i++)
			if (b->actors[i].entry_index == entry_index && strncmp(b->actors[i].res_name, key, 8) == 0)
				return &b->actors[i];
	}
	for (int i = 0; i < b->actor_count; i++)
		if (b->actors[i].entry_index < 0 && strncmp(b->actors[i].res_name, key, 8) == 0)
			return &b->actors[i];
	return NULL;
}

/* Return whether a complete bundle is available for a film tuple. */
bool TieScene2dManifest_HasCompleteBundle(const TieScene2dManifest* cs, const char* lfd_basename,
										  const char* film_name) {
	if (!cs || !lfd_basename || !film_name)
		return false;
	TieScene2dFilmBundle* b =
		(TieScene2dFilmBundle*)TieScene2dManifest_FindBundle(cs, lfd_basename, film_name);
	return b && b->complete;
}

/* Pick the active variant from a sorted timeline at the given film
 * cel. Latest variant whose `from_cel` <= cel wins. With one variant
 * (the dominant case) returns 0 in O(1). */
int TieScene2dManifest_VariantAtCel(const TieScene2dAssetVariant* vs, int n, int cel) {
	int found = 0;
	for (int i = 1; i < n; i++) {
		if (vs[i].from_cel <= cel)
			found = i;
		else
			break;
	}
	return found;
}

bool TieScene2dManifest_ResolveVariant(const TieScene2dFilmBundle* b, const char* res_name,
									   int16_t entry_index, int cur_cel,
									   const TieScene2dActorEntry** out_entry,
									   const TieScene2dAssetVariant** out_variant) {
	if (!b || !res_name || !out_entry || !out_variant)
		return false;
	const TieScene2dActorEntry* e = TieScene2dManifest_FindActor(b, res_name, entry_index);
	if (!e || e->hide || e->variant_count == 0)
		return false;
	int vi = TieScene2dManifest_VariantAtCel(e->variants, e->variant_count, cur_cel);
	*out_entry = e;
	*out_variant = &e->variants[vi];
	return true;
}

bool TieScene2dManifest_BundleMatchesSource(const TieScene2dFilmBundle* b, int source_w, int source_h,
											uint8_t source_pixel_aspect) {
	if (!b || !b->source_space_explicit)
		return b != NULL;
	if (b->source_width == source_w && b->source_height == source_h &&
		b->source_pixel_aspect == source_pixel_aspect)
		return true;

	TieScene2dFilmBundle* mutable_bundle = (TieScene2dFilmBundle*)b;
	if (!mutable_bundle->source_mismatch_warned) {
		Aeron_LogWarn("tie.cutscene",
					  "manifest source mismatch: bundle=%s/%s expected=%ux%u/%u active=%dx%d/%u",
					  b->lfd_basename, b->film_name, b->source_width, b->source_height,
					  b->source_pixel_aspect, source_w, source_h, source_pixel_aspect);
		mutable_bundle->source_mismatch_warned = true;
	}
	return false;
}

/* ---------------- manifest-presence query ----------------
 *
 * Lets out-of-tree tooling (filmview's actors table) flag actors
 * whose res_name has no manifest mapping. Two-pass match mirrors
 * find_actor: instance-specific entry wins, bare-name falls back. */
bool TieScene2dManifest_ActorMatches(const TieScene2dManifest* cs, const char* lfd_basename,
									 const char* film_name, const char* res_name, int16_t entry_index) {
	if (!cs || !lfd_basename || !film_name || !res_name)
		return false;
	TieScene2dFilmBundle* b =
		(TieScene2dFilmBundle*)TieScene2dManifest_FindBundle(cs, lfd_basename, film_name);
	if (!b || !b->complete)
		return false;
	return TieScene2dManifest_FindActor(b, res_name, entry_index) != NULL;
}

/* ------------------- atlas-rect editing API -------------------
 *
 * Shared resolver for the three editor entry points below. Returns the
 * TieScene2dAssetVariant for the atlas resolved at (lfd, film, res_name, entry,
 * cur_cel) — non-const because the editor mutates frames[] and the
 * save path passes the same pointer through. NULL when the bundle
 * resolves but the entry is missing, hidden, not ATLAS-kind, or its
 * sibling layout YAML failed to load at manifest open time.
 */
static TieScene2dAssetVariant* TieScene2dManifest_ResolveAtlasVariantMut(TieScene2dManifest* cs,
																		 const char* lfd_basename,
																		 const char* film_name,
																		 const char* res_name,
																		 int16_t entry_index, int cur_cel) {
	if (!cs || !lfd_basename || !film_name || !res_name)
		return NULL;
	TieScene2dFilmBundle* b =
		(TieScene2dFilmBundle*)TieScene2dManifest_FindBundle(cs, lfd_basename, film_name);
	if (!b || !b->complete)
		return NULL;
	TieScene2dActorEntry* e = (TieScene2dActorEntry*)TieScene2dManifest_FindActor(b, res_name, entry_index);
	if (!e || e->hide || e->kind != ASSET_KIND_ATLAS || e->variant_count == 0)
		return NULL;
	int vi = TieScene2dManifest_VariantAtCel(e->variants, e->variant_count, cur_cel);
	TieScene2dAssetVariant* av = &e->variants[vi];
	if (!av->atlas_loaded)
		return NULL;
	return av;
}

bool TieScene2dManifest_AtlasGet(const TieScene2dManifest* cs, const char* lfd_basename,
								 const char* film_name, const char* res_name, int16_t entry_index,
								 int cur_cel, int frame_idx, TieScene2dRect* out_rect, int* out_atlas_w,
								 int* out_atlas_h, int* out_frame_count, const char** out_yaml_path,
								 const char** out_asset_path) {
	TieScene2dAssetVariant* av = TieScene2dManifest_ResolveAtlasVariantMut(
		(TieScene2dManifest*)cs, lfd_basename, film_name, res_name, entry_index, cur_cel);
	if (!av)
		return false;
	if (frame_idx < 0 || frame_idx >= av->atlas.frame_count)
		return false;
	if (out_rect)
		*out_rect = av->atlas.frames[frame_idx];
	if (out_atlas_w)
		*out_atlas_w = av->atlas.atlas_w;
	if (out_atlas_h)
		*out_atlas_h = av->atlas.atlas_h;
	if (out_frame_count)
		*out_frame_count = av->atlas.frame_count;
	if (out_yaml_path)
		*out_yaml_path = av->yaml_path;
	if (out_asset_path)
		*out_asset_path = av->asset_path;
	return true;
}

bool TieScene2dManifest_AtlasSet(TieScene2dManifest* cs, const char* lfd_basename, const char* film_name,
								 const char* res_name, int16_t entry_index, int cur_cel, int frame_idx,
								 TieScene2dRect new_rect) {
	TieScene2dAssetVariant* av = TieScene2dManifest_ResolveAtlasVariantMut(cs, lfd_basename, film_name,
																		   res_name, entry_index, cur_cel);
	if (!av)
		return false;
	if (frame_idx < 0 || frame_idx >= av->atlas.frame_count)
		return false;
	av->atlas.frames[frame_idx] = new_rect;
	return true;
}

bool TieScene2dManifest_AtlasSave(TieScene2dManifest* cs, const char* lfd_basename, const char* film_name,
								  const char* res_name, int16_t entry_index, int cur_cel, char* err,
								  size_t errsz) {
	TieScene2dAssetVariant* av = TieScene2dManifest_ResolveAtlasVariantMut(cs, lfd_basename, film_name,
																		   res_name, entry_index, cur_cel);
	if (!av) {
		if (err && errsz)
			snprintf(err, errsz, "no atlas variant for %s in %s/%s", res_name, lfd_basename, film_name);
		return false;
	}
	return Aeron_SpriteAtlasSave(&av->atlas, av->yaml_path, err, errsz);
}

bool TieScene2dManifest_AtlasReload(TieScene2dManifest* cs, const char* lfd_basename, const char* film_name,
									const char* res_name, int16_t entry_index, int cur_cel) {
	TieScene2dAssetVariant* av = TieScene2dManifest_ResolveAtlasVariantMut(cs, lfd_basename, film_name,
																		   res_name, entry_index, cur_cel);
	if (!av || !av->yaml_path[0])
		return false;
	/* Drop the current parsed copy and re-parse from disk. On failure
	 * we leave atlas_loaded = false; the compose path skips the actor
	 * for this frame, surfacing the breakage rather than rendering
	 * stale data. */
	Aeron_SpriteAtlasFree(&av->atlas);
	av->atlas_loaded = Aeron_SpriteAtlasLoad(&av->atlas, av->yaml_path);
	return av->atlas_loaded;
}

bool TieScene2dManifest_SpriteGet(const TieScene2dManifest* cs, const char* lfd_basename,
								  const char* film_name, const char* res_name, int16_t entry_index,
								  int cur_cel, const char** out_asset_path) {
	if (!cs || !lfd_basename || !film_name || !res_name)
		return false;
	const TieScene2dFilmBundle* b = TieScene2dManifest_FindBundle(cs, lfd_basename, film_name);
	if (!b || !b->complete)
		return false;
	const TieScene2dActorEntry* e = TieScene2dManifest_FindActor(b, res_name, entry_index);
	if (!e || e->hide || e->kind != ASSET_KIND_SPRITE || e->variant_count == 0)
		return false;
	int vi = TieScene2dManifest_VariantAtCel(e->variants, e->variant_count, cur_cel);
	const TieScene2dAssetVariant* av = &e->variants[vi];
	if (!av->asset_path[0])
		return false;
	if (out_asset_path)
		*out_asset_path = av->asset_path;
	return true;
}
