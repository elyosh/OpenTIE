#ifndef TIE_REMASTER_FLIGHT_SSAO_H
#define TIE_REMASTER_FLIGHT_SSAO_H

/* Runtime tuning and inspection API for SSAO. Accessors operate on an
 * explicit renderer instance. */

#include <stdbool.h>

#include "aeron/scene/settings.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct TieFlightRenderer TieFlightRenderer;

/* SSAO quality tier (runtime-switchable from the debug UI).
 *   SSAO_OFF  — chain skipped; the geometry pass stays monolithic.
 *   SSAO_LOW  — half-res compute, 8-tap golden-angle kernel with IGN
 *               per-pixel rotation, single-pass 3×3 bilateral blur.
 *   SSAO_HIGH — half-res compute, 16-tap noise-rotated kernel, separable
 *               bilateral blur H+V. */
typedef enum { SSAO_OFF, SSAO_LOW, SSAO_HIGH } SsaoQuality;
void TieFlightRenderer_SsaoGet(const TieFlightRenderer* g, AeronSceneSsaoSettings* out);
void TieFlightRenderer_SsaoSet(TieFlightRenderer* g, const AeronSceneSsaoSettings* settings);
SsaoQuality TieFlightRenderer_SsaoGetQuality(const TieFlightRenderer* g);
void TieFlightRenderer_SsaoSetQuality(TieFlightRenderer* g, SsaoQuality q);

/* Strength of AO darkening. 0 = pass entirely off (the geometry pass
 * stays monolithic and the SSAO pass is skipped). Independent of the
 * quality tier: AO is active only when quality != SSAO_OFF AND
 * intensity > 0. */
float TieFlightRenderer_SsaoGetIntensity(const TieFlightRenderer* g);
void TieFlightRenderer_SsaoSetIntensity(TieFlightRenderer* g, float v);

/* Hemisphere radius in view-space units. Larger = broader occlusion
 * (good for big concave hull pockets), smaller = tight contact AO. */
float TieFlightRenderer_SsaoGetRadiusView(const TieFlightRenderer* g);
void TieFlightRenderer_SsaoSetRadiusView(TieFlightRenderer* g, float v);

/* Depth-compare bias preventing flat surfaces from self-occluding due
 * to floating-point precision. Set just above the depth quantisation
 * step at the working range. */
float TieFlightRenderer_SsaoGetBiasView(const TieFlightRenderer* g);
void TieFlightRenderer_SsaoSetBiasView(TieFlightRenderer* g, float v);

/* Nonlinear contrast exponent: `pow(ao, power)`. 1.0 = linear (no
 * change). Higher values push mid-tones toward black, producing a
 * crunchier AO look. Typical range 1.0–4.0. */
float TieFlightRenderer_SsaoGetPower(const TieFlightRenderer* g);
void TieFlightRenderer_SsaoSetPower(TieFlightRenderer* g, float v);

/* How much AO occludes the DIRECT diffuse term (sun·lambert), 0..1.
 * 0 = ambient-only (physically correct, but subtle when the ambient
 * cube is a small fraction of the lighting); 1 = direct diffuse fully
 * occluded too. Specular, emissive, and point lights are never occluded. */
float TieFlightRenderer_SsaoGetDirect(const TieFlightRenderer* g);
void TieFlightRenderer_SsaoSetDirect(TieFlightRenderer* g, float v);

/* Debug visualisation. When true, the apply pass switches to the
 * no-blend debug pipeline and pushes intensity=1, preserve=0 so the
 * scene RT shows raw AO as grayscale. Cockpit overlay still draws on
 * top so you can see context. */
bool TieFlightRenderer_SsaoGetDebugViz(const TieFlightRenderer* g);
void TieFlightRenderer_SsaoSetDebugViz(TieFlightRenderer* g, bool on);

#ifdef __cplusplus
}
#endif

#endif
