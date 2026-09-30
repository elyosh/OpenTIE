#ifndef TIE_TRAIN_H
#define TIE_TRAIN_H

#include "tie/shellext.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void train_OpenScene(SceneHeadStruct* the_head, bool svga);
void train_CloseScene(bool svga);

#ifdef __cplusplus
}
#endif

#endif
