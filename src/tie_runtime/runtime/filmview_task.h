#ifndef TIE_RUNTIME_FILMVIEW_TASK_H
#define TIE_RUNTIME_FILMVIEW_TASK_H

#include "tie/shellext.h"
#include <landru/input.h>

void TieFilmView_Begin(SceneHeadStruct* scene_head);
void TieFilmView_RequestFiles(void);
void TieFilmView_RequestDelete(Input* input);

#endif
