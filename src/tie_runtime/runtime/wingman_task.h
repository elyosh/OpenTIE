#ifndef TIE_RUNTIME_WINGMAN_TASK_H
#define TIE_RUNTIME_WINGMAN_TASK_H

#include <stdbool.h>
#include <stdint.h>

typedef struct WingmanRoomState {
	int16_t selected_idx;
	int16_t prev_buttons;
	int16_t ret_delta;
	bool started;
	bool render;
	bool finished;
} WingmanRoomState;

/* Begin the wingman menu; completion publishes TieFlightRequest_SubmodalResult. */
void TieWingman_Begin(void);

#endif
