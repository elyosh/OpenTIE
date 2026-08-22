#ifndef __MAP_H__
#define __MAP_H__

#include "tie/shellext.h"
#include <stdint.h>

/* Push the MAP/briefing display module as a tie_core task.
 * Scenes: 123=training, 133/134=combat sim, 181=briefing. */
void map_Push_Map_Task(SceneHeadStruct* scene_head);

/* Training pilot medal status — extern per watdbg, set by MAP, read by SHELL */
extern int16_t train_pilot_medal_status;

#endif
