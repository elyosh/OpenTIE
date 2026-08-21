#ifndef TIE_REMASTER_FLIGHT_CLASSIC_DRAW_H
#define TIE_REMASTER_FLIGHT_CLASSIC_DRAW_H

/* Classic palette mesh and line rendering for the main flight and PIP
 * passes. Callers provide an open render pass, view-projection matrix,
 * and snapshot state. */

#include "aeron/render.h"
#include <stdint.h>

struct TieFlightRenderer;
struct TieSnapshot;
struct TieFlightObjectState;

#ifdef __cplusplus
extern "C" {
#endif

/* Draw classic meshes, lines, and projectile bolts. When scene models are
 * active, their retained-scene pass owns eligible hulls while projectile
 * bolts remain on this path. */
void TieFlightRenderer_ClassicDrawPass(struct TieFlightRenderer* g, AeronCommandBuffer* cmd,
									   AeronRenderPass* pass, const struct TieSnapshot* curr,
									   const float view_proj[16]);

/* PIP single-craft draw. Renders one targeted craft through the classic
 * mesh + line pipelines at the caller-supplied view_proj. `pip_w/h`
 * size the line VS's pixel→clip scale and the engine `thicknessMultiple`
 * rebase. Returns void; caller owns the render pass. */
void TieFlightRenderer_ClassicDrawSingle(struct TieFlightRenderer* g, AeronCommandBuffer* cmd,
										 AeronRenderPass* pass, const struct TieSnapshot* snap,
										 const struct TieFlightObjectState* fl, const float view_proj[16],
										 int pip_w, int pip_h);

#ifdef __cplusplus
}
#endif

#endif
