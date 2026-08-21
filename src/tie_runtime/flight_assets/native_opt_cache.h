#ifndef TIE98_NATIVE_OPT_CACHE_H
#define TIE98_NATIVE_OPT_CACHE_H

#include <stddef.h>

#include "tie_runtime/flight_assets/model_types.h"

typedef struct TieFlightAssetStore TieFlightAssetStore;
typedef struct Tie98NativeOptCache Tie98NativeOptCache;

Tie98NativeOptCache* Tie98NativeOptCache_Create(TieFlightAssetStore* store, char* error,
												size_t error_capacity);
void Tie98NativeOptCache_Clear(Tie98NativeOptCache* cache);
void Tie98NativeOptCache_Destroy(Tie98NativeOptCache* cache);
Tie98OptApi Tie98NativeOptCache_Api(Tie98NativeOptCache* cache);

#endif
