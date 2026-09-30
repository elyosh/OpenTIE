#ifndef TIE_COMBAT_H
#define TIE_COMBAT_H

#include "tie/shellext.h"
#include "tie_runtime/storage/score_tables.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

extern GameScoreHead* combat_score_data;

int16_t combat_Combat(SceneHeadStruct* scene_head);

#ifdef __cplusplus
}
#endif

#endif
