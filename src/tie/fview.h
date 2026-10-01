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

/* Turret-aim variant of the rotation block referenced from a turret
 * mesh's rotation_offset. First 6 bytes overlap ComponentRotData's
 * pivot triplet (the local-frame origin offset, Q15 with one extra
 * fractional bit -- callers extract via `>> 1`). Bytes +6..+11 are
 * not read by the turret-aim path. The trailing 12 bytes hold a 2x3
 * projection matrix (Q15 int16) mapping the turret-local point
 * (worldlocx, local_y, worldlocz) to the (aim_x, aim_y) screen-plane
 * vector consumed by trig2_arctan -> mesh_rotation byte. */
typedef struct TurretRotData {
	int16_t origin_x_q15;   /* +0x00 */
	int16_t origin_y_q15;   /* +0x02 */
	int16_t origin_z_q15;   /* +0x04 */
	int16_t reserved_06[3]; /* +0x06..+0x0B */
	int16_t aim_x_wx;       /* +0x0C: factor for worldlocx in aim_x */
	int16_t aim_x_ly;       /* +0x0E: factor for local_y in aim_x */
	int16_t aim_x_wz;       /* +0x10: factor for worldlocz in aim_x */
	int16_t aim_y_wx;       /* +0x12 */
	int16_t aim_y_ly;       /* +0x14 */
	int16_t aim_y_wz;       /* +0x16 */
} TurretRotData;            /* 24 bytes */

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
void fview_bwingrotation(int16_t angle, uint16_t part_id);

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
