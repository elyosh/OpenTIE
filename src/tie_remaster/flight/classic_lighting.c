#include "tie_remaster/flight/classic_lighting.h"

#include "aeron/scene/world.h"
#include "tie_remaster/flight/point_lights.h"
#include "tie_remaster/flight/renderer_internal.h"
#include "tie_runtime/snapshot/snapshot.h"

#include <math.h>

static const float k_explosion_color[3] = { 1.0f, 0.75f, 0.30f };
static const float k_ion_explosion_color[3] = { 0.60f, 0.40f, 1.0f };
static const float k_falloff_radius_engine = 64.0f;

uint32_t TieFlightClassicLights_BuildPool(const TieSnapshot* snapshot, const int32_t origin_world[3],
										  TieFlightClassicLightCandidate* out_pool, uint32_t capacity) {
	if (!snapshot || !origin_world || !out_pool || capacity == 0)
		return 0;
	uint32_t count = 0;
	for (uint16_t i = 0; i < snapshot->flight_count && count < capacity; ++i) {
		const TieFlightObjectState* flight = &snapshot->flights[i];
		if (flight->genus != TIE_GENUS_EXPLOSION)
			continue;
		TieFlightClassicLightCandidate* candidate = &out_pool[count++];
		AeronWorld_LocalI32(origin_world, flight->world_pos, candidate->pos_world);
		candidate->range_engine = TieFlightPointLights_SourceexplosionIntensity(
			flight->ship_idx, flight->anim_frame, flight->damage_state);
		const float* color =
			flight->ship_idx == TIE_SPECIES_ION_SPARKLE ? k_ion_explosion_color : k_explosion_color;
		candidate->color[0] = color[0];
		candidate->color[1] = color[1];
		candidate->color[2] = color[2];
		candidate->falloff_radius_engine = k_falloff_radius_engine;
	}
	return count;
}

void TieFlightClassicLights_CullForCraft(const TieFlightClassicLightCandidate* pool, uint32_t pool_count,
										 const float craft_pos_world[3], float craft_bound_radius_world,
										 float craft_scale_world_per_unit, TieFlightLightBufferGpu* out) {
	if (!out)
		return;
	out->light_count = 0;
	if (!pool || pool_count == 0 || !craft_pos_world || craft_scale_world_per_unit <= 0.0f)
		return;

	const float scale = craft_scale_world_per_unit / TIE_CLASSIC_VERTEX_TO_WORLD_UNITS;
	const float inv_scale = 1.0f / scale;
	const float reach_per_range_unit_world = 128.0f * TIE_CLASSIC_VERTEX_TO_WORLD_UNITS;
	for (uint32_t i = 0; i < pool_count && out->light_count < TIE_FLIGHT_CLASSIC_LIGHTS_PER_CRAFT; ++i) {
		const TieFlightClassicLightCandidate* candidate = &pool[i];
		const float dx = candidate->pos_world[0] - craft_pos_world[0];
		const float dy = candidate->pos_world[1] - craft_pos_world[1];
		const float dz = candidate->pos_world[2] - craft_pos_world[2];
		const float distance = sqrtf(dx * dx + dy * dy + dz * dz);
		const float reach_world = candidate->range_engine * reach_per_range_unit_world;
		if (distance > reach_world + craft_bound_radius_world)
			continue;

		TieFlightLightGpu* light = &out->lights[out->light_count++];
		light->pos[0] = candidate->pos_world[0];
		light->pos[1] = candidate->pos_world[1];
		light->pos[2] = candidate->pos_world[2];
		light->range = candidate->range_engine * inv_scale;
		light->color[0] = candidate->color[0];
		light->color[1] = candidate->color[1];
		light->color[2] = candidate->color[2];
		const float falloff = candidate->falloff_radius_engine * inv_scale;
		light->falloff_sq = falloff * falloff;
	}
}
