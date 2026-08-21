#ifndef __TIELOGO_H__
#define __TIELOGO_H__

#include "tie/shellext.h"
#include <stdint.h>

/* Push the tielogo scene as a tie_core task. Used by ShellTask in
 * shell.c — the host loop drives the scene via the task stack. */
void tielogo_Push_TieLogo_Task(SceneHeadStruct* scene_head);

/* Per-tick HD-snapshot hook — appends the sticky stamp list to
 * actors_2d so the cutscene compositor renders the assembled-logo
 * accumulation that classic gets from draw_Backdrop's bitmap blit.
 * No-op outside SCENE_TIELOGO. Called from the runtime tick pipeline in port.c.
 * emit chain alongside lactor_emit_render_state et al. */

#endif
