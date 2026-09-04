#ifndef TIE_MODERN_AUDIO_OPTIONS_H
#define TIE_MODERN_AUDIO_OPTIONS_H

#include <stdbool.h>
#include <stddef.h>

#include "tie_app/config/app_config.h"

typedef bool (*TieAudioOptionsApplyFn)(const TieAppLiveAudioOptions* previous,
									   const TieAppLiveAudioOptions* requested, void* user, char* error,
									   size_t error_capacity);
typedef bool (*TieAudioOptionsPersistFn)(const TieAppLiveAudioOptions* options, void* user, char* error,
										 size_t error_capacity);

bool TieAudioOptions_Configure(const TieAppLiveAudioOptions* requested, TieAudioOptionsApplyFn apply,
							   TieAudioOptionsPersistFn persist, void* user);
void TieAudioOptions_Shutdown(void);
void TieAudioOptions_Get(TieAppLiveAudioOptions* out);
bool TieAudioOptions_Set(const TieAppLiveAudioOptions* options, char* error, size_t error_capacity);
bool TieAudioOptions_Flush(char* error, size_t error_capacity);

#endif
