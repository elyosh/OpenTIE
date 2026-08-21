#ifndef __CREDITS_H__
#define __CREDITS_H__

#include "tie/shellext.h"
#include <stdint.h>

/* Push the credits scene as a tie_core task. Used by ShellTask in
 * shell.c — the host loop drives the scene via the task stack. */
void credits_Push_Credits_Task(SceneHeadStruct* scene_head);

#endif
