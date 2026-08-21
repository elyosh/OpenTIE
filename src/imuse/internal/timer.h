#ifndef __IMUSE_TIMER_H__
#define __IMUSE_TIMER_H__

#include <stdint.h>

#include <imuse/handle.h>

/* The host supplies elapsed time to imuse_advance. Sequencer and wave work run
 * on a fixed 8060-us internal cadence independent of host update frequency. */

/* Internal-tick period in microseconds. Sets the granularity at
 * which MIDI sequencer steps + wave frame pumps fire. Independent
 * of (and decoupled from) the host's imuse_advance call rate. */
#define IM_USEC_PER_INT 8060

int ImTimer_Init(imuse_t* im);
int ImTimer_Deinit(imuse_t* im);

/* Returns the engine's internal-tick period (= IM_USEC_PER_INT).
 * Sequencer tempo math reads this. */
int32_t ImTimer_GetUsecPerInt(imuse_t* im);

#endif /* __IMUSE_TIMER_H__ */
