#ifndef TIE_RUNTIME_DISPLAY_TIE98_STARFIELD_H
#define TIE_RUNTIME_DISPLAY_TIE98_STARFIELD_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* TIE98 RTSVGA2_drawstars keeps its random star positions and its indexed
 * and packed-color tables in Memory_AllocHandle blocks (g_starPositionHandle,
 * g_starColor8Handle, g_starColor16Handle). The port backs those blocks with
 * fixed storage that lives for the whole process, preserving their lifetime
 * and separation. */
#define TIE98_STARFIELD_STAR_COUNT 3072

extern uint8_t g_tie98StarPositionIndex[TIE98_STARFIELD_STAR_COUNT];
extern uint8_t g_tie98StarColor8[TIE98_STARFIELD_STAR_COUNT];
extern uint16_t g_tie98StarColor16[TIE98_STARFIELD_STAR_COUNT];

/* Force the star color tables to be rebuilt after a palette or 16-bit
 * display-format change. */
void Tie98StarColors_Invalidate(void);

#ifdef __cplusplus
}
#endif

#endif
