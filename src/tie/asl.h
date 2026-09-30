#ifndef TIE_ASL_H
#define TIE_ASL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void asl_Open_ASL(void);
void asl_Close_ASL(void);
int asl_Case_Bail(void);

#ifdef __cplusplus
}
#endif

#endif
