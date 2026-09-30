#ifndef TIE_GAMESND_H
#define TIE_GAMESND_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int16_t gamesnd_Open_Pre_iMuse(void);
void gamesnd_Close_Pre_iMuse(void);

/* Advance iMUSE sequencing from the synthetic-clock delta. PCM rendering is
 * independently paced by the host output worker. No-op before iMUSE opens. */
void gamesnd_AdvanceAudio(int32_t elapsed_us);
void gamesnd_Set_CD_Volume(int volume);
bool gamesnd_SetMusicDuckingVolumePercent(int percent);

void gamesnd_game_Open_iMuse(void);
void gamesnd_game_Close_iMuse(void);
void gamesnd_game_Set_Front_Sound(void);
void gamesnd_game_Set_Flight_Sound(void);
void gamesnd_Transition_Sound(void);
void gamesnd_End_Transition_Sound(void);

/* Globals set by GAMESND */
extern int16_t frontendflag;

#ifdef __cplusplus
}
#endif

#endif
