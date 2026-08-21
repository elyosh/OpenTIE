#ifndef TIE_MATH3D_TIE98_H
#define TIE_MATH3D_TIE98_H

typedef struct Vec3f {
	float x;
	float y;
	float z;
} Vec3f;

typedef struct Matrix3x3 {
	float m[9];
} Matrix3x3;

float Math3D_Dot3(const float* lhs, const float* rhs);
void Math3D_RotateVec3(Vec3f* vec, Matrix3x3* matrix);
float Math3D_RotateVec3X(Vec3f* vec, Matrix3x3* matrix);
float Math3D_RotateVec3Y(Vec3f* vec, Matrix3x3* matrix);
float Math3D_RotateVec3Z(Vec3f* vec, Matrix3x3* matrix);
Matrix3x3* Math3D_MulMatrix3x3(Matrix3x3* dst, Matrix3x3* rhs);
Matrix3x3* Math3D_MulMatrix3x3T(Matrix3x3* dst, Matrix3x3* rhs);
Matrix3x3* Math3D_BuildAxisAngleMatrix(Matrix3x3* out, float* axis_angle);

#endif
