#ifndef TIE_MODELBOUNDS_H
#define TIE_MODELBOUNDS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int modelbounds_getmaxextent(uint16_t model_type);
int modelbounds_getminx(uint16_t model_type);
int modelbounds_getminy(uint16_t model_type);
int modelbounds_getminz(uint16_t model_type);
int modelbounds_getmaxx(uint16_t model_type);
int modelbounds_getmaxy(uint16_t model_type);
int modelbounds_getmaxz(uint16_t model_type);
int modelbounds_getsizex(uint16_t model_type);
int modelbounds_getsizey(uint16_t model_type);
int modelbounds_getsizez(uint16_t model_type);

#ifdef __cplusplus
}
#endif

#endif
