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
#if defined(TIE95) && !defined(TIE_MODERN)
extern uint8_t install_cfg_mode;
#endif

void shell_programexit(const char* str) __attribute__((noreturn));

#ifdef __cplusplus
}
#endif

#endif
