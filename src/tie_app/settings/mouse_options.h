#ifndef TIE_MODERN_MOUSE_OPTIONS_H
#define TIE_MODERN_MOUSE_OPTIONS_H

#include <stdbool.h>
#include <stddef.h>

#include "tie_app/config/app_config.h"

typedef bool (*TieMouseOptionsApplyFn)(const TieMouseFlightOptions* previous,
									   const TieMouseFlightOptions* requested, void* user, char* error,
									   size_t error_capacity);
typedef bool (*TieMouseOptionsPersistFn)(const TieMouseFlightOptions* options, void* user, char* error,
										 size_t error_capacity);

bool TieMouseOptions_Configure(const TieMouseFlightOptions* requested, TieMouseOptionsApplyFn apply,
							   TieMouseOptionsPersistFn persist, void* user);
void TieMouseOptions_Shutdown(void);
void TieMouseOptions_Get(TieMouseFlightOptions* out);
bool TieMouseOptions_Set(const TieMouseFlightOptions* options, char* error, size_t error_capacity);
bool TieMouseOptions_Flush(char* error, size_t error_capacity);

#endif
