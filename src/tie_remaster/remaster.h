#ifndef TIE_REMASTER_REMASTER_H
#define TIE_REMASTER_REMASTER_H

/* Process-wide modern renderer. Presentation policy belongs to the application
 * frame loop; this module owns only renderer resources and composition. */

#include <stdbool.h>
#include <stdint.h>

#include "aeron/aeron.h"
#include "tie_remaster/flight/pbr.h"
#include "tie_remaster/flight/point_lights.h"
#include "tie_remaster/flight/render_config.h"
#include "tie_runtime/snapshot/snapshot_types.h"

#ifdef __cplusplus
extern "C" {
#endif

struct TieFlightAssetSource;

/* Decode curve for SDR-authored layers in an HDR composition. sRGB remains
 * representable for configuration and Apple's fixed platform behavior. */
typedef enum TieSdrContentGamma {
	TIE_SDR_CONTENT_GAMMA_2_2 = 0,
	TIE_SDR_CONTENT_GAMMA_2_4,
	TIE_SDR_CONTENT_GAMMA_SRGB,
} TieSdrContentGamma;

typedef struct TieVideoOptions {
	bool hdr;
	TieSdrContentGamma sdr_content_gamma;
	bool paper_white_auto;
	float paper_white_nits;
	int ssao_quality;
	bool shadows_enabled;
	int shadow_atlas_size;
	int fsr_mode;
	float fsr_sharpness;
	int motion_blur_quality;
	float motion_blur_shutter;
	int msaa_samples;
	TieFlightStarfieldStyle starfield_style;
} TieVideoOptions;

typedef struct TieRemasterConfig {
	const char* remaster_dir;
	const char* frontend_profile_id;
	const struct TieFlightAssetSource* flight_source;
	bool aspect_correct_legacy_scenes;
	TieVideoOptions video_options;
	TieFlightRenderConfig render;
	TieFlightPbrConfig pbr;
	TieFlightPointLightParams point_lights;
} TieRemasterConfig;

bool TieRemaster_Init(AeronCommandBuffer* startup_cmd, const TieRemasterConfig* config);
void TieRemaster_BeginFrame(const AeronInputSnapshot* input);
bool TieRemaster_Frame(const TieSnapshot* snapshot, int32_t delta_us, bool paused);
void TieRemaster_ReleaseFlightResources(void);
void TieRemaster_Shutdown(void);
/* Video changes are applied at the next begin-frame boundary so an HDR
 * swapchain reconfiguration cannot occur while frame draws are being built. */
bool TieRemaster_ApplyVideoOptions(const TieVideoOptions* options);
bool TieRemaster_GetVideoOptions(TieVideoOptions* out_options);
bool TieRemaster_SetAspectCorrectLegacyScenes(bool enabled);
bool TieRemaster_SuppressesClassicFlight(const TieSnapshot* snapshot);
/* Switches the renderer to a runtime-selected source and schedules the
 * flight-only GPU resources for rebuilding during the loading sequence. */
bool TieRemaster_SetFlightSource(const struct TieFlightAssetSource* source);

#ifdef __cplusplus
}
#endif

#endif /* TIE_REMASTER_REMASTER_H */
