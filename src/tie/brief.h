#ifndef TIE_BRIEF_H
#define TIE_BRIEF_H

#include "tie/shellext.h"

#include <landru/input.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int16_t brief_Brief(SceneHeadStruct* scene_head);
void brief_end_View(int32_t time);

#ifdef __cplusplus
}
#endif

#endif
