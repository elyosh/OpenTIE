#include "tie_runtime/audio/music_policy.h"
#include "tie_runtime/audio/imuse_session.h"

static uint64_t music_clock_us;

TieMusicSource TieMusicPolicy_Source(void) { return TieAudio_Config()->music_source; }

bool TieMusicPolicy_UsesImuse(void) { return TieMusicPolicy_Source() == TIE_MUSIC_IMUSE; }
bool TieMusicPolicy_UsesTie98(void) { return TieMusicPolicy_Source() == TIE_MUSIC_TIE98; }
void TieMusicPolicy_ResetClock(void) { music_clock_us = 0; }

void TieMusicPolicy_AdvanceTime(int32_t delta_us) {
	if (delta_us > 0)
		music_clock_us += (uint32_t)delta_us;
}

uint32_t TieMusicPolicy_NowMs(void) { return (uint32_t)(music_clock_us / 1000u); }
