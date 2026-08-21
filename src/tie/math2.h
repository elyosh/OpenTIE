#ifndef __MATH2_H__
#define __MATH2_H__

#include <stdint.h>

int32_t math2_ABoverC32(int32_t a, int32_t b, int32_t c);
uint16_t math2_fraction(uint16_t val, uint16_t frac);
int32_t math2_longfraction(int32_t val, uint16_t frac);
int16_t math2_divide(uint16_t a, uint16_t b);
uint16_t math2_percentage(uint16_t a, uint16_t b);
uint16_t math2_longpercentage(uint32_t a, uint32_t b);
int16_t math2_getrandom(void);
void math2_setrandomseed(void);
uint16_t math2_mphconvert(int16_t speed, uint16_t divisor);
uint16_t math2_calcratio(uint16_t a, uint16_t b, uint16_t c);
int32_t math2_convertwdw(uint16_t val);
uint32_t math2_divide32u(uint32_t a, uint32_t b);
void math2_getradarcoord(int32_t dx, int32_t dy, int32_t dz);
int16_t math2_halfplane(int32_t x1, int32_t y1, int32_t x2, int32_t y2);

extern int16_t math2_remainder;
extern int16_t math2_randomseed;

/* Radar bound table: 37 (x,y) pairs clipping the 320x200 radar sweep.
 * watdbg marks it static in math2.c, but MOVE_moveobjects reads it for
 * the missile homing-rate table index -- so exposed here. */
extern const uint8_t radarmax320[74];

#endif
