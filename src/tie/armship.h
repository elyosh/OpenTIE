#ifndef TIE_ARMSHIP_H
#define TIE_ARMSHIP_H

#include "tie/shellext.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Returns zero on setup failure. CloseScene also releases partial setup. */
int16_t armship_OpenScene(SceneHeadStruct* scene_head, ResFile** launch_resource);
void armship_CloseScene(ResFile* launch_resource);

#ifdef __cplusplus
}
#endif

#endif
