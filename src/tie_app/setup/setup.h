#ifndef TIE_APP_SETUP_H
#define TIE_APP_SETUP_H

#include <stdbool.h>
#include <stddef.h>

#include "aeron/vfs.h"

#include "tie_app/config/app_config.h"
#include "tie_app/setup/installation.h"

typedef struct TieUi TieUi;

typedef enum TieSetupResult {
	TIE_SETUP_SUCCESS,
	TIE_SETUP_CANCELLED,
	TIE_SETUP_ERROR,
} TieSetupResult;

TieSetupResult TieSetup_ResolveInstallations(TieUi* ui, AeronVfs* application_vfs, TieAppConfigState* config,
											 const char* tie95_override, const char* tie98_override,
											 TieInstallationSet* out, char* error, size_t error_capacity);

#endif
