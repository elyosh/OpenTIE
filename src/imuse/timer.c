#include "internal/timer.h"

#include <imuse/commands.h>

/* Timing cadence is fixed; initialization and teardown require no state. */

int ImTimer_Init(imuse_t* im) {
	(void)im;
	return 0;
}

int ImTimer_Deinit(imuse_t* im) {
	(void)im;
	return 0;
}

int32_t ImTimer_GetUsecPerInt(imuse_t* im) {
	(void)im;
	return IM_LOGICAL_TICK_US;
}
