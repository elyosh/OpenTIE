#include "tie_runtime/flight_assets/assets.h"

#include "aeron/config_file.h"
#include "tie_runtime/audio/config.h"
#include "tie_runtime/diagnostics/diagnostics.h"
#include "tie_runtime/display/classic_display.h"
#include "tie_runtime/display/classic_framebuffer.h"
#include "tie_runtime/flight_assets/model_types.h"
#include "tie_runtime/input/input.h"
#include "tie_runtime/runtime/exports.h"
#include "tie_runtime/runtime/profile.h"
#include "tie_runtime/storage/storage.h"

#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TIE_FLIGHT_MODEL_MAX 161
#define TIE_FLIGHT_BILLBOARD_MAX 161
#define TIE_FLIGHT_COCKPIT_VIEW_MAX 96
#define TIE_FLIGHT_COCKPIT_PARTS_MAX 32
#define TIE_FLIGHT_CRT_MASK_MAX 32

struct TieFlightAssetBundle {
	TieFlightAssetCatalogKind kind;
	TieFlightAssetEntry models[TIE_FLIGHT_MODEL_MAX];
	int model_count;
	TieFlightAssetBillboard billboards[TIE_FLIGHT_BILLBOARD_MAX];
	int billboard_count;
	TieFlightAssetCockpitView cockpit_views[TIE_FLIGHT_COCKPIT_VIEW_MAX];
	int cockpit_view_count;
	TieFlightAssetCockpitParts cockpit_parts[TIE_FLIGHT_COCKPIT_PARTS_MAX];
	int cockpit_parts_count;
	TieFlightAssetCrtMask cockpit_masks[TIE_FLIGHT_CRT_MASK_MAX];
	int cockpit_mask_count;
	char skybox[TIE_FLIGHT_ASSET_PATH_MAX];
	char cockpit_micro[TIE_FLIGHT_ASSET_PATH_MAX];
	char cockpit_tiny[TIE_FLIGHT_ASSET_PATH_MAX];
	char content_prefix[32];
	int presentation_aspect_width;
	int presentation_aspect_height;
};

static bool TieAssets_CatalogError(char* error, size_t capacity, const AeronConfigNode* node,
								   const char* yaml_path, const char* format, ...) {
	if (error && capacity) {
		char detail[384];
		va_list arguments;
		va_start(arguments, format);
		vsnprintf(detail, sizeof detail, format, arguments);
		va_end(arguments);
		snprintf(error, capacity, "%s:%d: %s: %s",
				 node && AeronConfigNode_SourcePath(node) ? AeronConfigNode_SourcePath(node)
														  : "flight catalog",
				 AeronConfigNode_Line(node), yaml_path, detail);
	}
	return false;
}

static int TieAssets_AspectGcd(int a, int b) {
	while (b != 0) {
		const int remainder = a % b;
		a = b;
		b = remainder;
	}
	return a;
}

static bool TieAssets_CopyPath(char destination[TIE_FLIGHT_ASSET_PATH_MAX], const AeronConfigNode* node,
							   const char* yaml_path, char* error, size_t capacity) {
	const char* path = AeronConfigNode_String(node, NULL);
	if (!path || !path[0])
		return TieAssets_CatalogError(error, capacity, node, yaml_path, "expected a non-empty path");
	if (path[0] == '/' || strstr(path, "../") || strstr(path, "/..") ||
		strlen(path) >= TIE_FLIGHT_ASSET_PATH_MAX)
		return TieAssets_CatalogError(error, capacity, node, yaml_path,
									  "path is absolute, escaping, or too long");
	snprintf(destination, TIE_FLIGHT_ASSET_PATH_MAX, "%s", path);
	return true;
}

static bool TieAssets_KeyAllowed(const char* key, const char* const* allowed, size_t allowed_count) {
	for (size_t index = 0; index < allowed_count; ++index)
		if (strcmp(key, allowed[index]) == 0)
			return true;
	return false;
}

static bool TieAssets_ValidateMap(const AeronConfigNode* map, const char* yaml_path,
								  const char* const* allowed, size_t allowed_count, char* error,
								  size_t capacity) {
	if (AeronConfigNode_Type(map) != AERON_CONFIG_MAP)
		return TieAssets_CatalogError(error, capacity, map, yaml_path, "expected a mapping");
	const size_t count = AeronConfigNode_MapCount(map);
	for (size_t index = 0; index < count; ++index) {
		const char* key = AeronConfigNode_MapKeyAt(map, index);
		if (!TieAssets_KeyAllowed(key, allowed, allowed_count))
			return TieAssets_CatalogError(error, capacity, AeronConfigNode_MapValueAt(map, index), yaml_path,
										  "unknown key '%s'", key);
		for (size_t preceding = 0; preceding < index; ++preceding) {
			if (strcmp(key, AeronConfigNode_MapKeyAt(map, preceding)) == 0)
				return TieAssets_CatalogError(error, capacity, AeronConfigNode_MapValueAt(map, index),
											  yaml_path, "duplicate key '%s'", key);
		}
	}
	return true;
}

static int TieAssets_ParseSpecies(const AeronConfigNode* node) {
	if (AeronConfigNode_Type(node) == AERON_CONFIG_INT) {
		int64_t index = AeronConfigNode_Int(node, -1);
		return index >= 0 && index < TIE_SPECIES_COUNT ? (int)index : -1;
	}
	const char* name = AeronConfigNode_String(node, NULL);
	return name ? TieRecoveredData_SpeciesLookup(name) : -1;
}

static bool TieAssets_ParseModels(TieFlightAssetBundle* bundle, const AeronConfigNode* models, char* error,
								  size_t capacity) {
	if (AeronConfigNode_Type(models) != AERON_CONFIG_SEQUENCE)
		return TieAssets_CatalogError(error, capacity, models, "models", "expected a sequence");
	const size_t count = AeronConfigNode_SequenceCount(models);
	if (count > TIE_FLIGHT_MODEL_MAX)
		return TieAssets_CatalogError(error, capacity, models, "models", "too many model rows");
	const char* const model_keys[] = { "species", "path" };
	bool seen[TIE_SPECIES_COUNT] = { false };
	for (size_t index = 0; index < count; ++index) {
		const AeronConfigNode* row = AeronConfigNode_SequenceGet(models, index);
		char yaml_path[64];
		snprintf(yaml_path, sizeof yaml_path, "models[%zu]", index);
		if (!TieAssets_ValidateMap(row, yaml_path, model_keys, sizeof model_keys / sizeof model_keys[0],
								   error, capacity))
			return false;
		const int species = TieAssets_ParseSpecies(AeronConfigNode_MapGet(row, "species"));
		if (species < 0)
			return TieAssets_CatalogError(error, capacity, row, yaml_path, "invalid species");
		if (seen[species])
			return TieAssets_CatalogError(error, capacity, row, yaml_path, "duplicate species %d", species);
		seen[species] = true;
		TieFlightAssetEntry* entry = &bundle->models[bundle->model_count++];
		entry->species_idx = (uint16_t)species;
		if (!TieAssets_CopyPath(entry->path, AeronConfigNode_MapGet(row, "path"), yaml_path, error, capacity))
			return false;
	}
	return true;
}

static bool TieAssets_ParseSpriteRows(TieFlightAssetBundle* bundle, const AeronConfigNode* rows,
									  const char* name, bool seen[TIE_SPECIES_COUNT], char* error,
									  size_t capacity) {
	if (AeronConfigNode_Type(rows) != AERON_CONFIG_SEQUENCE)
		return TieAssets_CatalogError(error, capacity, rows, name, "expected a sequence");
	const char* const keys[] = { "species", "atlas", "layout" };
	for (size_t index = 0; index < AeronConfigNode_SequenceCount(rows); ++index) {
		const AeronConfigNode* row = AeronConfigNode_SequenceGet(rows, index);
		char yaml_path[64];
		snprintf(yaml_path, sizeof yaml_path, "%s[%zu]", name, index);
		if (bundle->billboard_count >= TIE_FLIGHT_BILLBOARD_MAX ||
			!TieAssets_ValidateMap(row, yaml_path, keys, sizeof keys / sizeof keys[0], error, capacity))
			return false;
		const int species = TieAssets_ParseSpecies(AeronConfigNode_MapGet(row, "species"));
		if (species < 0 || seen[species])
			return TieAssets_CatalogError(error, capacity, row, yaml_path,
										  species < 0 ? "invalid species" : "duplicate species");
		seen[species] = true;
		TieFlightAssetBillboard* entry = &bundle->billboards[bundle->billboard_count++];
		entry->species_idx = (uint16_t)species;
		if (!TieAssets_CopyPath(entry->atlas_path, AeronConfigNode_MapGet(row, "atlas"), yaml_path, error,
								capacity) ||
			!TieAssets_CopyPath(entry->layout_path, AeronConfigNode_MapGet(row, "layout"), yaml_path, error,
								capacity))
			return false;
	}
	return true;
}

static bool TieAssets_ParseCockpits(TieFlightAssetBundle* bundle, const AeronConfigNode* root, char* error,
									size_t capacity) {
	const char* const view_keys[] = { "view", "bitmap_hd", "bitmap_4_3", "damage" };
	const AeronConfigNode* views = AeronConfigNode_MapGet(root, "cockpit_views");
	if (AeronConfigNode_Type(views) != AERON_CONFIG_SEQUENCE)
		return TieAssets_CatalogError(error, capacity, views, "cockpit_views", "expected a sequence");
	for (size_t index = 0; index < AeronConfigNode_SequenceCount(views); ++index) {
		const AeronConfigNode* row = AeronConfigNode_SequenceGet(views, index);
		char path[64];
		snprintf(path, sizeof path, "cockpit_views[%zu]", index);
		if (bundle->cockpit_view_count >= TIE_FLIGHT_COCKPIT_VIEW_MAX ||
			!TieAssets_ValidateMap(row, path, view_keys, sizeof view_keys / sizeof view_keys[0], error,
								   capacity))
			return false;
		const char* name = AeronConfigNode_String(AeronConfigNode_MapGet(row, "view"), NULL);
		if (!name || !name[0] || strlen(name) >= sizeof bundle->cockpit_views[0].view_name)
			return TieAssets_CatalogError(error, capacity, row, path, "invalid view name");
		for (int preceding = 0; preceding < bundle->cockpit_view_count; ++preceding)
			if (strcmp(name, bundle->cockpit_views[preceding].view_name) == 0)
				return TieAssets_CatalogError(error, capacity, row, path, "duplicate view name");
		TieFlightAssetCockpitView* view = &bundle->cockpit_views[bundle->cockpit_view_count++];
		snprintf(view->view_name, sizeof view->view_name, "%s", name);
		if (!TieAssets_CopyPath(view->bitmap_hd, AeronConfigNode_MapGet(row, "bitmap_hd"), path, error,
								capacity) ||
			!TieAssets_CopyPath(view->bitmap_4_3, AeronConfigNode_MapGet(row, "bitmap_4_3"), path, error,
								capacity))
			return false;
		const AeronConfigNode* damage = AeronConfigNode_MapGet(row, "damage");
		if (damage && !TieAssets_CopyPath(view->damage, damage, path, error, capacity))
			return false;
	}

	const char* const part_keys[] = { "parts", "atlas", "layout" };
	const AeronConfigNode* parts = AeronConfigNode_MapGet(root, "cockpit_parts");
	if (AeronConfigNode_Type(parts) != AERON_CONFIG_SEQUENCE)
		return TieAssets_CatalogError(error, capacity, parts, "cockpit_parts", "expected a sequence");
	for (size_t index = 0; index < AeronConfigNode_SequenceCount(parts); ++index) {
		const AeronConfigNode* row = AeronConfigNode_SequenceGet(parts, index);
		char path[64];
		snprintf(path, sizeof path, "cockpit_parts[%zu]", index);
		if (bundle->cockpit_parts_count >= TIE_FLIGHT_COCKPIT_PARTS_MAX ||
			!TieAssets_ValidateMap(row, path, part_keys, sizeof part_keys / sizeof part_keys[0], error,
								   capacity))
			return false;
		const char* name = AeronConfigNode_String(AeronConfigNode_MapGet(row, "parts"), NULL);
		if (!name || !name[0] || strlen(name) >= sizeof bundle->cockpit_parts[0].parts_name)
			return TieAssets_CatalogError(error, capacity, row, path, "invalid parts name");
		for (int preceding = 0; preceding < bundle->cockpit_parts_count; ++preceding)
			if (strcmp(name, bundle->cockpit_parts[preceding].parts_name) == 0)
				return TieAssets_CatalogError(error, capacity, row, path, "duplicate parts name");
		TieFlightAssetCockpitParts* entry = &bundle->cockpit_parts[bundle->cockpit_parts_count++];
		snprintf(entry->parts_name, sizeof entry->parts_name, "%s", name);
		if (!TieAssets_CopyPath(entry->atlas, AeronConfigNode_MapGet(row, "atlas"), path, error, capacity) ||
			!TieAssets_CopyPath(entry->layout, AeronConfigNode_MapGet(row, "layout"), path, error, capacity))
			return false;
	}

	const char* const mask_keys[] = { "variant", "classic_w", "path" };
	const AeronConfigNode* masks = AeronConfigNode_MapGet(root, "cockpit_masks");
	if (AeronConfigNode_Type(masks) != AERON_CONFIG_SEQUENCE)
		return TieAssets_CatalogError(error, capacity, masks, "cockpit_masks", "expected a sequence");
	for (size_t index = 0; index < AeronConfigNode_SequenceCount(masks); ++index) {
		const AeronConfigNode* row = AeronConfigNode_SequenceGet(masks, index);
		char path[64];
		snprintf(path, sizeof path, "cockpit_masks[%zu]", index);
		if (bundle->cockpit_mask_count >= TIE_FLIGHT_CRT_MASK_MAX ||
			!TieAssets_ValidateMap(row, path, mask_keys, sizeof mask_keys / sizeof mask_keys[0], error,
								   capacity))
			return false;
		const int64_t variant = AeronConfigNode_Int(AeronConfigNode_MapGet(row, "variant"), -1);
		const int64_t width = AeronConfigNode_Int(AeronConfigNode_MapGet(row, "classic_w"), -1);
		if (variant < 0 || variant > UINT8_MAX || (width != 320 && width != 640))
			return TieAssets_CatalogError(error, capacity, row, path, "invalid mask key");
		for (int preceding = 0; preceding < bundle->cockpit_mask_count; ++preceding) {
			const TieFlightAssetCrtMask* prior = &bundle->cockpit_masks[preceding];
			if (prior->variant == variant && prior->classic_w == width)
				return TieAssets_CatalogError(error, capacity, row, path, "duplicate mask key");
		}
		TieFlightAssetCrtMask* mask = &bundle->cockpit_masks[bundle->cockpit_mask_count++];
		mask->variant = (uint8_t)variant;
		mask->classic_w = (uint16_t)width;
		if (!TieAssets_CopyPath(mask->path, AeronConfigNode_MapGet(row, "path"), path, error, capacity))
			return false;
	}
	return true;
}

static bool TieAssets_AssetExists(AeronVfs* vfs, const TieFlightAssetBundle* bundle, const char* relative,
								  const char* label, char* error, size_t capacity) {
	char path[TIE_FLIGHT_ASSET_PATH_MAX + 32];
	int length = snprintf(path, sizeof path, "%s/%s", bundle->content_prefix, relative);
	if (length < 0 || (size_t)length >= sizeof path || !AeronVfs_Exists(vfs, AERON_VFS_ROOT_ASSET, path)) {
		if (error && capacity)
			snprintf(error, capacity, "%s is missing required ASSET/%s", label, path);
		return false;
	}
	return true;
}

static bool TieAssets_ValidateStaticAssets(AeronVfs* vfs, const TieFlightAssetBundle* bundle, char* error,
										   size_t capacity) {
	if (!TieAssets_AssetExists(vfs, bundle, bundle->skybox, "skybox", error, capacity))
		return false;
	/* Billboard files are resolved per species so an absent remastered
	 * sprite can fall back to its original XACT source. */
	for (int index = 0; index < bundle->cockpit_view_count; ++index) {
		const TieFlightAssetCockpitView* entry = &bundle->cockpit_views[index];
		if (!TieAssets_AssetExists(vfs, bundle, entry->bitmap_hd, "cockpit view", error, capacity) ||
			!TieAssets_AssetExists(vfs, bundle, entry->bitmap_4_3, "cockpit view", error, capacity) ||
			(entry->damage[0] &&
			 !TieAssets_AssetExists(vfs, bundle, entry->damage, "cockpit damage", error, capacity)))
			return false;
	}
	for (int index = 0; index < bundle->cockpit_parts_count; ++index) {
		const TieFlightAssetCockpitParts* entry = &bundle->cockpit_parts[index];
		if (!TieAssets_AssetExists(vfs, bundle, entry->atlas, "cockpit parts", error, capacity) ||
			!TieAssets_AssetExists(vfs, bundle, entry->layout, "cockpit layout", error, capacity))
			return false;
	}
	for (int index = 0; index < bundle->cockpit_mask_count; ++index)
		if (!TieAssets_AssetExists(vfs, bundle, bundle->cockpit_masks[index].path, "cockpit mask", error,
								   capacity))
			return false;
	char font_file[TIE_FLIGHT_ASSET_PATH_MAX];
	const char* fonts[] = { bundle->cockpit_micro, bundle->cockpit_tiny };
	for (int font = 0; font < 2; ++font) {
		for (int suffix = 0; suffix < 2; ++suffix) {
			const char* extension = suffix ? ".fnt" : ".png";
			int length = snprintf(font_file, sizeof font_file, "%s%s", fonts[font], extension);
			if (length < 0 || (size_t)length >= sizeof font_file ||
				!TieAssets_AssetExists(vfs, bundle, font_file, "cockpit font", error, capacity))
				return false;
		}
	}
	return true;
}

static bool TieAssets_ParsePresentation(TieFlightAssetBundle* bundle, const AeronConfigNode* root,
										char* error, size_t capacity) {
	const AeronConfigNode* presentation = AeronConfigNode_MapGet(root, "presentation");
	const AeronConfigNode* aspect = AeronConfigNode_MapGet(presentation, "aspect_ratio");
	const char* const presentation_keys[] = { "aspect_ratio" };
	const char* const aspect_keys[] = { "width", "height" };
	if (!TieAssets_ValidateMap(presentation, "presentation", presentation_keys, 1, error, capacity) ||
		!TieAssets_ValidateMap(aspect, "presentation.aspect_ratio", aspect_keys, 2, error, capacity)) {
		return false;
	}
	const AeronConfigNode* width_node = AeronConfigNode_MapGet(aspect, "width");
	const AeronConfigNode* height_node = AeronConfigNode_MapGet(aspect, "height");
	const int64_t width = AeronConfigNode_Int(width_node, 0);
	const int64_t height = AeronConfigNode_Int(height_node, 0);
	if (AeronConfigNode_Type(width_node) != AERON_CONFIG_INT ||
		AeronConfigNode_Type(height_node) != AERON_CONFIG_INT || width <= 0 || height <= 0 ||
		width > INT_MAX || height > INT_MAX) {
		return TieAssets_CatalogError(error, capacity, aspect, "presentation.aspect_ratio",
									  "width and height must be positive integers");
	}
	const int divisor = TieAssets_AspectGcd((int)width, (int)height);
	bundle->presentation_aspect_width = (int)width / divisor;
	bundle->presentation_aspect_height = (int)height / divisor;
	return true;
}

TieFlightAssetBundle* TieFlightAssets_Open(AeronVfs* vfs, TieFlightAssetCatalogKind kind, char* error,
										   size_t error_capacity) {
	if (error && error_capacity)
		error[0] = '\0';
	const AeronVfsRoot root =
		kind == TIE_FLIGHT_ASSET_CATALOG_REMASTER ? AERON_VFS_ROOT_ASSET : AERON_VFS_ROOT_RESOURCE;
	const char* path = kind == TIE_FLIGHT_ASSET_CATALOG_REMASTER ? "tie_remaster/flight/assets.yaml"
																 : "flight/tie98-models.yaml";
	AeronConfigFile* document = NULL;
	AeronConfigError config_error = { 0 };
	if (!vfs || !AeronConfigFile_LoadYamlEx(vfs, root, path, &document, &config_error)) {
		if (error && error_capacity)
			snprintf(error, error_capacity, "%s:%d: %s", path, config_error.line, config_error.message);
		return NULL;
	}
	TieFlightAssetBundle* bundle = calloc(1, sizeof *bundle);
	if (!bundle) {
		AeronConfigFile_Destroy(document);
		if (error && error_capacity)
			snprintf(error, error_capacity, "allocation failed for %s", path);
		return NULL;
	}
	bundle->kind = kind;
	if (kind == TIE_FLIGHT_ASSET_CATALOG_REMASTER) {
		snprintf(bundle->content_prefix, sizeof bundle->content_prefix, "remaster");
		bundle->presentation_aspect_width = 16;
		bundle->presentation_aspect_height = 9;
	}
	const AeronConfigNode* document_root = AeronConfigFile_Root(document);
	const char* const remaster_v1_keys[] = { "version",       "models",        "skybox",
											 "billboards",    "backdrops",     "cockpit_views",
											 "cockpit_parts", "cockpit_masks", "cockpit_fonts" };
	const char* const remaster_v2_keys[] = { "version",       "presentation",  "models",
											 "skybox",        "billboards",    "backdrops",
											 "cockpit_views", "cockpit_parts", "cockpit_masks",
											 "cockpit_fonts" };
	const char* const tie98_keys[] = { "version", "models" };
	const AeronConfigNode* version = AeronConfigNode_MapGet(document_root, "version");
	const int64_t catalog_version = AeronConfigNode_Int(version, 0);
	bool valid = AeronConfigNode_Type(version) == AERON_CONFIG_INT;
	if (!valid || (kind == TIE_FLIGHT_ASSET_CATALOG_TIE98 && catalog_version != 1) ||
		(kind == TIE_FLIGHT_ASSET_CATALOG_REMASTER && catalog_version != 1 && catalog_version != 2)) {
		valid = TieAssets_CatalogError(error, error_capacity, version, "version",
									   kind == TIE_FLIGHT_ASSET_CATALOG_REMASTER ? "expected version 1 or 2"
																				 : "expected version 1");
	}
	if (valid) {
		const char* const* allowed = tie98_keys;
		size_t allowed_count = sizeof tie98_keys / sizeof tie98_keys[0];
		if (kind == TIE_FLIGHT_ASSET_CATALOG_REMASTER) {
			allowed = catalog_version == 2 ? remaster_v2_keys : remaster_v1_keys;
			allowed_count = catalog_version == 2 ? sizeof remaster_v2_keys / sizeof remaster_v2_keys[0]
												 : sizeof remaster_v1_keys / sizeof remaster_v1_keys[0];
		}
		valid = TieAssets_ValidateMap(document_root, "", allowed, allowed_count, error, error_capacity);
	}
	if (valid && kind == TIE_FLIGHT_ASSET_CATALOG_REMASTER && catalog_version == 2) {
		valid = TieAssets_ParsePresentation(bundle, document_root, error, error_capacity);
	}
	if (valid)
		valid = TieAssets_ParseModels(bundle, AeronConfigNode_MapGet(document_root, "models"), error,
									  error_capacity);
	if (valid && kind == TIE_FLIGHT_ASSET_CATALOG_REMASTER) {
		valid = TieAssets_CopyPath(bundle->skybox, AeronConfigNode_MapGet(document_root, "skybox"), "skybox",
								   error, error_capacity);
		bool seen[TIE_SPECIES_COUNT] = { false };
		if (valid)
			valid = TieAssets_ParseSpriteRows(bundle, AeronConfigNode_MapGet(document_root, "billboards"),
											  "billboards", seen, error, error_capacity);
		if (valid)
			valid = TieAssets_ParseSpriteRows(bundle, AeronConfigNode_MapGet(document_root, "backdrops"),
											  "backdrops", seen, error, error_capacity);
		if (valid)
			valid = TieAssets_ParseCockpits(bundle, document_root, error, error_capacity);
		const AeronConfigNode* fonts = AeronConfigNode_MapGet(document_root, "cockpit_fonts");
		const char* const font_keys[] = { "micro", "tiny" };
		if (valid)
			valid = TieAssets_ValidateMap(fonts, "cockpit_fonts", font_keys, 2, error, error_capacity) &&
					TieAssets_CopyPath(bundle->cockpit_micro, AeronConfigNode_MapGet(fonts, "micro"),
									   "cockpit_fonts.micro", error, error_capacity) &&
					TieAssets_CopyPath(bundle->cockpit_tiny, AeronConfigNode_MapGet(fonts, "tiny"),
									   "cockpit_fonts.tiny", error, error_capacity);
		if (valid)
			valid = TieAssets_ValidateStaticAssets(vfs, bundle, error, error_capacity);
	}
	AeronConfigFile_Destroy(document);
	if (!valid) {
		free(bundle);
		return NULL;
	}
	return bundle;
}

void TieFlightAssets_Close(TieFlightAssetBundle* bundle) { free(bundle); }
int TieFlightAssets_Count(const TieFlightAssetBundle* bundle) { return bundle ? bundle->model_count : 0; }
const TieFlightAssetEntry* TieFlightAssets_Find(const TieFlightAssetBundle* bundle, uint16_t species_idx) {
	if (!bundle)
		return NULL;
	for (int index = 0; index < bundle->model_count; ++index)
		if (bundle->models[index].species_idx == species_idx)
			return &bundle->models[index];
	return NULL;
}
const char* TieFlightAssets_ContentPrefix(const TieFlightAssetBundle* bundle) {
	return bundle ? bundle->content_prefix : NULL;
}
const char* TieFlightAssets_Skybox(const TieFlightAssetBundle* bundle) {
	return bundle && bundle->skybox[0] ? bundle->skybox : NULL;
}
const TieFlightAssetBillboard* TieFlightAssets_BillboardForSpecies(const TieFlightAssetBundle* bundle,
																   uint16_t species_idx) {
	if (!bundle)
		return NULL;
	for (int index = 0; index < bundle->billboard_count; ++index)
		if (bundle->billboards[index].species_idx == species_idx)
			return &bundle->billboards[index];
	return NULL;
}
int TieFlightAssets_BillboardCount(const TieFlightAssetBundle* bundle) {
	return bundle ? bundle->billboard_count : 0;
}
static bool TieAssets_CaseEqual(const char* left, const char* right) {
	while (*left && *right) {
		char a = *left++, b = *right++;
		if (a >= 'A' && a <= 'Z')
			a = (char)(a - 'A' + 'a');
		if (b >= 'A' && b <= 'Z')
			b = (char)(b - 'A' + 'a');
		if (a != b)
			return false;
	}
	return *left == *right;
}
const TieFlightAssetCockpitView* TieFlightAssets_CockpitView(const TieFlightAssetBundle* bundle,
															 const char* view_name) {
	if (!bundle || !view_name)
		return NULL;
	for (int index = 0; index < bundle->cockpit_view_count; ++index)
		if (TieAssets_CaseEqual(bundle->cockpit_views[index].view_name, view_name))
			return &bundle->cockpit_views[index];
	return NULL;
}
const TieFlightAssetCockpitParts* TieFlightAssets_CockpitParts(const TieFlightAssetBundle* bundle,
															   const char* parts_name) {
	if (!bundle || !parts_name)
		return NULL;
	for (int index = 0; index < bundle->cockpit_parts_count; ++index)
		if (TieAssets_CaseEqual(bundle->cockpit_parts[index].parts_name, parts_name))
			return &bundle->cockpit_parts[index];
	return NULL;
}
const TieFlightAssetCrtMask* TieFlightAssets_CrtMask(const TieFlightAssetBundle* bundle, uint8_t variant,
													 uint16_t classic_w) {
	if (!bundle)
		return NULL;
	for (int index = 0; index < bundle->cockpit_mask_count; ++index) {
		const TieFlightAssetCrtMask* mask = &bundle->cockpit_masks[index];
		if (mask->variant == variant && mask->classic_w == classic_w)
			return mask;
	}
	return NULL;
}
const char* TieFlightAssets_CockpitFont(const TieFlightAssetBundle* bundle, bool tiny) {
	if (!bundle)
		return NULL;
	return tiny ? bundle->cockpit_tiny : bundle->cockpit_micro;
}

bool TieFlightAssets_PresentationAspect(const TieFlightAssetBundle* bundle, int* width, int* height) {
	if (!bundle || bundle->kind != TIE_FLIGHT_ASSET_CATALOG_REMASTER ||
		bundle->presentation_aspect_width <= 0 || bundle->presentation_aspect_height <= 0) {
		return false;
	}
	if (width)
		*width = bundle->presentation_aspect_width;
	if (height)
		*height = bundle->presentation_aspect_height;
	return true;
}
