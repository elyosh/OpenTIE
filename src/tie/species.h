#ifndef TIE_SPECIES_H
#define TIE_SPECIES_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Per-species data, bitmap palettes, and genus object-slot ranges. */

/* Per-genus half-open range in objects[]. */
typedef struct GenusSlotRange {
	uint16_t start;
	uint16_t limit;
} GenusSlotRange;

extern GenusSlotRange genus_table[16];
extern const uint8_t tie98_model_variant_enabled[161];

/* 8 palette pointers (16 bytes each) used when painting planet sprites
 * on the skybox. Selected via fg.special_flag at mission load. */
extern uint8_t* planetpalptrs[8];

/*
 * Hyperspace starburst polygon data. Owned by species.c per watdbg
 * (lives in the projectile/mesh-data section at 0xC398). 24 bytes,
 * 2 points + 1 edge + a per-frame color-variation byte at +0x14.
 *
 * The two animated x coords (offsets +6 and +12) are mutated by
 * anim_dohyperspace during the hyperspace warp; draw_drawhyperstar
 * writes the per-star color byte at offset +0x14.
 */
extern uint8_t hyperstardata[24];

/* Projectile polygon models: six 183-byte laser bolts and four 186-byte
 * warheads, reached through projectiledataptrs[ship_idx - 137]. */
extern uint8_t rebellaserdata[183];
extern uint8_t turborebellaserdata[183];
extern uint8_t empirelaserdata[183];
extern uint8_t turboempirelaserdata[183];
extern uint8_t ioncannondata[183];
extern uint8_t turboioncannondata[183];
extern uint8_t torpedodata[186];
extern uint8_t concussiondata[186];
extern uint8_t rocketdata[186];
extern uint8_t magneticpulsedata[186];

/* Projectile model table indexed by (ship_idx - 137); slot 13 is NULL and
 * later slots alias earlier models. */
extern const uint8_t* projectiledataptrs[18];

#ifdef __cplusplus
}
#endif

#endif
