#ifndef TIE_REMASTER_FLIGHT_RENDER_CONFIG_H
#define TIE_REMASTER_FLIGHT_RENDER_CONFIG_H

#include "aeron/scene/settings.h"

typedef enum TieFlightTemporalMode {
	TIE_FLIGHT_TEMPORAL_OFF = 0,
	TIE_FLIGHT_TEMPORAL_NATIVE_AA,
	TIE_FLIGHT_TEMPORAL_QUALITY,
	TIE_FLIGHT_TEMPORAL_BALANCED,
	TIE_FLIGHT_TEMPORAL_PERFORMANCE,
} TieFlightTemporalMode;

typedef enum TieFlightStarfieldStyle {
	TIE_FLIGHT_STARFIELD_STYLE_TIE95 = 0,
	TIE_FLIGHT_STARFIELD_STYLE_TIE98,
} TieFlightStarfieldStyle;

typedef struct TieFlightRenderConfig {
	float anisotropy;
	AeronSceneSsaoSettings ssao;
	AeronSceneTonemapSettings tonemap;
	TieFlightTemporalMode temporal_mode;
	float temporal_sharpness;
	int motion_blur_quality;
	float motion_blur_shutter;
	int msaa_samples;
	TieFlightStarfieldStyle starfield_style;
	AeronSceneShadowSettings shadows;
} TieFlightRenderConfig;

#endif
