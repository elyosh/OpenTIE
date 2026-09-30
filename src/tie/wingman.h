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

int32_t wingman_wingmanroom(void);

#ifdef __cplusplus
}
#endif

#endif
