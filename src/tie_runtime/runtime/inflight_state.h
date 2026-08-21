#ifndef TIE_INFLIGHT_STATE_H
#define TIE_INFLIGHT_STATE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Options, replay capture, and flight logic share this per-mission state.
 * Storage is owned by option.c. */

extern int8_t inflight_music_vol;
extern int8_t inflight_sound_vol;
extern int8_t inflight_speech_vol;
extern int8_t inflight_unlimited;
extern int8_t inflight_invulnerable;
extern int8_t inflight_collision;

typedef struct TieInflightOptions {
	bool starfighter_collision_damage;
	bool player_invulnerable;
	bool unlimited_ammunition;
	uint8_t sound_effects_volume;
	uint8_t music_volume;
	uint8_t speech_volume;
} TieInflightOptions;

/* option.c owns the shared options.cfg cache used by classic and modern UI. */
void TieInflightOptions_Load(void);
void TieInflightOptions_Reset(void);
void TieInflightOptions_Get(TieInflightOptions* out);
bool TieInflightOptions_Set(const TieInflightOptions* options);
bool TieInflightOptions_Flush(char* error, size_t error_capacity);

#endif /* TIE_INFLIGHT_STATE_H */
