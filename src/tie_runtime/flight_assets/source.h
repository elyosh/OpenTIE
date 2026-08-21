#ifndef TIE_FLIGHT_ASSET_SOURCE_H
#define TIE_FLIGHT_ASSET_SOURCE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "aeron/asset/flight_model.h"
#include "tie_runtime/flight_assets/assets.h"
#include "tie_runtime/presentation/presentation.h"
#include "tie_runtime/runtime/profile_types.h"

/* Immutable renderer-facing description of one selected asset source. Runtime
 * caches and archives remain owned by the private store referenced by store. */
typedef struct TieFlightAssetSource {
	AeronVfs* vfs;
	const TieFlightAssetBundle* catalog;
	const char* name;
	TieGameVersion version;
	TieFlightModelSource model_source;
	TieAspectRatio presentation_aspect;
	float smooth_angle_degrees;
	float opt_projectile_emissive_strength;
	const struct TieFlightAssetSource* fallback;
	struct TieFlightAssetStore* store;
} TieFlightAssetSource;
struct TieSpeciesLfdLocation;

static inline bool TieFlightAssetSource_IsTie95(const TieFlightAssetSource* source) {
	return source && source->version == TIE_GAME_VERSION_TIE95;
}

static inline bool TieFlightAssetSource_IsRemastered(const TieFlightAssetSource* source) {
	return source && source->model_source == TIE_FLIGHT_MODEL_SOURCE_REMASTERED;
}

static inline bool TieFlightAssetSource_UsesRuntimeOpt(const TieFlightAssetSource* source) {
	return source && source->version == TIE_GAME_VERSION_TIE98 &&
		   source->model_source == TIE_FLIGHT_MODEL_SOURCE_ORIGINAL;
}

bool TieFlightAssetSource_ReadOriginalEntry(const TieFlightAssetSource* source,
											const struct TieSpeciesLfdLocation* location, uint8_t** out_bytes,
											size_t* out_size, char* error, size_t error_capacity);
bool TieFlightAssetSource_ReadModel(const TieFlightAssetSource* source, uint16_t species_idx,
									uint8_t** out_bytes, size_t* out_size,
									const TieFlightAssetEntry** out_catalog_entry, char* error,
									size_t error_capacity);
const AeronFlightModel* TieFlightAssetSource_AcquireModel(const TieFlightAssetSource* source,
														  uint16_t species_idx, char* error,
														  size_t error_capacity);
void TieFlightAssetSource_ReleaseModelRenderData(const TieFlightAssetSource* source, uint16_t species_idx);

#endif
