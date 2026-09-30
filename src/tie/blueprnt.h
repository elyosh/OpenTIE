#ifndef TIE_BLUEPRNT_H
#define TIE_BLUEPRNT_H

#include "tie/shellext.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void blueprnt_OpenScene(SceneHeadStruct* the_head);
void blueprnt_CloseScene(void);

int16_t blueprnt_Flight_Object_Size(void);

#ifdef __cplusplus
}
#endif

#endif
