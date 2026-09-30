#ifndef TIE_DELTADD_H
#define TIE_DELTADD_H

#include "landru/actor.h"
#include "landru/rect.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int16_t deltadd_Draw_Delta_Add_Actor(Actor* actor, Rect* draw_rect, Rect* clip_rect, int16_t off_x,
									 int16_t off_y, int16_t refresh);

#ifdef __cplusplus
}
#endif

#endif
