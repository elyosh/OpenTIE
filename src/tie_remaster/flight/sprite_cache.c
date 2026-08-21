#include "tie_remaster/flight/sprite_cache.h"

#include "aeron/aeron.h"
#include "aeron/scene/image_cache.h"
#include "tie_formats/xact.h"
#include "tie_runtime/flight_assets/source.h"
#include "tie_runtime/runtime/exports.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct TieFlightPreparedSprite {
	bool ready;
	bool has_original_location;
	TieSpeciesLfdLocation original_location;
	AeronRuntimeAtlas original;
	AeronSpriteAtlas remaster_layout;
	const AeronImageCacheEntry* remaster_image;
} TieFlightPreparedSprite;

struct TieFlightSpriteCache {
	const TieFlightAssetSource* source;
	AeronImageCache* image_cache;
	TieFlightPreparedSprite prepared[TIE_SPECIES_COUNT];
	TieFlightSpriteEntry entries[TIE_SPECIES_COUNT];
	bool entry_ready[TIE_SPECIES_COUNT];
};

static bool TieFlightSpriteCache_SpriteError(const TieFlightSpriteCache* cache, char* error, size_t capacity,
											 const char* format, ...) {
	char detail[640];
	va_list arguments;
	va_start(arguments, format);
	vsnprintf(detail, sizeof detail, format, arguments);
	va_end(arguments);
	if (error && capacity)
		snprintf(error, capacity, "flight model source %s: %s",
				 cache && cache->source ? cache->source->name : "invalid", detail);
	return false;
}

static bool TieFlightSpriteCache_LocationsEqual(const TieSpeciesLfdLocation* left,
												const TieSpeciesLfdLocation* right) {
	return left->entry == right->entry && left->resource_set == right->resource_set &&
		   left->lfd_file == right->lfd_file;
}

TieFlightSpriteCache* TieFlightSpriteCache_Create(const TieFlightAssetSource* source) {
	if (!source)
		return NULL;
	TieFlightSpriteCache* cache = calloc(1, sizeof *cache);
	if (!cache)
		return NULL;
	cache->source = source;
	if (TieFlightAssetSource_IsRemastered(source) ||
		TieFlightAssetSource_IsRemastered(source ? source->fallback : NULL)) {
		cache->image_cache = Aeron_ImageCacheCreate();
		if (!cache->image_cache) {
			free(cache);
			return NULL;
		}
	}
	return cache;
}

void TieFlightSpriteCache_ReleaseMissionAssets(TieFlightSpriteCache* cache) {
	if (!cache)
		return;
	for (uint16_t species = 0; species < TIE_SPECIES_COUNT; ++species) {
		Aeron_RuntimeAtlasRelease(&cache->prepared[species].original);
		Aeron_SpriteAtlasFree(&cache->prepared[species].remaster_layout);
	}
	Aeron_ImageCacheDestroy(cache->image_cache);
	cache->image_cache = NULL;
	memset(cache->prepared, 0, sizeof cache->prepared);
	memset(cache->entries, 0, sizeof cache->entries);
	memset(cache->entry_ready, 0, sizeof cache->entry_ready);
}

void TieFlightSpriteCache_Destroy(TieFlightSpriteCache* cache) {
	if (!cache)
		return;
	TieFlightSpriteCache_ReleaseMissionAssets(cache);
	free(cache);
}

void TieFlightSpriteCache_Reset(TieFlightSpriteCache* cache) {
	if (!cache)
		return;
	memset(cache->entries, 0, sizeof cache->entries);
	memset(cache->entry_ready, 0, sizeof cache->entry_ready);
	if (cache->image_cache)
		return;
	for (uint16_t species = 0; species < TIE_SPECIES_COUNT; ++species)
		Aeron_RuntimeAtlasRelease(&cache->prepared[species].original);
	memset(cache->prepared, 0, sizeof cache->prepared);
}

static bool TieFlightSpriteCache_PrepareOriginal(TieFlightSpriteCache* cache, AeronCommandBuffer* cmd,
												 uint16_t species_idx, const TieFlightAssetSource* source,
												 const TieSpeciesLfdLocation* location, char* error,
												 size_t capacity) {
	TieFlightPreparedSprite* slot = &cache->prepared[species_idx];
	uint8_t* bytes = NULL;
	size_t size = 0;
	TieFormatError codec_error = { 0 };
	TieRgbaFrames decoded = { 0 };
	if (!TieFlightAssetSource_ReadOriginalEntry(source, location, &bytes, &size, error, capacity))
		return false;
	if (!TieXact_DecodeRgba8(bytes, size, &decoded, &codec_error)) {
		free(bytes);
		return TieFlightSpriteCache_SpriteError(cache, error, capacity, "cannot decode species %u XACT: %s",
												species_idx, codec_error.message);
	}
	free(bytes);
	AeronRuntimeAtlasFrame* frames = calloc(decoded.count, sizeof *frames);
	if (!frames) {
		TieRgbaFrames_Free(&decoded);
		return TieFlightSpriteCache_SpriteError(cache, error, capacity,
												"species %u XACT atlas allocation failed", species_idx);
	}
	for (uint16_t index = 0; index < decoded.count; ++index) {
		frames[index] = (AeronRuntimeAtlasFrame) {
			.rgba = decoded.frames[index].rgba,
			.width = decoded.frames[index].width,
			.height = decoded.frames[index].height,
			.id = decoded.frames[index].stable_id,
			.anchor_x = decoded.frames[index].anchor_x,
			.anchor_y = decoded.frames[index].anchor_y,
		};
	}
	const AeronRuntimeAtlasOptions options = {
		.format = AERON_TEXTURE_FORMAT_RGBA8_SRGB,
		.color_space = AERON_COLOR_SPACE_SRGB,
		.alpha_mode = AERON_IMAGE_ALPHA_STRAIGHT,
		.generate_mips = true,
		.debug_name = "TIE original XACT",
	};
	const bool built = Aeron_RuntimeAtlasBuild(&slot->original, cmd, frames, decoded.count, &options);
	free(frames);
	TieRgbaFrames_Free(&decoded);
	if (!built)
		return TieFlightSpriteCache_SpriteError(cache, error, capacity,
												"species %u XACT GPU atlas creation failed", species_idx);
	slot->original_location = *location;
	slot->has_original_location = true;
	slot->ready = true;
	return true;
}

static bool TieFlightSpriteCache_PrepareRemaster(TieFlightSpriteCache* cache, AeronCommandBuffer* cmd,
												 uint16_t species_idx, const TieFlightAssetSource* source,
												 char* error, size_t capacity) {
	TieFlightPreparedSprite* slot = &cache->prepared[species_idx];
	if (slot->ready)
		return true;
	if (!cache->image_cache) {
		cache->image_cache = Aeron_ImageCacheCreate();
		if (!cache->image_cache)
			return TieFlightSpriteCache_SpriteError(cache, error, capacity,
													"remastered sprite image cache allocation failed");
	}
	const TieFlightAssetBundle* catalog = source->catalog;
	AeronVfs* vfs = source->vfs;
	const TieFlightAssetBillboard* asset = TieFlightAssets_BillboardForSpecies(catalog, species_idx);
	if (!asset)
		return TieFlightSpriteCache_SpriteError(
			cache, error, capacity, "remaster catalog has no sprite row for species %u", species_idx);
	const char* prefix = TieFlightAssets_ContentPrefix(catalog);
	char layout_path[TIE_FLIGHT_ASSET_PATH_MAX + 32];
	char atlas_path[TIE_FLIGHT_ASSET_PATH_MAX + 32];
	if (snprintf(layout_path, sizeof layout_path, "%s/%s", prefix, asset->layout_path) >=
			(int)sizeof layout_path ||
		snprintf(atlas_path, sizeof atlas_path, "%s/%s", prefix, asset->atlas_path) >= (int)sizeof atlas_path)
		return TieFlightSpriteCache_SpriteError(cache, error, capacity,
												"sprite paths for species %u are too long", species_idx);
	if (!Aeron_SpriteAtlasLoadVfs(&slot->remaster_layout, vfs, AERON_VFS_ROOT_ASSET, layout_path))
		return TieFlightSpriteCache_SpriteError(cache, error, capacity, "cannot parse required ASSET/%s",
												layout_path);
	slot->remaster_image = Aeron_ImageCacheLoadVfs(cache->image_cache, cmd, vfs, AERON_VFS_ROOT_ASSET,
												   atlas_path, 64u * 1024u * 1024u);
	if (!slot->remaster_image) {
		Aeron_SpriteAtlasFree(&slot->remaster_layout);
		return TieFlightSpriteCache_SpriteError(cache, error, capacity, "cannot load required ASSET/%s",
												atlas_path);
	}
	slot->ready = true;
	return true;
}

static bool TieFlightSpriteCache_RemasterSpritePresent(const TieFlightAssetSource* source,
													   uint16_t species_idx) {
	if (!TieFlightAssetSource_IsRemastered(source))
		return false;
	const TieFlightAssetBundle* catalog = source->catalog;
	AeronVfs* vfs = source->vfs;
	const TieFlightAssetBillboard* asset = TieFlightAssets_BillboardForSpecies(catalog, species_idx);
	if (!asset)
		return false;
	const char* prefix = TieFlightAssets_ContentPrefix(catalog);
	char layout_path[TIE_FLIGHT_ASSET_PATH_MAX + 32];
	char atlas_path[TIE_FLIGHT_ASSET_PATH_MAX + 32];
	if (snprintf(layout_path, sizeof layout_path, "%s/%s", prefix, asset->layout_path) >=
			(int)sizeof layout_path ||
		snprintf(atlas_path, sizeof atlas_path, "%s/%s", prefix, asset->atlas_path) >= (int)sizeof atlas_path)
		return false;
	return AeronVfs_Exists(vfs, AERON_VFS_ROOT_ASSET, layout_path) &&
		   AeronVfs_Exists(vfs, AERON_VFS_ROOT_ASSET, atlas_path);
}

static bool TieFlightSpriteCache_OriginalSpritePresent(const TieFlightAssetSource* source,
													   uint16_t species_idx,
													   TieSpeciesLfdLocation* out_location) {
	static const char* const directories[] = { "RES320", "RES640" };
	static const char* const names[] = { "SPECIES.LFD", "SPECIES2.LFD", "SPECIES3.LFD" };
	if (!source || !out_location || !TieRecoveredData_SpeciesXactLocation(species_idx, out_location))
		return false;
	char path[64];
	const int length = snprintf(path, sizeof path, "%s/%s", directories[out_location->resource_set],
								names[out_location->lfd_file]);
	return length >= 0 && (size_t)length < sizeof path &&
		   AeronVfs_Exists(source->vfs, AERON_VFS_ROOT_ASSET, path);
}

static bool TieFlightSpriteCache_PrepareSpecies(TieFlightSpriteCache* cache, AeronCommandBuffer* cmd,
												uint16_t species_idx, char* error, size_t capacity) {
	if (species_idx >= TIE_SPECIES_COUNT)
		return TieFlightSpriteCache_SpriteError(cache, error, capacity, "invalid sprite species %u",
												species_idx);
	TieFlightPreparedSprite* owner = NULL;
	if (TieFlightAssetSource_IsRemastered(cache->source)) {
		owner = &cache->prepared[species_idx];
		if (TieFlightSpriteCache_RemasterSpritePresent(cache->source, species_idx)) {
			if (!TieFlightSpriteCache_PrepareRemaster(cache, cmd, species_idx, cache->source, error,
													  capacity))
				return false;
		} else {
			const TieFlightAssetSource* fallback = cache->source->fallback;
			TieSpeciesLfdLocation location;
			if (!TieFlightSpriteCache_OriginalSpritePresent(fallback, species_idx, &location))
				return TieFlightSpriteCache_SpriteError(
					cache, error, capacity, "sprite species %u is absent from both sources", species_idx);
			if (!TieFlightSpriteCache_PrepareOriginal(cache, cmd, species_idx, fallback, &location, error,
													  capacity))
				return false;
			Aeron_LogInfo("tie.assets", "species %u sprite source: %s (fallback)", species_idx,
						  fallback->name);
		}
	} else {
		TieSpeciesLfdLocation location;
		if (TieFlightSpriteCache_OriginalSpritePresent(cache->source, species_idx, &location)) {
			for (uint16_t candidate = 0; candidate < TIE_SPECIES_COUNT; ++candidate) {
				TieFlightPreparedSprite* slot = &cache->prepared[candidate];
				if (slot->ready && slot->has_original_location &&
					TieFlightSpriteCache_LocationsEqual(&slot->original_location, &location)) {
					owner = slot;
					break;
				}
			}
			if (!owner) {
				owner = &cache->prepared[species_idx];
				if (!TieFlightSpriteCache_PrepareOriginal(cache, cmd, species_idx, cache->source, &location,
														  error, capacity))
					return false;
			}
		} else {
			const TieFlightAssetSource* fallback = cache->source->fallback;
			owner = &cache->prepared[species_idx];
			if (!TieFlightSpriteCache_RemasterSpritePresent(fallback, species_idx))
				return TieFlightSpriteCache_SpriteError(
					cache, error, capacity, "sprite species %u is absent from both sources", species_idx);
			if (!TieFlightSpriteCache_PrepareRemaster(cache, cmd, species_idx, fallback, error, capacity))
				return false;
			Aeron_LogInfo("tie.assets", "species %u sprite source: %s (fallback)", species_idx,
						  fallback->name);
		}
	}

	TieFlightSpriteEntry* entry = &cache->entries[species_idx];
	entry->species_idx = species_idx;
	if (!owner->has_original_location) {
		entry->atlas = &owner->remaster_layout;
		entry->texture = owner->remaster_image->tex;
	} else {
		entry->atlas = &owner->original.layout;
		entry->runtime_atlas = &owner->original;
	}
	entry->scale_factor = 1.0f;
	if (entry->atlas->classic_atlas_w > 0)
		entry->scale_factor = (float)entry->atlas->atlas_w / (float)entry->atlas->classic_atlas_w;
	cache->entry_ready[species_idx] = true;
	return true;
}

bool TieFlightSpriteCache_Prepare(TieFlightSpriteCache* cache, AeronCommandBuffer* cmd,
								  const uint16_t* species, uint16_t species_count, char* error,
								  size_t error_capacity) {
	if (error && error_capacity)
		error[0] = '\0';
	if (!cache || !cmd || (species_count && !species))
		return TieFlightSpriteCache_SpriteError(cache, error, error_capacity,
												"invalid sprite preparation request");
	for (uint16_t index = 0; index < species_count; ++index)
		if (!TieFlightSpriteCache_PrepareSpecies(cache, cmd, species[index], error, error_capacity))
			return false;
	return true;
}

const TieFlightSpriteEntry* TieFlightSpriteCache_Resolve(const TieFlightSpriteCache* cache,
														 uint16_t species_idx) {
	if (!cache || species_idx >= TIE_SPECIES_COUNT || !cache->entry_ready[species_idx]) {
		char message[128];
		snprintf(message, sizeof message, "flight sprite species %u was not prepared for this mission",
				 (unsigned)species_idx);
		Aeron_RequestFatalError("Flight Asset Error", message);
		return NULL;
	}
	return &cache->entries[species_idx];
}

AeronTexture* TieFlightSpriteEntry_Texture(const TieFlightSpriteEntry* entry, uint16_t frame_index) {
	if (!entry || !entry->atlas || frame_index >= entry->atlas->frame_count)
		return NULL;
	if (!entry->runtime_atlas)
		return entry->texture;
	const int page = entry->atlas->pages[frame_index];
	if (page < 0 || page >= entry->atlas->page_count)
		return NULL;
	return entry->runtime_atlas->pages[page].texture;
}

bool TieFlightSpriteEntry_PageSize(const TieFlightSpriteEntry* entry, uint16_t frame_index, int* out_width,
								   int* out_height) {
	if (out_width)
		*out_width = 0;
	if (out_height)
		*out_height = 0;
	if (!entry || !entry->atlas || frame_index >= entry->atlas->frame_count)
		return false;
	int width = entry->atlas->atlas_w;
	int height = entry->atlas->atlas_h;
	if (entry->runtime_atlas) {
		const int page = entry->atlas->pages[frame_index];
		if (page < 0 || page >= entry->atlas->page_count)
			return false;
		width = entry->runtime_atlas->pages[page].width;
		height = entry->runtime_atlas->pages[page].height;
	}
	if (width <= 0 || height <= 0)
		return false;
	if (out_width)
		*out_width = width;
	if (out_height)
		*out_height = height;
	return true;
}
