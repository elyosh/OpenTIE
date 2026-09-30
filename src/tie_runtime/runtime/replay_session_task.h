#ifndef TIE_RUNTIME_REPLAY_SESSION_TASK_H
#define TIE_RUNTIME_REPLAY_SESSION_TASK_H

#include <stdbool.h>
#include <stdint.h>
typedef enum ReplayioPhase {
	REPLAYIO_PHASE_PREPARE,
	REPLAYIO_PHASE_ENTER,
	REPLAYIO_PHASE_VIEW,
	REPLAYIO_PHASE_AFTER_VIEWER,
	REPLAYIO_PHASE_AFTER_REENTERSIM,
} ReplayioPhase;
typedef struct ReplayioTask {
	int16_t saved_res;
	bool finished;
	ReplayioPhase phase;
} ReplayioTask;

void TieReplaySession_Begin(void);

#endif
