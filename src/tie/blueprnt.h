#ifndef __BLUEPRNT_H__
#define __BLUEPRNT_H__

#include <stdint.h>

#include "tie/shellext.h"

/* Push the blueprint scene as a tie_core task. */
void blueprnt_Push_Blueprint_Task(SceneHeadStruct* the_head);

int16_t blueprnt_Flight_Object_Size(void);

#endif
