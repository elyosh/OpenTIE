#ifndef __TALK_H__
#define __TALK_H__

#include "tie/shellext.h"
#include <stdint.h>

struct Sound;

/* Push the officer/priest talk scene as a tie_core task.
 * Scenes: 182/183 = briefing officer/priest, 191/192 = debrief officer/priest. */
void talk_Push_Talk_Task(SceneHeadStruct* scene_head);

/* Officer/priest mood control (0-4). Drives face animation. */
void talk_Set_Officer_Mood(int16_t mood);
int16_t talk_Get_Officer_Mood(void);

/* Reinitialize talk state for text-mode display (used by MAP module). */
void talk_Set_Talk_To_Text(void);
void talk_Set_Talk_Paragraph(void);

/* Question/paragraph text extraction (used by MAP module). */
void talk_Get_Talk_Question(char* out, int16_t id);
void talk_Get_Talk_Paragraph(char* out, int16_t line);

/* ----------------------------------------------------------------------
 * Voice-over machinery. Owned by talk.c; reused by map.c for the in-
 * flight VR talk overlay (retail does the same — TALK exports the
 * helpers and MAP_Map calls them). The streaming Sound and timer
 * are externs so map.c can drive them when no talk scene is active.
 * -------------------------------------------------------------------- */
extern struct Sound* talk_speech_sound;
extern int32_t talk_paragraph_timer; /* INT32_MAX = auto-advance off */
extern int16_t talk_voice_species;   /* >0 numeric, <0 char-encoded */
extern int16_t talk_voice_mission;
extern int16_t talk_voice_question; /* 1-based paragraph index */
extern uint8_t talk_voice_officer;  /* filename char: 'o','p','i' */
extern uint8_t talk_voice_mood;     /* filename char: 'b','d','h','o' */

void talk_Alloc_Speech_Sound(void);
void talk_Free_Speech_Sound(void);
void talk_Start_Speech_Stream(void);

/* Set talk_voice_species + talk_voice_mission for TALK_Talk. */
void talk_Set_Voice_Species_Mission(void);

#endif
