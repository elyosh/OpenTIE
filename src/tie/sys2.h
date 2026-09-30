#ifndef TIE_SYS2_H
#define TIE_SYS2_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int sys2_checkctrlkey(void);
int16_t sys2_calclength(const uint8_t* s);

#ifdef __cplusplus
}
#endif

#endif
