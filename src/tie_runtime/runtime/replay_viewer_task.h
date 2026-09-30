#ifndef TIE_RUNTIME_REPLAY_VIEWER_TASK_H
#define TIE_RUNTIME_REPLAY_VIEWER_TASK_H

#include "tie_runtime/runtime/flight_screen.h"
#include <stdbool.h>
#include <stdint.h>
typedef struct ReplayScreenState {
	uint16_t last_chase_status;
	uint16_t last_track_status;
	bool started;
	bool pushed_subtask;
	bool save_requested;
	TieFlightScreen previous_screen;
} ReplayScreenState;

void TieReplayViewer_Begin(void);

#endif
