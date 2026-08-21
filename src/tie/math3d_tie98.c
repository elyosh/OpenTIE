#include "tie/math3d_tie98.h"

#include <math.h>

// FUNCTION: TIE98 0x420130
float Math3D_Dot3(const float* lhs, const float* rhs) {
	return rhs[1] * lhs[1] + rhs[2] * lhs[2] + lhs[0] * rhs[0];
}

// FUNCTION: TIE98 0x4201D0
float Math3D_RotateVec3X(Vec3f* vec, Matrix3x3* matrix) {
	return matrix->m[6] * vec->z + vec->y * matrix->m[3] + matrix->m[0] * vec->x;
}

// FUNCTION: TIE98 0x420200
float Math3D_RotateVec3Y(Vec3f* vec, Matrix3x3* matrix) {
	return vec->y * matrix->m[4] + vec->z * matrix->m[7] + matrix->m[1] * vec->x;
}

// FUNCTION: TIE98 0x420230
float Math3D_RotateVec3Z(Vec3f* vec, Matrix3x3* matrix) {
	return matrix->m[8] * vec->z + vec->y * matrix->m[5] + matrix->m[2] * vec->x;
}

// FUNCTION: TIE98 0x420260
Matrix3x3* Math3D_MulMatrix3x3(Matrix3x3* dst, Matrix3x3* rhs) {
	float old0;
	float old1;
	float old2;
	float old3;
	float old4;
	float old5;
	float old6;
	float old7;
	float old8;
	float new0;
	float new1;
	float new2;
	float new3;
	float new4;
	float new5;
	float new6;
	float new7;
	float new8;

	old0 = dst->m[0];
	old1 = dst->m[1];
	old2 = dst->m[2];
	old3 = dst->m[3];
	old4 = dst->m[4];
	old5 = dst->m[5];
	old6 = dst->m[6];
	old7 = dst->m[7];
	old8 = dst->m[8];

	new1 = old0 * rhs->m[1] + rhs->m[4] * old1 + rhs->m[7] * old2;
	new2 = old0 * rhs->m[2] + old1 * rhs->m[5] + old2 * rhs->m[8];
	new3 = rhs->m[0] * old3 + rhs->m[6] * old5 + rhs->m[3] * old4;
	new4 = rhs->m[7] * old5 + rhs->m[4] * old4 + rhs->m[1] * old3;
	new5 = old5 * rhs->m[8] + old3 * rhs->m[2] + old4 * rhs->m[5];
	new6 = rhs->m[0] * old6 + old8 * rhs->m[6] + old7 * rhs->m[3];
	new7 = old7 * rhs->m[4] + old8 * rhs->m[7] + old6 * rhs->m[1];
	new8 = old6 * rhs->m[2] + old8 * rhs->m[8] + old7 * rhs->m[5];
	new0 = old1 * rhs->m[3] + rhs->m[0] * old0 + rhs->m[6] * old2;

	dst->m[1] = new1;
	dst->m[2] = new2;
	dst->m[0] = new0;
	dst->m[3] = new3;
	dst->m[4] = new4;
	dst->m[5] = new5;
	dst->m[6] = new6;
	dst->m[7] = new7;
	dst->m[8] = new8;
	return dst;
}

// FUNCTION: TIE98 0x4203E0
Matrix3x3* Math3D_MulMatrix3x3T(Matrix3x3* dst, Matrix3x3* rhs) {
	float old0;
	float old1;
	float old2;
	float old3;
	float old4;
	float old5;
	float old6;
	float old7;
	float old8;
	float new0;
	float new1;
	float new2;
	float new3;
	float new4;
	float new5;
	float new6;
	float new7;
	float new8;

	old0 = dst->m[0];
	old1 = dst->m[1];
	old2 = dst->m[2];
	old3 = dst->m[3];
	old4 = dst->m[4];
	old5 = dst->m[5];
	old6 = dst->m[6];
	old7 = dst->m[7];
	old8 = dst->m[8];

	new1 = rhs->m[0] * old1 + old7 * rhs->m[6] + old4 * rhs->m[3];
	new2 = rhs->m[0] * old2 + rhs->m[6] * old8 + rhs->m[3] * old5;
	new3 = old0 * rhs->m[1] + old6 * rhs->m[7] + old3 * rhs->m[4];
	new4 = old7 * rhs->m[7] + old1 * rhs->m[1] + old4 * rhs->m[4];
	new5 = rhs->m[7] * old8 + rhs->m[1] * old2 + rhs->m[4] * old5;
	new6 = old0 * rhs->m[2] + rhs->m[5] * old3 + rhs->m[8] * old6;
	new7 = rhs->m[5] * old4 + rhs->m[8] * old7 + rhs->m[2] * old1;
	new8 = rhs->m[2] * old2 + rhs->m[5] * old5 + rhs->m[8] * old8;
	new0 = old6 * rhs->m[6] + rhs->m[0] * old0 + rhs->m[3] * old3;

	dst->m[1] = new1;
	dst->m[2] = new2;
	dst->m[0] = new0;
	dst->m[3] = new3;
	dst->m[4] = new4;
	dst->m[5] = new5;
	dst->m[6] = new6;
	dst->m[7] = new7;
	dst->m[8] = new8;
	return dst;
}

// FUNCTION: TIE98 0x420560
Matrix3x3* Math3D_BuildAxisAngleMatrix(Matrix3x3* out, float* axis_angle) {
	float cos_angle;
	float sin_angle;
	float one_minus_cos;
	float axis_x;
	float axis_y;
	float axis_z;

	cos_angle = (float)cos(axis_angle[3]);
	sin_angle = (float)sin(axis_angle[3]);
	one_minus_cos = 1.0f - cos_angle;
	axis_y = axis_angle[1];
	axis_z = axis_angle[2];
	axis_x = axis_angle[0];

	out->m[1] = one_minus_cos * axis_y * axis_x + sin_angle * axis_z;
	out->m[0] = one_minus_cos * axis_x * axis_x + cos_angle;
	out->m[2] = one_minus_cos * axis_z * axis_x - sin_angle * axis_y;
	out->m[3] = one_minus_cos * axis_y * axis_x - sin_angle * axis_z;
	out->m[4] = one_minus_cos * axis_y * axis_y + cos_angle;
	out->m[5] = one_minus_cos * axis_z * axis_y + sin_angle * axis_x;
	out->m[6] = one_minus_cos * axis_z * axis_x + sin_angle * axis_y;
	out->m[7] = one_minus_cos * axis_z * axis_y - sin_angle * axis_x;
	out->m[8] = one_minus_cos * axis_z * axis_z + cos_angle;
	return out;
}

// FUNCTION: TIE98 0x420160
void Math3D_RotateVec3(Vec3f* vec, Matrix3x3* matrix) {
	float x;
	float y;
	float z;
	float m0;
	float m6;

	x = vec->x;
	y = vec->y;
	z = vec->z;
	m0 = matrix->m[0];
	m6 = matrix->m[6];
	vec->x = (m0 * x + m6 * z) + matrix->m[3] * y;
	vec->y = matrix->m[7] * z + matrix->m[4] * y + matrix->m[1] * x;
	vec->z = matrix->m[8] * z + matrix->m[5] * y + matrix->m[2] * x;
}
