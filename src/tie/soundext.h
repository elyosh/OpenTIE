#ifndef __SOUNDEXT_H__
#define __SOUNDEXT_H__

#include <stdint.h>

#include "landru/sound.h"

/* SoundSFXType — from watdbg debug info (1-based into Sound_SFX_Name[19]) */
typedef enum {
	sfxNull = 0,
	sfxSmallDoorOpen = 1,  /* "door-1" (randomized to 1-4 at play time) */
	sfxSmallDoorOpen1 = 2, /* "door-1a" */
	sfxSmallDoorOpen2 = 3, /* "door-1b" */
	sfxSmallDoorOpen3 = 4, /* "door-1c" */
	sfxSmallDoorShut = 5,  /* "dr-cls-1" */
	sfxLargeDoorOpen = 6,  /* "door-1" */
	sfxLargeDoorShut = 7,  /* "dr-cls-1" */
	sfxGunCock = 8,        /* "guncock" */
	sfxLogon = 9,          /* "log-on-c" */
	sfxVisor = 10,         /* "visor-1b" */
	sfxButton = 11,        /* "button-1" */
	sfxButton2 = 12,       /* "button-2" */
	sfxTarget1 = 13,       /* "target-4" */
	sfxTarget2 = 14,       /* "target-5" */
	sfxPressureDoor = 15,  /* "door-6" */
	sfxVisorClick = 16,    /* "vis-clk2" */
	sfxText = 17,          /* "text-5" */
	sfxAirLock = 18,       /* "door-5" */
	sfxKaWhoosh = 19,      /* "cannon-1" */
} SoundSFX;

/* SoundSpeechType — from watdbg debug info (1-based into Sound_Speech_Name[1]) */
typedef enum {
	speechNull = 0,
	speechRegisterGuard = 1, /* "reg1" */
} SoundSpeech;

/* Sound scene list entry: [scene, type, time, arg] */
/* type: 0=cue, 1=state, 2=sequence */
/* time: -1 = pre-scene setup, >=0 = real-time trigger */

void soundext_Open_Post_iMuse(int16_t use_script);
void soundext_Close_Post_iMuse(void);
void soundext_Open_Sound_Scene(int16_t scene);
void soundext_Close_Sound_Scene(int16_t scene, int16_t next_scene);
void soundext_Prep_Sound_Scene(int16_t next_scene);
void soundext_Play_SFX(uint8_t sound_index, int16_t volume);
void soundext_Fade_SFX(uint8_t sound_index, int16_t volume, int16_t time);
void soundext_Stop_SFX(uint8_t sound_index);
void soundext_Play_Speech(uint8_t sound_index);
void soundext_compact_Sound(int16_t post_compaction);
void soundext_Action_iMuse(int16_t state, Sound* the_sound, int16_t var1, int16_t var2);
void* soundext_TIE_Load_Sound(const char* name);
void soundext_TIE_Unload_Sound(void* sound);
void soundext_TIE_Print_Msg(const char* ptr);

#endif
