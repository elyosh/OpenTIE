#ifndef TIE_HELP_H
#define TIE_HELP_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Bounds of the two-column command grid, including SVGA colour-group gaps. */
extern int32_t helpTop;
extern int32_t helpBottom;

/*
 * 48-entry pointer tables into stringdata_buf (STRINGS.DAT), populated by
 * fediskio_loadstringdata. [i] is the key-name / description for command i:
 *   helpkeystrings[i]    - left-column label ("[ESC]", "FIRE", ...)
 *   helpscreenstrings[i] - right-column descriptive text
 */
extern char** helpkeystrings;
extern char** helpscreenstrings;

typedef struct HelpRoomState {
	int16_t cursor_idx;
	int16_t previous_cursor;
	int16_t redraw_all;
	int16_t page_delta;
	uint16_t prev_buttons;
	int16_t row_step;
	int16_t group_gap;
} HelpRoomState;

/* Nonzero start_right_col selects row 24 at room entry. */
void help_OpenRoom(HelpRoomState* state, int32_t start_right_col);
void help_render_rows(HelpRoomState* state);
/* Poll result: 0 idle, 1 exit, 2 redraw. On exit, page_delta is -1/+1
 * for adjacent screens or 0 for cancellation. */
int help_poll_once(HelpRoomState* state);

#ifdef __cplusplus
}
#endif

#endif
