#ifndef TIE_KEYBOARD_CONFIG_H
#define TIE_KEYBOARD_CONFIG_H
#include "aeron/config_file.h"
#include "tie_runtime/input/keyboard_mapping.h"

bool TieKeyboardConfig_Read(const AeronConfigFile* document, TieKeyboardBindings* profile, char* error,
							size_t capacity);
bool TieKeyboardConfig_Write(AeronConfigFile* document, const TieKeyboardBindings* profile,
							 AeronConfigError* error);
bool TieKeyboardConfig_Migrate(AeronConfigFile* document, const TieKeyboardBindings* defaults, char* error,
							   size_t capacity);
/* Resolve user precedence into the temporary merged document without changing user overrides. */
bool TieKeyboardConfig_Resolve(const TieKeyboardBindings* defaults, const AeronConfigFile* user,
							   AeronConfigFile* merged, char* error, size_t capacity);
#endif
