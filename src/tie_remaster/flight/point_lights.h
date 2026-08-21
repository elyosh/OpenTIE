#ifndef TIE_REMASTER_FLIGHT_POINT_LIGHTS_H
#define TIE_REMASTER_FLIGHT_POINT_LIGHTS_H

#include <stdbool.h>
#include <stdint.h>

#include "aeron/scene/scene3d.h"

#ifdef __cplusplus
extern "C" {
#endif

struct TieSnapshot;

#define TIE_FLIGHT_POINT_LIGHT_CAPACITY 256

typedef struct TieFlightPointLightParams {
	bool enabled;
	bool clustered;
	int cluster_depth_slices;
	bool cluster_debug;
	float scale;
	float range_scale;
	float min_distance;
	float spec_weight;
	float diffuse_wrap;
	float contrib_cap;
	/* Remaster-only navigation light for the enclosed training course. */
	bool training_headlight_enabled;
	float training_headlight_color[3];
	float training_headlight_intensity;
	float training_headlight_range_m;
	float training_headlight_nose_offset_m;
} TieFlightPointLightParams;

typedef struct TieFlightPointLightFrame {
	AeronSceneLight candidates[TIE_FLIGHT_POINT_LIGHT_CAPACITY];
	uint32_t count;
	uint32_t invalid_count;
	uint32_t dropped_count;
} TieFlightPointLightFrame;

typedef struct TieFlightPointLightStats {
	uint32_t generated_count;
	uint32_t valid_count;
	uint32_t invalid_count;
	uint32_t candidate_overflow_count;
	AeronSceneClusteredLightStats scene;
} TieFlightPointLightStats;

void TieFlightPointLights_Init(const TieFlightPointLightParams* params);
void TieFlightPointLights_GetParams(TieFlightPointLightParams* out);
void TieFlightPointLights_SetParams(const TieFlightPointLightParams* in);
void TieFlightPointLights_GetDefaultParams(TieFlightPointLightParams* out);

void TieFlightPointLights_Derive(TieFlightPointLightFrame* frame, const struct TieSnapshot* snapshot,
								 const int32_t origin_world[3]);
/* Primary-view derivation also inserts main-scene-only light sources before
 * transient scene effects so they retain priority under candidate overflow. */
void TieFlightPointLights_DerivePrimaryView(TieFlightPointLightFrame* frame,
											const struct TieSnapshot* snapshot,
											const int32_t origin_world[3]);
uint32_t TieFlightPointLights_Submit(AeronScene3D* scene, TieFlightPointLightFrame* frame,
									 bool publish_stats);
bool TieFlightPointLights_ConfigureScene(AeronScene3D* scene);
void TieFlightPointLights_CaptureSceneStats(const AeronScene3D* scene);
void TieFlightPointLights_GetStats(TieFlightPointLightStats* out);

/* Recovered TIE explosion animation envelope, shared by the HD point-light
 * source and the classic per-vertex explosion-light path. */
float TieFlightPointLights_SourceexplosionIntensity(uint8_t species, uint8_t animation_frame,
													uint8_t damage_state);

#ifdef __cplusplus
}
#endif

#endif
