#ifndef TIE_GAMESND_H
#define TIE_GAMESND_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int16_t gamesnd_Open_Pre_iMuse(void);
void gamesnd_Close_Pre_iMuse(void);

void* gamesnd_GetSoundAddr(intptr_t sound);
void gamesnd_Set_CD_Volume(int volume);

void gamesnd_game_Open_iMuse(void);
void gamesnd_game_Set_Front_Sound(void);
void gamesnd_game_Set_Flight_Sound(void);
void gamesnd_Transition_Sound(void);

/* Globals set by GAMESND */
extern int16_t frontendflag;

#ifdef __cplusplus
}
#endif

#endif
