#ifndef TIE_TITLE_H
#define TIE_TITLE_H

#include "tie/shellext.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { TITLE_MAX_LINES = 18 };

typedef struct TitleSceneResources {
	ResFile* file;
	char film_name[16];
} TitleSceneResources;

int16_t title_OpenScene(SceneHeadStruct* scene_head, TitleSceneResources* resources, int16_t font_slot);
void title_CloseScene(TitleSceneResources* resources);

extern int16_t title_num_lines;
extern LandruHandle title_text;

#ifdef __cplusplus
}
#endif

#endif
