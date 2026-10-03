#include "tie/rtsvga2.h"
#include "landru/pal.h"  /* xpal_Set_VGA_Palette */
#include "landru/vesa.h" /* vesa_buff_gbl — the scanout buffer vgapointer aliases */
#include "tie/edition.h"
#include "tie/frontend_display_tie98.h"
#include "tie/logbuf2.h" /* pixelswide / pixelsdeep / halfpixels / displaycorner / deepspacecolor */
#include "tie/math2.h"
#include "tie/panel.h" /* RadarBlip (x, y, color) */
#include "tie/render_texture_tie98.h"
#include "tie/tie.h"
#include "tie/transfm2.h" /* transfm2_screenyoffset, worldeye* */
#include "tie/xtrans2.h"
#include "tie_runtime/audio/config.h"
#include "tie_runtime/diagnostics/diagnostics.h"
#include "tie_runtime/display/classic_display.h"
#include "tie_runtime/display/classic_framebuffer.h"
#include "tie_runtime/display/dos_bios.h"
#include "tie_runtime/display/tie98_starfield.h"
#include "tie_runtime/flight_assets/model_types.h"
#include "tie_runtime/input/input.h"
#include "tie_runtime/runtime/exports.h"
#include "tie_runtime/runtime/profile.h"
#include "tie_runtime/runtime/wide_arithmetic.h"

#include <stdint.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Module-local static state (watdbg "static" in demo; retail matches) */
/* ------------------------------------------------------------------ */

/* 10-point bracket shape (5-wide x 3-tall dx/dy deltas around centre). */
// GLOBAL: TIE95 0xC7A20
// GLOBAL: TIE98 0x4EB760
static const int8_t bracketdef_10pt[20] = { -1, +1, -2, +1, -2, 0, -2, -1, -1, -1,
											+1, -1, +2, -1, +2, 0, +2, +1, +1, +1 };

/* 12-point bracket shape (5-wide x 4-tall; new in retail for VESA 640x480). */
// GLOBAL: TIE95 0xC7A34
// GLOBAL: TIE98 0x4EB778
static const int8_t bracketdef_12pt[24] = { -1, +2, -2, +2, -2, +1, -2, 0,  -2, -1, -1, -1,
											+1, -1, +2, -1, +2, 0,  +2, +1, +2, +2, +1, +2 };

/* 7-point cross shape (4 arms + centre duplicate). */
// GLOBAL: TIE95 0xC7A54
// GLOBAL: TIE98 0x4EB798
static const int8_t crossdef[14] = { -2, 0, -1, 0, 0, 0, +1, 0, +2, 0, 0, +1, 0, -1 };

/* Pointer+count selected by initgraphVGA based on flightResolution. */
// GLOBAL: TIE95 0xC7A4C
// GLOBAL: TIE98 0x4EB790
static const int8_t* bracketdef_ptr = bracketdef_10pt;
// GLOBAL: TIE95 0xC7A50
// GLOBAL: TIE98 0x4EB794
static uint32_t bracketdef_count = 10;

/* Saved pixels under the bracket / cross so remove* can restore them. */
// GLOBAL: TIE95 0xDC414
static uint8_t bracketsave[16];
// GLOBAL: TIE98 0x5897B0
static uint16_t bracketsave_tie98[10];
// GLOBAL: TIE95 0xDE77E
static uint8_t crosssave[7];

/* Last VESA window-A / window-B page (-1 = unknown, force BIOS call). */
// GLOBAL: TIE95 0xC7A64
// GLOBAL: TIE98 0x4EB7AC
static uint32_t lastpageA = 0xFFFFFFFFu;
// GLOBAL: TIE95 0xC7A68
// GLOBAL: TIE98 0x4EB7B0
static uint32_t lastpageB = 0xFFFFFFFFu;

/* fillrectangle / autofill scratch state (module-local in demo watdbg). */
// GLOBAL: TIE95 0xDE776
// GLOBAL: TIE98 0x5897AC
static int16_t topfill;
// GLOBAL: TIE95 0xDE772
// GLOBAL: TIE98 0x5897A0
static int16_t bottomfill;
// GLOBAL: TIE95 0xDE77C
// GLOBAL: TIE98 0x5897A8
static int16_t leftfill;
// GLOBAL: TIE95 0xDE77A
static int16_t rightfill;
/* fillrectangle per-row y counter */
// GLOBAL: TIE95 0xDE778
static int16_t starty;
/* fillrectangle remaining-row counter */
// GLOBAL: TIE95 0xDE764
static uint32_t htemp2;

/* Brightness setting from OPTION_optionsroom (0..256; 256 = unchanged). */
// GLOBAL: TIE95 0xCDD0C
// GLOBAL: TIE98 0x4F3C68
uint32_t brightness_setting = 256;

/* ------------------------------------------------------------------ */
/* RTSVGA2-owned shared globals (extern in header)                    */
/* ------------------------------------------------------------------ */

// GLOBAL: TIE95 0xDC424
// GLOBAL: TIE98 0x591920
uint8_t rtsvga2_vgapalette[768];
// GLOBAL: TIE95 0xC7A04
uint8_t* vgapointer;
/* One-past-the-screen sentinel: drawshape's 0xFE end-of-line bumps
 * drawshapey before the outer loop checks for 0xFF (end-of-shape), so an
 * icon whose last row sits on screenYRes-1 triggers a single trailing
 * read at index screenYRes. The retail database had the same 480 layout
 * and tolerated the OOB read because no write follows (the next opcode
 * is 0xFF which returns); the port adds one entry so UBSan doesn't trip. */
// GLOBAL: TIE95 0xDBC94
// GLOBAL: TIE98 0x5F3BE0
int32_t lineaddressVGA[RTSVGA2_LINE_ADDRESS_COUNT];

/* Star double-buffer pair + the two underlying buffers. */
// GLOBAL: TIE95 0xDD3E4
// GLOBAL: TIE98 0x5F44C0
static int32_t starposbuf1[768];
// GLOBAL: TIE95 0xDC7E4
// GLOBAL: TIE98 0x5F50C0
static int32_t starposbuf2[768];
// GLOBAL: TIE95 0xC7A08
// GLOBAL: TIE98 0x4EB748
int32_t* newstarptr = starposbuf1;
// GLOBAL: TIE95 0xC7A0C
// GLOBAL: TIE98 0x4EB74C
int32_t* oldstarptr = starposbuf2;

// GLOBAL: TIE95 0xDC724
// GLOBAL: TIE98 0x5F43A0
int32_t shiftA1mul[16];
// GLOBAL: TIE95 0xDC764
// GLOBAL: TIE98 0x5F3B80
int32_t shiftA2mul[16];
// GLOBAL: TIE95 0xDC7A4
// GLOBAL: TIE98 0x5F4360
int32_t shiftA3mul[16];
// GLOBAL: TIE95 0xDE6A4
// GLOBAL: TIE98 0x5F4480
int32_t shiftB1mul[16];
// GLOBAL: TIE95 0xDE6E4
// GLOBAL: TIE98 0x5F43E0
int32_t shiftB2mul[16];
// GLOBAL: TIE95 0xDE724
// GLOBAL: TIE98 0x5F4420
int32_t shiftB3mul[16];
// GLOBAL: TIE95 0xDDFE4
// GLOBAL: TIE98 0x5F5D60
int32_t shiftC1mul[16];
// GLOBAL: TIE95 0xDE024
// GLOBAL: TIE98 0x5F5CC0
int32_t shiftC2mul[16];
// GLOBAL: TIE95 0xDE064
// GLOBAL: TIE98 0x5F5D20
int32_t shiftC3mul[16];

// GLOBAL: TIE95 0xDE2A4
// GLOBAL: TIE98 0x5F3980
int32_t stareyex[128];
// GLOBAL: TIE95 0xDE4A4
// GLOBAL: TIE98 0x5F3580
int32_t stareyey[128];
// GLOBAL: TIE95 0xDE0A4
// GLOBAL: TIE98 0x5F3780
int32_t stareyez[128];

// GLOBAL: TIE95 0xC7A00
// GLOBAL: TIE98 0x4EB740
uint16_t stardetaillevel = 1;
// GLOBAL: TIE95 0xDE774
// GLOBAL: TIE98 0x5F3BC0
int16_t drawshapex;
// GLOBAL: TIE95 0xDE768
// GLOBAL: TIE98 0x5F3BC2
int16_t drawshapey;
// GLOBAL: TIE95 0xDE770
int16_t drawwidth;
// GLOBAL: TIE95 0xDE785
// GLOBAL: TIE98 0x5F4460
uint8_t basecolor;
// GLOBAL: TIE95 0xDE786
// GLOBAL: TIE98 0x5F4464
int16_t skipcolorvga;

/* TIE98 star-table lazy-initialisation flags. The tables themselves live in
 * Memory_AllocHandle blocks in the original; see tie98_starfield.h. */
// GLOBAL: TIE98 0x589824
int g_starColor8Allocated;
// GLOBAL: TIE98 0x58A290
int g_starColor8Initialized;
// GLOBAL: TIE98 0x589828
int g_starColor16Initialized;
// GLOBAL: TIE98 0x58982C
int g_starPositionsInitialized;
static void rtsvga2_drawmonoshapeVGA_tie98(const uint8_t* shape, uint16_t x, uint16_t y, uint16_t skip_color,
										   uint8_t color);
static void rtsvga2_drawblipsVGA_tie98(struct RadarBlip* blips, uint16_t count);
static void rtsvga2_removeblipsVGA_tie98(struct RadarBlip* blips, uint16_t count);
static void rtsvga2_drawbracket_tie98(void);
static void rtsvga2_removebracket_tie98(void);

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

	if (TIE_DISPLAY_DX5) {
		uint32_t y;

		for (y = 0; y < screenYRes; ++y)
			lineaddressVGA[y] = (int32_t)g_surfacePitch * y;
		memset(vgapointer, 0, (size_t)screenYRes * g_surfacePitch);
		if (flightResolution == TIE_FLIGHT_RES_SVGA || flightResolution == TIE_FLIGHT_RES_SVGA_16 ||
			flightResolution == TIE_FLIGHT_RES_SVGA_D3D) {
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
		uint32_t y;
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

	if (flightResolution == TIE_FLIGHT_RES_SVGA || flightResolution == TIE_FLIGHT_RES_SVGA_16 ||
		flightResolution == TIE_FLIGHT_RES_SVGA_D3D) {
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
		int16_t y;

		vgapointer = (uint8_t*)vga_ptr;
		for (y = 0; y < num_lines; ++y)
			lineaddressVGA[y] = (int32_t)mem_width * y;
	} else {
		int16_t y;

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
 * scaled by (brightness_setting / 256) and clamped to 63.
 * brightness_setting == 256 takes a straight copy fast path. */
// FUNCTION: TIE95 0x4BAF0
void rtsvga2_applyBrightness(const uint8_t* rgb_src, uint8_t* rgb_dst, int start_idx, int count) {
	if (brightness_setting == 256) {
		while (count--) {
			rgb_dst[start_idx * 3 + 0] = rgb_src[start_idx * 3 + 0];
			rgb_dst[start_idx * 3 + 1] = rgb_src[start_idx * 3 + 1];
			rgb_dst[start_idx * 3 + 2] = rgb_src[start_idx * 3 + 2];
			start_idx++;
		}
		return;
	}

	while (count--) {
		uint8_t r = rgb_src[start_idx * 3 + 0], g = rgb_src[start_idx * 3 + 1],
				b = rgb_src[start_idx * 3 + 2];
		uint8_t value;
		uint8_t v_max;
		uint8_t v_min;
		uint8_t sector;
		uint8_t sat;
		uint8_t hue;

		if (r >= g && r >= b)
			v_max = r;
		else if (g >= r && g >= b)
			v_max = g;
		else
			v_max = b;

		if (r <= g && r <= b)
			v_min = r;
		else if (g <= r && g <= b)
			v_min = g;
		else
			v_min = b;

		value = v_max;
		if (value != 0)
			sat = (value - v_min) * 63 / value;
		else
			sat = 0;

		if (sat != 0) {
			if (r == v_max) {
				if (g >= b) {
					hue = (g - b) * 63 / (r - v_min);
					sector = 0;
				} else {
					hue = 63 - (b - g) * 63 / (r - v_min);
					sector = 5;
				}
			} else if (g == v_max) {
				if (b >= r) {
					hue = (b - r) * 63 / (g - v_min);
					sector = 2;
				} else {
					hue = 63 - (r - b) * 63 / (g - v_min);
					sector = 1;
				}
			} else if (r >= g) {
				hue = (r - g) * 63 / (v_max - v_min);
				sector = 4;
			} else {
				hue = 63 - (g - r) * 63 / (v_max - v_min);
				sector = 3;
			}
		}

		value = (value * brightness_setting) >> 8;
		if (value > 63)
			value = 63;

		if (sat != 0) {
			uint8_t lo = (63 - sat) * value / 63;
			uint8_t falling = value * (63 - sat * hue / 63) / 63;
			uint8_t rising = value * (63 - sat * (63 - hue) / 63) / 63;

			switch (sector) {
				case 0:
					r = value;
					g = rising;
					b = lo;
					break;
				case 1:
					r = falling;
					g = value;
					b = lo;
					break;
				case 2:
					r = lo;
					g = value;
					b = rising;
					break;
				case 3:
					r = lo;
					g = falling;
					b = value;
					break;
				case 4:
					r = rising;
					g = lo;
					b = value;
					break;
				case 5:
					r = value;
					g = lo;
					b = falling;
					break;
			}
		} else {
			r = value;
			g = value;
			b = value;
		}

		rgb_dst[start_idx * 3 + 0] = r;
		rgb_dst[start_idx * 3 + 1] = g;
		rgb_dst[start_idx * 3 + 2] = b;
		start_idx++;
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
	RGBStruct zeroes[256];
	memset(zeroes, 0, sizeof zeroes);
	colorcycleflag = 0;
	xpal_Set_VGA_Palette(zeroes, 0, 256);
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
	xpal_Set_VGA_Palette((RGBStruct*)scaled, 0, 256);
	blankcondition &= ~1u;
	colorcycleflag = 1;
}

/* ------------------------------------------------------------------ */
/* rtsvga2_buildpaletteVGA (0x4BF88)                                  */
/* ------------------------------------------------------------------ */

/* Copy `count` RGB triplets (R, G, B; 6-bit DAC range) from rgb_src
 * into rtsvga2_vgapalette[start_idx..start_idx+count]. In-memory only
 * -- the caller pushes to hardware via XPAL/unblank. */
// FUNCTION: TIE95 0x4BF88
// FUNCTION: TIE98 0x47AAE0
void rtsvga2_buildpaletteVGA(const uint8_t* rgb_src, uint16_t start_idx, uint16_t count) {
	uint16_t i;

	for (i = start_idx; i < start_idx + count; ++i, rgb_src += 3) {
		uint32_t off = 3u * i;
		rtsvga2_vgapalette[off] = rgb_src[0];
		rtsvga2_vgapalette[off + 1] = rgb_src[1];
		rtsvga2_vgapalette[off + 2] = rgb_src[2];
	}

#if defined(TIE98) || defined(TIE_MODERN)
	/* TIE98 16bpp modes also rebuild the RGB565/555 lookup; the first 64
	 * entries map the deep-space colour to transparent. */
	if (TIE_DISPLAY_DX5 && g_flight16bppBytesPerPixel == 2) {
		rtsvga2_applyBrightness16_tie98(rtsvga2_vgapalette, g_flightTextPalette, start_idx, count);
		if (start_idx == 0 && count == 64) {
			const uint16_t transparent_color = g_flightTextPalette[deepspacecolor];
			uint16_t index;

			for (index = 0; index < 64; ++index) {
				if (g_flightTextPalette[index] == transparent_color)
					g_flightTextPalette[index] = 0;
			}
		}
		return;
	}
#endif
#ifdef TIE_MODERN
	/* Publish immediately because the first flight frame may precede unblankVGA. */
	TieClassicFramebuffer_SetPalette(&rtsvga2_vgapalette[3u * start_idx], (int)start_idx, (int)count);
#endif
}

/* ------------------------------------------------------------------ */
/* rtsvga2_savepaletteVGA (0x4BFD0)                                   */
/* ------------------------------------------------------------------ */

/* Snapshot the full in-memory palette (256 triplets = 768 bytes) into
 * rgb_dst. */
// FUNCTION: TIE95 0x4BFD0
// FUNCTION: TIE98 0x47AB90
void rtsvga2_savepaletteVGA(uint8_t* rgb_dst) {
	uint16_t i;

	for (i = 0; i < 256; ++i, rgb_dst += 3) {
		rgb_dst[0] = rtsvga2_vgapalette[3 * i];
		rgb_dst[1] = rtsvga2_vgapalette[3 * i + 1];
		rgb_dst[2] = rtsvga2_vgapalette[3 * i + 2];
	}
}

/* ------------------------------------------------------------------ */
/* rtsvga2_restorepaletteVGA (0x4C00C)                                */
/* ------------------------------------------------------------------ */

/* Replace the full in-memory palette with `rgb_src`. */
// FUNCTION: TIE95 0x4C00C
// FUNCTION: TIE98 0x47ABC0
void rtsvga2_restorepaletteVGA(const uint8_t* rgb_src) { rtsvga2_buildpaletteVGA(rgb_src, 0, 256); }

// FUNCTION: TIE98 0x47A500
void rtsvga2_clearflightdisplay(void) {
	rtsvga2_blankVGA();
	blankcondition = 0;
	xvesa_Erase_Video(16);
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
/* rtsvga2_calcpositionVGA (0x4C208)                                  */
/* ------------------------------------------------------------------ */

/* Compute framebuffer byte offset for (x, y) = y * screenMemWidth + x. */
// FUNCTION: TIE95 0x4C208
// FUNCTION: TIE98 0x47AF20
uint32_t rtsvga2_calcpositionVGA(uint16_t x, uint16_t y) {
	return TIE_DISPLAY_EDITION((uint32_t)screenMemWidth * y + x,
							   g_surfacePitch * y + g_flight16bppBytesPerPixel * x);
}

/* ------------------------------------------------------------------ */
/* rtsvga2_drawshapeVGA (0x4C224)                                     */
/* ------------------------------------------------------------------ */

// FUNCTION: TIE95 0x4C224
void rtsvga2_drawshapeVGA(const uint8_t* shape, int16_t x, int16_t y, int16_t skip_color, uint16_t flip_x) {
	basecolor = 0;
	rtsvga2__lowdrawshapeVGA(shape, (uint16_t)x, (uint16_t)y, (uint16_t)skip_color, (int)flip_x, 0);
}

/* ------------------------------------------------------------------ */
/* rtsvga2_drawmonoshapeVGA (0x4C254)                                 */
/* ------------------------------------------------------------------ */

/* Mono (single-colour stencil) variant: sets basecolor = color and calls
 * _lowdrawshapeVGA with mono_flag = 1 so every painted pixel is `color`. */
// FUNCTION: TIE95 0x4C254
// FUNCTION: TIE98 0x47AF80
void rtsvga2_drawmonoshapeVGA(const uint8_t* shape, uint16_t x, uint16_t y, uint16_t skip_color,
							  uint16_t color) {
	if (TIE_FLIGHT_TIE98 && g_flight16bppBytesPerPixel == 2) {
		rtsvga2_drawmonoshapeVGA_tie98(shape, x, y, skip_color, color);
		return;
	}
	basecolor = color;
	rtsvga2__lowdrawshapeVGA(shape, x, y, skip_color, 0, 1);
}

// FUNCTION: TIE98 0x478B40
void rtsvga2_applyBrightness16_tie98(const uint8_t* rgb6, uint16_t* output, uint32_t start_idx,
									 uint32_t count) {
	uint8_t adjusted[3 * 1024];

	uint32_t i;

	rtsvga2_applyBrightness(rgb6, adjusted, (int)start_idx, (int)count);
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

		uint8_t op;
		uint8_t color;
		int16_t run_raw;

		for (;;) {
			op = *shape++;
			if (op < 251 || op == 253) {
				uint16_t run_len;

				if (op == 253) {
					/* 0xFD: explicit (count, color) run. */
					run_raw = (int16_t)shape[0];
					color = shape[1];
					shape += 2;
				} else {
					color = (uint8_t)(op >> 2);
					if (!mono_flag)
						color += basecolor;
					run_raw = (int16_t)(op & 3);
				}

				run_len = (uint16_t)(run_raw + 1);
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
				continue;
			}

			if (op == 251) {
				/* 0xFB: set basecolor (or skip byte in mono mode). */
				if (mono_flag)
					++shape;
				else
					basecolor = *shape++;
				continue;
			}

			if (op == 252 && !mono_flag) {
				/* 0xFC: alternating (color, color+1) pair run on the same
				 * scanline; later opcodes continue from the current dst. */
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
				continue;
			}
			break;
		}

		/* 0xFF: end-of-shape. */
		if (op == 255)
			return;

		/* 0xFE or others: end-of-line. */
		++drawshapey;
	}
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
		int draw_row;

		if (y >= bottommargin)
			break;

		row_width = glyph_width;
		row_x = cursorx;
		row_bits = row_ptr[0];

		if (dropflag)
			++row_width;

		draw_row = 1;
		if (cursorx < leftmargin) {
			int shift = leftmargin - cursorx;
			if (shift >= row_width) {
				draw_row = 0;
			} else {
				row_width -= shift;
				row_x = leftmargin;
				row_bits <<= shift;
			}
		}

		if (draw_row && y >= topmargin) {
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
		int draw_row;

		if (y >= bottommargin)
			break;

		row_width = glyph_width;
		row_x = cursorx;
		row_bits = row_ptr[0];

		if (saved_dropflag)
			++row_width;

		draw_row = 1;
		if (cursorx < leftmargin) {
			int shift = leftmargin - cursorx;
			if (shift >= row_width) {
				draw_row = 0;
			} else {
				row_width -= shift;
				row_x = leftmargin;
				row_bits <<= shift;
			}
		}

		if (draw_row && y >= topmargin) {
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
	bottomfill = (int16_t)bottom;
	if ((uint16_t)leftfill < leftmargin)
		leftfill = leftmargin;
	if ((uint16_t)rightfill > rightmargin)
		rightfill = rightmargin;
	if ((uint16_t)topfill < topmargin)
		topfill = topmargin;
	if ((uint16_t)bottomfill > bottommargin)
		bottomfill = bottommargin;
	if ((uint16_t)bottomfill > (uint16_t)topfill && (uint16_t)rightfill > (uint16_t)leftfill)
		rtsvga2_fillrectangleVGA();
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

	if (TIE_FLIGHT_TIE98 && g_flight16bppBytesPerPixel == 2) {
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

	if (TIE_FLIGHT_TIE98 && g_flight16bppBytesPerPixel == 2) {
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

// FUNCTION: TIE98 0x479CC0
void rtsvga2_saveboxVGA_tie98(uint8_t* dst, uint16_t x, uint16_t y, uint16_t width, uint16_t height) {
	uint16_t row;

	for (row = 0; row < height; ++row) {
		const uint8_t* source = vgapointer + lineaddressVGA[y + row] + 2 * x;
		memcpy(dst, source, (size_t)width * sizeof(uint16_t));
		dst += (size_t)width * sizeof(uint16_t);
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

	if (TIE_FLIGHT_TIE98 && g_flight16bppBytesPerPixel == 2) {
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

/* ------------------------------------------------------------------ */
/* rtsvga2_removebracket (0x4D79C)                                    */
/* ------------------------------------------------------------------ */

/* Erase the bracket at (oldbracketx, oldbrackety) by restoring
 * bracketsave[0..bracketdef_count-1]. */
// FUNCTION: TIE95 0x4D79C
// FUNCTION: TIE98 0x47C530
void rtsvga2_removebracket(void) {
	uint32_t i;

	if (TIE_FLIGHT_TIE98 && g_flight16bppBytesPerPixel == 2) {
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

/* ------------------------------------------------------------------ */
/* rtsvga2_removecross (0x4D938)                                      */
/* ------------------------------------------------------------------ */

/* Erase the previously-drawn cross at (x, y) using crosssave. */
// FUNCTION: TIE95 0x4D938
void rtsvga2_removecross(uint16_t x, uint16_t y) {
	uint16_t j;
	uint16_t n;
	uint16_t i;

	i = 0;
	j = 0;
	for (n = 7; n > 0; --n) {
		uint32_t off = lineaddressVGA[crossdef[j + 1] + y] + (crossdef[j] + x);

		if (flightResolution != (int16_t)TIE_FLIGHT_RES_VGA && (uintptr_t)vgapointer == 0xA0000) {
			uint16_t page = (uint16_t)(off / vesa_page_size);

			off %= vesa_page_size;
			rtsvga2_SetCurrentPage(vesa_window, page);
		}
		vgapointer[off] = crosssave[i];
		i++;
		j += 2;
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
// FUNCTION: TIE95 0x4D9F8
void rtsvga2_drawstars(void) {
	int32_t star_offsets[768];
	uint8_t star_colors[768];
	int32_t* cursor;
	int32_t eye_x, eye_y, eye_z;
	int32_t base_x_a, base_y_a, base_z_a;
	int32_t base_x_b, base_y_b, base_z_b;
	int32_t base_x_c, base_y_c, base_z_c;
	int32_t origin_x, origin_y, origin_z;
	int star_count;
	int outer_a, inner_a, star_a;
	int outer_b, inner_b, star_b;
	int outer_c, inner_c, star_c;
	int page_count;
	int page;
	int i;

	star_count = 0;
	for (i = 0; i < 512; ++i)
		starhashtable[i] = 0;

	cursor = oldstarptr;
	oldstarptr = newstarptr;
	newstarptr = cursor;

	if (fullupdateflag)
		*oldstarptr = -1;

	/* Cube-origin eye-space coords = sum of (negated) eye-basis rows shifted. */
	base_x_a = -worldeyeA1;
	base_x_a -= worldeyeB1;
	base_x_a -= worldeyeC1;
	base_x_a >>= 2;
	origin_x = base_x_a;
	base_y_a = -worldeyeA2;
	base_y_a -= worldeyeB2;
	base_y_a -= worldeyeC2;
	base_y_a >>= 2;
	origin_y = base_y_a;
	base_z_a = -worldeyeA3;
	base_z_a -= worldeyeB3;
	base_z_a -= worldeyeC3;
	base_z_a >>= 2;
	origin_z = base_z_a;

	/* Lobe A: inner shiftA, outer shiftB. */
	star_a = 0;
	outer_a = 0;
	do {
		for (inner_a = 0; inner_a < 16; star_a += 2, inner_a += stardetaillevel) {
			uint8_t shade;
			uint8_t s;

			eye_x = base_x_a + shiftA1mul[inner_a];
			eye_y = base_y_a + shiftA2mul[inner_a];
			eye_z = base_z_a + shiftA3mul[inner_a];
			s = stars[star_a];
			eye_x += stareyex[s];
			eye_y += stareyey[s];
			eye_z += stareyez[s];
			/* Mirror camera space so z >= 0. */
			if (eye_z < 0) {
				eye_x = -eye_x;
				eye_y = -eye_y;
				eye_z = -eye_z;
			}
			if (eye_x < 0) {
				eye_x = -eye_x;
				if (eye_x > eye_z)
					continue;
				eye_x = (int32_t)(0u - math2_project_persp((uint32_t)eye_x, (uint32_t)eye_z));
			} else {
				if (eye_x > eye_z)
					continue;
				eye_x = (int32_t)math2_project_persp((uint32_t)eye_x, (uint32_t)eye_z);
			}
			if (eye_y < 0) {
				eye_y = -eye_y;
				if (eye_y > eye_z)
					continue;
				eye_y = (int32_t)(0u - math2_project_persp((uint32_t)eye_y, (uint32_t)eye_z));
			} else {
				if (eye_y > eye_z)
					continue;
				eye_y = (int32_t)math2_project_persp((uint32_t)eye_y, (uint32_t)eye_z);
			}
			eye_x += halfpixelswide;
			if (eye_x < 0 || eye_x >= (int)pixelswide)
				continue;
			eye_y = eye_y + halfpixelsdeep + transfm2_screenyoffset;
			if (eye_y < 0 || eye_y >= (int)pixelsdeep)
				continue;
			eye_x = eye_x + displaycorner_columns + lineaddressVGA[eye_y + displaycorner_lines];
			/* stars[] is stride-2: [2k]=index, [2k+1]=palette delta. */
			shade = (uint8_t)(stars[star_a + 1] + starcol1);
			if (shade > 3)
				shade = 3;
			star_colors[star_count] = (uint8_t)(shade + 0xFC);
			star_offsets[star_count] = eye_x;
			++star_count;
		}
		base_x_a = origin_x + shiftB1mul[outer_a];
		base_y_a = origin_y + shiftB2mul[outer_a];
		base_z_a = origin_z + shiftB3mul[outer_a];
		outer_a += stardetaillevel;
	} while (outer_a < 16);

	/* Lobe B: inner shiftA, outer shiftC. */
	star_b = 0;
	base_x_b = origin_x;
	base_y_b = origin_y;
	base_z_b = origin_z;
	outer_b = 0;
	do {
		for (inner_b = 0; inner_b < 16; star_b += 2, inner_b += stardetaillevel) {
			uint8_t shade;
			uint8_t s;

			eye_x = base_x_b + shiftA1mul[inner_b];
			eye_y = base_y_b + shiftA2mul[inner_b];
			eye_z = base_z_b + shiftA3mul[inner_b];
			s = stars[star_b];
			eye_x += stareyex[s];
			eye_y += stareyey[s];
			eye_z += stareyez[s];
			/* Mirror camera space so z >= 0. */
			if (eye_z < 0) {
				eye_x = -eye_x;
				eye_y = -eye_y;
				eye_z = -eye_z;
			}
			if (eye_x < 0) {
				eye_x = -eye_x;
				if (eye_x > eye_z)
					continue;
				eye_x = (int32_t)(0u - math2_project_persp((uint32_t)eye_x, (uint32_t)eye_z));
			} else {
				if (eye_x > eye_z)
					continue;
				eye_x = (int32_t)math2_project_persp((uint32_t)eye_x, (uint32_t)eye_z);
			}
			if (eye_y < 0) {
				eye_y = -eye_y;
				if (eye_y > eye_z)
					continue;
				eye_y = (int32_t)(0u - math2_project_persp((uint32_t)eye_y, (uint32_t)eye_z));
			} else {
				if (eye_y > eye_z)
					continue;
				eye_y = (int32_t)math2_project_persp((uint32_t)eye_y, (uint32_t)eye_z);
			}
			eye_x += halfpixelswide;
			if (eye_x < 0 || eye_x >= (int)pixelswide)
				continue;
			eye_y = eye_y + halfpixelsdeep + transfm2_screenyoffset;
			if (eye_y < 0 || eye_y >= (int)pixelsdeep)
				continue;
			eye_x = eye_x + displaycorner_columns + lineaddressVGA[eye_y + displaycorner_lines];
			/* stars[] is stride-2: [2k]=index, [2k+1]=palette delta. */
			shade = (uint8_t)(stars[star_b + 1] + starcol1);
			if (shade > 3)
				shade = 3;
			star_colors[star_count] = (uint8_t)(shade + 0xFC);
			star_offsets[star_count] = eye_x;
			++star_count;
		}
		base_x_b = origin_x + shiftC1mul[outer_b];
		base_y_b = origin_y + shiftC2mul[outer_b];
		base_z_b = origin_z + shiftC3mul[outer_b];
		outer_b += stardetaillevel;
	} while (outer_b < 16);

	/* Lobe C: inner shiftB, outer shiftC. */
	star_c = 0;
	base_x_c = origin_x;
	base_y_c = origin_y;
	base_z_c = origin_z;
	outer_c = 0;
	do {
		for (inner_c = 0; inner_c < 16; star_c += 2, inner_c += stardetaillevel) {
			uint8_t shade;
			uint8_t s;

			eye_x = base_x_c + shiftB1mul[inner_c];
			eye_y = base_y_c + shiftB2mul[inner_c];
			eye_z = base_z_c + shiftB3mul[inner_c];
			s = stars[star_c];
			eye_x += stareyex[s];
			eye_y += stareyey[s];
			eye_z += stareyez[s];
			/* Mirror camera space so z >= 0. */
			if (eye_z < 0) {
				eye_x = -eye_x;
				eye_y = -eye_y;
				eye_z = -eye_z;
			}
			if (eye_x < 0) {
				eye_x = -eye_x;
				if (eye_x > eye_z)
					continue;
				eye_x = (int32_t)(0u - math2_project_persp((uint32_t)eye_x, (uint32_t)eye_z));
			} else {
				if (eye_x > eye_z)
					continue;
				eye_x = (int32_t)math2_project_persp((uint32_t)eye_x, (uint32_t)eye_z);
			}
			if (eye_y < 0) {
				eye_y = -eye_y;
				if (eye_y > eye_z)
					continue;
				eye_y = (int32_t)(0u - math2_project_persp((uint32_t)eye_y, (uint32_t)eye_z));
			} else {
				if (eye_y > eye_z)
					continue;
				eye_y = (int32_t)math2_project_persp((uint32_t)eye_y, (uint32_t)eye_z);
			}
			eye_x += halfpixelswide;
			if (eye_x < 0 || eye_x >= (int)pixelswide)
				continue;
			eye_y = eye_y + halfpixelsdeep + transfm2_screenyoffset;
			if (eye_y < 0 || eye_y >= (int)pixelsdeep)
				continue;
			eye_x = eye_x + displaycorner_columns + lineaddressVGA[eye_y + displaycorner_lines];
			/* stars[] is stride-2: [2k]=index, [2k+1]=palette delta. */
			shade = (uint8_t)(stars[star_c + 1] + starcol1);
			if (shade > 3)
				shade = 3;
			star_colors[star_count] = (uint8_t)(shade + 0xFC);
			star_offsets[star_count] = eye_x;
			++star_count;
		}
		base_x_c = origin_x + shiftC1mul[outer_c];
		base_y_c = origin_y + shiftC2mul[outer_c];
		base_z_c = origin_z + shiftC3mul[outer_c];
		outer_c += stardetaillevel;
	} while (outer_c < 16);

	/* Paint pass. Banked SVGA (vgapointer at the real-mode VGA window)
	 * walks every VESA page and only paints the stars that land in it. */
	page_count = 1;
	if ((uint16_t)flightResolution != TIE_FLIGHT_RES_VGA && (uintptr_t)vgapointer == 0xA0000)
		page_count = screenYRes * screenMemWidth / vesa_page_size;
	for (page = 0; page < page_count; ++page) {
		if ((uint16_t)flightResolution != TIE_FLIGHT_RES_VGA && (uintptr_t)vgapointer == 0xA0000) {
			rtsvga2_SetCurrentPage(vesa_window, (uint16_t)page);
			rtsvga2_SetCurrentPage(1, (uint16_t)page);
		}
		for (i = 0; i < star_count; ++i) {
			uint32_t offset = star_offsets[i];
			int32_t key = star_offsets[i];
			uint8_t* dst;

			if ((uint16_t)flightResolution != TIE_FLIGHT_RES_VGA && (uintptr_t)vgapointer == 0xA0000) {
				if (offset / vesa_page_size != (uint32_t)page)
					continue;
				offset %= vesa_page_size;
			}
			dst = &vgapointer[offset];
			/* Only paint over deep space (don't overwrite the HUD). */
			if (*dst >= deepspacecolor) {
				int h;

				*dst = star_colors[i];
				/* Linear-probe into starhashtable (mask 0x1FF). */
				h = key;
				do {
					h = (h + 1) & 0x1FF;
				} while (starhashtable[h]);
				starhashtable[h] = key;
				*cursor = key;
				++cursor;
			}
		}
	}
	*cursor = -1;

	/* Erase pass: anything in the old list whose pixel is still a star
	 * colour and isn't found in this frame's hashtable gets overwritten
	 * with deepspacecolor. */
	cursor = oldstarptr;
	while (*cursor != -1) {
		uint32_t offset = *cursor;
		int32_t key = *cursor++;
		uint8_t* dst;

		if ((uint16_t)flightResolution != TIE_FLIGHT_RES_VGA && (uintptr_t)vgapointer == 0xA0000) {
			uint16_t erase_page = (uint16_t)(offset / vesa_page_size);

			offset %= vesa_page_size;
			rtsvga2_SetCurrentPage(vesa_window, erase_page);
			rtsvga2_SetCurrentPage(1, erase_page);
		}
		dst = &vgapointer[offset];
		if (*dst > deepspacecolor) {
			int h = key;

			for (;;) {
				int32_t entry;

				h = (h + 1) & 0x1FF;
				entry = starhashtable[h];
				if (entry == key)
					break;
				if (!entry) {
					*dst = deepspacecolor;
					break;
				}
			}
		}
	}
}

/* ------------------------------------------------------------------ */
/* rtsvga2_InvalidatePageCache (0x4E5C0)                              */
/* ------------------------------------------------------------------ */

/* Reset the lastpageA/B cache so the next rtsvga2_SetCurrentPage issues
 * the BIOS call even if the requested page matches the previously-cached
 * value. Called after pause/resume transitions that might have moved the
 * VESA window outside RTSVGA2's control. */
// FUNCTION: TIE95 0x4E5C0
void rtsvga2_InvalidatePageCache(void) {
	lastpageA = 0xFFFFFFFFu;
	lastpageB = 0xFFFFFFFFu;
}

/* ------------------------------------------------------------------ */
/* Cross-module globals (forward decls for locals we consume)         */
/* ------------------------------------------------------------------ */

/* tie.c owns the 256 packed {grid index, palette delta} star pairs and
 * the shared base palette slot consumed below. */

/* xtrans2.c owns the 512-entry star hash table. */

/* tie.c-ish: color cycle + blank bitmask (demo watdbg puts them in rts/tie). */

/* ------------------------------------------------------------------ */
/* rtsvga2_SetCurrentPage (0x4E5D4)                                   */
/* ------------------------------------------------------------------ */

/* Update the emulated VESA page cache; redundant requests are skipped. */
// FUNCTION: TIE95 0x4E5D4
void rtsvga2_SetCurrentPage(uint8_t window, uint16_t page) {
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
/* rtsvga2_SetVESAScanLineLength (0x4E644)                            */
/* ------------------------------------------------------------------ */

/* VESA logical scanline-length call retained for the weak platform hook. */
// FUNCTION: TIE95 0x4E644
void rtsvga2_SetVESAScanLineLength(uint32_t width_px) {
	uint32_t regs[7];
	regs[0] = 0x4F06;
	regs[1] = 0;        /* BL=0 = Set in pixels */
	regs[2] = width_px; /* ECX = new scanline length */
	regs[3] = 0;        /* EDX slot unused in retail (junk stack) */
	regs[4] = regs[5] = regs[6] = 0;
	int386(0x10, regs, regs);
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

	if (g_flight16bppBytesPerPixel == 2 && !g_starColor16Initialized) {
		const uint16_t gray_step = FrontendDisplay_GetPixelFormat555() ? 1057 : 2113;
		int i;

		for (i = 0; i < 3072; ++i)
			g_tie98StarColor16[i] = (uint16_t)(gray_step * ((math2_getrandom() & 0xF) + 8));
		g_starColor16Initialized = 1;
	} else if (g_flight16bppBytesPerPixel != 2 && (!g_starColor8Allocated || !g_starColor8Initialized)) {
		int i;

		for (i = 0; i < 3072; ++i) {
			const uint8_t brightness = (uint8_t)((math2_getrandom() & 0xF) + 8);
			uint8_t rgb[3];
			rgb[0] = brightness;
			rgb[1] = brightness;
			rgb[2] = brightness;
			g_tie98StarColor8[i] = (uint8_t)rtsvga2_findNearestColor(rgb, rtsvga2_vgapalette, 0x40, 0x100);
		}
		g_starColor8Initialized = 1;
		g_starColor8Allocated = 1;
	}

	if (!g_starPositionsInitialized) {
		int i;

		for (i = 0; i < 3072; ++i) {
			uint16_t index;
			do {
				index = (uint16_t)math2_getrandom() & 0x7F;
			} while (index > 124);
			g_tie98StarPositionIndex[i] = (uint8_t)index;
		}
		g_starPositionsInitialized = 1;
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
				const uint8_t position_index = g_tie98StarPositionIndex[star_index];
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
								*destination16 = g_tie98StarColor16[star_index];
						} else if (*destination == deepspacecolor) {
							*destination = g_tie98StarColor8[star_index];
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

// FUNCTION: TIE95 0x4E670
int rtsvga2_takeScreenshot(void) {
	/* Retail: rtsvga2_saveboxVGA(newbuf, 0, 0, screenXRes, screenYRes), then
	 * Save_PCX_Screenshot(newbuf, screenXRes, screenYRes, rtsvga2_vgapalette).
	 * The PCX writer is an unrecovered library routine; capture stays
	 * disabled until it is recovered. */
	return 0;
}
