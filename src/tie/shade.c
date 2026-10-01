/*
 * SHADE — palette shading for talk/debrief screens.
 * Builds a 256-entry lookup table (shade_palette) that maps each palette
 * index to the nearest available color after shifting toward a target.
 * Used by Draw_Talk_Shade_Rect to darken rectangular regions.
 */

#include "tie/shade.h"
#include "tie/shellext.h"
#ifdef TIE_MODERN
#include "tie_runtime/snapshot/snapshot.h"
#include "tie_runtime/snapshot/snapshot_internal.h"
#endif

#include "landru/bitmap.h"
#include "landru/canvas.h"
#include "landru/paint.h"
#include "landru/pal.h"

#include <stdlib.h>
#include <string.h>

/* --- SHADE global --- */

// GLOBAL: TIE95 0xF57AC
// GLOBAL: TIE98 0x5F3480
uint8_t shade_palette[256];

/* --- Helpers --- */

/*
 * Find which palette entries are available for shade mapping.
 * Entries in active cycling ranges are marked 0 (locked), all others 1.
 */
// FUNCTION: TIE95 0x6CA50
void shade_Find_Shade_Cycles(uint8_t* mask) {
	int i;
	Palette* pal;

	for (i = 0; i < 256; i++)
		mask[i] = 1;

	pal = xpal_Ask_Palette_List();
	while (pal) {
		if (pal->cycle_active) {
			for (i = 0; i < pal->cycle_count; i++) {
				/* Retail gates on the word at offset 0 of the cycle struct
				 * (set in Set_Cycle to +/-1 for any defined cycle), NOT on
				 * the per-Start_Cycle 'active' flag. Match that so cycle
				 * ranges are reserved at palette load time, before
				 * Start_Cycle is called. */
				if (pal->cycles[i].dir) {
					int c;

					for (c = pal->cycles[i].low; c <= pal->cycles[i].high; c++)
						mask[c] = 0;
				}
			}
		}
		pal = pal->next;
	}
}

/* --- Public API --- */

// FUNCTION: TIE95 0x6C720
void shade_Build_Shaded_Palette(void) {
	Palette* dest_pal = xpal_Get_Dest_Palette();
	uint8_t* pal_data = xmemhdl_Lock_Handle(dest_pal->colors);
	if (!pal_data)
		return;

	if (shellext_Get_Cur_Scene() == SCENE_TOUR_DESK) {
		/* Tour desk: shift toward red (63, 0, 0) */
		shade_Set_Shaded_Palette(pal_data, 160, 63, 0, 0);
	} else {
		/* All other scenes: shift toward black (0, 0, 0) */
		shade_Set_Shaded_Palette(pal_data, 160, 0, 0, 0);
	}

	xmemhdl_Unlock_Handle(dest_pal->colors);
}

/*
 * Build a weighted squared-distance table for color matching
 * (dist[i] = cumulative weighted distance, weight[i] = shift amount), then
 * shift each palette entry's RGB toward the target color and find the
 * nearest available color (per the cycle mask) using the distance table.
 */
// FUNCTION: TIE95 0x6C77C
void shade_Set_Shaded_Palette(uint8_t* pal_data, int16_t intensity, int16_t target_r, int16_t target_g,
							  uint16_t target_b) {
	uint16_t dist[256];
	uint8_t weight[256];
	uint8_t cycle_mask[256];
	int32_t inc = intensity;
	int i, j;

	shade_Find_Shade_Cycles(cycle_mask);

	dist[0] = 0;
	weight[0] = 0;
	for (i = 1; i < 256; i++) {
		dist[i] = dist[i - 1] + (2 * i - 1);
		weight[i] = (uint8_t)(inc >> 8);
		inc += intensity;
	}

	for (i = 0; i < 256; i++) {
		uint8_t* src = &pal_data[3 * i];
		int16_t r = src[0];
		int16_t g = src[1];
		int16_t b = src[2];
		int16_t best_dist;
		int16_t best_match;

		/* Shift toward target */
		if (r <= target_r)
			r += weight[target_r - r];
		else
			r -= weight[r - target_r];

		if (g <= target_g)
			g += weight[target_g - g];
		else
			g -= weight[g - target_g];

		if (b <= (int16_t)target_b)
			b += weight[target_b - b];
		else
			b -= weight[b - target_b];

		/* Find nearest available color */
		best_dist = 4095;
		best_match = i;

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

// FUNCTION: TIE95 0x6CAC0
void shade_Draw_Talk_Shade_Rect(Rect* r) {
	int16_t w;
	int16_t h;

	xpaint_Frame_Clipped_Rect(r, 16);

	w = r->right - r->left;
	h = r->bottom - r->top;

	if (w > 2 && h > 2) {
		shade_Shadow_Line_List(shade_palette, r->left + 1, r->top + 1, w - 2, h - 2);

#ifdef TIE_MODERN
		/* Emit a TIE_PAINT_SHADE_RECT for the HD overlay. The classic
		 * FB pixel-walk above mutates indexed pixels in place — those
		 * writes never enter lpaint_*, so without this emit the HD
		 * compositor sees only the xpaint_Frame_Clipped_Rect border
		 * and the dimmed interior is hidden behind the opaque HD
		 * background sprite. Target / intensity mirror the values
		 * used by shade_Build_Shaded_Palette: red for tourdesk,
		 * black for everything else; intensity 160. The compositor
		 * decomposes this into one PMA alpha-over quad whose tint is
		 * (target_RGB * alpha, alpha) with alpha = intensity / 256. */
		if (xcanvas_Render_Emit_Allowed()) {
			TiePaintCmd* out = TieSnapshotBuilder_AllocPaintCmd();
			if (out) {
				int is_tour = (shellext_Get_Cur_Scene() == SCENE_TOUR_DESK);
				/* Engine target is in 0..63 VGA-DAC; rescale to
				 * 0..255 for the compositor (252 = 63 * 4). */
				Rect cc;

				out->op = TIE_PAINT_SHADE_RECT;
				out->pressed = 0;
				out->colors[0] = is_tour ? 252 : 0; /* target R */
				out->colors[1] = 0;                 /* target G */
				out->colors[2] = 0;                 /* target B */
				out->colors[3] = 160;               /* intensity */
				out->colors[4] = 0;
				out->target = xcanvas_Render_Emit_Target();
				out->x = r->left + 1;
				out->y = r->top + 1;
				out->w = w - 2;
				out->h = h - 2;

				xcanvas_Get_Drawing_Canvas_Clip(&cc);
				out->clip_left = cc.left;
				out->clip_top = cc.top;
				out->clip_right = cc.right;
				out->clip_bottom = cc.bottom;
			}
		}
#endif
	}
}

// FUNCTION: TIE95 0x8B270
void shade_Shadow_Line_List(const uint8_t* palette, int16_t x, int16_t y, int16_t width, int16_t height) {
	BitmapStruct* canvas = xcanvas_Get_Current_Canvas_Bitmap();
	uint8_t* pixels = (uint8_t*)xbm_Lock_Bitmap(canvas);
	int16_t stride = canvas->w;

	uint8_t* row = pixels + stride * y + x;

	int16_t h;

	for (h = height; h > 0; h--) {
		int16_t w;

		for (w = width; w > 0; w--) {
			*row = palette[*row];
			row++;
		}
		row += stride - width;
	}

	xbm_Unlock_Bitmap(canvas);
}
