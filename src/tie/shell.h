#ifndef TIE_SHELL_H
#define TIE_SHELL_H

/* Compiler annotations are advisory when GNU attributes are unavailable. */
#if defined(__WATCOMC__) || (defined(_MSC_VER) && !defined(__clang__))
#define __attribute__(x)
#endif

#include "tie/shellext.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

extern SceneHeadStruct* sHead_gbl;
extern int16_t digital_exists;
#ifndef TIE_MODERN
extern uint8_t install_cfg_mode;
#endif

int32_t shell_Shell(int32_t scene, int32_t script);

void shell_programexit(const char* str) __attribute__((noreturn));
#ifdef __WATCOMC__
#pragma aux shell_programexit aborts;
#endif

#ifdef __cplusplus
}
#endif

#endif
