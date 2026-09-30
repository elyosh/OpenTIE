#ifndef TIE_TITLE_H
#define TIE_TITLE_H

#include "landru/bitmap.h"
#include "tie/shellext.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { TITLE_MAX_LINES = 18 };

extern BitmapStruct title_background;

int16_t title_Title(SceneHeadStruct* scene_head);

extern int16_t title_num_lines;
extern LandruHandle title_text;

#ifdef __cplusplus
}
#endif

#endif
