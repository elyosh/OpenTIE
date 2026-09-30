#ifndef TIE_RUNTIME_CREDITS_TASK_H
#define TIE_RUNTIME_CREDITS_TASK_H

#include "tie/shellext.h"

void TieCredits_Begin(SceneHeadStruct* scene_head);

void TieCredits_RunView(ResFile* credit_res, ResFile* text_res, bool ready);

#endif
