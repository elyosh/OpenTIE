#ifndef TIE_DEBRIEF_H
#define TIE_DEBRIEF_H

#include "tie/shellext.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

ResFile* debrief_OpenScene(SceneHeadStruct* scene_head, bool svga);
void debrief_CloseScene(ResFile* resource, bool svga);

#ifdef __cplusplus
}
#endif

#endif
