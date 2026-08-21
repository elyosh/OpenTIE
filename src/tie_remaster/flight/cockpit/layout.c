/* cockpit_layout — implementation. */
#include "tie_remaster/flight/cockpit/layout.h"
#include "aeron/config_file.h"
#include "aeron/log.h"
#include "aeron/render.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tie_runtime/snapshot/snapshot.h" /* TIE_HUDI_* + TIE_MAX_HUD_INSTRUMENTS */

/* Parse a `{ w: <i>, h: <i> }` mapping into two int slots. */
static int TieCockpitLayout_NodeInt(const AeronConfigNode* map, const char* key) {
	return (int)AeronConfigNode_Int(AeronConfigNode_MapGet(map, key), 0);
}
static float TieCockpitLayout_NodeFloat(const AeronConfigNode* map, const char* key) {
	return (float)AeronConfigNode_Float(AeronConfigNode_MapGet(map, key), 0.0);
}

static void TieCockpitLayout_ParseWh(const AeronConfigNode* m, int* out_w, int* out_h) {
	*out_w = TieCockpitLayout_NodeInt(m, "w");
	*out_h = TieCockpitLayout_NodeInt(m, "h");
}

/* Parse a `{ x: <i>, y: <i>, w: <i>, h: <i> }` mapping. */
static void TieCockpitLayout_ParseXywh(const AeronConfigNode* m, int* x, int* y, int* w, int* h) {
	*x = TieCockpitLayout_NodeInt(m, "x");
	*y = TieCockpitLayout_NodeInt(m, "y");
	*w = TieCockpitLayout_NodeInt(m, "w");
	*h = TieCockpitLayout_NodeInt(m, "h");
}

/* Parse one cel entry: `{ size: {...}, atlas: {...} }`. */
static void TieCockpitLayout_ParseCel(const AeronConfigNode* m, TieCockpitLayoutCel* out) {
	TieCockpitLayout_ParseWh(AeronConfigNode_MapGet(m, "size"), &out->size_w, &out->size_h);
	TieCockpitLayout_ParseXywh(AeronConfigNode_MapGet(m, "atlas"), &out->atlas_x, &out->atlas_y,
							   &out->atlas_w, &out->atlas_h);
}

/* Symbol→id table for the `instruments:` block. Accepts both the full
 * "TIE_HUDI_X" name and the bare "X" suffix; either resolves to the
 * same integer index. Kept in sync with TieHudInstrumentId. */
typedef struct {
	const char* name;
	int id;
} TieCockpitLayoutHudIdEntry;
static const TieCockpitLayoutHudIdEntry k_hud_id_table[] = {
	{ "RADAR_LEFT", TIE_HUDI_RADAR_LEFT },
	{ "RADAR_RIGHT", TIE_HUDI_RADAR_RIGHT },
	{ "CMD_3D_CRT", TIE_HUDI_CMD_3D_CRT },
	{ "LASER_LED_FIRST", TIE_HUDI_LASER_LED_FIRST },
	{ "LASER_LED_LAST", TIE_HUDI_LASER_LED_LAST },
	{ "MISSILE_HP_FIRST", TIE_HUDI_MISSILE_HP_FIRST },
	{ "MISSILE_HP_LAST", TIE_HUDI_MISSILE_HP_LAST },
	{ "MISSILE_AMMO_FIRST", TIE_HUDI_MISSILE_AMMO_FIRST },
	{ "MISSILE_AMMO_LAST", TIE_HUDI_MISSILE_AMMO_LAST },
	{ "SHIELD_FWD_NORMAL", TIE_HUDI_SHIELD_FWD_NORMAL },
	{ "SHIELD_FWD_OVER", TIE_HUDI_SHIELD_FWD_OVER },
	{ "SHIELD_REAR_NORMAL", TIE_HUDI_SHIELD_REAR_NORMAL },
	{ "SHIELD_REAR_OVER", TIE_HUDI_SHIELD_REAR_OVER },
	{ "HULL_DAMAGE_LEVER", TIE_HUDI_HULL_DAMAGE_LEVER },
	{ "SPEED_DIGITS", TIE_HUDI_SPEED_DIGITS },
	{ "THROTTLE_DIGITS", TIE_HUDI_THROTTLE_DIGITS },
	{ "POWER_BALANCE", TIE_HUDI_POWER_BALANCE },
	{ "POWER_LASERS", TIE_HUDI_POWER_LASERS },
	{ "POWER_SHIELDS", TIE_HUDI_POWER_SHIELDS },
	{ "POWER_BEAM", TIE_HUDI_POWER_BEAM },
	{ "CLOCK_DIGITS", TIE_HUDI_CLOCK_DIGITS },
	{ "REC_LED", TIE_HUDI_REC_LED },
	{ "REC_PCT", TIE_HUDI_REC_PCT },
	{ "VIEW17_TITLE", TIE_HUDI_VIEW17_TITLE },
	{ "BEAM_ARC", TIE_HUDI_BEAM_ARC },
	{ "GUNSIGHT", TIE_HUDI_GUNSIGHT },
	{ "WEAPON_FIRE_FIRST", TIE_HUDI_WEAPON_FIRE_FIRST },
	{ "WEAPON_FIRE_LAST", TIE_HUDI_WEAPON_FIRE_LAST },
	{ "DAMAGE_CRACK_FIRST", TIE_HUDI_DAMAGE_CRACK_FIRST },
	{ "DAMAGE_CRACK_LAST", TIE_HUDI_DAMAGE_CRACK_LAST },
	{ "TARGET_SUBSYSTEM_PCT", TIE_HUDI_TARGET_SUBSYSTEM_PCT },
	{ "TARGET_DIST_KM_INT", TIE_HUDI_TARGET_DIST_KM_INT },
	{ "TARGET_DIST_KM_FRAC", TIE_HUDI_TARGET_DIST_KM_FRAC },
	{ "TARGET_SHIELD_PCT", TIE_HUDI_TARGET_SHIELD_PCT },
	{ "TARGET_HULL_PCT", TIE_HUDI_TARGET_HULL_PCT },
	{ "TARGET_CARGO", TIE_HUDI_TARGET_CARGO },
	{ "TARGET_SUBSYSTEM_FOCUS", TIE_HUDI_TARGET_SUBSYSTEM_FOCUS },
	{ "WARN_INCOMING", TIE_HUDI_WARN_INCOMING },
	{ "WARN_LOCK", TIE_HUDI_WARN_LOCK },
	{ "WARN_IMPACT", TIE_HUDI_WARN_IMPACT },
	{ "THREAT_DIST_KM_INT", TIE_HUDI_THREAT_DIST_KM_INT },
	{ "THREAT_DIST_KM_FRAC", TIE_HUDI_THREAT_DIST_KM_FRAC },
	{ "THREAT_ION", TIE_HUDI_THREAT_ION },
	{ "THREAT_TORP", TIE_HUDI_THREAT_TORP },
	{ "THREAT_MISSILE", TIE_HUDI_THREAT_MISSILE },
	{ "THREAT_BEAM", TIE_HUDI_THREAT_BEAM },
	{ "THREAT_SHIELD_PCT", TIE_HUDI_THREAT_SHIELD_PCT },
	{ "THREAT_HULL_PCT", TIE_HUDI_THREAT_HULL_PCT },
	{ "COVER_SHIELDS", TIE_HUDI_COVER_SHIELDS },
	{ "COVER_BEAM_UP", TIE_HUDI_COVER_BEAM_UP },
	{ "COVER_BEAM_DOWN", TIE_HUDI_COVER_BEAM_DOWN },
	{ "BEAM_FIRE", TIE_HUDI_BEAM_FIRE },
};

/* Resolve `id_str` to a HUD instrument index. Accepts an integer
 * literal in [0, TIE_MAX_HUD_INSTRUMENTS), or a symbolic name with or
 * without the "TIE_HUDI_" prefix. Returns -1 on failure. */
static int TieCockpitLayout_HudIdResolve(const char* s) {
	if (!s || !s[0])
		return -1;
	/* Integer literal. */
	if (s[0] >= '0' && s[0] <= '9') {
		int v = atoi(s);
		if (v >= 0 && v < TIE_MAX_HUD_INSTRUMENTS)
			return v;
		return -1;
	}
	const char* bare = s;
	if (!strncmp(bare, "TIE_HUDI_", 9))
		bare += 9;
	for (size_t i = 0; i < sizeof k_hud_id_table / sizeof k_hud_id_table[0]; ++i) {
		if (!strcmp(bare, k_hud_id_table[i].name))
			return k_hud_id_table[i].id;
	}
	return -1;
}

/* Parse one `instruments:` entry. Accepts:
 *   { id, x, y }                              — basic anchor
 *   { id: RADAR_LEFT, x, y, radius }          — radar disc radius
 *   { id: POWER_LASERS, x, y, stride_y }      — vertical cluster stride
 *   { id: LASER_LED_FIRST, x, y, stride_x }   — horizontal cluster stride
 *   { id: BEAM_ARC, x, y, stride_x, stride_y } — both axes (curved arc)
 *   { id: TARGET_CARGO, x, y, right_at }      — right-aligned text column
 *   { id: 90, x, y, center_at }               — centered text column
 *   { id: 79, x, y, label_at }                — threat label X position */
static void TieCockpitLayout_ParseInstrument(const AeronConfigNode* m, TieCockpitLayout* layout) {
	const AeronConfigNode* id_node = AeronConfigNode_MapGet(m, "id");
	const AeronConfigNode* x_node = AeronConfigNode_MapGet(m, "x");
	const AeronConfigNode* y_node = AeronConfigNode_MapGet(m, "y");
	char id_text[16];
	const char* id_name = AeronConfigNode_String(id_node, NULL);
	int id;

	if (!id_name && AeronConfigNode_Type(id_node) == AERON_CONFIG_INT) {
		snprintf(id_text, sizeof id_text, "%lld", (long long)AeronConfigNode_Int(id_node, -1));
		id_name = id_text;
	}
	id = TieCockpitLayout_HudIdResolve(id_name);
	if (id < 0 || id >= COCKPIT_LAYOUT_MAX_INSTRUMENTS)
		return;
	if (!x_node && !y_node)
		return;
	layout->instruments[id].x = (int16_t)AeronConfigNode_Int(x_node, 0);
	layout->instruments[id].y = (int16_t)AeronConfigNode_Int(y_node, 0);
	layout->instruments[id].radius = (int16_t)TieCockpitLayout_NodeInt(m, "radius");
	layout->instruments[id].stride_x = (int16_t)TieCockpitLayout_NodeInt(m, "stride_x");
	layout->instruments[id].stride_y = (int16_t)TieCockpitLayout_NodeInt(m, "stride_y");
	layout->instruments[id].right_at = (int16_t)TieCockpitLayout_NodeInt(m, "right_at");
	layout->instruments[id].center_at = (int16_t)TieCockpitLayout_NodeInt(m, "center_at");
	layout->instruments[id].label_at = (int16_t)TieCockpitLayout_NodeInt(m, "label_at");
	layout->instruments[id].hdr_boost = TieCockpitLayout_NodeFloat(m, "hdr_boost");
	layout->instruments[id].present = 1;
}

/* Parse the `font:` map. Today only `atlas_scale` is consumed. */
static void TieCockpitLayout_ParseFont(const AeronConfigNode* m, TieCockpitLayout* layout) {
	layout->font_atlas_scale = TieCockpitLayout_NodeFloat(m, "atlas_scale");
}

/* Walk an HD layout (reference frame != classic 4:3) and log one
 * warning per missing field its drawers would otherwise have to fake
 * via the per-axis ref/classic linear fallback. The fallback is rarely
 * what the artist wants in a hand-authored cockpit. Skipped entirely
 * for 4:3 layouts (where fallback IS identity and there's nothing to
 * warn about). */
static void TieCockpitLayout_ValidateHdLayout(const TieCockpitLayout* l, const char* yaml_path) {
	if (!l || l->reference_w <= 0 || l->reference_h <= 0)
		return;
	/* Skip 4:3 layouts (extractor's classic output: 640×480 / 320×200). */
	if ((l->reference_w == 640 && l->reference_h == 480) || (l->reference_w == 320 && l->reference_h == 200))
		return;

	if (!l->pip_present)
		Aeron_LogWarn("tie.assets", "%s: HD cockpit layout has no pip_rect override", yaml_path);

	/* Per-instrument requirements. Each pair is (id, "field name"). The
	 * runtime only reads `radius` when the radar entry is present,
	 * `stride_*` when the cluster entry is present, etc. — so check
	 * each only when the instrument itself has an authored anchor. */
	struct {
		int id;
		const char* name;
		int want_radius;
		int want_stride_x;
		int want_stride_y;
		int want_right_at;
		int want_center_at;
		int want_label_at;
	} reqs[] = {
		/* Radars: per-disc radius for blip + bracket positioning. */
		{ 0, "RADAR_LEFT", 1, 0, 0, 0, 0, 0 },
		{ 1, "RADAR_RIGHT", 1, 0, 0, 0, 0, 0 },
		/* Laser-LED row: stride_x for the 10-LED stack. */
		{ 3, "LASER_LED[0]", 0, 1, 0, 0, 0, 0 },
		{ 4, "LASER_LED[1]", 0, 1, 0, 0, 0, 0 },
		{ 5, "LASER_LED[2]", 0, 1, 0, 0, 0, 0 },
		{ 6, "LASER_LED[3]", 0, 1, 0, 0, 0, 0 },
		{ 7, "LASER_LED[4]", 0, 1, 0, 0, 0, 0 },
		{ 8, "LASER_LED[5]", 0, 1, 0, 0, 0, 0 },
		{ 9, "LASER_LED[6]", 0, 1, 0, 0, 0, 0 },
		{ 10, "LASER_LED[7]", 0, 1, 0, 0, 0, 0 },
		/* Power sliders: stride_y for the 12-rung vertical stack. */
		{ 26, "POWER_BALANCE", 0, 0, 1, 0, 0, 0 },
		{ 27, "POWER_LASERS", 0, 0, 1, 0, 0, 0 },
		{ 28, "POWER_SHIELDS", 0, 0, 1, 0, 0, 0 },
		{ 29, "POWER_BEAM", 0, 0, 1, 0, 0, 0 },
		/* Beam arc: stride on both axes. */
		{ 35, "BEAM_ARC", 0, 1, 1, 0, 0, 0 },
		/* CMD readout text: right-aligned column for cargo + subsystem. */
		{ 63, "TARGET_CARGO", 0, 0, 0, 1, 0, 0 },
		{ 65, "TARGET_SUBSYSTEM", 0, 0, 0, 1, 0, 0 },
		/* CMD target name: centered. */
		{ 90, "CMD_TARGET_NAME", 0, 0, 0, 0, 1, 0 },
		/* Threat readout target name: centered. */
		{ 69, "THREAT_NAME", 0, 0, 0, 0, 1, 0 },
		/* Threat readout label/value rows: label column X. */
		{ 79, "THREAT_ORDER", 0, 0, 0, 0, 0, 1 },
		{ 80, "THREAT_LINK_TGT", 0, 0, 0, 0, 0, 1 },
		{ 81, "THREAT_LINK_DIST", 0, 0, 0, 0, 0, 1 },
		{ 82, "THREAT_ETA", 0, 0, 0, 0, 0, 1 },
	};
	const int n = (int)(sizeof reqs / sizeof reqs[0]);
	for (int i = 0; i < n; ++i) {
		const TieCockpitLayoutInstrumentAnchor* a = &l->instruments[reqs[i].id];
		if (!a->present)
			continue; /* not used on this craft */
		if (reqs[i].want_radius && a->radius == 0)
			Aeron_LogWarn("tie.assets", "%s: %s has no radius", yaml_path, reqs[i].name);
		if (reqs[i].want_stride_x && a->stride_x == 0)
			Aeron_LogWarn("tie.assets", "%s: %s has no stride_x", yaml_path, reqs[i].name);
		if (reqs[i].want_stride_y && a->stride_y == 0)
			Aeron_LogWarn("tie.assets", "%s: %s has no stride_y", yaml_path, reqs[i].name);
		if (reqs[i].want_right_at && a->right_at == 0)
			Aeron_LogWarn("tie.assets", "%s: %s has no right_at", yaml_path, reqs[i].name);
		if (reqs[i].want_center_at && a->center_at == 0)
			Aeron_LogWarn("tie.assets", "%s: %s has no center_at", yaml_path, reqs[i].name);
		if (reqs[i].want_label_at && a->label_at == 0)
			Aeron_LogWarn("tie.assets", "%s: %s has no label_at", yaml_path, reqs[i].name);
	}
}

bool TieCockpitLayout_Load(TieCockpitLayout* out, AeronVfs* vfs, AeronVfsRoot root, const char* yaml_path) {
	AeronConfigFile* document = NULL;
	AeronConfigError error = { 0 };
	const AeronConfigNode* root_node;
	const AeronConfigNode* node;
	size_t index;

	if (!out || !yaml_path)
		return false;
	memset(out, 0, sizeof *out);

	if (!AeronConfigFile_LoadYamlEx(vfs, root, yaml_path, &document, &error))
		return false;
	root_node = AeronConfigFile_Root(document);
	if (AeronConfigNode_Type(root_node) != AERON_CONFIG_MAP) {
		Aeron_LogWarn("tie.assets", "%s: cockpit root is not a mapping", yaml_path);
		goto fail;
	}

	node = AeronConfigNode_MapGet(root_node, "reference");
	if (AeronConfigNode_Type(node) == AERON_CONFIG_MAP)
		TieCockpitLayout_ParseWh(node, &out->reference_w, &out->reference_h);
	node = AeronConfigNode_MapGet(root_node, "atlas_size");
	if (AeronConfigNode_Type(node) == AERON_CONFIG_MAP)
		TieCockpitLayout_ParseWh(node, &out->atlas_w, &out->atlas_h);
	node = AeronConfigNode_MapGet(root_node, "pip_rect");
	if (AeronConfigNode_Type(node) == AERON_CONFIG_MAP) {
		int x = 0, y = 0, w = 0, h = 0;
		TieCockpitLayout_ParseXywh(node, &x, &y, &w, &h);
		if (w > 0 && h > 0) {
			out->pip_x = (int16_t)x;
			out->pip_y = (int16_t)y;
			out->pip_w = (int16_t)w;
			out->pip_h = (int16_t)h;
			out->pip_present = 1;
		}
	}
	node = AeronConfigNode_MapGet(root_node, "font");
	if (AeronConfigNode_Type(node) == AERON_CONFIG_MAP)
		TieCockpitLayout_ParseFont(node, out);
	node = AeronConfigNode_MapGet(root_node, "instruments");
	if (AeronConfigNode_Type(node) == AERON_CONFIG_SEQUENCE) {
		for (index = 0; index < AeronConfigNode_SequenceCount(node); ++index) {
			const AeronConfigNode* entry = AeronConfigNode_SequenceGet(node, index);
			if (AeronConfigNode_Type(entry) == AERON_CONFIG_MAP)
				TieCockpitLayout_ParseInstrument(entry, out);
		}
	}
	node = AeronConfigNode_MapGet(root_node, "cels");
	if (AeronConfigNode_Type(node) == AERON_CONFIG_SEQUENCE) {
		int n = (int)AeronConfigNode_SequenceCount(node);
		if (n > 0)
			out->cels = (TieCockpitLayoutCel*)calloc((size_t)n, sizeof(TieCockpitLayoutCel));
		if (n > 0 && !out->cels) {
			Aeron_LogError("tie.cockpit", "%s: cannot allocate %d cels", yaml_path, n);
			Aeron_RequestFatalRendererError("cockpit layout allocation");
			goto fail;
		}
		for (index = 0; index < (size_t)n; ++index) {
			const AeronConfigNode* cel = AeronConfigNode_SequenceGet(node, index);
			if (AeronConfigNode_Type(cel) == AERON_CONFIG_MAP)
				TieCockpitLayout_ParseCel(cel, &out->cels[index]);
		}
		out->cel_count = n;
	}

	AeronConfigFile_Destroy(document);

	if (out->cel_count == 0) {
		Aeron_LogWarn("tie.assets", "%s: cockpit layout contains no cels", yaml_path);
		TieCockpitLayout_Free(out);
		return false;
	}
	TieCockpitLayout_ValidateHdLayout(out, yaml_path);
	return true;

fail:
	AeronConfigFile_Destroy(document);
	TieCockpitLayout_Free(out);
	return false;
}

void TieCockpitLayout_Free(TieCockpitLayout* l) {
	if (!l)
		return;
	free(l->cels);
	memset(l, 0, sizeof *l);
}

const TieCockpitLayoutCel* TieCockpitLayout_Cel(const TieCockpitLayout* l, int idx) {
	if (!l || idx < 0 || idx >= l->cel_count)
		return NULL;
	const TieCockpitLayoutCel* c = &l->cels[idx];
	/* Skip empty/missing cels (extractor emits these for shapes that
	 * measured to 0×0). */
	if (c->atlas_w <= 0 || c->atlas_h <= 0)
		return NULL;
	return c;
}

void TieCockpitLayout_Anchor(const TieCockpitLayout* l, int id, int16_t snap_x, int16_t snap_y, int classic_w,
							 int classic_h, float* out_x, float* out_y) {
	/* No layout, no rescale frame, or out-of-range id → return the
	 * snapshot value verbatim. Matches today's behaviour. */
	if (!l || l->reference_w <= 0 || l->reference_h <= 0 || classic_w <= 0 || classic_h <= 0) {
		if (out_x)
			*out_x = (float)snap_x;
		if (out_y)
			*out_y = (float)snap_y;
		return;
	}

	/* Authored anchor wins. */
	if (id >= 0 && id < COCKPIT_LAYOUT_MAX_INSTRUMENTS && l->instruments[id].present) {
		if (out_x)
			*out_x = (float)l->instruments[id].x;
		if (out_y)
			*out_y = (float)l->instruments[id].y;
		return;
	}

	/* No authored anchor → linear rescale (identity for 4:3 layouts).
	 * For hand-authored cockpits whose layout differs non-uniformly
	 * from the engine's 4:3, callers should provide an authored
	 * anchor for every instrument they care about. */
	float sx, sy;
	TieCockpitLayout_ClassicPixelSize(l, classic_w, classic_h, &sx, &sy);
	if (out_x)
		*out_x = (float)snap_x * sx;
	if (out_y)
		*out_y = (float)snap_y * sy;
}

float TieCockpitLayout_HdrBoost(const TieCockpitLayout* l, int id) {
	if (!l || id < 0 || id >= COCKPIT_LAYOUT_MAX_INSTRUMENTS || !l->instruments[id].present)
		return 1.0f;
	const float b = l->instruments[id].hdr_boost;
	return (b > 0.0f) ? b : 1.0f;
}

void TieCockpitLayout_ClassicPixelSize(const TieCockpitLayout* l, int classic_w, int classic_h, float* out_sx,
									   float* out_sy) {
	if (!l || l->reference_w <= 0 || l->reference_h <= 0 || classic_w <= 0 || classic_h <= 0) {
		if (out_sx)
			*out_sx = 1.0f;
		if (out_sy)
			*out_sy = 1.0f;
		return;
	}
	if (out_sx)
		*out_sx = (float)l->reference_w / (float)classic_w;
	if (out_sy)
		*out_sy = (float)l->reference_h / (float)classic_h;
}

void TieCockpitLayout_CoordFrame(const TieCockpitLayout* l, int classic_w, int classic_h, int* out_w,
								 int* out_h) {
	int w = classic_w, h = classic_h;
	if (l && l->reference_w > 0 && l->reference_h > 0) {
		w = l->reference_w;
		h = l->reference_h;
	}
	if (out_w)
		*out_w = w;
	if (out_h)
		*out_h = h;
}

int TieCockpitLayout_RadarRadius(const TieCockpitLayout* l, int id) {
	if (!l || id < 0 || id >= COCKPIT_LAYOUT_MAX_INSTRUMENTS)
		return 0;
	if (!l->instruments[id].present)
		return 0;
	return (int)l->instruments[id].radius;
}

void TieCockpitLayout_Stride(const TieCockpitLayout* l, int id, int16_t* out_sx, int16_t* out_sy) {
	int16_t sx = 0, sy = 0;
	if (l && id >= 0 && id < COCKPIT_LAYOUT_MAX_INSTRUMENTS && l->instruments[id].present) {
		sx = l->instruments[id].stride_x;
		sy = l->instruments[id].stride_y;
	}
	if (out_sx)
		*out_sx = sx;
	if (out_sy)
		*out_sy = sy;
}
