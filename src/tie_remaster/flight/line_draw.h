#ifndef TIE_REMASTER_FLIGHT_LINE_DRAW_H
#define TIE_REMASTER_FLIGHT_LINE_DRAW_H

/*
 * Classic-mode line / bolt draw path. Owns the classic line and bolt-
 * line pipelines: projectile bolts (laser / missile streaks) and ship
 * antennas / struts. Both are screen-space line geometry with the
 * engine's distance-driven thickness law.
 *
 * Only called by the orchestrator in classic mesh mode. HD mode renders
 * projectiles and antennas as glb mesh primitives through the cooked-
 * glb pass, so this module is never invoked there.
 *
 * Runs AFTER the classic mesh sub-pass, so the bolt pipeline's depth-
 * test reads ship hulls already in the depth buffer.
 */

#include "aeron/render.h"
#include <stdint.h>

struct TieFlightRenderer;
struct TieSnapshot;
struct TieFlightObjectState;

#ifdef __cplusplus
extern "C" {
#endif

/* Main-pass line+bolt draw. Walks the snapshot's flight objects,
 * frustum-culls them, and emits projectile bolts (always) + ship
 * antennas (classic mode only) through the line / bolt-line pipelines. */
void TieFlightRenderer_LinesDrawPass(struct TieFlightRenderer* g, AeronCommandBuffer* cmd,
									 AeronRenderPass* pass, const struct TieSnapshot* curr,
									 const float view_proj[16]);

/* PIP single-craft line+bolt draw. Emits the line/bolt geometry for
 * the target craft (if it has any). Called after
 * TieFlightRenderer_ClassicDrawSingle so per-craft VS uniforms and the
 * decal SSBO are already pushed for this craft — the function only
 * re-pushes the TieFlightLineVertexUniforms slot and pipeline-specific binds. */
void TieFlightRenderer_LinesDrawSingle(struct TieFlightRenderer* g, AeronCommandBuffer* cmd,
									   AeronRenderPass* pass, const struct TieSnapshot* snap,
									   const struct TieFlightObjectState* fl, const float view_proj[16],
									   int pip_w, int pip_h);

#ifdef __cplusplus
}
#endif

#endif
