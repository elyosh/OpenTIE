#ifndef __MOUSE2_H__
#define __MOUSE2_H__

#include <stdint.h>

int16_t mouse2_checkformouse(void);
int16_t mouse2_readmouse(int16_t* x, int16_t* y);
void mouse2_deltamouse(int16_t* dx, int16_t* dy);

#endif
