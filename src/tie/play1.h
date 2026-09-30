#ifndef TIE_PLAY1_H
#define TIE_PLAY1_H

#include "tie/shellext.h"

#include <landru/bitmap.h>
#include <landru/surface.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int16_t play1_Play1(SceneHeadStruct* the_head);
extern int16_t play1_is_streaming;
extern LandruHandle play1_read_buffer;
extern BitmapStruct play1_last_frame;
extern BitmapStruct play1_current_frame;

#ifdef __cplusplus
}
#endif

#endif
