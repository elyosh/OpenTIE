#ifndef __SPECIES_H__
#define __SPECIES_H__

#include <stdint.h>

/* Per-species data, bitmap palettes, and genus object-slot ranges. */

/* Per-genus range [start, limit) within the objects[NUM_OBJECTS] table.
 * 16 genera max; only 14 populated in shipped demo. */
extern uint16_t genus[16];
extern uint16_t genus_limit[16];
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

/*
 * Projectile data dispatch table. 18 entries, indexed by projectile kind
 * (laser bank types, warhead types, additional aliased weapon IDs). Each
 * entry points to one of 10 opaque 183-186 byte data blobs owned by
 * species.c (rebellaserdata, turborebellaserdata, empirelaserdata, ...).
 *
 * The blobs themselves are file-static in species.c -- consumers reach
 * them only through this dispatch table. Slot [13] is NULL (reserved).
 *
 * The demo binary has no consumers for this dispatch table.
 */
extern const uint8_t* projectiledataptrs[18];

#endif
