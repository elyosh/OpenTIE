#ifndef TIE_RUNTIME_GOALS_TASK_H
#define TIE_RUNTIME_GOALS_TASK_H

#include <stdbool.h>
#include <stdint.h>

typedef struct GoalsRoomState {
	int16_t scroll_y;
	int16_t content_height;
	int16_t nav_code;
	bool started;
	bool render;
	bool finished;
} GoalsRoomState;

void TieGoals_Begin(void);

#endif
