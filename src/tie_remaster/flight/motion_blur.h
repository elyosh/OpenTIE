#ifndef TIE_REMASTER_FLIGHT_MOTION_BLUR_H
#define TIE_REMASTER_FLIGHT_MOTION_BLUR_H

/* Runtime tuning and inspection API for flight motion blur. Accessors operate
 * on an explicit renderer instance. */

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct TieFlightRenderer TieFlightRenderer;

/* Motion-blur quality tier.
 *   MB_OFF  — no velocity_rt; prepass stays 1-RT (depth/normal only);
 *             the renderer flow is the no-motion-blur path.
 *   MB_LOW  — velocity-weighted gather along each pixel's own velocity.
 *   MB_HIGH — adds TileMax/NeighborMax so fast objects streak onto
 *             adjacent pixels; 16-tap gather.
 * Switching to/from MB_OFF lazily (re)allocates velocity_rt + mb_rt —
 * single-threaded with the UI, like the gltf reload path. */
typedef enum { MB_OFF, MB_LOW, MB_HIGH } MbQuality;
MbQuality TieFlightRenderer_MbGetQuality(const TieFlightRenderer* g);
void TieFlightRenderer_MbSetQuality(TieFlightRenderer* g, MbQuality q);

/* Shutter fraction (≈0.5 = 180° shutter). Scales the per-pixel velocity
 * at resolve time (blur length = velocity × shutter). */
float TieFlightRenderer_MbGetShutter(const TieFlightRenderer* g);
void TieFlightRenderer_MbSetShutter(TieFlightRenderer* g, float s);

/* Debug visualisation — when true, a fullscreen pass replaces the scene
 * colour with a false-colour render of velocity_rt (R→+X green, etc.),
 * so the generated velocity (incl. the camera-rotational sky fill) can
 * be inspected. No-op when quality is MB_OFF (no velocity_rt). */
bool TieFlightRenderer_MbGetVelocityViz(const TieFlightRenderer* g);
void TieFlightRenderer_MbSetVelocityViz(TieFlightRenderer* g, bool on);

/* Keep blurring while paused (default true). The prepass keeps
 * regenerating velocity_rt from the frozen snapshots, so the blur holds
 * at the last pre-pause motion — a paused frame of a moving scene
 * legitimately carries motion blur (like pausing a video), and it lets
 * you inspect the effect frame-by-frame. Turn off for a crisp still. */
bool TieFlightRenderer_MbGetPauseKeepBlur(const TieFlightRenderer* g);
void TieFlightRenderer_MbSetPauseKeepBlur(TieFlightRenderer* g, bool on);

/* Blur camera motion (default true). When false, only world-space object
 * motion blurs: the prepass emits object-only velocity (current vertex
 * reprojected through the previous camera, cancelling the camera term) and
 * the camera-rotational sky fill is skipped, so panning over a static scene
 * stays crisp while moving ships / bolts still streak. */
bool TieFlightRenderer_MbGetCameraBlur(const TieFlightRenderer* g);
void TieFlightRenderer_MbSetCameraBlur(TieFlightRenderer* g, bool on);

#ifdef __cplusplus
}
#endif

#endif
