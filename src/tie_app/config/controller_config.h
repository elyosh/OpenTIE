#ifndef TIE_CONTROLLER_CONFIG_H
#define TIE_CONTROLLER_CONFIG_H
#include "aeron/config_file.h"
#include "tie_runtime/input/controller_mapping.h"
bool TieControllerConfig_ReadProfile(const AeronConfigFile* document, const char* path,
									 AeronControllerKind kind, TieControllerProfile* profile, char* error,
									 size_t capacity);
bool TieControllerConfig_Read(const AeronConfigFile* document, TieControllerOptions* options, char* error,
							  size_t capacity);
bool TieControllerConfig_Write(AeronConfigFile* document, const TieControllerOptions* options,
							   AeronConfigError* error);
#endif
