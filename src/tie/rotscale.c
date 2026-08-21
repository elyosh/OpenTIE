#include <stdint.h>
#include <string.h>

#include "tie/rotscale.h"
#include "tie_runtime/audio/config.h"
#include "tie_runtime/diagnostics/diagnostics.h"
#include "tie_runtime/display/classic_display.h"
#include "tie_runtime/display/classic_framebuffer.h"
#include "tie_runtime/flight_assets/model_types.h"
#include "tie_runtime/input/input.h"
#include "tie_runtime/runtime/exports.h"
#include "tie_runtime/runtime/profile.h"
#include "tie_runtime/storage/storage.h"

#include "tie/drawpol.h" /* worldx, worldy, parentobject, objectnum,
                        * layervalue, flatobjnum                */
#include "tie/flight_surface_tie98.h"
#include "tie/logbuf2.h" /* buffer_ptr, pixelswide(min1), pixelsdeep(min1) */
#include "tie/render_scene_tie98.h"
#include "tie/rtsvga2.h" /* rtsvga2_vgapalette                            */
#include "tie/tie.h"     /* bytesPerPixel, worldz, yAspect                */
#include "tie/trace2.h"  /* TRACE2 edge pool + cursors                    */
#include "tie/trig2.h"   /* trig2_getsine                                 */
#include "tie/xtrans2.h" /* flatcolors / flatcomponentnum / flatparentobj
                        * / flatx / flaty / flatz                       */

/* Debug counters (file-local). Sampled by an instrumented sxform clip
 * path; printed externally from a debugger. Zero by default. */
static int g_dbg_sx_calls;
static int g_dbg_sx_early_ymax;
static int g_dbg_sx_early_ymin;
static int g_dbg_sx_early_xmax;
static int g_dbg_sx_early_xmin;
static int g_dbg_sx_completed;
static int32_t g_dbg_sx_last_ymin, g_dbg_sx_last_ymax;
static int32_t g_dbg_sx_last_xmin, g_dbg_sx_last_xmax;

/* --- module-owned globals ---------------------------------------- */

/* Per-aspect tangent ramps used by buildlinedata. Imported verbatim
 * from the Watcom binary (offsets 0xd67e0 / 0xd68f2 / 0xd6a04). The
 * 91/100/110 suffixes name the aspect-correction rates: 100 = square
 * pixels, 91 / 110 = the two non-square halves around the diagonal.
 *
 * Each ramp is a one-quadrant sin lookup wrapping into the negative
 * half (the table doubles as a tangent step accumulator). All three
 * tables share the same fold-over structure: monotonic positive ramp
 * to ~+32K near index 82 (tangent091/100) or 70 (tangent110), then
 * monotonic descent through the negative range back to 0 (or -1) at
 * the last index. */
// GLOBAL: TIE 0xC7564
static const int16_t tangent091[137] = {
	0,      366,    731,    1097,   1463,   1828,   2194,   2561,   2927,   3293,   3660,   4027,   4395,
	4762,   5131,   5499,   5868,   6237,   6607,   6977,   7348,   7720,   8092,   8464,   8838,   9212,
	9586,   9962,   10338,  10715,  11093,  11471,  11851,  12231,  12613,  12995,  13379,  13763,  14149,
	14536,  14924,  15313,  15703,  16095,  16488,  16882,  17277,  17674,  18073,  18473,  18874,  19277,
	19682,  20088,  20496,  20906,  21317,  21731,  22146,  22563,  22982,  23403,  23826,  24251,  24678,
	25107,  25539,  25973,  26409,  26848,  27289,  27732,  28178,  28627,  29078,  29532,  29989,  30449,
	30911,  31377,  31845,  32317,  -32745, -32267, -31785, -31301, -30813, -30321, -29826, -29327, -28825,
	-28319, -27809, -27294, -26776, -26254, -25727, -25196, -24661, -24121, -23576, -23027, -22473, -21914,
	-21350, -20781, -20206, -19626, -19041, -18450, -17853, -17250, -16641, -16027, -15405, -14778, -14144,
	-13503, -12855, -12200, -11537, -10868, -10191, -9506,  -8813,  -8112,  -7402,  -6684,  -5958,  -5222,
	-4477,  -3723,  -2959,  -2185,  -1401,  -607,   -1,
};

// GLOBAL: TIE 0xC7676
static const int16_t tangent100[137] = {
	0,      402,    804,    1206,   1608,   2011,   2414,   2817,   2927,   3293,   3660,   4027,   4395,
	4762,   5131,   5499,   5868,   6237,   6607,   6977,   7348,   7720,   8092,   8464,   8838,   9212,
	9586,   9962,   10338,  10715,  11093,  11471,  11851,  12231,  12613,  12995,  13379,  13763,  14149,
	14536,  14924,  15313,  15703,  16095,  16488,  16882,  17277,  17674,  18073,  18473,  18874,  19277,
	19682,  20088,  20496,  20906,  21317,  21731,  22146,  22563,  22982,  23403,  23826,  24251,  24678,
	25107,  25539,  25973,  26409,  26848,  27289,  27732,  28178,  28627,  29078,  29532,  29989,  30449,
	30911,  31377,  31845,  32317,  -32745, -32267, -31785, -31301, -30813, -30321, -29826, -29327, -28825,
	-28319, -27809, -27294, -26776, -26254, -25727, -25196, -24661, -24121, -23576, -23027, -22473, -21914,
	-21350, -20781, -20206, -19626, -19041, -18450, -17853, -17250, -16641, -16027, -15405, -14778, -14144,
	-13503, -12855, -12200, -11537, -10868, -10191, -9506,  -8813,  -8112,  -7402,  -6684,  -5958,  -5222,
	-4477,  -3723,  -2959,  -2185,  -1401,  -607,   -1,
};

// GLOBAL: TIE 0xC7788
static const int16_t tangent110[122] = {
	0,      442,    885,    1327,   1770,   2212,   2655,   3098,   3542,   3985,   4429,   4873,   5318,
	5763,   6208,   6654,   7100,   7547,   7995,   8443,   8891,   9341,   9791,   10242,  10693,  11146,
	11599,  12054,  12509,  12965,  13422,  13880,  14340,  14800,  15261,  15724,  16188,  16654,  17120,
	17588,  18058,  18528,  19001,  19474,  19950,  20427,  20906,  21386,  21868,  22352,  22838,  23326,
	23815,  24307,  24800,  25296,  25794,  26294,  26796,  27301,  27808,  28317,  28829,  29344,  29860,
	30380,  30902,  31427,  31955,  32486,  -32517, -31980, -31440, -30897, -30351, -29802, -29249, -28693,
	-28133, -27570, -27003, -26433, -25858, -25280, -24698, -24111, -23521, -22926, -22327, -21724, -21116,
	-20503, -19886, -19264, -18637, -18004, -17367, -16725, -16077, -15424, -14765, -14100, -13430, -12753,
	-12071, -11382, -10687, -9985,  -9277,  -8562,  -7839,  -7110,  -6374,  -5630,  -4878,  -4119,  -3351,
	-2575,  -1792,  -999,   -198,   -1,
};

// GLOBAL: TIE 0xC7898
uint16_t reverseflag; /* 1 = horizontal-flip sprite */
// GLOBAL: TIE 0xC7880
uint16_t bSquarePixels; /* set in preparefastdraw / scalesetup */

/* paletteconvert state populated by preparecolor.
 *
 * Retail uses a 6-bit color index (0..0x3F) in 8-bit mode; the demo used
 * only a 4-bit index (0..0x0F). We size the tables to 64 slots to match
 * retail's maximum. The tables are indexed by the raw sprite byte that
 * ROTSCALE_rotatescale wrote into buffer_ptr, then by the edition-specific
 * completion scan, which checks it against the transparency threshold
 * (0x40 in retail) and remaps to a physical palette index via
 * paletteconvert[] (8bpp) or
 * paletteconvertlo/hi[] (16bpp). */
static uint8_t paletteconverthi[64];
static uint8_t paletteconvertlo[64];
static uint8_t paletteconvert[64];

/* RLE decode scratch: rotatescale buffers up to ~512 spans per row. */
typedef struct rotscale_run {
	uint32_t x_start;
	uint32_t color;
	uint32_t run_len;
} rotscale_run;
static rotscale_run runtable[512]; /* 6144 bytes per watdbg */

/*
 * Retail variable bit-split tables for the default RLE opcode.
 * A sprite byte carries (color_top_N_bits):(length_bottom_M_bits) where
 * M = image_hdr[+0x20] (the bit_split argument threaded through
 * ROTSCALE_rotatescale from ROTSCALE_rotatescaleimage) and N = 8 - M.
 * The demo used a fixed 4:4 split; retail bitmaps pick per sprite.
 *
 *   run_length = rotscale_run_mask[bit_split] & op
 *   color_hi   = op >> rotscale_run_shift[bit_split]
 *
 * Mirrors byte_C7884 / byte_C788D in Z_TIE__.EXE (0xC7884/0xC788D).
 */
static const uint8_t rotscale_run_mask[16] = {
	0x00, 0x01, 0x03, 0x07, 0x0F, 0x1F, 0x3F, 0x7F, 0xFF, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
};
static const uint8_t rotscale_run_shift[16] = {
	0, 1, 2, 3, 4, 5, 6, 7, 8, 0, 1, 2, 3, 4, 5, 6,
};

/* Draw-buffer state (set by preparefastdraw). */
// GLOBAL: TIE 0xD797C
static uint8_t* pDrawBuffer; /* base of current draw target */
// GLOBAL: TIE 0xDB848
static int32_t nDrawBufferMemoryWidth; /* bytes per scanline */
// GLOBAL: TIE 0xDBC7E
static int16_t nDrawBufferWidth;
// GLOBAL: TIE 0xDBC80
static int16_t nDrawBufferDepth;
// GLOBAL: TIE 0xDBC82
static int16_t nDrawBufferWidthMin1;
// GLOBAL: TIE 0xDBC88
static int16_t nDrawBufferDepthMin1;
// GLOBAL: TIE 0xDBC8E
static int16_t nDrawBufferOrientation;
// GLOBAL: TIE 0xDBC90
static int16_t nDiagonalAngle;

/* Per-sprite cel state (refreshed each rotatescaleimage call). */
// GLOBAL: TIE 0xDBC8A
static int16_t celoffsetx;
// GLOBAL: TIE 0xDBC8C
static int16_t celoffsety;

/* Per-row scan state (mutated by setstartcase / updatecase handlers) */
// GLOBAL: TIE 0xDBC58
static int16_t perpendflag;
// GLOBAL: TIE 0xDBC64
static uint16_t perpendfrac;
// GLOBAL: TIE 0xDBC5A
static int16_t startdrawpoint;
// GLOBAL: TIE 0xDBC5C
static int16_t firstvispoint;
// GLOBAL: TIE 0xDBC7C
static int16_t lastvispoint;
// GLOBAL: TIE 0xDBC66
static int16_t firstxincoffset;
static int16_t lastxincoffset;
// GLOBAL: TIE 0xDBC6E
static int16_t firstyincoffset;
// GLOBAL: TIE 0xDBC78
static int16_t lastyincoffset;
// GLOBAL: TIE 0xDBC76
static int16_t linestartx;
// GLOBAL: TIE 0xDBC74
static int16_t linestarty;
// GLOBAL: TIE 0xDBC6A
static int16_t lineendx;
// GLOBAL: TIE 0xDBC6C
static int16_t lineendy;
// GLOBAL: TIE 0xDBC70
static int16_t plotx;
// GLOBAL: TIE 0xDBC72
static int16_t ploty;
// GLOBAL: TIE 0xDBC86
static int16_t adjustplotx;
// GLOBAL: TIE 0xDBC84
static int16_t adjustploty;

/* --- LineData (per-angle precomputed rotation tables) ----------- */

/*
 * Per-angle rotation/scan cache. preparefastdraw rebuilds when angle
 * changes (cached_angle at offset 0 is the cache key).
 *
 * dda[] is interleaved (x-count, y-count) WORD pairs - one per scan
 * column. Each octant handler interprets the indexing differently.
 *
 * The +9652 'current_row_base' slot doubles as the per-row pDrawBuffer
 * pointer that rotatescale advances each row.
 *
 * Total size 16060 bytes per watdbg.
 */
typedef struct rotscale_line_data {
	uint16_t cached_angle;       /* +0    cache key */
	uint16_t sin_quad;           /* +2    sin(angle in first quadrant) */
	uint16_t y_flip;             /* +4    angle & 0x8000 */
	uint16_t cos_quad;           /* +6    cos = sin(angle + 0x4000) */
	uint16_t x_quad_marker;      /* +8 */
	uint16_t perp_sin;           /* +10   perpendicular sin */
	uint16_t inv_perp_sin;       /* +12   1/perp_sin (aspect-corrected) */
	uint16_t scan_count;         /* +14   width or depth */
	uint16_t x_flip;             /* +16   X-flip flag */
	uint16_t major_axis_flag;    /* +18   0 or 2 */
	uint16_t packed_octant_a;    /* +20 */
	uint16_t octant_case;        /* +22   0..7 dispatch byte */
	uint16_t case_type;          /* +24   0 or 4 */
	uint16_t perp_case_type;     /* +26 */
	uint16_t perp_update_inc;    /* +28 */
	uint16_t perp_flag;          /* +30 */
	uint16_t linestart_x_init;   /* +32 */
	uint16_t lineend_x_init;     /* +34 */
	uint16_t linestart_y_inv;    /* +36 */
	uint16_t lineend_y_inv;      /* +38 */
	uint16_t linestart_y_init;   /* +40 */
	uint16_t lineend_y_init;     /* +42 */
	uint16_t dx_abs;             /* +44 */
	uint16_t dy_abs;             /* +46 */
	uint16_t dda[3200];          /* +48..+6448 */
	uint16_t perpfrac_inc;       /* +6448 */
	uint16_t num_run_lengths;    /* +6450 */
	uint16_t run_lengths[1600];  /* +6452..+9652 */
	uint8_t* current_row_base;   /* +9652 walking pDrawBuffer for this row */
	int32_t screen_offset[1600]; /* +9656..+16056 per-column pDrawBuffer delta */
	int32_t row_step;            /* +16056 ±nDrawBufferMemoryWidth */
} rotscale_line_data;

static rotscale_line_data line_data_storage;
// GLOBAL: TIE 0xDB844
static rotscale_line_data* pCurrentLine;

/* Cache-validity flag. The binary uses dword_C787C for the same gate;
 * tie_simulator clears it on mission start to force a rebuild on the
 * next preparefastdraw call. (Owned by rotscale.c per watdbg.) */
// GLOBAL: TIE 0xC787C
int rotscale_linedata_built;

/* --- ScaleData (per-scale lookup tables) ------------------------ */

typedef struct rotscale_scale_data {
	uint16_t scale_req;        /* +0    input scale request */
	uint16_t prev_eff_scale_x; /* +2    stale-check: prior frame's eff_scale_x */
	uint16_t eff_scale_x;      /* +4    effective per-axis scale (8.8) */
	uint16_t eff_scale_y;      /* +6 */
	uint16_t x_lookup_lo[256]; /* +8..+520   linear accumulator low word */
	uint16_t x_lookup_hi[256]; /* +520..+1032 same accumulator high word */
	uint16_t aspect_x;         /* +1032 */
	uint16_t aspect_y;         /* +1034 */
} rotscale_scale_data;

static rotscale_scale_data ScaleData;

/* ===================================================================
 * Leaf helpers
 * ================================================================ */

/*
 * GetUpdateIncrement: per-angle tangent lookup. pos is shifted right
 * 6 bits before indexing (the binary packs angles into a wider range
 * before passing here).
 */
static int16_t GetUpdateIncrement(uint16_t pos, int16_t rate) {
	uint16_t idx = (uint16_t)(pos >> 6);
	if (rate == 91)
		return tangent091[idx];
	if (rate == 110)
		return tangent110[idx];
	return tangent100[idx];
}

/*
 * rotscale_calcscale: clamp((factor * bound_hwidth / (|depth|>>8)) >> 8, 1024).
 * Near objects -> max clamp 1024. Far objects scale down.
 */
// FUNCTION: TIE 0x4B8A4
int16_t rotscale_calcscale(int32_t depth, uint16_t bound_hwidth, uint16_t factor) {
	int32_t abs_depth = depth < 0 ? -depth : depth;
	int32_t ratio = abs_depth >> 8;
	if (ratio)
		ratio = bound_hwidth / ratio;
	int32_t scaled = (factor * ratio) >> 8;
	if (scaled > 1024)
		return 1024;
	return (int16_t)scaled;
}

/*
 * preparecolor: rebuild per-sprite 16-entry palette LUT.
 *
 * palette_entries points to a retail sprite sub-image containing a variable-
 * length color table.
 *
 * TIE95 stores one VGA palette index per entry and converts it for its
 * 16-bit path. TIE98's RGB sprite loader has already converted each entry
 * to the active surface format, so its 16-bit path copies two bytes per
 * entry unchanged.
 */
void rotscale_prepare_color(const char* palette_entries) {
	/* Both retail implementations read:
	 *   v1 = *(int32_t *)(a1 + 40)           -- entry count
	 *   v2 = a1 + *(int32_t *)(a1 + 12)      -- source (offset inside a1)
	 * and write v1 entries from v2 into the conversion tables. */
	const uint8_t* hdr = (const uint8_t*)palette_entries;
	int32_t count = *(const int32_t*)(hdr + 40);
	if (count <= 0)
		return;
	if (count > 64)
		count = 64; /* clamp to table size */
	const uint8_t* src = hdr + *(const int32_t*)(hdr + 12);

	if (TieProfile_UsesTie98Logic()) {
		if (g_flight16bppBytesPerPixel == 2) {
			for (int k = 0; k < count; ++k) {
				paletteconvertlo[k] = *src++;
				paletteconverthi[k] = *src++;
			}
		} else {
			for (int k = 0; k < count; ++k)
				paletteconvert[k] = *src++;
		}
		return;
	}

	if (bytesPerPixel == 2) {
		/* Retail asm (0x48e94..0x48eca) packs rgb555-ish via partial-
		 * register arithmetic:
		 *   ecx  = (R>>1) << 5                           (bits 5..11)
		 *   ah   = ((B>>1) << 2) & 0xFF                  (bits 2..7 of hi byte)
		 *   al   = G>>1                                  (bits 0..6)
		 *   packed = ecx | (ah << 8) | al
		 * One sprite byte per entry: it serves as BOTH the stored
		 * index (paletteconvert[k]) AND the vgapalette lookup key. */
		for (int k = 0; k < count; ++k) {
			uint8_t idx = *src++;
			const uint8_t* rgb = &rtsvga2_vgapalette[3 * idx];
			uint8_t ah_val = (uint8_t)(((uint8_t)(rgb[2] >> 1)) << 2);
			uint32_t ecx_v = (uint32_t)(rgb[0] >> 1) << 5;
			uint16_t eax16 = (uint16_t)(((uint16_t)ah_val << 8) | (rgb[1] >> 1));
			uint16_t packed = (uint16_t)((ecx_v | eax16) & 0xFFFF);
			paletteconvert[k] = idx;
			paletteconvertlo[k] = (uint8_t)(packed & 0xFF);
			paletteconverthi[k] = (uint8_t)(packed >> 8);
		}
	} else {
		for (int k = 0; k < count; ++k)
			paletteconvert[k] = *src++;
	}
}

/* ===================================================================
 * scalesetup - rebuild ScaleData lookup tables
 * ================================================================ */
static void scalesetup(uint16_t scale, rotscale_line_data* line_data, rotscale_scale_data* sd) {
	sd->scale_req = scale;
	if (bSquarePixels == 1) {
		sd->aspect_x = 256;
		sd->aspect_y = 256;
		nDiagonalAngle = 0x2000;
	} else {
		sd->aspect_x = 233;
		sd->aspect_y = 282;
		nDiagonalAngle = 8704;
	}

	/* Effective X scale = sin_quad * scale, optionally aspect-adjusted */
	uint32_t sx = ((uint32_t)line_data->perp_sin * (uint32_t)scale) >> 16;
	sd->eff_scale_x = (uint16_t)sx;
	if (line_data->case_type)
		sd->eff_scale_x = (uint16_t)((sd->aspect_x * sx) >> 8);

	/* Effective Y scale = X scale * (1 + perpfrac_inc), optionally aspected */
	uint32_t sy = (sx * line_data->perpfrac_inc) >> 8;
	sy += sx;
	if (!line_data->perp_case_type)
		sy = (sd->aspect_y * sy) >> 8;
	sd->eff_scale_y = (uint16_t)sy;

	/* Stale-check: regen iff eff_scale_x word differs from the prior
	 * frame's. eff_scale_y is not used as a stale-check input — the
	 * lookup table is keyed only on eff_scale_x. */
	if (sd->eff_scale_x == sd->prev_eff_scale_x)
		return;
	sd->prev_eff_scale_x = sd->eff_scale_x;

	/* Fill 256-entry linear accumulator: lookup[k] = (eff_scale_x * 8 * k) */
	uint32_t step = (uint32_t)sd->eff_scale_x << 8;
	uint32_t acc = step;
	for (int k = 0; k < 256; ++k) {
		sd->x_lookup_lo[k] = (uint16_t)(acc & 0xFFFF);
		sd->x_lookup_hi[k] = (uint16_t)(acc >> 16);
		acc += step;
	}
}

/* ===================================================================
 * adjustoffsets - rotate (celoffsetx, celoffsety) into screen space
 *
 * Reads sin/cos and quadrant-sign bits from line_data, applies a
 * scaled 2D rotation matrix, and stores the resulting screen-space
 * delta into adjustplotx, adjustploty.
 *
 * Called 4 times per sprite, once per quadrant; rotatescaleimage
 * flips celoffset signs between calls to walk all 4 corners.
 * ================================================================ */
static void adjustoffsets(rotscale_line_data* line_data, rotscale_scale_data* sd) {
	/* Snapshot the pre-abs sign bits */
	int16_t saved_cox_sign = celoffsetx;
	int16_t saved_coy_sign = celoffsety;

	if (celoffsetx < 0)
		celoffsetx = (int16_t)-celoffsetx;
	if (celoffsety < 0)
		celoffsety = (int16_t)-celoffsety;

	uint16_t cx_scaled = (uint16_t)(((uint32_t)celoffsetx * sd->scale_req + 128) >> 8);
	/* Y is multiplied by aspect_y in addition to the scale. */
	uint32_t cy_aspect = ((uint32_t)sd->aspect_y * (uint32_t)celoffsety + 128) >> 8;
	uint16_t cy_scaled = (uint16_t)((sd->scale_req * cy_aspect + 128) >> 8);

	/* Read packed rotation vector from line_data: WORD index 1..4
	 * = sin_quad, y_flip, cos_quad, x_quad_marker. */
	uint16_t* rot_vec = &line_data->cached_angle;

	/* X output: cos_x + sin_y, with quadrant sign flips */
	int32_t cos_x = (int32_t)rot_vec[3] * (int32_t)(uint32_t)cx_scaled;
	if (((((uint16_t)(saved_cox_sign ^ rot_vec[4])) >> 8) & 0x80u) != 0)
		cos_x = -cos_x;
	int32_t sin_y = (int32_t)rot_vec[1] * (int32_t)(uint32_t)cy_scaled;
	if (((((uint16_t)(rot_vec[2] ^ saved_coy_sign)) >> 8) & 0x80u) != 0)
		sin_y = -sin_y;
	adjustplotx = (int16_t)((uint32_t)(sin_y + cos_x + 0x8000) >> 16);

	/* Y output: -cos_y + sin_x (note opposite sign flip on cos_y) */
	int32_t sin_x = (int32_t)rot_vec[1] * (int32_t)(uint32_t)cx_scaled;
	if (((((uint16_t)(saved_cox_sign ^ rot_vec[2])) >> 8) & 0x80u) != 0)
		sin_x = -sin_x;
	int32_t cos_y = (int32_t)rot_vec[3] * (int32_t)(uint32_t)cy_scaled;
	if (((((uint16_t)(rot_vec[4] ^ saved_coy_sign)) >> 8) & 0x80u) == 0)
		cos_y = -cos_y;
	int32_t y_unaspected = (int32_t)((uint32_t)(cos_y + sin_x + 0x8000) >> 16);
	int16_t y_sign = (int16_t)y_unaspected;
	if (y_sign < 0)
		y_unaspected = -y_unaspected;
	int32_t result = (y_unaspected * sd->aspect_x + 128) >> 8;
	if (y_sign < 0)
		result = -result;
	adjustploty = (int16_t)result;
}

/* ===================================================================
 * buildlinedata - precompute per-angle rotation/DDA tables.
 * Called by preparefastdraw when the cached angle differs.
 * ================================================================ */
static void buildlinedata(uint16_t angle, rotscale_line_data* line_data) {
	int32_t saved_mem_width = nDrawBufferMemoryWidth;

	line_data->cached_angle = angle;
	line_data->y_flip = (uint16_t)(angle & 0x8000);
	/* x_quad_marker = ((angle >> 8) + 64) & 0x80 ... <<8 */
	line_data->x_quad_marker = (uint16_t)((((uint8_t)(angle >> 8) + 64) & 0x80) << 8);

	uint16_t angle_lo = angle;
	uint16_t x_flip_flag;
	uint16_t x_quad;
	if (angle < 0x8000u) {
		x_flip_flag = 0;
		x_quad = (angle < 0x4000u) ? 0 : 2;
	} else {
		angle_lo = (uint16_t)(angle & 0x7FFFu);
		x_flip_flag = 1;
		x_quad = (angle_lo < 0x4000u) ? 2 : 0;
	}
	line_data->x_flip = x_flip_flag;
	line_data->major_axis_flag = x_quad;

	/* Fold angle into the first quadrant for trig lookup */
	int32_t quad_angle = (angle_lo < 0x4000u) ? angle_lo : (0x8000 - angle_lo);
	nDrawBufferMemoryWidth = saved_mem_width;
	line_data->sin_quad = trig2_getsine((uint16_t)quad_angle);
	line_data->cos_quad = trig2_getsine((uint16_t)(quad_angle + 0x4000));

	int32_t perp_quad = quad_angle;
	uint16_t case_type;
	int16_t tan_rate;
	uint16_t tan_arg;
	if ((uint16_t)quad_angle >= (uint16_t)nDiagonalAngle) {
		case_type = 4;
		line_data->case_type = 4;
		perp_quad = 0x4000 - perp_quad;
		tan_rate = (bSquarePixels == 1) ? 100 : 110;
		tan_arg = (uint16_t)perp_quad;
	} else {
		case_type = 0;
		line_data->case_type = 0;
		tan_rate = (bSquarePixels == 1) ? 100 : 91;
		tan_arg = (uint16_t)quad_angle;
	}
	int16_t tan_inc = GetUpdateIncrement(tan_arg, tan_rate);

	/* perpendicular sin */
	perp_quad = (perp_quad & 0xFFFF00FFu) | ((((perp_quad >> 8) + 64) & 0xFFu) << 8);
	uint16_t perp_sin = trig2_getsine((uint16_t)perp_quad);
	line_data->perp_sin = perp_sin;

	/* 1/perp_sin, optionally aspect-corrected when on the cos-major case */
	uint32_t inv_perp = (perp_sin == 0) ? 0u : (0x80000000u / perp_sin);
	if (case_type) {
		if (!bSquarePixels) {
			uint32_t lo = inv_perp & 0xFFFFu;
			inv_perp = ((uint32_t)yAspect * lo + 0x8000u) >> 16;
		}
	}
	line_data->inv_perp_sin = (uint16_t)inv_perp;

	/* Build DDA accumulator: each scan step adds tan_inc to a 16-bit
	 * fractional counter; on rollover, advance the integer counter. */
	line_data->dda[0] = 0;
	line_data->dda[1] = 0;
	int32_t saved_mem_w_2 = nDrawBufferMemoryWidth;
	uint16_t case_type2 = line_data->case_type;
	uint32_t frac = 0x8000;

	if (case_type2) {
		/* Y-major: scan_count = depth */
		line_data->scan_count = nDrawBufferDepth;
		uint16_t y_acc = 0, x_acc = 0;
		int n = nDrawBufferDepth - 1;
		int idx = 1;
		while (--n != -1) {
			uint16_t prev = (uint16_t)frac;
			frac += (uint16_t)tan_inc;
			++x_acc;
			if ((uint16_t)frac < prev)
				++y_acc;
			line_data->dda[2 * idx] = y_acc;
			line_data->dda[2 * idx + 1] = x_acc;
			++idx;
		}
	} else {
		/* X-major: scan_count = width */
		line_data->scan_count = nDrawBufferWidth;
		uint16_t y_acc = 0, x_acc = 0;
		int n = nDrawBufferWidth - 1;
		int idx = 1;
		while (--n != -1) {
			uint16_t prev = (uint16_t)frac;
			frac += (uint16_t)tan_inc;
			++x_acc;
			if ((uint16_t)frac < prev)
				++y_acc;
			line_data->dda[2 * idx] = x_acc;
			line_data->dda[2 * idx + 1] = y_acc;
			++idx;
		}
	}

	/*
	 * Run-length compress the carry pattern used by lineendy clamp logic.
	 *
	 * Disasm 0x46e7f-0x46ee0: starts at dda[dy_bit] (1 for X-major
	 * case_type=0, 0 for Y-major case_type=4) and steps by 2 dda WORDs
	 * per inner iteration. Each run records how many consecutive dda
	 * pairs share the same y-count (or x-count) value.
	 *
	 * The original indexes line_data[24+v24] (absolute WORD index) which
	 * is the same as dda[v24] in this struct.
	 */
	int dy_bit = (line_data->case_type == 0) ? 1 : 0;
	uint16_t run_count = 0;
	int idx = dy_bit;
	int remaining = line_data->scan_count;
	while (remaining) {
		uint16_t run_len = 1;
		uint16_t first_v = line_data->dda[idx];
		while (--remaining && first_v == line_data->dda[idx + 2]) {
			++run_len;
			idx += 2;
		}
		idx += 2;
		line_data->run_lengths[run_count] = run_len;
		++run_count;
	}
	line_data->num_run_lengths = run_count;

	/* Perpendicular case selection (mirror of the major-case branch above) */
	int32_t perp_angle = angle;
	perp_angle = (perp_angle & 0xFFFF00FFu) | ((((perp_angle >> 8) + 64) & 0x7Fu) << 8);
	if ((uint16_t)perp_angle >= 0x4000u) {
		perp_angle = (perp_angle & 0xFFFF00FFu) | ((((perp_angle >> 8) + 0x80) & 0xFFu) << 8);
		perp_angle = -perp_angle;
	}

	uint16_t perp_case_type;
	int16_t perp_tan_rate;
	if ((uint16_t)perp_angle >= (uint16_t)nDiagonalAngle) {
		perp_case_type = 4;
		nDrawBufferMemoryWidth = saved_mem_w_2;
		perp_angle = 0x4000 - perp_angle;
		line_data->perp_case_type = 4;
		perp_tan_rate = (bSquarePixels == 1) ? 100 : 110;
	} else {
		perp_case_type = 0;
		nDrawBufferMemoryWidth = saved_mem_w_2;
		line_data->perp_case_type = 0;
		perp_tan_rate = (bSquarePixels == 1) ? 100 : 91;
	}
	line_data->perp_update_inc = GetUpdateIncrement((uint16_t)perp_angle, perp_tan_rate);
	int32_t saved_mem_w_3 = nDrawBufferMemoryWidth;
	line_data->perp_flag = 0;

	/*
	 * perpfrac_inc selection (verified against disasm 0x46f8b-0x47033).
	 *
	 * Default branch (case_type_saved != perp_case_type, OR bSquarePixels
	 * set) and the 256 branch both produce a 32-bit product whose top
	 * byte is what gets stored: result = (32-bit product) >> 24.
	 *
	 * The inverse-tangent branch is the special case used for nearly-
	 * vertical lines (angle near 0x4000 / 0xC000). It computes
	 *   inv16 = (uint16_t)(0x1000000 / perp_update_inc)
	 * and writes the 32-bit value (inv16 << 8) as a DWORD covering
	 * [perp_update_inc, perp_flag] (low/high halves), then sets
	 * perpfrac_inc = (uint16_t)tan_inc >> 8 (matching the SAR EAX,8
	 * after the zero-extending MOV AX, var_1C).
	 */
	uint32_t perp_frac_inc;
	if (case_type2 != perp_case_type || bSquarePixels) {
		uint32_t prod = (uint32_t)line_data->perp_update_inc * (uint32_t)(uint16_t)tan_inc + 0x800000u;
		perp_frac_inc = prod >> 24;
	} else {
		uint16_t a = line_data->cached_angle;
		if ((a >= 0xA000u && a < 0xE000u) || (a >= 0x2000u && a < 0x6000u)) {
			/* Inverse-tangent path: write (inv16 << 8) as a DWORD into
			 * the contiguous (perp_update_inc, perp_flag) word pair. */
			uint16_t inv16 = (uint16_t)(0x1000000u / line_data->perp_update_inc);
			uint32_t shifted = (uint32_t)inv16 << 8;
			line_data->perp_update_inc = (uint16_t)(shifted & 0xFFFFu);
			line_data->perp_flag = (uint16_t)(shifted >> 16);
			perp_frac_inc = (uint32_t)(uint16_t)tan_inc >> 8;
		} else {
			/* 256 path */
			line_data->perp_update_inc = 256;
			line_data->perp_flag = 256;
			uint32_t prod = 55296u * (uint32_t)(uint16_t)tan_inc;
			perp_frac_inc = prod >> 24;
		}
	}
	line_data->perpfrac_inc = (uint16_t)perp_frac_inc;

	/* Pack octant case index */
	uint16_t y_flip_b = line_data->major_axis_flag;
	line_data->octant_case = (uint16_t)(y_flip_b | line_data->x_flip | line_data->case_type);
	line_data->packed_octant_a = (uint16_t)((y_flip_b >> 1) + line_data->x_flip);

	/* Initialize line endpoints from the dda extremes. The dda loop
	 * writes scan_count pairs at indices 0..2*(scan_count-1)+1; the
	 * lineend pair is the LAST written entry, not one past it. The
	 * original reads `a2[2*scan_count + 22]` (= dda[2*(scan_count-1)]
	 * in this struct's indexing) — keep that semantic. Reading
	 * dda[2*scan_count] yields stale/zero data, which propagates
	 * dx_abs/dy_abs = 0, defeats setstartcase's column clip, and
	 * lets the 8-bit fast path write past the framebuffer end. */
	line_data->linestart_x_init = line_data->dda[0];
	uint16_t y_first = line_data->dda[1];
	line_data->linestart_y_init = y_first;
	line_data->linestart_y_inv = (uint16_t)(nDrawBufferDepthMin1 - y_first);
	int last_pair = 2 * (line_data->scan_count - 1);
	line_data->lineend_x_init = line_data->dda[last_pair];
	line_data->lineend_y_init = line_data->dda[last_pair + 1];
	line_data->lineend_y_inv = (uint16_t)(nDrawBufferDepthMin1 - line_data->lineend_y_init);

	/* dx_abs / dy_abs = magnitudes */
	int32_t dx = (int16_t)line_data->lineend_x_init - (int16_t)line_data->linestart_x_init;
	if (dx < 0)
		dx = -dx;
	line_data->dx_abs = (uint16_t)dx;
	int32_t dy = (int16_t)line_data->lineend_y_inv - (int16_t)line_data->linestart_y_inv;
	if (dy < 0)
		dy = -dy;
	line_data->dy_abs = (uint16_t)dy;

	/* Per-column screen-buffer offsets:
	 *   screen_offset[k] = bytesPerPixel * ±dx + nDrawBufferMemoryWidth * ±dy
	 * with sign flips per the major/x-flip flags and orientation. */
	for (int k = 0; k < line_data->scan_count; ++k) {
		int32_t dx_i = (int32_t)(int16_t)line_data->dda[2 * k];
		int32_t dy_i = (int32_t)(int16_t)line_data->dda[2 * k + 1];
		if (line_data->major_axis_flag)
			dx_i = -dx_i;
		if (line_data->x_flip)
			dy_i = -dy_i;
		if (nDrawBufferOrientation <= 0)
			dy_i = -dy_i;
		line_data->screen_offset[k] = saved_mem_w_3 * dy_i + bytesPerPixel * dx_i;
	}
	nDrawBufferMemoryWidth = saved_mem_w_3;
}

/* ===================================================================
 * preparefastdraw - install draw target; rebuild line_data on angle change
 * ================================================================ */
void rotscale_prepare_fastdraw(uint16_t angle) {
	nDrawBufferWidth = (int16_t)pixelswide;
	nDrawBufferWidthMin1 = (int16_t)pixelswidemin1;
	nDrawBufferDepth = (int16_t)pixelsdeep;
	nDrawBufferDepthMin1 = (int16_t)pixelsdeepmin1;
	nDrawBufferOrientation = -1;
	nDrawBufferMemoryWidth = bytesPerPixel * pixelswide;
	pDrawBuffer = (uint8_t*)buffer_ptr;
	bSquarePixels = (yAspect == 0) ? 1 : 0;
	pCurrentLine = &line_data_storage;
	nDiagonalAngle = (yAspect == 0) ? 0x2000 : 8704;
	if (angle != pCurrentLine->cached_angle || !rotscale_linedata_built) {
		buildlinedata(angle, pCurrentLine);
		rotscale_linedata_built = 1;
	}
}

/* Force a line_data rebuild on the next preparefastdraw call. The
 * binary's TIE_simulator clears dword_C787C at sim startup so the first
 * sprite render in the new mission rebuilds the cache from scratch
 * (defensive — prevents stale data from a previous flight bleeding into
 * the new one). */
void rotscale_invalidate_linedata(void) { rotscale_linedata_built = 0; }

/* ===================================================================
 * updateperp - keep startdrawpoint within column bounds across rows
 * ================================================================ */
static uint16_t updateperp(void) {
	int16_t new_startdraw = startdrawpoint;
	uint16_t result = 0;
	int16_t step = 1;

	if (perpendflag) {
		perpendflag = 0;
		startdrawpoint = new_startdraw;
		return 0;
	}

	result = perpendfrac;
	perpendfrac = (uint16_t)(perpendfrac + pCurrentLine->perp_update_inc);
	if (pCurrentLine->perp_flag) {
		if (result > perpendfrac)
			step = 2;
	} else if (result <= perpendfrac) {
		startdrawpoint = new_startdraw;
		return result;
	}

	int16_t idx = startdrawpoint;
	while (idx < 0)
		idx = (int16_t)(idx + pCurrentLine->scan_count);
	while (idx >= (int16_t)pCurrentLine->scan_count)
		idx = (int16_t)(idx - pCurrentLine->scan_count);

	new_startdraw = (int16_t)(startdrawpoint - step);
	uint16_t* e = (uint16_t*)((uint8_t*)pCurrentLine + 4 * idx);

	if (pCurrentLine->case_type) {
		result = e[24];
		if (pCurrentLine->packed_octant_a == 1) {
			if (result == e[22]) {
				startdrawpoint = new_startdraw;
				return result;
			}
		} else {
			new_startdraw = (int16_t)(startdrawpoint + step);
			if (result == e[26]) {
				startdrawpoint = new_startdraw;
				return result;
			}
		}
	} else {
		result = e[25];
		if (pCurrentLine->packed_octant_a == 1) {
			new_startdraw = (int16_t)(startdrawpoint + step);
			if (result == e[27]) {
				startdrawpoint = new_startdraw;
				return result;
			}
		} else if (result == e[23]) {
			startdrawpoint = new_startdraw;
			return result;
		}
	}
	perpendflag = 1;
	startdrawpoint = new_startdraw;
	return result;
}

/* ===================================================================
 * The 8 octant setstartcase / updatecase handlers.
 *
 * Octant encoding: bit 2 = axis (0=Y-major, 1=X-major),
 *                  bit 1 = Y direction, bit 0 = X direction.
 *
 * Each setstartcase clips plot{x,y} into the screen rect by stepping
 * (±dx, ±dy) per the octant convention, then computes line{start,end}
 * {x,y}, first/last vispoint+incoffset, and startdrawpoint.
 * Returns 0 (skip render) if the line is fully off-screen.
 *
 * Each updatecase advances pDrawBuffer by ±row_step (Y-major) or
 * ±bytesPerPixel (X-major), bumps line{start,end}{x,y} by ±1, and
 * refunds/extends first/last vispoint via the dy-run-length table.
 * ================================================================ */

/* helper: pCurrentLine 4-byte stride access (for vispoint scans) */
static inline uint16_t* pcl_word_at(int byte_off) { return (uint16_t*)((uint8_t*)pCurrentLine + byte_off); }

/* --- octant 0 : X-major, X-right, Y-down ------------------------- */
static int setstartcase0(void) {
	int16_t plotx_loc = plotx;
	int16_t ploty_loc = ploty;
	int16_t row_off = 0;

	while (plotx_loc < 0) {
		ploty_loc = (int16_t)(ploty_loc + pCurrentLine->dy_abs + 1);
		plotx_loc = (int16_t)(plotx_loc + pCurrentLine->dx_abs + 1);
		row_off -= nDrawBufferWidth;
	}
	while (plotx_loc >= nDrawBufferWidth) {
		ploty_loc = (int16_t)(ploty_loc - (pCurrentLine->dy_abs + 1));
		plotx_loc = (int16_t)(plotx_loc - (pCurrentLine->dx_abs + 1));
		row_off += nDrawBufferWidth;
	}
	int16_t y_off = (int16_t)pCurrentLine->dda[2 * plotx_loc + 1];
	int16_t lsy = (int16_t)(ploty_loc - y_off);
	if (lsy >= nDrawBufferDepth) {
		linestarty = lsy;
		lineendy = (int16_t)(pCurrentLine->dy_abs + lsy);
		ploty = lsy;
		plotx = plotx_loc;
		return 0;
	}
	linestartx = 0;
	linestarty = lsy;
	int16_t first_vp = 0;
	if (lsy < 0) {
		int16_t neg = (int16_t)-lsy;
		if (neg > (int16_t)pCurrentLine->dy_abs) {
			/* Sprite is entirely above the screen (lineendy = dy_abs +
			 * lsy = dy_abs - neg < 0 → lastvispoint = -1 → no render).
			 * Original at 0x4a06d continues with first_vp = -1 and a
			 * huge firstyincoffset, then wastes hundreds of updatecase0
			 * iterations reading past run_lengths into adjacent BSS
			 * (silent garbage on the DOS heap, ASAN-trap on tight
			 * allocators). Short-circuit since nothing is rendered
			 * anyway -- same visual outcome, no OOB. */
			ploty = lsy;
			plotx = plotx_loc;
			return 0;
		}
		firstyincoffset = neg;
		first_vp = neg;
		for (uint16_t i = 0; i <= pCurrentLine->dx_abs; ++i) {
			uint16_t* e = pcl_word_at(4 * i);
			if (neg == (int16_t)e[25]) {
				first_vp = (int16_t)e[24];
				break;
			}
		}
	}
	lineendx = (int16_t)(pCurrentLine->dx_abs + linestartx);
	lastyincoffset = nDrawBufferDepth;
	firstvispoint = first_vp;
	int16_t ley = (int16_t)(pCurrentLine->dy_abs + linestarty);
	lineendy = ley;
	int16_t last_vp;
	if (ley < 0) {
		last_vp = -1;
	} else if (ley >= nDrawBufferDepth) {
		lastyincoffset = (int16_t)(nDrawBufferDepth - linestarty);
		uint16_t k;
		for (k = 0; k <= pCurrentLine->dx_abs && (uint16_t)lastyincoffset != pCurrentLine->dda[2 * k + 1];
			 ++k)
			;
		last_vp = (int16_t)(pCurrentLine->dda[2 * k] - 1);
	} else {
		last_vp = (int16_t)(pCurrentLine->scan_count - 1);
	}
	lastvispoint = last_vp;
	startdrawpoint = (int16_t)(row_off + plotx_loc);
	ploty = lsy;
	plotx = plotx_loc;
	return 1;
}

static int updatecase0(void) {
	int16_t fvp = firstvispoint;
	int16_t fyi = firstyincoffset;
	int16_t ley_local = lineendy;
	int result;
	pDrawBuffer -= pCurrentLine->row_step;
	int16_t lsy_new = (int16_t)(linestarty + 1);
	if (linestarty == -1) {
		fvp = 0;
	} else if (lsy_new >= nDrawBufferDepth) {
		/* Retail jumps to the trailing fall-through with result=0;
		 * the lineendy update is skipped so v1 stays at its initial
		 * value (= lineendy). */
		result = 0;
		goto done;
	} else if (lsy_new < 0) {
		fyi = (int16_t)(firstyincoffset - 1);
		fvp = (int16_t)(firstvispoint - (int16_t)pCurrentLine->run_lengths[fyi]);
	}
	ley_local = (int16_t)(lineendy + 1);
	if (lineendy == -1) {
		int16_t lyi = (int16_t)(pCurrentLine->num_run_lengths - 1);
		lastyincoffset = lyi;
		lastvispoint = (int16_t)(pCurrentLine->scan_count - 1);
		int16_t cand = (int16_t)(lastvispoint - (int16_t)pCurrentLine->run_lengths[lyi] + 1);
		if (cand < 0)
			cand = 0;
		fvp = cand;
	} else if (ley_local >= nDrawBufferDepth) {
		if (ley_local == nDrawBufferDepth)
			lastyincoffset = (int16_t)pCurrentLine->num_run_lengths;
		--lastyincoffset;
		lastvispoint = (int16_t)(lastvispoint - (int16_t)pCurrentLine->run_lengths[lastyincoffset]);
	}
	result = 1;
done:
	firstyincoffset = fyi;
	++linestarty;
	lineendy = ley_local;
	firstvispoint = fvp;
	return result;
}

/* --- octant 1 : X-major, X-right, Y-up --------------------------- */
static int setstartcase1(void) {
	int16_t plotx_loc = plotx;
	int16_t ploty_loc = ploty;
	int16_t row_off = 0;
	while (plotx_loc < 0) {
		ploty_loc = (int16_t)(ploty_loc - (pCurrentLine->dy_abs + 1));
		plotx_loc = (int16_t)(plotx_loc + (pCurrentLine->dx_abs + 1));
		row_off -= nDrawBufferWidth;
	}
	while (plotx_loc >= nDrawBufferWidth) {
		ploty_loc = (int16_t)(ploty_loc + (pCurrentLine->dy_abs + 1));
		plotx_loc = (int16_t)(plotx_loc - (pCurrentLine->dx_abs + 1));
		row_off += nDrawBufferWidth;
	}
	int16_t lsy = (int16_t)(pCurrentLine->dda[2 * plotx_loc + 1] + ploty_loc);
	linestarty = lsy;
	linestartx = 0;
	int16_t first_vp = -1;
	if (lsy >= 0) {
		if (lsy >= nDrawBufferDepth) {
			firstyincoffset = (int16_t)(lsy - nDrawBufferDepth);
			if ((int16_t)(lsy - nDrawBufferDepth) + 1 > (int16_t)pCurrentLine->dx_abs) {
				first_vp = -1;
			} else {
				for (uint16_t i = 0; i <= pCurrentLine->dx_abs; ++i) {
					uint16_t* e = pcl_word_at(4 * i);
					if ((uint16_t)(firstyincoffset + 1) == e[25]) {
						first_vp = (int16_t)e[24];
						break;
					}
				}
			}
		} else {
			first_vp = 0;
		}
	}
	lineendx = (int16_t)(pCurrentLine->dx_abs + linestartx);
	lastyincoffset = nDrawBufferDepth;
	firstvispoint = first_vp;
	int16_t ley = (int16_t)(linestarty - pCurrentLine->dy_abs);
	lineendy = ley;
	if (ley >= nDrawBufferDepth) {
		ploty = lsy;
		plotx = plotx_loc;
		return 0;
	}
	int16_t last_vp;
	if (ley < 0 || ley >= nDrawBufferDepth) {
		if (linestarty < 0) {
			lastvispoint = -1;
			startdrawpoint = (int16_t)(row_off + plotx_loc);
			ploty = lsy;
			plotx = plotx_loc;
			return 1;
		}
		lastyincoffset = linestarty;
		uint16_t k;
		/* Original at 0x4a489 reads (pCurrentLine + 40) which is
		 * linestart_y_init (byte 40), NOT packed_octant_a (byte 20). */
		for (k = 0; k <= pCurrentLine->dx_abs && (uint16_t)(pCurrentLine->linestart_y_init + linestarty +
															1) != pCurrentLine->dda[2 * k + 1];
			 ++k)
			;
		last_vp = (int16_t)pCurrentLine->dda[2 * k];
	} else {
		last_vp = (int16_t)pCurrentLine->scan_count;
	}
	lastvispoint = (int16_t)(last_vp - 1);
	startdrawpoint = (int16_t)(row_off + plotx_loc);
	ploty = lsy;
	plotx = plotx_loc;
	return 1;
}

static int updatecase1(void) {
	int16_t lvp = lastvispoint;
	pDrawBuffer -= pCurrentLine->row_step;
	int16_t lsy_new = (int16_t)(linestarty + 1);
	if (linestarty == -1) {
		firstvispoint = 0;
		lastyincoffset = 0;
		/* Original at 0x4a532 reads (pCurrentLine + 6452) which is
		 * run_lengths[0], NOT dy_abs. */
		lvp = (int16_t)(pCurrentLine->run_lengths[0] - 1);
	} else if (lsy_new >= nDrawBufferDepth) {
		if (lsy_new == nDrawBufferDepth)
			firstyincoffset = -1;
		++firstyincoffset;
		/* Original at 0x4a565 reads (pCurrentLine + 6452 + 2*N) which
		 * is run_lengths[N], NOT dda[]. Same pattern below for lvp. */
		firstvispoint = (int16_t)(firstvispoint + (int16_t)pCurrentLine->run_lengths[firstyincoffset]);
	}
	int16_t ley_new = (int16_t)(lineendy + 1);
	if (lineendy == -1) {
		lvp = (int16_t)(pCurrentLine->scan_count - 1);
	} else if (ley_new >= nDrawBufferDepth) {
		++lineendy;
		++linestarty;
		lastvispoint = lvp;
		return 0;
	} else if (ley_new < 0 && lsy_new > 0) {
		/* Original at 0x4a58c uses strict `> 0` (not `>= 0`). When
		 * linestarty just transitioned from -1 to 0, lsy_new is 0 and
		 * the accumulator must NOT run (the linestarty==-1 branch
		 * above already seeded lvp from run_lengths[0]). */
		++lastyincoffset;
		lvp = (int16_t)(lvp + (int16_t)pCurrentLine->run_lengths[lastyincoffset]);
	}
	++lineendy;
	++linestarty;
	lastvispoint = lvp;
	return 1;
}

/* --- octant 2 : X-major, X-left, Y-up --------------------------- */
static int setstartcase2(void) {
	int16_t plotx_loc = plotx;
	int16_t ploty_loc = ploty;
	int16_t row_off = 0;
	while (plotx_loc < 0) {
		ploty_loc = (int16_t)(ploty_loc - (pCurrentLine->dy_abs + 1));
		plotx_loc = (int16_t)(plotx_loc + (pCurrentLine->dx_abs + 1));
		row_off += nDrawBufferWidth;
	}
	while (plotx_loc >= nDrawBufferWidth) {
		ploty_loc = (int16_t)(ploty_loc + (pCurrentLine->dy_abs + 1));
		plotx_loc = (int16_t)(plotx_loc - (pCurrentLine->dx_abs + 1));
		row_off -= nDrawBufferWidth;
	}
	int16_t lsy =
		(int16_t)(ploty_loc - (int16_t)pCurrentLine->dda[2 * (nDrawBufferWidthMin1 - plotx_loc) + 1]);
	linestartx = nDrawBufferWidthMin1;
	lineendx = (int16_t)(nDrawBufferWidthMin1 - pCurrentLine->dx_abs);
	lineendy = (int16_t)(pCurrentLine->dy_abs + lsy);
	linestarty = lsy;
	firstvispoint = -1;
	lastvispoint = -1;
	firstyincoffset = -1;
	lastyincoffset = nDrawBufferDepth;
	int16_t first_vp = -1;
	int16_t x_pos = (int16_t)(nDrawBufferWidthMin1 - plotx_loc);
	if (lsy < 0) {
		firstyincoffset = (int16_t)(-lsy - 1);
		if ((int16_t)-lsy > (int16_t)pCurrentLine->dy_abs) {
			first_vp = -1;
		} else {
			for (int16_t i = 0; i <= (int16_t)pCurrentLine->dx_abs; ++i) {
				uint16_t* e = pcl_word_at(4 * i);
				if ((uint16_t)-lsy == e[25]) {
					first_vp = (int16_t)e[24];
					break;
				}
			}
		}
	} else if (lsy >= nDrawBufferDepth) {
		startdrawpoint = (int16_t)(row_off + x_pos);
		ploty = lsy;
		plotx = plotx_loc;
		return 1;
	} else {
		first_vp = 0;
	}
	firstvispoint = first_vp;
	int16_t last_vp = -1;
	if (lineendy >= 0) {
		if (lineendy >= nDrawBufferDepth) {
			lastyincoffset = (int16_t)(nDrawBufferDepth - linestarty - 1);
			int16_t k;
			for (k = (int16_t)pCurrentLine->dx_abs; k >= 0; --k) {
				uint16_t* e = pcl_word_at(4 * k);
				if ((uint16_t)lastyincoffset == e[25]) {
					last_vp = (int16_t)e[24];
					break;
				}
			}
		} else {
			last_vp = (int16_t)(pCurrentLine->scan_count - 1);
		}
		lastvispoint = last_vp;
		startdrawpoint = (int16_t)(row_off + x_pos);
		ploty = lsy;
		plotx = plotx_loc;
		return 1;
	}
	ploty = lsy;
	plotx = plotx_loc;
	return 0;
}

static int updatecase2(void) {
	int16_t lvp = lastvispoint;
	int16_t lsy_new = (int16_t)(linestarty - 1);
	pDrawBuffer += pCurrentLine->row_step;
	if (lsy_new == nDrawBufferDepthMin1) {
		firstvispoint = 0;
		lvp = -1;
		lastyincoffset = -1;
	} else if (lsy_new < 0) {
		if (lsy_new == -1)
			firstyincoffset = -1;
		++firstyincoffset;
		firstvispoint = (int16_t)(firstvispoint + (int16_t)pCurrentLine->run_lengths[firstyincoffset]);
	}
	int16_t ley_new = (int16_t)(lineendy - 1);
	if (lineendy - 1 == nDrawBufferDepthMin1) {
		lvp = (int16_t)(pCurrentLine->scan_count - 1);
	} else if (ley_new < 0) {
		--lineendy;
		--linestarty;
		lastvispoint = lvp;
		return 0;
	} else if (ley_new >= nDrawBufferDepth && lsy_new < nDrawBufferDepth) {
		++lastyincoffset;
		lvp = (int16_t)(lvp + (int16_t)pCurrentLine->run_lengths[lastyincoffset]);
	}
	--lineendy;
	--linestarty;
	lastvispoint = lvp;
	return 1;
}

/* --- octant 3 : X-major, X-left, Y-down ------------------------- */
static int setstartcase3(void) {
	int16_t plotx_loc = plotx;
	int16_t ploty_loc = ploty;
	int16_t row_off = 0;
	while (plotx_loc < 0) {
		ploty_loc = (int16_t)(ploty_loc + (pCurrentLine->dy_abs + 1));
		plotx_loc = (int16_t)(plotx_loc + (pCurrentLine->dx_abs + 1));
		row_off += nDrawBufferWidth;
	}
	while (plotx_loc >= nDrawBufferWidth) {
		ploty_loc = (int16_t)(ploty_loc - (pCurrentLine->dy_abs + 1));
		plotx_loc = (int16_t)(plotx_loc - (pCurrentLine->dx_abs + 1));
		row_off -= nDrawBufferWidth;
	}
	int16_t lsy = (int16_t)(pCurrentLine->dda[2 * (nDrawBufferWidthMin1 - plotx_loc) + 1] + ploty_loc);
	linestartx = nDrawBufferWidthMin1;
	lineendx = (int16_t)(nDrawBufferWidthMin1 - pCurrentLine->dx_abs);
	lineendy = (int16_t)(lsy - pCurrentLine->dy_abs);
	linestarty = lsy;
	firstvispoint = -1;
	lastvispoint = -1;
	firstyincoffset = -1;
	lastyincoffset = (int16_t)pCurrentLine->num_run_lengths;
	if (lsy < 0) {
		ploty = lsy;
		plotx = plotx_loc;
		return 0;
	}
	int16_t first_vp;
	if (lsy >= nDrawBufferDepth) {
		firstyincoffset = (int16_t)(lsy - nDrawBufferDepthMin1);
		if ((int16_t)(lsy - nDrawBufferDepthMin1) > (int16_t)pCurrentLine->dy_abs) {
			first_vp = -1;
		} else {
			first_vp = -1;
			for (int16_t i = 0; i <= (int16_t)pCurrentLine->dx_abs; ++i) {
				uint16_t* e = pcl_word_at(4 * i);
				if ((uint16_t)(lsy - nDrawBufferDepthMin1) == e[25]) {
					first_vp = (int16_t)e[24];
					break;
				}
			}
		}
	} else {
		first_vp = 0;
	}
	firstvispoint = first_vp;
	int16_t last_vp;
	if (lineendy >= nDrawBufferDepth) {
		last_vp = -1;
	} else if (lineendy < 0 || lineendy >= nDrawBufferDepth) {
		int16_t off = (int16_t)(pCurrentLine->dy_abs + lineendy);
		lastyincoffset = (int16_t)(off + 1);
		int16_t k;
		last_vp = -1;
		for (k = (int16_t)pCurrentLine->dx_abs; k >= 0; --k) {
			uint16_t* e = pcl_word_at(4 * k);
			if ((uint16_t)off == e[25]) {
				last_vp = (int16_t)e[24];
				break;
			}
		}
	} else {
		last_vp = (int16_t)(pCurrentLine->scan_count - 1);
	}
	lastvispoint = last_vp;
	startdrawpoint = (int16_t)(row_off + nDrawBufferWidthMin1 - plotx_loc);
	ploty = lsy;
	plotx = plotx_loc;
	return 1;
}

static int updatecase3(void) {
	int16_t fvp = firstvispoint;
	int16_t fyi = firstyincoffset;
	int16_t lyi = lastyincoffset;
	int16_t lsy_new = (int16_t)(linestarty - 1);
	int16_t ley_new = (int16_t)(lineendy - 1);
	pDrawBuffer += pCurrentLine->row_step;
	if (lsy_new == nDrawBufferDepthMin1) {
		fyi = -1;
		fvp = 0;
	} else {
		if (lsy_new < 0) {
			--linestarty;
			--lineendy;
			firstyincoffset = fyi;
			firstvispoint = fvp;
			return 0;
		}
		if (lsy_new >= nDrawBufferDepth && ley_new < nDrawBufferDepth) {
			fyi = (int16_t)(firstyincoffset - 1);
			fvp = (int16_t)(firstvispoint - (int16_t)pCurrentLine->run_lengths[fyi]);
		}
	}
	if (ley_new == nDrawBufferDepthMin1) {
		lyi = (int16_t)(pCurrentLine->num_run_lengths - 1);
		lastvispoint = (int16_t)(pCurrentLine->scan_count - 1);
		int16_t cand = (int16_t)(lastvispoint - (int16_t)pCurrentLine->run_lengths[lyi] + 1);
		if (cand < 0)
			cand = 0;
		fvp = cand;
	} else if (ley_new < 0) {
		lyi = (int16_t)(lastyincoffset - 1);
		lastvispoint = (int16_t)(lastvispoint - (int16_t)pCurrentLine->run_lengths[lyi]);
	}
	--linestarty;
	--lineendy;
	lastyincoffset = lyi;
	firstyincoffset = fyi;
	firstvispoint = fvp;
	return 1;
}

/* --- octant 4 : Y-major, X-right, Y-down ------------------------ */
static int setstartcase4(void) {
	int16_t ploty_loc = ploty;
	int16_t plotx_loc = plotx;
	int16_t row_off = 0;
	while (ploty_loc < 0) {
		ploty_loc = (int16_t)(ploty_loc + (pCurrentLine->dy_abs + 1));
		plotx_loc = (int16_t)(plotx_loc + (pCurrentLine->dx_abs + 1));
		row_off -= nDrawBufferDepth;
	}
	while (ploty_loc >= nDrawBufferDepth) {
		ploty_loc = (int16_t)(ploty_loc - (pCurrentLine->dy_abs + 1));
		plotx_loc = (int16_t)(plotx_loc - (pCurrentLine->dx_abs + 1));
		row_off += nDrawBufferDepth;
	}
	int16_t lsx = (int16_t)(plotx_loc - (int16_t)pCurrentLine->dda[2 * ploty_loc]);
	linestarty = 0;
	firstvispoint = 0;
	firstxincoffset = -1;
	linestartx = lsx;
	lastxincoffset = nDrawBufferWidth;
	if (lsx < 0) {
		firstxincoffset = (int16_t)(-lsx - 1);
		int16_t first_vp = -1;
		if ((int16_t)-lsx <= (int16_t)pCurrentLine->dx_abs) {
			for (int16_t i = 0; i <= (int16_t)pCurrentLine->dy_abs; ++i) {
				uint16_t* e = pcl_word_at(4 * i);
				if ((uint16_t)-lsx == e[24]) {
					first_vp = (int16_t)e[25];
					break;
				}
			}
		}
		firstvispoint = first_vp;
	}
	lineendx = (int16_t)(pCurrentLine->dx_abs + linestartx);
	lineendy = (int16_t)(pCurrentLine->dy_abs + linestarty);
	if (lineendx < 0) {
		plotx = lsx;
		ploty = ploty_loc;
		return 0;
	}
	int16_t last_vp;
	if (lineendx >= nDrawBufferWidth) {
		if (nDrawBufferWidth <= linestartx) {
			last_vp = -1;
		} else {
			last_vp = -1;
			lastxincoffset = (int16_t)(nDrawBufferWidth - linestartx - 1);
			for (int16_t k = 0; k <= (int16_t)pCurrentLine->dy_abs; ++k) {
				uint16_t* e = pcl_word_at(4 * k);
				if ((uint16_t)(nDrawBufferWidth - linestartx) == e[24]) {
					last_vp = (int16_t)(e[25] - 1);
					break;
				}
			}
		}
	} else {
		last_vp = (int16_t)(pCurrentLine->scan_count - 1);
	}
	lastvispoint = last_vp;
	startdrawpoint = (int16_t)(row_off + ploty_loc);
	plotx = lsx;
	ploty = ploty_loc;
	return 1;
}

static int updatecase4(void) {
	int16_t lxi = lastxincoffset;
	int16_t lvp = lastvispoint;
	int16_t lsx_new = (int16_t)(linestartx - 1);
	int result;
	pDrawBuffer -= bytesPerPixel;
	if (lsx_new == nDrawBufferWidthMin1) {
		lvp = -1;
		firstvispoint = 0;
		lxi = -1;
	} else if (lsx_new < 0) {
		++firstxincoffset;
		firstvispoint = (int16_t)(firstvispoint + (int16_t)pCurrentLine->run_lengths[firstxincoffset]);
	}
	int16_t lex_new = (int16_t)(lineendx - 1);
	if (lex_new < 0) {
		result = 0;
	} else {
		if (lex_new == nDrawBufferWidthMin1) {
			lvp = (int16_t)(pCurrentLine->scan_count - 1);
		} else if (lex_new >= nDrawBufferWidth && lsx_new < nDrawBufferWidth) {
			++lxi;
			lvp = (int16_t)(lvp + (int16_t)pCurrentLine->run_lengths[lxi]);
		}
		result = 1;
	}
	lastvispoint = lvp;
	lastxincoffset = lxi;
	--linestartx;
	--lineendx;
	return result;
}

/* --- octant 5 : Y-major, X-left, Y-down ------------------------- */
static int setstartcase5(void) {
	int16_t ploty_loc = ploty;
	int16_t plotx_loc = plotx;
	int16_t row_off = 0;
	while (ploty_loc < 0) {
		ploty_loc = (int16_t)(ploty_loc + (pCurrentLine->dy_abs + 1));
		plotx_loc = (int16_t)(plotx_loc - (pCurrentLine->dx_abs + 1));
		row_off += nDrawBufferDepth;
	}
	while (ploty_loc >= nDrawBufferDepth) {
		ploty_loc = (int16_t)(ploty_loc - (pCurrentLine->dy_abs + 1));
		plotx_loc = (int16_t)(plotx_loc + (pCurrentLine->dx_abs + 1));
		row_off -= nDrawBufferDepth;
	}
	int16_t lsx = (int16_t)(plotx_loc - (int16_t)pCurrentLine->dda[2 * (nDrawBufferDepthMin1 - ploty_loc)]);
	linestarty = nDrawBufferDepthMin1;
	lineendy = (int16_t)(nDrawBufferDepthMin1 - pCurrentLine->dy_abs);
	lineendx = (int16_t)(pCurrentLine->dx_abs + lsx);
	linestartx = lsx;
	firstvispoint = 0;
	firstxincoffset = -1;
	lastxincoffset = nDrawBufferWidth;
	if (lsx >= nDrawBufferWidth) {
		plotx = lsx;
		ploty = ploty_loc;
		return 0;
	}
	if (lsx < 0) {
		if (lineendx < 0) {
			lastvispoint = -1;
			startdrawpoint = (int16_t)(row_off + nDrawBufferDepthMin1 - ploty_loc);
			plotx = lsx;
			ploty = ploty_loc;
			return 1;
		}
		firstxincoffset = (int16_t)-lsx;
		int16_t first_vp = -1;
		if ((int16_t)-lsx <= (int16_t)pCurrentLine->dx_abs) {
			for (int16_t k = 0; k <= (int16_t)pCurrentLine->dy_abs; ++k) {
				uint16_t* e = pcl_word_at(4 * k);
				if ((uint16_t)-lsx == e[24]) {
					first_vp = (int16_t)e[25];
					break;
				}
			}
		}
		firstvispoint = first_vp;
	}
	int16_t last_vp;
	if (lineendx >= nDrawBufferWidth || lineendx < 0) {
		if (lineendx < nDrawBufferWidth || nDrawBufferWidth <= linestartx) {
			last_vp = -1;
		} else {
			lastxincoffset = (int16_t)(nDrawBufferWidth - linestartx);
			last_vp = -1;
			for (int16_t k = 0; k <= (int16_t)pCurrentLine->dy_abs; ++k) {
				uint16_t* e = pcl_word_at(4 * k);
				if ((uint16_t)(nDrawBufferWidth - linestartx) == e[24]) {
					last_vp = (int16_t)(e[25] - 1);
					break;
				}
			}
		}
	} else {
		last_vp = (int16_t)(pCurrentLine->scan_count - 1);
	}
	lastvispoint = last_vp;
	startdrawpoint = (int16_t)(row_off + nDrawBufferDepthMin1 - ploty_loc);
	plotx = lsx;
	ploty = ploty_loc;
	return 1;
}

static int updatecase5(void) {
	int16_t fvp = firstvispoint;
	int16_t fxi = firstxincoffset;
	int16_t lex = lineendx;
	pDrawBuffer += bytesPerPixel;
	int16_t lsx_new = (int16_t)(linestartx + 1);
	if (linestartx == -1) {
		fvp = 0;
	} else if (lsx_new < 0) {
		fxi = (int16_t)(firstxincoffset - 1);
		/* Latent OOB in the binary: when setstartcase5 takes the
		 * off-screen-left return (linestartx < 0 && lineendx < 0),
		 * firstxincoffset is left at its default -1 and the
		 * decrement here drives run_lengths[] to a negative index.
		 * Watcom emits the read anyway (sign-extended via `movsx`).
		 * Skip it in C — the only path producing fxi < 0 also has
		 * lastvispoint == -1 until lex transitions through -1, which
		 * unconditionally rewrites fvp in the `if (lex == -1)` block
		 * below. */
		if (fxi >= 0)
			fvp = (int16_t)(firstvispoint - (int16_t)pCurrentLine->run_lengths[fxi]);
		if (fvp < 0)
			fvp = 0;
	} else if (lsx_new >= nDrawBufferWidth) {
		/* Retail jumps past the lineendx update — leave it untouched. */
		++linestartx;
		firstxincoffset = fxi;
		firstvispoint = fvp;
		return 0;
	}
	int16_t lex_new = (int16_t)(lex + 1);
	if (lex == -1) {
		fxi = (int16_t)(pCurrentLine->num_run_lengths - 1);
		lastvispoint = (int16_t)(pCurrentLine->scan_count - 1);
		/* Original at 0x4b1e9 reads run_lengths[num_run_lengths - 1]. */
		fvp = (int16_t)(nDrawBufferDepth - (int16_t)pCurrentLine->run_lengths[fxi]);
		if (fvp < 0)
			fvp = 0;
	} else if (lex_new >= nDrawBufferWidth) {
		if (lex_new == nDrawBufferWidth) {
			lastvispoint = nDrawBufferDepthMin1;
			lastxincoffset = (int16_t)pCurrentLine->num_run_lengths;
		}
		--lastxincoffset;
		/* Original at 0x4b23d reads run_lengths[lastxincoffset]
		 * (HIWORD of the dword-packed lastyincoffset/lastxincoffset). */
		lastvispoint = (int16_t)(lastvispoint - (int16_t)pCurrentLine->run_lengths[lastxincoffset]);
	}
	lineendx = lex_new;
	++linestartx;
	firstxincoffset = fxi;
	firstvispoint = fvp;
	return 1;
}

/* --- octant 6 : Y-major, X-left, Y-up --------------------------- */
static int setstartcase6(void) {
	int16_t ploty_loc = ploty;
	int16_t plotx_loc = plotx;
	int16_t row_off = 0;
	while (ploty_loc < 0) {
		ploty_loc = (int16_t)(ploty_loc + (pCurrentLine->dy_abs + 1));
		plotx_loc = (int16_t)(plotx_loc - (pCurrentLine->dx_abs + 1));
		row_off -= nDrawBufferDepth;
	}
	while (ploty_loc >= nDrawBufferDepth) {
		ploty_loc = (int16_t)(ploty_loc - (pCurrentLine->dy_abs + 1));
		plotx_loc = (int16_t)(plotx_loc + (pCurrentLine->dx_abs + 1));
		row_off += nDrawBufferDepth;
	}
	int16_t lsx = (int16_t)(pCurrentLine->dda[2 * ploty_loc] + plotx_loc);
	linestarty = 0;
	lineendx = (int16_t)(lsx - pCurrentLine->dx_abs);
	lineendy = (int16_t)pCurrentLine->dy_abs;
	linestartx = lsx;
	firstvispoint = -1;
	firstxincoffset = -1;
	lastvispoint = -1;
	lastxincoffset = (int16_t)pCurrentLine->num_run_lengths;
	if (lsx < 0) {
		plotx = lsx;
		ploty = ploty_loc;
		return 0;
	}
	int16_t first_vp;
	if (lsx >= nDrawBufferWidth) {
		firstxincoffset = (int16_t)(lsx - nDrawBufferWidthMin1);
		if ((int16_t)(lsx - nDrawBufferWidthMin1) > (int16_t)pCurrentLine->dx_abs) {
			first_vp = -1;
		} else {
			first_vp = -1;
			for (int16_t i = 0; i <= (int16_t)pCurrentLine->dy_abs; ++i) {
				uint16_t* e = pcl_word_at(4 * i);
				if ((uint16_t)firstxincoffset == e[24]) {
					first_vp = (int16_t)e[25];
					break;
				}
			}
		}
	} else {
		first_vp = 0;
	}
	firstvispoint = first_vp;
	int16_t last_vp;
	if (lineendx >= nDrawBufferWidth) {
		last_vp = -1;
	} else if (lineendx < 0 || lineendx >= nDrawBufferWidth) {
		int16_t off = (int16_t)(pCurrentLine->dx_abs + lineendx);
		lastxincoffset = (int16_t)(off + 1);
		last_vp = -1;
		for (int16_t k = (int16_t)pCurrentLine->dy_abs; k >= 0; --k) {
			uint16_t* e = pcl_word_at(4 * k);
			if ((uint16_t)off == e[24]) {
				last_vp = (int16_t)e[25];
				break;
			}
		}
	} else {
		last_vp = (int16_t)(pCurrentLine->scan_count - 1);
	}
	lastvispoint = last_vp;
	startdrawpoint = (int16_t)(row_off + ploty_loc);
	plotx = lsx;
	ploty = ploty_loc;
	return 1;
}

static int updatecase6(void) {
	int16_t fvp = firstvispoint;
	int16_t lvp = lastvispoint;
	int16_t fxi = firstxincoffset;
	int16_t lex_new = (int16_t)(lineendx - 1);
	int16_t lsx_new = (int16_t)(linestartx - 1);
	int result;
	pDrawBuffer -= bytesPerPixel;
	if (lsx_new >= 0) {
		if (lsx_new == nDrawBufferWidthMin1 ||
			(lsx_new >= nDrawBufferWidth && lex_new < nDrawBufferWidth &&
			 (fxi = (int16_t)(firstxincoffset - 1),
			  /* Original at 0x4b4f7 reads run_lengths[firstxincoffset-1]. */
			  fvp = (int16_t)(firstvispoint - (int16_t)pCurrentLine->run_lengths[fxi]), fvp < 0))) {
			fvp = 0;
		}
		if (lex_new == nDrawBufferWidthMin1) {
			fxi = (int16_t)(pCurrentLine->num_run_lengths - 1);
			lvp = (int16_t)(pCurrentLine->scan_count - 1);
			/* Original at 0x4b526 reads run_lengths[num_run_lengths - 1]. */
			fvp = (int16_t)(pCurrentLine->scan_count - (int16_t)pCurrentLine->run_lengths[fxi]);
			if (fvp < 0)
				fvp = 0;
		} else if (lex_new < 0) {
			--lastxincoffset;
			/* Original at 0x4b55a reads run_lengths[lastxincoffset]
			 * (HIWORD of dword-packed lastyincoffset/lastxincoffset). */
			lvp = (int16_t)(lastvispoint - (int16_t)pCurrentLine->run_lengths[lastxincoffset]);
			if (lvp < 0)
				lvp = 0;
		}
		result = 1;
	} else {
		result = 0;
	}
	firstxincoffset = fxi;
	--lineendx;
	--linestartx;
	lastvispoint = lvp;
	firstvispoint = fvp;
	return result;
}

/* --- octant 7 : Y-major, X-right, Y-up -------------------------- */
static int setstartcase7(void) {
	int16_t ploty_loc = ploty;
	int16_t plotx_loc = plotx;
	int16_t row_off = 0;
	while (ploty_loc < 0) {
		ploty_loc = (int16_t)(ploty_loc + (pCurrentLine->dy_abs + 1));
		plotx_loc = (int16_t)(plotx_loc + (pCurrentLine->dx_abs + 1));
		row_off += nDrawBufferDepth;
	}
	while (ploty_loc >= nDrawBufferDepth) {
		ploty_loc = (int16_t)(ploty_loc - (pCurrentLine->dy_abs + 1));
		plotx_loc = (int16_t)(plotx_loc - (pCurrentLine->dx_abs + 1));
		row_off -= nDrawBufferDepth;
	}
	int16_t lsx = (int16_t)(pCurrentLine->dda[2 * (nDrawBufferDepthMin1 - ploty_loc)] + plotx_loc);
	linestarty = nDrawBufferDepthMin1;
	lineendx = (int16_t)(lsx - pCurrentLine->dx_abs);
	lineendy = (int16_t)(nDrawBufferDepthMin1 - pCurrentLine->dy_abs);
	linestartx = lsx;
	firstxincoffset = -1;
	lastxincoffset = nDrawBufferWidth;
	int16_t first_vp;
	if (lsx < 0) {
		first_vp = -1;
	} else if (lsx >= nDrawBufferWidth) {
		firstxincoffset = (int16_t)(lsx - nDrawBufferWidthMin1 - 1);
		if ((int16_t)(lsx - nDrawBufferWidthMin1) > (int16_t)pCurrentLine->dx_abs) {
			first_vp = -1;
		} else {
			first_vp = -1;
			for (int16_t i = 0; i <= (int16_t)pCurrentLine->dy_abs; ++i) {
				uint16_t* e = pcl_word_at(4 * i);
				if ((uint16_t)(lsx - nDrawBufferWidthMin1) == e[24]) {
					first_vp = (int16_t)e[25];
					break;
				}
			}
		}
	} else {
		first_vp = 0;
	}
	firstvispoint = first_vp;
	int16_t last_vp;
	if (lineendx >= nDrawBufferWidth) {
		plotx = lsx;
		ploty = ploty_loc;
		return 0;
	}
	if (lineendx < 0 || lineendx >= nDrawBufferWidth) {
		if (lineendx >= 0 || linestartx < 0) {
			last_vp = -1;
		} else {
			lastxincoffset = (int16_t)(pCurrentLine->dx_abs + lineendx);
			last_vp = -1;
			for (int16_t k = (int16_t)pCurrentLine->dy_abs; k >= 0; --k) {
				uint16_t* e = pcl_word_at(4 * k);
				if ((uint16_t)lastxincoffset == e[24]) {
					last_vp = (int16_t)e[25];
					break;
				}
			}
		}
	} else {
		last_vp = (int16_t)(pCurrentLine->scan_count - 1);
	}
	lastvispoint = last_vp;
	startdrawpoint = (int16_t)(row_off + nDrawBufferDepthMin1 - ploty_loc);
	plotx = lsx;
	ploty = ploty_loc;
	return 1;
}

static int updatecase7(void) {
	int16_t lvp = lastvispoint;
	int16_t fxi = firstxincoffset;
	pDrawBuffer += bytesPerPixel;
	int16_t lsx_new = (int16_t)(linestartx + 1);
	if (linestartx == -1) {
		lvp = -1;
		firstvispoint = 0;
		lastxincoffset = -1;
	} else if (lsx_new >= nDrawBufferWidth) {
		if (lsx_new == nDrawBufferWidth)
			fxi = -1;
		++fxi;
		firstvispoint = (int16_t)(firstvispoint + (int16_t)pCurrentLine->run_lengths[fxi]);
	}
	int16_t lex_new = (int16_t)(lineendx + 1);
	if (lineendx == -1) {
		lvp = (int16_t)(pCurrentLine->scan_count - 1);
	} else if (lex_new >= nDrawBufferWidth) {
		++lineendx;
		++linestartx;
		firstxincoffset = fxi;
		lastvispoint = lvp;
		return 0;
	} else if (lex_new < 0 && lsx_new >= 0) {
		++lastxincoffset;
		lvp = (int16_t)(lvp + (int16_t)pCurrentLine->run_lengths[lastxincoffset]);
	}
	firstxincoffset = fxi;
	lastvispoint = lvp;
	++lineendx;
	++linestartx;
	return 1;
}

/* --- dispatchers ------------------------------------------------- */

static int setstartvars(void) {
	switch (pCurrentLine->octant_case) {
		case 0:
			return setstartcase0();
		case 1:
			return setstartcase1();
		case 2:
			return setstartcase2();
		case 3:
			return setstartcase3();
		case 4:
			return setstartcase4();
		case 5:
			return setstartcase5();
		case 6:
			return setstartcase6();
		case 7:
			return setstartcase7();
		default:
			return 0;
	}
}

static int updatecases(void) {
	switch (pCurrentLine->octant_case) {
		case 0:
			return updatecase0();
		case 1:
			return updatecase1();
		case 2:
			return updatecase2();
		case 3:
			return updatecase3();
		case 4:
			return updatecase4();
		case 5:
			return updatecase5();
		case 6:
			return updatecase6();
		case 7:
			return updatecase7();
		default:
			return 0;
	}
}

/* Decode a 0xFF-terminated stream of rows whose opcodes end at 0xFE:
 *
 *   0xFB : set running base color := data[1]; skip 3 bytes total
 *   0xFC : extended x-step; index data[1] looks up word_DB854/word_DBA54
 *          to advance the 24-bit x accumulator without emitting
 *   0xFD : explicit (run_len, color) = (data[1], data[2]); skip 3 bytes
 *   else : run_color = base_color + (op >> shift[bit_split])
 *          run_len   = op & mask[bit_split]
 *
 * Runs are repeated according to the fractional Y accumulator while the
 * octant walker advances the rotated destination scanline. */
static int rotatescale(const uint8_t* data, int32_t bit_split) {
	{
		static int dbg_rs_frame = 0;
		static int dbg_rs_seen = 0;
		if ((++dbg_rs_frame % 60) == 1 && dbg_rs_seen < 3) {
			dbg_rs_seen++;
			TieDiagnostics_Log(TIE_LOG_INFO,
							   "[rs] data=%p bit_split=%d mask=0x%02X shift=%d "
							   "data[0..31]="
							   "%02X %02X %02X %02X %02X %02X %02X %02X "
							   "%02X %02X %02X %02X %02X %02X %02X %02X "
							   "%02X %02X %02X %02X %02X %02X %02X %02X "
							   "%02X %02X %02X %02X %02X %02X %02X %02X\n",
							   (const void*)data, (int)bit_split, rotscale_run_mask[bit_split & 0x0F],
							   rotscale_run_shift[bit_split & 0x0F], data[0], data[1], data[2], data[3],
							   data[4], data[5], data[6], data[7], data[8], data[9], data[10], data[11],
							   data[12], data[13], data[14], data[15], data[16], data[17], data[18], data[19],
							   data[20], data[21], data[22], data[23], data[24], data[25], data[26], data[27],
							   data[28], data[29], data[30], data[31]);
		}
	}
	reverseflag = 1;
	if (!setstartvars())
		return 0;

	uint8_t* buf_base = (uint8_t*)buffer_ptr;
	int32_t mem_w = nDrawBufferMemoryWidth;
	uint8_t* buf_end = buf_base + (size_t)mem_w * nDrawBufferDepth;
	int32_t row_step;
	if (nDrawBufferOrientation <= 0) {
		row_step = mem_w;
		pCurrentLine->current_row_base =
			pDrawBuffer + mem_w * (nDrawBufferDepthMin1 - linestarty) + bytesPerPixel * linestartx;
	} else {
		row_step = -mem_w;
		pCurrentLine->current_row_base = pDrawBuffer + mem_w * linestarty + bytesPerPixel * linestartx;
	}
	pCurrentLine->row_step = row_step;
	perpendflag = 1;
	pDrawBuffer = pCurrentLine->current_row_base;
	perpendfrac = 0;

	/* 3-byte y-fractional accumulator (v65:v66:v67 in the retail asm).
	 * Stepped per source-row by the eff_scale_y low/high bytes, the
	 * delta vs. pre-step state gives the row replay count. */
	uint8_t acc_lo = 0;
	uint8_t acc_mid = 0;
	uint8_t acc_hi = 0;

	uint8_t mask_N = rotscale_run_mask[bit_split & 0x0F];
	uint8_t shift_N = rotscale_run_shift[bit_split & 0x0F];
	uint8_t step_lo = (uint8_t)(ScaleData.eff_scale_y & 0xFF);
	uint8_t step_hi = (uint8_t)(ScaleData.eff_scale_y >> 8);

	while (1) {
		if (*data == 0xFF)
			break;

		/* Compute replay count for this source row: integer delta
		 * between the pre-step and post-step 16-bit fractional state.
		 * Matches the 498ae..49904 instruction sequence. */
		uint16_t pre_state = (uint16_t)((acc_hi << 8) | acc_mid);
		uint8_t tmp_lo = (uint8_t)(acc_lo + step_lo);
		uint8_t tmp_mid = acc_mid;
		uint8_t tmp_hi = acc_hi;
		if (tmp_lo < acc_lo) {
			tmp_mid = (uint8_t)(acc_mid + 1);
			if (acc_mid == 0xFF)
				tmp_hi = (uint8_t)(acc_hi + 1);
		}
		uint8_t tmp_mid_before = tmp_mid;
		tmp_mid = (uint8_t)(tmp_mid + step_hi);
		if (tmp_mid < tmp_mid_before)
			tmp_hi = (uint8_t)(tmp_hi + 1);
		uint16_t post_state = (uint16_t)((tmp_hi << 8) | tmp_mid);
		int16_t row_count = (int16_t)((int32_t)post_state - (int32_t)pre_state);

		/* Parse the row's opcodes into runtable[]. retail's reverseflag
		 * branch only fires when reverseflag == 1; the other path never
		 * enters the inner loop (n_runs stays 0). The function's own
		 * prologue forces reverseflag = 1 so we only model that path. */
		int n_runs = 0;
		int32_t x_cur = 0;
		uint16_t x_frac = 0;
		uint8_t base_color = 0;
		rotscale_run* p_run = &runtable[0];

		while (1) {
			uint8_t op = *data;
			if (op == 0xFE) {
				data++;
				break;
			}

			if (op == 0xFB) {
				base_color = data[1];
				data += 3;
				continue;
			}
			if (op == 0xFC) {
				uint8_t idx = data[1];
				data += 2;
				uint16_t prev = x_frac;
				x_frac = (uint16_t)(x_frac + ScaleData.x_lookup_lo[idx]);
				x_cur += ScaleData.x_lookup_hi[idx];
				if (x_frac < prev)
					x_cur++;
				continue;
			}

			uint8_t run_len;
			uint8_t run_color;
			if (op == 0xFD) {
				run_len = data[1];
				run_color = data[2];
				data += 3;
			} else {
				run_color = (uint8_t)(base_color + (op >> shift_N));
				run_len = (uint8_t)(op & mask_N);
				data += 1;
			}

			int32_t run_x_start = x_cur;
			uint16_t prev_frac = x_frac;
			x_frac = (uint16_t)(x_frac + ScaleData.x_lookup_lo[run_len]);
			x_cur += ScaleData.x_lookup_hi[run_len];
			if (x_frac < prev_frac)
				x_cur++;

			if (n_runs < (int)(sizeof(runtable) / sizeof(runtable[0]))) {
				p_run->x_start = (uint32_t)run_x_start;
				p_run->color = run_color;
				p_run->run_len = (uint32_t)(x_cur - run_x_start + 1);
				p_run++;
				n_runs++;
			}
		}

		/* Replay the parsed row row_count times. */
		int16_t rem = row_count;
		do {
			if (n_runs && lastvispoint >= 0) {
				int32_t first_end = startdrawpoint + x_cur;
				int clipped = (first_end < 0) || (first_end >= lastvispoint) || (startdrawpoint < 0) ||
							  (startdrawpoint < firstvispoint);

				if (!clipped) {
					if (bytesPerPixel == 2) {
						rotscale_run* r = &runtable[0];
						for (int i = 0; i < n_runs; ++i, ++r) {
							uint32_t xs = r->x_start + startdrawpoint;
							uint8_t col = (uint8_t)r->color;
							uint32_t ln = r->run_len;
							while (ln--) {
								uint8_t* plot = pDrawBuffer + pCurrentLine->screen_offset[xs];
								if (plot >= buf_base && plot < buf_end - 1) {
									*plot = col;
									*(plot + 1) = 0x80;
								}
								xs++;
							}
						}
					} else {
						rotscale_run* r = &runtable[0];
						for (int i = 0; i < n_runs; ++i, ++r) {
							uint32_t xs = r->x_start + startdrawpoint;
							uint8_t col = (uint8_t)r->color;
							uint32_t ln = r->run_len;
							while (ln--) {
								uint8_t* plot = pDrawBuffer + pCurrentLine->screen_offset[xs];
								*plot = col;
								xs++;
							}
						}
					}
				} else {
					rotscale_run* r = &runtable[0];
					for (int i = 0; i < n_runs; ++i, ++r) {
						int32_t xs = (int32_t)r->x_start + startdrawpoint;
						uint8_t col = (uint8_t)r->color;
						int32_t ln = (int32_t)r->run_len;
						int32_t xe = xs + ln;
						if (xs < firstvispoint)
							xs = firstvispoint;
						if (xs > lastvispoint)
							continue;
						if (xe < firstvispoint)
							continue;
						if (xe > lastvispoint)
							xe = lastvispoint + 1;
						while (xs < xe) {
							uint8_t* plot = pDrawBuffer + pCurrentLine->screen_offset[xs];
							if (plot >= buf_base && plot < buf_end &&
								(bytesPerPixel != 2 || plot < buf_end - 1)) {
								*plot = col;
								if (bytesPerPixel == 2)
									*(plot + 1) = 0x80;
							}
							xs++;
						}
					}
				}
			}

			if (row_count) {
				if (!updatecases())
					return 0;
				updateperp();
			}
			rem--;
		} while (rem > 0 && row_count);

		/* Commit the fractional step back to (acc_lo, acc_mid, acc_hi).
		 * Retail asm at 0x49d4f..0x49d83: prev > new signals wrap. */
		uint8_t prev_lo = acc_lo;
		acc_lo = (uint8_t)(acc_lo + step_lo);
		if (prev_lo > acc_lo)
			acc_mid = (uint8_t)(acc_mid + 1);
		uint8_t prev_mid = acc_mid;
		acc_mid = (uint8_t)(acc_mid + step_hi);
		if (prev_mid > acc_mid)
			acc_hi = (uint8_t)(acc_hi + 1);
	}
	return 1;
}

/* ===================================================================
 * scantoxtrans - palette-convert plotted pixels and register the sprite
 * as a TRACE2 flat object for depth sorting.
 *
 * Walks the bbox of the 4 corners. In 16-bit mode opaque pixels are
 * those whose +1 byte == 0x80 (the marker rotatescale stamped); the
 * pixel byte is then remapped via paletteconvertlo/paletteconverthi.
 * In 8-bit mode opaque pixels are those with value < 0x10 (the
 * remapped sprite values); they are remapped via paletteconvert.
 *
 * For each contiguous opaque run, two TRACE2 edge entries are pushed
 * into trace2_rowheaders[y] (enter/exit x). The whole bbox is
 * registered as one flat object slot.
 * ================================================================ */
static int16_t scantoxtrans(int32_t* quad_corners) {
	trace2_EdgeInfo* p_einfo = trace2_newedgeinfo;
	trace2_EdgeHeader* p_ehdr = trace2_newedgeheader;
	int32_t max_y = pixelsdeepmin1;

	g_dbg_sx_calls++;

	/* Invert Y of each corner. */
	quad_corners[1] = max_y - quad_corners[1];
	quad_corners[3] = max_y - quad_corners[3];
	quad_corners[5] = max_y - quad_corners[5];
	quad_corners[7] = max_y - quad_corners[7];

	int32_t xmin = quad_corners[0], xmax = quad_corners[0];
	int32_t ymin = quad_corners[1], ymax = quad_corners[1];
	for (int i = 1; i < 4; ++i) {
		int32_t x = quad_corners[2 * i];
		int32_t y = quad_corners[2 * i + 1];
		if (x < xmin)
			xmin = x;
		if (x > xmax)
			xmax = x;
		if (y < ymin)
			ymin = y;
		if (y > ymax)
			ymax = y;
	}
	g_dbg_sx_last_xmin = xmin;
	g_dbg_sx_last_xmax = xmax;
	g_dbg_sx_last_ymin = ymin;
	g_dbg_sx_last_ymax = ymax;
	int32_t ymin_clip = ymin - 2, ymax_pad = ymax + 2;
	int32_t xmin_clip = xmin - 2, xmax_clip = xmax + 2;
	int16_t result = (int16_t)(xmin - 2);
	if (ymax_pad < 0) {
		g_dbg_sx_early_ymax++;
		goto done;
	}
	result = nDrawBufferDepth;
	if (ymin_clip >= nDrawBufferDepth) {
		g_dbg_sx_early_ymin++;
		goto done;
	}
	if (nDrawBufferDepth <= ymax_pad) {
		result = nDrawBufferDepthMin1;
		ymax_pad = nDrawBufferDepthMin1;
	}
	if (ymin_clip < 0)
		ymin_clip = 0;
	if (xmax_clip < 0) {
		g_dbg_sx_early_xmax++;
		goto done;
	}
	result = nDrawBufferWidth;
	if (nDrawBufferWidth <= xmin_clip) {
		g_dbg_sx_early_xmin++;
		goto done;
	}
	if (xmax_clip >= nDrawBufferWidth)
		xmax_clip = nDrawBufferWidthMin1;
	if (xmin_clip < 0)
		xmin_clip = 0;
	int32_t ymax_clip = ymax_pad;

	uint8_t* p_pixel = (uint8_t*)buffer_ptr + ymin_clip * nDrawBufferMemoryWidth + bytesPerPixel * xmin_clip;

	/* Deep diagnostic: for the first sx call of each frame, dump the
	 * first 16 entries of paletteconvert (what prepare_color wrote)
	 * plus a 32-byte sample from the row of buffer_ptr that this call
	 * is about to scan for >=0x40 pixels. Lets us tell whether the
	 * row contains any opaque (>=0x40) pixel at all. */
	{
		static int dbg_sx_prints = 0;

		if (g_dbg_sx_calls == 1 && dbg_sx_prints < 3) {
			dbg_sx_prints++;
			TieDiagnostics_Log(TIE_LOG_INFO,
							   "[sx-deep] ymin=%d ymax=%d xmin=%d xmax=%d "
							   "nDBW=%d nDBD=%d memw=%d buf=%p pixel_off=%ld\n",
							   (int)ymin_clip, (int)ymax_clip, (int)xmin_clip, (int)xmax_clip,
							   (int)nDrawBufferWidth, (int)nDrawBufferDepth, (int)nDrawBufferMemoryWidth,
							   buffer_ptr, (long)(p_pixel - (uint8_t*)buffer_ptr));
			TieDiagnostics_Log(TIE_LOG_INFO, "[sx-deep] paletteconvert[0..15] =");
			for (int k = 0; k < 16; k++)
				TieDiagnostics_Log(TIE_LOG_INFO, " %02X", paletteconvert[k]);
			TieDiagnostics_Log(TIE_LOG_INFO, "\n[sx-deep] row bytes (first 32):");
			for (int k = 0; k < 32 && k < (xmax_clip - xmin_clip); k++)
				TieDiagnostics_Log(TIE_LOG_INFO, " %02X", p_pixel[k]);
			TieDiagnostics_Log(TIE_LOG_INFO, "\n");
		}
	}

	uint16_t obj_id = flatobjnum;
	int32_t row_step = nDrawBufferMemoryWidth - bytesPerPixel * (xmax_clip - xmin_clip);

	flatcolors[flatobjnum] = 0;
	flatcomponentnum[obj_id] = (uint8_t)objectnum;
	flatparentobj[obj_id] = parentobject;
	flatx[obj_id] = (int16_t)(worldx >> 5);
	flaty[obj_id] = (int16_t)(worldy >> 5);
	flatz[obj_id] = (int16_t)(worldz >> 5);

	/* Two paired edge headers/infos for the row span (left + right). */
	p_ehdr->next = trace2_rowheaders[ymin_clip];
	trace2_rowheaders[ymin_clip] = p_ehdr;
	p_ehdr->numscanlines = ymax_clip - ymin_clip;
	++p_ehdr;
	p_ehdr[-1].objectid = obj_id + 128;
	p_ehdr[-1].info = p_einfo;
	p_ehdr[-1].edgeid = layervalue;
	if (p_ehdr > trace2_lastedgeheader)
		p_ehdr = trace2_lastedgeheader;

	trace2_EdgeInfo* p_einfo_walker = p_einfo;
	p_einfo += (ymax_clip - ymin_clip);
	if (p_einfo > trace2_lastedgeinfo)
		p_einfo = trace2_lastedgeinfo;

	p_ehdr->next = trace2_rowheaders[ymin_clip];
	trace2_rowheaders[ymin_clip] = p_ehdr;
	p_ehdr->numscanlines = ymax_clip - ymin_clip;
	++p_ehdr;
	p_ehdr[-1].objectid = flatobjnum + 128;
	p_ehdr[-1].info = p_einfo;
	p_ehdr[-1].edgeid = layervalue;
	if (p_ehdr > trace2_lastedgeheader)
		p_ehdr = trace2_lastedgeheader;

	int32_t* p_x = &p_einfo->x;
	p_einfo += (ymax_clip - ymin_clip);
	if (p_einfo > trace2_lastedgeinfo)
		p_einfo = trace2_lastedgeinfo;

	if (bytesPerPixel == 2) {
		for (int32_t y = ymin_clip; y < ymax_clip; ++y) {
			int32_t x = xmin_clip;
			/* Left edge of first opaque run */
			while (x < xmax_clip && p_pixel[1] != 0x80) {
				p_pixel += 2;
				++x;
			}
			p_einfo_walker->x = x << 8;
			++p_einfo_walker;
			while (x < xmax_clip && p_pixel[1] == 0x80) {
				uint8_t lo = paletteconvertlo[*p_pixel];
				*p_pixel = lo;
				*(p_pixel + 1) = paletteconverthi[lo];
				p_pixel += 2;
				++x;
			}
			*p_x = x << 8;
			p_x += 2;
			/* Additional opaque runs in this row produce singleton-row edges */
			while (x < xmax_clip) {
				while (x < xmax_clip && p_pixel[1] != 0x80) {
					p_pixel += 2;
					++x;
				}
				if (x == xmax_clip)
					break;
				p_ehdr->next = trace2_rowheaders[y];
				trace2_rowheaders[y] = p_ehdr;
				++p_ehdr;
				p_ehdr[-1].numscanlines = 1;
				p_ehdr[-1].objectid = flatobjnum + 128;
				p_ehdr[-1].info = p_einfo;
				p_ehdr[-1].edgeid = layervalue;
				if (p_ehdr > trace2_lastedgeheader)
					p_ehdr = trace2_lastedgeheader;
				trace2_EdgeInfo* e_in = p_einfo + 1;
				p_einfo->x = x << 8;
				if (e_in > trace2_lastedgeinfo)
					e_in = trace2_lastedgeinfo;
				while (x < xmax_clip && p_pixel[1] == 0x80) {
					uint8_t lo = paletteconvertlo[*p_pixel];
					*p_pixel = lo;
					*(p_pixel + 1) = paletteconverthi[lo];
					p_pixel += 2;
					++x;
				}
				p_ehdr->next = trace2_rowheaders[y];
				trace2_rowheaders[y] = p_ehdr;
				++p_ehdr;
				p_ehdr[-1].numscanlines = 1;
				p_ehdr[-1].objectid = flatobjnum + 128;
				p_ehdr[-1].info = e_in;
				p_ehdr[-1].edgeid = layervalue;
				if (p_ehdr > trace2_lastedgeheader)
					p_ehdr = trace2_lastedgeheader;
				p_einfo = e_in + 1;
				e_in->x = x << 8;
				if (p_einfo > trace2_lastedgeinfo)
					p_einfo = trace2_lastedgeinfo;
			}
			p_pixel += row_step;
		}
	} else {
		/* Retail transparency threshold is 0x40 (supports 64 remappable
		 * palette entries); the demo used 0x10 (16 entries). Every
		 * comparison against 0x10 in the original port needs to become
		 * 0x40 to correctly classify retail bitmap bytes. */
		for (int32_t y = ymin_clip; y < ymax_clip; ++y) {
			int32_t x = xmin_clip;
			while (x < xmax_clip && *p_pixel >= 0x40) {
				++p_pixel;
				++x;
			}
			p_einfo_walker->x = x << 8;
			++p_einfo_walker;
			while (x < xmax_clip) {
				uint8_t c = *p_pixel;
				if (c >= 0x40)
					break;
				++p_pixel;
				++x;
				*(p_pixel - 1) = paletteconvert[c];
			}
			*p_x = x << 8;
			p_x += 2;
			while (x < xmax_clip) {
				while (x < xmax_clip && *p_pixel >= 0x40) {
					++p_pixel;
					++x;
				}
				if (x == xmax_clip)
					break;
				p_ehdr->next = trace2_rowheaders[y];
				trace2_rowheaders[y] = p_ehdr;
				++p_ehdr;
				p_ehdr[-1].numscanlines = 1;
				p_ehdr[-1].objectid = flatobjnum + 128;
				p_ehdr[-1].info = p_einfo;
				p_ehdr[-1].edgeid = layervalue;
				if (p_ehdr > trace2_lastedgeheader)
					p_ehdr = trace2_lastedgeheader;
				trace2_EdgeInfo* e_in = p_einfo + 1;
				p_einfo->x = x << 8;
				if (e_in > trace2_lastedgeinfo)
					e_in = trace2_lastedgeinfo;
				while (x < xmax_clip) {
					uint8_t c2 = *p_pixel;
					if (c2 >= 0x40)
						break;
					++p_pixel;
					++x;
					*(p_pixel - 1) = paletteconvert[c2];
				}
				p_ehdr->next = trace2_rowheaders[y];
				trace2_rowheaders[y] = p_ehdr;
				++p_ehdr;
				p_ehdr[-1].numscanlines = 1;
				p_ehdr[-1].objectid = flatobjnum + 128;
				p_ehdr[-1].info = e_in;
				p_ehdr[-1].edgeid = layervalue;
				if (p_ehdr > trace2_lastedgeheader)
					p_ehdr = trace2_lastedgeheader;
				p_einfo = e_in + 1;
				e_in->x = x << 8;
				if (p_einfo > trace2_lastedgeinfo)
					p_einfo = trace2_lastedgeinfo;
			}
			p_pixel += row_step;
		}
	}

	uint16_t next_obj = (uint16_t)(flatobjnum + 1);
	result = (int16_t)next_obj;
	++flatobjnum;
	if (next_obj >= 0x70u)
		flatobjnum = (uint16_t)(next_obj - 1);

	g_dbg_sx_completed++;
done:
	trace2_newedgeheader = p_ehdr;
	trace2_newedgeinfo = p_einfo;
	return result;
}

// FUNCTION: TIE98 0x43DDB0
static void composite_tie98_sprite_raster(uint8_t* pixel, int row_advance, int32_t xmin, int32_t ymin,
										  int32_t xmax, int32_t ymax) {
	const int bytes_per_pixel = g_flight16bppBytesPerPixel;
	const float depth = (float)(uint32_t)perspFactor / (float)objecteyez;
	const int lock_surface = !g_flightSurfaceAlreadyLocked;
	if (lock_surface)
		FlightSurface_Lock();

	for (int32_t y = ymin; y < ymax; ++y) {
		int32_t x = xmin;
		if (bytes_per_pixel == 2) {
			while (x < xmax) {
				while (x < xmax && pixel[1] != 0x80) {
					pixel += 2;
					++x;
				}
				const int32_t run_start = x;
				uint8_t* run_pixels = pixel;
				while (x < xmax && pixel[1] == 0x80) {
					const uint8_t color = *pixel;
					*pixel++ = paletteconvertlo[color];
					*pixel++ = paletteconverthi[color];
					++x;
				}
				if (run_start < x)
					sw3d_BlitOccludedSpan(run_pixels, run_start, x, y, depth);
			}
		} else {
			while (x < xmax) {
				while (x < xmax && *pixel >= 0x40) {
					++pixel;
					++x;
				}
				const int32_t run_start = x;
				uint8_t* run_pixels = pixel;
				while (x < xmax && *pixel < 0x40) {
					*pixel = paletteconvert[*pixel];
					++pixel;
					++x;
				}
				if (run_start < x)
					sw3d_BlitOccludedSpan(run_pixels, run_start, x, y, depth);
			}
		}
		pixel += row_advance;
	}

	if (lock_surface)
		FlightSurface_Unlock();
}

/* TIE98 replaces TIE95's TRACE2 edge registration with depth-aware
 * compositing against the software renderer's scene spans. */
// FUNCTION: TIE98 0x476340
static int16_t composite_to_tie98_scene(int32_t* quad_corners) {
	const int32_t max_y = pixelsdeepmin1;
	quad_corners[1] = max_y - quad_corners[1];
	quad_corners[3] = max_y - quad_corners[3];
	quad_corners[5] = max_y - quad_corners[5];
	quad_corners[7] = max_y - quad_corners[7];

	int32_t xmin = quad_corners[0];
	int32_t xmax = quad_corners[0];
	int32_t ymin = quad_corners[1];
	int32_t ymax = quad_corners[1];
	for (int index = 1; index < 4; ++index) {
		const int32_t x = quad_corners[2 * index];
		const int32_t y = quad_corners[2 * index + 1];
		if (x < xmin)
			xmin = x;
		if (x > xmax)
			xmax = x;
		if (y < ymin)
			ymin = y;
		if (y > ymax)
			ymax = y;
	}

	xmin -= 2;
	xmax += 2;
	ymin -= 2;
	ymax += 2;
	if (ymax < 0 || ymin >= nDrawBufferDepth || xmax < 0 || xmin >= nDrawBufferWidth)
		return 0;
	if (ymax >= nDrawBufferDepth)
		ymax = nDrawBufferDepthMin1;
	if (ymin < 0)
		ymin = 0;
	if (xmax >= nDrawBufferWidth)
		xmax = nDrawBufferWidthMin1;
	if (xmin < 0)
		xmin = 0;

	const int bytes_per_pixel = g_flight16bppBytesPerPixel;
	uint8_t* pixel = (uint8_t*)buffer_ptr + ymin * nDrawBufferMemoryWidth + bytes_per_pixel * xmin;
	const int row_advance = nDrawBufferMemoryWidth - bytes_per_pixel * (xmax - xmin);
	composite_tie98_sprite_raster(pixel, row_advance, xmin, ymin, xmax, ymax);
	return 0;
}

/* ===================================================================
 * rotatescaleimage - public entry point.
 *
 * Retail (ROTSCALE_rotatescaleimage @ 0x48530) parses the sub-header
 * that image_hdr points into via a relative offset at image_hdr[+8]:
 *
 *   sub = image_hdr + *(u32 *)(image_hdr + 8)
 *     sub[+0 word]  : celoffsetx for reverseflag == 1 (top-left x)
 *     sub[+4 word]  : celoffsety (negated to get image-top anchor)
 *     sub[+8 word]  : celoffsetx for reverseflag != 1 (top-right x, negated)
 *     sub+0x10      : start of RLE data passed to rotatescale()
 *
 *   image_hdr[+0x10 word] : sprite width  (added to anchor for right edge)
 *   image_hdr[+0x14 word] : sprite height (subtracted for bottom edge; y
 *                           is flipped by the edition-specific completion scan)
 *   image_hdr[+0x20 dword]: bit_split parameter forwarded to rotatescale
 *
 * The four adjustoffsets calls produce the 4 screen-space corners of
 * the rotated bounding box (top-left, top-right, bottom-right,
 * bottom-left) that scantoxtrans uses to walk the sprite region.
 * ================================================================ */
int16_t rotscale_rotate_scale_image(int16_t screen_x, int16_t screen_y, uint16_t scale,
									const uint8_t* image_hdr) {
	uint32_t sub_off = *(const uint32_t*)(image_hdr + 8);
	const uint8_t* sub = image_hdr + sub_off;

	int16_t cox0;
	if (reverseflag == 1)
		cox0 = *(const int16_t*)(sub + 0);
	else
		cox0 = (int16_t)-(*(const int16_t*)(sub + 8));
	int16_t coy0 = (int16_t)-(*(const int16_t*)(sub + 4));

	int16_t sprite_w = *(const int16_t*)(image_hdr + 0x10);
	int16_t sprite_h = *(const int16_t*)(image_hdr + 0x14);
	uint32_t bit_split = *(const uint32_t*)(image_hdr + 0x20);

	celoffsetx = cox0;
	celoffsety = coy0;

	scalesetup(scale, pCurrentLine, &ScaleData);

	int32_t quad[8];

	/* Corner 0: top-left of sprite at (cox0, coy0). */
	adjustoffsets(pCurrentLine, &ScaleData);
	plotx = (int16_t)(adjustplotx + screen_x);
	ploty = (int16_t)(adjustploty + screen_y);
	quad[0] = (int16_t)(adjustplotx + screen_x);
	quad[1] = (int16_t)(adjustploty + screen_y);

	rotatescale(sub + 0x10, (int32_t)bit_split);

	/* Corner 1: top-right, x += sprite_w. */
	celoffsetx = (int16_t)(cox0 + sprite_w);
	celoffsety = coy0;
	adjustoffsets(pCurrentLine, &ScaleData);
	quad[2] = screen_x + adjustplotx;
	quad[3] = screen_y + adjustploty;

	/* Corner 2: bottom-right. */
	celoffsetx = (int16_t)(cox0 + sprite_w);
	celoffsety = (int16_t)(coy0 - sprite_h);
	adjustoffsets(pCurrentLine, &ScaleData);
	quad[4] = screen_x + adjustplotx;
	quad[5] = screen_y + adjustploty;

	/* Corner 3: bottom-left. */
	celoffsetx = cox0;
	celoffsety = (int16_t)(coy0 - sprite_h);
	adjustoffsets(pCurrentLine, &ScaleData);
	quad[6] = adjustplotx + screen_x;
	quad[7] = adjustploty + screen_y;

	if (TieProfile_UsesTie98Logic())
		return composite_to_tie98_scene(quad);
	return scantoxtrans(quad);
}
