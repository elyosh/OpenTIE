#ifndef TIE_RUNTIME_PROFILE_H
#define TIE_RUNTIME_PROFILE_H

#include <stdbool.h>
#include <stdint.h>

#include "tie_runtime/runtime/profile_types.h"

typedef struct TieFrontendProfile {
	TieFrontendProfileId id;
	uint16_t vesa_mode;
	int16_t width;
	int16_t height;
	int16_t scratch_size;
	uint8_t font_count;
	bool secondary_vga;
	const char* asset_source_id;
} TieFrontendProfile;

const TieFrontendProfile* TieProfile_Frontend(void);
void TieProfile_SetFrontend(TieFrontendProfileId id);
TieFrontendProfileId TieProfile_FrontendId(void);
void TieProfile_SetFlight(const TieFlightProfile* profile);
const TieFlightProfile* TieProfile_Flight(void);
bool TieProfile_RequestFlight(const TieFlightProfile* profile);
bool TieProfile_SetTie98Renderer(Tie98OriginalRenderer renderer);
bool TieProfile_SetFlightUpdateRate(TieFlightUpdateRate update_rate);
bool TieProfile_SetPlayerEngineSoundEnabled(bool enabled);
bool TieProfile_SetPlayerEngineSoundVolume(int volume_percent);
bool TieProfile_UsesTie98Logic(void);
bool TieProfile_UsesDx5(void);
bool TieProfile_ApplyPendingFlight(void);
bool TieProfile_RendererChangePending(void);
void TieProfile_CompleteRendererChange(void);

#endif
