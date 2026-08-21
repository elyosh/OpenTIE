/* PBR tuning state and per-mission ambient and sun lighting. Aeron's shadow
 * renderer owns b0; TieFlightPbr_BuildPassUniforms supplies the TIE state at
 * b1 space3 for each scene pass. */

#include "tie_remaster/flight/pbr.h"
#include "aeron/aeron.h"
#include "aeron/log.h"

#include "aeron/render.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "tie_remaster/flight/point_lights.h"
#include "tie_remaster/flight/world_ambient.h"

/* ===== Module-private state ========================================= */

static struct {
	bool initialised;
	/* Runtime tuning state. */
	TieFlightPbrUniforms uniforms;
	TieWorldAmbientCube ambient;
	TieWorldAmbientLibrary library;
	int active_battle; /* -1 = uninstalled */
	TieWorldAmbientCube ambient_default;
} g_pbr = {
	.active_battle = -1,
};

/* ===== Lifecycle ==================================================== */

void TieFlightPbr_Init(const char* remaster_dir, const TieFlightPbrConfig* config) {
	if (g_pbr.initialised || !config)
		return;

	memset(&g_pbr.uniforms, 0, sizeof g_pbr.uniforms);
	g_pbr.uniforms.light_intensity = config->light_intensity;
	g_pbr.uniforms.global_spec_mul = config->global_specular_multiplier;
	g_pbr.uniforms.light_wrap = config->light_wrap;
	g_pbr.uniforms.spec_geom_adapt = config->geometric_specular_adaptation ? 1.0f : 0.0f;
	g_pbr.ambient_default = config->ambient_default;
	g_pbr.ambient = g_pbr.ambient_default;
	for (int i = 0; i <= WORLD_AMBIENT_BATTLE_MAX; ++i)
		g_pbr.library.slot[i] = g_pbr.ambient_default;
	g_pbr.library.authored_mask = 0;
	g_pbr.active_battle = -1;
	g_pbr.initialised = true;

	if (remaster_dir && remaster_dir[0]) {
		const char* path = "flight/world_ambient.yaml";
		if (TieWorldAmbient_LoadYaml(Aeron_GetVfs(), AERON_VFS_ROOT_RESOURCE, path, &g_pbr.library))
			Aeron_LogInfo("tie.flight", "world ambient loaded: %s", path);
	}
}

void TieFlightPbr_Shutdown(void) { g_pbr.initialised = false; }

/* ===== Per-pass helpers ============================================= */

void TieFlightPbr_BuildPassUniforms(TieFlightPbrPassUniforms* out, const float cam_pos_world[3],
									const float directional_dir[3], bool xvt_flat,
									const TieFlightPbrAoParams* ao,
									const TieFlightPointLightParams* point_lights) {
	if (!out || !cam_pos_world || !directional_dir || !point_lights)
		return;

	/* Push a copy with the per-pass XvT flag + SSAO inputs stamped in,
	 * leaving the inspector-tuned global state untouched. A NULL `ao`
	 * disables AO (intensity 0 → ambient term unchanged in the FS). */
	TieFlightPbrUniforms pu = g_pbr.uniforms;
	pu.xvt_flat = xvt_flat ? 1.0f : 0.0f;
	if (ao) {
		pu.ssao_intensity = ao->intensity;
		pu.ssao_power = ao->power;
		pu.ssao_rt_w = ao->rt_w;
		pu.ssao_rt_h = ao->rt_h;
		pu.ssao_direct = ao->direct;
	} else {
		pu.ssao_intensity = 0.0f;
		pu.ssao_power = 1.0f;
		pu.ssao_rt_w = 0.0f;
		pu.ssao_rt_h = 0.0f;
		pu.ssao_direct = 0.0f;
	}
	*out = (TieFlightPbrPassUniforms) { 0 };
	out->light_intensity = pu.light_intensity;
	out->global_spec_mul = pu.global_spec_mul;
	out->debug_isolate_term = pu.debug_isolate_term;
	out->light_wrap = pu.light_wrap;
	out->xvt_flat = pu.xvt_flat;
	out->ssao_intensity = pu.ssao_intensity;
	out->ssao_power = pu.ssao_power;
	out->ssao_rt_w = pu.ssao_rt_w;
	out->ssao_rt_h = pu.ssao_rt_h;
	out->ssao_direct = pu.ssao_direct;
	out->spec_geom_adapt = pu.spec_geom_adapt;
	memcpy(out->camera_pos_world, cam_pos_world, sizeof out->camera_pos_world);
	memcpy(out->directional_dir, directional_dir, sizeof out->directional_dir);
	memcpy(out->sun_color, g_pbr.ambient.sun_color, sizeof out->sun_color);
	memcpy(out->amb_pos_x, g_pbr.ambient.pos_x, sizeof out->amb_pos_x);
	memcpy(out->amb_neg_x, g_pbr.ambient.neg_x, sizeof out->amb_neg_x);
	memcpy(out->amb_pos_y, g_pbr.ambient.pos_y, sizeof out->amb_pos_y);
	memcpy(out->amb_neg_y, g_pbr.ambient.neg_y, sizeof out->amb_neg_y);
	memcpy(out->amb_pos_z, g_pbr.ambient.pos_z, sizeof out->amb_pos_z);
	memcpy(out->amb_neg_z, g_pbr.ambient.neg_z, sizeof out->amb_neg_z);
	out->point_params[0] = point_lights->min_distance;
	out->point_params[1] = point_lights->spec_weight;
	out->point_params[2] = point_lights->diffuse_wrap;
	out->point_params[3] = point_lights->contrib_cap;
}

/* ===== Tuning surface (debug-UI tools) ============================== */

void TieFlightPbr_GetUniforms(TieFlightPbrUniforms* out) {
	if (out)
		*out = g_pbr.uniforms;
}

void TieFlightPbr_SetUniforms(const TieFlightPbrUniforms* in) {
	if (in && g_pbr.initialised)
		g_pbr.uniforms = *in;
}

void TieFlightPbr_GetWorldAmbient(TieWorldAmbientCube* out) {
	if (out)
		*out = g_pbr.ambient;
}

void TieFlightPbr_SetWorldAmbient(const TieWorldAmbientCube* in) {
	if (in && g_pbr.initialised)
		g_pbr.ambient = *in;
}

void TieFlightPbr_GetWorldAmbientDefault(TieWorldAmbientCube* out) {
	if (out)
		*out = g_pbr.ambient_default;
}

void TieFlightPbr_RefreshForBattle(uint8_t battle_id) {
	int slot;
	bool authored;
	if (!g_pbr.initialised || g_pbr.active_battle == (int)battle_id)
		return;
	g_pbr.active_battle = (int)battle_id;
	slot = battle_id < WORLD_AMBIENT_BATTLE_MAX ? (int)battle_id + 1 : 0;
	authored = (g_pbr.library.authored_mask & (UINT64_C(1) << slot)) != 0;
	if (!authored)
		slot = 0;
	g_pbr.ambient = g_pbr.library.slot[slot];
}

void TieFlightPbr_GetWorldAmbientLibrary(TieWorldAmbientLibrary* out) {
	int index;
	if (!out)
		return;
	if (g_pbr.initialised) {
		*out = g_pbr.library;
		return;
	}
	for (index = 0; index <= WORLD_AMBIENT_BATTLE_MAX; ++index)
		out->slot[index] = g_pbr.ambient_default;
	out->authored_mask = 0;
}

void TieFlightPbr_SetWorldAmbientLibrary(const TieWorldAmbientLibrary* in) {
	if (!in || !g_pbr.initialised)
		return;
	g_pbr.library = *in;
	g_pbr.active_battle = -1;
}

int TieFlightPbr_GetActiveBattle(void) { return g_pbr.initialised ? g_pbr.active_battle : -1; }
