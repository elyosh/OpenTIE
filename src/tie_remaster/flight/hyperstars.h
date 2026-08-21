#ifndef TIE_REMASTER_FLIGHT_HYPERSTARS_H
#define TIE_REMASTER_FLIGHT_HYPERSTARS_H

/* Procedural hyperspace streaks generated from the flight snapshot. */

#include <stdbool.h>
#include <stdint.h>

#include "aeron/render.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct TieFlightHyperstars TieFlightHyperstars;
struct TieSnapshot;
struct TieFlightCamera;

/* `rt_format` must match the flight color target. */
TieFlightHyperstars* TieFlightHyperstars_Create(AeronTextureFormat rt_format);

void TieFlightHyperstars_Destroy(TieFlightHyperstars*);

/* Prepare uploads geometry before the render pass. DrawInPass uses the
 * matching flight targets and camera inside that pass. */
bool TieFlightHyperstars_Prepare(TieFlightHyperstars*, AeronCommandBuffer* cmd,
								 const struct TieSnapshot* snap, const struct TieFlightCamera* fcam);
void TieFlightHyperstars_DrawInPass(TieFlightHyperstars*, AeronCommandBuffer* cmd, AeronRenderPass* pass,
									const AeronRectI* vp, const struct TieFlightCamera* fcam);

#ifdef __cplusplus
}
#endif

#endif
