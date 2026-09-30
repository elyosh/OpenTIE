#ifndef TIE_CREDITS_H
#define TIE_CREDITS_H

#include "tie/shellext.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct CreditsSceneResources {
	ResFile* credit_res;
	ResFile* text_res;
	int16_t view_configured;
} CreditsSceneResources;

/* Resources must be zero-initialized. Close also releases partial setup. */
int16_t credits_OpenScene(SceneHeadStruct* scene_head, CreditsSceneResources* resources, int16_t tie98);
void credits_CloseScene(CreditsSceneResources* resources);

#ifdef __cplusplus
}
#endif

#endif
