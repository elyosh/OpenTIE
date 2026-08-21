#ifndef LANDRU_ACTDELT_H
#define LANDRU_ACTDELT_H

#include <stdbool.h>
#include <stdint.h>

#include <landru/actor.h>

void lactdelt_Create_Delta_Actor_Module(void);
void lactdelt_Destroy_Delta_Actor_Module(void);
void lactdelt_Get_Delta_Actor_Frame(Actor* actor, Rect* outFrame);
Actor* lactdelt_Alloc_Delta_Actor(void* data, Rect* rect, int16_t x, int16_t y, int16_t z);
Actor* lactdelt_Res_Delta_Actor(const char* resName, Rect* rect, int16_t x, int16_t y, int16_t z);
void lactdelt_Init_Delta_Actor(Actor* actor, void* data, Rect* rect, int16_t x, int16_t y, int16_t z);
int lactdelt_Draw_Delta_Actor(Actor* actor, Rect* clip, Rect* dest, int16_t xoff, int16_t yoff,
							  int16_t refresh);
void lactdelt_Update_Delta_Actor(Actor* actor);
int lactdelt_Draw_Clipped_Delta(void* data, int16_t x, int16_t y, int dirty);
int lactdelt_Draw_Flipped_Clipped_Delta(Actor* actor, void* data, int16_t xoff, int16_t yoff, int dirty);
int lactdelt_Draw_Scaled_Clipped_Delta(Actor* actor, void* data, int16_t xoff, int16_t yoff, int dirty);
int lactdelt_Draw_Color_Clipped_Delta(void* data, int16_t xoff, int16_t color, int16_t yoff, int dirty);

#endif
