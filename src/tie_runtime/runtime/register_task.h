#ifndef TIE_REGISTER_TASK_H
#define TIE_REGISTER_TASK_H

#include "tie/shellext.h"

void TieRegister_Begin(SceneHeadStruct* head);

void TieRegister_RunView(ResFile* resource, bool protect, const char* missing_resource);

#endif
