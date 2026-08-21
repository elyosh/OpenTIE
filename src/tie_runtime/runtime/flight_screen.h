#ifndef TIE_RUNTIME_FLIGHT_SCREEN_H
#define TIE_RUNTIME_FLIGHT_SCREEN_H

#include "tie_runtime/snapshot/snapshot_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Tracks the currently presented full-screen flight UI. Tasks save the
 * returned value and restore it when they pop so nested screens compose. */
void TieFlightScreen_Reset(void);
TieFlightScreen TieFlightScreen_Active(void);
TieFlightScreen TieFlightScreen_SetActive(TieFlightScreen screen);

#ifdef __cplusplus
}
#endif

#endif /* TIE_RUNTIME_FLIGHT_SCREEN_H */
