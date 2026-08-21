#include "tie_runtime/flight_assets/store.h"

#include "aeron/aeron.h"
#include "tie_runtime/flight_assets/model_cache.h"
#include "tie_runtime/flight_assets/native_opt_cache.h"
#include "tie_runtime/runtime/exports.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TIE_MODEL_MAX_BYTES (64u * 1024u * 1024u)

static const char* TieFlightAssetStore_Name(const TieFlightAssetConfig* config) {
	if (!config)
		return "invalid";
	if (TieFlightAssetConfig_IsTie95(config))
		return "TIE95 original";
	return TieFlightAssetConfig_IsRemastered(config) ? "TIE98 remastered" : "TIE98 original";
}

static TieAspectRatio TieFlightAssetStore_PresentationAspect(const TieFlightAssetStore* store) {
	TieAspectRatio aspect = { 4, 3 };
	if (store && TieFlightAssetConfig_IsRemastered(&store->config)) {
		int width = 0;
		int height = 0;
		if (TieFlightAssets_PresentationAspect(store->catalog, &width, &height)) {
			aspect.width = width;
			aspect.height = height;
		}
	}
	return aspect;
}

static bool TieFlightAssetStore_Error(const TieFlightAssetStore* store, char* error, size_t capacity,
									  const char* format, ...) {
	char detail[640];
	va_list arguments;
	va_start(arguments, format);
	vsnprintf(detail, sizeof detail, format, arguments);
	va_end(arguments);
	if (error && capacity)
		snprintf(error, capacity, "flight model source %s: %s",
				 TieFlightAssetStore_Name(store ? &store->config : NULL), detail);
	return false;
}

bool TieFlightAssetSource_ReadOriginalEntry(const TieFlightAssetSource* source,
											const TieSpeciesLfdLocation* location, uint8_t** out_bytes,
											size_t* out_size, char* error, size_t error_capacity) {
	TieFlightAssetStore* store = source ? source->store : NULL;
	if (!store || !location || !out_bytes || !out_size)
		return TieFlightAssetStore_Error(store, error, error_capacity, "invalid original archive request");
	TieFormatError archive_error = { 0 };
	if (TieOriginalArchiveCache_ReadEntry(&store->archives, location, out_bytes, out_size, &archive_error))
		return true;
	return TieFlightAssetStore_Error(
		store, error, error_capacity, "cannot read ASSET/%s: %s",
		store->archives.archives[location->resource_set][location->lfd_file].path, archive_error.message);
}

bool TieFlightAssetStore_Init(TieFlightAssetStore* store, AeronVfs* vfs, const TieFlightAssetConfig* config,
							  char* error, size_t error_capacity) {
	if (error && error_capacity)
		error[0] = '\0';
	if (!store || !vfs || !config)
		return false;
	memset(store, 0, sizeof *store);
	store->vfs = vfs;
	store->config = *config;
	TieOriginalArchiveCache_Init(&store->archives, vfs);
	if (!TieFlightAssetConfig_IsTie95(config)) {
		const TieFlightAssetCatalogKind kind = TieFlightAssetConfig_IsRemastered(config)
												   ? TIE_FLIGHT_ASSET_CATALOG_REMASTER
												   : TIE_FLIGHT_ASSET_CATALOG_TIE98;
		store->catalog = TieFlightAssets_Open(vfs, kind, error, error_capacity);
		if (!store->catalog) {
			TieOriginalArchiveCache_Release(&store->archives);
			memset(store, 0, sizeof *store);
			return false;
		}
	}
	if (!TieFlightAssetConfig_IsTie95(config)) {
		store->models = TieFlightModelCache_Create(store, error, error_capacity);
		if (!store->models) {
			TieFlightAssets_Close(store->catalog);
			TieOriginalArchiveCache_Release(&store->archives);
			memset(store, 0, sizeof *store);
			return false;
		}
	}
	if (!TieFlightAssetConfig_IsTie95(config) && !TieFlightAssetConfig_IsRemastered(config)) {
		store->native_opts = Tie98NativeOptCache_Create(store, error, error_capacity);
		if (!store->native_opts) {
			TieFlightModelCache_Destroy(store->models);
			TieFlightAssets_Close(store->catalog);
			TieOriginalArchiveCache_Release(&store->archives);
			memset(store, 0, sizeof *store);
			return false;
		}
	}
	store->source = (TieFlightAssetSource) {
		.vfs = store->vfs,
		.catalog = store->catalog,
		.name = TieFlightAssetStore_Name(config),
		.version = config->profile.version,
		.model_source = config->profile.model_source,
		.presentation_aspect = TieFlightAssetStore_PresentationAspect(store),
		.smooth_angle_degrees = config->smooth_angle_degrees,
		.opt_projectile_emissive_strength = config->opt_projectile_emissive_strength,
		.store = store,
	};
	return true;
}

void TieFlightAssetStore_SetFallback(TieFlightAssetStore* store, TieFlightAssetStore* fallback) {
	if (!store)
		return;
	store->fallback = fallback;
	store->source.fallback = fallback ? &fallback->source : NULL;
}

void TieFlightAssetStore_ClearRuntimeCaches(TieFlightAssetStore* store) {
	if (!store)
		return;
	Tie98NativeOptCache_Clear(store->native_opts);
	TieFlightModelCache_Clear(store->models);
}

void TieFlightAssetStore_Release(TieFlightAssetStore* store) {
	if (!store)
		return;
	Tie98NativeOptCache_Destroy(store->native_opts);
	TieFlightModelCache_Destroy(store->models);
	TieFlightAssets_Close(store->catalog);
	TieOriginalArchiveCache_Release(&store->archives);
	memset(store, 0, sizeof *store);
}

TieFlightModelApi TieFlightAssetStore_ModelApi(TieFlightAssetStore* store) {
	return store && store->models ? TieFlightModelCache_Api(store->models) : (TieFlightModelApi) { 0 };
}

Tie98OptApi TieFlightAssetStore_NativeOptApi(TieFlightAssetStore* store) {
	return store ? Tie98NativeOptCache_Api(store->native_opts) : (Tie98OptApi) { 0 };
}

const AeronFlightModel* TieFlightAssetStore_AcquireModel(TieFlightAssetStore* store, uint16_t species_idx,
														 char* error, size_t error_capacity) {
	if (!store || !store->models)
		return NULL;
	return TieFlightModelCache_AcquireModel(store->models, species_idx, error, error_capacity);
}

void TieFlightAssetStore_ReleaseModelRenderData(TieFlightAssetStore* store, uint16_t species_idx) {
	if (store && store->models)
		TieFlightModelCache_ReleaseRenderData(store->models, species_idx);
}

bool TieFlightAssetSource_ReadModel(const TieFlightAssetSource* source, uint16_t species_idx,
									uint8_t** out_bytes, size_t* out_size,
									const TieFlightAssetEntry** out_catalog_entry, char* error,
									size_t error_capacity) {
	return TieFlightAssetStore_ReadModel(source ? source->store : NULL, species_idx, out_bytes, out_size,
										 out_catalog_entry, error, error_capacity);
}

const AeronFlightModel* TieFlightAssetSource_AcquireModel(const TieFlightAssetSource* source,
														  uint16_t species_idx, char* error,
														  size_t error_capacity) {
	return TieFlightAssetStore_AcquireModel(source ? source->store : NULL, species_idx, error,
											error_capacity);
}

void TieFlightAssetSource_ReleaseModelRenderData(const TieFlightAssetSource* source, uint16_t species_idx) {
	TieFlightAssetStore_ReleaseModelRenderData(source ? source->store : NULL, species_idx);
}

bool TieFlightAssetStore_HasModel(const TieFlightAssetStore* store, uint16_t species_idx) {
	if (!store || species_idx >= TIE_SPECIES_COUNT)
		return false;
	if (!TieFlightAssetConfig_IsRemastered(&store->config))
		return true;
	const TieFlightAssetEntry* entry = TieFlightAssets_Find(store->catalog, species_idx);
	if (!entry)
		return false;
	char path[TIE_FLIGHT_ASSET_PATH_MAX + 32];
	const char* prefix = TieFlightAssets_ContentPrefix(store->catalog);
	const int length = prefix && prefix[0] ? snprintf(path, sizeof path, "%s/%s", prefix, entry->path)
										   : snprintf(path, sizeof path, "%s", entry->path);
	return length >= 0 && (size_t)length < sizeof path &&
		   AeronVfs_Exists(store->vfs, AERON_VFS_ROOT_ASSET, path);
}

bool TieFlightAssetStore_ReadModel(TieFlightAssetStore* store, uint16_t species_idx, uint8_t** out_bytes,
								   size_t* out_size, const TieFlightAssetEntry** out_catalog_entry,
								   char* error, size_t error_capacity) {
	if (out_bytes)
		*out_bytes = NULL;
	if (out_size)
		*out_size = 0;
	if (out_catalog_entry)
		*out_catalog_entry = NULL;
	if (!store || !out_bytes || !out_size || species_idx >= TIE_SPECIES_COUNT)
		return TieFlightAssetStore_Error(store, error, error_capacity, "invalid model request for species %u",
										 species_idx);

	if (TieFlightAssetConfig_IsTie95(&store->config)) {
		TieSpeciesLfdLocation location;
		if (!TieRecoveredData_SpeciesDosModelLocation(species_idx, &location))
			return TieFlightAssetStore_Error(store, error, error_capacity,
											 "species %u has no loaded ShipModelData source", species_idx);
		TieFormatError archive_error = { 0 };
		if (!TieOriginalArchiveCache_ReadEntry(&store->archives, &location, out_bytes, out_size,
											   &archive_error))
			return TieFlightAssetStore_Error(
				store, error, error_capacity, "species %u in ASSET/%s failed: %s", species_idx,
				store->archives.archives[location.resource_set][location.lfd_file].path,
				archive_error.message);
		if (*out_size <= 2) {
			free(*out_bytes);
			*out_bytes = NULL;
			*out_size = 0;
			return TieFlightAssetStore_Error(store, error, error_capacity, "species %u payload is too short",
											 species_idx);
		}
		memmove(*out_bytes, *out_bytes + 2, *out_size - 2);
		*out_size -= 2;
		return true;
	}

	const TieFlightAssetEntry* entry = TieFlightAssets_Find(store->catalog, species_idx);
	if (!entry)
		return TieFlightAssetStore_Error(store, error, error_capacity,
										 "catalog has no model row for species %u", species_idx);
	char path[TIE_FLIGHT_ASSET_PATH_MAX + 32];
	const char* prefix = TieFlightAssets_ContentPrefix(store->catalog);
	const int length = prefix && prefix[0] ? snprintf(path, sizeof path, "%s/%s", prefix, entry->path)
										   : snprintf(path, sizeof path, "%s", entry->path);
	if (length < 0 || (size_t)length >= sizeof path ||
		!AeronVfs_ReadAll(store->vfs, AERON_VFS_ROOT_ASSET, path, TIE_MODEL_MAX_BYTES, out_bytes, out_size))
		return TieFlightAssetStore_Error(store, error, error_capacity,
										 "cannot read required ASSET/%s for species %u", path, species_idx);
	if (out_catalog_entry)
		*out_catalog_entry = entry;
	return true;
}
