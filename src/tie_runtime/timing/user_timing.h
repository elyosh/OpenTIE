#ifndef TIE_RUNTIME_TIMING_USER_TIMING_H
#define TIE_RUNTIME_TIMING_USER_TIMING_H

#include <stdint.h>

int16_t TieUserTiming_ScaleValue(int32_t value, int32_t* remainder);
int32_t TieUserTiming_ScaleCompatibilityIncrement(int32_t value, uint16_t* remainder, int8_t* previous_sign);
int16_t TieUserTiming_SlewAxis(int16_t current, int16_t target, unsigned int axis);

#endif
