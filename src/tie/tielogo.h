#ifndef TIE_TIELOGO_H
#define TIE_TIELOGO_H

#include "landru/bitmap.h"
#include "tie/shellext.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

extern BitmapStruct tielogo_background;

int tielogo_TieLogo(SceneHeadStruct* scene_head);

extern Actor* tie_actor;
extern Actor* fighter_actor;
extern Actor* fighter2_actor;
extern int16_t tielogo_fight_x[32];
extern int16_t tielogo_fight_y[32];
extern int16_t tielogo_fight_state[32];

#ifdef __cplusplus
}
#endif

#endif
