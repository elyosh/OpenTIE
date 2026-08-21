#ifndef TIE_INSTALLATION_H
#define TIE_INSTALLATION_H

#include <stdbool.h>
#include <stddef.h>

#include "aeron/vfs.h"
#include "tie_runtime/audio/config.h"
#include "tie_runtime/diagnostics/diagnostics.h"
#include "tie_runtime/display/classic_display.h"
#include "tie_runtime/display/classic_framebuffer.h"
#include "tie_runtime/flight_assets/model_types.h"
#include "tie_runtime/input/input.h"
#include "tie_runtime/runtime/exports.h"
#include "tie_runtime/runtime/profile.h"
#include "tie_runtime/storage/storage.h"

#include "tie_app/config/app_config.h"

typedef struct TieInstallation {
	TieGameVersion version;
	char root[TIE_GAME_DATA_PATH_MAX];
	AeronVfs* vfs;
} TieInstallation;

typedef struct TieInstallationSet {
	TieInstallation tie95;
	TieInstallation tie98;
	bool has_tie95;
	bool has_tie98;
} TieInstallationSet;

bool TieInstallation_Classify(const char* path, TieGameVersion* out_version, char* error,
							  size_t error_capacity);
bool TieInstallation_Open(TieInstallation* installation, TieGameVersion version, const char* path,
						  const char* resource_root, char* error, size_t error_capacity);
void TieInstallation_Close(TieInstallation* installation);
void TieInstallation_SetClose(TieInstallationSet* installations);
const TieInstallation* TieInstallation_Get(const TieInstallationSet* installations, TieGameVersion version);

#endif
