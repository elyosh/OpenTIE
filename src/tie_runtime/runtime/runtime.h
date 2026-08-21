#ifndef TIE_RUNTIME_RUNTIME_H
#define TIE_RUNTIME_RUNTIME_H

#include "tie_runtime/audio/imuse_session.h"
#include "tie_runtime/flight_assets/service.h"
#include "tie_runtime/runtime/profile_types.h"
#include "tie_runtime/storage/storage.h"

typedef struct TieRuntimeConfig {
	TieStorageConfig storage;
	TieFlightAssetsConfig flight_assets;
	TieFrontendProfileId frontend_profile;
	TieFlightProfile flight_profile;
	TieAudioConfig audio;
} TieRuntimeConfig;

bool TieRuntime_Init(const TieRuntimeConfig* config, char* error, size_t error_capacity);
void TieRuntime_Shutdown(void);
bool TieRuntime_IsActive(void);

/* Requests cooperative shutdown of both frontend and flight tasks. */
void TieRuntime_RequestExit(void);
bool TieRuntime_ShouldExit(void);
void TieRuntime_SetWindowActive(bool active);

/* One-shot request from recovered UI tasks to the application overlay. */
void TieRuntime_RequestSettingsMenu(void);
bool TieRuntime_ConsumeSettingsMenuRequest(void);

/* Advances one host-visible tick using the supplied synthetic-clock delta. */
void TieRuntime_Tick(int32_t delta_us);

/* UINT64_MAX means no task deadline precedes the presentation cadence. */
uint64_t TieRuntime_NextWakeDelayUs(void);
void TieRuntime_RequestFlightResourceRelease(void);
bool TieRuntime_FlightResourceReleaseRequested(void);
void TieRuntime_CompleteFlightResourceRelease(void);

#endif
