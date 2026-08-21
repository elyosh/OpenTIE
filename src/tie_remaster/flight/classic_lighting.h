#ifndef TIE_REMASTER_FLIGHT_CLASSIC_LIGHTS_H
#define TIE_REMASTER_FLIGHT_CLASSIC_LIGHTS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct TieSnapshot;
struct TieFlightLightBufferGpu;

#define TIE_FLIGHT_CLASSIC_LIGHT_POOL_CAP 128
#define TIE_FLIGHT_CLASSIC_LIGHTS_PER_CRAFT 16

typedef struct TieFlightClassicLightCandidate {
	float pos_world[3];
	float range_engine;
	float color[3];
	float falloff_radius_engine;
} TieFlightClassicLightCandidate;

uint32_t TieFlightClassicLights_BuildPool(const struct TieSnapshot* snapshot, const int32_t origin_world[3],
										  TieFlightClassicLightCandidate* out_pool, uint32_t capacity);
void TieFlightClassicLights_CullForCraft(const TieFlightClassicLightCandidate* pool, uint32_t pool_count,
										 const float craft_pos_world[3], float craft_bound_radius_world,
										 float craft_scale_world_per_unit,
										 struct TieFlightLightBufferGpu* out);

#ifdef __cplusplus
}
#endif

#endif
