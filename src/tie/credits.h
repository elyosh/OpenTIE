#ifndef TIE_CREDITS_H
#define TIE_CREDITS_H

#include "tie/shellext.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

extern LandruHandle credits_star_buffer;
extern LandruHandle credits_text;

int credits_Credits(SceneHeadStruct* scene_head);

#ifdef __cplusplus
}
#endif

#endif
