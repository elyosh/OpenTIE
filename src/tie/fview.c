/*
 * fview.c — 3D view/camera rotation engine.
 * Builds rotation matrices from Euler angles using Q15 fixed-point arithmetic.
 * Handles camera view, craft orientation, and articulated component rotation
 * (S-foils, corvette turrets, B-wing cockpit) via Rodrigues rotation formula.
 *
 * Original: D:\GAMES\XTIE\CODE\fview.c (names-only module, no debug records)
 */

#include "tie/fview.h"
#include "tie/transfm2.h"
#include "tie/trig2.h"
#include <stdint.h>

/* Globals owned by fview.c — saved rotation state for component rotations */
int32_t fview_sfoiltempA1, fview_sfoiltempA2, fview_sfoiltempA3;
int32_t fview_sfoiltempB1, fview_sfoiltempB2, fview_sfoiltempB3;
int32_t fview_sfoiltempC1, fview_sfoiltempC2, fview_sfoiltempC3;
int32_t fview_sfoiltemplightX, fview_sfoiltemplightY, fview_sfoiltemplightZ;
int32_t fview_sfoiltempx, fview_sfoiltempy, fview_sfoiltempz;

/* Globals from tie.c (declared centrally in tie.h). */
#include "tie/drawpol.h"
#include "tie/tie.h"

/* ---------- helpers ---------- */

/* Clamp a Q30 product to ±0x3FFF0000, then arithmetic-shift right by 15 → Q15 */
static inline int32_t q15_clamp_shift(int32_t val) {
	if (val >= 0x40000000)
		val = 0x3FFF0000;
	if (val <= -0x40000000)
		val = -0x3FFF0000;
	return val >> 15;
}

/* Save current rotworldeye / rotlight / objecteye into sfoiltemp */
static void save_rotation_state(void) {
	fview_sfoiltempA1 = rotworldeyeA1;
	fview_sfoiltempA2 = rotworldeyeA2;
	fview_sfoiltempA3 = rotworldeyeA3;
	fview_sfoiltempB1 = rotworldeyeB1;
	fview_sfoiltempB2 = rotworldeyeB2;
	fview_sfoiltempB3 = rotworldeyeB3;
	fview_sfoiltempC1 = rotworldeyeC1;
	fview_sfoiltempC2 = rotworldeyeC2;
	fview_sfoiltempC3 = rotworldeyeC3;
	fview_sfoiltemplightX = rotlightX;
	fview_sfoiltemplightY = rotlightY;
	fview_sfoiltemplightZ = rotlightZ;
	fview_sfoiltempx = objecteyex;
	fview_sfoiltempy = objecteyey;
	fview_sfoiltempz = objecteyez;
}

/*
 * Build a 3x3 Rodrigues rotation matrix from axis (x,y,z) and angle.
 * The axis must be a Q15 unit vector.  Two branches to handle cos >= 0
 * and cos < 0 separately to avoid fixed-point overflow.
 *
 * Matrix layout:  rot[row][col], row-major.
 *   row 0 = axis 'A' (first component of the axis-aligned frame)
 *   row 1 = axis 'B'
 *   row 2 = axis 'C'
 */
static void build_rodrigues(int32_t ax, int32_t ay, int32_t az, int16_t angle, int32_t rot[9]) {
	int32_t c = trig2_getsignedcos(angle);
	int32_t s = trig2_getsignedsin(angle);

	if (c >= 0) {
		int32_t omc = 0x7FFF - c; /* 1 - cos in Q15 */
		rot[0] = q15_clamp_shift((c * 32768) + omc * ((ax * ax) >> 15));
		rot[1] = q15_clamp_shift(((az * s) >> 15) * 32768 + omc * ((ay * ax) >> 15));
		rot[2] = q15_clamp_shift(-32768 * ((ay * s) >> 15) + omc * ((az * ax) >> 15));
		rot[3] = q15_clamp_shift(-32768 * ((az * s) >> 15) + omc * ((ay * ax) >> 15));
		rot[4] = q15_clamp_shift((c * 32768) + omc * ((ay * ay) >> 15));
		rot[5] = q15_clamp_shift(((ax * s) >> 15) * 32768 + omc * ((az * ay) >> 15));
		rot[6] = q15_clamp_shift(((ay * s) >> 15) * 32768 + omc * ((az * ax) >> 15));
		rot[7] = q15_clamp_shift(-32768 * ((ax * s) >> 15) + omc * ((az * ay) >> 15));
		rot[8] = q15_clamp_shift((c * 32768) + omc * ((az * az) >> 15));
	} else {
		int32_t nc = -c; /* |cos| */
		rot[0] = q15_clamp_shift((c * 32768) + ax * ax + nc * ((ax * ax) >> 15));
		rot[1] = q15_clamp_shift(((az * s) >> 15) * 32768 + ay * ax + nc * ((ay * ax) >> 15));
		rot[2] = q15_clamp_shift(-32768 * ((ay * s) >> 15) + az * ax + nc * ((az * ax) >> 15));
		rot[3] = q15_clamp_shift(-32768 * ((az * s) >> 15) + ay * ax + nc * ((ay * ax) >> 15));
		rot[4] = q15_clamp_shift((c * 32768) + ay * ay + nc * ((ay * ay) >> 15));
		rot[5] = q15_clamp_shift(((ax * s) >> 15) * 32768 + az * ay + nc * ((az * ay) >> 15));
		rot[6] = q15_clamp_shift(((ay * s) >> 15) * 32768 + az * ax + nc * ((az * ax) >> 15));
		rot[7] = q15_clamp_shift(-32768 * ((ax * s) >> 15) + az * ay + nc * ((az * ay) >> 15));
		rot[8] = q15_clamp_shift((c * 32768) + az * az + nc * ((az * az) >> 15));
	}
}

/* ---------- core pipeline ---------- */

// FUNCTION: TIE 0x263AC
void fview_calcrotatemove(int16_t heading, int16_t pitch, FlightObject* craft) {
	int16_t neg_pitch = -pitch;
	int16_t adj_heading = -16384 - heading;

	int16_t cos_pitch = trig2_getsignedcos(neg_pitch);
	int16_t cos_heading = trig2_getsignedcos(adj_heading);
	int16_t sin_pitch = trig2_getsignedsin(neg_pitch);
	int16_t sin_heading = trig2_getsignedsin(adj_heading);

	calcS1 = cos_pitch;
	calcS2 = sin_pitch;
	calcS3 = 0;
	calcf1 = (cos_heading * -sin_pitch) >> 15;
	calcf2 = (cos_heading * cos_pitch) >> 15;
	calcf3 = sin_heading;
	calcU1 = -((sin_heading * sin_pitch) >> 15);
	calcU2 = -((sin_heading * -cos_pitch) >> 15);
	calcU3 = -cos_heading;

	craftmoveX = -calcf1;
	craftmoveY = -calcf2;
	craftmoveZ = -sin_heading;

	if (craft) {
		craft->move_dirty = 0;
		craft->moveX = craftmoveX;
		craft->moveY = craftmoveY;
		craft->moveZ = craftmoveZ;
	}
}

// FUNCTION: TIE 0x264DC
void fview_calcrotateorient(int16_t roll, int16_t bank, FlightObject* craft) {
	fview_transformaxes(calcU1, calcU2, calcU3, bank);
	fview_transformaxes(calcf1, calcf2, calcf3, roll);

	craftS1 = calcS1;
	craftS2 = calcS2;
	craftS3 = calcS3;
	craftf1 = -calcf1;
	craftf2 = -calcf2;
	craftf3 = -calcf3;
	craftU1 = calcU1;
	craftU2 = calcU2;
	craftU3 = calcU3;

	if (craft) {
		craft->orient_dirty = 0;
		craft->fwd_x = craftf1;
		craft->fwd_y = craftf2;
		craft->fwd_z = craftf3;
		craft->side_x = craftS1;
		craft->side_y = craftS2;
		craft->side_z = craftS3;
		craft->up_x = craftU1;
		craft->up_y = craftU2;
		craft->up_z = craftU3;
	}
}

// FUNCTION: TIE 0x265F8
void fview_calcrotworldeye(void) {
	rotworldeyeA1 = q15_clamp_shift(worldeyeA1 * calcS1 + worldeyeB1 * calcS2 + worldeyeC1 * calcS3);
	rotworldeyeA2 = q15_clamp_shift(worldeyeA2 * calcS1 + worldeyeB2 * calcS2 + worldeyeC2 * calcS3);
	rotworldeyeA3 = q15_clamp_shift(worldeyeA3 * calcS1 + worldeyeB3 * calcS2 + worldeyeC3 * calcS3);
	rotworldeyeB1 = q15_clamp_shift(worldeyeA1 * calcf1 + worldeyeB1 * calcf2 + worldeyeC1 * calcf3);
	rotworldeyeB2 = q15_clamp_shift(worldeyeA2 * calcf1 + worldeyeB2 * calcf2 + worldeyeC2 * calcf3);
	rotworldeyeB3 = q15_clamp_shift(worldeyeA3 * calcf1 + worldeyeB3 * calcf2 + worldeyeC3 * calcf3);
	rotworldeyeC1 = q15_clamp_shift(worldeyeA1 * calcU1 + worldeyeB1 * calcU2 + worldeyeC1 * calcU3);
	rotworldeyeC2 = q15_clamp_shift(worldeyeA2 * calcU1 + worldeyeB2 * calcU2 + worldeyeC2 * calcU3);
	rotworldeyeC3 = q15_clamp_shift(worldeyeA3 * calcU1 + worldeyeB3 * calcU2 + worldeyeC3 * calcU3);

	if (lightflag) {
		rotlightX = q15_clamp_shift(lightX * calcS1 + lightY * calcS2 + lightZ * calcS3);
		rotlightY = q15_clamp_shift(lightX * calcf1 + lightY * calcf2 + lightZ * calcf3);
		rotlightZ = q15_clamp_shift(lightX * calcU1 + lightY * calcU2 + lightZ * calcU3);
	} else {
		rotlightX = lightX;
		rotlightY = lightY;
		rotlightZ = lightZ;
	}
}

// FUNCTION: TIE 0x287F4
void fview_transformaxes(int32_t axis_x, int32_t axis_y, int32_t axis_z, int16_t angle) {
	int32_t rot[9];
	int32_t new_S1, new_S2, new_U1, new_U2, new_f1, new_f2;

	if (!angle)
		return;

	build_rodrigues(axis_x, axis_y, axis_z, angle, rot);

	new_S1 = q15_clamp_shift(rot[0] * calcS1 + rot[3] * calcS2 + rot[6] * calcS3);
	new_S2 = q15_clamp_shift(rot[1] * calcS1 + rot[4] * calcS2 + rot[7] * calcS3);
	calcS3 = q15_clamp_shift(rot[2] * calcS1 + rot[5] * calcS2 + rot[8] * calcS3);
	calcS1 = new_S1;
	calcS2 = new_S2;

	new_U1 = q15_clamp_shift(rot[0] * calcU1 + rot[3] * calcU2 + rot[6] * calcU3);
	new_U2 = q15_clamp_shift(rot[1] * calcU1 + rot[4] * calcU2 + rot[7] * calcU3);
	calcU3 = q15_clamp_shift(rot[2] * calcU1 + rot[5] * calcU2 + rot[8] * calcU3);
	calcU1 = new_U1;
	calcU2 = new_U2;

	new_f1 = q15_clamp_shift(rot[0] * calcf1 + rot[3] * calcf2 + rot[6] * calcf3);
	new_f2 = q15_clamp_shift(rot[1] * calcf1 + rot[4] * calcf2 + rot[7] * calcf3);
	calcf3 = q15_clamp_shift(rot[2] * calcf1 + rot[5] * calcf2 + rot[8] * calcf3);
	calcf1 = new_f1;
	calcf2 = new_f2;
}

// FUNCTION: TIE 0x26140
void fview_newcalcview(int16_t roll, int16_t heading, int16_t pitch, int16_t bank, int16_t side_angle,
					   int16_t up_angle, FlightObject* craft) {
	int32_t neg_U1, neg_U2, neg_U3;

	fview_calcrotatemove(heading, pitch, craft);
	fview_calcrotateorient(roll, bank, craft);

	calcf1 = -calcf1;
	calcf2 = -calcf2;
	calcf3 = -calcf3;
	calcU1 = -calcU1;
	calcU2 = -calcU2;
	calcU3 = -calcU3;

	neg_U1 = calcU1;
	neg_U2 = calcU2;
	neg_U3 = calcU3;

	fview_transformaxes(calcS1, calcS2, calcS3, side_angle);
	fview_transformaxes(neg_U1, neg_U2, neg_U3, up_angle);

	worldeyeA1 = calcS1;
	worldeyeB1 = calcS2;
	worldeyeC1 = calcS3;
	worldeyeA2 = calcU1;
	worldeyeB2 = calcU2;
	worldeyeC2 = calcU3;
	worldeyeA3 = calcf1;
	worldeyeB3 = calcf2;
	worldeyeC3 = calcf3;
}

// FUNCTION: TIE 0x26258
void fview_newcalcrotate(int16_t roll, int16_t heading, int16_t pitch, int16_t bank, FlightObject* craft) {
	if (craft) {
		if (craft->orient_dirty) {
			fview_calcrotatemove(heading, pitch, craft);
			fview_calcrotateorient(roll, bank, craft);
			fview_calcrotworldeye();
			return;
		}
		/* Read cached orientation from struct */
		craftf1 = craft->fwd_x;
		craftf2 = craft->fwd_y;
		craftf3 = craft->fwd_z;
		craftS1 = craft->side_x;
		craftS2 = craft->side_y;
		craftS3 = craft->side_z;
		craftU1 = craft->up_x;
		craftU2 = craft->up_y;
		craftU3 = craft->up_z;
		/* Forward is stored negated; negate back for calc */
		calcS1 = craftS1;
		calcS2 = craftS2;
		calcS3 = craftS3;
		calcf1 = -craftf1;
		calcf2 = -craftf2;
		calcf3 = -craftf3;
		calcU1 = craftU1;
		calcU2 = craftU2;
		calcU3 = craftU3;
	} else {
		fview_calcrotatemove(heading, pitch, NULL);
		fview_calcrotateorient(roll, bank, NULL);
	}
	fview_calcrotworldeye();
}

// FUNCTION: TIE 0x28100
void fview_restorerotation(void) {
	rotworldeyeA1 = fview_sfoiltempA1;
	rotworldeyeA2 = fview_sfoiltempA2;
	rotworldeyeA3 = fview_sfoiltempA3;
	rotworldeyeB1 = fview_sfoiltempB1;
	rotworldeyeB2 = fview_sfoiltempB2;
	rotworldeyeB3 = fview_sfoiltempB3;
	rotworldeyeC1 = fview_sfoiltempC1;
	rotworldeyeC2 = fview_sfoiltempC2;
	rotworldeyeC3 = fview_sfoiltempC3;
	rotlightX = fview_sfoiltemplightX;
	rotlightY = fview_sfoiltemplightY;
	rotlightZ = fview_sfoiltemplightZ;
	objecteyex = fview_sfoiltempx;
	objecteyey = fview_sfoiltempy;
	objecteyez = fview_sfoiltempz;
}

/* ---------- ship-specific component rotations ---------- */

// FUNCTION: TIE 0x269F4
void fview_sfoilrotation(int16_t angle) {
	int32_t sin_a, cos_a, neg_sin;

	save_rotation_state();

	sin_a = trig2_getsignedsin(angle);
	cos_a = trig2_getsignedcos(angle);
	neg_sin = -sin_a;

	/* Rotate A and C rows around B axis */
	rotworldeyeA1 = q15_clamp_shift(fview_sfoiltempA1 * cos_a + fview_sfoiltempC1 * neg_sin);
	rotworldeyeA2 = q15_clamp_shift(fview_sfoiltempA2 * cos_a + fview_sfoiltempC2 * neg_sin);
	rotworldeyeA3 = q15_clamp_shift(fview_sfoiltempA3 * cos_a + fview_sfoiltempC3 * neg_sin);
	rotworldeyeC1 = q15_clamp_shift(fview_sfoiltempA1 * sin_a + fview_sfoiltempC1 * cos_a);
	rotworldeyeC2 = q15_clamp_shift(fview_sfoiltempA2 * sin_a + fview_sfoiltempC2 * cos_a);
	rotworldeyeC3 = q15_clamp_shift(fview_sfoiltempA3 * sin_a + fview_sfoiltempC3 * cos_a);
}

// FUNCTION: TIE 0x26C24
void fview_corvettegunrotation(int16_t angle) {
	int32_t sin_a, cos_a, neg_sin;

	save_rotation_state();

	sin_a = trig2_getsignedsin(angle);
	cos_a = trig2_getsignedcos(angle);
	neg_sin = -sin_a;

	/* Rotate A and B rows around C axis */
	rotworldeyeA1 = q15_clamp_shift(fview_sfoiltempA1 * cos_a + fview_sfoiltempB1 * neg_sin);
	rotworldeyeA2 = q15_clamp_shift(fview_sfoiltempA2 * cos_a + fview_sfoiltempB2 * neg_sin);
	rotworldeyeA3 = q15_clamp_shift(fview_sfoiltempA3 * cos_a + fview_sfoiltempB3 * neg_sin);
	rotworldeyeB1 = q15_clamp_shift(fview_sfoiltempA1 * sin_a + fview_sfoiltempB1 * cos_a);
	rotworldeyeB2 = q15_clamp_shift(fview_sfoiltempA2 * sin_a + fview_sfoiltempB2 * cos_a);
	rotworldeyeB3 = q15_clamp_shift(fview_sfoiltempA3 * sin_a + fview_sfoiltempB3 * cos_a);
}

// FUNCTION: TIE 0x26E54
void fview_bwingrotation(int16_t angle, uint16_t part_id) {
	int32_t cos_a, sin_a, neg_sin;
	int32_t lat_offset, vert_offset;
	int32_t x_fwd, z_side;
	int32_t pivot_lat, pivot_z;
	int32_t rot_fwd, adj_z;
	int32_t dot;

	save_rotation_state();

	if (angle == 0x4000) {
		/* 90-degree special case — hardcoded offsets */
		if (part_id == 5) {
			lat_offset = -92;
			vert_offset = -8;
		} else if (part_id == 4) {
			lat_offset = 92;
			vert_offset = -8;
		} else {
			lat_offset = 0;
			vert_offset = 0;
		}

		x_fwd = vert_offset - 170;
		z_side = 170 - lat_offset;

		/* Translate objecteye via A and C columns */
		objecteyex += q15_clamp_shift(rotworldeyeA1 * x_fwd + rotworldeyeC1 * z_side);
		objecteyey += q15_clamp_shift(rotworldeyeA2 * x_fwd + rotworldeyeC2 * z_side);
		objecteyez += q15_clamp_shift(rotworldeyeA3 * x_fwd + rotworldeyeC3 * z_side);

		if (part_id == 5) {
			/* Identity — no rotation change */
		} else if (part_id == 4) {
			/* 180-degree: negate A and C */
			rotworldeyeA1 = -fview_sfoiltempA1;
			rotworldeyeA2 = -fview_sfoiltempA2;
			rotworldeyeA3 = -fview_sfoiltempA3;
			rotworldeyeC1 = -fview_sfoiltempC1;
			rotworldeyeC2 = -fview_sfoiltempC2;
			rotworldeyeC3 = -fview_sfoiltempC3;
		} else {
			/* 90-degree: A = -C, C = A */
			rotworldeyeA1 = -fview_sfoiltempC1;
			rotworldeyeA2 = -fview_sfoiltempC2;
			rotworldeyeA3 = -fview_sfoiltempC3;
			rotworldeyeC1 = fview_sfoiltempA1;
			rotworldeyeC2 = fview_sfoiltempA2;
			rotworldeyeC3 = fview_sfoiltempA3;
		}
		return;
	}

	/* General angle case */
	sin_a = trig2_getsignedsin(angle);
	cos_a = trig2_getsignedcos(angle);
	neg_sin = -sin_a;

	if (part_id == 5) {
		dot = q15_clamp_shift(-50 * sin_a + 42 * cos_a);
		pivot_z = q15_clamp_shift(50 * cos_a + 42 * sin_a) - 50;
		pivot_lat = dot - 42;
	} else if (part_id == 4) {
		dot = q15_clamp_shift(50 * sin_a - 42 * cos_a);
		pivot_z = q15_clamp_shift(50 * cos_a + 42 * sin_a) - 50;
		pivot_lat = dot + 42;
	} else {
		pivot_z = 0;
		pivot_lat = 0;
	}

	adj_z = pivot_z - 170;
	rot_fwd = q15_clamp_shift(adj_z * sin_a + pivot_lat * cos_a);
	adj_z = q15_clamp_shift(adj_z * cos_a + pivot_lat * neg_sin) + 170;

	/* Translate objecteye via A and C columns */
	objecteyex += q15_clamp_shift(rotworldeyeA1 * rot_fwd + rotworldeyeC1 * adj_z);
	objecteyey += q15_clamp_shift(rotworldeyeA2 * rot_fwd + rotworldeyeC2 * adj_z);
	objecteyez += q15_clamp_shift(rotworldeyeA3 * rot_fwd + rotworldeyeC3 * adj_z);

	if (part_id == 5) {
		/* No rotation change */
	} else {
		if (part_id == 4) {
			/* Use double angle for the rotation matrix */
			sin_a = trig2_getsignedsin(2 * angle);
			cos_a = trig2_getsignedcos(2 * angle);
			neg_sin = -sin_a;
		}
		/* Rotate A and C rows */
		rotworldeyeA1 = q15_clamp_shift(fview_sfoiltempA1 * cos_a + fview_sfoiltempC1 * neg_sin);
		rotworldeyeA2 = q15_clamp_shift(fview_sfoiltempA2 * cos_a + fview_sfoiltempC2 * neg_sin);
		rotworldeyeA3 = q15_clamp_shift(fview_sfoiltempA3 * cos_a + fview_sfoiltempC3 * neg_sin);
		rotworldeyeC1 = q15_clamp_shift(fview_sfoiltempA1 * sin_a + fview_sfoiltempC1 * cos_a);
		rotworldeyeC2 = q15_clamp_shift(fview_sfoiltempA2 * sin_a + fview_sfoiltempC2 * cos_a);
		rotworldeyeC3 = q15_clamp_shift(fview_sfoiltempA3 * sin_a + fview_sfoiltempC3 * cos_a);
	}
}

/* ---------- general component rotation ---------- */

// FUNCTION: TIE 0x2759C
void fview_componentrotation(int16_t angle, const ShipModelMesh* mesh) {
	int32_t axis_x, axis_y, axis_z;
	int32_t pivot_raw, pivot_x, pivot_z;
	int32_t rot[9];
	int32_t disp_a, disp_b, disp_c;
	const ComponentRotData* rd;

	save_rotation_state();

	if (mission.train_craft_type) {
		axis_x = 0x7FFF;
		axis_y = 0;
		axis_z = 0;
		pivot_raw = 0;
		pivot_z = 0;
		pivot_x = -(mesh->center_fwd >> 1);
	} else {
		rd = (const ComponentRotData*)((const uint8_t*)mesh + mesh->rotation_offset);

		pivot_raw = rd->pivot_value;
		axis_y = rd->axis_y;
		axis_x = rd->axis_x;
		axis_z = rd->axis_z;

		/* Both the pivot and vertex data use model_scale_shift. */
		if (objectblockptr->model_scale_shift) {
			int8_t shift = (int8_t)objectblockptr->model_scale_shift - 1;
			pivot_raw = pivot_raw << shift;
			pivot_z = rd->pivot_z << shift;
			pivot_x = rd->pivot_x << shift;
		} else {
			pivot_raw = pivot_raw >> 1;
			pivot_x = rd->pivot_x >> 1;
			pivot_z = rd->pivot_z >> 1;
		}
	}

	build_rodrigues(axis_y, axis_x, axis_z, angle, rot);

	/* Compute pivot displacement: disp = pivot + R * (-pivot) */
	disp_a = pivot_raw + (int32_t)(((int64_t)rot[0] * -pivot_raw) >> 15) +
			 (int32_t)(((int64_t)rot[3] * -pivot_x) >> 15) + (int32_t)(((int64_t)rot[6] * -pivot_z) >> 15);
	disp_b = pivot_x + (int32_t)(((int64_t)rot[1] * -pivot_raw) >> 15) +
			 (int32_t)(((int64_t)rot[4] * -pivot_x) >> 15) + (int32_t)(((int64_t)rot[7] * -pivot_z) >> 15);
	disp_c = pivot_z + (int32_t)(((int64_t)rot[2] * -pivot_raw) >> 15) +
			 (int32_t)(((int64_t)rot[5] * -pivot_x) >> 15) + (int32_t)(((int64_t)rot[8] * -pivot_z) >> 15);

	/* Translate objecteye through rotworldeye */
	objecteyex += (int32_t)(((int64_t)rotworldeyeA1 * disp_a) >> 15) +
				  (int32_t)(((int64_t)rotworldeyeB1 * disp_b) >> 15) +
				  (int32_t)(((int64_t)rotworldeyeC1 * disp_c) >> 15);
	objecteyey += (int32_t)(((int64_t)rotworldeyeA2 * disp_a) >> 15) +
				  (int32_t)(((int64_t)rotworldeyeB2 * disp_b) >> 15) +
				  (int32_t)(((int64_t)rotworldeyeC2 * disp_c) >> 15);
	objecteyez += (int32_t)(((int64_t)rotworldeyeA3 * disp_a) >> 15) +
				  (int32_t)(((int64_t)rotworldeyeB3 * disp_b) >> 15) +
				  (int32_t)(((int64_t)rotworldeyeC3 * disp_c) >> 15);

	/* Rotate rotworldeye = saved * R */
	rotworldeyeA1 =
		q15_clamp_shift(fview_sfoiltempA1 * rot[0] + fview_sfoiltempB1 * rot[1] + fview_sfoiltempC1 * rot[2]);
	rotworldeyeA2 =
		q15_clamp_shift(fview_sfoiltempA2 * rot[0] + fview_sfoiltempB2 * rot[1] + fview_sfoiltempC2 * rot[2]);
	rotworldeyeA3 =
		q15_clamp_shift(fview_sfoiltempA3 * rot[0] + fview_sfoiltempB3 * rot[1] + fview_sfoiltempC3 * rot[2]);
	rotworldeyeB1 =
		q15_clamp_shift(fview_sfoiltempA1 * rot[3] + fview_sfoiltempB1 * rot[4] + fview_sfoiltempC1 * rot[5]);
	rotworldeyeB2 =
		q15_clamp_shift(fview_sfoiltempA2 * rot[3] + fview_sfoiltempB2 * rot[4] + fview_sfoiltempC2 * rot[5]);
	rotworldeyeB3 =
		q15_clamp_shift(fview_sfoiltempA3 * rot[3] + fview_sfoiltempB3 * rot[4] + fview_sfoiltempC3 * rot[5]);
	rotworldeyeC1 =
		q15_clamp_shift(fview_sfoiltempA1 * rot[6] + fview_sfoiltempB1 * rot[7] + fview_sfoiltempC1 * rot[8]);
	rotworldeyeC2 =
		q15_clamp_shift(fview_sfoiltempA2 * rot[6] + fview_sfoiltempB2 * rot[7] + fview_sfoiltempC2 * rot[8]);
	rotworldeyeC3 =
		q15_clamp_shift(fview_sfoiltempA3 * rot[6] + fview_sfoiltempB3 * rot[7] + fview_sfoiltempC3 * rot[8]);

	/* Rotate light direction */
	rotlightX = q15_clamp_shift(fview_sfoiltemplightX * rot[0] + fview_sfoiltemplightY * rot[1] +
								fview_sfoiltemplightZ * rot[2]);
	rotlightY = q15_clamp_shift(fview_sfoiltemplightX * rot[3] + fview_sfoiltemplightY * rot[4] +
								fview_sfoiltemplightZ * rot[5]);
	rotlightZ = q15_clamp_shift(fview_sfoiltemplightX * rot[6] + fview_sfoiltemplightY * rot[7] +
								fview_sfoiltemplightZ * rot[8]);
}

// FUNCTION: TIE 0x28198
void fview_comprotatepoint(int16_t angle, const ShipModelMesh* mesh, int32_t point_x, int32_t point_y,
						   int32_t point_z) {
	const ComponentRotData* rd;
	int32_t axis_x, axis_y, axis_z;
	int32_t rot[9];
	int32_t pivot_half, pivot_xv, pivot_zv;
	int32_t rel_x, rel_y, rel_z;
	int32_t rx, ry, rz;

	rd = (const ComponentRotData*)((const uint8_t*)mesh + mesh->rotation_offset);

	axis_y = rd->axis_y;
	axis_x = rd->axis_x;
	axis_z = rd->axis_z;

	build_rodrigues(axis_y, axis_x, axis_z, angle, rot);

	pivot_half = rd->pivot_value >> 1;
	pivot_xv = rd->pivot_x >> 1;
	pivot_zv = rd->pivot_z >> 1;

	/* Relative point = point - pivot */
	rel_x = point_x - pivot_half;
	rel_y = point_y - pivot_xv;
	rel_z = point_z - pivot_zv;

	/* Rotate relative point by R */
	rx = q15_clamp_shift(rot[0] * rel_x + rot[3] * rel_y + rot[6] * rel_z);
	ry = q15_clamp_shift(rot[1] * rel_x + rot[4] * rel_y + rot[7] * rel_z);
	rz = q15_clamp_shift(rot[2] * rel_x + rot[5] * rel_y + rot[8] * rel_z);

	/* Result = rotated + pivot */
	rotatedx = pivot_half + rx;
	rotatedy = pivot_xv + ry;
	rotatedz = pivot_zv + rz;
}
