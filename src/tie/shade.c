/*
 * SHADE — palette shading for talk/debrief screens.
 * Builds a 256-entry lookup table (shade_palette) that maps each palette
 * index to the nearest available color after shifting toward a target.
 * Used by Draw_Talk_Shade_Rect to darken rectangular regions.
 */

#include "tie/shade.h"
#include "tie/shellext.h"
#include "tie_runtime/snapshot/snapshot.h"
#include "tie_runtime/snapshot/snapshot_internal.h"

#include "landru/bitmap.h"
#include "landru/canvas.h"
#include "landru/paint.h"
#include "landru/pal.h"

#include <stdlib.h>
#include <string.h>

/* --- SHADE global --- */

uint8_t shade_palette[256];

/* --- Helpers --- */

/*
 * Find which palette entries are available for shade mapping.
 * Entries in active cycling ranges are marked 0 (locked), all others 1.
 */
// FUNCTION: TIE 0x6CA50
void shade_Find_Shade_Cycles(uint8_t* mask) {
	int i;
	Palette* pal;

	for (i = 0; i < 256; i++)
		mask[i] = 1;

	pal = lpal_Ask_Palette_List();
	while (pal) {
		if (pal->cycle_active) {
			for (i = 0; i < pal->cycle_count; i++) {
				/* Retail gates on the word at offset 0 of the cycle struct
				 * (set in Set_Cycle to +/-1 for any defined cycle), NOT on
				 * the per-Start_Cycle 'active' flag. Match that so cycle
				 * ranges are reserved at palette load time, before
				 * Start_Cycle is called. */
				if (pal->cycles[i].dir) {
					for (int c = pal->cycles[i].low; c <= pal->cycles[i].high; c++)
						mask[c] = 0;
				}
			}
		}
		pal = pal->next;
	}
}

/*
 * Build a weighted squared-distance table for color matching.
 * dist[i] = cumulative weighted distance, weight[i] = shift amount.
 * The weighting curve starts slow and accelerates, making small
 * differences cheaper than large ones (perceptual color distance).
 */
static void build_distance_table(uint16_t* dist, uint8_t* weight, int16_t intensity) {
	int16_t accum = 0;
	int32_t inc = intensity;

	dist[0] = 0;
	weight[0] = 0;

	for (int i = 1; i < 256; i++) {
		accum = dist[i - 1] + (2 * i - 1);
		dist[i] = accum;
		weight[i] = (uint8_t)(inc >> 8);
		inc += intensity;
	}
}

/*
 * For each palette entry, shift its RGB toward the target color by
 * the amount given in the weight table, then find the nearest available
 * color (per cycle_mask) using the distance table for matching.
 */
static void build_shade_table(uint8_t* pal_data, const uint16_t* dist, const uint8_t* weight,
							  const uint8_t* cycle_mask, int16_t target_r, int16_t target_g,
							  int16_t target_b) {
	int i, j;

	for (i = 0; i < 256; i++) {
		uint8_t* src = &pal_data[3 * i];
		int16_t r = src[0];
		int16_t g = src[1];
		int16_t b = src[2];

		/* Shift toward target */
		if (r <= target_r)
			r += weight[target_r - r];
		else
			r -= weight[r - target_r];

		if (g <= target_g)
			g += weight[target_g - g];
		else
			g -= weight[g - target_g];

		if (b <= target_b)
			b += weight[target_b - b];
		else
			b -= weight[b - target_b];

		/* Find nearest available color */
		int16_t best_dist = 4095;
		int16_t best_match = i;

		for (j = 0; j < 256; j++) {
			if (cycle_mask[j]) {
				uint8_t* cand = &pal_data[3 * j];
				int16_t d = dist[abs(r - cand[0])] + dist[abs(g - cand[1])] + dist[abs(b - cand[2])];
				if (d < best_dist) {
					best_match = j;
					best_dist = d;
				}
			}
		}

		shade_palette[i] = best_match;
	}
}

/* --- Public API --- */

// FUNCTION: TIE 0x6C720
void shade_Build_Shaded_Palette(void) {
	Palette* dest_pal = lpal_Get_Dest_Palette();
	/* Palette colors are directly addressable host memory. */
	uint8_t* pal_data = (uint8_t*)dest_pal->colors;

	uint16_t dist[256];
	uint8_t weight[256];
	uint8_t cycle_mask[256];

	shade_Find_Shade_Cycles(cycle_mask);
	build_distance_table(dist, weight, 160);

	if (shellext_Get_Cur_Scene() == SCENE_TOUR_DESK) {
		/* Tour desk: shift toward gray (63, 0, 0) */
		build_shade_table(pal_data, dist, weight, cycle_mask, 63, 0, 0);
	} else {
		/* All other scenes: shift toward black (0, 0, 0) */
		build_shade_table(pal_data, dist, weight, cycle_mask, 0, 0, 0);
	}

	/* Binary does Unlock_Handle(pal->colors_handle). No-op with direct pointers. */
}

// FUNCTION: TIE 0x6C77C
void shade_Set_Shaded_Palette(uint8_t* pal_data, int16_t intensity, int16_t target_r, int16_t target_g,
							  uint16_t target_b) {
	uint16_t dist[256];
	uint8_t avail[512]; /* avail[0..255] = weight (from build_distance_table), avail[256..511] = cycle mask */

	/* Build availability mask from palette cycling ranges */
	for (int i = 0; i < 256; i++)
		avail[256 + i] = 1;

	Palette* pal = lpal_Ask_Palette_List();
	while (pal) {
		if (pal->cycle_active) {
			for (int j = 0; j < pal->cycle_count; j++) {
				if (pal->cycles[j].active) {
					for (int c = pal->cycles[j].low; c <= pal->cycles[j].high; c++)
						avail[256 + c] = 0;
				}
			}
		}
		pal = pal->next;
	}

	build_distance_table(dist, avail, intensity);

	/* Build shade table using avail[256..] as cycle mask */
	for (int i = 0; i < 256; i++) {
		uint8_t* src = &pal_data[3 * i];
		int16_t r = src[0];
		int16_t g = src[1];
		int16_t b = src[2];

		if (r <= target_r)
			r += avail[target_r - r];
		else
			r -= avail[r - target_r];

		if (g <= target_g)
			g += avail[target_g - g];
		else
			g -= avail[g - target_g];

		if (b <= (int16_t)target_b)
			b += avail[target_b - b];
		else
			b -= avail[b - target_b];

		int16_t best_dist = 4095;
		int16_t best_match = i;

		for (int j = 0; j < 256; j++) {
			if (avail[256 + j]) {
				uint8_t* cand = &pal_data[3 * j];
				int16_t d = dist[abs(r - cand[0])] + dist[abs(g - cand[1])] + dist[abs(b - cand[2])];
				if (d < best_dist) {
					best_match = j;
					best_dist = d;
				}
			}
		}

		shade_palette[i] = best_match;
	}
}

// FUNCTION: TIE 0x6CAC0
void shade_Draw_Talk_Shade_Rect(Rect* r) {
	lpaint_Frame_Clipped_Rect(r, 16);

	int16_t w = r->right - r->left;
	int16_t h = r->bottom - r->top;

	if (w > 2 && h > 2) {
		shade_Shadow_Line_List(shade_palette, r->left + 1, r->top + 1, w - 2, h - 2);

		/* Emit a TIE_PAINT_SHADE_RECT for the HD overlay. The classic
		 * FB pixel-walk above mutates indexed pixels in place — those
		 * writes never enter lpaint_*, so without this emit the HD
		 * compositor sees only the lpaint_Frame_Clipped_Rect border
		 * and the dimmed interior is hidden behind the opaque HD
		 * background sprite. Target / intensity mirror the values
		 * used by shade_Build_Shaded_Palette: red for tourdesk,
		 * black for everything else; intensity 160. The compositor
		 * decomposes this into one PMA alpha-over quad whose tint is
		 * (target_RGB * alpha, alpha) with alpha = intensity / 256. */
		if (lcanvas_Render_Emit_Allowed()) {
			TiePaintCmd* out = TieSnapshotBuilder_AllocPaintCmd();
			if (out) {
				int is_tour = (shellext_Get_Cur_Scene() == SCENE_TOUR_DESK);
				/* Engine target is in 0..63 VGA-DAC; rescale to
				 * 0..255 for the compositor (252 = 63 * 4). */
				out->op = TIE_PAINT_SHADE_RECT;
				out->pressed = 0;
				out->colors[0] = is_tour ? 252 : 0; /* target R */
				out->colors[1] = 0;                 /* target G */
				out->colors[2] = 0;                 /* target B */
				out->colors[3] = 160;               /* intensity */
				out->colors[4] = 0;
				out->target = lcanvas_Render_Emit_Target();
				out->x = r->left + 1;
				out->y = r->top + 1;
				out->w = w - 2;
				out->h = h - 2;
				Rect cc;
				lcanvas_Get_Drawing_Canvas_Clip(&cc);
				out->clip_left = cc.left;
				out->clip_top = cc.top;
				out->clip_right = cc.right;
				out->clip_bottom = cc.bottom;
			}
		}
	}
}

// FUNCTION: TIE 0x8B270
void shade_Shadow_Line_List(const uint8_t* palette, int16_t x, int16_t y, int16_t width, int16_t height) {
	BitmapStruct* canvas = lcanvas_Get_Current_Canvas_Bitmap();
	uint8_t* pixels = (uint8_t*)lbitmap_Lock_Bitmap(canvas);
	int16_t stride = canvas->w;

	uint8_t* row = pixels + stride * y + x;

	for (int16_t h = height; h > 0; h--) {
		for (int16_t w = width; w > 0; w--) {
			*row = palette[*row];
			row++;
		}
		row += stride - width;
	}

	lbitmap_Unlock_Bitmap(canvas);
}
