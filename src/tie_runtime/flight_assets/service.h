#ifndef TIE_RUNTIME_FLIGHT_ASSETS_SERVICE_H
#define TIE_RUNTIME_FLIGHT_ASSETS_SERVICE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "tie_runtime/flight_assets/config.h"
#include "tie_runtime/flight_assets/model_types.h"
#include "tie_runtime/flight_assets/source.h"

typedef struct TieFlightAssetsConfig {
	AeronVfs* tie95_vfs;
	AeronVfs* tie98_vfs;
	TieFlightAssetConfig source;
} TieFlightAssetsConfig;

bool TieFlightAssets_Init(const TieFlightAssetsConfig* config, char* error, size_t error_capacity);
void TieFlightAssets_Shutdown(void);
bool TieFlightAssets_SelectProfile(const TieFlightProfile* profile, char* error, size_t error_capacity);
const TieFlightAssetSource* TieFlightAssets_CurrentSource(void);
TieFlightModelApi TieFlightAssets_ModelApi(void);
TieFlightModelApi TieFlightAssets_Tie98OriginalModelApi(void);
Tie98OptApi TieFlightAssets_NativeOptApi(void);
bool TieFlightAssets_Tie98Available(void);
uint32_t TieFlightAssets_ProfileGeneration(void);
void TieFlightAssets_ClearRuntimeCaches(void);

#endif
