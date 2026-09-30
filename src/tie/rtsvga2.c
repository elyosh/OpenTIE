#include "tie/rtsvga2.h"
#include "landru/vesa.h" /* vesa_buff_gbl — the scanout buffer vgapointer aliases */
#include "tie/frontend_display_tie98.h"
#include "tie/logbuf2.h" /* pixelswide / pixelsdeep / halfpixels / displaycorner / deepspacecolor */
#include "tie/math2.h"
#include "tie/math2_wide.h"
#include "tie/panel.h" /* RadarBlip (x, y, color) */
#include "tie/render_texture_tie98.h"
#include "tie/tie.h"
#include "tie/transfm2.h" /* transfm2_screenyoffset, worldeye* */
#include "tie/xtrans2.h"
#include "tie_runtime/audio/config.h"
#include "tie_runtime/diagnostics/diagnostics.h"
#include "tie_runtime/display/classic_display.h"
#include "tie_runtime/display/classic_framebuffer.h"
#include "tie_runtime/flight_assets/model_types.h"
#include "tie_runtime/input/input.h"
#include "tie_runtime/runtime/exports.h"
#include "tie_runtime/runtime/profile.h"
#include "tie_runtime/storage/storage.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Module-local static state (watdbg "static" in demo; retail matches) */
/* ------------------------------------------------------------------ */

/* 10-point bracket shape (5-wide x 3-tall dx/dy deltas around centre).
 * Bytes verified against retail binary at 0xC7A20. */
static const int8_t bracketdef_10pt[20] = { -1, +1, -2, +1, -2, 0, -2, -1, -1, -1,
											+1, -1, +2, -1, +2, 0, +2, +1, +1, +1 };

/* 12-point bracket shape (5-wide x 4-tall; new in retail for VESA 640x480).
 * Bytes verified against retail binary at 0xC7A34. */
static const int8_t bracketdef_12pt[24] = { -1, +2, -2, +2, -2, +1, -2, 0,  -2, -1, -1, -1,
											+1, -1, +2, -1, +2, 0,  +2, +1, +2, +2, +1, +2 };

/* 7-point cross shape (4 arms + centre duplicate). Bytes verified
 * against retail binary at 0xC7A54. */
static const int8_t crossdef[14] = { -2, 0, -1, 0, 0, 0, +1, 0, +2, 0, 0, +1, 0, -1 };

/* Pointer+count selected by initgraphVGA based on flightResolution. */
static const int8_t* bracketdef_ptr = bracketdef_10pt;
static uint32_t bracketdef_count = 10;

/* Saved pixels under the bracket / cross so remove* can restore them. */
// GLOBAL: TIE95 0xDC414
static uint8_t bracketsave[16];
// GLOBAL: TIE98 0x5897B0
static uint16_t bracketsave_tie98[10];
static uint8_t crosssave[7];

/* Last VESA window-A / window-B page (-1 = unknown, force BIOS call). */
static uint32_t lastpageA = 0xFFFFFFFFu;
static uint32_t lastpageB = 0xFFFFFFFFu;

/* fillrectangle / autofill scratch state (module-local in demo watdbg). */
// GLOBAL: TIE95 0xDE772
static int16_t topfill;
// GLOBAL: TIE95 0xDE776
static int16_t bottomfill;
// GLOBAL: TIE95 0xDE77A
static int16_t leftfill;
// GLOBAL: TIE95 0xDE77C
static int16_t rightfill;
static int16_t starty;  /* fillrectangle per-row y counter */
static uint32_t htemp2; /* compiler register-spill slot in retail */

/* Brightness setting from OPTION_optionsroom (0..256; 256 = unchanged). */
uint32_t brightness_setting = 256;

/* Retail screenshot counter -> screen%d.pcx */
static int screenshot_seq;

/* ------------------------------------------------------------------ */
/* RTSVGA2-owned shared globals (extern in header)                    */
/* ------------------------------------------------------------------ */

uint8_t rtsvga2_vgapalette[768];
// GLOBAL: TIE95 0xC7A04
uint8_t* vgapointer;
/* One-past-the-screen sentinel: drawshape's 0xFE end-of-line bumps
 * drawshapey before the outer loop checks for 0xFF (end-of-shape), so an
 * icon whose last row sits on screenYRes-1 triggers a single trailing
 * read at index screenYRes. The retail database had the same 480 layout
 * and tolerated the OOB read because no write follows (the next opcode
 * is 0xFF which returns); we size [481] purely so UBSan doesn't trip. */
// GLOBAL: TIE95 0xDBC94
int32_t lineaddressVGA[481];

/* Star double-buffer pair + the two underlying buffers. */
static int32_t starposbuf1[768];
static int32_t starposbuf2[768];
int32_t* newstarptr = starposbuf1;
int32_t* oldstarptr = starposbuf2;

int32_t shiftA1mul[16], shiftA2mul[16], shiftA3mul[16];
int32_t shiftB1mul[16], shiftB2mul[16], shiftB3mul[16];
int32_t shiftC1mul[16], shiftC2mul[16], shiftC3mul[16];

int32_t stareyex[128], stareyey[128], stareyez[128];

// GLOBAL: TIE98 0x4EB740
uint16_t stardetaillevel = 1;
// GLOBAL: TIE95 0xDE774
int16_t drawshapex;
// GLOBAL: TIE95 0xDE768
int16_t drawshapey;
int16_t tempdepth;
int16_t drawdepth;
int16_t tempwidth;
// GLOBAL: TIE95 0xDE770
int16_t drawwidth;
// GLOBAL: TIE95 0xDE785
uint8_t basecolor;
// GLOBAL: TIE95 0xDE786
int16_t skipcolorvga;

/* ------------------------------------------------------------------ */
/* Cross-module globals (forward decls for locals we consume)         */
/* ------------------------------------------------------------------ */

/* tie.c owns the 256 packed {grid index, palette delta} star pairs and
 * the shared base palette slot consumed below. */

/* xtrans2.c owns the 512-entry star hash table. */

/* tie.c-ish: color cycle + blank bitmask (demo watdbg puts them in rts/tie). */

/* ------------------------------------------------------------------ */
/* External platform stubs                                            */
/* ------------------------------------------------------------------ */

/* Forward flight palette updates to the shared classic framebuffer. */
// FUNCTION: TIE95 0x8FE4C
void XPAL_Set_VGA_Palette(uint16_t count, uint16_t start_idx, const uint8_t* rgb) {
	TieClassicFramebuffer_SetPalette(rgb, (int)start_idx, (int)count);
}

/* Host default for unsupported BIOS interrupts. */
__attribute__((weak)) int int386(int int_no, const void* inregs, void* outregs) {
	(void)int_no;
	(void)inregs;
	(void)outregs;
	return 0;
}

/* ------------------------------------------------------------------ */
/* rtsvga2_setcurrentpage (0x4E5D4)                                   */
/* ------------------------------------------------------------------ */

/* Update the emulated VESA page cache; redundant requests are skipped. */
// FUNCTION: TIE95 0x4E5D4
void rtsvga2_setcurrentpage(uint8_t window, uint16_t page) {
	uint32_t regs[7];
	uint32_t prev_page = (window == 1) ? lastpageB : lastpageA;

	if (window == 1)
		lastpageB = page;
	else
		lastpageA = page;

	if ((uint32_t)page == prev_page)
		return;

	/* VESA INT 10h AX=4F05h parameters retained for the weak platform hook. */
	regs[0] = 0x4F05;
	regs[1] = window;
	regs[2] = 0;
	regs[3] = (uint32_t)page * vesa_grains_per_page;
	regs[4] = regs[5] = regs[6] = 0;
	int386(0x10, regs, regs);
}

/* ------------------------------------------------------------------ */
/* rtsvga2_invalidatepagecache (0x4E5C0)                              */
/* ------------------------------------------------------------------ */

/* Reset the lastpageA/B cache so the next rtsvga2_setcurrentpage issues
 * the BIOS call even if the requested page matches the previously-cached
 * value. Called after pause/resume transitions that might have moved the
 * VESA window outside RTSVGA2's control. */
// FUNCTION: TIE95 0x4E5C0
void rtsvga2_invalidatepagecache(void) {
	lastpageA = 0xFFFFFFFFu;
	lastpageB = 0xFFFFFFFFu;
}

/* ------------------------------------------------------------------ */
/* rtsvga2_setvesascanlinelength (0x4E644)                            */
/* ------------------------------------------------------------------ */

/* VESA logical scanline-length call retained for the weak platform hook. */
// FUNCTION: TIE95 0x4E644
void rtsvga2_setvesascanlinelength(uint32_t width_px) {
	uint32_t regs[7];
	regs[0] = 0x4F06;
	regs[1] = 0;        /* BL=0 = Set in pixels */
	regs[2] = width_px; /* ECX = new scanline length */
	regs[3] = 0;        /* EDX slot unused in retail (junk stack) */
	regs[4] = regs[5] = regs[6] = 0;
	int386(0x10, regs, regs);
}

/* ------------------------------------------------------------------ */
/* rtsvga2_initgraphVGA (0x4B8E0)                                     */
/* ------------------------------------------------------------------ */

/* Initialise graphics state for flight:
 *   (1) reset star double-buffers to -1 (empty)
 *   (2) rebuild lineaddressVGA[y] = y * screenMemWidth for every scanline
 *   (3) retain the framebuffer pointer installed by setvgapointers
 *   (4) clear the framebuffer
 *   (5) bind the bracket definition (10-pt for VGA 13h, 12-pt for VESA 257)
 * Retail adds a fractional-page clear after the full-page loop (demo
 * dropped the remainder) -- handled via the final memset in the linear
 * fallback path.
 */
// FUNCTION: TIE95 0x4B8E0
// FUNCTION: TIE98 0x47A360
void rtsvga2_initgraphVGA(void) {
	int i;
	for (i = 0; i < 768; ++i) {
		starposbuf1[i] = -1;
		starposbuf2[i] = -1;
	}

	if (TieClassicDisplay_UsesDx5()) {
		int32_t y;

		for (y = 0; y < screenYRes; ++y)
			lineaddressVGA[y] = (int32_t)g_surfacePitch * y;
		memset(vgapointer, 0, (size_t)screenYRes * g_surfacePitch);
		if (tie_is_high_resolution_flight()) {
			bracketdef_ptr = bracketdef_12pt;
			bracketdef_count = 12;
		} else {
			bracketdef_ptr = bracketdef_10pt;
			bracketdef_count = 10;
		}
		return;
	}

	{
		int32_t mem_width = screenMemWidth;
		int32_t y;
		for (y = 0; y < screenYRes; ++y)
			lineaddressVGA[y] = mem_width * y;
	}

	/* Retail binds vgapointer = 0xA0000 (and xtrans2_videobaseptr to the
	 * same CRT scanout memory) here. Mirror that by aliasing both to the
	 * Landru framebuffer, which is what the SDL presenter actually shows.
	 * bpflight rebinds these to a private bitmap for its own 3D viewer
	 * and restores NULL on exit; main-flight never touched them before
	 * this line, so they stayed NULL and every rtsvga2/xtrans2 write into
	 * the HUD, stars, radar, reticle, or 3D rasterizer segfaulted. */
	vgapointer = vesa_buff_gbl;
	xtrans2_videobaseptr = vesa_buff_gbl;

	if (vgapointer)
		memset(vgapointer, 0, (size_t)screenYRes * (size_t)screenMemWidth);

	if (tie_is_high_resolution_flight()) {
		bracketdef_ptr = bracketdef_12pt;
		bracketdef_count = 12;
	} else {
		bracketdef_ptr = bracketdef_10pt;
		bracketdef_count = 10;
	}
}

/* ------------------------------------------------------------------ */
/* rtsvga2_setvgapointers (0x4BA64)                                   */
/* ------------------------------------------------------------------ */

/* Rebind vgapointer and rebuild lineaddressVGA. If vga_ptr is NULL,
 * the retail code defaults to the real-mode VGA segment (0xA0000); we
 * mirror that by snapping vgapointer back to vesa_buff_gbl so writes
 * resume hitting the on-screen framebuffer. Maproom (and other off-
 * screen renderers) rely on this restore: they call
 * setvgapointers(buf, w, h) to redirect into a private buffer, then
 * setvgapointers(NULL, ...) to undo it. Without the restore, drawshape
 * after the off-screen pass keeps writing into the private buffer and
 * never reaches the screen. */
// FUNCTION: TIE95 0x4BA64
void rtsvga2_setvgapointers(void* vga_ptr, uint16_t mem_width, uint16_t num_lines) {
	if (vga_ptr) {
		int y;

		vgapointer = (uint8_t*)vga_ptr;
		for (y = 0; y < num_lines; ++y)
			lineaddressVGA[y] = (int32_t)mem_width * y;
	} else {
		int y;

		vgapointer = vesa_buff_gbl;
		for (y = 0; y < screenYRes; ++y)
			lineaddressVGA[y] = screenMemWidth * y;
	}
}

/* ------------------------------------------------------------------ */
/* rtsvga2_applyBrightness (0x4BAF0)                                  */
/* ------------------------------------------------------------------ */

/* Retail-only. For each of `count` RGB triplets (stride 3) starting at
 * start_idx, copies rgb_src into rgb_dst with the Value channel of HSV
 * scaled by (brightness_setting / 256). brightness_setting == 256 takes
 * a straight memcpy fast path. */
// FUNCTION: TIE95 0x4BAF0
void rtsvga2_applyBrightness(const uint8_t* rgb_src, uint8_t* rgb_dst, uint16_t start_idx, uint16_t count) {
	const uint8_t* s = rgb_src + 3 * start_idx;
	uint8_t* d = rgb_dst + 3 * start_idx;

	uint16_t i;

	if (brightness_setting == 256) {
		memcpy(d, s, (size_t)count * 3);
		return;
	}

	for (i = 0; i < count; ++i, s += 3, d += 3) {
		uint8_t r = s[0], g = s[1], b = s[2];

		/* v_max = max(r, g, b) */
		uint8_t v_max;
		uint8_t v_min;
		uint8_t hsv_s;
		uint8_t hue_frac;
		uint8_t hue_sext;
		uint32_t v_scaled32;
		uint8_t v_scaled;

		uint8_t out_r, out_g, out_b;

		if (r < g || r < b)
			v_max = (g < r || g < b) ? b : g;
		else
			v_max = r;

		/* v_min = min(r, g, b) */

		if (r > g || r > b)
			v_min = (g > r || g > b) ? b : g;
		else
			v_min = r;

		hsv_s = v_max ? (uint8_t)(63u * (v_max - v_min) / v_max) : 0;
		hue_frac = 0;
		hue_sext = 0;

		if (hsv_s) {
			if (r == v_max) {
				if (g < b) {
					hue_frac = (uint8_t)(63u - 63u * (b - g) / (r - v_min));
					hue_sext = 5;
				} else {
					hue_frac = (uint8_t)(63u * (g - b) / (r - v_min));
					hue_sext = 0;
				}
			} else if (g == v_max) {
				if (b < r) {
					hue_frac = (uint8_t)(63u - 63u * (r - b) / (g - v_min));
					hue_sext = 1;
				} else {
					hue_frac = (uint8_t)(63u * (b - r) / (g - v_min));
					hue_sext = 2;
				}
			} else if (r < g) {
				hue_frac = (uint8_t)(63u - 63u * (g - r) / (v_max - v_min));
				hue_sext = 3;
			} else {
				hue_frac = (uint8_t)(63u * (r - g) / (v_max - v_min));
				hue_sext = 4;
			}
		}

		v_scaled32 = (brightness_setting * v_max) >> 8;
		v_scaled = (v_scaled32 > 0x3F) ? 63 : (uint8_t)v_scaled32;

		if (hsv_s) {
			/* Intermediate: fall-off term for the "mid" channel. */
			uint32_t mid_hi = (63u - (uint32_t)hsv_s * (63u - hue_frac) / 63u) * v_scaled / 63u;
			uint32_t mid_lo = v_scaled * (63u - hsv_s) / 63u;
			uint32_t mid_sub = (63u - (uint32_t)hue_frac * hsv_s / 63u) * v_scaled / 63u;
			switch (hue_sext) {
				case 0:
					out_r = v_scaled;
					out_g = (uint8_t)mid_hi;
					out_b = (uint8_t)mid_lo;
					break;
				case 1:
					out_r = (uint8_t)mid_sub;
					out_g = v_scaled;
					out_b = (uint8_t)mid_lo;
					break;
				case 2:
					out_r = (uint8_t)mid_lo;
					out_g = v_scaled;
					out_b = (uint8_t)mid_hi;
					break;
				case 3:
					out_r = (uint8_t)mid_lo;
					out_g = (uint8_t)mid_sub;
					out_b = v_scaled;
					break;
				case 4:
					out_r = (uint8_t)mid_hi;
					out_g = (uint8_t)mid_lo;
					out_b = v_scaled;
					break;
				case 5:
					out_r = v_scaled;
					out_g = (uint8_t)mid_lo;
					out_b = (uint8_t)mid_sub;
					break;
				default:
					out_r = out_g = out_b = v_scaled;
					break;
			}
		} else {
			out_r = out_g = out_b = v_scaled;
		}

		d[0] = out_r;
		d[1] = out_g;
		d[2] = out_b;
	}
}

/* ------------------------------------------------------------------ */
/* rtsvga2_blankVGA (0x4BEE8)                                         */
/* ------------------------------------------------------------------ */

/* Fade-to-black: push an all-zero 768-byte palette to the DAC.
 * colorcycleflag cleared first so any palette animation stops.
 * blankcondition bit 0 marks the fade. */
// FUNCTION: TIE95 0x4BEE8
// FUNCTION: TIE98 0x47AA30
void rtsvga2_blankVGA(void) {
	uint8_t zeroes[768];
	memset(zeroes, 0, sizeof zeroes);
	colorcycleflag = 0;
	XPAL_Set_VGA_Palette(256, 0, zeroes);
	blankcondition |= 1u;
}

/* ------------------------------------------------------------------ */
/* rtsvga2_unblankVGA (0x4BF3C)                                       */
/* ------------------------------------------------------------------ */

/* Unblank: push the in-memory vgapalette to the DAC, run through the
 * retail brightness scaler, then restart color cycling. */
// FUNCTION: TIE95 0x4BF3C
// FUNCTION: TIE98 0x47AA80
void rtsvga2_unblankVGA(void) {
	uint8_t scaled[768];
	colorcycleflag = 0;
	rtsvga2_applyBrightness(rtsvga2_vgapalette, scaled, 0, 256);
	XPAL_Set_VGA_Palette(256, 0, scaled);
	blankcondition &= ~1u;
	colorcycleflag = 1;
}

// FUNCTION: TIE98 0x47A500
void rtsvga2_clearflightdisplay(void) {
	rtsvga2_blankVGA();
	blankcondition = 0;
	xvesa_Erase_Video(16);
}

/* ------------------------------------------------------------------ */
/* rtsvga2_buildpaletteVGA (0x4BF88)                                  */
/* ------------------------------------------------------------------ */

/* Copy `count` RGB triplets (R, G, B; 6-bit DAC range) from rgb_src
 * into rtsvga2_vgapalette[start_idx..start_idx+count]. In-memory only
 * -- the caller pushes to hardware via XPAL/unblank. */
// FUNCTION: TIE95 0x4BF88
void rtsvga2_buildpaletteVGA(const uint8_t* rgb_src, uint16_t start_idx, uint16_t count) {
	uint16_t i;

	for (i = start_idx; i < start_idx + count; ++i, rgb_src += 3) {
		uint32_t off = 3u * i;
		rtsvga2_vgapalette[off] = rgb_src[0];
		rtsvga2_vgapalette[off + 1] = rgb_src[1];
		rtsvga2_vgapalette[off + 2] = rgb_src[2];
	}

	/* Publish immediately because the first flight frame may precede unblankVGA. */
	TieClassicFramebuffer_SetPalette(&rtsvga2_vgapalette[3u * start_idx], (int)start_idx, (int)count);
}

/* ------------------------------------------------------------------ */
/* rtsvga2_savepaletteVGA (0x4BFD0)                                   */
/* ------------------------------------------------------------------ */

/* Snapshot the full in-memory palette (256 triplets = 768 bytes) into
 * rgb_dst. */
// FUNCTION: TIE95 0x4BFD0
void rtsvga2_savepaletteVGA(uint8_t* rgb_dst) { memcpy(rgb_dst, rtsvga2_vgapalette, 768); }

/* ------------------------------------------------------------------ */
/* rtsvga2_restorepaletteVGA (0x4C00C)                                */
/* ------------------------------------------------------------------ */

/* Replace the full in-memory palette with `rgb_src`. */
// FUNCTION: TIE95 0x4C00C
void rtsvga2_restorepaletteVGA(const uint8_t* rgb_src) { rtsvga2_buildpaletteVGA(rgb_src, 0, 256); }

/* ------------------------------------------------------------------ */
/* rtsvga2_findNearestColor (0x4C168)                                 */
/* ------------------------------------------------------------------ */

/* Retail-only. Find index in palette[start_idx..end_idx) whose RGB
 * minimizes squared Euclidean distance to rgb_target (each component
 * 6-bit 0..63). Returns start_idx if the range is empty. */
// FUNCTION: TIE95 0x4C168
uint32_t rtsvga2_findNearestColor(const uint8_t* rgb_target, const uint8_t* palette, uint32_t start_idx,
								  uint32_t end_idx) {
	int best_dist_sq = 0x7FFFFFFF;
	uint32_t best_idx = start_idx;

	int tgt_r = rgb_target[0];
	int tgt_g = rgb_target[1];
	int tgt_b = rgb_target[2];

	const uint8_t* p = palette + 3 * start_idx;
	uint32_t i;

	for (i = start_idx; i < end_idx; ++i, p += 3) {
		int dr = tgt_r - p[0], dg = tgt_g - p[1], db = tgt_b - p[2];
		int dist_sq = dr * dr + dg * dg + db * db;
		if (dist_sq < best_dist_sq) {
			best_dist_sq = dist_sq;
			best_idx = i;
		}
	}
	return best_idx;
}

/* ------------------------------------------------------------------ */
/* rtsvga2_remapRGBImage (0x4C020)                                    */
/* ------------------------------------------------------------------ */

/* Retail-only. Quantise a 24-bit RGB sprite (layout described below)
 * to 8bpp using the current vgapalette[0x40..0x100]. Called from
 * FEDISKIO_loadspecies.
 *
 * Input `image_header` is an array of DWORDs:
 *   [0]  body_off       -> output pixel array (and primary RGB output)
 *   [3]  data_off       -> primary RGB triplets (stride 4 bytes: R, G, B, _pad)
 *   [4]  subhdr_off_tbl -> array of subheader offsets
 *   [5]  output_off     (written := body_off)
 *   [6]  num_subhdrs
 *   [9]  (in each sub) subheader type (24 = RGB present)
 *   [10] (in each sub) subheader RGB count
 *   [11] main_type      (24 = primary RGB present)
 *   [12] main_count     (primary RGB count)
 *
 * For each RGB triplet, compacts to 6-bit (>>2) and picks nearest via
 * rtsvga2_findNearestColor over rtsvga2_vgapalette[0x40..0x100]. */
// FUNCTION: TIE95 0x4C020
void rtsvga2_remapRGBImage(uint32_t* image_header) {
	uint8_t* out_px = (uint8_t*)image_header + image_header[0];
	uint8_t rgb_target[3];

	uint32_t h;

	if (image_header[11] == 24) {
		const uint8_t* rgb_src = (const uint8_t*)image_header + image_header[3];
		uint32_t i;

		image_header[5] = image_header[0];
		for (i = 0; i < image_header[12]; ++i, rgb_src += 4, ++out_px) {
			rgb_target[0] = (uint8_t)((int)rgb_src[0] >> 2);
			rgb_target[1] = (uint8_t)((int)rgb_src[1] >> 2);
			rgb_target[2] = (uint8_t)((int)rgb_src[2] >> 2);
			*out_px = (uint8_t)rtsvga2_findNearestColor(rgb_target, rtsvga2_vgapalette, 0x40, 0x100);
		}
	}

	for (h = 0; h < image_header[6]; ++h) {
		uint32_t* sub = (uint32_t*)((uint8_t*)image_header +
									*((uint32_t*)((uint8_t*)image_header + image_header[4]) + h));
		sub[3] = (uint32_t)(out_px - (uint8_t*)sub);
		if (sub[9] == 24) {
			const uint8_t* rgb_src = (const uint8_t*)sub + sub[1];
			uint32_t i;

			for (i = 0; i < sub[10]; ++i, rgb_src += 4, ++out_px) {
				rgb_target[0] = (uint8_t)((int)rgb_src[0] >> 2);
				rgb_target[1] = (uint8_t)((int)rgb_src[1] >> 2);
				rgb_target[2] = (uint8_t)((int)rgb_src[2] >> 2);
				*out_px = (uint8_t)rtsvga2_findNearestColor(rgb_target, rtsvga2_vgapalette, 0x40, 0x100);
			}
		}
	}
}

// FUNCTION: TIE98 0x478B40
void rtsvga2_applyBrightness16_tie98(const uint8_t* rgb6, uint16_t* output, uint32_t start_idx,
									 uint32_t count) {
	uint8_t adjusted[3 * 1024];

	uint32_t i;

	rtsvga2_applyBrightness(rgb6, adjusted, (uint16_t)start_idx, (uint16_t)count);
	for (i = start_idx; i < start_idx + count; ++i) {
		const uint8_t* color = &adjusted[3 * i];
		if (FrontendDisplay_GetPixelFormat555()) {
			output[i] = (uint16_t)(((uint16_t)(color[0] >> 1) << 10) | ((uint16_t)(color[1] >> 1) << 5) |
								   (color[2] >> 1));
		} else {
			output[i] =
				(uint16_t)(((uint16_t)(color[0] >> 1) << 11) | ((uint16_t)color[1] << 5) | (color[2] >> 1));
		}
	}
}

// FUNCTION: TIE98 0x47AAE0
void rtsvga2_buildpaletteVGA_tie98(const uint8_t* rgb_src, uint16_t start_idx, uint16_t count) {
	uint16_t palette_index;
	for (palette_index = start_idx; palette_index < start_idx + count; ++palette_index) {
		rtsvga2_vgapalette[3 * palette_index] = *rgb_src++;
		rtsvga2_vgapalette[3 * palette_index + 1] = *rgb_src++;
		rtsvga2_vgapalette[3 * palette_index + 2] = *rgb_src++;
	}
	if (g_flight16bppBytesPerPixel == 2) {
		rtsvga2_applyBrightness16_tie98(rtsvga2_vgapalette, g_flightTextPalette, start_idx, count);
		if (start_idx == 0 && count == 64) {
			const uint16_t transparent_color = g_flightTextPalette[deepspacecolor];
			uint16_t index;

			for (index = 0; index < 64; ++index) {
				if (g_flightTextPalette[index] == transparent_color)
					g_flightTextPalette[index] = 0;
			}
		}
	}
}

// FUNCTION: TIE98 0x47AB90
void rtsvga2_savepaletteVGA_tie98(uint8_t* rgb_dst) {
	memcpy(rgb_dst, rtsvga2_vgapalette, sizeof rtsvga2_vgapalette);
}

// FUNCTION: TIE98 0x47ABC0
void rtsvga2_restorepaletteVGA_tie98(const uint8_t* rgb_src) {
	rtsvga2_buildpaletteVGA_tie98(rgb_src, 0, 256);
}

// FUNCTION: TIE98 0x47ABE0
void rtsvga2_remapRGBImage_tie98(uint32_t* image_header) {
	uint16_t* output = (uint16_t*)((uint8_t*)image_header + image_header[0]);
	uint8_t rgb6[3 * 1024];

	uint32_t h;

	if (image_header[11] == 24) {
		const uint32_t count = image_header[12];
		const uint8_t* source = (const uint8_t*)image_header + image_header[3];
		image_header[5] = image_header[0];
		if (count < 1024) {
			uint32_t i;

			for (i = 0; i < count; ++i, source += 4) {
				rgb6[3 * i] = source[0] >> 2;
				rgb6[3 * i + 1] = source[1] >> 2;
				rgb6[3 * i + 2] = source[2] >> 2;
			}
			rtsvga2_applyBrightness16_tie98(rgb6, output, 0, count);
			output += count;
		}
	}

	for (h = 0; h < image_header[6]; ++h) {
		uint32_t* sub = (uint32_t*)((uint8_t*)image_header +
									*((uint32_t*)((uint8_t*)image_header + image_header[4]) + h));
		sub[3] = (uint32_t)((uint8_t*)output - (uint8_t*)sub);
		if (sub[9] == 24) {
			const uint32_t count = sub[10];
			const uint8_t* source = (const uint8_t*)sub + sub[1];
			if (count < 1024) {
				uint32_t i;

				for (i = 0; i < count; ++i, source += 4) {
					rgb6[3 * i] = source[0] >> 2;
					rgb6[3 * i + 1] = source[1] >> 2;
					rgb6[3 * i + 2] = source[2] >> 2;
				}
				rtsvga2_applyBrightness16_tie98(rgb6, output, 0, count);
				output += count;
			}
		}
	}
}

/* ------------------------------------------------------------------ */
/* rtsvga2_calcpositionVGA (0x4C208)                                  */
/* ------------------------------------------------------------------ */

/* Compute framebuffer byte offset for (x, y) = y * screenMemWidth + x. */
// FUNCTION: TIE95 0x4C208
uint32_t rtsvga2_calcpositionVGA(uint16_t x, uint16_t y) { return (uint32_t)screenMemWidth * y + x; }

// FUNCTION: TIE98 0x47AF20
// RTSVGA2_calcpositionVGA
uint32_t rtsvga2_calcpositionVGA_tie98(uint16_t x, uint16_t y) {
	return g_flight16bppBytesPerPixel * x + g_surfacePitch * y;
}

/* ------------------------------------------------------------------ */
/* rtsvga2__lowdrawshapeVGA (0x4C280)                                 */
/* ------------------------------------------------------------------ */

/*
 * Low-level VGA RLE shape blitter. Shape opcodes (byte `op`):
 *   0x00..0xFA: packed pixel run. op[7:2] = color delta (added to basecolor
 *               unless mono), op[1:0] = count-1 (1..4 pixels). color ==
 *               skip_color is treated as transparent (advances cursor only).
 *   0xFB:       next byte -> new basecolor (skipped in mono mode so mono
 *               glyphs don't mutate the running base).
 *   0xFC:       double-row fill for 2-pixel-tall textures. Next bytes
 *               (color, count-1) -> outputs count+1 pairs (color, color+1).
 *               Skipped (treated as end-of-line) in mono mode.
 *   0xFD:       explicit pixel run: next bytes are (count-1, color) raw.
 *               Joins the pixel-run code at LABEL_10 in the retail asm.
 *   0xFE/other: end-of-line, ++drawshapey, rewind to (drawshapex, y+1).
 *   0xFF:       end-of-shape.
 *
 * flip_x != 0 renders right-to-left. mono_flag = 1 forces every painted
 * pixel to basecolor. Output is the linear framebuffer at vgapointer. */
// FUNCTION: TIE95 0x4C280
void rtsvga2__lowdrawshapeVGA(const uint8_t* shape, uint16_t x, uint16_t y, uint16_t skip_color, int flip_x,
							  char mono_flag) {
	/* Anything this blitter paints lands in vesa_buff_gbl via vgapointer.
	 * Flag the frame dirty so the application's end-of-tick pull picks up
	 * the update — callers that draw then yield (goals screen,
	 * debrief prompts, ...) rely on this to get their content onto
	 * the window before the next input poll. */
	vesa_dirty_gbl = true;

	drawshapex = (int16_t)x;
	drawshapey = (int16_t)y;
	skipcolorvga = (int16_t)(skip_color & 0xFF);

	for (;;) {
		uint32_t off = lineaddressVGA[(uint16_t)drawshapey] + (uint16_t)drawshapex;
		uint8_t* dst = vgapointer + off;

		int opcode;
		uint8_t op;
		uint8_t color;
		int16_t run_raw;

	read_ops_same_row:
		for (;;) {
			/* Pixel-run path. */
			for (;;) {
				op = *shape;
				opcode = *shape++;
				if (opcode >= 251)
					break;

				color = (uint8_t)(op >> 2);
				if (!mono_flag)
					color += basecolor;
				run_raw = (int16_t)(opcode & 3);

			label_10: {
				uint16_t run_len = (uint16_t)(run_raw + 1);
				if (color == (uint8_t)skip_color) {
					if (flip_x)
						dst -= run_len;
					else
						dst += run_len;
				} else {
					if (mono_flag)
						color = basecolor;
					if (flip_x) {
						int cnt = 0;
						while ((uint16_t)cnt < run_len) {
							dst[1] = color;
							--dst;
							++cnt;
						}
					} else {
						int cnt = 0;
						if (run_len) {
							do {
								*dst++ = color;
								++cnt;
							} while ((uint16_t)cnt < run_len);
						}
					}
				}
			}
			}

			if (opcode > 251)
				break; /* 0xFC..0xFF drop out */

			/* opcode == 0xFB: set basecolor (or skip byte in mono mode) */
			if (mono_flag)
				++shape;
			else
				basecolor = *shape++;
		}

		/* opcode is now in {0xFC, 0xFD, 0xFE, 0xFF}. */
		if (opcode == 252 && !mono_flag) {
			/* 0xFC: alternating (color, color+1) pair run on same scanline.
			 * Retail jmp target is 4C2FC (read next opcode without
			 * recomputing dst) -- we jump back to read_ops_same_row so
			 * subsequent opcodes continue from the current dst. Using
			 * `continue` here would rewind dst to drawshapex and produce
			 * horizontal smearing where later runs overwrite earlier ones. */
			uint8_t dbl_color = *shape;
			const uint8_t* after_col = shape + 1;
			int dbl_cnt = (int)(uint8_t)*after_col;
			int dbl_rem;

			shape = after_col + 1;

			dbl_rem = dbl_cnt + 1;
			while ((int16_t)dbl_rem > 0) {
				*dst = dbl_color;
				if (flip_x)
					--dst;
				else
					++dst;
				--dbl_rem;
				if ((int16_t)dbl_rem > 0) {
					*dst = (uint8_t)(dbl_color + 1);
					if (flip_x)
						--dst;
					else
						++dst;
					--dbl_rem;
				}
			}
			goto read_ops_same_row;
		}

		if (op == 253) {
			/* 0xFD: explicit (count, color) run -- rejoin pixel-run code. */
			run_raw = (int16_t)shape[0];
			color = shape[1];
			shape += 2;
			goto label_10;
		}

		if (op != 255) {
			/* 0xFE or others: end-of-line */
			++drawshapey;
			continue;
		}

		/* 0xFF: end-of-shape */
		return;
	}
}

/* ------------------------------------------------------------------ */
/* rtsvga2_drawshapeVGA (0x4C224)                                     */
/* ------------------------------------------------------------------ */

// FUNCTION: TIE95 0x4C224
void rtsvga2_drawshapeVGA(const uint8_t* shape, int16_t x, int16_t y, int16_t skip_color, uint16_t flip_x) {
	basecolor = 0;
	rtsvga2__lowdrawshapeVGA(shape, (uint16_t)x, (uint16_t)y, (uint16_t)skip_color, (int)flip_x, 0);
}

// FUNCTION: TIE98 0x479180
static void rtsvga2__lowdrawshapeVGA_tie98(const uint8_t* shape, uint16_t x, uint16_t y, uint16_t skip_color,
										   int flip_x, int mono) {
	drawshapex = (int16_t)x;
	drawshapey = (int16_t)y;
	skipcolorvga = (int16_t)(skip_color & 0xFF);

	for (;;) {
		uint16_t* destination =
			(uint16_t*)(vgapointer + lineaddressVGA[(uint16_t)drawshapey] + 2 * (uint16_t)drawshapex);
		for (;;) {
			uint8_t opcode = *shape++;
			uint8_t color_index;
			uint16_t run_length;

			uint16_t color;
			uint16_t pixel;

			if (opcode < 0xFB) {
				color_index = opcode >> 2;
				if (!mono)
					color_index += basecolor;
				run_length = (opcode & 3) + 1;
			} else if (opcode == 0xFB) {
				if (mono)
					++shape;
				else
					basecolor = *shape++;
				continue;
			} else if (opcode == 0xFC && !mono) {
				const uint8_t first_color = *shape++;
				int run_remaining = *shape++ + 1;
				while (run_remaining > 0) {
					*destination = g_flightTextPalette[first_color];
					destination += flip_x ? -1 : 1;
					if (--run_remaining == 0)
						break;
					*destination = g_flightTextPalette[(uint8_t)(first_color + 1)];
					destination += flip_x ? -1 : 1;
					--run_remaining;
				}
				continue;
			} else if (opcode == 0xFD) {
				run_length = (uint16_t)(*shape++ + 1);
				color_index = *shape++;
			} else if (opcode == 0xFF) {
				return;
			} else {
				break;
			}

			if (color_index == (uint8_t)skip_color) {
				destination += flip_x ? -(int)run_length : (int)run_length;
				continue;
			}
			if (mono)
				color_index = basecolor;
			color = g_flightTextPalette[color_index];
			for (pixel = 0; pixel < run_length; ++pixel) {
				*destination = color;
				destination += flip_x ? -1 : 1;
			}
		}
		++drawshapey;
	}
}

// FUNCTION: TIE98 0x479120
void rtsvga2_drawshapeVGA_tie98(const uint8_t* shape, int16_t x, int16_t y, int16_t skip_color,
								uint16_t flip_x) {
	basecolor = 0;
	rtsvga2__lowdrawshapeVGA_tie98(shape, (uint16_t)x, (uint16_t)y, (uint16_t)skip_color, flip_x, 0);
}

// FUNCTION: TIE98 0x479150
static void rtsvga2_drawmonoshapeVGA_tie98(const uint8_t* shape, uint16_t x, uint16_t y, uint16_t skip_color,
										   uint8_t color) {
	basecolor = color;
	rtsvga2__lowdrawshapeVGA_tie98(shape, x, y, skip_color, 0, 1);
}

/* ------------------------------------------------------------------ */
/* rtsvga2_drawmonoshapeVGA (0x4C254)                                 */
/* ------------------------------------------------------------------ */

/* Mono (single-colour stencil) variant: sets basecolor = color and calls
 * _lowdrawshapeVGA with mono_flag = 1 so every painted pixel is `color`. */
// FUNCTION: TIE95 0x4C254
// FUNCTION: TIE98 0x47AF80
void rtsvga2_drawmonoshapeVGA(const uint8_t* shape, uint16_t x, uint16_t y, uint16_t skip_color,
							  uint8_t color) {
	if (TieProfile_UsesTie98Logic() && g_flight16bppBytesPerPixel == 2) {
		rtsvga2_drawmonoshapeVGA_tie98(shape, x, y, skip_color, color);
		return;
	}
	basecolor = color;
	rtsvga2__lowdrawshapeVGA(shape, x, y, skip_color, 0, 1);
}

/* ------------------------------------------------------------------ */
/* rtsvga2_drawdotVGA (0x4C440)                                       */
/* ------------------------------------------------------------------ */

/* Plot a single 8bpp pixel at (x, y) with `color`. */
// FUNCTION: TIE95 0x4C440
void rtsvga2_drawdotVGA(uint16_t x, uint16_t y, uint8_t color) {
	uint32_t off = lineaddressVGA[y] + x;
	if (vgapointer)
		vgapointer[off] = color;
}

/* ------------------------------------------------------------------ */
/* rtsvga2_outcharVGA (0x4C4B0)                                       */
/* ------------------------------------------------------------------ */

/*
 * Render a character at (cursorx, cursory) using the 8bpp glyph stored
 * at curfontptr + (ch - 0x20) * fontcharsize. Glyph header is
 *   [0]: glyph_width (pixels)
 *   [1]: glyph_height (scanlines)
 * followed by 2 * glyph_height bytes -- pairs (main_row_bits,
 * drop_shadow_row_bits). Bits are scanned MSB->LSB; a set main bit emits
 * textcolor; if dropflag is on, the previous row's shadow bit paints
 * dropcolor; otherwise backcolor.
 *
 * Handles '\n' (newline), lwrapflag auto-wrap, autofill-before-wrap,
 * uppercase fold when fontflag && !fontlowercase. Clips to
 * [left,right) x [top,bottom) margin rectangle. */
// FUNCTION: TIE95 0x4C4B0
void rtsvga2_outcharVGA(uint8_t ch) {
	/* Every visible glyph lands in vesa_buff_gbl; see the note on
	 * rtsvga2__lowdrawshapeVGA above. */
	const uint8_t* glyph;
	uint8_t glyph_width;
	uint8_t glyph_height;
	const uint8_t* row_ptr;
	int32_t newx;
	uint8_t prev_row;
	int16_t y;
	int i;

	vesa_dirty_gbl = true;

	if (ch == '\n') {
		if (autofillflag)
			rtsvga2_autofillVGA();
		cursorx = leftmargin;
		cursory += (int16_t)fontheight;
		return;
	}
	if ((int8_t)ch < 32)
		return;

	if (fontflag && !fontlowercase && ch >= 'a' && ch <= 'z')
		ch -= 32;

	glyph = (const uint8_t*)curfontptr + (size_t)(uint8_t)(ch - 32) * (uint16_t)fontcharsize;
	glyph_width = glyph[0];
	glyph_height = glyph[1];
	row_ptr = glyph + 2;

	newx = (int32_t)cursorx + glyph_width;
	if (newx >= rightmargin && lwrapflag) {
		if (autofillflag)
			rtsvga2_autofillVGA();
		cursorx = leftmargin;
		cursory += (int16_t)glyph_height;
	}

	prev_row = 0;
	y = cursory;
	for (i = 0; i < glyph_height; ++i, ++y) {
		int row_width;
		int16_t row_x;
		uint8_t row_bits;

		if (y >= bottommargin)
			break;

		row_width = glyph_width;
		row_x = cursorx;
		row_bits = row_ptr[0];

		if (dropflag)
			++row_width;

		if (cursorx < leftmargin) {
			int shift = leftmargin - cursorx;
			if (shift >= row_width)
				goto next_row;
			row_width -= shift;
			row_x = leftmargin;
			row_bits <<= shift;
		}

		if (y >= topmargin) {
			uint8_t* dst;
			int px;

			if (row_x + row_width > rightmargin) {
				row_width = rightmargin - row_x;
				if (row_width <= 0)
					break;
			}
			dst = vgapointer + lineaddressVGA[y] + row_x;
			for (px = 0; px < row_width; ++px) {
				if (row_bits & 0x80) {
					*dst++ = textcolor;
				} else if (dropflag && (int8_t)prev_row < 0) {
					*dst++ = dropcolor;
				} else {
					*dst++ = backcolor;
				}
				prev_row <<= 1;
				row_bits <<= 1;
			}
		}
	next_row:
		prev_row = (uint8_t)(row_ptr[0] >> 1);
		row_ptr += 2;
	}

	cursorx += (int16_t)glyph_width;
	if (cursorx >= rightmargin && lwrapflag) {
		if (autofillflag)
			rtsvga2_autofillVGA();
		cursorx = leftmargin;
		cursory += (int16_t)glyph_height;
	}
}

/* ------------------------------------------------------------------ */
/* rtsvga2_outchar32VGA (0x4C994)                                     */
/* ------------------------------------------------------------------ */

/* 32-bit-wide variant of outcharVGA. Each glyph row is 4 bytes (up to
 * 32 pixels wide). row_ptr advances by 2 ints (main row + drop row,
 * 8 bytes total). */
// FUNCTION: TIE95 0x4C994
void rtsvga2_outchar32VGA(uint8_t ch) {
	/* 32-bit glyph path -- same dirty invariant as outcharVGA. */
	const uint8_t* glyph;
	uint8_t glyph_width;
	uint8_t glyph_height;
	const uint32_t* row_ptr;
	int32_t newx;
	uint8_t saved_dropflag;
	uint32_t prev_row;
	int16_t y;
	int i;

	vesa_dirty_gbl = true;

	if (ch == '\n') {
		if (autofillflag)
			rtsvga2_autofillVGA();
		cursorx = leftmargin;
		cursory += (int16_t)fontheight;
		return;
	}
	if ((int8_t)ch < 32)
		return;

	if (fontflag && !fontlowercase && ch >= 'a' && ch <= 'z')
		ch -= 32;

	glyph = (const uint8_t*)curfontptr + (size_t)(uint8_t)(ch - 32) * (uint16_t)fontcharsize;
	glyph_width = glyph[0];
	glyph_height = glyph[1];
	row_ptr = (const uint32_t*)(glyph + 2);

	newx = (int32_t)cursorx + glyph_width;
	if (newx >= rightmargin && lwrapflag) {
		if (autofillflag)
			rtsvga2_autofillVGA();
		cursorx = leftmargin;
		cursory += (int16_t)glyph_height;
	}

	saved_dropflag = dropflag;
	prev_row = 0;
	y = cursory;

	for (i = 0; i < glyph_height; ++i, ++y) {
		int row_width;
		int16_t row_x;
		uint32_t row_bits;

		if (y >= bottommargin)
			break;

		row_width = glyph_width;
		row_x = cursorx;
		row_bits = row_ptr[0];

		if (saved_dropflag)
			++row_width;

		if (cursorx < leftmargin) {
			int shift = leftmargin - cursorx;
			if (shift >= row_width)
				goto next_row32;
			row_width -= shift;
			row_x = leftmargin;
			row_bits <<= shift;
		}

		if (y >= topmargin) {
			uint8_t* dst;
			int px;

			if (row_x + row_width > rightmargin) {
				row_width = rightmargin - row_x;
				if (row_width <= 0)
					break;
			}
			dst = vgapointer + lineaddressVGA[y] + row_x;
			for (px = 0; px < row_width; ++px) {
				if (row_bits & 0x80000000u) {
					*dst++ = textcolor;
				} else if (saved_dropflag && (prev_row & 0x80000000u)) {
					*dst++ = dropcolor;
				} else {
					*dst++ = backcolor;
				}
				row_bits <<= 1;
				prev_row <<= 1;
			}
		}
	next_row32:
		prev_row = row_ptr[0] / 2;
		row_ptr += 2;
	}

	cursorx += (int16_t)glyph_width;
	if (cursorx >= rightmargin && lwrapflag) {
		if (autofillflag)
			rtsvga2_autofillVGA();
		cursorx = leftmargin;
		cursory += (int16_t)glyph_height;
	}
	dropflag = saved_dropflag;
}

// FUNCTION: TIE98 0x479710
void rtsvga2_outchar32VGA_tie98(uint8_t ch) {
	const uint8_t* glyph;
	uint8_t glyph_width;
	uint8_t glyph_height;
	const uint8_t* row_data;
	uint32_t shadow_bits;
	int16_t y;

	if (ch == '\n') {
		if (autofillflag)
			rtsvga2_autofillVGA_tie98();
		cursory += (int16_t)fontheight;
		cursorx = leftmargin;
		return;
	}
	if (ch < 32)
		return;
	if (fontflag && !fontlowercase && ch >= 'a' && ch <= 'z')
		ch -= 32;

	glyph = (const uint8_t*)curfontptr + (size_t)(uint8_t)(ch - 32) * (uint16_t)fontcharsize;
	glyph_width = glyph[0];
	glyph_height = glyph[1];
	row_data = glyph + 2;
	if ((int32_t)cursorx + glyph_width >= rightmargin && lwrapflag) {
		if (autofillflag)
			rtsvga2_autofillVGA_tie98();
		cursorx = leftmargin;
		cursory += glyph_height;
	}

	shadow_bits = 0;
	for (y = cursory; y < cursory + glyph_height; ++y, row_data += 8) {
		uint32_t row_bits;
		/* PORT: glyph rows are serialized at two-byte alignment. */
		int width;
		int16_t x;

		memcpy(&row_bits, row_data, sizeof row_bits);
		width = glyph_width + (dropflag != 0);
		x = cursorx;
		if (cursorx < leftmargin) {
			const int clipped = leftmargin - cursorx;
			if (clipped >= width) {
				width = 0;
			} else {
				width -= clipped;
				x = leftmargin;
				row_bits <<= clipped;
			}
		}
		if (y >= bottommargin)
			break;
		if (y >= topmargin && width > 0) {
			uint16_t* destination;
			int pixel;

			if (x + width > rightmargin) {
				width = rightmargin - x;
				if (width <= 0)
					break;
			}
			destination = (uint16_t*)(vgapointer + lineaddressVGA[y] + 2 * x);
			for (pixel = 0; pixel < width; ++pixel) {
				uint8_t color_index;
				if ((int32_t)row_bits < 0)
					color_index = textcolor;
				else if (dropflag && (int32_t)shadow_bits < 0)
					color_index = dropcolor;
				else
					color_index = backcolor;
				*destination++ = g_flightTextPalette[color_index];
				row_bits <<= 1;
				shadow_bits <<= 1;
			}
		}
		memcpy(&shadow_bits, row_data, sizeof shadow_bits);
		shadow_bits >>= 1;
	}

	cursorx += glyph_width;
	if (cursorx >= rightmargin && lwrapflag) {
		if (autofillflag)
			rtsvga2_autofillVGA_tie98();
		cursorx = leftmargin;
		cursory += glyph_height;
	}
}

/* ------------------------------------------------------------------ */
/* rtsvga2_clearwindowVGA (0x4CEB4)                                   */
/* ------------------------------------------------------------------ */

/* Clear the margin-delimited text window to backcolor. */
// FUNCTION: TIE95 0x4CEB4
void rtsvga2_clearwindowVGA(void) {
	bottomfill = bottommargin;
	topfill = topmargin;
	leftfill = leftmargin;
	rightfill = rightmargin;
	rtsvga2_fillrectangleVGA();
}

/* ------------------------------------------------------------------ */
/* rtsvga2_fillrectangleVGA (0x4CEE4)                                 */
/* ------------------------------------------------------------------ */

/* Fill rect (leftfill..rightfill, topfill..bottomfill) with backcolor.
 * starty iterates over y; htemp2 is a spill slot in the retail asm
 * across the SetCurrentPage call -- preserved here only for layout
 * compatibility. */
// FUNCTION: TIE95 0x4CEE4
void rtsvga2_fillrectangleVGA(void) {
	int rows_remaining = (uint16_t)bottomfill - (uint16_t)topfill;
	htemp2 = (uint32_t)rows_remaining;

	starty = topfill;
	while (rows_remaining) {
		int run_len = rightfill - leftfill;
		uint8_t* dst;

		if (run_len <= 0)
			break;
		dst = vgapointer + lineaddressVGA[(uint16_t)starty] + (uint16_t)leftfill;
		memset(dst, backcolor, (size_t)run_len);
		--rows_remaining;
		++starty;
	}
	htemp2 = (uint32_t)rows_remaining;
}

/* ------------------------------------------------------------------ */
/* rtsvga2_fillboxVGA (0x4D0D4)                                       */
/* ------------------------------------------------------------------ */

/* Clipped solid-colour fill: clamp (left, top, right, bottom) against
 * the active margin rect, then dispatch to fillrectangleVGA when the
 * clipped rect is non-empty. */
// FUNCTION: TIE95 0x4D0D4
void rtsvga2_fillboxVGA(uint16_t left, uint16_t top, uint16_t right, uint16_t bottom) {
	leftfill = (int16_t)left;
	topfill = (int16_t)top;
	rightfill = (int16_t)right;
	if (left < (uint16_t)leftmargin)
		leftfill = leftmargin;
	if (right > (uint16_t)rightmargin)
		rightfill = rightmargin;
	if (top < (uint16_t)topmargin)
		topfill = topmargin;
	if (bottom > (uint16_t)bottommargin)
		bottom = (uint16_t)bottommargin;
	bottomfill = (int16_t)bottom;
	if (bottom > (uint16_t)topfill && (uint16_t)rightfill > (uint16_t)leftfill)
		rtsvga2_fillrectangleVGA();
}

// FUNCTION: TIE98 0x479A40
void rtsvga2_fillrectangleVGA_tie98(void) {
	const uint16_t color = g_flightTextPalette[backcolor];
	int16_t y;

	for (y = topfill; y < bottomfill; ++y) {
		uint16_t* destination =
			(uint16_t*)(vgapointer + lineaddressVGA[(uint16_t)y] + 2 * (uint16_t)leftfill);
		int16_t x;

		for (x = leftfill; x < rightfill; ++x)
			*destination++ = color;
	}
}

// FUNCTION: TIE98 0x479A00
void rtsvga2_clearwindowVGA_tie98(void) {
	bottomfill = bottommargin;
	topfill = topmargin;
	leftfill = leftmargin;
	rightfill = rightmargin;
	rtsvga2_fillrectangleVGA_tie98();
}

// FUNCTION: TIE98 0x479B40
void rtsvga2_fillboxVGA_tie98(uint16_t left, uint16_t top, uint16_t right, uint16_t bottom) {
	leftfill = (int16_t)left;
	topfill = (int16_t)top;
	rightfill = (int16_t)right;
	bottomfill = (int16_t)bottom;
	if (left < (uint16_t)leftmargin)
		leftfill = leftmargin;
	if (right > (uint16_t)rightmargin)
		rightfill = rightmargin;
	if (top < (uint16_t)topmargin)
		topfill = topmargin;
	if (bottom > (uint16_t)bottommargin)
		bottomfill = bottommargin;
	if ((uint16_t)bottomfill > (uint16_t)topfill && (uint16_t)rightfill > (uint16_t)leftfill)
		rtsvga2_fillrectangleVGA_tie98();
}

/* ------------------------------------------------------------------ */
/* rtsvga2_autofillVGA (0x4D190)                                      */
/* ------------------------------------------------------------------ */

/* Clear from cursor to right margin for `fontheight` rows. Called by
 * outcharVGA/outchar32VGA before newline/wrap when autofillflag is on. */
// FUNCTION: TIE95 0x4D190
void rtsvga2_autofillVGA(void) {
	if ((uint16_t)rightmargin > (uint16_t)cursorx) {
		rightfill = rightmargin;
		leftfill = cursorx;
		topfill = cursory;
		bottomfill = (int16_t)fontheight + cursory;
		if ((uint16_t)cursorx < (uint16_t)leftmargin)
			leftfill = leftmargin;
		if ((uint16_t)topfill < (uint16_t)topmargin)
			topfill = topmargin;
		if ((uint16_t)bottomfill > (uint16_t)bottommargin)
			bottomfill = bottommargin;
		if ((uint16_t)bottomfill > (uint16_t)topfill)
			rtsvga2_fillrectangleVGA();
	}
}

// FUNCTION: TIE98 0x479C00
void rtsvga2_autofillVGA_tie98(void) {
	if ((uint16_t)rightmargin <= (uint16_t)cursorx)
		return;
	rightfill = rightmargin;
	leftfill = cursorx;
	topfill = cursory;
	bottomfill = (int16_t)(cursory + fontheight);
	if ((uint16_t)leftfill < (uint16_t)leftmargin)
		leftfill = leftmargin;
	if ((uint16_t)topfill < (uint16_t)topmargin)
		topfill = topmargin;
	if ((uint16_t)bottomfill > (uint16_t)bottommargin)
		bottomfill = bottommargin;
	if ((uint16_t)bottomfill > (uint16_t)topfill)
		rtsvga2_fillrectangleVGA_tie98();
}

/* ------------------------------------------------------------------ */
/* rtsvga2_scrollbufferVGA (0x4D250)                                  */
/* ------------------------------------------------------------------ */

/* Vertical scroll of a full-screen linear byte buffer by num_rows
 * scanlines. Uses screenXRes (NOT screenMemWidth) as the stride -- this
 * is intended for off-screen buffers, not the VESA-paged framebuffer.
 * scroll_up != 0 copies buffer+scroll_bytes -> buffer (content up);
 * scroll_up == 0 copies buffer -> buffer+scroll_bytes (content down). */
// FUNCTION: TIE95 0x4D250
void rtsvga2_scrollbufferVGA(uint8_t* buffer, uint16_t num_rows, int16_t scroll_up) {
	size_t total_bytes = (size_t)screenYRes * (size_t)screenXRes;
	size_t scroll_bytes = (size_t)screenXRes * num_rows;
	size_t move_count;

	if (scroll_bytes >= total_bytes)
		return;
	move_count = total_bytes - scroll_bytes;

	if (scroll_up)
		memmove(buffer, buffer + scroll_bytes, move_count);
	else
		memmove(buffer + scroll_bytes, buffer, move_count);
}

/* ------------------------------------------------------------------ */
/* rtsvga2_saveboxVGA (0x4D2A4)                                       */
/* ------------------------------------------------------------------ */

/* Copy `width * height` pixels from framebuffer rect (x, y) into a
 * linear byte buffer dst. Used for UI-overlay backing-store snapshots.
 * (Retail Z_TIE__.EXE IDB originally had this labelled "restoreboxVGA"
 * due to a manual-naming swap -- see memory note.) */
// FUNCTION: TIE95 0x4D2A4
void rtsvga2_saveboxVGA(uint8_t* dst, uint16_t x, uint16_t y, uint16_t width, uint16_t height) {
	uint16_t row;

	for (row = 0; row < height; ++row) {
		const uint8_t* src = vgapointer + lineaddressVGA[y + row] + x;
		memcpy(dst, src, width);
		dst += width;
	}
}

// FUNCTION: TIE98 0x479CC0
void rtsvga2_saveboxVGA_tie98(uint8_t* dst, uint16_t x, uint16_t y, uint16_t width, uint16_t height) {
	uint16_t row;

	for (row = 0; row < height; ++row) {
		const uint8_t* source = vgapointer + lineaddressVGA[y + row] + 2 * x;
		memcpy(dst, source, (size_t)width * sizeof(uint16_t));
		dst += (size_t)width * sizeof(uint16_t);
	}
}

/* ------------------------------------------------------------------ */
/* rtsvga2_restoreboxVGA (0x4D37C)                                    */
/* ------------------------------------------------------------------ */

/* Blit `width * height` bytes from src back into the framebuffer at
 * (x, y). Inverse of saveboxVGA. */
// FUNCTION: TIE95 0x4D37C
void rtsvga2_restoreboxVGA(const uint8_t* src, uint16_t x, uint16_t y, uint16_t width, uint16_t height) {
	uint16_t row;

	for (row = 0; row < height; ++row) {
		uint8_t* dst = vgapointer + lineaddressVGA[y + row] + x;
		memcpy(dst, src, width);
		src += width;
	}
}

// FUNCTION: TIE98 0x479DA0
void rtsvga2_restoreboxVGA_tie98(const uint8_t* src, uint16_t x, uint16_t y, uint16_t width,
								 uint16_t height) {
	uint16_t row;

	for (row = 0; row < height; ++row) {
		uint8_t* destination = vgapointer + lineaddressVGA[y + row] + 2 * x;
		memcpy(destination, src, (size_t)width * sizeof(uint16_t));
		src += (size_t)width * sizeof(uint16_t);
	}
}

/* ------------------------------------------------------------------ */
/* rtsvga2_drawblipsVGA (0x4D444)                                     */
/* ------------------------------------------------------------------ */

// FUNCTION: TIE98 0x479E80
static void rtsvga2_drawblipsVGA_tie98(struct RadarBlip* blips, uint16_t count) {
	uint16_t index;

	for (index = 0; index < count; ++index, ++blips) {
		uint16_t* destination = (uint16_t*)(vgapointer + lineaddressVGA[blips->y] + 2 * blips->x);
		if (*destination == g_flightTextPalette[44])
			*destination = g_flightTextPalette[(uint8_t)blips->color];
		else
			blips->color = 0;
	}
}

// FUNCTION: TIE98 0x479F50
static void rtsvga2_removeblipsVGA_tie98(struct RadarBlip* blips, uint16_t count) {
	uint16_t index;

	for (index = 0; index < count; ++index, ++blips) {
		if ((uint8_t)blips->color != 0) {
			uint16_t* destination = (uint16_t*)(vgapointer + lineaddressVGA[blips->y] + 2 * blips->x);
			*destination = g_flightTextPalette[44];
		}
	}
}

/*
 * Draw `count` radar blips. Each RadarBlip is 6 bytes: (u16 x, u16 y,
 * u16 color_in/status_out). On input the low byte of `color` holds the
 * caller's colour; on output it becomes a 2-bit status bitmap:
 *   bit 0 = drew primary (x, y) row, bit 1 = drew secondary (x, y+1)
 * row. Only radar-background pixels (== 0x2C) are painted, so overlays
 * (HUD ship icon, etc.) aren't clobbered.
 *
 * The second row at y+1 is emitted only in SVGA mode. */
// FUNCTION: TIE95 0x4D444
// FUNCTION: TIE98 0x47C1D0
void rtsvga2_drawblipsVGA(struct RadarBlip* blips, uint16_t count) {
	uint16_t i;

	if (TieProfile_UsesTie98Logic() && g_flight16bppBytesPerPixel == 2) {
		rtsvga2_drawblipsVGA_tie98(blips, count);
		return;
	}
	for (i = 0; i < count; ++i, ++blips) {
		uint8_t color_in = (uint8_t)blips->color;
		uint8_t* dst_prim = vgapointer + lineaddressVGA[blips->y] + blips->x;
		if (*dst_prim == 44) {
			*dst_prim = color_in;
			blips->color = 1;
		} else {
			blips->color = 0;
		}

		if (flightResolution == TIE_FLIGHT_RES_SVGA) {
			uint8_t* dst_sec = vgapointer + lineaddressVGA[(uint16_t)(blips->y + 1)] + blips->x;
			if (*dst_sec == 44) {
				*dst_sec = color_in;
				blips->color |= 2;
			} else {
				blips->color &= 1;
			}
		}
	}
}

/* ------------------------------------------------------------------ */
/* rtsvga2_removeblipsVGA (0x4D5A0)                                   */
/* ------------------------------------------------------------------ */

/* Erase the blips previously drawn by drawblipsVGA. The status byte
 * (low byte of blip->color) encodes which rows to erase. */
// FUNCTION: TIE95 0x4D5A0
// FUNCTION: TIE98 0x47C320
void rtsvga2_removeblipsVGA(struct RadarBlip* blips, uint16_t count) {
	uint16_t i;

	if (TieProfile_UsesTie98Logic() && g_flight16bppBytesPerPixel == 2) {
		rtsvga2_removeblipsVGA_tie98(blips, count);
		return;
	}
	for (i = 0; i < count; ++i, ++blips) {
		uint16_t status = blips->color;
		uint8_t* dst_prim = vgapointer + lineaddressVGA[blips->y] + blips->x;
		if (status & 1)
			*dst_prim = 44;
		if (flightResolution == TIE_FLIGHT_RES_SVGA) {
			uint8_t* dst_sec = vgapointer + lineaddressVGA[(uint16_t)(blips->y + 1)] + blips->x;
			if (status & 2)
				*dst_sec = 44;
		}
	}
}

/* ------------------------------------------------------------------ */
/* rtsvga2_drawbracket (0x4D6B8)                                      */
/* ------------------------------------------------------------------ */

// FUNCTION: TIE98 0x479FF0
static void rtsvga2_drawbracket_tie98(void) {
	uint16_t index;

	for (index = 0; index < 10; ++index) {
		const int8_t dx = bracketdef_10pt[2 * index];
		const int8_t dy = bracketdef_10pt[2 * index + 1];
		uint16_t* destination = (uint16_t*)(vgapointer + lineaddressVGA[(uint16_t)(brackety + dy)] +
											2 * (uint16_t)(bracketx + dx));
		bracketsave_tie98[index] = *destination;
		*destination = g_flightTextPalette[206];
	}
}

// FUNCTION: TIE98 0x47A0D0
static void rtsvga2_removebracket_tie98(void) {
	uint16_t index;

	for (index = 0; index < 10; ++index) {
		const int8_t dx = bracketdef_10pt[2 * index];
		const int8_t dy = bracketdef_10pt[2 * index + 1];
		uint16_t* destination = (uint16_t*)(vgapointer + lineaddressVGA[(uint16_t)(oldbrackety + dy)] +
											2 * (uint16_t)(oldbracketx + dx));
		*destination = bracketsave_tie98[index];
	}
}

/* Draw target-reticle bracket at (bracketx, brackety). Walks
 * bracketdef_count (dx, dy) signed-byte pairs from bracketdef_ptr,
 * saves each existing pixel into bracketsave[], paints 0xCE (206).
 * Retail binds bracketdef_ptr/count to the 10-pt or 12-pt table in
 * initgraphVGA based on flightResolution. */
// FUNCTION: TIE95 0x4D6B8
// FUNCTION: TIE98 0x47C440
void rtsvga2_drawbracket(void) {
	uint32_t i;

	if (TieProfile_UsesTie98Logic() && g_flight16bppBytesPerPixel == 2) {
		rtsvga2_drawbracket_tie98();
		return;
	}
	if (bracketdef_count == 0)
		return;
	for (i = 0; i < bracketdef_count; ++i) {
		int8_t dx = (int8_t)bracketdef_ptr[2 * i];
		int8_t dy = (int8_t)bracketdef_ptr[2 * i + 1];
		uint32_t off = lineaddressVGA[(uint16_t)(brackety + dy)] + (uint16_t)(bracketx + dx);
		uint8_t* p = vgapointer + off;
		bracketsave[i] = *p;
		*p = 0xCE;
	}
}

/* ------------------------------------------------------------------ */
/* rtsvga2_removebracket (0x4D79C)                                    */
/* ------------------------------------------------------------------ */

/* Erase the bracket at (oldbracketx, oldbrackety) by restoring
 * bracketsave[0..bracketdef_count-1]. */
// FUNCTION: TIE95 0x4D79C
// FUNCTION: TIE98 0x47C530
void rtsvga2_removebracket(void) {
	uint32_t i;

	if (TieProfile_UsesTie98Logic() && g_flight16bppBytesPerPixel == 2) {
		rtsvga2_removebracket_tie98();
		return;
	}
	if (bracketdef_count == 0)
		return;
	for (i = 0; i < bracketdef_count; ++i) {
		int8_t dx = (int8_t)bracketdef_ptr[2 * i];
		int8_t dy = (int8_t)bracketdef_ptr[2 * i + 1];
		uint32_t off = lineaddressVGA[(uint16_t)(oldbrackety + dy)] + (uint16_t)(oldbracketx + dx);
		vgapointer[off] = bracketsave[i];
	}
}

/* ------------------------------------------------------------------ */
/* rtsvga2_drawcross (0x4D85C)                                        */
/* ------------------------------------------------------------------ */

/* Draw the seven-point crosshair at (x, y). */
// FUNCTION: TIE95 0x4D85C
void rtsvga2_drawcross(uint16_t x, uint16_t y, uint8_t color) {
	int i;

	for (i = 0; i < 7; ++i) {
		int8_t dx = crossdef[2 * i];
		int8_t dy = crossdef[2 * i + 1];
		uint32_t off = lineaddressVGA[(uint16_t)(y + dy)] + (uint16_t)(x + dx);
		uint8_t* p = vgapointer + off;
		crosssave[i] = *p;
		*p = color;
	}
}

/* ------------------------------------------------------------------ */
/* rtsvga2_removecross (0x4D938)                                      */
/* ------------------------------------------------------------------ */

/* Erase the previously-drawn cross at (x, y) using crosssave. */
// FUNCTION: TIE95 0x4D938
void rtsvga2_removecross(uint16_t x, uint16_t y) {
	int i;

	for (i = 0; i < 7; ++i) {
		int8_t dx = crossdef[2 * i];
		int8_t dy = crossdef[2 * i + 1];
		uint32_t off = lineaddressVGA[(uint16_t)(y + dy)] + (uint16_t)(x + dx);
		vgapointer[off] = crosssave[i];
	}
}

/* ------------------------------------------------------------------ */
/* rtsvga2_drawstars (0x4D9F8)                                        */
/* ------------------------------------------------------------------ */

/*
 * Parallax starfield renderer for the linear framebuffer.
 *
 * Algorithm:
 *   1. Clear starhashtable[512] (as uint32_t*) to 0.
 *   2. Swap oldstarptr/newstarptr. fullupdateflag -> empty the old list.
 *   3. base_{x,y,z} = (-worldeye{A,B,C}{1,2,3}) >> 2 per axis.
 *   4. Three octant lobes A/B/C, each a 16x16 nested scan over
 *      shift{A,B,C}{1,2,3}mul stepped by stardetaillevel:
 *        - eye = stareye{x,y,z}[stars[off]] + shift*[inner] + base
 *        - flip if z < 0 (puts the star in front of the camera)
 *        - frustum cull |x|>|z| or |y|>|z|
 *        - perspective project: (halfPerspFactor + |ax| << perspShift) / |z|
 *          with overflow clamp to 2147483392
 *        - screen (halfpixelswide+sx, transfm2_screenyoffset+halfpixelsdeep+sy)
 *        - skip if outside viewport
 *        - only paint when *dst >= deepspacecolor (don't overwrite HUD)
 *        - color = clamp(starcol1+stars_palette_delta[off], 0..3) - 4
 *          (lands in palette slot 0xFC..0xFF)
 *        - insert pix_off into starhashtable via linear probe (mask 0x1FF)
 *        - append pix_off to new list
 *   5. Terminate new list with -1.
 *   6. Erase pass: walk old list, erase any pixel still >= deepspacecolor
 *      whose offset is not in the hash. */
static inline int project_axis(int ax, int z, int* out_signed) {
	/* Returns 1 if the axis projects successfully (|ax|<=z), 0 to cull.
	 * *out_signed receives the projected signed screen delta. */
	int abs_ax = (ax < 0) ? -ax : ax;
	uint32_t q;

	if (abs_ax > z)
		return 0;

	q = math2_project_u32((uint32_t)abs_ax, perspShift, halfPerspFactor, (uint32_t)z);
	*out_signed = (int32_t)(ax < 0 ? 0u - q : q);
	return 1;
}

int g_dbg_star_total;
int g_dbg_star_cull_z;
int g_dbg_star_cull_x;
int g_dbg_star_cull_y;
int g_dbg_star_cull_screen_x;
int g_dbg_star_cull_screen_y;
int g_dbg_star_cull_deepspace;
int g_dbg_star_painted;
static inline void try_draw_star(int eye_x, int eye_y, int eye_z, int star_off, int32_t** new_cursor) {
	int screen_x;
	int screen_y;
	int32_t pix_off;
	uint8_t* dst;
	uint8_t palette_delta;
	uint8_t shade;
	uint32_t* hash32;
	uint16_t h;

	int sx, sy;

	g_dbg_star_total++;
	/* Mirror camera space so z >= 0. */
	if (eye_z < 0) {
		eye_x = -eye_x;
		eye_y = -eye_y;
		eye_z = -eye_z;
	}

	if (!project_axis(eye_x, eye_z, &sx)) {
		g_dbg_star_cull_x++;
		return;
	}
	if (!project_axis(eye_y, eye_z, &sy)) {
		g_dbg_star_cull_y++;
		return;
	}

	screen_x = halfpixelswide + sx;
	if (screen_x < 0 || screen_x >= (int)pixelswide) {
		g_dbg_star_cull_screen_x++;
		return;
	}
	screen_y = transfm2_screenyoffset + halfpixelsdeep + sy;
	if (screen_y < 0 || screen_y >= (int)pixelsdeep) {
		g_dbg_star_cull_screen_y++;
		return;
	}

	pix_off = lineaddressVGA[displaycorner_lines + screen_y] + (int)displaycorner_columns + screen_x;
	dst = vgapointer + (uint32_t)pix_off;
	if (*dst < deepspacecolor) {
		g_dbg_star_cull_deepspace++;
		return;
	}
	g_dbg_star_painted++;

	/* stars[] is stride-2: stars[2k]=index, stars[2k+1]=palette delta.
	 * star_off is the byte offset into stars[]. */
	palette_delta = stars[star_off + 1];
	shade = (uint8_t)(starcol1 + palette_delta);
	if (shade > 3)
		shade = 3;
	*dst = (uint8_t)(shade - 4);

	/* Linear-probe into starhashtable (512 dwords, mask 0x1FF). */
	hash32 = (uint32_t*)starhashtable;
	h = (uint16_t)pix_off;
	do {
		h = (h + 1) & 0x1FF;
	} while (hash32[h]);
	hash32[h] = (uint32_t)pix_off;

	*(*new_cursor)++ = pix_off;
}

// FUNCTION: TIE95 0x4D9F8
void rtsvga2_drawstars(void) {
	int32_t* new_cursor;
	int32_t* prev_old;
	int32_t base_x;
	int32_t base_y;
	int32_t base_z;
	int32_t base_x_saved;
	int32_t base_y_saved;
	int32_t base_z_saved;
	uint32_t* hash32;
	int32_t* m;

	int A_outer, A_star_off;
	int B_outer, B_star_off;
	int32_t base_xB, base_yB, base_zB;
	int C_outer, C_star_off;
	int32_t base_xC, base_yC, base_zC;

	g_dbg_star_total = 0;
	g_dbg_star_cull_z = 0;
	g_dbg_star_cull_x = 0;
	g_dbg_star_cull_y = 0;
	g_dbg_star_cull_screen_x = 0;
	g_dbg_star_cull_screen_y = 0;
	g_dbg_star_cull_deepspace = 0;
	g_dbg_star_painted = 0;
	memset(starhashtable, 0, 2048);

	new_cursor = oldstarptr;
	prev_old = oldstarptr;
	oldstarptr = newstarptr;
	newstarptr = prev_old;

	if (fullupdateflag)
		*oldstarptr = -1;

	/* Cube-origin eye-space coords = sum of (negated) eye-basis rows shifted. */
	base_x = (-worldeyeA1 - worldeyeB1 - worldeyeC1) >> 2;
	base_y = (-worldeyeA2 - worldeyeB2 - worldeyeC2) >> 2;
	base_z = (-worldeyeA3 - worldeyeB3 - worldeyeC3) >> 2;

	base_x_saved = base_x;
	base_y_saved = base_y;
	base_z_saved = base_z;

	/* Lobe A: inner shiftA, outer shiftB. */
	A_outer = 0;
	A_star_off = 0;
	do {
		int i;

		for (i = 0; i < 16; i += stardetaillevel) {
			int s = stars[A_star_off];
			try_draw_star(stareyex[s] + shiftA1mul[i] + base_x, stareyey[s] + shiftA2mul[i] + base_y,
						  stareyez[s] + shiftA3mul[i] + base_z, A_star_off, &new_cursor);
			A_star_off += 2;
		}
		base_x = shiftB1mul[A_outer] + base_x_saved;
		base_y = shiftB2mul[A_outer] + base_y_saved;
		base_z = shiftB3mul[A_outer] + base_z_saved;
		A_outer += stardetaillevel;
	} while (A_outer < 16);

	/* Lobe B: inner shiftA, outer shiftC. */
	B_outer = 0;
	B_star_off = 0;
	base_xB = base_x_saved;
	base_yB = base_y_saved;
	base_zB = base_z_saved;
	do {
		int j;

		for (j = 0; j < 16; j += stardetaillevel) {
			int s = stars[B_star_off];
			try_draw_star(stareyex[s] + shiftA1mul[j] + base_xB, stareyey[s] + shiftA2mul[j] + base_yB,
						  stareyez[s] + shiftA3mul[j] + base_zB, B_star_off, &new_cursor);
			B_star_off += 2;
		}
		base_xB = shiftC1mul[B_outer] + base_x_saved;
		base_yB = shiftC2mul[B_outer] + base_y_saved;
		base_zB = shiftC3mul[B_outer] + base_z_saved;
		B_outer += stardetaillevel;
	} while (B_outer < 16);

	/* Lobe C: inner shiftB, outer shiftC. */
	C_outer = 0;
	C_star_off = 0;
	base_xC = base_x_saved;
	base_yC = base_y_saved;
	base_zC = base_z_saved;
	do {
		int k;

		for (k = 0; k < 16; k += stardetaillevel) {
			int s = stars[C_star_off];
			try_draw_star(stareyex[s] + shiftB1mul[k] + base_xC, stareyey[s] + shiftB2mul[k] + base_yC,
						  stareyez[s] + shiftB3mul[k] + base_zC, C_star_off, &new_cursor);
			C_star_off += 2;
		}
		base_xC = shiftC1mul[C_outer] + base_x_saved;
		base_yC = shiftC2mul[C_outer] + base_y_saved;
		base_zC = shiftC3mul[C_outer] + base_z_saved;
		C_outer += stardetaillevel;
	} while (C_outer < 16);

	*new_cursor = -1;

	/* Erase pass: anything in the old list whose pixel is still a star
	 * colour and isn't found in this frame's hashtable gets overwritten
	 * with deepspacecolor. */
	hash32 = (uint32_t*)starhashtable;
	m = oldstarptr;
	while (*m != -1) {
		uint32_t pix_off = (uint32_t)*m;
		int32_t key = *m;
		uint8_t* dst;

		++m;
		dst = vgapointer + pix_off;
		if (*dst > deepspacecolor) {
			uint16_t h = (uint16_t)key;
			for (;;) {
				uint32_t v;

				h = (h + 1) & 0x1FF;
				v = hash32[h];
				if ((int32_t)v == key)
					break;
				if (!v) {
					*dst = deepspacecolor;
					break;
				}
			}
		}
	}
}

/* TIE98 keeps random star positions plus indexed and packed-color tables in
 * handle-backed arrays. Fixed storage preserves that lifetime and separation. */
static uint8_t tie98_star_position_index[3072];
static uint8_t tie98_star_color8[3072];
static uint16_t tie98_star_color16[3072];
static int tie98_star_positions_initialized;
static int tie98_star_color8_initialized;
static int tie98_star_color16_initialized;

/* PORT: refresh colors after palette or 16-bit display-format changes. */
void Tie98StarColors_Invalidate(void) {
	tie98_star_color8_initialized = 0;
	tie98_star_color16_initialized = 0;
}

// FUNCTION: TIE98 0x47C7F0
// RTSVGA2_drawstars
void rtsvga2_drawstars_tie98(void) {
	float steps[3][3];
	float base[3];
	static const int inner_axis[3] = { 0, 0, 1 };
	static const int outer_axis[3] = { 1, 2, 2 };
	int grid_size;
	float reciprocal;
	uint16_t background_color16;
	int star_index;
	int lobe;

	if (g_flight16bppBytesPerPixel == 2 && !tie98_star_color16_initialized) {
		const uint16_t gray_step = FrontendDisplay_GetPixelFormat555() ? 1057 : 2113;
		int i;

		for (i = 0; i < 3072; ++i)
			tie98_star_color16[i] = (uint16_t)(gray_step * ((math2_getrandom() & 0xF) + 8));
		tie98_star_color16_initialized = 1;
	} else if (g_flight16bppBytesPerPixel != 2 && !tie98_star_color8_initialized) {
		int i;

		for (i = 0; i < 3072; ++i) {
			const uint8_t brightness = (uint8_t)((math2_getrandom() & 0xF) + 8);
			uint8_t rgb[3];
			rgb[0] = brightness;
			rgb[1] = brightness;
			rgb[2] = brightness;
			tie98_star_color8[i] = (uint8_t)rtsvga2_findNearestColor(rgb, rtsvga2_vgapalette, 0x40, 0x100);
		}
		tie98_star_color8_initialized = 1;
	}

	if (!tie98_star_positions_initialized) {
		int i;

		for (i = 0; i < 3072; ++i) {
			uint16_t index;
			do {
				index = (uint16_t)math2_getrandom() & 0x7F;
			} while (index > 124);
			tie98_star_position_index[i] = (uint8_t)index;
		}
		tie98_star_positions_initialized = 1;
	}

	grid_size = 32 / stardetaillevel;
	reciprocal = 1.0f / (float)grid_size;
	steps[0][0] = (float)(worldeyeA1 >> 1) * reciprocal;
	steps[0][1] = (float)(worldeyeA2 >> 1) * reciprocal;
	steps[0][2] = (float)(worldeyeA3 >> 1) * reciprocal;
	steps[1][0] = (float)(worldeyeB1 >> 1) * reciprocal;
	steps[1][1] = (float)(worldeyeB2 >> 1) * reciprocal;
	steps[1][2] = (float)(worldeyeB3 >> 1) * reciprocal;
	steps[2][0] = (float)(worldeyeC1 >> 1) * reciprocal;
	steps[2][1] = (float)(worldeyeC2 >> 1) * reciprocal;
	steps[2][2] = (float)(worldeyeC3 >> 1) * reciprocal;
	base[0] = (float)(-(worldeyeA1 + worldeyeB1 + worldeyeC1) >> 2);
	base[1] = (float)(-(worldeyeA2 + worldeyeB2 + worldeyeC2) >> 2);
	base[2] = (float)(-(worldeyeA3 + worldeyeB3 + worldeyeC3) >> 2);
	background_color16 = g_flightTextPalette[deepspacecolor];
	star_index = 0;

	for (lobe = 0; lobe < 3; ++lobe) {
		float row_x = base[0];
		float row_y = base[1];
		float row_z = base[2];
		const float* inner_step = steps[inner_axis[lobe]];
		const float* outer_step = steps[outer_axis[lobe]];
		int row;

		for (row = 0; row < grid_size; ++row) {
			float eye_x = row_x;
			float eye_y = row_y;
			float eye_z = row_z;
			int column;

			for (column = 0; column < grid_size; ++column, ++star_index) {
				const uint8_t position_index = tie98_star_position_index[star_index];
				float x = (float)stareyex[position_index] + eye_x;
				float y = (float)stareyey[position_index] + eye_y;
				float z = (float)stareyez[position_index] + eye_z;
				if (z < 0.0f) {
					x = -x;
					y = -y;
					z = -z;
				}
				if (-x < z && x < z && -y < z && y < z) {
					const float projection = (float)(uint32_t)perspFactor / z;
					const int screen_x = (int)halfpixelswide + (int)(projection * x);
					const int screen_y = transfm2_screenyoffset + (int)halfpixelsdeep + (int)(projection * y);
					if (screen_x >= 0 && screen_x < (int)pixelswide && screen_y >= 0 &&
						screen_y < (int)pixelsdeep) {
						uint8_t* destination =
							vgapointer + (size_t)g_surfacePitch * (displaycorner_lines + (uint32_t)screen_y) +
							(size_t)g_flight16bppBytesPerPixel * (displaycorner_columns + (uint32_t)screen_x);
						if (g_flight16bppBytesPerPixel == 2) {
							uint16_t* destination16 = (uint16_t*)destination;
							if (*destination16 == background_color16)
								*destination16 = tie98_star_color16[star_index];
						} else if (*destination == deepspacecolor) {
							*destination = tie98_star_color8[star_index];
						}
					}
				}
				eye_x += inner_step[0];
				eye_y += inner_step[1];
				eye_z += inner_step[2];
			}
			row_x += outer_step[0];
			row_y += outer_step[1];
			row_z += outer_step[2];
		}
	}
}

/* ------------------------------------------------------------------ */
/* rtsvga2_takeScreenshot (0x4E670)                                   */
/* ------------------------------------------------------------------ */

/* Write the framebuffer and active palette as PCX. */
static int write_pcx(const char* path, const uint8_t* img, uint16_t width, uint16_t height,
					 const uint8_t* pal_768) {
	uint8_t header[128];
	TieFile* f = TieStorage_Open(TIE_FILE_ROOT_USER, path, "wb");
	int i;
	uint16_t bpl;
	uint16_t row;

	if (!f)
		return 0;

	memset(header, 0, sizeof header);
	header[0] = 0x0A; /* ZSoft PCX */
	header[1] = 5;    /* version */
	header[2] = 1;    /* RLE */
	header[3] = 8;    /* 8 bits per plane */
	/* xmin, ymin = 0 */
	header[8] = (uint8_t)((width - 1) & 0xFF);
	header[9] = (uint8_t)(((width - 1) >> 8) & 0xFF);
	header[10] = (uint8_t)((height - 1) & 0xFF);
	header[11] = (uint8_t)(((height - 1) >> 8) & 0xFF);
	header[12] = (uint8_t)(width & 0xFF);
	header[13] = (uint8_t)((width >> 8) & 0xFF);
	header[14] = (uint8_t)(height & 0xFF);
	header[15] = (uint8_t)((height >> 8) & 0xFF);
	/* 48-byte 16-color palette placeholder: retail writes 0, 1, .. 47. */
	for (i = 0; i < 48; ++i)
		header[16 + i] = (uint8_t)i;
	header[64] = 0; /* reserved */
	header[65] = 1; /* 1 plane */
	bpl = (uint16_t)(width + (width & 1));
	header[66] = (uint8_t)(bpl & 0xFF);
	header[67] = (uint8_t)((bpl >> 8) & 0xFF);
	/* Remainder (palette info + padding) = 0. */
	if (TieStorage_Write(header, 1, 128, f) != 128) {
		TieStorage_Close(f);
		return 0;
	}

	/* RLE-encode each scanline and write. */
	for (row = 0; row < height; ++row) {
		const uint8_t* line = img + (size_t)row * width;
		uint16_t x = 0;
		while (x < bpl) {
			uint8_t v = (x < width) ? line[x] : 0;
			uint8_t run = 1;
			while (run < 63 && (x + run) < bpl && ((x + run) < width ? line[x + run] : 0) == v)
				++run;
			if (run > 1 || (v & 0xC0) == 0xC0) {
				uint8_t hdr = (uint8_t)(0xC0 | run);
				TieStorage_Putc(hdr, f);
			}
			TieStorage_Putc(v, f);
			x = (uint16_t)(x + run);
		}
	}

	/* 256-color palette trailer: marker 0x0C then 768 bytes RGB (8-bit). */
	TieStorage_Putc(0x0C, f);
	for (i = 0; i < 768; ++i)
		TieStorage_Putc((uint8_t)(pal_768[i] << 2), f); /* 6-bit DAC -> 8-bit */

	TieStorage_Close(f);
	return 1;
}

// FUNCTION: TIE95 0x4E670
int rtsvga2_takeScreenshot(void) {
	char path[64];
	size_t sz;
	uint8_t* buf;
	int ok;

	return 0;

	if (!vgapointer || screenXRes <= 0 || screenYRes <= 0)
		return 0;
	sz = (size_t)screenXRes * (size_t)screenYRes;
	buf = (uint8_t*)malloc(sz);
	if (!buf)
		return 0;

	rtsvga2_saveboxVGA(buf, 0, 0, (uint16_t)screenXRes, (uint16_t)screenYRes);

	snprintf(path, sizeof path, "screenshots/screen%d.pcx", screenshot_seq++);
	ok = write_pcx(path, buf, (uint16_t)screenXRes, (uint16_t)screenYRes, rtsvga2_vgapalette);
	free(buf);
	return ok;
}
