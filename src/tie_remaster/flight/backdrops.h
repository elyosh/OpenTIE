/*
 * flight_backdrop — at-infinity skybox backdrop planets (tie_core BACKDRP2).
 *
 * Submits every tile in the snapshot's TieBackdropSet as a batched
 * SKY-stage scene billboard (AeronScene_AddBillboard) over the base
 * star cubemap: hand-placed PLANET tiles (slot_species 114..116,
 * variant from slot_planet_version) and galaxy/cluster/star filler
 * (117..126). Direction-only (no parallax) — world quads at a large
 * fixed distance from the camera, drawn by the scene after the
 * BEFORE_OPAQUE hook (cubemap) and before scene geometry at the
 * reversed-Z far depth, so ships occlude them.
 *
 * This module keeps only the CPU tile decode + sprite cache; the GPU
 * plumbing lives in aeron_scene_billboards3d.
 */
#ifndef TIE_FLIGHT_BACKDROP_H
#define TIE_FLIGHT_BACKDROP_H

#include <stdbool.h>

#include "aeron/render.h"
#include "tie_runtime/snapshot/snapshot_types.h"

typedef struct AeronScene3D AeronScene3D;
typedef struct TieFlightBackdrop TieFlightBackdrop;
struct TieFlightSpriteCache;
struct TieFlightCamera;

TieFlightBackdrop* TieFlightBackdrop_Create(struct TieFlightSpriteCache* sprites);
void TieFlightBackdrop_Destroy(TieFlightBackdrop*);

/* Decode and queue tiles using the mission-prepared sprite cache. Call
 * between AeronScene_Begin and AeronScene_Render. Returns true if anything
 * was queued. */
bool TieFlightBackdrop_Submit(TieFlightBackdrop*, AeronScene3D* scene, AeronCommandBuffer* cmd,
							  const TieSnapshot* snap, const struct TieFlightCamera* fcam,
							  TieFlightLegacyRenderConvention convention);

#endif /* TIE_FLIGHT_BACKDROP_H */
