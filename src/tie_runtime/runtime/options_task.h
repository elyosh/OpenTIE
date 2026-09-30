#ifndef TIE_RUNTIME_OPTIONS_TASK_H
#define TIE_RUNTIME_OPTIONS_TASK_H

#include "tie/option.h"
#include <stdbool.h>
typedef struct OptionRoomState {
	uint8_t values[OPTION_ROW_COUNT];
	uint8_t max_values[OPTION_ROW_COUNT];
	uint8_t kind_offsets[OPTION_ROW_COUNT];
	uint16_t prev_buttons;
	int16_t selection;
	int16_t previous_selection;
	int16_t redraw_all;
	int16_t exit_code;
	bool started, render, finished;
} OptionRoomState;

/* Open the classic 14-row DOS options editor. */
void TieOptions_Begin(void);

#endif
