#ifndef TIE_MODERN_VIDEO_OPTIONS_H
#define TIE_MODERN_VIDEO_OPTIONS_H

#include <stdbool.h>
#include <stddef.h>

#include "tie_app/config/app_config.h"

typedef bool (*TieVideoOptionsApplyFn)(const TieAppVideoConfig* previous, const TieAppVideoConfig* requested,
									   void* user, char* error, size_t error_capacity);
typedef bool (*TieVideoOptionsPersistFn)(const TieAppVideoConfig* options, void* user, char* error,
										 size_t error_capacity);

bool TieVideoOptions_Configure(const TieAppVideoConfig* defaults, const TieAppVideoConfig* requested,
							   TieVideoOptionsApplyFn apply, TieVideoOptionsPersistFn persist, void* user);
void TieVideoOptions_Shutdown(void);
void TieVideoOptions_Get(TieAppVideoConfig* out);
/* Failed runtime applications do not change requested state. */
bool TieVideoOptions_Set(const TieAppVideoConfig* options, char* error, size_t error_capacity);
bool TieVideoOptions_RestoreDefaults(char* error, size_t error_capacity);
/* Persists only when requested state differs from the last successful flush. */
bool TieVideoOptions_Flush(char* error, size_t error_capacity);

#endif
