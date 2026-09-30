#ifndef TIE_RUNTIME_HELP_TASK_H
#define TIE_RUNTIME_HELP_TASK_H

#include <stdbool.h>
#include <stdint.h>

typedef struct HelpRoomState {
	int16_t cursor_idx;
	int16_t previous_cursor;
	int16_t redraw_all;
	int16_t page_delta;
	uint16_t prev_buttons;
	int16_t row_step;
	int16_t group_gap;
	bool started;
	bool render;
	bool finished;
} HelpRoomState;

#include <stdint.h>

/* Begin the help menu; completion publishes user_submodal_result. */
void TieHelp_Begin(int32_t start_right_col);

#endif
