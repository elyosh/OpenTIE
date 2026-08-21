#ifndef TIE_FLIGHT_SPRITE_CACHE_H
#define TIE_FLIGHT_SPRITE_CACHE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "aeron/render.h"
#include "aeron/scene/runtime_atlas.h"
#include "aeron/scene/sprite_atlas.h"

struct TieFlightAssetSource;
typedef struct TieFlightSpriteCache TieFlightSpriteCache;

typedef struct TieFlightSpriteEntry {
	uint16_t species_idx;
	const AeronSpriteAtlas* atlas;
	const AeronRuntimeAtlas* runtime_atlas;
	AeronTexture* texture;
	float scale_factor;
} TieFlightSpriteEntry;

TieFlightSpriteCache* TieFlightSpriteCache_Create(const struct TieFlightAssetSource* source);
void TieFlightSpriteCache_Destroy(TieFlightSpriteCache* cache);
void TieFlightSpriteCache_ReleaseMissionAssets(TieFlightSpriteCache* cache);
void TieFlightSpriteCache_Reset(TieFlightSpriteCache* cache);
bool TieFlightSpriteCache_Prepare(TieFlightSpriteCache* cache, AeronCommandBuffer* cmd,
								  const uint16_t* species, uint16_t species_count, char* error,
								  size_t error_capacity);
const TieFlightSpriteEntry* TieFlightSpriteCache_Resolve(const TieFlightSpriteCache* cache,
														 uint16_t species_idx);
AeronTexture* TieFlightSpriteEntry_Texture(const TieFlightSpriteEntry* entry, uint16_t frame_index);
bool TieFlightSpriteEntry_PageSize(const TieFlightSpriteEntry* entry, uint16_t frame_index, int* out_width,
								   int* out_height);

#endif
