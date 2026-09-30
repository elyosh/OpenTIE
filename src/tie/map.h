#ifndef TIE_MAP_H
#define TIE_MAP_H

#include "tie/shellext.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* MAP/briefing display.
 * Scenes: 123=training, 133/134=combat sim, 181=briefing. */
int16_t map_Map(SceneHeadStruct* scene_head);

/* Distinct VR debrief counters; TIE95 folds both onto one body. */
int16_t Count_VR_Debrief_Header(void);
int16_t Count_VR_Debrief_Goals(void);

/* Training pilot medal status — extern per watdbg, set by MAP, read by SHELL */
extern int16_t train_pilot_medal_status;

#ifdef __cplusplus
}
#endif

#endif
