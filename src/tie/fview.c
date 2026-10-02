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
#include "tie_runtime/runtime/wide_arithmetic.h"

/* Saved rotation state for component rotations. */
// GLOBAL: TIE95 0xD4AC0
int32_t sfoiltempy;
// GLOBAL: TIE95 0xD4AC4
int32_t sfoiltempz;
// GLOBAL: TIE95 0xD4AC8
int32_t sfoiltemplightZ;
// GLOBAL: TIE95 0xD4ACC
int32_t sfoiltemplightY;
// GLOBAL: TIE95 0xD4AD0
int32_t sfoiltemplightX;
// GLOBAL: TIE95 0xD4AD4
int32_t sfoiltempA1;
// GLOBAL: TIE95 0xD4AD8
int32_t sfoiltempA2;
// GLOBAL: TIE95 0xD4ADC
int32_t sfoiltempA3;
// GLOBAL: TIE95 0xD4AE0
int32_t sfoiltempC1;
// GLOBAL: TIE95 0xD4AE4
int32_t sfoiltempC2;
// GLOBAL: TIE95 0xD4AE8
int32_t sfoiltempC3;
// GLOBAL: TIE95 0xD4AEC
int32_t sfoiltempx;
// GLOBAL: TIE95 0xD4AF0
int32_t sfoiltempB1;
// GLOBAL: TIE95 0xD4AF4
int32_t sfoiltempB2;
// GLOBAL: TIE95 0xD4AF8
int32_t sfoiltempB3;

/* Globals from tie.c (declared centrally in tie.h). */
#include "tie/drawpol.h"
#include "tie/tie.h"

#include <stdint.h>

// FUNCTION: TIE95 0x26140
void fview_newcalcview(uint16_t roll, uint16_t pitch, uint16_t heading, uint16_t yaw, uint16_t side_angle,
					   uint16_t up_angle, FlightObject* craft) {
	int32_t neg_U1, neg_U2, neg_U3;

	fview_calcrotatemove(pitch, heading, craft);
	fview_calcrotateorient(roll, yaw, craft);

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

// FUNCTION: TIE95 0x26258
void fview_newcalcrotate(uint16_t roll, uint16_t pitch, uint16_t heading, uint16_t yaw, FlightObject* craft) {
	if (craft == NULL) {
		fview_calcrotatemove(pitch, heading, craft);
		fview_calcrotateorient(roll, yaw, craft);
	} else if (craft->orient_dirty) {
		fview_calcrotatemove(pitch, heading, craft);
		fview_calcrotateorient(roll, yaw, craft);
		fview_calcrotworldeye();
		return;
	} else {
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
		calcf1 = -craftf1;
		calcf2 = -craftf2;
		calcf3 = -craftf3;
		calcS1 = craftS1;
		calcS2 = craftS2;
		calcS3 = craftS3;
		calcU1 = craftU1;
		calcU2 = craftU2;
		calcU3 = craftU3;
	}
	fview_calcrotworldeye();
}

// FUNCTION: TIE95 0x263AC
void fview_calcrotatemove(uint16_t pitch, uint16_t heading, FlightObject* craft) {
	int16_t cos_heading, cos_pitch, sin_heading, sin_pitch;

	heading = -heading;
	pitch = 0xC000 - pitch;

	cos_heading = trig2_getsignedcos((int16_t)heading);
	cos_pitch = trig2_getsignedcos((int16_t)pitch);
	sin_heading = trig2_getsignedsin((int16_t)heading);
	sin_pitch = trig2_getsignedsin((int16_t)pitch);

	calcS1 = cos_heading;
	calcS2 = sin_heading;
	calcS3 = 0;
	calcf1 = (-calcS2 * cos_pitch) >> 15;
	calcf2 = (calcS1 * cos_pitch) >> 15;
	calcf3 = sin_pitch;
	calcU1 = (calcS2 * calcf3) >> 15;
	calcU1 = -calcU1;
	calcU2 = (-calcS1 * calcf3) >> 15;
	calcU2 = -calcU2;
	calcU3 = cos_pitch;
	calcU3 = -calcU3;

	craftmoveX = calcf1;
	craftmoveX = -craftmoveX;
	craftmoveY = calcf2;
	craftmoveY = -craftmoveY;
	craftmoveZ = calcf3;
	craftmoveZ = -craftmoveZ;

	if (craft) {
		craft->move_dirty = 0;
		craft->moveX = craftmoveX;
		craft->moveY = craftmoveY;
		craft->moveZ = craftmoveZ;
	}
}

// FUNCTION: TIE95 0x264DC
void fview_calcrotateorient(uint16_t roll, uint16_t yaw, FlightObject* craft) {
	fview_transformaxes(calcU1, calcU2, calcU3, yaw);
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

// FUNCTION: TIE95 0x265F8
void fview_calcrotworldeye(void) {
	rotworldeyeA1 = math2_dot3_q15_clamped(calcS1, calcS2, calcS3, worldeyeA1, worldeyeB1, worldeyeC1);
	rotworldeyeA2 = math2_dot3_q15_clamped(calcS1, calcS2, calcS3, worldeyeA2, worldeyeB2, worldeyeC2);
	rotworldeyeA3 = math2_dot3_q15_clamped(calcS1, calcS2, calcS3, worldeyeA3, worldeyeB3, worldeyeC3);
	rotworldeyeB1 = math2_dot3_q15_clamped(calcf1, calcf2, calcf3, worldeyeA1, worldeyeB1, worldeyeC1);
	rotworldeyeB2 = math2_dot3_q15_clamped(calcf1, calcf2, calcf3, worldeyeA2, worldeyeB2, worldeyeC2);
	rotworldeyeB3 = math2_dot3_q15_clamped(calcf1, calcf2, calcf3, worldeyeA3, worldeyeB3, worldeyeC3);
	rotworldeyeC1 = math2_dot3_q15_clamped(calcU1, calcU2, calcU3, worldeyeA1, worldeyeB1, worldeyeC1);
	rotworldeyeC2 = math2_dot3_q15_clamped(calcU1, calcU2, calcU3, worldeyeA2, worldeyeB2, worldeyeC2);
	rotworldeyeC3 = math2_dot3_q15_clamped(calcU1, calcU2, calcU3, worldeyeA3, worldeyeB3, worldeyeC3);

	if (lightflag) {
		rotlightX = math2_dot3_q15_clamped(calcS1, calcS2, calcS3, lightX, lightY, lightZ);
		rotlightY = math2_dot3_q15_clamped(calcf1, calcf2, calcf3, lightX, lightY, lightZ);
		rotlightZ = math2_dot3_q15_clamped(calcU1, calcU2, calcU3, lightX, lightY, lightZ);
	} else {
		rotlightX = lightX;
		rotlightY = lightY;
		rotlightZ = lightZ;
	}
}

/* ---------- ship-specific component rotations ---------- */

// FUNCTION: TIE95 0x269F4
void fview_sfoilrotation(int16_t angle) {
	int32_t temp;
	int16_t sin_a, cos_a;
	int32_t neg_sin;

	sfoiltempA1 = rotworldeyeA1;
	sfoiltempA2 = rotworldeyeA2;
	sfoiltempA3 = rotworldeyeA3;
	sfoiltempB1 = rotworldeyeB1;
	sfoiltempB2 = rotworldeyeB2;
	sfoiltempB3 = rotworldeyeB3;
	sfoiltempC1 = rotworldeyeC1;
	sfoiltempC2 = rotworldeyeC2;
	sfoiltempC3 = rotworldeyeC3;
	sfoiltemplightX = rotlightX;
	sfoiltemplightY = rotlightY;
	sfoiltemplightZ = rotlightZ;
	sfoiltempx = objecteyex;
	sfoiltempy = objecteyey;
	sfoiltempz = objecteyez;

	sin_a = trig2_getsignedsin((int16_t)angle);
	cos_a = trig2_getsignedcos((int16_t)angle);
	neg_sin = -sin_a;

	/* Rotate A and C rows around B axis */
	temp = sfoiltempA1 * cos_a + sfoiltempC1 * neg_sin;
	if (temp >= 0x40000000)
		temp = 0x3FFF0000;
	if (temp <= -0x40000000)
		temp = -0x3FFF0000;
	rotworldeyeA1 = temp >> 15;
	temp = sfoiltempA2 * cos_a + sfoiltempC2 * neg_sin;
	if (temp >= 0x40000000)
		temp = 0x3FFF0000;
	if (temp <= -0x40000000)
		temp = -0x3FFF0000;
	rotworldeyeA2 = temp >> 15;
	temp = sfoiltempA3 * cos_a + sfoiltempC3 * neg_sin;
	if (temp >= 0x40000000)
		temp = 0x3FFF0000;
	if (temp <= -0x40000000)
		temp = -0x3FFF0000;
	rotworldeyeA3 = temp >> 15;
	temp = sfoiltempA1 * sin_a + sfoiltempC1 * cos_a;
	if (temp >= 0x40000000)
		temp = 0x3FFF0000;
	if (temp <= -0x40000000)
		temp = -0x3FFF0000;
	rotworldeyeC1 = temp >> 15;
	temp = sfoiltempA2 * sin_a + sfoiltempC2 * cos_a;
	if (temp >= 0x40000000)
		temp = 0x3FFF0000;
	if (temp <= -0x40000000)
		temp = -0x3FFF0000;
	rotworldeyeC2 = temp >> 15;
	temp = sfoiltempA3 * sin_a + sfoiltempC3 * cos_a;
	if (temp >= 0x40000000)
		temp = 0x3FFF0000;
	if (temp <= -0x40000000)
		temp = -0x3FFF0000;
	rotworldeyeC3 = temp >> 15;
}

// FUNCTION: TIE95 0x26C24
void fview_corvettegunrotation(int16_t angle) {
	int32_t temp;
	int16_t sin_a, cos_a, neg_sin;

	sfoiltempA1 = rotworldeyeA1;
	sfoiltempA2 = rotworldeyeA2;
	sfoiltempA3 = rotworldeyeA3;
	sfoiltempB1 = rotworldeyeB1;
	sfoiltempB2 = rotworldeyeB2;
	sfoiltempB3 = rotworldeyeB3;
	sfoiltempC1 = rotworldeyeC1;
	sfoiltempC2 = rotworldeyeC2;
	sfoiltempC3 = rotworldeyeC3;
	sfoiltemplightX = rotlightX;
	sfoiltemplightY = rotlightY;
	sfoiltemplightZ = rotlightZ;
	sfoiltempx = objecteyex;
	sfoiltempy = objecteyey;
	sfoiltempz = objecteyez;

	sin_a = trig2_getsignedsin(angle);
	cos_a = trig2_getsignedcos(angle);
	neg_sin = -sin_a;

	/* Rotate A and B rows around C axis */
	temp = cos_a * sfoiltempA1 + neg_sin * sfoiltempB1;
	if (temp >= 0x40000000)
		temp = 0x3FFF0000;
	if (temp <= -0x40000000)
		temp = -0x3FFF0000;
	rotworldeyeA1 = temp >> 15;
	temp = cos_a * sfoiltempA2 + neg_sin * sfoiltempB2;
	if (temp >= 0x40000000)
		temp = 0x3FFF0000;
	if (temp <= -0x40000000)
		temp = -0x3FFF0000;
	rotworldeyeA2 = temp >> 15;
	temp = cos_a * sfoiltempA3 + neg_sin * sfoiltempB3;
	if (temp >= 0x40000000)
		temp = 0x3FFF0000;
	if (temp <= -0x40000000)
		temp = -0x3FFF0000;
	rotworldeyeA3 = temp >> 15;
	temp = sin_a * sfoiltempA1 + cos_a * sfoiltempB1;
	if (temp >= 0x40000000)
		temp = 0x3FFF0000;
	if (temp <= -0x40000000)
		temp = -0x3FFF0000;
	rotworldeyeB1 = temp >> 15;
	temp = sin_a * sfoiltempA2 + cos_a * sfoiltempB2;
	if (temp >= 0x40000000)
		temp = 0x3FFF0000;
	if (temp <= -0x40000000)
		temp = -0x3FFF0000;
	rotworldeyeB2 = temp >> 15;
	temp = sin_a * sfoiltempA3 + cos_a * sfoiltempB3;
	if (temp >= 0x40000000)
		temp = 0x3FFF0000;
	if (temp <= -0x40000000)
		temp = -0x3FFF0000;
	rotworldeyeB3 = temp >> 15;
}

// FUNCTION: TIE95 0x26E54
void fview_bwingrotation(uint16_t angle, uint16_t part_id) {
	int32_t temp;
	int32_t cos_a, sin_a, neg_sin;
	int32_t lat, fwd;
	int32_t pivot_lat, pivot_fwd;
	int32_t x, y, z;

	sfoiltempA1 = rotworldeyeA1;
	sfoiltempA2 = rotworldeyeA2;
	sfoiltempA3 = rotworldeyeA3;
	sfoiltempB1 = rotworldeyeB1;
	sfoiltempB2 = rotworldeyeB2;
	sfoiltempB3 = rotworldeyeB3;
	sfoiltempC1 = rotworldeyeC1;
	sfoiltempC2 = rotworldeyeC2;
	sfoiltempC3 = rotworldeyeC3;
	sfoiltemplightX = rotlightX;
	sfoiltemplightY = rotlightY;
	sfoiltemplightZ = rotlightZ;
	sfoiltempx = objecteyex;
	sfoiltempy = objecteyey;
	sfoiltempz = objecteyez;

	if (angle == 0x4000) {
		/* 90-degree special case — hardcoded offsets */
		y = 0;
		if (part_id == 5) {
			lat = -92;
			fwd = -8;
		} else if (part_id == 4) {
			lat = 92;
			fwd = -8;
		} else {
			lat = 0;
			fwd = 0;
		}

		x = fwd - 170;
		z = -lat + 170;

		/* Translate objecteye */
		objecteyex += math2_dot3_q15_clamped(x, y, z, rotworldeyeA1, rotworldeyeB1, rotworldeyeC1);
		objecteyey += math2_dot3_q15_clamped(x, y, z, rotworldeyeA2, rotworldeyeB2, rotworldeyeC2);
		objecteyez += math2_dot3_q15_clamped(x, y, z, rotworldeyeA3, rotworldeyeB3, rotworldeyeC3);

		if (part_id == 5) {
			/* Identity — no rotation change */
		} else if (part_id == 4) {
			/* 180-degree: negate A and C */
			rotworldeyeA1 = -sfoiltempA1;
			rotworldeyeA2 = -sfoiltempA2;
			rotworldeyeA3 = -sfoiltempA3;
			rotworldeyeC1 = -sfoiltempC1;
			rotworldeyeC2 = -sfoiltempC2;
			rotworldeyeC3 = -sfoiltempC3;
		} else {
			/* 90-degree: A = -C, C = A */
			rotworldeyeA1 = -sfoiltempC1;
			rotworldeyeA2 = -sfoiltempC2;
			rotworldeyeA3 = -sfoiltempC3;
			rotworldeyeC1 = sfoiltempA1;
			rotworldeyeC2 = sfoiltempA2;
			rotworldeyeC3 = sfoiltempA3;
		}
		return;
	}

	/* General angle case */
	sin_a = trig2_getsignedsin((int16_t)angle);
	cos_a = trig2_getsignedcos((int16_t)angle);
	neg_sin = -sin_a;
	y = 0;

	if (part_id == 5) {
		pivot_lat = 42;
		pivot_fwd = 50;
		temp = pivot_fwd * neg_sin + pivot_lat * cos_a;
		if (temp >= 0x40000000)
			temp = 0x3FFF0000;
		if (temp <= -0x40000000)
			temp = -0x3FFF0000;
		lat = temp >> 15;
		temp = pivot_lat * -neg_sin + pivot_fwd * cos_a;
		if (temp >= 0x40000000)
			temp = 0x3FFF0000;
		if (temp <= -0x40000000)
			temp = -0x3FFF0000;
		fwd = (temp >> 15) - 50;
		lat -= 42;
	} else if (part_id == 4) {
		pivot_lat = -42;
		pivot_fwd = 50;
		temp = pivot_fwd * sin_a + pivot_lat * cos_a;
		if (temp >= 0x40000000)
			temp = 0x3FFF0000;
		if (temp <= -0x40000000)
			temp = -0x3FFF0000;
		lat = temp >> 15;
		temp = neg_sin * pivot_lat + cos_a * pivot_fwd;
		if (temp >= 0x40000000)
			temp = 0x3FFF0000;
		if (temp <= -0x40000000)
			temp = -0x3FFF0000;
		fwd = (temp >> 15) - 50;
		lat += 42;
	} else {
		fwd = 0;
		lat = 0;
	}

	fwd -= 170;
	temp = sin_a * fwd + cos_a * lat;
	if (temp >= 0x40000000)
		temp = 0x3FFF0000;
	if (temp <= -0x40000000)
		temp = -0x3FFF0000;
	x = temp >> 15;
	temp = lat * neg_sin + fwd * cos_a;
	if (temp >= 0x40000000)
		temp = 0x3FFF0000;
	if (temp <= -0x40000000)
		temp = -0x3FFF0000;
	z = (temp >> 15) + 170;

	/* Translate objecteye */
	objecteyex += math2_dot3_q15_clamped(x, y, z, rotworldeyeA1, rotworldeyeB1, rotworldeyeC1);
	objecteyey += math2_dot3_q15_clamped(x, y, z, rotworldeyeA2, rotworldeyeB2, rotworldeyeC2);
	objecteyez += math2_dot3_q15_clamped(x, y, z, rotworldeyeA3, rotworldeyeB3, rotworldeyeC3);

	if (part_id == 5) {
		/* No rotation change */
	} else {
		if (part_id == 4) {
			/* Use double angle for the rotation matrix */
			angle *= 2;
			sin_a = trig2_getsignedsin((int16_t)angle);
			cos_a = trig2_getsignedcos((int16_t)angle);
			neg_sin = -sin_a;
		}
		/* Rotate A and C rows */
		temp = sfoiltempA1 * cos_a + sfoiltempC1 * neg_sin;
		if (temp >= 0x40000000)
			temp = 0x3FFF0000;
		if (temp <= -0x40000000)
			temp = -0x3FFF0000;
		rotworldeyeA1 = temp >> 15;
		temp = sfoiltempA2 * cos_a + sfoiltempC2 * neg_sin;
		if (temp >= 0x40000000)
			temp = 0x3FFF0000;
		if (temp <= -0x40000000)
			temp = -0x3FFF0000;
		rotworldeyeA2 = temp >> 15;
		temp = sfoiltempC3 * neg_sin + sfoiltempA3 * cos_a;
		if (temp >= 0x40000000)
			temp = 0x3FFF0000;
		if (temp <= -0x40000000)
			temp = -0x3FFF0000;
		rotworldeyeA3 = temp >> 15;
		temp = sfoiltempA1 * sin_a + sfoiltempC1 * cos_a;
		if (temp >= 0x40000000)
			temp = 0x3FFF0000;
		if (temp <= -0x40000000)
			temp = -0x3FFF0000;
		rotworldeyeC1 = temp >> 15;
		temp = sfoiltempA2 * sin_a + sfoiltempC2 * cos_a;
		if (temp >= 0x40000000)
			temp = 0x3FFF0000;
		if (temp <= -0x40000000)
			temp = -0x3FFF0000;
		rotworldeyeC2 = temp >> 15;
		temp = sfoiltempC3 * cos_a + sfoiltempA3 * sin_a;
		if (temp >= 0x40000000)
			temp = 0x3FFF0000;
		if (temp <= -0x40000000)
			temp = -0x3FFF0000;
		rotworldeyeC3 = temp >> 15;
	}
}

/* ---------- general component rotation ---------- */

// FUNCTION: TIE95 0x2759C
void fview_componentrotation(int16_t angle, const ShipModelMesh* mesh) {
	int32_t temp;
	int32_t cos_a, sin_a;
	int32_t axis_x, axis_y, axis_z;
	int32_t pivot_raw, pivot_x, pivot_z;
	int32_t rot[9];
	int32_t disp_a, disp_b, disp_c;
	const ComponentRotData* rd;

	sfoiltempA1 = rotworldeyeA1;
	sfoiltempA2 = rotworldeyeA2;
	sfoiltempA3 = rotworldeyeA3;
	sfoiltempB1 = rotworldeyeB1;
	sfoiltempB2 = rotworldeyeB2;
	sfoiltempB3 = rotworldeyeB3;
	sfoiltempC1 = rotworldeyeC1;
	sfoiltempC2 = rotworldeyeC2;
	sfoiltempC3 = rotworldeyeC3;
	sfoiltemplightX = rotlightX;
	sfoiltemplightY = rotlightY;
	sfoiltemplightZ = rotlightZ;
	sfoiltempx = objecteyex;
	sfoiltempy = objecteyey;
	sfoiltempz = objecteyez;

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

	cos_a = trig2_getsignedcos((int16_t)angle);
	sin_a = trig2_getsignedsin((int16_t)angle);

	if (cos_a >= 0) {
		int32_t one_minus_cos = 0x7FFF - cos_a; /* 1 - cos in Q15 */
		temp = (cos_a * 32768) + one_minus_cos * ((axis_y * axis_y) >> 15);
		if (temp >= 0x40000000)
			temp = 0x3FFF0000;
		if (temp <= -0x40000000)
			temp = -0x3FFF0000;
		rot[0] = temp >> 15;
		temp = ((axis_z * sin_a) >> 15) * 32768 + one_minus_cos * ((axis_x * axis_y) >> 15);
		if (temp >= 0x40000000)
			temp = 0x3FFF0000;
		if (temp <= -0x40000000)
			temp = -0x3FFF0000;
		rot[1] = temp >> 15;
		temp = -32768 * ((axis_x * sin_a) >> 15) + one_minus_cos * ((axis_z * axis_y) >> 15);
		if (temp >= 0x40000000)
			temp = 0x3FFF0000;
		if (temp <= -0x40000000)
			temp = -0x3FFF0000;
		rot[2] = temp >> 15;
		temp = -32768 * ((axis_z * sin_a) >> 15) + one_minus_cos * ((axis_x * axis_y) >> 15);
		if (temp >= 0x40000000)
			temp = 0x3FFF0000;
		if (temp <= -0x40000000)
			temp = -0x3FFF0000;
		rot[3] = temp >> 15;
		temp = (cos_a * 32768) + one_minus_cos * ((axis_x * axis_x) >> 15);
		if (temp >= 0x40000000)
			temp = 0x3FFF0000;
		if (temp <= -0x40000000)
			temp = -0x3FFF0000;
		rot[4] = temp >> 15;
		temp = ((axis_y * sin_a) >> 15) * 32768 + one_minus_cos * ((axis_z * axis_x) >> 15);
		if (temp >= 0x40000000)
			temp = 0x3FFF0000;
		if (temp <= -0x40000000)
			temp = -0x3FFF0000;
		rot[5] = temp >> 15;
		temp = ((axis_x * sin_a) >> 15) * 32768 + one_minus_cos * ((axis_z * axis_y) >> 15);
		if (temp >= 0x40000000)
			temp = 0x3FFF0000;
		if (temp <= -0x40000000)
			temp = -0x3FFF0000;
		rot[6] = temp >> 15;
		temp = -32768 * ((axis_y * sin_a) >> 15) + one_minus_cos * ((axis_z * axis_x) >> 15);
		if (temp >= 0x40000000)
			temp = 0x3FFF0000;
		if (temp <= -0x40000000)
			temp = -0x3FFF0000;
		rot[7] = temp >> 15;
		temp = (cos_a * 32768) + one_minus_cos * ((axis_z * axis_z) >> 15);
		if (temp >= 0x40000000)
			temp = 0x3FFF0000;
		if (temp <= -0x40000000)
			temp = -0x3FFF0000;
		rot[8] = temp >> 15;
	} else {
		int32_t neg_cos = -cos_a; /* |cos| */
		temp = (cos_a * 32768) + axis_y * axis_y + neg_cos * ((axis_y * axis_y) >> 15);
		if (temp >= 0x40000000)
			temp = 0x3FFF0000;
		if (temp <= -0x40000000)
			temp = -0x3FFF0000;
		rot[0] = temp >> 15;
		temp = ((axis_z * sin_a) >> 15) * 32768 + axis_x * axis_y + neg_cos * ((axis_x * axis_y) >> 15);
		if (temp >= 0x40000000)
			temp = 0x3FFF0000;
		if (temp <= -0x40000000)
			temp = -0x3FFF0000;
		rot[1] = temp >> 15;
		temp = -32768 * ((axis_x * sin_a) >> 15) + axis_z * axis_y + neg_cos * ((axis_z * axis_y) >> 15);
		if (temp >= 0x40000000)
			temp = 0x3FFF0000;
		if (temp <= -0x40000000)
			temp = -0x3FFF0000;
		rot[2] = temp >> 15;
		temp = -32768 * ((axis_z * sin_a) >> 15) + axis_x * axis_y + neg_cos * ((axis_x * axis_y) >> 15);
		if (temp >= 0x40000000)
			temp = 0x3FFF0000;
		if (temp <= -0x40000000)
			temp = -0x3FFF0000;
		rot[3] = temp >> 15;
		temp = (cos_a * 32768) + axis_x * axis_x + neg_cos * ((axis_x * axis_x) >> 15);
		if (temp >= 0x40000000)
			temp = 0x3FFF0000;
		if (temp <= -0x40000000)
			temp = -0x3FFF0000;
		rot[4] = temp >> 15;
		temp = ((axis_y * sin_a) >> 15) * 32768 + axis_z * axis_x + neg_cos * ((axis_z * axis_x) >> 15);
		if (temp >= 0x40000000)
			temp = 0x3FFF0000;
		if (temp <= -0x40000000)
			temp = -0x3FFF0000;
		rot[5] = temp >> 15;
		temp = ((axis_x * sin_a) >> 15) * 32768 + axis_z * axis_y + neg_cos * ((axis_z * axis_y) >> 15);
		if (temp >= 0x40000000)
			temp = 0x3FFF0000;
		if (temp <= -0x40000000)
			temp = -0x3FFF0000;
		rot[6] = temp >> 15;
		temp = -32768 * ((axis_y * sin_a) >> 15) + axis_z * axis_x + neg_cos * ((axis_z * axis_x) >> 15);
		if (temp >= 0x40000000)
			temp = 0x3FFF0000;
		if (temp <= -0x40000000)
			temp = -0x3FFF0000;
		rot[7] = temp >> 15;
		temp = (cos_a * 32768) + axis_z * axis_z + neg_cos * ((axis_z * axis_z) >> 15);
		if (temp >= 0x40000000)
			temp = 0x3FFF0000;
		if (temp <= -0x40000000)
			temp = -0x3FFF0000;
		rot[8] = temp >> 15;
	}

	/* Compute pivot displacement: disp = pivot + R * (-pivot) */
	disp_a = math2_dot3_q15(-pivot_raw, rot[0], -pivot_x, rot[3], -pivot_z, rot[6]) + pivot_raw;
	disp_b = math2_dot3_q15(-pivot_raw, rot[1], -pivot_x, rot[4], -pivot_z, rot[7]) + pivot_x;
	disp_c = math2_dot3_q15(-pivot_raw, rot[2], -pivot_x, rot[5], -pivot_z, rot[8]) + pivot_z;

	/* Translate objecteye through rotworldeye */
	objecteyex += math2_dot3_q15(disp_a, rotworldeyeA1, disp_b, rotworldeyeB1, disp_c, rotworldeyeC1);
	objecteyey += math2_dot3_q15(disp_a, rotworldeyeA2, disp_b, rotworldeyeB2, disp_c, rotworldeyeC2);
	objecteyez += math2_dot3_q15(disp_a, rotworldeyeA3, disp_b, rotworldeyeB3, disp_c, rotworldeyeC3);

	/* Rotate rotworldeye = saved * R */
	rotworldeyeA1 = math2_dot3_q15_clamped(rot[0], rot[1], rot[2], sfoiltempA1, sfoiltempB1, sfoiltempC1);
	rotworldeyeA2 = math2_dot3_q15_clamped(rot[0], rot[1], rot[2], sfoiltempA2, sfoiltempB2, sfoiltempC2);
	rotworldeyeA3 = math2_dot3_q15_clamped(rot[0], rot[1], rot[2], sfoiltempA3, sfoiltempB3, sfoiltempC3);
	rotworldeyeB1 = math2_dot3_q15_clamped(rot[3], rot[4], rot[5], sfoiltempA1, sfoiltempB1, sfoiltempC1);
	rotworldeyeB2 = math2_dot3_q15_clamped(rot[3], rot[4], rot[5], sfoiltempA2, sfoiltempB2, sfoiltempC2);
	rotworldeyeB3 = math2_dot3_q15_clamped(rot[3], rot[4], rot[5], sfoiltempA3, sfoiltempB3, sfoiltempC3);
	rotworldeyeC1 = math2_dot3_q15_clamped(rot[6], rot[7], rot[8], sfoiltempA1, sfoiltempB1, sfoiltempC1);
	rotworldeyeC2 = math2_dot3_q15_clamped(rot[6], rot[7], rot[8], sfoiltempA2, sfoiltempB2, sfoiltempC2);
	rotworldeyeC3 = math2_dot3_q15_clamped(rot[6], rot[7], rot[8], sfoiltempA3, sfoiltempB3, sfoiltempC3);

	/* Rotate light direction */
	rotlightX =
		math2_dot3_q15_clamped(rot[0], rot[1], rot[2], sfoiltemplightX, sfoiltemplightY, sfoiltemplightZ);
	rotlightY =
		math2_dot3_q15_clamped(rot[3], rot[4], rot[5], sfoiltemplightX, sfoiltemplightY, sfoiltemplightZ);
	rotlightZ =
		math2_dot3_q15_clamped(rot[6], rot[7], rot[8], sfoiltemplightX, sfoiltemplightY, sfoiltemplightZ);
}

// FUNCTION: TIE95 0x28100
void fview_restorerotation(void) {
	rotworldeyeA1 = sfoiltempA1;
	rotworldeyeA2 = sfoiltempA2;
	rotworldeyeA3 = sfoiltempA3;
	rotworldeyeB1 = sfoiltempB1;
	rotworldeyeB2 = sfoiltempB2;
	rotworldeyeB3 = sfoiltempB3;
	rotworldeyeC1 = sfoiltempC1;
	rotworldeyeC2 = sfoiltempC2;
	rotworldeyeC3 = sfoiltempC3;
	rotlightX = sfoiltemplightX;
	rotlightY = sfoiltemplightY;
	rotlightZ = sfoiltemplightZ;
	objecteyex = sfoiltempx;
	objecteyey = sfoiltempy;
	objecteyez = sfoiltempz;
}

/* Rodrigues rotation-matrix element: clamp(term * 1.0 + (a * b) * scale)
 * in Q15, with the same saturation as math2_dot3_q15_clamped. For a
 * negative cosine, `scale` is -cos and the a * b product is added back in
 * full (1 - cos = 1 + |cos|). TIE95 inlines both forms at every use; their
 * names are not known. Other toolchains compute the same values in C. */
#ifdef __WATCOMC__
int32_t fview_rotelement(int32_t a, int32_t b, int32_t scale, int32_t term);
#pragma aux fview_rotelement =                                                                               \
	"imul eax, ebx"                                                                                          \
	"sar eax, 15"                                                                                            \
	"imul eax, ecx"                                                                                          \
	"shl edx, 15"                                                                                            \
	"add eax, edx"                                                                                           \
	"cmp eax, 40000000h"                                                                                     \
	"jl re_positive_ok"                                                                                      \
	"mov eax, 3fff0000h"                                                                                     \
	"re_positive_ok: cmp eax, 0c0000000h"                                                                    \
	"jg re_negative_ok"                                                                                      \
	"mov eax, 0c0010000h"                                                                                    \
	"re_negative_ok: sar eax, 15" parm[eax][ebx][ecx][edx] value[eax] modify exact[eax ecx edx];
int32_t fview_rotelementneg(int32_t a, int32_t b, int32_t scale, int32_t term);
#pragma aux fview_rotelementneg =                                                                            \
	"imul eax, ebx"                                                                                          \
	"mov ebx, eax"                                                                                           \
	"sar eax, 15"                                                                                            \
	"imul eax, ecx"                                                                                          \
	"shl edx, 15"                                                                                            \
	"add eax, ebx"                                                                                           \
	"add eax, edx"                                                                                           \
	"cmp eax, 40000000h"                                                                                     \
	"jl rn_positive_ok"                                                                                      \
	"mov eax, 3fff0000h"                                                                                     \
	"rn_positive_ok: cmp eax, 0c0000000h"                                                                    \
	"jg rn_negative_ok"                                                                                      \
	"mov eax, 0c0010000h"                                                                                    \
	"rn_negative_ok: sar eax, 15" parm[eax][ebx][ecx][edx] value[eax] modify exact[eax ebx ecx edx];
#endif

// FUNCTION: TIE95 0x28198
void fview_comprotatepoint(int16_t angle, const ShipModelMesh* mesh, int32_t point_x, int32_t point_y,
						   int32_t point_z) {
	int32_t axis_x, axis_y, axis_z;
	const ComponentRotData* rd;
	int32_t cos_a, sin_a;
	int32_t m0, m1, m2, m3, m4, m5, m6, m7, m8;
	int32_t rx, ry, rz;

	rd = (const ComponentRotData*)((const uint8_t*)mesh + mesh->rotation_offset);

	axis_y = rd->axis_y;
	axis_x = rd->axis_x;
	axis_z = rd->axis_z;

	cos_a = trig2_getsignedcos(angle);
	sin_a = trig2_getsignedsin(angle);

	if (cos_a >= 0) {
		int32_t one_minus_cos = 0x7FFF - cos_a; /* 1 - cos in Q15 */
#ifdef __WATCOMC__
		m0 = fview_rotelement(axis_y, axis_y, one_minus_cos, cos_a);
		m1 = fview_rotelement(axis_y, axis_x, one_minus_cos, math2_mul16_q15(sin_a, axis_z));
		m2 = fview_rotelement(axis_y, axis_z, one_minus_cos, -math2_mul16_q15(sin_a, axis_x));
		m3 = fview_rotelement(axis_y, axis_x, one_minus_cos, -math2_mul16_q15(sin_a, axis_z));
		m4 = fview_rotelement(axis_x, axis_x, one_minus_cos, cos_a);
		m5 = fview_rotelement(axis_x, axis_z, one_minus_cos, math2_mul16_q15(sin_a, axis_y));
		m6 = fview_rotelement(axis_y, axis_z, one_minus_cos, math2_mul16_q15(sin_a, axis_x));
		m7 = fview_rotelement(axis_x, axis_z, one_minus_cos, -math2_mul16_q15(sin_a, axis_y));
		m8 = fview_rotelement(axis_z, axis_z, one_minus_cos, cos_a);
#else
		int32_t temp;

		temp = (cos_a << 15) + math2_mul16_q15(axis_y, axis_y) * one_minus_cos;
		if (temp >= 0x40000000)
			temp = 0x3FFF0000;
		if (temp <= -0x40000000)
			temp = -0x3FFF0000;
		m0 = temp >> 15;
		temp = math2_mul16_q15(sin_a, axis_z) * 32768 + math2_mul16_q15(axis_y, axis_x) * one_minus_cos;
		if (temp >= 0x40000000)
			temp = 0x3FFF0000;
		if (temp <= -0x40000000)
			temp = -0x3FFF0000;
		m1 = temp >> 15;
		temp = -math2_mul16_q15(sin_a, axis_x) * 32768 + math2_mul16_q15(axis_y, axis_z) * one_minus_cos;
		if (temp >= 0x40000000)
			temp = 0x3FFF0000;
		if (temp <= -0x40000000)
			temp = -0x3FFF0000;
		m2 = temp >> 15;
		temp = -math2_mul16_q15(sin_a, axis_z) * 32768 + math2_mul16_q15(axis_y, axis_x) * one_minus_cos;
		if (temp >= 0x40000000)
			temp = 0x3FFF0000;
		if (temp <= -0x40000000)
			temp = -0x3FFF0000;
		m3 = temp >> 15;
		temp = (cos_a << 15) + math2_mul16_q15(axis_x, axis_x) * one_minus_cos;
		if (temp >= 0x40000000)
			temp = 0x3FFF0000;
		if (temp <= -0x40000000)
			temp = -0x3FFF0000;
		m4 = temp >> 15;
		temp = math2_mul16_q15(sin_a, axis_y) * 32768 + math2_mul16_q15(axis_x, axis_z) * one_minus_cos;
		if (temp >= 0x40000000)
			temp = 0x3FFF0000;
		if (temp <= -0x40000000)
			temp = -0x3FFF0000;
		m5 = temp >> 15;
		temp = math2_mul16_q15(sin_a, axis_x) * 32768 + math2_mul16_q15(axis_y, axis_z) * one_minus_cos;
		if (temp >= 0x40000000)
			temp = 0x3FFF0000;
		if (temp <= -0x40000000)
			temp = -0x3FFF0000;
		m6 = temp >> 15;
		temp = -math2_mul16_q15(sin_a, axis_y) * 32768 + math2_mul16_q15(axis_x, axis_z) * one_minus_cos;
		if (temp >= 0x40000000)
			temp = 0x3FFF0000;
		if (temp <= -0x40000000)
			temp = -0x3FFF0000;
		m7 = temp >> 15;
		temp = (cos_a << 15) + math2_mul16_q15(axis_z, axis_z) * one_minus_cos;
		if (temp >= 0x40000000)
			temp = 0x3FFF0000;
		if (temp <= -0x40000000)
			temp = -0x3FFF0000;
		m8 = temp >> 15;
#endif
	} else {
		int32_t neg_cos = -cos_a; /* |cos| */
#ifdef __WATCOMC__
		m0 = fview_rotelementneg(axis_y, axis_y, neg_cos, cos_a);
		m1 = fview_rotelementneg(axis_y, axis_x, neg_cos, math2_mul16_q15(sin_a, axis_z));
		m2 = fview_rotelementneg(axis_y, axis_z, neg_cos, -math2_mul16_q15(sin_a, axis_x));
		m3 = fview_rotelementneg(axis_y, axis_x, neg_cos, -math2_mul16_q15(sin_a, axis_z));
		m4 = fview_rotelementneg(axis_x, axis_x, neg_cos, cos_a);
		m5 = fview_rotelementneg(axis_x, axis_z, neg_cos, math2_mul16_q15(sin_a, axis_y));
		m6 = fview_rotelementneg(axis_y, axis_z, neg_cos, math2_mul16_q15(sin_a, axis_x));
		m7 = fview_rotelementneg(axis_x, axis_z, neg_cos, -math2_mul16_q15(sin_a, axis_y));
		m8 = fview_rotelementneg(axis_z, axis_z, neg_cos, cos_a);
#else
		int32_t temp;

		temp = (cos_a << 15) + axis_y * axis_y + ((axis_y * axis_y) >> 15) * neg_cos;
		if (temp >= 0x40000000)
			temp = 0x3FFF0000;
		if (temp <= -0x40000000)
			temp = -0x3FFF0000;
		m0 = temp >> 15;
		temp = ((sin_a * axis_z) >> 15) * 32768 + axis_y * axis_x + ((axis_y * axis_x) >> 15) * neg_cos;
		if (temp >= 0x40000000)
			temp = 0x3FFF0000;
		if (temp <= -0x40000000)
			temp = -0x3FFF0000;
		m1 = temp >> 15;
		temp = -((sin_a * axis_x) >> 15) * 32768 + axis_y * axis_z + ((axis_y * axis_z) >> 15) * neg_cos;
		if (temp >= 0x40000000)
			temp = 0x3FFF0000;
		if (temp <= -0x40000000)
			temp = -0x3FFF0000;
		m2 = temp >> 15;
		temp = -((sin_a * axis_z) >> 15) * 32768 + axis_y * axis_x + ((axis_y * axis_x) >> 15) * neg_cos;
		if (temp >= 0x40000000)
			temp = 0x3FFF0000;
		if (temp <= -0x40000000)
			temp = -0x3FFF0000;
		m3 = temp >> 15;
		temp = (cos_a << 15) + axis_x * axis_x + ((axis_x * axis_x) >> 15) * neg_cos;
		if (temp >= 0x40000000)
			temp = 0x3FFF0000;
		if (temp <= -0x40000000)
			temp = -0x3FFF0000;
		m4 = temp >> 15;
		temp = ((sin_a * axis_y) >> 15) * 32768 + axis_x * axis_z + ((axis_x * axis_z) >> 15) * neg_cos;
		if (temp >= 0x40000000)
			temp = 0x3FFF0000;
		if (temp <= -0x40000000)
			temp = -0x3FFF0000;
		m5 = temp >> 15;
		temp = ((sin_a * axis_x) >> 15) * 32768 + axis_y * axis_z + ((axis_y * axis_z) >> 15) * neg_cos;
		if (temp >= 0x40000000)
			temp = 0x3FFF0000;
		if (temp <= -0x40000000)
			temp = -0x3FFF0000;
		m6 = temp >> 15;
		temp = -((sin_a * axis_y) >> 15) * 32768 + axis_x * axis_z + ((axis_x * axis_z) >> 15) * neg_cos;
		if (temp >= 0x40000000)
			temp = 0x3FFF0000;
		if (temp <= -0x40000000)
			temp = -0x3FFF0000;
		m7 = temp >> 15;
		temp = (cos_a << 15) + axis_z * axis_z + ((axis_z * axis_z) >> 15) * neg_cos;
		if (temp >= 0x40000000)
			temp = 0x3FFF0000;
		if (temp <= -0x40000000)
			temp = -0x3FFF0000;
		m8 = temp >> 15;
#endif
	}

	/* Rotate the point about the component pivot */
	point_x -= rd->pivot_value >> 1;
	point_y -= rd->pivot_x >> 1;
	point_z -= rd->pivot_z >> 1;

	rx = math2_dot3_q15_clamped(point_x, point_y, point_z, m0, m3, m6);
	ry = math2_dot3_q15_clamped(point_x, point_y, point_z, m1, m4, m7);
	rz = math2_dot3_q15_clamped(point_x, point_y, point_z, m2, m5, m8);

	point_x = rx + (rd->pivot_value >> 1);
	point_y = ry + (rd->pivot_x >> 1);
	point_z = rz + (rd->pivot_z >> 1);

	rotatedx = point_x;
	rotatedy = point_y;
	rotatedz = point_z;
}

// FUNCTION: TIE95 0x287F4
void fview_transformaxes(int32_t axis_x, int32_t axis_y, int32_t axis_z, uint16_t angle) {
	int32_t temp;
	int16_t cos_a, sin_a;
	int32_t rot0, rot1, rot2, rot3, rot4, rot5, rot6, rot7, rot8;
	int32_t new_S1, new_S2, new_U1, new_U2, new_f1, new_f2;
	int32_t sine_term;

	if (!angle)
		return;

	cos_a = trig2_getsignedcos((int16_t)angle);
	sin_a = trig2_getsignedsin((int16_t)angle);

	if (cos_a >= 0) {
		int32_t cosine = cos_a;
		int32_t sine = sin_a;
		int32_t one_minus_cos = 0x7FFF - cosine; /* 1 - cos in Q15 */
		temp = ((axis_x * axis_x) >> 15) * one_minus_cos + (cosine * 32768);
		if (temp >= 0x40000000)
			temp = 0x3FFF0000;
		if (temp <= -0x40000000)
			temp = -0x3FFF0000;
		rot0 = temp >> 15;
		sine_term = (sine * axis_z) >> 15;
		temp = ((axis_x * axis_y) >> 15) * one_minus_cos + sine_term * 32768;
		if (temp >= 0x40000000)
			temp = 0x3FFF0000;
		if (temp <= -0x40000000)
			temp = -0x3FFF0000;
		rot1 = temp >> 15;
		sine_term = -((sine * axis_y) >> 15);
		temp = ((axis_x * axis_z) >> 15) * one_minus_cos + sine_term * 32768;
		if (temp >= 0x40000000)
			temp = 0x3FFF0000;
		if (temp <= -0x40000000)
			temp = -0x3FFF0000;
		rot2 = temp >> 15;
		sine_term = -((sine * axis_z) >> 15);
		temp = ((axis_x * axis_y) >> 15) * one_minus_cos + sine_term * 32768;
		if (temp >= 0x40000000)
			temp = 0x3FFF0000;
		if (temp <= -0x40000000)
			temp = -0x3FFF0000;
		rot3 = temp >> 15;
		temp = ((axis_y * axis_y) >> 15) * one_minus_cos + (cosine * 32768);
		if (temp >= 0x40000000)
			temp = 0x3FFF0000;
		if (temp <= -0x40000000)
			temp = -0x3FFF0000;
		rot4 = temp >> 15;
		sine_term = (sine * axis_x) >> 15;
		temp = ((axis_y * axis_z) >> 15) * one_minus_cos + sine_term * 32768;
		if (temp >= 0x40000000)
			temp = 0x3FFF0000;
		if (temp <= -0x40000000)
			temp = -0x3FFF0000;
		rot5 = temp >> 15;
		sine_term = (sine * axis_y) >> 15;
		temp = ((axis_x * axis_z) >> 15) * one_minus_cos + sine_term * 32768;
		if (temp >= 0x40000000)
			temp = 0x3FFF0000;
		if (temp <= -0x40000000)
			temp = -0x3FFF0000;
		rot6 = temp >> 15;
		sine_term = -((sine * axis_x) >> 15);
		temp = ((axis_y * axis_z) >> 15) * one_minus_cos + sine_term * 32768;
		if (temp >= 0x40000000)
			temp = 0x3FFF0000;
		if (temp <= -0x40000000)
			temp = -0x3FFF0000;
		rot7 = temp >> 15;
		temp = ((axis_z * axis_z) >> 15) * one_minus_cos + (cosine * 32768);
		if (temp >= 0x40000000)
			temp = 0x3FFF0000;
		if (temp <= -0x40000000)
			temp = -0x3FFF0000;
		rot8 = temp >> 15;
	} else {
		int32_t cosine = cos_a;
		int32_t sine = sin_a;
		int32_t neg_cos = -cosine; /* |cos| */
		temp = ((axis_x * axis_x) >> 15) * neg_cos + axis_x * axis_x + (cosine * 32768);
		if (temp >= 0x40000000)
			temp = 0x3FFF0000;
		if (temp <= -0x40000000)
			temp = -0x3FFF0000;
		rot0 = temp >> 15;
		sine_term = (sine * axis_z) >> 15;
		temp = ((axis_x * axis_y) >> 15) * neg_cos + axis_x * axis_y + sine_term * 32768;
		if (temp >= 0x40000000)
			temp = 0x3FFF0000;
		if (temp <= -0x40000000)
			temp = -0x3FFF0000;
		rot1 = temp >> 15;
		sine_term = -((sine * axis_y) >> 15);
		temp = ((axis_x * axis_z) >> 15) * neg_cos + axis_x * axis_z + sine_term * 32768;
		if (temp >= 0x40000000)
			temp = 0x3FFF0000;
		if (temp <= -0x40000000)
			temp = -0x3FFF0000;
		rot2 = temp >> 15;
		sine_term = -((sine * axis_z) >> 15);
		temp = ((axis_x * axis_y) >> 15) * neg_cos + axis_x * axis_y + sine_term * 32768;
		if (temp >= 0x40000000)
			temp = 0x3FFF0000;
		if (temp <= -0x40000000)
			temp = -0x3FFF0000;
		rot3 = temp >> 15;
		temp = ((axis_y * axis_y) >> 15) * neg_cos + axis_y * axis_y + (cosine * 32768);
		if (temp >= 0x40000000)
			temp = 0x3FFF0000;
		if (temp <= -0x40000000)
			temp = -0x3FFF0000;
		rot4 = temp >> 15;
		sine_term = (sine * axis_x) >> 15;
		temp = ((axis_y * axis_z) >> 15) * neg_cos + axis_y * axis_z + sine_term * 32768;
		if (temp >= 0x40000000)
			temp = 0x3FFF0000;
		if (temp <= -0x40000000)
			temp = -0x3FFF0000;
		rot5 = temp >> 15;
		sine_term = (sine * axis_y) >> 15;
		temp = ((axis_x * axis_z) >> 15) * neg_cos + axis_x * axis_z + sine_term * 32768;
		if (temp >= 0x40000000)
			temp = 0x3FFF0000;
		if (temp <= -0x40000000)
			temp = -0x3FFF0000;
		rot6 = temp >> 15;
		sine_term = -((sine * axis_x) >> 15);
		temp = ((axis_y * axis_z) >> 15) * neg_cos + axis_y * axis_z + sine_term * 32768;
		if (temp >= 0x40000000)
			temp = 0x3FFF0000;
		if (temp <= -0x40000000)
			temp = -0x3FFF0000;
		rot7 = temp >> 15;
		temp = ((axis_z * axis_z) >> 15) * neg_cos + axis_z * axis_z + (cosine * 32768);
		if (temp >= 0x40000000)
			temp = 0x3FFF0000;
		if (temp <= -0x40000000)
			temp = -0x3FFF0000;
		rot8 = temp >> 15;
	}
	new_S1 = math2_dot3_q15_clamped(calcS1, calcS2, calcS3, rot0, rot3, rot6);
	new_S2 = math2_dot3_q15_clamped(calcS1, calcS2, calcS3, rot1, rot4, rot7);
	calcS3 = math2_dot3_q15_clamped(calcS1, calcS2, calcS3, rot2, rot5, rot8);
	calcS1 = new_S1;
	calcS2 = new_S2;
	new_U1 = math2_dot3_q15_clamped(calcU1, calcU2, calcU3, rot0, rot3, rot6);
	new_U2 = math2_dot3_q15_clamped(calcU1, calcU2, calcU3, rot1, rot4, rot7);
	calcU3 = math2_dot3_q15_clamped(calcU1, calcU2, calcU3, rot2, rot5, rot8);
	calcU1 = new_U1;
	calcU2 = new_U2;
	new_f1 = math2_dot3_q15_clamped(calcf1, calcf2, calcf3, rot0, rot3, rot6);
	new_f2 = math2_dot3_q15_clamped(calcf1, calcf2, calcf3, rot1, rot4, rot7);
	calcf3 = math2_dot3_q15_clamped(calcf1, calcf2, calcf3, rot2, rot5, rot8);
	calcf1 = new_f1;
	calcf2 = new_f2;
}
