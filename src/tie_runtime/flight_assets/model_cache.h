#ifndef TIE_FLIGHT_MODEL_CACHE_H
#define TIE_FLIGHT_MODEL_CACHE_H

#include <stddef.h>
#include <stdint.h>

#include "aeron/asset/flight_model.h"
#include "tie_runtime/flight_assets/model_types.h"

typedef struct TieFlightAssetStore TieFlightAssetStore;
typedef struct TieFlightModelCache TieFlightModelCache;

TieFlightModelCache* TieFlightModelCache_Create(TieFlightAssetStore* store, char* error,
												size_t error_capacity);
void TieFlightModelCache_Clear(TieFlightModelCache* cache);
void TieFlightModelCache_Destroy(TieFlightModelCache* cache);
TieFlightModelApi TieFlightModelCache_Api(TieFlightModelCache* cache);

const AeronFlightModel* TieFlightModelCache_AcquireModel(TieFlightModelCache* cache, uint16_t model_type,
														 char* error, size_t error_capacity);
void TieFlightModelCache_ReleaseRenderData(TieFlightModelCache* cache, uint16_t model_type);

#endif
