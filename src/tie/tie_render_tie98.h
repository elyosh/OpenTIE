#ifndef TIE_TIE_RENDER_TIE98_H
#define TIE_TIE_RENDER_TIE98_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* TIE98 0x592204: consumed by the next TIE_Update_Screen call. */
extern uint16_t g_flightInitialTextureCacheFlushPending;

void tie_updatescreen_tie98(void);

#ifdef __cplusplus
}
#endif

#endif
