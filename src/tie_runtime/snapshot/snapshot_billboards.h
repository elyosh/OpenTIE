#ifndef TIE_RUNTIME_SNAPSHOT_BILLBOARDS_H
#define TIE_RUNTIME_SNAPSHOT_BILLBOARDS_H

/* Captures view-dependent billboard values while classic rendering computes
 * them. End-of-tick snapshot capture cannot reconstruct these values because
 * the renderer mutates the world-to-eye basis. BeginTick clears the cache so
 * entries not rendered during the tick remain invalid. */

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Per-flight-slot debris/explosion sprite. pixel_scale_q8 is the final
 * rotscale_calcscale result, including its depth, damage, and clamp rules. */
typedef struct TieBillboardCaptureFlight {
	uint8_t species_idx; /* 0 = not captured this tick */
	uint8_t bitmap_idx;
	uint16_t pixel_scale_q8; /* engine calcscale OUTPUT (Q8.8) */
	uint16_t bound_hwidth;   /* species_table[bitmap_species].bound_hwidth */
	int16_t rotation_bam;
} TieBillboardCaptureFlight;

/* Per-craft-slot capture for the lightning bolt the classic engine
 * emits on critically damaged craft. `active == 0` means no bolt this
 * tick. */
typedef struct TieBillboardCaptureLightning {
	uint8_t active; /* 0 = not captured this tick */
	uint8_t species_idx;
	uint8_t bitmap_idx;
	uint16_t pixel_scale_q8;
	uint16_t bound_hwidth;
	int16_t rotation_bam;
} TieBillboardCaptureLightning;

/* Reset the cache for a new tick. Called by TieSnapshotBuilder_BeginTick. */
void TieBillboardCapture_BeginTick(void);

/* Called from anim_drawverysimpleobject's bitmap branch with the
 * rotscale_calcscale output and the engine-derived rotation. Safe to
 * call with `op` that isn't an is_bitmap opcode — the call is then a
 * no-op. */
void TieBillboardCapture_Flight(uint16_t obj_slot, uint16_t op, uint16_t pixel_scale_q8,
								uint16_t bound_hwidth, int16_t rotation_bam);

/* Called from draw_drawcraft's lightning emit branch. */
void TieBillboardCapture_Lightning(uint16_t craft_slot, uint16_t op, uint16_t pixel_scale_q8,
								   uint16_t bound_hwidth, int16_t rotation_bam);

/* Drain accessors used by tie_snap_emit_billboards. */
const TieBillboardCaptureFlight* TieBillboardCapture_FlightSlot(uint16_t obj_slot);
const TieBillboardCaptureLightning* TieBillboardCapture_LightningSlot(uint16_t craft_slot);

#ifdef __cplusplus
}
#endif

#endif
