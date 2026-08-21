#include <stdint.h>
#include <string.h>

#include "tie/drawstrm.h"

#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 200
#define SCREEN_SIZE (SCREEN_WIDTH * SCREEN_HEIGHT)
#define BLOCK_SIZE 8
#define BLOCKS_X (SCREEN_WIDTH / BLOCK_SIZE)
#define BLOCKS_Y (SCREEN_HEIGHT / BLOCK_SIZE)

/* 4-bit pixel bitmask tables for the DIFF block decoder.
 * Each nibble value (0-15) selects which of the 4 pixels in a half-row
 * to copy from the previous frame. Bit 0 = pixel 0, bit 1 = pixel 1,
 * bit 2 = pixel 2, bit 3 = pixel 3. A set bit means DON'T copy (keep
 * current); a clear bit means copy from reference. */

static void diff_apply_nibble(uint8_t* dst, const uint8_t* ref, uint8_t nibble) {
	if (!(nibble & 0x01))
		dst[0] = ref[0];
	if (!(nibble & 0x02))
		dst[1] = ref[1];
	if (!(nibble & 0x04))
		dst[2] = ref[2];
	if (!(nibble & 0x08))
		dst[3] = ref[3];
}

/*
 * Decode one frame from the stream.
 *
 * prev_frame: 64000-byte reference buffer (read for reference, overwritten
 *             at the end with the decoded frame)
 * stream_data: encoded frame data from the CD stream
 * cur_frame:   64000-byte output buffer for the decoded frame
 */
// FUNCTION: TIE 0x89600
void drawstrm_Convert_Frame_To_Palette(void* prev_frame, void* stream_data, void* cur_frame) {
	uint8_t* prev = (uint8_t*)prev_frame;
	const uint8_t* stream = (const uint8_t*)stream_data;
	uint8_t* cur = (uint8_t*)cur_frame;

	uint8_t* dst_base = cur;
	uint8_t* prev_base = prev;

	for (int by = 0; by < BLOCKS_Y; by++) {
		for (int bx = 0; bx < BLOCKS_X; bx++) {
			uint8_t* dst = dst_base;

			int16_t cmd = *(const int16_t*)stream;
			stream += 2;

			if (cmd == 0x7FFE) {
				/* SKIP: block unchanged from previous decode in cur_frame */
				dst_base += BLOCK_SIZE;
				prev_base += BLOCK_SIZE;
				continue;
			}

			if (cmd == 0x7FFD) {
				/* COPY: copy 8x8 from prev_frame at block-relative offset */
				int16_t offset = *(const int16_t*)stream;
				stream += 2;
				uint8_t* src = &prev_base[offset];
				for (int row = 0; row < BLOCK_SIZE; row++) {
					memcpy(dst, src, BLOCK_SIZE);
					dst += SCREEN_WIDTH;
					src += SCREEN_WIDTH;
				}
				dst_base += BLOCK_SIZE;
				prev_base += BLOCK_SIZE;
				continue;
			}

			if (cmd == 0x7FFF) {
				/* DIFF: per-row bitmask differential */
				int16_t offset = *(const int16_t*)stream;
				stream += 2;
				uint8_t* src = &prev_base[offset];
				for (int row = 0; row < BLOCK_SIZE; row++) {
					uint8_t mask = *stream++;
					uint8_t lo_nibble = mask & 0x0F;
					uint8_t hi_nibble = (mask >> 4) & 0x0F;

					/* Low nibble controls pixels 0-3 */
					diff_apply_nibble(dst, src, lo_nibble);
					/* High nibble controls pixels 4-7 */
					diff_apply_nibble(dst + 4, src + 4, hi_nibble);

					dst += SCREEN_WIDTH;
					src += SCREEN_WIDTH;
				}
				dst_base += BLOCK_SIZE;
				prev_base += BLOCK_SIZE;
				continue;
			}

			/* MIXED: RLE-encoded block with reference at cmd offset */
			uint8_t* mix_ref = &prev_base[cmd];
			int pixels_done = 0;
			int col = 0; /* column within the 8-pixel row */

			while (pixels_done < BLOCK_SIZE * BLOCK_SIZE) {
				uint8_t opcode = *stream++;

				if ((opcode & 1) == 0) {
					/* Raw copy: count = opcode >> 1, pixels follow in stream */
					int count = opcode >> 1;
					pixels_done += count;
					const uint8_t* src_pixels = stream;
					stream += count;

					while (count > 0) {
						int avail = BLOCK_SIZE - col;
						int n = (count < avail) ? count : avail;
						memcpy(dst + col, src_pixels, n);
						src_pixels += n;
						col += n;
						count -= n;
						if (col >= BLOCK_SIZE) {
							col = 0;
							dst += SCREEN_WIDTH;
							mix_ref += SCREEN_WIDTH;
						}
					}
				} else if (opcode == 1) {
					/* Fill: next byte = count, next byte = color */
					int count = (uint8_t)*stream++;
					uint8_t color = *stream++;
					pixels_done += count;

					while (count > 0) {
						int avail = BLOCK_SIZE - col;
						int n = (count < avail) ? count : avail;
						memset(dst + col, color, n);
						col += n;
						count -= n;
						if (col >= BLOCK_SIZE) {
							col = 0;
							dst += SCREEN_WIDTH;
							mix_ref += SCREEN_WIDTH;
						}
					}
				} else if ((opcode & 3) == 3) {
					/* Copy from previous frame: count = opcode >> 2 */
					int count = opcode >> 2;
					pixels_done += count;

					while (count > 0) {
						int avail = BLOCK_SIZE - col;
						int n = (count < avail) ? count : avail;
						memcpy(dst + col, mix_ref + col, n);
						col += n;
						count -= n;
						if (col >= BLOCK_SIZE) {
							col = 0;
							dst += SCREEN_WIDTH;
							mix_ref += SCREEN_WIDTH;
						}
					}
				} else {
					/* Skip: count = opcode >> 2 */
					int count = opcode >> 2;
					pixels_done += count;

					while (count > 0) {
						int avail = BLOCK_SIZE - col;
						int n = (count < avail) ? count : avail;
						col += n;
						count -= n;
						if (col >= BLOCK_SIZE) {
							col = 0;
							dst += SCREEN_WIDTH;
							mix_ref += SCREEN_WIDTH;
						}
					}
				}
			}
			dst_base += BLOCK_SIZE;
			prev_base += BLOCK_SIZE;
		}
		/* Advance to next block row: skip 7 scanlines (already advanced 1 block = 8 pixels wide,
		 * need to move down 8 scanlines total = 8*320 - 320 already covered by line advances = 7*320) */
		dst_base += (BLOCK_SIZE - 1) * SCREEN_WIDTH;
		prev_base += (BLOCK_SIZE - 1) * SCREEN_WIDTH;
	}

	/* Update reference frame for next decode */
	memcpy(prev_frame, cur_frame, SCREEN_SIZE);
}
