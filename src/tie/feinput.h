#ifndef __FEINPUT_H__
#define __FEINPUT_H__

#include <stdint.h>

void feinput_checkinput(void);
void feinput_degitterinput(void);
void feinput_getinput(void);
void feinput_clearinput(void);
void feinput_waitpress(void);
void feinput_waitrelease(void);
void feinput_setupinputdevices(void);
uint16_t feinput_getrawinput(void);
void feinput_setupgraphics(uint8_t detail_level);
void feinput_SetGraphicsPtrs(uint8_t mode);
int8_t FlightInput_GetChar(void);

/* FEINPUT globals */
/* Three modes with thirteen graphics callbacks each. */
extern void* graphroutines[39];
extern int16_t buffer256flag;
extern int16_t thrustmastertopflag;

#endif
