#ifndef TIE_CREDITS_INTERNAL_H
#define TIE_CREDITS_INTERNAL_H

#include <landru/actor.h>
#include <landru/memhdl.h>
#include <landru/rect.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Globals shared by the DOS and Windows credits callbacks. */
extern Rect credits_dirty_rect;
extern Actor* credits_red_bar;
extern Actor* credits_blue_bar;
extern int16_t credits_text_len;
extern int16_t credits_film_time;
extern int16_t credits_num_credit_lines;
extern int16_t credits_film_len;
extern LandruHandle credits_text;

void credits_Credit_Stars_To_Back(void);
void credits_Init_Credit_Info_tie98(void);
int16_t credits_draw_Credit_tie98(Actor* actor, Rect* bounds, Rect* clip, int16_t xoff, int16_t yoff,
								  int16_t refresh);

#ifdef __cplusplus
}
#endif

#endif
