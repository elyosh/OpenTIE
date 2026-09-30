#ifndef TIE_RUNTIME_DISPLAY_TIE98_DISPLAY_H
#define TIE_RUNTIME_DISPLAY_TIE98_DISPLAY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool Tie98Display_Startup(uint16_t initial_mode);
void Tie98Display_Shutdown(void);

/* Host-owned coverage plane for composing the TIE98 cockpit mask over the
 * GPU render target. Grows to at least `size` bytes; NULL on allocation
 * failure (the previous plane is retained). */
uint8_t* Tie98Display_ReserveCockpitCoverage(size_t size);

/* Per-projection map of emitted hardware vertices for the TIE98 mesh draw,
 * grown to at least `count` entries. */
int* Tie98Display_ReserveEmittedVertexMap(size_t count);

#endif
