#ifndef __IMUSE_TIMER_H__
#define __IMUSE_TIMER_H__

#include <stdint.h>

#include <imuse/handle.h>

/* The original services iMUSE every two approximately 4-ms PIT interrupts. Each
 * service advances the engine's logical clock by the host's nominal 8060 us. */
#define IM_SERVICE_PERIOD_US 8000
#define IM_LOGICAL_TICK_US 8060

int ImTimer_Init(imuse_t* im);
int ImTimer_Deinit(imuse_t* im);

/* Returns the logical duration of one iMUSE service tick. Sequencer tempo math
 * reads this value; host scheduling uses IM_SERVICE_PERIOD_US instead. */
int32_t ImTimer_GetUsecPerInt(imuse_t* im);

#endif /* __IMUSE_TIMER_H__ */
