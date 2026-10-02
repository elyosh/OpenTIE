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

int32_t help_helproom(int16_t start_right_col);

#ifdef __cplusplus
}
#endif

#endif
