#ifndef TIE_FLIGHT_SURFACE_TIE98_H
#define TIE_FLIGHT_SURFACE_TIE98_H

#include <stdint.h>

extern int g_flightDrawToOffscreenSurface;

int FrontendDisplay_GetDrawSurfacePitch(void);
int FlightSurface_GetLockCount(void);
void FlightSurface_Lock(void);
void FlightSurface_Unlock(void);

#endif
