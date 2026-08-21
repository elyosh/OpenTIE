#ifndef TIE_MODERN_FLIGHT_OPTIONS_H
#define TIE_MODERN_FLIGHT_OPTIONS_H

#include <stdbool.h>
#include <stddef.h>

#include "tie_app/config/app_config.h"

typedef bool (*TieFlightOptionsApplyFn)(const TieAppLiveFlightOptions* previous,
										const TieAppLiveFlightOptions* requested, void* user, char* error,
										size_t error_capacity);
typedef bool (*TieFlightOptionsPersistFn)(const TieAppLiveFlightOptions* options, void* user, char* error,
										  size_t error_capacity);

bool TieFlightOptions_Configure(const TieAppLiveFlightOptions* requested, TieFlightOptionsApplyFn apply,
								TieFlightOptionsPersistFn persist, void* user);
void TieFlightOptions_Shutdown(void);
void TieFlightOptions_Get(TieAppLiveFlightOptions* out);
/* Failed runtime applications do not change requested state. */
bool TieFlightOptions_Set(const TieAppLiveFlightOptions* options, char* error, size_t error_capacity);
/* Persists only when requested state differs from the last successful flush. */
bool TieFlightOptions_Flush(char* error, size_t error_capacity);

#endif
