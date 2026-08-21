#ifndef __SHELL_H__
#define __SHELL_H__

/* Watcom C has no __attribute__; annotations here are advisory. */
#if defined(__WATCOMC__)
#define __attribute__(x)
#endif

#include <stdint.h>

#include "tie/shellext.h"

extern SceneHeadStruct* sHead_gbl;
extern int16_t digital_exists;

void shell_programexit(const char* str) __attribute__((noreturn));

/* Push the front-end ShellTask onto the task stack — the top-of-
 * stack task that drives scene dispatch + transitions. TieRuntime_Tick
 * steps whichever task is on top (ShellTask itself, or a
 * scene/modal task it has pushed). When ShellTask returns DONE
 * the stack empties and TieRuntime_IsActive() goes false. Called
 * from tie_init; not part of the shell-public API. */
void shell_session_begin(int16_t scene, int16_t script);

#endif
