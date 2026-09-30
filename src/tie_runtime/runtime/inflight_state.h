#ifndef TIE_INFLIGHT_STATE_H
#define TIE_INFLIGHT_STATE_H

#include "tie/option.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct TieInflightOptions {
	bool starfighter_collision_damage;
	bool player_invulnerable;
	bool unlimited_ammunition;
	uint8_t sound_effects_volume;
	uint8_t music_volume;
	uint8_t speech_volume;
} TieInflightOptions;

/* Runtime owns the shared options.cfg cache used by classic and modern UI. */
void TieInflightOptions_Load(void);
void TieInflightOptions_Apply(void);
void TieInflightOptions_ApplyAudio(void);
/* Internal bridge from the classic 14-byte editor to the persisted cache. */
void TieInflightOptions_StoreLegacy(const uint8_t* values);
void TieInflightOptions_Reset(void);
void TieInflightOptions_Get(TieInflightOptions* out);
bool TieInflightOptions_Set(const TieInflightOptions* options);
bool TieInflightOptions_Flush(char* error, size_t error_capacity);

#endif /* TIE_INFLIGHT_STATE_H */
