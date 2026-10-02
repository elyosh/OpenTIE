#ifndef TIE_MODELBOUNDS_H
#define TIE_MODELBOUNDS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void modelbounds_ensurecached(int model_type);
int modelbounds_getmaxextent(int model_type);
int modelbounds_getminx(int model_type);
int modelbounds_getminy(int model_type);
int modelbounds_getminz(int model_type);
int modelbounds_getmaxx(int model_type);
int modelbounds_getmaxy(int model_type);
int modelbounds_getmaxz(int model_type);
int modelbounds_getsizex(int model_type);
int modelbounds_getsizey(uint16_t model_type);
int modelbounds_getsizez(int model_type);

#ifdef __cplusplus
}
#endif

#endif
