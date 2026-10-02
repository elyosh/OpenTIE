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

enum {
	SHADE_SCREEN_WIDTH = 320,
};

/* --- SHADE global --- */

// GLOBAL: TIE95 0xF57AC
// GLOBAL: TIE98 0x5F3480
uint8_t shade_palette[256];

/* --- Public API --- */

// FUNCTION: TIE95 0x6C720
void shade_Build_Shaded_Palette(void) {
	Palette* dest_pal = xpal_Get_Dest_Palette();
	uint8_t* pal_data = xmemhdl_Lock_Handle(dest_pal->colors);

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
int shade_Set_Shaded_Palette(uint8_t* pal_data, int16_t intensity, uint16_t target_r, uint16_t target_g,
							 uint16_t target_b) {
	uint8_t weight[256];
	uint16_t dist[256];
	uint8_t cycle_mask[256];
	int16_t inc = intensity;
	int16_t i, j;

	shade_Find_Shade_Cycles(cycle_mask);

	dist[0] = 0;
	weight[0] = 0;
	for (i = 1; i < 256; i++) {
		dist[i] = dist[i - 1] + (2 * i - 1);
		weight[i] = inc >> 8;
		inc += intensity;
	}

	for (i = 0; i < 256; i++) {
		uint8_t* color = &pal_data[3 * i];
		uint16_t r = color[0];
		uint16_t g = color[1];
		uint16_t b = color[2];
		int16_t best_dist;
		int16_t best_match;

		/* Shift toward target */
		if (r > target_r)
			r -= weight[r - target_r];
		else
			r += weight[target_r - r];

		if (g > target_g)
			g -= weight[g - target_g];
		else
			g += weight[target_g - g];

		if (b > target_b)
			b -= weight[b - target_b];
		else
			b += weight[target_b - b];

		/* Find nearest available color */
		best_dist = 4095;
		best_match = i;

		for (j = 0; j < 256; j++) {
			if (cycle_mask[j]) {
				int16_t d = dist[abs(r - pal_data[3 * j])] + dist[abs(g - pal_data[3 * j + 1])] +
							dist[abs(b - pal_data[3 * j + 2])];
				if (d < best_dist) {
					best_match = j;
					best_dist = d;
				}
			}
		}

		shade_palette[i] = best_match;
	}

	return 1;
}

/* --- Helpers --- */

/*
 * Find which palette entries are available for shade mapping.
 * Entries in active cycling ranges are marked 0 (locked), all others 1.
 */
// FUNCTION: TIE95 0x6CA50
void shade_Find_Shade_Cycles(uint8_t* mask) {
	int16_t i;
	Palette* pal;

	pal = xpal_Ask_Palette_List();

	for (i = 0; i < 256; i++)
		mask[i] = 1;

	while (pal) {
		if (pal->cycle_active) {
			for (i = 0; i < pal->cycle_count; i++) {
				/* Retail gates on the cycle rate (set at palette load for
				 * any defined cycle), NOT on the per-Start_Cycle 'active'
				 * flag, so cycle ranges are reserved before Start_Cycle. */
				if (pal->cycles[i].rate) {
					int16_t c;

					for (c = pal->cycles[i].low; c <= pal->cycles[i].high; c++)
						mask[c] = 0;
				}
			}
		}
		pal = pal->next;
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
	uint8_t* row = pixels + x + y * SHADE_SCREEN_WIDTH;
	int16_t h;

	for (h = height; h > 0; h--) {
		int16_t w;

		for (w = width; w > 0; w--) {
			*row = palette[*row];
			row++;
		}
		row += SHADE_SCREEN_WIDTH - width;
	}

	xbm_Unlock_Bitmap(canvas);
}
