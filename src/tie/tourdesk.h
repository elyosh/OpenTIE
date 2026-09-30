#ifndef TIE_TOURDESK_H
#define TIE_TOURDESK_H

#include "tie/shellext.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void tourdesk_OpenScene(SceneHeadStruct* scene_head, bool svga);
void tourdesk_CloseScene(bool svga);

#ifdef __cplusplus
}
#endif

#endif
