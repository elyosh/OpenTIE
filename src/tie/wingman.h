#ifndef TIE_WINGMAN_H
#define TIE_WINGMAN_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Pointer to the 10-entry array of wingman command strings, bound by
 * FEDISKIO_loadstringdata after STRINGS.DAT is relocated. Byte +6 inside
 * each string is the hotkey character the menu forwards via inputkey. */
extern const char** wingmanstrings;

/* Menu state shared by rendering and input; scheduling belongs to the host. */
typedef struct WingmanRoomState {
	int16_t selected_idx;
	int16_t prev_buttons;
	int16_t ret_delta;
} WingmanRoomState;

void wingman_OpenRoom(WingmanRoomState* state);
void wingman_render_page(int16_t selected_idx);
/* Poll result: 0 idle, 1 exit, 2 redraw. On exit, ret_delta is -1/+1
 * for adjacent screens, 0 for a selected command, or 2 for cancellation. */
int wingman_poll_once(WingmanRoomState* state);

#ifdef __cplusplus
}
#endif

#endif
