#ifndef TIE_XTIMER_H
#define TIE_XTIMER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Read and consume elapsed 4 ms PIT ticks. */
#ifdef TIE_MODERN
uint32_t xtimer_time_elapsed(void);
#else
uint16_t xtimer_time_elapsed(void);
#endif

#ifdef __cplusplus
}
#endif

#endif
