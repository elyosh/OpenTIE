#ifndef __GAMESND_H__
#define __GAMESND_H__

#include <stdint.h>

int16_t gamesnd_Open_Pre_iMuse(void);
void gamesnd_Close_Pre_iMuse(void);

/* Advance iMUSE sequencing from the synthetic-clock delta. PCM rendering is
 * independently paced by the host output worker. No-op before iMUSE opens. */
void gamesnd_AdvanceAudio(int32_t elapsed_us);
void gamesnd_Set_CD_Volume(int volume);

/* Engine-glow palette cycle driver. Reproduces the second half of
 * retail GAMESND_Host_Int (0x88adc..0x88bcf): on its own PIT-tick
 * cursor, decrements a countdown and on expiry advances a 5-phase
 * wheel that rewrites VGA DAC slots 0xF8/0xF9/0xFA. Cadence is 18
 * PIT ticks (~72 ms) by default, 36 (~144 ms) when colorcycleuserflag
 * is set. Gated by (palette_cycle_user & colorcycleflag) ||
 * colorcycleuserflag. Called once per TieRuntime_Tick from runtime.c. */
void gamesnd_drive_palette_cycle(void);

void gamesnd_game_Open_iMuse(void);
void gamesnd_game_Close_iMuse(void);
void gamesnd_game_Set_Front_Sound(void);
void gamesnd_game_Set_Flight_Sound(void);
void gamesnd_Transition_Sound(void);
void gamesnd_End_Transition_Sound(void);

/* Globals set by GAMESND */
extern int16_t frontendflag;

#endif
