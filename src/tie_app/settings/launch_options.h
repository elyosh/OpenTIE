#ifndef TIE_MODERN_LAUNCH_OPTIONS_H
#define TIE_MODERN_LAUNCH_OPTIONS_H

#include <stdbool.h>
#include <stddef.h>

#include "tie_app/config/app_config.h"

typedef bool (*TieLaunchOptionsPersistFn)(const TieAppLaunchOptions* options, void* user, char* error,
										  size_t error_capacity);

bool TieLaunchOptions_Configure(const TieAppLaunchOptions* active, TieLaunchOptionsPersistFn persist,
								void* user);
void TieLaunchOptions_Shutdown(void);
void TieLaunchOptions_Get(TieAppLaunchOptions* out);
bool TieLaunchOptions_Set(const TieAppLaunchOptions* options);
/* Saving does not clear this; only a process restart changes the active baseline. */
bool TieLaunchOptions_RestartRequired(void);
bool TieLaunchOptions_Flush(char* error, size_t error_capacity);

#endif
