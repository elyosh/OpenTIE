#ifndef TIE_REMASTER_FLIGHT_SHADOWS_H
#define TIE_REMASTER_FLIGHT_SHADOWS_H

/*
 * Runtime directional-shadow controls for the Aeron flight renderer.
 *
 * Settings originate in application config.yaml and can be edited live by
 * the debug inspector. Persistent edits are stored as user overrides.
 */

#include "aeron/scene/scene3d.h"
#include "aeron/scene/settings.h"

#ifdef __cplusplus
extern "C" {
#endif
struct TieFlightRenderer;

typedef AeronSceneShadowSettings TieFlightShadowSettings;

void TieFlightRenderer_ShadowsGet(const struct TieFlightRenderer* gpu, TieFlightShadowSettings* out);
void TieFlightRenderer_ShadowsSet(struct TieFlightRenderer* gpu, const TieFlightShadowSettings* settings);
bool TieFlightRenderer_ShadowsDebugCascades(const struct TieFlightRenderer* gpu);
void TieFlightRenderer_ShadowsSetDebugCascades(struct TieFlightRenderer* gpu, bool enabled);
void TieFlightRenderer_ShadowsGetStats(const struct TieFlightRenderer* gpu,
									   AeronSceneDirectionalShadowStats* out);

#ifdef __cplusplus
}
#endif

#endif
