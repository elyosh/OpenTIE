#ifndef TIE_MODELBOUNDS_H
#define TIE_MODELBOUNDS_H

#include <stdint.h>

typedef struct TieFlightModelApi TieFlightModelApi;

int modelbounds_getmaxextent(uint16_t model_type);
int modelbounds_getmaxextent_from_api(const TieFlightModelApi* models, uint16_t model_type);
int modelbounds_getminx(uint16_t model_type);
int modelbounds_getminy(uint16_t model_type);
int modelbounds_getminz(uint16_t model_type);
int modelbounds_getmaxx(uint16_t model_type);
int modelbounds_getmaxy(uint16_t model_type);
int modelbounds_getmaxz(uint16_t model_type);
int modelbounds_getsizex(uint16_t model_type);
int modelbounds_getsizey(uint16_t model_type);
int modelbounds_getsizez(uint16_t model_type);

#endif
