/*
 * flight_billboards — snapshot billboard sprites (explosions, debris,
 * lightning) submitted as batched OVERLAY scene billboards.
 *
 * Consumer of the snapshot's TieBillboardState[] queue (populated by
 * tie_snap_emit_billboards). The module keeps the game-shaped CPU
 * work — species→atlas mapping via the TieFlightAssetBundle `billboards:`
 * section (lazy KTX2/YAML load through the shared sprite cache;
 * unauthored species are skipped with a one-shot warning), the
 * selected classic size/rotation/anchor convention, the flat-object
 * depth bias, and the parent-displacement velocity. Batching, pipelines
 * and velocity stamping live in aeron_scene_billboards3d
 * (AeronScene_AddBillboard).
 */
#ifndef TIE_REMASTER_FLIGHT_BILLBOARDS_H
#define TIE_REMASTER_FLIGHT_BILLBOARDS_H

#include <stdbool.h>
#include <stdint.h>

#include "aeron/render.h"
#include "tie_runtime/snapshot/snapshot_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct AeronScene3D AeronScene3D;
typedef struct TieFlightBillboards TieFlightBillboards;
struct TieFlightSpriteCache;
struct TieFlightMotionBlurPrevious;
struct TieFlightCamera;

TieFlightBillboards* TieFlightBillboards_Create(struct TieFlightSpriteCache* sprites);
void TieFlightBillboards_Destroy(TieFlightBillboards*);

/* Queue this snapshot's billboards using the mission-prepared sprite cache.
 * Call between AeronScene_Begin and AeronScene_Render. `mb` carries
 * the previous-snapshot matching for own-motion blur (NULL/disabled =
 * camera-motion-only velocity). Returns true if anything was queued. */
bool TieFlightBillboards_Submit(TieFlightBillboards*, AeronScene3D* scene, AeronCommandBuffer* cmd,
								const TieSnapshot* snap, const struct TieFlightCamera* fcam,
								const struct TieFlightMotionBlurPrevious* mb,
								TieFlightLegacyRenderConvention convention);

#ifdef __cplusplus
}
#endif

#endif /* TIE_REMASTER_FLIGHT_BILLBOARDS_H */
