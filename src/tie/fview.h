#ifndef TIE_FVIEW_H
#define TIE_FVIEW_H

/* Full definition in tie.h — include it for FlightObject */
#include "tie/tie.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Engine angles are 16-bit binary angles. Heading is the world-frame angle in
 * the XY plane and pitch the angle of the forward vector from world +Z:
 * 0 is straight up, 0x4000 level and 0x8000 straight down. Yaw and roll turn
 * the resulting basis about its own up and forward axes. */

/* Camera view matrix from Euler angles, then look offsets about the view's
 * side axis (look pitch) and up axis (look yaw) */
void fview_newcalcview(uint16_t roll, uint16_t pitch, uint16_t heading, uint16_t yaw, uint16_t side_angle,
					   uint16_t up_angle, FlightObject* craft);

/* Craft rotation from Euler angles with optional cached orientation */
void fview_newcalcrotate(uint16_t roll, uint16_t pitch, uint16_t heading, uint16_t yaw, FlightObject* craft);

/* Build S/f/U basis vectors from pitch + heading */
void fview_calcrotatemove(uint16_t pitch, uint16_t heading, FlightObject* craft);

/* Apply yaw + roll rotations to craft orientation */
void fview_calcrotateorient(uint16_t roll, uint16_t yaw, FlightObject* craft);

/* Matrix multiply rotworldeye = worldeye * calc + light transform */
void fview_calcrotworldeye(void);

/* Rodrigues rotation of calc S/U/f around arbitrary axis */
void fview_transformaxes(int32_t axis_x, int32_t axis_y, int32_t axis_z, uint16_t angle);

/* Per-component rotation data referenced from a ShipModelMesh's
 * rotation_offset. 12 bytes, naturally aligned (all int16). */
typedef struct ComponentRotData {
	int16_t pivot_value;
	int16_t pivot_x;
	int16_t pivot_z;
	int16_t axis_y;
	int16_t axis_x;
	int16_t axis_z;
} ComponentRotData;

/* Turret rotation block referenced from a turret mesh's rotation_offset,
 * laid out like the TIE98 OPT RotationScale: pivot, rotation axis, then the
 * direction and up axes that ANIM_updateanimation projects the target onto
 * before trig2_arctan. Pivot values carry one extra fractional bit (callers
 * use `>> 1`); axes are Q15. The first 6 bytes overlap ComponentRotData's
 * pivot triplet. */
typedef struct TurretRotData {
	int16_t pivot_x;         /* +0x00 */
	int16_t pivot_y;         /* +0x02 */
	int16_t pivot_z;         /* +0x04 */
	int16_t rotation_axis_x; /* +0x06 */
	int16_t rotation_axis_y; /* +0x08 */
	int16_t rotation_axis_z; /* +0x0A */
	int16_t direction_x;     /* +0x0C */
	int16_t direction_y;     /* +0x0E */
	int16_t direction_z;     /* +0x10 */
	int16_t up_x;            /* +0x12 */
	int16_t up_y;            /* +0x14 */
	int16_t up_z;            /* +0x16 */
} TurretRotData;             /* 24 bytes */

/* Articulated component rotation. `mesh` points at the on-disk ship-
 * model mesh entry; comp_rotation_offset locates the ComponentRotData
 * relative to it. */
void fview_componentrotation(int16_t angle, const ShipModelMesh* mesh);

/* Restore rotworldeye/light/objecteye from saved state */
void fview_restorerotation(void);

/* Rotate a point around a component pivot */
void fview_comprotatepoint(int16_t angle, const ShipModelMesh* mesh, int32_t point_x, int32_t point_y,
						   int32_t point_z);

/* S-foil rotation around B axis */
void fview_sfoilrotation(int16_t angle);

/* Corvette turret rotation around C axis */
void fview_corvettegunrotation(int16_t angle);

/* B-wing cockpit rotation */
void fview_bwingrotation(uint16_t angle, uint16_t part_id);

/* Saved rotation matrix (Q15 fixed-point) */
extern int32_t sfoiltempA1, sfoiltempA2, sfoiltempA3;
extern int32_t sfoiltempB1, sfoiltempB2, sfoiltempB3;
extern int32_t sfoiltempC1, sfoiltempC2, sfoiltempC3;

/* Saved light direction (Q15) */
extern int32_t sfoiltemplightX, sfoiltemplightY, sfoiltemplightZ;

/* Saved objecteye position (Q15) */
extern int32_t sfoiltempx, sfoiltempy, sfoiltempz;

#ifdef __cplusplus
}
#endif

#endif
