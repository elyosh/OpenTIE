/*
 * fview.c — 3D view/camera rotation engine.
 * Builds rotation matrices from Euler angles using Q15 fixed-point arithmetic.
 * Handles camera view, craft orientation, and articulated component rotation
 * (S-foils, corvette turrets, B-wing cockpit) via Rodrigues rotation formula.
 *
 * Original: D:\GAMES\XTIE\CODE\fview.c (names-only module, no debug records)
 */

#include "tie/fview.h"
#include "tie/math2_wide.h"
#include "tie/transfm2.h"
#include "tie/trig2.h"

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
	int32_t temp;
	temp = worldeyeA1 * calcS1 + worldeyeB1 * calcS2 + worldeyeC1 * calcS3;
	if (temp >= 0x40000000)
		temp = 0x3FFF0000;
	if (temp <= -0x40000000)
		temp = -0x3FFF0000;
	rotworldeyeA1 = temp >> 15;
	temp = worldeyeA2 * calcS1 + worldeyeB2 * calcS2 + worldeyeC2 * calcS3;
	if (temp >= 0x40000000)
		temp = 0x3FFF0000;
	if (temp <= -0x40000000)
		temp = -0x3FFF0000;
	rotworldeyeA2 = temp >> 15;
	temp = worldeyeA3 * calcS1 + worldeyeB3 * calcS2 + worldeyeC3 * calcS3;
	if (temp >= 0x40000000)
		temp = 0x3FFF0000;
	if (temp <= -0x40000000)
		temp = -0x3FFF0000;
	rotworldeyeA3 = temp >> 15;
	temp = worldeyeA1 * calcf1 + worldeyeB1 * calcf2 + worldeyeC1 * calcf3;
	if (temp >= 0x40000000)
		temp = 0x3FFF0000;
	if (temp <= -0x40000000)
		temp = -0x3FFF0000;
	rotworldeyeB1 = temp >> 15;
	temp = worldeyeA2 * calcf1 + worldeyeB2 * calcf2 + worldeyeC2 * calcf3;
	if (temp >= 0x40000000)
		temp = 0x3FFF0000;
	if (temp <= -0x40000000)
		temp = -0x3FFF0000;
	rotworldeyeB2 = temp >> 15;
	temp = worldeyeA3 * calcf1 + worldeyeB3 * calcf2 + worldeyeC3 * calcf3;
	if (temp >= 0x40000000)
		temp = 0x3FFF0000;
	if (temp <= -0x40000000)
		temp = -0x3FFF0000;
	rotworldeyeB3 = temp >> 15;
	temp = worldeyeA1 * calcU1 + worldeyeB1 * calcU2 + worldeyeC1 * calcU3;
	if (temp >= 0x40000000)
		temp = 0x3FFF0000;
	if (temp <= -0x40000000)
		temp = -0x3FFF0000;
	rotworldeyeC1 = temp >> 15;
	temp = worldeyeA2 * calcU1 + worldeyeB2 * calcU2 + worldeyeC2 * calcU3;
	if (temp >= 0x40000000)
		temp = 0x3FFF0000;
	if (temp <= -0x40000000)
		temp = -0x3FFF0000;
	rotworldeyeC2 = temp >> 15;
	temp = worldeyeA3 * calcU1 + worldeyeB3 * calcU2 + worldeyeC3 * calcU3;
	if (temp >= 0x40000000)
		temp = 0x3FFF0000;
	if (temp <= -0x40000000)
		temp = -0x3FFF0000;
	rotworldeyeC3 = temp >> 15;

	if (lightflag) {
		temp = lightX * calcS1 + lightY * calcS2 + lightZ * calcS3;
		if (temp >= 0x40000000)
			temp = 0x3FFF0000;
		if (temp <= -0x40000000)
			temp = -0x3FFF0000;
		rotlightX = temp >> 15;
		temp = lightX * calcf1 + lightY * calcf2 + lightZ * calcf3;
		if (temp >= 0x40000000)
			temp = 0x3FFF0000;
		if (temp <= -0x40000000)
			temp = -0x3FFF0000;
		rotlightY = temp >> 15;
		temp = lightX * calcU1 + lightY * calcU2 + lightZ * calcU3;
		if (temp >= 0x40000000)
			temp = 0x3FFF0000;
		if (temp <= -0x40000000)
			temp = -0x3FFF0000;
		rotlightZ = temp >> 15;
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
void fview_bwingrotation(int16_t angle, uint16_t part_id) {
	int32_t temp;
	int32_t cos_a, sin_a, neg_sin;
	int32_t lat_offset, vert_offset;
	int32_t x_fwd, z_side;
	int32_t pivot_lat, pivot_z;
	int32_t rot_fwd, adj_z;
	int32_t dot;

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
		temp = rotworldeyeA1 * x_fwd + rotworldeyeC1 * z_side;
		if (temp >= 0x40000000)
			temp = 0x3FFF0000;
		if (temp <= -0x40000000)
			temp = -0x3FFF0000;
		objecteyex += temp >> 15;
		temp = rotworldeyeA2 * x_fwd + rotworldeyeC2 * z_side;
		if (temp >= 0x40000000)
			temp = 0x3FFF0000;
		if (temp <= -0x40000000)
			temp = -0x3FFF0000;
		objecteyey += temp >> 15;
		temp = rotworldeyeA3 * x_fwd + rotworldeyeC3 * z_side;
		if (temp >= 0x40000000)
			temp = 0x3FFF0000;
		if (temp <= -0x40000000)
			temp = -0x3FFF0000;
		objecteyez += temp >> 15;

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

	if (part_id == 5) {
		temp = -50 * sin_a + 42 * cos_a;
		if (temp >= 0x40000000)
			temp = 0x3FFF0000;
		if (temp <= -0x40000000)
			temp = -0x3FFF0000;
		dot = temp >> 15;
		temp = 50 * cos_a + 42 * sin_a;
		if (temp >= 0x40000000)
			temp = 0x3FFF0000;
		if (temp <= -0x40000000)
			temp = -0x3FFF0000;
		pivot_z = (temp >> 15) - 50;
		pivot_lat = dot - 42;
	} else if (part_id == 4) {
		temp = 50 * sin_a - 42 * cos_a;
		if (temp >= 0x40000000)
			temp = 0x3FFF0000;
		if (temp <= -0x40000000)
			temp = -0x3FFF0000;
		dot = temp >> 15;
		temp = 50 * cos_a + 42 * sin_a;
		if (temp >= 0x40000000)
			temp = 0x3FFF0000;
		if (temp <= -0x40000000)
			temp = -0x3FFF0000;
		pivot_z = (temp >> 15) - 50;
		pivot_lat = dot + 42;
	} else {
		pivot_z = 0;
		pivot_lat = 0;
	}

	adj_z = pivot_z - 170;
	temp = adj_z * sin_a + pivot_lat * cos_a;
	if (temp >= 0x40000000)
		temp = 0x3FFF0000;
	if (temp <= -0x40000000)
		temp = -0x3FFF0000;
	rot_fwd = temp >> 15;
	temp = adj_z * cos_a + pivot_lat * neg_sin;
	if (temp >= 0x40000000)
		temp = 0x3FFF0000;
	if (temp <= -0x40000000)
		temp = -0x3FFF0000;
	adj_z = (temp >> 15) + 170;

	/* Translate objecteye via A and C columns */
	temp = rotworldeyeA1 * rot_fwd + rotworldeyeC1 * adj_z;
	if (temp >= 0x40000000)
		temp = 0x3FFF0000;
	if (temp <= -0x40000000)
		temp = -0x3FFF0000;
	objecteyex += temp >> 15;
	temp = rotworldeyeA2 * rot_fwd + rotworldeyeC2 * adj_z;
	if (temp >= 0x40000000)
		temp = 0x3FFF0000;
	if (temp <= -0x40000000)
		temp = -0x3FFF0000;
	objecteyey += temp >> 15;
	temp = rotworldeyeA3 * rot_fwd + rotworldeyeC3 * adj_z;
	if (temp >= 0x40000000)
		temp = 0x3FFF0000;
	if (temp <= -0x40000000)
		temp = -0x3FFF0000;
	objecteyez += temp >> 15;

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
	disp_a = pivot_raw + math2_mul_q15(rot[0], -pivot_raw) + math2_mul_q15(rot[3], -pivot_x) +
			 math2_mul_q15(rot[6], -pivot_z);
	disp_b = pivot_x + math2_mul_q15(rot[1], -pivot_raw) + math2_mul_q15(rot[4], -pivot_x) +
			 math2_mul_q15(rot[7], -pivot_z);
	disp_c = pivot_z + math2_mul_q15(rot[2], -pivot_raw) + math2_mul_q15(rot[5], -pivot_x) +
			 math2_mul_q15(rot[8], -pivot_z);

	/* Translate objecteye through rotworldeye */
	objecteyex += math2_mul_q15(rotworldeyeA1, disp_a) + math2_mul_q15(rotworldeyeB1, disp_b) +
				  math2_mul_q15(rotworldeyeC1, disp_c);
	objecteyey += math2_mul_q15(rotworldeyeA2, disp_a) + math2_mul_q15(rotworldeyeB2, disp_b) +
				  math2_mul_q15(rotworldeyeC2, disp_c);
	objecteyez += math2_mul_q15(rotworldeyeA3, disp_a) + math2_mul_q15(rotworldeyeB3, disp_b) +
				  math2_mul_q15(rotworldeyeC3, disp_c);

	/* Rotate rotworldeye = saved * R */
	temp = sfoiltempA1 * rot[0] + sfoiltempB1 * rot[1] + sfoiltempC1 * rot[2];
	if (temp >= 0x40000000)
		temp = 0x3FFF0000;
	if (temp <= -0x40000000)
		temp = -0x3FFF0000;
	rotworldeyeA1 = temp >> 15;
	temp = sfoiltempA2 * rot[0] + sfoiltempB2 * rot[1] + sfoiltempC2 * rot[2];
	if (temp >= 0x40000000)
		temp = 0x3FFF0000;
	if (temp <= -0x40000000)
		temp = -0x3FFF0000;
	rotworldeyeA2 = temp >> 15;
	temp = sfoiltempA3 * rot[0] + sfoiltempB3 * rot[1] + sfoiltempC3 * rot[2];
	if (temp >= 0x40000000)
		temp = 0x3FFF0000;
	if (temp <= -0x40000000)
		temp = -0x3FFF0000;
	rotworldeyeA3 = temp >> 15;
	temp = sfoiltempA1 * rot[3] + sfoiltempB1 * rot[4] + sfoiltempC1 * rot[5];
	if (temp >= 0x40000000)
		temp = 0x3FFF0000;
	if (temp <= -0x40000000)
		temp = -0x3FFF0000;
	rotworldeyeB1 = temp >> 15;
	temp = sfoiltempA2 * rot[3] + sfoiltempB2 * rot[4] + sfoiltempC2 * rot[5];
	if (temp >= 0x40000000)
		temp = 0x3FFF0000;
	if (temp <= -0x40000000)
		temp = -0x3FFF0000;
	rotworldeyeB2 = temp >> 15;
	temp = sfoiltempA3 * rot[3] + sfoiltempB3 * rot[4] + sfoiltempC3 * rot[5];
	if (temp >= 0x40000000)
		temp = 0x3FFF0000;
	if (temp <= -0x40000000)
		temp = -0x3FFF0000;
	rotworldeyeB3 = temp >> 15;
	temp = sfoiltempA1 * rot[6] + sfoiltempB1 * rot[7] + sfoiltempC1 * rot[8];
	if (temp >= 0x40000000)
		temp = 0x3FFF0000;
	if (temp <= -0x40000000)
		temp = -0x3FFF0000;
	rotworldeyeC1 = temp >> 15;
	temp = sfoiltempA2 * rot[6] + sfoiltempB2 * rot[7] + sfoiltempC2 * rot[8];
	if (temp >= 0x40000000)
		temp = 0x3FFF0000;
	if (temp <= -0x40000000)
		temp = -0x3FFF0000;
	rotworldeyeC2 = temp >> 15;
	temp = sfoiltempA3 * rot[6] + sfoiltempB3 * rot[7] + sfoiltempC3 * rot[8];
	if (temp >= 0x40000000)
		temp = 0x3FFF0000;
	if (temp <= -0x40000000)
		temp = -0x3FFF0000;
	rotworldeyeC3 = temp >> 15;

	/* Rotate light direction */
	temp = sfoiltemplightX * rot[0] + sfoiltemplightY * rot[1] + sfoiltemplightZ * rot[2];
	if (temp >= 0x40000000)
		temp = 0x3FFF0000;
	if (temp <= -0x40000000)
		temp = -0x3FFF0000;
	rotlightX = temp >> 15;
	temp = sfoiltemplightX * rot[3] + sfoiltemplightY * rot[4] + sfoiltemplightZ * rot[5];
	if (temp >= 0x40000000)
		temp = 0x3FFF0000;
	if (temp <= -0x40000000)
		temp = -0x3FFF0000;
	rotlightY = temp >> 15;
	temp = sfoiltemplightX * rot[6] + sfoiltemplightY * rot[7] + sfoiltemplightZ * rot[8];
	if (temp >= 0x40000000)
		temp = 0x3FFF0000;
	if (temp <= -0x40000000)
		temp = -0x3FFF0000;
	rotlightZ = temp >> 15;
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

// FUNCTION: TIE95 0x28198
void fview_comprotatepoint(int16_t angle, const ShipModelMesh* mesh, int32_t point_x, int32_t point_y,
						   int32_t point_z) {
	int32_t temp;
	int32_t cos_a, sin_a;
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

	pivot_half = rd->pivot_value >> 1;
	pivot_xv = rd->pivot_x >> 1;
	pivot_zv = rd->pivot_z >> 1;

	/* Relative point = point - pivot */
	rel_x = point_x - pivot_half;
	rel_y = point_y - pivot_xv;
	rel_z = point_z - pivot_zv;

	/* Rotate relative point by R */
	temp = rot[0] * rel_x + rot[3] * rel_y + rot[6] * rel_z;
	if (temp >= 0x40000000)
		temp = 0x3FFF0000;
	if (temp <= -0x40000000)
		temp = -0x3FFF0000;
	rx = temp >> 15;
	temp = rot[1] * rel_x + rot[4] * rel_y + rot[7] * rel_z;
	if (temp >= 0x40000000)
		temp = 0x3FFF0000;
	if (temp <= -0x40000000)
		temp = -0x3FFF0000;
	ry = temp >> 15;
	temp = rot[2] * rel_x + rot[5] * rel_y + rot[8] * rel_z;
	if (temp >= 0x40000000)
		temp = 0x3FFF0000;
	if (temp <= -0x40000000)
		temp = -0x3FFF0000;
	rz = temp >> 15;

	/* Result = rotated + pivot */
	rotatedx = pivot_half + rx;
	rotatedy = pivot_xv + ry;
	rotatedz = pivot_zv + rz;
}

// FUNCTION: TIE95 0x287F4
void fview_transformaxes(int32_t axis_x, int32_t axis_y, int32_t axis_z, uint16_t angle) {
	int32_t temp;
	int32_t cos_a, sin_a;
	int32_t rot[9];
	int32_t new_S1, new_S2, new_U1, new_U2, new_f1, new_f2;

	if (!angle)
		return;

	cos_a = trig2_getsignedcos((int16_t)angle);
	sin_a = trig2_getsignedsin((int16_t)angle);

	if (cos_a >= 0) {
		int32_t one_minus_cos = 0x7FFF - cos_a; /* 1 - cos in Q15 */
		temp = (cos_a * 32768) + one_minus_cos * ((axis_x * axis_x) >> 15);
		if (temp >= 0x40000000)
			temp = 0x3FFF0000;
		if (temp <= -0x40000000)
			temp = -0x3FFF0000;
		rot[0] = temp >> 15;
		temp = ((axis_z * sin_a) >> 15) * 32768 + one_minus_cos * ((axis_y * axis_x) >> 15);
		if (temp >= 0x40000000)
			temp = 0x3FFF0000;
		if (temp <= -0x40000000)
			temp = -0x3FFF0000;
		rot[1] = temp >> 15;
		temp = -32768 * ((axis_y * sin_a) >> 15) + one_minus_cos * ((axis_z * axis_x) >> 15);
		if (temp >= 0x40000000)
			temp = 0x3FFF0000;
		if (temp <= -0x40000000)
			temp = -0x3FFF0000;
		rot[2] = temp >> 15;
		temp = -32768 * ((axis_z * sin_a) >> 15) + one_minus_cos * ((axis_y * axis_x) >> 15);
		if (temp >= 0x40000000)
			temp = 0x3FFF0000;
		if (temp <= -0x40000000)
			temp = -0x3FFF0000;
		rot[3] = temp >> 15;
		temp = (cos_a * 32768) + one_minus_cos * ((axis_y * axis_y) >> 15);
		if (temp >= 0x40000000)
			temp = 0x3FFF0000;
		if (temp <= -0x40000000)
			temp = -0x3FFF0000;
		rot[4] = temp >> 15;
		temp = ((axis_x * sin_a) >> 15) * 32768 + one_minus_cos * ((axis_z * axis_y) >> 15);
		if (temp >= 0x40000000)
			temp = 0x3FFF0000;
		if (temp <= -0x40000000)
			temp = -0x3FFF0000;
		rot[5] = temp >> 15;
		temp = ((axis_y * sin_a) >> 15) * 32768 + one_minus_cos * ((axis_z * axis_x) >> 15);
		if (temp >= 0x40000000)
			temp = 0x3FFF0000;
		if (temp <= -0x40000000)
			temp = -0x3FFF0000;
		rot[6] = temp >> 15;
		temp = -32768 * ((axis_x * sin_a) >> 15) + one_minus_cos * ((axis_z * axis_y) >> 15);
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
		temp = (cos_a * 32768) + axis_x * axis_x + neg_cos * ((axis_x * axis_x) >> 15);
		if (temp >= 0x40000000)
			temp = 0x3FFF0000;
		if (temp <= -0x40000000)
			temp = -0x3FFF0000;
		rot[0] = temp >> 15;
		temp = ((axis_z * sin_a) >> 15) * 32768 + axis_y * axis_x + neg_cos * ((axis_y * axis_x) >> 15);
		if (temp >= 0x40000000)
			temp = 0x3FFF0000;
		if (temp <= -0x40000000)
			temp = -0x3FFF0000;
		rot[1] = temp >> 15;
		temp = -32768 * ((axis_y * sin_a) >> 15) + axis_z * axis_x + neg_cos * ((axis_z * axis_x) >> 15);
		if (temp >= 0x40000000)
			temp = 0x3FFF0000;
		if (temp <= -0x40000000)
			temp = -0x3FFF0000;
		rot[2] = temp >> 15;
		temp = -32768 * ((axis_z * sin_a) >> 15) + axis_y * axis_x + neg_cos * ((axis_y * axis_x) >> 15);
		if (temp >= 0x40000000)
			temp = 0x3FFF0000;
		if (temp <= -0x40000000)
			temp = -0x3FFF0000;
		rot[3] = temp >> 15;
		temp = (cos_a * 32768) + axis_y * axis_y + neg_cos * ((axis_y * axis_y) >> 15);
		if (temp >= 0x40000000)
			temp = 0x3FFF0000;
		if (temp <= -0x40000000)
			temp = -0x3FFF0000;
		rot[4] = temp >> 15;
		temp = ((axis_x * sin_a) >> 15) * 32768 + axis_z * axis_y + neg_cos * ((axis_z * axis_y) >> 15);
		if (temp >= 0x40000000)
			temp = 0x3FFF0000;
		if (temp <= -0x40000000)
			temp = -0x3FFF0000;
		rot[5] = temp >> 15;
		temp = ((axis_y * sin_a) >> 15) * 32768 + axis_z * axis_x + neg_cos * ((axis_z * axis_x) >> 15);
		if (temp >= 0x40000000)
			temp = 0x3FFF0000;
		if (temp <= -0x40000000)
			temp = -0x3FFF0000;
		rot[6] = temp >> 15;
		temp = -32768 * ((axis_x * sin_a) >> 15) + axis_z * axis_y + neg_cos * ((axis_z * axis_y) >> 15);
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
	temp = rot[0] * calcS1 + rot[3] * calcS2 + rot[6] * calcS3;
	if (temp >= 0x40000000)
		temp = 0x3FFF0000;
	if (temp <= -0x40000000)
		temp = -0x3FFF0000;
	new_S1 = temp >> 15;
	temp = rot[1] * calcS1 + rot[4] * calcS2 + rot[7] * calcS3;
	if (temp >= 0x40000000)
		temp = 0x3FFF0000;
	if (temp <= -0x40000000)
		temp = -0x3FFF0000;
	new_S2 = temp >> 15;
	temp = rot[2] * calcS1 + rot[5] * calcS2 + rot[8] * calcS3;
	if (temp >= 0x40000000)
		temp = 0x3FFF0000;
	if (temp <= -0x40000000)
		temp = -0x3FFF0000;
	calcS3 = temp >> 15;
	calcS1 = new_S1;
	calcS2 = new_S2;
	temp = rot[0] * calcU1 + rot[3] * calcU2 + rot[6] * calcU3;
	if (temp >= 0x40000000)
		temp = 0x3FFF0000;
	if (temp <= -0x40000000)
		temp = -0x3FFF0000;
	new_U1 = temp >> 15;
	temp = rot[1] * calcU1 + rot[4] * calcU2 + rot[7] * calcU3;
	if (temp >= 0x40000000)
		temp = 0x3FFF0000;
	if (temp <= -0x40000000)
		temp = -0x3FFF0000;
	new_U2 = temp >> 15;
	temp = rot[2] * calcU1 + rot[5] * calcU2 + rot[8] * calcU3;
	if (temp >= 0x40000000)
		temp = 0x3FFF0000;
	if (temp <= -0x40000000)
		temp = -0x3FFF0000;
	calcU3 = temp >> 15;
	calcU1 = new_U1;
	calcU2 = new_U2;
	temp = rot[0] * calcf1 + rot[3] * calcf2 + rot[6] * calcf3;
	if (temp >= 0x40000000)
		temp = 0x3FFF0000;
	if (temp <= -0x40000000)
		temp = -0x3FFF0000;
	new_f1 = temp >> 15;
	temp = rot[1] * calcf1 + rot[4] * calcf2 + rot[7] * calcf3;
	if (temp >= 0x40000000)
		temp = 0x3FFF0000;
	if (temp <= -0x40000000)
		temp = -0x3FFF0000;
	new_f2 = temp >> 15;
	temp = rot[2] * calcf1 + rot[5] * calcf2 + rot[8] * calcf3;
	if (temp >= 0x40000000)
		temp = 0x3FFF0000;
	if (temp <= -0x40000000)
		temp = -0x3FFF0000;
	calcf3 = temp >> 15;
	calcf1 = new_f1;
	calcf2 = new_f2;
}
