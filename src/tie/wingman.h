#ifndef __WINGMAN_H__
#define __WINGMAN_H__

#include <stdint.h>

/* Pointer to the 10-entry array of wingman command strings, bound by
 * FEDISKIO_loadstringdata after STRINGS.DAT is relocated. Byte +6 inside
 * each string is the hotkey character the menu forwards via inputkey. */
extern const char** wingmanstrings;

/* Push the in-flight wingman command menu as a tie_core task
 * (USER_inflightinfo screen 4). The task latches a screen-delta into
 * `user_submodal_result` before pop:
 *   -1 = previous in-flight screen (Up arrow)
 *    0 = a command was selected; inputkey carries the hotkey letter
 *   +1 = next in-flight screen (Down arrow)
 *   +2 = cancel (ESC / F1 / 'Q' / 'q' / 'w') */
void wingman_Push_WingmanRoom_Task(void);

#endif
