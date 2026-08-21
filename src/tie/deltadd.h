#ifndef __DELTADD_H__
#define __DELTADD_H__

#include <stdint.h>

#include "landru/actor.h"
#include "landru/rect.h"

int deltadd_Draw_Delta_Add_Actor(Actor* actor, Rect* draw_rect, Rect* clip_rect, int16_t off_x, int16_t off_y,
								 int16_t refresh);

#endif
