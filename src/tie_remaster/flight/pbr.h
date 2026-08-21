#ifndef TIE_REMASTER_FLIGHT_PBR_H
#define TIE_REMASTER_FLIGHT_PBR_H

/* Process-wide PBR tuning and per-mission ambient lighting state. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "aeron/render.h"

struct AeronRenderPass;
typedef struct TieFlightPointLightParams TieFlightPointLightParams;

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Public tuning types ---------------------------------------- */

/* Debug term-isolation mode for the global inspector. The FS reads
 * `debug_isolate_term` from the tuning prefix of PbrLightFS and
 * replaces the composed output with a single term's contribution. */
typedef enum TieFlightPbrIsolateMode {
	TIE_FLIGHT_PBR_ISOLATE_NONE = 0,
	TIE_FLIGHT_PBR_ISOLATE_BASE = 1, /* base diffuse only */
	TIE_FLIGHT_PBR_ISOLATE_SPEC = 2, /* specular only     */
	TIE_FLIGHT_PBR_ISOLATE_G = 3,    /* Smith-Schlick geometry term (grayscale) */
	/* Diagnostic modes — bypass shading composition entirely so the
	 * intermediate geometric quantity is visible as a grayscale image. */
	TIE_FLIGHT_PBR_ISOLATE_NDOTV = 4,
	TIE_FLIGHT_PBR_ISOLATE_NDOTL = 5,
	TIE_FLIGHT_PBR_ISOLATE_NORMAL = 6,
	TIE_FLIGHT_PBR_ISOLATE_VIEW = 7,
	TIE_FLIGHT_PBR_ISOLATE_NDOTV_SIGNED = 8,
} TieFlightPbrIsolateMode;

typedef struct TieFlightPbrUniforms {
	float light_intensity;    /* directional sun weight */
	float global_spec_mul;    /* overall multiplier on Cook-Torrance
							   * specular (Fresnel is intrinsic to
							   * the lobe, so no separate global). */
	float debug_isolate_term; /* TieFlightPbrIsolateMode cast to float        */
	float light_wrap;         /* 0..1, diffuse softness. 0 = Lambert
							   * (default), 1 = Half-Lambert (full
							   * hemisphere wrap with squaring);
							   * intermediate values blend. */
	float xvt_flat;           /* XvT render style: 0 = off, non-zero =
							   * glTF FS uses flat diffuse-only shading.
							   * Set per-pass by the selected render style, not
							   * via the inspector. */
	/* SSAO — consumed in the FS (occludes the ambient term only). All
	 * four are stamped per-pass from TieFlightPbrAoParams: 0 intensity on
	 * the monolithic / PIP / classic paths (AO off → ambient unchanged),
	 * the live SSAO knobs on the SSAO-active forward pass. */
	float ssao_intensity; /* 0 = AO off; else lerp weight */
	float ssao_power;     /* pow(ao, power) contrast — 1 = linear */
	float ssao_rt_w;      /* scene RT width  — screen-UV denominator */
	float ssao_rt_h;      /* scene RT height */
	/* How much AO additionally occludes the DIRECT diffuse (sun·lambert).
	 * 0 = ambient-only (physically correct, but subtle when ambient is a
	 * small fraction of the lighting); 1 = direct diffuse fully occluded
	 * too. Specular, emissive, and point lights are never occluded. */
	float ssao_direct;
	/* Specular shading-normal adaptation: 0 = use the raw shading normal
	 * (legacy behaviour — hard N·V=0 specular cutoff on the low-poly
	 * Gouraud meshes); 1 = blend toward the geometric face normal as
	 * N·V→0 so specular reflects the real surface. Debug A/B toggle. */
	float spec_geom_adapt;
	float _pad_ssao; /* 16-byte cbuffer row alignment */
} TieFlightPbrUniforms;

typedef struct TieFlightPbrPassUniforms {
	float light_intensity;
	float global_spec_mul;
	float debug_isolate_term;
	float light_wrap;
	float xvt_flat;
	float ssao_intensity;
	float ssao_power;
	float ssao_rt_w;
	float ssao_rt_h;
	float ssao_direct;
	float spec_geom_adapt;
	float _pad_tuning;
	float camera_pos_world[3];
	float _pad_cam;
	float directional_dir[3];
	float _pad_dir;
	float sun_color[3];
	float _pad_sun;
	float amb_pos_x[3];
	float _pad_amb_px;
	float amb_neg_x[3];
	float _pad_amb_nx;
	float amb_pos_y[3];
	float _pad_amb_py;
	float amb_neg_y[3];
	float _pad_amb_ny;
	float amb_pos_z[3];
	float _pad_amb_pz;
	float amb_neg_z[3];
	float _pad_amb_nz;
	float extra_dir[3][4];
	float extra_col[3][4];
	float point_params[4];
	float environment_params[4];
	float environment_right[4];
	float environment_up[4];
	float environment_forward[4];
} TieFlightPbrPassUniforms;

#ifdef __cplusplus
static_assert(offsetof(TieFlightPbrPassUniforms, camera_pos_world) == 48,
			  "PBR tuning prefix must match the shader ABI");
static_assert(offsetof(TieFlightPbrPassUniforms, environment_params) == 304,
			  "PBR environment parameters must match the shader ABI");
static_assert(sizeof(TieFlightPbrPassUniforms) == 368, "PBR pass uniforms must match the shader ABI");
static_assert(sizeof(TieFlightPbrPassUniforms) <= AERON_MAX_UNIFORM_DATA_SIZE,
			  "PBR pass uniforms exceed Aeron's uniform limit");
#else
_Static_assert(offsetof(TieFlightPbrPassUniforms, camera_pos_world) == 48,
			   "PBR tuning prefix must match the shader ABI");
_Static_assert(offsetof(TieFlightPbrPassUniforms, environment_params) == 304,
			   "PBR environment parameters must match the shader ABI");
_Static_assert(sizeof(TieFlightPbrPassUniforms) == 368, "PBR pass uniforms must match the shader ABI");
_Static_assert(sizeof(TieFlightPbrPassUniforms) <= AERON_MAX_UNIFORM_DATA_SIZE,
			   "PBR pass uniforms exceed Aeron's uniform limit");
#endif

/* Per-pass AO inputs for TieFlightPbr_BuildPassUniforms. Pass NULL to
 * disable AO (intensity 0): the FS then leaves the ambient term
 * untouched and never samples the AO texture. */
typedef struct TieFlightPbrAoParams {
	float intensity;  /* SSAO strength (0 = off) */
	float power;      /* pow(ao, power) contrast */
	float rt_w, rt_h; /* scene RT dimensions for screen-UV */
	float direct;     /* AO weight on direct diffuse (0 = ambient-only) */
} TieFlightPbrAoParams;

/* Per-mission ambient + sun lighting environment.
 *
 * The 6 ambient RGBs are one per world axis (±X, ±Y, ±Z) in linear
 * space. The PBR FS blends them by N² along each axis (the Valve /
 * Half-Life-2 ambient-cube formulation) to produce a smooth fill that
 * varies with surface orientation.
 *
 * `sun_color` multiplies the directional sun's lambert + Cook-Torrance
 * contributions; the snapshot's `directional_dir` (carried in
 * PbrLightFS) supplies the direction.
 *
 * Padding mirrors the host-side cbuffer alignment requirement that
 * each vec3 field sits on a 16-byte boundary (HLSL/std140 layout).
 * Authored per-mission via the world-ambient YAML overlay; defaults
 * give a uniform cool fill + neutral white sun. */
typedef struct TieWorldAmbientCube {
	float pos_x[3];
	float _pad_px;
	float neg_x[3];
	float _pad_nx;
	float pos_y[3];
	float _pad_py;
	float neg_y[3];
	float _pad_ny;
	float pos_z[3];
	float _pad_pz;
	float neg_z[3];
	float _pad_nz;
	float sun_color[3];
	float _pad_sun;
} TieWorldAmbientCube; /* sizeof = 112 B; cbuffer-aligned */

typedef struct TieFlightPbrConfig {
	float light_intensity;
	float global_specular_multiplier;
	float light_wrap;
	bool geometric_specular_adaptation;
	TieWorldAmbientCube ambient_default;
} TieFlightPbrConfig;

/* Per-mission ambient library. Slot 0 is the `default:` entry used as
 * fallback; slots 1..WORLD_AMBIENT_BATTLE_MAX are per-battle overrides
 * keyed by battle_id. `authored_mask` bit N tracks whether slot N
 * actually came from YAML (so the resolver can skip un-set slots and
 * fall back to slot 0). Sized one wider than the 13 retail battles
 * for headroom; cbuffer push always uses the active slot only. */
#define WORLD_AMBIENT_BATTLE_MAX 32
typedef struct TieWorldAmbientLibrary {
	TieWorldAmbientCube slot[WORLD_AMBIENT_BATTLE_MAX + 1];
	uint64_t authored_mask;
} TieWorldAmbientLibrary;

/* ---- Lifecycle ------------------------------------------------- */

void TieFlightPbr_Init(const char* remaster_dir, const TieFlightPbrConfig* config);
void TieFlightPbr_Shutdown(void);

/* ---- Per-pass helpers ----------------------------------------------- */

/* Build the FS b1 PbrLightFS block read by Aeron's cooked-glTF shader.
 * Aeron owns b0 for directional-shadow state.
 *
 * The PIP pass calls with its corrected local camera position; the main
 * pass uses zero in its camera-relative frame. `xvt_flat` selects the flat
 * diffuse-only glTF shading for the XvT render style; it overrides the
 * inspector-tuned uniforms for this push only.
 *
 * `ao` carries the per-pass SSAO inputs the FS folds into the ambient
 * term; pass NULL to disable AO (intensity 0) on the HD PIP scene.
 * `point_lights` supplies the frame-global punctual-light response. */
void TieFlightPbr_BuildPassUniforms(TieFlightPbrPassUniforms* out, const float cam_pos_world[3],
									const float directional_dir[3], bool xvt_flat,
									const TieFlightPbrAoParams* ao,
									const TieFlightPointLightParams* point_lights);

/* ---- Tuning surface (debug-UI tools) --------------------------- */

void TieFlightPbr_GetUniforms(TieFlightPbrUniforms* out);
void TieFlightPbr_SetUniforms(const TieFlightPbrUniforms* in);
void TieFlightPbr_GetWorldAmbient(TieWorldAmbientCube* out);
void TieFlightPbr_SetWorldAmbient(const TieWorldAmbientCube* in);
void TieFlightPbr_GetWorldAmbientDefault(TieWorldAmbientCube* out);

/* Battle-driven ambient swap. Picks the library's slot for `battle_id`
 * (fallback: slot 0 = `default:`) and installs it as the live ambient.
 * Called from the per-frame skybox refresh; no-op when the resolved
 * ambient hasn't changed since the last call. */
void TieFlightPbr_RefreshForBattle(uint8_t battle_id);

/* Library accessors — used by the world-ambient YAML loader and the
 * debug-UI editor. */
void TieFlightPbr_GetWorldAmbientLibrary(TieWorldAmbientLibrary* out);
void TieFlightPbr_SetWorldAmbientLibrary(const TieWorldAmbientLibrary* in);

/* Last battle id installed via TieFlightPbr_RefreshForBattle, or -1
 * if none yet. */
int TieFlightPbr_GetActiveBattle(void);

#ifdef __cplusplus
}
#endif

#endif /* TIE_REMASTER_FLIGHT_PBR_H */
