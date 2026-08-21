#ifndef LANDRU_ACTRAW_H
#define LANDRU_ACTRAW_H

#include <stdint.h>

#include <landru/actor.h>

void lactraw_Create_Raw_Actor_Module(void);
void lactraw_Destroy_Raw_Actor_Module(void);
void lactraw_Get_Raw_Actor_Frame(Actor* actor, Rect* outFrame);
Actor* lactraw_Alloc_Raw_Actor(void* data, Rect* rect, int16_t x, int16_t y, int16_t z);
Actor* lactraw_Res_Raw_Actor(const char* resName, Rect* rect, int16_t x, int16_t y, int16_t z);
void lactraw_Init_Raw_Actor(Actor* actor, void* data, Rect* rect, int16_t x, int16_t y, int16_t z);
int lactraw_Draw_Raw_Actor(Actor* actor, Rect* clip, Rect* dest, int16_t xoff, int16_t yoff, int16_t refresh);
void lactraw_Update_Raw_Actor(Actor* actor);
int lactraw_Draw_Clipped_Raw(void* data, int16_t xoff, int16_t yoff, int dirty);
void* lactraw_Uncompress_Delta_Data(void* src);

#endif
