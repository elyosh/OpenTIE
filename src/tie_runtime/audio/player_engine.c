#include "tie_runtime/audio/player_engine.h"

#include <stddef.h>

#include "tie/frontend_sound_tie98.h"
#include "tie/fsfx.h"

void TiePlayerEngineSound_StopActive(void) {
	static const uint16_t sound_ids[] = {
		FSFX_PLAYER_ENGINE_TIE_ID,
		FSFX_PLAYER_ENGINE_REBEL_ID,
	};
	for (size_t index = 0; index < sizeof sound_ids / sizeof sound_ids[0]; ++index) {
		if (lolevel_ImGetParam_tie98(sound_ids[index], 0x100) != 0)
			(void)lolevel_ImStopSound_tie98(sound_ids[index]);
	}
}
