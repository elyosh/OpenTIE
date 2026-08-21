#ifndef __MOVE_H__
#define __MOVE_H__

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct FlightObject;

/*
 * MOVE -- per-frame physics integrator.
 *
 * move_moveobjects is called once per sim tick from TIE_doframe. It advances
 * every populated slot of objects[NUM_OBJECTS] according to the object's
 * genus (ship, missile, simple projectile), handles death timers,
 * death-spin roll decay, external-push accumulators, linked-craft follow,
 * and missile homing.
 *
 * move_updatexyz integrates the per-frame world-velocity globals
 * (xmovedist / ymovedist / zmovedist; owned by trig2) into the given
 * object's world_{x,y,z} with a +/- 0x1000000 world-box clamp.
 */
void move_updatexyz(struct FlightObject* obj);
void move_moveobjects(void);

/* Logical flight-frame counter — incremented once per move_moveobjects
 * (one position-integration step), independent of host-tick rate.
 * Stamped into TieSnapshot.flight_frame so consumers can detect whether
 * the sim advanced between two host-tick snapshots. */
uint32_t move_flight_frame(void);

#endif
