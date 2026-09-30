#ifndef TIE_COMBAT_H
#define TIE_COMBAT_H

#include "tie/shellext.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void combat_OpenScene(SceneHeadStruct* the_head);
void combat_CloseScene(void);

#ifdef __cplusplus
}
#endif

#endif
