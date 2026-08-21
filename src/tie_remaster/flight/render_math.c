/* Math helpers for the flight renderer's engine-derived conventions. */

#include "tie_remaster/flight/render_math.h"

#include <math.h>
#include <string.h>

#include "aeron/scene/scene3d.h"
#include "aeron/scene/world.h"

#include "tie_runtime/snapshot/snapshot.h"

void TieRenderMath_Mat4Identity(float m[16]) {
	memset(m, 0, sizeof(float) * 16);
	m[0] = m[5] = m[10] = m[15] = 1.0f;
}

/* Build a row-major perspective matrix with reversed-Z mapping and
 * INDEPENDENT X / Y scales taken from the engine's classic half-FOVs.
 * Row layout:
 *   [1/tan(h_half), 0,            0,       0    ]
 *   [0,            -1/tan(v_half),0,       0    ]   (engine Y-down flip)
 *   [0,             0,            0,   near_z   ]   clip.z = near_z (const)
 *   [0,             0,            1,       0    ]   clip.w = z_eye
 * Gives:
 *   ndc.x =  eye_x / eye_z / tan(h_half)
 *   ndc.y = -eye_y / eye_z / tan(v_half)
 *   ndc.z =  near_z / eye_z   (reversed-Z; z_ndc=1 at near → 0 at ∞)
 * near_z controls only the depth mapping; XY scale comes purely from
 * the engine's perspFactor + halfpixelswide/deep + yAspect, NOT from
 * the HD render-target aspect. */
void TieRenderMath_Mat4PerspectiveReverseZ(float m[16], float fov_h_half_rad, float fov_v_half_rad,
										   float near_z) {
	memset(m, 0, sizeof(float) * 16);
	m[0] = 1.0f / tanf(fov_h_half_rad);
	/* Negated for engine's eye-y-down convention (transfm2_getscreeny:
	 * positive eye_y → below screen center). */
	m[5] = -1.0f / tanf(fov_v_half_rad);
	m[11] = near_z;
	m[14] = 1.0f;
}

void TieRenderMath_QuaternionToMat3(const float q[4], float m[9]) {
	const float w = q[0], x = q[1], y = q[2], z = q[3];
	const float xx = x * x, yy = y * y, zz = z * z;
	const float xy = x * y, xz = x * z, yz = y * z;
	const float wx = w * x, wy = w * y, wz = w * z;
	m[0] = 1.0f - 2.0f * (yy + zz);
	m[1] = 2.0f * (xy - wz);
	m[2] = 2.0f * (xz + wy);
	m[3] = 2.0f * (xy + wz);
	m[4] = 1.0f - 2.0f * (xx + zz);
	m[5] = 2.0f * (yz - wx);
	m[6] = 2.0f * (xz - wy);
	m[7] = 2.0f * (yz + wx);
	m[8] = 1.0f - 2.0f * (xx + yy);
}

void TieRenderMath_QuaternionToRenderBasis(const float q[4], float out[9]) {
	TieRenderMath_QuaternionToMat3(q, out);
	/* Column-1 negation: vertex coords use -Y = forward but the
	 * snapshot quaternion stores col 1 = +real_forward (a proper
	 * rotation, det=+1, which is the only thing a quaternion can
	 * encode). Negating col 1 makes vertex Y multiply by
	 * -real_forward, so a mesh vertex at Vy < 0 lands at +forward
	 * in world. det(out) = -1 after this — a reflection, no longer
	 * quaternion-encodable; that's exactly why the snapshot doesn't
	 * pre-apply this and the renderer must. */
	out[1] = -out[1];
	out[4] = -out[4];
	out[7] = -out[7];
}

void TieRenderMath_Mat4FromQuaternionTranslation(float out[16], const float q[4], const float t[3],
												 float scale) {
	float r3[9];
	TieRenderMath_QuaternionToRenderBasis(q, r3);
	out[0] = r3[0] * scale;
	out[1] = r3[1] * scale;
	out[2] = r3[2] * scale;
	out[3] = t[0];
	out[4] = r3[3] * scale;
	out[5] = r3[4] * scale;
	out[6] = r3[5] * scale;
	out[7] = t[1];
	out[8] = r3[6] * scale;
	out[9] = r3[7] * scale;
	out[10] = r3[8] * scale;
	out[11] = t[2];
	out[12] = 0.0f;
	out[13] = 0.0f;
	out[14] = 0.0f;
	out[15] = 1.0f;
}

void TieRenderMath_Mat4Multiply(float out[16], const float a[16], const float b[16]) {
	float r[16];
	for (int row = 0; row < 4; ++row) {
		for (int col = 0; col < 4; ++col) {
			float s = 0.0f;
			for (int k = 0; k < 4; ++k)
				s += a[row * 4 + k] * b[k * 4 + col];
			r[row * 4 + col] = s;
		}
	}
	memcpy(out, r, sizeof r);
}

void TieRenderMath_Mat3x4RotationAboutPivot(float out[3][4], const float axis[3], const float pivot[3],
											float angle) {
	/* Identity when the axis is degenerate (no-rotation entry). */
	float ax = axis[0], ay = axis[1], az = axis[2];
	float len = sqrtf(ax * ax + ay * ay + az * az);
	if (len < 1e-4f) {
		TieRenderMath_Mat3x4Identity(out);
		return;
	}
	ax /= len;
	ay /= len;
	az /= len;
	float c = cosf(angle), s = sinf(angle), omc = 1.0f - c;

	/* Rodrigues — standard rotation matrix. */
	float r00 = c + ax * ax * omc;
	float r01 = ax * ay * omc - az * s;
	float r02 = ax * az * omc + ay * s;
	float r10 = ay * ax * omc + az * s;
	float r11 = c + ay * ay * omc;
	float r12 = ay * az * omc - ax * s;
	float r20 = az * ax * omc - ay * s;
	float r21 = az * ay * omc + ax * s;
	float r22 = c + az * az * omc;

	/* Translation = pivot - R * pivot keeps `pivot` fixed. */
	float tx = pivot[0] - (r00 * pivot[0] + r01 * pivot[1] + r02 * pivot[2]);
	float ty = pivot[1] - (r10 * pivot[0] + r11 * pivot[1] + r12 * pivot[2]);
	float tz = pivot[2] - (r20 * pivot[0] + r21 * pivot[1] + r22 * pivot[2]);

	out[0][0] = r00;
	out[0][1] = r01;
	out[0][2] = r02;
	out[0][3] = tx;
	out[1][0] = r10;
	out[1][1] = r11;
	out[1][2] = r12;
	out[1][3] = ty;
	out[2][0] = r20;
	out[2][1] = r21;
	out[2][2] = r22;
	out[2][3] = tz;
}

void TieRenderMath_Mat3x4Identity(float out[3][4]) {
	out[0][0] = 1.0f;
	out[0][1] = 0.0f;
	out[0][2] = 0.0f;
	out[0][3] = 0.0f;
	out[1][0] = 0.0f;
	out[1][1] = 1.0f;
	out[1][2] = 0.0f;
	out[1][3] = 0.0f;
	out[2][0] = 0.0f;
	out[2][1] = 0.0f;
	out[2][2] = 1.0f;
	out[2][3] = 0.0f;
}

void TieRenderMath_Mat4ViewFromSnapshot(float out[16], const float quat[4], const float pos[3]) {
	float r3[9];
	TieRenderMath_QuaternionToMat3(quat, r3);
	float t[3] = { -pos[0], -pos[1], -pos[2] };
	/* V[0..2][0..2] = r3; V[0..2][3] = r3 * t. */
	float rt[3] = {
		r3[0] * t[0] + r3[1] * t[1] + r3[2] * t[2],
		r3[3] * t[0] + r3[4] * t[1] + r3[5] * t[2],
		r3[6] * t[0] + r3[7] * t[1] + r3[8] * t[2],
	};
	out[0] = r3[0];
	out[1] = r3[1];
	out[2] = r3[2];
	out[3] = rt[0];
	out[4] = r3[3];
	out[5] = r3[4];
	out[6] = r3[5];
	out[7] = rt[1];
	out[8] = r3[6];
	out[9] = r3[7];
	out[10] = r3[8];
	out[11] = rt[2];
	out[12] = 0.0f;
	out[13] = 0.0f;
	out[14] = 0.0f;
	out[15] = 1.0f;
}

void TieRenderMath_Mat3Transpose(const float src[9], float dst[9]) {
	dst[0] = src[0];
	dst[1] = src[3];
	dst[2] = src[6];
	dst[3] = src[1];
	dst[4] = src[4];
	dst[5] = src[7];
	dst[6] = src[2];
	dst[7] = src[5];
	dst[8] = src[8];
}

void TieRenderMath_BuildCamera(TieFlightCamera* out, const struct TieCameraState* cam, int rt_w, int rt_h) {
	TieRenderMath_BuildCameraAtOrigin(out, cam, cam ? cam->world_pos : NULL, rt_w, rt_h);
}

void TieRenderMath_BuildCameraAtOrigin(TieFlightCamera* out, const struct TieCameraState* cam,
									   const int32_t origin_world[3], int rt_w, int rt_h) {
	if (!out)
		return;
	memset(out, 0, sizeof *out);
	out->rt_w = rt_w;
	out->rt_h = rt_h;
	if (!cam || rt_w <= 0 || rt_h <= 0) {
		TieRenderMath_Mat4Identity(out->view_proj);
		return;
	}
	if (origin_world)
		memcpy(out->origin_world, origin_world, sizeof out->origin_world);

	/* The classic framebuffer always occupies a centered, full-height 4:3
	 * reference region. It may extend beyond a future narrower output. */
	const float classic_aspect = 4.0f / 3.0f;
	out->fit_h = (float)rt_h;
	out->fit_w = out->fit_h * classic_aspect;
	out->fit_x = ((float)rt_w - out->fit_w) * 0.5f;
	out->fit_y = 0.0f;

	const float frac_x = cam->viewport_frac_w > 0.0f ? cam->viewport_frac_x : 0.0f;
	const float frac_y = cam->viewport_frac_h > 0.0f ? cam->viewport_frac_y : 0.0f;
	const float frac_w = cam->viewport_frac_w > 0.0f ? cam->viewport_frac_w : 1.0f;
	const float frac_h = cam->viewport_frac_h > 0.0f ? cam->viewport_frac_h : 1.0f;
	const float aperture_w = frac_w * out->fit_w;
	const float aperture_h = frac_h * out->fit_h;
	const float classic_h_half = cam->fov_h_half_rad > 0.0f ? cam->fov_h_half_rad : 0.5586f;
	const float classic_v_half = cam->fov_v_half_rad > 0.0f ? cam->fov_v_half_rad : 0.3f;

	/* The captured half-FOVs belong to the classic cockpit aperture, not
	 * the whole framebuffer. Recover that aperture's focal lengths after
	 * mapping it into the 4:3 reference, then extend the same projection
	 * across the full output. Independent axes preserve DOS yAspect. */
	const float tan_h = tanf(classic_h_half) * (float)rt_w / aperture_w;
	const float tan_v = tanf(classic_v_half) * (float)rt_h / aperture_h;
	out->h_half_rad = atanf(tan_h);
	out->v_half_rad = atanf(tan_v);

	out->flight_vp_x = 0.0f;
	out->flight_vp_y = 0.0f;
	out->flight_vp_w = (float)rt_w;
	out->flight_vp_h = (float)rt_h;

	const float aperture_center_x = out->fit_x + (frac_x + frac_w * 0.5f) * out->fit_w;
	out->proj_x_offset = 2.0f * aperture_center_x / (float)rt_w - 1.0f;

	/* view_proj through the scene's own math (reverse-Z off-center
	 * perspective x quaternion view) — the same composition
	 * AeronScene_Begin runs on the latched camera, via the shared
	 * helper so this application keeps no drift-prone private copy. */
	out->proj_y_offset = TieFlightRenderer_ReticleProjYOffset(frac_y, frac_h, cam->screen_y_offset_ndc,
															  out->flight_vp_y, out->flight_vp_h, rt_w, rt_h);
	float camera_local[3];
	AeronWorld_LocalI32(out->origin_world, cam->world_pos, camera_local);
	out->camera = (AeronSceneCamera) {
		.pos = { camera_local[0], camera_local[1], camera_local[2] },
		.ori = { cam->ori[0], cam->ori[1], cam->ori[2], cam->ori[3] },
		.h_half_rad = out->h_half_rad,
		.v_half_rad = out->v_half_rad,
		.near_z = 1.0f,
		.proj_x_offset = out->proj_x_offset,
		.proj_y_offset = out->proj_y_offset,
	};
	AeronScene_ComputeViewProj(&out->camera, out->view_proj);
}

void TieRenderMath_BuildPipCamera(float out_cam_pos[3], float out_view[16], const int32_t pip_back_step[3],
								  const float pip_cam_ori[4]) {
	float r3[9];
	TieRenderMath_QuaternionToMat3(pip_cam_ori, r3);
	const float row2[3] = { r3[6], r3[7], r3[8] };
	const double x = (double)pip_back_step[0];
	const double y = (double)pip_back_step[1];
	const double z = (double)pip_back_step[2];
	const float bs_mag = (float)sqrt(x * x + y * y + z * z);
	out_cam_pos[0] = -bs_mag * row2[0];
	out_cam_pos[1] = -bs_mag * row2[1];
	out_cam_pos[2] = -bs_mag * row2[2];
	TieRenderMath_Mat4ViewFromSnapshot(out_view, pip_cam_ori, out_cam_pos);
}

float TieFlightRenderer_ReticleProjYOffset(float viewport_frac_y, float viewport_frac_h,
										   float screen_y_offset_ndc, float flight_vp_y, float flight_vp_h,
										   int rt_w, int rt_h) {
	if (flight_vp_h <= 0.0f || rt_w <= 0 || rt_h <= 0) {
		return -screen_y_offset_ndc; /* safe fallback */
	}

	/* The classic reference always fills the output height. This must
	 * stay identical to TieRenderMath_BuildCamera's fit even for a future
	 * output narrower than 4:3, where its sides are clipped. */
	const float fit_h = (float)rt_h;
	const float cock_off_y = 0.0f;

	/* Reticle Y in the classic frame as a fraction of screen height
	 * (transfm2_getscreeny: reticle_pix = pos_y + halfpixelsdeep +
	 * screenyoffset). screen_y_offset_ndc is engine Y-down NDC within
	 * the 3D viewport. */
	const float reticle_classic_frac_y =
		viewport_frac_y + viewport_frac_h * 0.5f * (1.0f + screen_y_offset_ndc);
	const float reticle_pixel = cock_off_y + reticle_classic_frac_y * fit_h;

	/* Solve for proj[6] s.t. NDC.y=proj[6] maps to reticle_pixel
	 * through the FLIGHT viewport. With proj[14]=1 and clip.w=eye.z,
	 * the forward axis (eye.y=0) gives ndc.y = proj[6]. */
	return 1.0f - 2.0f * (reticle_pixel - flight_vp_y) / flight_vp_h;
}
