#ifndef LANDRU_ACTCUST_H
#define LANDRU_ACTCUST_H

#include <stdbool.h>
#include <stdint.h>

#include <landru/actor.h>
#include <landru/rect.h>

void lactcust_Create_Custom_Actor_Module(void);
void lactcust_Destroy_Custom_Actor_Module(void);
Actor* lactcust_Alloc_Custom_Actor(void* data, Rect* frame, int16_t x, int16_t y, int16_t z);
void lactcust_Init_Custom_Actor(Actor* actor, Rect* frame, int16_t x, int16_t y, int16_t z);
int lactcust_Draw_Custom_Actor(Actor* actor, Rect* rect, Rect* clipRect, int16_t x, int16_t y,
							   int16_t refresh);
void lactcust_Update_Custom_Actor(Actor* actor);

#endif
