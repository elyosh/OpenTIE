#ifndef TIE_MOUSE2_H
#define TIE_MOUSE2_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int16_t mouse2_checkformouse(void);
int16_t mouse2_readmouse(int16_t* x, int16_t* y);
void mouse2_deltamouse(int16_t* dx, int16_t* dy);

#ifdef __cplusplus
}
#endif

#endif
