#ifndef TIE_RUNTIME_SNAPSHOT_ANIMOP_H
#define TIE_RUNTIME_SNAPSHOT_ANIMOP_H

/* Snapshot-side decoding of recovered AnimOp pattern codes (see tie/anim.h
 * for the encoding). Recovered code performs these tests inline. */

#include "tie/anim.h"

static inline int animop_is_mesh(AnimOp op) { return op < 0x8000u; }
static inline int animop_is_bitmap(AnimOp op) { return op >= 0x8000u && op < 0xFF00u; }
/* Bitmap opcode: species and bitmap-within-species indices. */
static inline uint8_t animop_bitmap_species(AnimOp op) { return (uint8_t)((op & 0x7FFFu) >> 7); }
static inline uint8_t animop_bitmap_index(AnimOp op) { return (uint8_t)(op & 0x7Fu); }

#endif
