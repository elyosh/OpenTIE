#ifndef TIE_FLIGHT_ASSETS_H
#define TIE_FLIGHT_ASSETS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "aeron/vfs.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TIE_FLIGHT_ASSET_PATH_MAX 256
typedef struct TieFlightAssetEntry {
	uint16_t species_idx;
	char path[TIE_FLIGHT_ASSET_PATH_MAX];
} TieFlightAssetEntry;

typedef struct TieFlightAssetBillboard {
	uint16_t species_idx;
	char atlas_path[TIE_FLIGHT_ASSET_PATH_MAX];
	char layout_path[TIE_FLIGHT_ASSET_PATH_MAX];
} TieFlightAssetBillboard;

typedef struct TieFlightAssetCockpitView {
	char view_name[16];
	char bitmap_hd[TIE_FLIGHT_ASSET_PATH_MAX];
	char bitmap_4_3[TIE_FLIGHT_ASSET_PATH_MAX];
	char damage[TIE_FLIGHT_ASSET_PATH_MAX];
} TieFlightAssetCockpitView;

typedef struct TieFlightAssetCockpitParts {
	char parts_name[16];
	char atlas[TIE_FLIGHT_ASSET_PATH_MAX];
	char layout[TIE_FLIGHT_ASSET_PATH_MAX];
} TieFlightAssetCockpitParts;

typedef struct TieFlightAssetCrtMask {
	uint8_t variant;
	uint16_t classic_w;
	char path[TIE_FLIGHT_ASSET_PATH_MAX];
} TieFlightAssetCrtMask;

typedef enum TieFlightAssetCatalogKind {
	TIE_FLIGHT_ASSET_CATALOG_REMASTER,
	TIE_FLIGHT_ASSET_CATALOG_TIE98,
} TieFlightAssetCatalogKind;

typedef struct TieFlightAssetBundle TieFlightAssetBundle;

TieFlightAssetBundle* TieFlightAssets_Open(AeronVfs* vfs, TieFlightAssetCatalogKind kind, char* error,
										   size_t error_capacity);
void TieFlightAssets_Close(TieFlightAssetBundle* bundle);
int TieFlightAssets_Count(const TieFlightAssetBundle* bundle);
const TieFlightAssetEntry* TieFlightAssets_Find(const TieFlightAssetBundle* bundle, uint16_t species_idx);
const char* TieFlightAssets_ContentPrefix(const TieFlightAssetBundle* bundle);
const char* TieFlightAssets_Skybox(const TieFlightAssetBundle* bundle);
const TieFlightAssetBillboard* TieFlightAssets_BillboardForSpecies(const TieFlightAssetBundle* bundle,
																   uint16_t species_idx);
int TieFlightAssets_BillboardCount(const TieFlightAssetBundle* bundle);
const TieFlightAssetCockpitView* TieFlightAssets_CockpitView(const TieFlightAssetBundle* bundle,
															 const char* view_name);
const TieFlightAssetCockpitParts* TieFlightAssets_CockpitParts(const TieFlightAssetBundle* bundle,
															   const char* parts_name);
const TieFlightAssetCrtMask* TieFlightAssets_CrtMask(const TieFlightAssetBundle* bundle, uint8_t variant,
													 uint16_t classic_w);
const char* TieFlightAssets_CockpitFont(const TieFlightAssetBundle* bundle, bool tiny);
bool TieFlightAssets_PresentationAspect(const TieFlightAssetBundle* bundle, int* width, int* height);

#ifdef __cplusplus
}
#endif

#endif
