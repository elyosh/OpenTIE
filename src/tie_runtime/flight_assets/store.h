#ifndef TIE_FLIGHT_ASSET_STORE_H
#define TIE_FLIGHT_ASSET_STORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "tie_runtime/flight_assets/config.h"
#include "tie_runtime/flight_assets/model_types.h"
#include "tie_runtime/flight_assets/original_archive.h"
#include "tie_runtime/flight_assets/source.h"

typedef struct TieFlightModelCache TieFlightModelCache;
typedef struct Tie98NativeOptCache Tie98NativeOptCache;

typedef struct TieFlightAssetStore {
	AeronVfs* vfs;
	TieFlightAssetConfig config;
	TieFlightAssetBundle* catalog;
	TieOriginalArchiveCache archives;
	TieFlightModelCache* models;
	Tie98NativeOptCache* native_opts;
	struct TieFlightAssetStore* fallback;
	TieFlightAssetSource source;
} TieFlightAssetStore;

bool TieFlightAssetStore_Init(TieFlightAssetStore* store, AeronVfs* vfs, const TieFlightAssetConfig* config,
							  char* error, size_t error_capacity);
void TieFlightAssetStore_ClearRuntimeCaches(TieFlightAssetStore* store);
void TieFlightAssetStore_Release(TieFlightAssetStore* store);
bool TieFlightAssetStore_ReadModel(TieFlightAssetStore* store, uint16_t species_idx, uint8_t** out_bytes,
								   size_t* out_size, const TieFlightAssetEntry** out_catalog_entry,
								   char* error, size_t error_capacity);
bool TieFlightAssetStore_HasModel(const TieFlightAssetStore* store, uint16_t species_idx);
TieFlightModelApi TieFlightAssetStore_ModelApi(TieFlightAssetStore* store);
Tie98OptApi TieFlightAssetStore_NativeOptApi(TieFlightAssetStore* store);
const AeronFlightModel* TieFlightAssetStore_AcquireModel(TieFlightAssetStore* store, uint16_t species_idx,
														 char* error, size_t error_capacity);
void TieFlightAssetStore_ReleaseModelRenderData(TieFlightAssetStore* store, uint16_t species_idx);
void TieFlightAssetStore_SetFallback(TieFlightAssetStore* store, TieFlightAssetStore* fallback);

#endif
