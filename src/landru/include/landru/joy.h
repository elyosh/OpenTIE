#ifndef LANDRU_JOY_H
#define LANDRU_JOY_H

#include <stdint.h>

/* Set non-zero to force ljoy_Joystick_Read to return zeroed axes. */
extern int16_t ngstickflag;

/* Returns the number of joysticks detected (0..2). */
int16_t ljoy_Joystick_Init(void);

/* Read axes (-127..+127 with dead zone) and return first 4 buttons in
 * the low nibble. */
int16_t ljoy_Joystick_Read(int16_t* outX, int16_t* outY, int16_t port);

/* Read up to n_axes analog channels into axes[] (-127..+127 with dead
 * zone). Axis 0 = X / yaw, 1 = Y / pitch, 2 = roll (second-stick or
 * twist on devices that have it). Returns first 4 buttons in low
 * nibble. Hosts without a requested axis leave it zero. */
int16_t ljoy_Joystick_Read_Axes(int16_t* axes, int n_axes, int16_t port);

#endif
