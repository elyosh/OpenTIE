#include "tie_runtime/hooks/orientation.h"

#include <math.h>
#include <string.h>

#define TIE_PI 3.14159265358979323846f
#define TIE_BAM_TO_RAD (TIE_PI / 32768.0f)
#define TIE_RAD_TO_BAM (32768.0f / TIE_PI)
#define TIE_GIMBAL_EPS (1.0e-5f)

static bool s_enabled = true;

bool TieOrientationHook_Enabled(void) { return s_enabled; }

void TieOrientationHook_SetEnabled(bool enabled) { s_enabled = enabled; }

static float TieOrientationHook_BamToRad(int16_t bam) { return (float)bam * TIE_BAM_TO_RAD; }

static int16_t TieOrientationHook_RadToBam(float radians) {
	const float bam = radians * TIE_RAD_TO_BAM;
	if (bam >= 32767.0f)
		return 32767;
	if (bam <= -32768.0f)
		return -32768;
	return (int16_t)lrintf(bam);
}

static void TieOrientationHook_MatrixFromEuler(int16_t heading, int16_t pitch, int16_t roll,
											   float matrix[3][3]) {
	const float beta = TieOrientationHook_BamToRad(heading);
	const float alpha = TieOrientationHook_BamToRad(pitch);
	const float gamma = TieOrientationHook_BamToRad(roll);
	const float cb = cosf(beta), sb = sinf(beta);
	const float ca = cosf(alpha), sa = sinf(alpha);
	const float cg = cosf(gamma), sg = sinf(gamma);
	const float side[3] = { ca, -sa, 0.0f };
	const float up[3] = { -cb * sa, -cb * ca, sb };
	const float forward[3] = { -sb * sa, -sb * ca, -cb };
	const float cross_side[3] = {
		forward[1] * side[2] - forward[2] * side[1],
		forward[2] * side[0] - forward[0] * side[2],
		forward[0] * side[1] - forward[1] * side[0],
	};
	const float cross_up[3] = {
		forward[1] * up[2] - forward[2] * up[1],
		forward[2] * up[0] - forward[0] * up[2],
		forward[0] * up[1] - forward[1] * up[0],
	};
	for (int axis = 0; axis < 3; ++axis) {
		matrix[0][axis] = cg * side[axis] + sg * cross_side[axis];
		matrix[1][axis] = cg * up[axis] + sg * cross_up[axis];
		matrix[2][axis] = forward[axis];
	}
}

static void TieOrientationHook_RotateLocal(float matrix[3][3], int axis, float angle) {
	if (angle == 0.0f)
		return;
	const float x = matrix[axis][0], y = matrix[axis][1], z = matrix[axis][2];
	const float cosine = cosf(angle), sine = sinf(angle), one_minus_cosine = 1.0f - cosine;
	const float rotation[3][3] = {
		{ cosine + x * x * one_minus_cosine, x * y * one_minus_cosine - z * sine,
		  x * z * one_minus_cosine + y * sine },
		{ y * x * one_minus_cosine + z * sine, cosine + y * y * one_minus_cosine,
		  y * z * one_minus_cosine - x * sine },
		{ z * x * one_minus_cosine - y * sine, z * y * one_minus_cosine + x * sine,
		  cosine + z * z * one_minus_cosine },
	};
	float rotated[3][3];
	for (int column = 0; column < 3; ++column)
		for (int row = 0; row < 3; ++row)
			rotated[column][row] = rotation[row][0] * matrix[column][0] +
								   rotation[row][1] * matrix[column][1] +
								   rotation[row][2] * matrix[column][2];
	memcpy(matrix, rotated, sizeof rotated);
}

static void TieOrientationHook_MatrixToQuaternion(const float matrix[3][3], float quaternion[4]) {
	const float m00 = matrix[0][0], m01 = matrix[1][0], m02 = matrix[2][0];
	const float m10 = matrix[0][1], m11 = matrix[1][1], m12 = matrix[2][1];
	const float m20 = matrix[0][2], m21 = matrix[1][2], m22 = matrix[2][2];
	const float trace = m00 + m11 + m22;
	if (trace > 0.0f) {
		const float scale = sqrtf(trace + 1.0f) * 2.0f;
		quaternion[3] = 0.25f * scale;
		quaternion[0] = (m21 - m12) / scale;
		quaternion[1] = (m02 - m20) / scale;
		quaternion[2] = (m10 - m01) / scale;
	} else if (m00 > m11 && m00 > m22) {
		const float scale = sqrtf(1.0f + m00 - m11 - m22) * 2.0f;
		quaternion[3] = (m21 - m12) / scale;
		quaternion[0] = 0.25f * scale;
		quaternion[1] = (m01 + m10) / scale;
		quaternion[2] = (m02 + m20) / scale;
	} else if (m11 > m22) {
		const float scale = sqrtf(1.0f + m11 - m00 - m22) * 2.0f;
		quaternion[3] = (m02 - m20) / scale;
		quaternion[0] = (m01 + m10) / scale;
		quaternion[1] = 0.25f * scale;
		quaternion[2] = (m12 + m21) / scale;
	} else {
		const float scale = sqrtf(1.0f + m22 - m00 - m11) * 2.0f;
		quaternion[3] = (m10 - m01) / scale;
		quaternion[0] = (m02 + m20) / scale;
		quaternion[1] = (m12 + m21) / scale;
		quaternion[2] = 0.25f * scale;
	}
}

static void TieOrientationHook_QuaternionToEuler(const float q[4], float* beta, float* alpha, float* gamma) {
	const float qx = q[0], qy = q[1], qz = q[2], qw = q[3];
	const float xx = qx * qx, yy = qy * qy, zz = qz * qz;
	const float fx = 2.0f * (qx * qz + qy * qw);
	const float fy = 2.0f * (qy * qz - qx * qw);
	const float fz = 1.0f - 2.0f * (xx + yy);
	const float sin_beta = sqrtf(fx * fx + fy * fy);
	*beta = atan2f(sin_beta, -fz);
	if (sin_beta > TIE_GIMBAL_EPS) {
		*alpha = atan2f(-fx, -fy);
		const float cb = -fz, sa = -fx / sin_beta, ca = -fy / sin_beta;
		const float u0x = -cb * sa, u0y = -cb * ca, u0z = sin_beta;
		const float ux = 2.0f * (qx * qy - qz * qw);
		const float uy = 1.0f - 2.0f * (xx + zz);
		const float uz = 2.0f * (qy * qz + qx * qw);
		const float cos_gamma = ux * u0x + uy * u0y + uz * u0z;
		const float sin_gamma =
			ux * (fy * u0z - fz * u0y) + uy * (fz * u0x - fx * u0z) + uz * (fx * u0y - fy * u0x);
		*gamma = atan2f(sin_gamma, cos_gamma);
	} else {
		*alpha = 0.0f;
		const float sx = 1.0f - 2.0f * (yy + zz);
		const float sy = 2.0f * (qx * qy + qz * qw);
		*gamma = fz > 0.0f ? atan2f(sy, sx) : atan2f(-sy, sx);
	}
}

void TieOrientationHook_Apply(int16_t heading, int16_t pitch, int16_t roll, int16_t delta_heading,
							  int16_t delta_pitch, bool allow_yaw, int16_t* out_heading, int16_t* out_pitch,
							  int16_t* out_roll) {
	float matrix[3][3];
	TieOrientationHook_MatrixFromEuler(heading, pitch, roll, matrix);
	TieOrientationHook_RotateLocal(matrix, 0, TieOrientationHook_BamToRad(delta_heading));
	if (allow_yaw)
		TieOrientationHook_RotateLocal(matrix, 1, TieOrientationHook_BamToRad(delta_pitch));
	float quaternion[4];
	TieOrientationHook_MatrixToQuaternion(matrix, quaternion);
	float beta, alpha, gamma;
	TieOrientationHook_QuaternionToEuler(quaternion, &beta, &alpha, &gamma);
	*out_heading = TieOrientationHook_RadToBam(beta);
	if (*out_heading < 0)
		*out_heading = 0;
	*out_pitch = TieOrientationHook_RadToBam(alpha);
	*out_roll = TieOrientationHook_RadToBam(gamma);
}
