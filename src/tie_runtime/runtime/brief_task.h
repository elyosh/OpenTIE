#ifndef TIE_RUNTIME_RUNTIME_BRIEF_TASK_H
#define TIE_RUNTIME_RUNTIME_BRIEF_TASK_H

#include "tie/shellext.h"
#include <landru/input.h>

void TieBrief_Begin(SceneHeadStruct* scene_head);

void TieBrief_RunView(Input* notice, bool svga, int16_t return_x, int16_t return_y);

#endif
