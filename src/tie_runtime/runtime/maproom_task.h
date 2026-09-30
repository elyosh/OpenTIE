#ifndef TIE_RUNTIME_MAPROOM_TASK_H
#define TIE_RUNTIME_MAPROOM_TASK_H

#include <stdbool.h>
#include <stdint.h>
typedef struct MaproomState {
	uint16_t view_mode;
	uint16_t view_transition_progress;
	int view_transition_active;
	int16_t view_pitch;
	int16_t view_heading;
	int32_t camera_distance;
	int8_t page_delta;
	int buffer_toggle;
	uint16_t focus_obj_ref;
	bool started, render, finished, waiting;
} MaproomState;

void TieMaproom_Begin(void);

#endif
