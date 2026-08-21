#include "tie_remaster/flight/point_lights.h"

#include "aeron/scene/world.h"
#include "tie_remaster/scene2d/srgb_math.h"
#include "tie_runtime/snapshot/snapshot.h"

#include <math.h>
#include <string.h>

typedef struct TieProjectileLightSource {
	uint8_t species;
	float srgb[3];
	float intensity;
} TieProjectileLightSource;

enum { TIE98_EXPLOSION_LIGHT_MULTIPLIER = 8 };

static const float explosion_srgb[3] = { 0.9f, 0.5f, 0.2f };
static const float ion_srgb[3] = { 0.2f, 0.2f, 1.0f };

static const TieProjectileLightSource k_projectile_sources[] = {
	/* LASERR.OPT / TLASERR.OPT */
	{ TIE_SPECIES_PROJECTILE_REBEL_LASER, { 1.0f, 0.2f, 0.0f }, 200.0f },
	{ TIE_SPECIES_PROJECTILE_REBEL_TURBOLASER, { 1.0f, 0.2f, 0.0f }, 200.0f },
	/* LASERG.OPT / TLASERG.OPT */
	{ TIE_SPECIES_PROJECTILE_IMPERIAL_LASER, { 0.0f, 1.0f, 0.2f }, 200.0f },
	{ TIE_SPECIES_PROJECTILE_IMPERIAL_TURBOLASER, { 0.0f, 1.0f, 0.2f }, 200.0f },
	/* LASERB.OPT / TLASERB.OPT */
	{ TIE_SPECIES_PROJECTILE_ION_CANNON, { 0.2f, 0.2f, 1.0f }, 200.0f },
	{ TIE_SPECIES_PROJECTILE_TURBO_ION_CANNON, { 0.2f, 0.2f, 1.0f }, 200.0f },
	/* MISSILEB.OPT / MISSILER.OPT */
	{ TIE_SPECIES_PROJECTILE_PROTON_TORPEDO, { 0.4f, 0.2f, 1.0f }, 250.0f },
	{ TIE_SPECIES_PROJECTILE_CONCUSSION_MISSILE, { 1.0f, 0.4f, 0.2f }, 250.0f },
	/* TIE98 aliases of the first turbo-laser and warhead assets. */
	{ TIE_SPECIES_PROJECTILE_REBEL_TURBOLASER_2, { 1.0f, 0.2f, 0.0f }, 200.0f },
	{ TIE_SPECIES_PROJECTILE_IMPERIAL_TURBOLASER_2, { 0.0f, 1.0f, 0.2f }, 200.0f },
	{ TIE_SPECIES_PROJECTILE_TURBO_ION_CANNON_2, { 0.2f, 0.2f, 1.0f }, 200.0f },
	{ TIE_SPECIES_PROJECTILE_ADVANCED_PROTON_TORPEDO, { 0.4f, 0.2f, 1.0f }, 250.0f },
	{ TIE_SPECIES_PROJECTILE_ADVANCED_CONCUSSION_MISSILE, { 1.0f, 0.4f, 0.2f }, 250.0f },
	/* BOMB.OPT, MISSILEY.OPT, and MISSILEP.OPT. */
	{ TIE_SPECIES_PROJECTILE_SPACE_BOMB, { 1.0f, 0.4f, 0.2f }, 250.0f },
	{ TIE_SPECIES_PROJECTILE_HEAVY_ROCKET, { 1.0f, 0.8f, 0.1f }, 250.0f },
	{ TIE_SPECIES_PROJECTILE_MAGNETIC_PULSE, { 1.0f, 0.2f, 1.0f }, 250.0f },
	{ TIE_SPECIES_PROJECTILE_MAGNETIC_PULSE_2, { 1.0f, 0.2f, 1.0f }, 250.0f },
	{ TIE_SPECIES_PROJECTILE_MAGNETIC_PULSE_3, { 1.0f, 0.2f, 1.0f }, 250.0f },
};

static struct {
	TieFlightPointLightParams live;
	TieFlightPointLightParams defaults;
	uint32_t generated_count;
	uint32_t valid_count;
	uint32_t invalid_count;
	uint32_t overflow_count;
	AeronSceneClusteredLightStats scene_stats;
} g_point_lights;

float TieFlightPointLights_SourceexplosionIntensity(uint8_t species, uint8_t animation_frame,
													uint8_t damage_state) {
	float intensity = 16.0f;
	if (species >= TIE_SPECIES_BIG_EXPLOSION_1 && species <= TIE_SPECIES_BIG_EXPLOSION_4) {
		switch (animation_frame) {
			case 2:
			case 9:
				intensity = 192.0f;
				break;
			case 3:
			case 5:
			case 6:
			case 7:
			case 8:
				intensity = 320.0f;
				break;
			case 4:
				intensity = 480.0f;
				break;
			case 10:
				intensity = 96.0f;
				break;
			case 11:
				intensity = 48.0f;
				break;
			default:
				break;
		}
		if (damage_state >= 4)
			intensity *= (float)(((int)damage_state + 4) >> 2);
	} else if (species == TIE_SPECIES_DEFLECTOR_SPARKLE || species == TIE_SPECIES_ION_SPARKLE) {
		switch (animation_frame) {
			case 2:
				intensity = 24.0f;
				break;
			case 3:
				intensity = 48.0f;
				break;
			case 4:
				intensity = 32.0f;
				break;
			default:
				break;
		}
	}
	return intensity;
}

static const TieProjectileLightSource* TiePointLights_ProjectileSource(uint8_t species) {
	for (size_t i = 0; i < sizeof k_projectile_sources / sizeof k_projectile_sources[0]; ++i) {
		if (k_projectile_sources[i].species == species)
			return &k_projectile_sources[i];
	}
	return NULL;
}

static void TieFlightPointLights_AppendSource(TieFlightPointLightFrame* frame, const int32_t origin_world[3],
											  const TieFlightObjectState* flight, const float srgb[3],
											  float intensity) {
	if (frame->count >= TIE_FLIGHT_POINT_LIGHT_CAPACITY) {
		frame->dropped_count++;
		return;
	}
	AeronSceneLight* light = &frame->candidates[frame->count++];
	memset(light, 0, sizeof *light);
	AeronWorld_LocalI32(origin_world, flight->world_pos, light->pos);
	light->radius = 50.0f * intensity;
	for (int channel = 0; channel < 3; ++channel)
		light->color[channel] = TieScene2dSrgb_ToLinear(srgb[channel]) * intensity;
}

void TieFlightPointLights_Init(const TieFlightPointLightParams* params) {
	if (!params)
		return;
	memset(&g_point_lights, 0, sizeof g_point_lights);
	g_point_lights.live = *params;
	g_point_lights.defaults = *params;
}

void TieFlightPointLights_GetParams(TieFlightPointLightParams* out) {
	if (out)
		*out = g_point_lights.live;
}

void TieFlightPointLights_SetParams(const TieFlightPointLightParams* in) {
	if (in)
		g_point_lights.live = *in;
}

void TieFlightPointLights_GetDefaultParams(TieFlightPointLightParams* out) {
	if (out)
		*out = g_point_lights.defaults;
}

void TieFlightPointLights_Derive(TieFlightPointLightFrame* frame, const TieSnapshot* snapshot,
								 const int32_t origin_world[3]) {
	if (!frame)
		return;
	memset(frame, 0, sizeof *frame);
	if (!g_point_lights.live.enabled || !snapshot || !origin_world)
		return;

	for (uint16_t i = 0; i < snapshot->flight_count; ++i) {
		const TieFlightObjectState* flight = &snapshot->flights[i];
		if (flight->genus == TIE_GENUS_EXPLOSION) {
			const float intensity = TieFlightPointLights_SourceexplosionIntensity(
										flight->ship_idx, flight->anim_frame, flight->damage_state) *
									(float)TIE98_EXPLOSION_LIGHT_MULTIPLIER;
			TieFlightPointLights_AppendSource(
				frame, origin_world, flight,
				flight->ship_idx == TIE_SPECIES_ION_SPARKLE ? ion_srgb : explosion_srgb, intensity);
			continue;
		}
		if (flight->genus != TIE_GENUS_PROJECTILE_PLAYER && flight->genus != TIE_GENUS_PROJECTILE_NPC)
			continue;
		const TieProjectileLightSource* source = TiePointLights_ProjectileSource(flight->ship_idx);
		if (source)
			TieFlightPointLights_AppendSource(frame, origin_world, flight, source->srgb, source->intensity);
	}
}

uint32_t TieFlightPointLights_Submit(AeronScene3D* scene, TieFlightPointLightFrame* frame,
									 bool publish_stats) {
	if (!scene || !frame)
		return 0;
	const uint32_t generated_count = frame->count + frame->dropped_count;
	uint32_t valid_count = 0;
	for (uint32_t i = 0; i < frame->count; ++i) {
		AeronSceneLight light = frame->candidates[i];
		for (int channel = 0; channel < 3; ++channel)
			light.color[channel] *= g_point_lights.live.scale;
		light.radius *= g_point_lights.live.range_scale;
		const float luminance =
			0.2126f * light.color[0] + 0.7152f * light.color[1] + 0.0722f * light.color[2];
		if (!(light.radius > 0.0f) || !(luminance > 0.0f) || !isfinite(light.radius) ||
			!isfinite(luminance) || !isfinite(light.pos[0]) || !isfinite(light.pos[1]) ||
			!isfinite(light.pos[2]) || !isfinite(light.color[0]) || !isfinite(light.color[1]) ||
			!isfinite(light.color[2])) {
			frame->invalid_count++;
			continue;
		}
		frame->candidates[valid_count++] = light;
	}
	frame->count = valid_count;

	uint32_t accepted = 0;
	for (uint32_t i = 0; i < valid_count; ++i)
		accepted += (uint32_t)AeronScene_AddLight(scene, &frame->candidates[i]);

	if (publish_stats) {
		g_point_lights.generated_count = generated_count;
		g_point_lights.valid_count = valid_count;
		g_point_lights.invalid_count = frame->invalid_count;
		g_point_lights.overflow_count = frame->dropped_count;
		AeronScene_GetClusteredLightStats(scene, &g_point_lights.scene_stats);
	}
	return accepted;
}

bool TieFlightPointLights_ConfigureScene(AeronScene3D* scene) {
	if (!scene)
		return false;
	const TieFlightPointLightParams* params = &g_point_lights.live;
	return AeronScene_SetClusteredLights(scene, &(AeronSceneClusteredLightDesc) {
													.enabled = params->clustered,
													.depth_slices = (uint32_t)params->cluster_depth_slices,
													.min_distance = params->min_distance,
													.contribution_cap = params->contrib_cap,
													.debug_view = params->cluster_debug,
												}) != 0;
}

void TieFlightPointLights_CaptureSceneStats(const AeronScene3D* scene) {
	AeronScene_GetClusteredLightStats(scene, &g_point_lights.scene_stats);
}

void TieFlightPointLights_GetStats(TieFlightPointLightStats* out) {
	if (!out)
		return;
	memset(out, 0, sizeof *out);
	out->generated_count = g_point_lights.generated_count;
	out->valid_count = g_point_lights.valid_count;
	out->invalid_count = g_point_lights.invalid_count;
	out->candidate_overflow_count = g_point_lights.overflow_count;
	out->scene = g_point_lights.scene_stats;
}
