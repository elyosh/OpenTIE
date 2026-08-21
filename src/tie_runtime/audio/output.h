#ifndef TIE_APP_AUDIO_OUTPUT_H
#define TIE_APP_AUDIO_OUTPUT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef void (*TieAudioRenderFunc)(void* userdata, int16_t* frames, size_t frame_count);

#include "tie_runtime/audio/config.h"
#include "tie_runtime/diagnostics/diagnostics.h"
#include "tie_runtime/display/classic_display.h"
#include "tie_runtime/display/classic_framebuffer.h"
#include "tie_runtime/flight_assets/model_types.h"
#include "tie_runtime/input/input.h"
#include "tie_runtime/runtime/exports.h"
#include "tie_runtime/runtime/profile.h"
#include "tie_runtime/storage/storage.h"

/* Aeron audio lifecycle backed by one queued Aeron PCM stream. */
bool TieAudioOutput_Start(int sample_rate, int channels, TieAudioRenderFunc render, void* render_userdata);
void TieAudioOutput_Stop(void);

#endif /* TIE_APP_AUDIO_OUTPUT_H */
