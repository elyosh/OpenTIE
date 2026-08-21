#ifndef TIE_MUSIC_POLICY_H
#define TIE_MUSIC_POLICY_H

#include "tie_runtime/audio/config.h"
#include "tie_runtime/diagnostics/diagnostics.h"
#include "tie_runtime/display/classic_display.h"
#include "tie_runtime/display/classic_framebuffer.h"
#include "tie_runtime/flight_assets/model_types.h"
#include "tie_runtime/input/input.h"
#include "tie_runtime/runtime/exports.h"
#include "tie_runtime/runtime/profile.h"
#include "tie_runtime/storage/storage.h"

#include <stdbool.h>
#include <stdint.h>

TieMusicSource TieMusicPolicy_Source(void);
bool TieMusicPolicy_UsesImuse(void);
bool TieMusicPolicy_UsesTie98(void);
void TieMusicPolicy_ResetClock(void);
void TieMusicPolicy_AdvanceTime(int32_t delta_us);
uint32_t TieMusicPolicy_NowMs(void);

#endif
