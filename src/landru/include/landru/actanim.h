#ifndef LANDRU_ACTANIM_H
#define LANDRU_ACTANIM_H

#include <stdbool.h>
#include <stdint.h>

#include <landru/actor.h>

void lactanim_Create_Anim_Actor_Module(void);
void lactanim_Destroy_Anim_Actor_Module(void);
void lactanim_Get_Anim_Actor_Frame(Actor* actor, Rect* outFrame);
void lactanim_Set_Anim_Actor_State(Actor* actor, int16_t state, int16_t stateFract);
Actor* lactanim_Alloc_Anim_Actor(void* frames, Rect* rect, int16_t x, int16_t y, int16_t z);
Actor* lactanim_Res_Anim_Actor(const char* resName, Rect* rect, int16_t x, int16_t y, int16_t z);
void lactanim_Init_Anim_Actor(Actor* actor, void* frames, Rect* rect, int16_t x, int16_t y, int16_t z);
void lactanim_Get_Anim_Actor_Bounds(Actor* actor, Rect* outRect);
int lactanim_Draw_Anim_Actor(Actor* actor, Rect* clip, Rect* dest, int16_t xoff, int16_t yoff,
							 int16_t refresh);
void lactanim_Update_Anim_Actor(Actor* actor);

#endif
