/* XACT species-billboard decoder. The source RGB triplets are decoded directly
 * instead of palette-quantized. Blob layout:
 *
 *   Main header (u32-indexed):
 *     [0]  body_off          alloc target (unused for extraction)
 *     [3]  data_off          primary RGB array (== 0 in shipped data; subs only)
 *     [4]  subhdr_off_tbl    relative offset to u32[num_subhdrs] frame table
 *     [6]  num_subhdrs       frame count
 *     [11] main_type         24 = primary RGB present (else absent)
 *     [12] main_count        primary RGB entry count
 *
 *   Per sub-frame header at `main + subhdr_offsets[i]` (u32-indexed):
 *     [1]  rgb_data_off      relative offset to N×4-byte RGB triplets
 *     [2]  rle_sub_off       relative offset to RLE sub-header (the
 *                            same +8 field rotscale_rotate_scale_image
 *                            reads as `sub_off`)
 *     [3]  output_off        rewritten at load-time; stale on disk
 *     [4]  sprite_w
 *     [5]  sprite_h
 *     [8]  bit_split         rotscale RLE mask/shift selector
 *     [9]  sub_type          24 = sub has RGB triplets
 *     [10] sub_rgb_count     = N (also the LUT cardinality for prepare_color)
 *
 *   RLE sub-header at `sub + rle_sub_off`:
 *     +0  i16  cox            anchor x (top-left in reverseflag=1)
 *     +4  i16  -coy_neg       anchor y (engine negates this)
 *     +8  i16  cox_b          anchor x (reverseflag=0; flipped variant)
 *     +0x10                   RLE byte stream — see rotatescale at
 *                             src/tie/rotscale.c:1808 for the opcode
 *                             reference.
 *
 * Output pixels carry palette indices
 * 0..N-1 for opaque sprite pixels and the sentinel value 0xFF for
 * transparent (0xFC skip). The companion species_bb_palette accessor
 * returns the RGB triplets so the extractor can render to RGBA8
 * directly.
 */
#ifndef TIE_TOOLS_SPECIES_BILLBOARD_H
#define TIE_TOOLS_SPECIES_BILLBOARD_H

#ifdef __cplusplus
extern "C" {
#endif

#include "imgbake/anim.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Sentinel index for transparent pixels in decoded Image8.pixels.
 * Picked outside any valid palette range — palettes cap at 64 entries
 * (rotscale clamps prepare_color count to 64). */
#define SPECIES_BB_TRANSPARENT 0xFF

/* Per-frame RGB palette resolved by species_bb_decode. count rows of
 * three bytes (R, G, B); count matches sub[+40]. Slot 0 is included —
 * the engine paints it as a real color, not as transparent. */
typedef struct {
	int count;
	uint8_t rgb[64][3]; /* [count][R,G,B] */
} TieSpeciesBillboardPalette;

/* Decode every frame in the XACT blob. On success returns true and
 * fills `*out` with AnimImage frames AND `*palettes_out` with one
 * TieSpeciesBillboardPalette per frame (allocated; caller frees with `free`).
 * Caller frees frames via TieSpeciesBillboard_Free.
 *
 * Index 0xFF in any frame's pixels[] marks a transparent texel; every
 * other value is < `palettes_out[frame].count` and indexes into the
 * matching RGB table. */
bool TieSpeciesBillboard_Decode(AnimImage* out, TieSpeciesBillboardPalette** palettes_out,
								const uint8_t* data, uint32_t size, char* err, size_t errsz);
void TieSpeciesBillboard_Free(AnimImage* frames);

#ifdef __cplusplus
}
#endif

#endif
