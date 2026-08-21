/*
 * SNAPSHOT-ONLY support module — see snapshot_billboards.h for design.
 *
 * Classic tie_core behaviour is NOT modified by this module. Capture
 * sites are single-line writes to the static arrays below; classic
 * rendering reads nothing from here.
 */

#include "tie_runtime/snapshot/snapshot_billboards.h"

#include "tie/anim.h" /* animop_is_bitmap, animop_bitmap_species/index */
#include "tie/tie.h"  /* NUM_OBJECTS, NUM_CRAFTS */

#include <string.h>

static TieBillboardCaptureFlight s_flight_capture[NUM_OBJECTS];
static TieBillboardCaptureLightning s_lightning_capture[NUM_CRAFTS];

void TieBillboardCapture_BeginTick(void) {
	memset(s_flight_capture, 0, sizeof s_flight_capture);
	memset(s_lightning_capture, 0, sizeof s_lightning_capture);
}

void TieBillboardCapture_Flight(uint16_t obj_slot, uint16_t op, uint16_t pixel_scale_q8,
								uint16_t bound_hwidth, int16_t rotation_bam) {
	if (obj_slot >= NUM_OBJECTS)
		return;
	if (!animop_is_bitmap((AnimOp)op))
		return;

	/* species_idx == 0 in species_table[] is the free-slot sentinel; the
	 * engine never points draw_data at a real billboard with species 0,
	 * so this can only fire if op encodes an unusual value. Reject so the
	 * snapshot's "species == 0 means invalid" invariant holds. */
	uint8_t species_idx = animop_bitmap_species((AnimOp)op);
	if (species_idx == TIE_SPECIES_NONE)
		return;

	TieBillboardCaptureFlight* out = &s_flight_capture[obj_slot];
	out->species_idx = species_idx;
	out->bitmap_idx = animop_bitmap_index((AnimOp)op);
	out->pixel_scale_q8 = pixel_scale_q8;
	out->bound_hwidth = bound_hwidth;
	out->rotation_bam = rotation_bam;
}

void TieBillboardCapture_Lightning(uint16_t craft_slot, uint16_t op, uint16_t pixel_scale_q8,
								   uint16_t bound_hwidth, int16_t rotation_bam) {
	if (craft_slot >= NUM_CRAFTS)
		return;
	if (!animop_is_bitmap((AnimOp)op))
		return;

	uint8_t species_idx = animop_bitmap_species((AnimOp)op);
	if (species_idx == TIE_SPECIES_NONE)
		return;

	TieBillboardCaptureLightning* out = &s_lightning_capture[craft_slot];
	out->active = 1;
	out->species_idx = species_idx;
	out->bitmap_idx = animop_bitmap_index((AnimOp)op);
	out->pixel_scale_q8 = pixel_scale_q8;
	out->bound_hwidth = bound_hwidth;
	out->rotation_bam = rotation_bam;
}

const TieBillboardCaptureFlight* TieBillboardCapture_FlightSlot(uint16_t obj_slot) {
	if (obj_slot >= NUM_OBJECTS)
		return NULL;
	return &s_flight_capture[obj_slot];
}

const TieBillboardCaptureLightning* TieBillboardCapture_LightningSlot(uint16_t craft_slot) {
	if (craft_slot >= NUM_CRAFTS)
		return NULL;
	return &s_lightning_capture[craft_slot];
}
