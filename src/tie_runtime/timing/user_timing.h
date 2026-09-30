#ifndef TIE_RUNTIME_TIMING_USER_TIMING_H
#define TIE_RUNTIME_TIMING_USER_TIMING_H

#include <stdbool.h>
#include <stdint.h>

int16_t TieUserTiming_ScaleValue(int32_t value, int32_t* remainder);
int32_t TieUserTiming_ScaleCompatibilityIncrement(int32_t value, uint16_t* remainder, int8_t* previous_sign);
int16_t TieUserTiming_SlewAxis(int16_t current, int16_t target, unsigned int axis);

/* Modern absolute throttle command: eligible while the player craft can fly
 * and the current key does not start an info-room replay payload. */
bool TieUserTiming_ThrottleCommandEligible(void);
/* Apply this frame's absolute throttle command (if any) and clear it. */
void TieUserTiming_ApplyThrottleCommand(void);

#endif
