/* Additive delta renderer. The stream format is:
 *   Header: 4 WORDs (left, top, right, bottom) defining the bounding box
 *   Scanlines: 2-byte length (low bit = compressed, value>>1 = pixel count),
 *     2-byte x offset, 2-byte y offset, then pixel data:
 *     - Compressed (odd length): RLE packets — byte with low bit = run type,
 *       value>>1 = count. Run-fill (bit set): skip source byte, process N pixels.
 *       Raw-copy (bit clear): skip N source bytes, process N pixels.
 *     - Uncompressed (even length): process length/2 sequential pixels.
 *   Terminated by a zero-length scanline.
 *
 * Source pixel values define coverage only; actor->foreColor is added to each
 * covered destination pixel.
 */

#include <stdint.h>
#include <string.h>

#include "landru/actor.h"
#include "landru/bitmap.h"
#include "landru/canvas.h"
#include "landru/rect.h"
#include "tie/deltadd.h"

#define SCREEN_WIDTH 320

/* Delta image header: 4 WORDs defining the bounding box offsets */
typedef struct {
	int16_t left;
	int16_t top;
	int16_t right;
	int16_t bottom;
} DeltaHeader;

static void add_color_run(uint8_t* dst, int16_t count, uint8_t color) {
	for (int16_t i = 0; i < count; i++)
		dst[i] = dst[i] + color;
}

static void copy_transparent(uint8_t* dst, const uint8_t* src, int16_t count) {
	for (int16_t i = 0; i < count; i++) {
		if (src[i])
			dst[i] = src[i];
	}
}

/*
 * Process one delta scanline with additive blending (unclipped).
 * Returns pointer past the consumed scanline data.
 */
static const uint8_t* process_scanline_add(const uint8_t* src, uint8_t* canvas_row, int16_t pixel_count,
										   int compressed, uint8_t color) {
	if (compressed) {
		int16_t remaining = pixel_count;
		while (remaining > 0) {
			uint8_t pack_byte = *src++;
			uint8_t pack_len = pack_byte >> 1;
			if (pack_byte & 1) {
				src++; /* skip fill byte — unused in additive mode */
			} else {
				src += pack_len; /* skip raw bytes */
			}
			add_color_run(canvas_row, pack_len, color);
			canvas_row += pack_len;
			remaining -= pack_len;
		}
	} else {
		add_color_run(canvas_row, pixel_count, color);
		src += pixel_count;
	}
	return src;
}

/*
 * Process one delta scanline with additive blending into a scratch buffer,
 * then clip and composite back to the canvas.
 * Returns pointer past the consumed scanline data.
 */
static const uint8_t* process_scanline_add_clipped(const uint8_t* src, uint8_t* scratch,
												   uint8_t* canvas_pixels, int16_t scan_x, int16_t scan_y,
												   int16_t pixel_count, int compressed, uint8_t color,
												   int16_t clip_left, int16_t clip_top, int16_t clip_right,
												   int16_t clip_bottom) {
	/* First pass: add color to scratch buffer at the scanline's x position */
	uint8_t* scratch_row = &scratch[scan_x];
	uint8_t* canvas_src = &canvas_pixels[scan_y * SCREEN_WIDTH + scan_x];

	if (compressed) {
		int16_t remaining = pixel_count;
		while (remaining > 0) {
			uint8_t pack_byte = *src++;
			uint8_t pack_len = pack_byte >> 1;
			if (pack_byte & 1) {
				src++;
			} else {
				src += pack_len;
			}
			for (int16_t i = 0; i < pack_len; i++)
				scratch_row[i] = color + canvas_src[i];
			scratch_row += pack_len;
			canvas_src += pack_len;
			remaining -= pack_len;
		}
	} else {
		for (int16_t i = 0; i < pixel_count; i++)
			scratch_row[i] = color + canvas_src[i];
		src += pixel_count;
	}

	/* Second pass: clip and copy non-zero pixels from scratch to canvas */
	if (scan_y >= clip_top && scan_y <= clip_bottom) {
		int16_t x_start = scan_x;
		int16_t width = pixel_count;

		if (x_start < clip_left) {
			width -= (clip_left - x_start);
			x_start = clip_left;
		}
		int16_t x_end = scan_x + pixel_count;
		if (x_end > clip_right)
			width -= (x_end - clip_right);

		if (width > 0) {
			uint8_t* dst = &canvas_pixels[scan_y * SCREEN_WIDTH + x_start];
			copy_transparent(dst, &scratch[x_start], width);
		}
	}

	return src;
}

/*
 * Fast unclipped additive delta renderer. Adds color to canvas pixels
 * at each position defined by the delta scanlines.
 */
// FUNCTION: TIE 0x650B0
static void deltadd_Delta_Add_Image(const uint16_t* data, int16_t off_x, int16_t off_y, uint8_t color) {
	BitmapStruct* bm = lcanvas_Get_Current_Canvas_Bitmap();
	uint8_t* canvas = (uint8_t*)lbitmap_Lock_Bitmap(bm);

	uint16_t length = *data++;
	while (length) {
		int16_t scan_x = off_x + (int16_t)*data++;
		int16_t scan_y = off_y + (int16_t)*data++;
		uint8_t* row = &canvas[scan_y * SCREEN_WIDTH + scan_x];

		int compressed = length & 1;
		int16_t pixel_count = length >> 1;

		data =
			(const uint16_t*)process_scanline_add((const uint8_t*)data, row, pixel_count, compressed, color);
		length = *data++;
	}

	lbitmap_Unlock_Bitmap(bm);
}

/*
 * Clipped additive delta renderer. Uses scratch buffer for two-pass
 * rendering: add color to scratch, then clip-copy to canvas.
 */
// FUNCTION: TIE 0x64E2C
static void deltadd_Delta_Add_Clip(const uint16_t* data, int16_t off_x, int16_t off_y, uint8_t color,
								   int16_t clip_left, int16_t clip_top, int16_t clip_right,
								   int16_t clip_bottom) {
	uint8_t scratch[SCREEN_WIDTH];
	memset(scratch, 0, sizeof(scratch));

	BitmapStruct* bm = lcanvas_Get_Current_Canvas_Bitmap();
	uint8_t* canvas = (uint8_t*)lbitmap_Lock_Bitmap(bm);

	uint16_t length = *data++;
	while (length) {
		int16_t scan_x = off_x + (int16_t)*data++;
		int16_t scan_y = off_y + (int16_t)*data++;

		int compressed = length & 1;
		int16_t pixel_count = length >> 1;

		data = (const uint16_t*)process_scanline_add_clipped((const uint8_t*)data, scratch, canvas, scan_x,
															 scan_y, pixel_count, compressed, color,
															 clip_left, clip_top, clip_right, clip_bottom);
		length = *data++;
	}

	lbitmap_Unlock_Bitmap(bm);
}

/*
 * Actor draw callback for additive delta blending.
 * Gets the actor's current frame data, checks bounds against the
 * canvas clip rect, dispatches to unclipped or clipped renderer.
 */
// FUNCTION: TIE 0x64C10
int deltadd_Draw_Delta_Add_Actor(Actor* actor, Rect* draw_rect, Rect* clip_rect, int16_t off_x, int16_t off_y,
								 int16_t refresh) {
	(void)draw_rect;
	(void)clip_rect;

	if (!refresh)
		return 0;

	/* Get the delta image handle for the current animation state */
	void* frame_data = lactor_Get_Actor_Array_Data(actor, actor->state);
	if (!frame_data)
		return 0;

	uint8_t color = (uint8_t)actor->foreColor;

	/* Get canvas clip rect */
	Rect canvas_clip;
	lcanvas_Get_Drawing_Canvas_Clip(&canvas_clip);
	int16_t clip_left = canvas_clip.left;
	int16_t clip_top = canvas_clip.top;
	int16_t clip_w = canvas_clip.right - canvas_clip.left;
	int16_t clip_h = canvas_clip.bottom - canvas_clip.top;

	/* Read delta header */
	const uint16_t* hdr = (const uint16_t*)frame_data;
	int16_t dest_left = off_x + (int16_t)hdr[0];
	int16_t dest_top = off_y + (int16_t)hdr[1];
	int16_t dest_right = off_x + (int16_t)hdr[2];
	int16_t dest_bottom = off_y + (int16_t)hdr[3];
	const uint16_t* data = hdr + 4;

	int drawn = 1;

	/* Check if fully inside canvas clip */
	if (clip_left <= dest_left && clip_top <= dest_top && dest_right < clip_left + clip_w &&
		dest_bottom < clip_top + clip_h) {
		deltadd_Delta_Add_Image(data, off_x, off_y, color);
	}
	/* Check if partially visible */
	else if (dest_left < clip_left + clip_w && dest_top < clip_top + clip_h && dest_right >= clip_left &&
			 dest_bottom >= clip_top) {
		deltadd_Delta_Add_Clip(data, off_x, off_y, color, clip_left, clip_top, clip_left + clip_w - 1,
							   clip_top + clip_h - 1);
	} else {
		drawn = 0;
	}

	return drawn;
}
