#ifndef TIE_REMASTER_FLIGHT_RENDER_MATH_H
#define TIE_REMASTER_FLIGHT_RENDER_MATH_H

#include <stdint.h>

#include "aeron/scene/scene3d.h"

/* Matrices are row-major. Quaternions use (w, x, y, z). */

#ifdef __cplusplus
extern "C" {
#endif

/* 4×4 identity. */
void TieRenderMath_Mat4Identity(float m[16]);

/* Reversed-Z infinite-far perspective built from the classic engine's
 * INDEPENDENT horizontal + vertical half-angles. Engine-derived Y-flip
 * is baked in (m[5] = -f_v) so positive eye_y → negative ndc_y per
 * transfm2_getscreeny. Use this variant when the snapshot ships both
 * half-FOVs (i.e. always for cockpit flight) — the X scale is
 * 1/tan(h_half) and the Y scale is 1/tan(v_half), so vertical FOV is
 * NOT derived from horizontal × HD aspect (that path loses the
 * cockpit-viewport aspect + yAspect correction and shifts apparent
 * object scale). */
void TieRenderMath_Mat4PerspectiveReverseZ(float m[16], float fov_h_half_rad, float fov_v_half_rad,
										   float near_z);

/* Quaternion → row-major 3×3 rotation matrix. Returns the proper
 * rotation the quaternion encodes (det = +1, columns = side, fwd,
 * up per the snapshot convention). Use this when you want the
 * craft's actual world-space basis (cameras, telemetry, target
 * tracking). For the *vertex rendering* basis, use
 * TieRenderMath_QuaternionToRenderBasis() instead. */
void TieRenderMath_QuaternionToMat3(const float q[4], float m[9]);

/* Quaternion → row-major 3×3 matrix in the vertex-rendering basis:
 * the raw rotation with column 1 negated. det = -1 (reflection),
 * not a rotation. Use this when building craft_to_world or any
 * transform that maps mesh vertices (where -Y = forward) into world
 * space. */
void TieRenderMath_QuaternionToRenderBasis(const float q[4], float out[9]);

/* Builds an affine transform with the classic reflected render basis. */
void TieRenderMath_Mat4FromQuaternionTranslation(float out[16], const float q[4], const float t[3],
												 float scale);

/* out = a × b (row-major). */
void TieRenderMath_Mat4Multiply(float out[16], const float a[16], const float b[16]);

/* Row-major 3×4 affine: Rodrigues rotation around `axis` (unit-length)
 * through `pivot`, by `angle` radians. Mirrors fview_componentrotation
 * for per-mesh turret animation. */
void TieRenderMath_Mat3x4RotationAboutPivot(float out[3][4], const float axis[3], const float pivot[3],
											float angle);

/* Identity 3×4 affine — used when a mesh has no rotation. */
void TieRenderMath_Mat3x4Identity(float out[3][4]);

/* View matrix from world→eye quaternion + world-space camera position:
 *   V = R * T(-cam_pos). Snapshot ships R as a quaternion encoding
 * the engine's worldeye basis. */
void TieRenderMath_Mat4ViewFromSnapshot(float out[16], const float quat[4], const float pos[3]);

/* 3×3 transpose. */
void TieRenderMath_Mat3Transpose(const float src[9], float dst[9]);

struct TieCameraState;

/* Resolved per-frame camera + viewport rects ready for the flight pass
 * AND any HD overlay that needs to project world coords to the same
 * swapchain pixels. All values are
 * in HD pixels (origin top-left, +Y down) except `view_proj` and the
 * projection offsets, which are in NDC space.
 *
 * The flight render pass uses `flight_vp_*`; reticle and aperture math
 * use `fit_*` (the centered, full-height 4:3 classic reference). The
 * cockpit overlay pass computes its own viewport from the active layout's
 * reference frame via `cockpit_fit_viewport`. */
typedef struct TieFlightCamera {
	int32_t origin_world[3];
	AeronSceneCamera camera;
	float view_proj[16];
	/* Half-FOV angles that landed inside view_proj's perspective rows
	 * (m[0] = 1/tan(h_half_rad), m[5] = -1/tan(v_half_rad)). Exposed so
	 * passes that need an inverse-projection ray (skybox) can rebuild
	 * the same tangent scales. */
	float h_half_rad;
	float v_half_rad;
	/* NDC offsets baked into view_proj. Y is solved from the classic
	 * reticle position; X maps the classic aperture center. */
	float proj_x_offset, proj_y_offset;
	float flight_vp_x, flight_vp_y, flight_vp_w, flight_vp_h;
	float fit_x, fit_y, fit_w, fit_h;
	int rt_w, rt_h;
} TieFlightCamera;

/* Build a full-output `TieFlightCamera` while preserving the classic 3D
 * aperture's focal scale inside the centered 4:3 reference frame. The
 * remaining output area extends that projection instead of rescaling its
 * common content. Also maps the classic aperture center and reticle to
 * projection offsets. */
void TieRenderMath_BuildCamera(TieFlightCamera* out, const struct TieCameraState* cam, int rt_w, int rt_h);
void TieRenderMath_BuildCameraAtOrigin(TieFlightCamera* out, const struct TieCameraState* cam,
									   const int32_t origin_world[3], int rt_w, int rt_h);

/* Build the PIP camera position + view matrix without losing precision:
 *
 *   cam_pos_centered = target_pos - |back_step| × quat_row2
 *   view             = R × T(-cam_pos_centered)
 *
 * The snapshot ships `pip_back_step` as an exact native int32 delta.
 * Using its magnitude along quat row-2 reconstructs cam_pos so the
 * view × target_pos product produces R × back_step without depending
 * on the absolute world position. Same helper used by the PIP
 * mesh pass and by overlays projecting into the PIP viewport. */
void TieRenderMath_BuildPipCamera(float out_cam_pos[3], float out_view[16], const int32_t pip_back_step[3],
								  const float pip_cam_ori[4]);

/* Compute the proj[6] (Y-row Z-column) value that lands the camera's
 * forward axis on the cockpit reticle's swapchain pixel.
 *
 * `screen_y_offset_ndc` is the reticle's Y-down NDC inside the classic
 * 3D aperture. Applying it directly to a full-output projection would
 * move the forward axis by the wrong number of pixels.
 *
 * The reticle's swapchain pixel is anchored to the full-height 4:3
 * classic reference (invariant across cockpit aspect choice).
 * This helper computes that pixel, then derives the proj[6] that
 * places NDC y=+forward_axis at the same pixel through the supplied
 * flight viewport. */
float TieFlightRenderer_ReticleProjYOffset(float viewport_frac_y, float viewport_frac_h,
										   float screen_y_offset_ndc, float flight_vp_y, float flight_vp_h,
										   int rt_w, int rt_h);

#ifdef __cplusplus
}
#endif

#endif
