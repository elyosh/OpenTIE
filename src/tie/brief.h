#ifndef TIE_BRIEF_H
#define TIE_BRIEF_H

#include "tie/shellext.h"

#include <landru/input.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

bool brief_OpenScene(SceneHeadStruct* scene_head, bool svga, Input** notice);
void brief_CloseNotice(Input* notice);
void brief_PrepareView(void);
void brief_CloseScene(bool svga);

#ifdef __cplusplus
}
#endif

#endif
