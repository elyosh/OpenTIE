/*
 * HELP — in-flight cockpit help room (USER_inflightinfo page 5).
 *
 * Port of help.c from the Watcom binary. Single public entry point plus the
 * module-owned globals that hold the 48-entry command reference table.
 */

#ifndef TIE_HELP_H
#define TIE_HELP_H

#include <stdint.h>

/*
 * Pixel Y bounds of the 24-row help grid. Set at the top of help_helproom:
 *   helpTop    = 44 in the 640x480 flight modes, 18 otherwise.
 *   helpBottom = helpTop + 24 * (fontheight + 2).
 * Exposed because watdbg lists them as extern (source-level) globals of
 * help.c; no other module currently reads them.
 */
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

/*
 * Push the 48-entry command-reference grid as a tie_core task (2 cols x
 * 24 rows). The task latches a navigation code into
 * `user_submodal_result` before pop:
 *   -1 = page backward (up-exit from the left column)
 *    0 = pick / ESC (stay and return to caller's outer loop)
 *   +1 = page forward (down-exit from the right column)
 *
 * start_right_col != 0 seeds the cursor at row 24 (top of the right
 * column) so a user page-walking in from the previous screen lands on
 * the first entry they see.
 */
void help_Push_HelpRoom_Task(int32_t start_right_col);

#endif /* TIE_HELP_H */
