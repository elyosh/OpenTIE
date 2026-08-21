#ifndef __MAINMENU_H__
#define __MAINMENU_H__

#include "tie/shellext.h"
#include <stdint.h>

/* Push the main-menu scene as a tie_core task. Used by ShellTask in
 * shell.c — the host loop drives the scene via the task stack. */
void mainmenu_Push_Main_Menu_Task(SceneHeadStruct* scene_head);

#endif
