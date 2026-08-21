/* Extracts an LFD FONT into an upscaled RGBA atlas and TFNT metrics. Packing
 * occurs before whole-atlas scaling so glyph metrics share the same transform. */

#include "imgbake/png_write.h"
#include "imgbake/upscale.h"
#include "lfd_file.h"
#include "tie_formats/common.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ATLAS_COLS 16
/* One transparent source row between glyph cell rows so the Lanczos
 * vertical pass (kernel radius ~3 mid rows ≈ 0.27 source rows) can't
 * reach across cell-row boundaries. Without this, runtime bilinear
 * sampling at the bottom UV of a glyph cell pulls in cross-cell content
 * baked in by the upscaler, showing as a thin line under each glyph. */
#define ROW_GUTTER 1
#define FNT_MAGIC 0x544E4654u /* 'TFNT' little-endian */
#define FNT_VERSION 2

/* Binary metrics file layout (little-endian, packed):
 *   u32 magic         ('TFNT')
 *   u16 version       (2)
 *   u16 first_char
 *   u16 num_chars
 *   u16 atlas_w       // POST-upscale, matches PNG dims
 *   u16 atlas_h       // POST-upscale
 *   u16 cell_w        // canonical cell width = src_width * 9
 *   u16 cell_h        // canonical cell height = scale_y_4k(src_height)
 *   u16 baseline      // POST-upscale baseline from cell top
 *   u16 _pad
 *   per-glyph (num_chars records, 10 bytes each):
 *     u16 atlas_x     // POST-upscale top-left in PNG (scale_y non-additive
 *     u16 atlas_y     //  so cell_y is per-glyph, not (g/cols)*cell_h)
 *     u16 atlas_w     // actual sub-rect width in atlas (== cell_w)
 *     u16 atlas_h     // actual sub-rect height (== cell_h ± 1)
 *     u16 advance     // POST-upscale advance (src_advance * 9)
 */

static void TieFontExtract_Die(const char* msg) {
	fprintf(stderr, "font_extract: %s\n", msg);
	exit(1);
}

static uint16_t TieFontExtract_ReadU16Le(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }

int main(int argc, char** argv) {
	if (argc < 4) {
		fprintf(stderr,
				"usage: %s <empire.lfd> <font_name> <out_basename>\n"
				"  e.g. %s tie-collector/RESOURCE/EMPIRE.LFD font8 subtitle\n",
				argv[0], argv[0]);
		return 2;
	}
	const char* lfd_path = argv[1];
	const char* font_name = argv[2];
	const char* out_base = argv[3];

	TieLfdFile lfd;
	char lfd_error[512];
	if (!TieLfdFile_Open(&lfd, lfd_path, lfd_error, sizeof lfd_error))
		TieFontExtract_Die(lfd_error);

	const TieLfdFileEntry* e = TieLfdFile_Find(&lfd, TIE_FOURCC('F', 'O', 'N', 'T'), font_name);
	if (!e)
		TieFontExtract_Die("font resource not found in LFD");

	const uint8_t* p = TieLfdFile_Data(&lfd, e);
	if (e->size < 12)
		TieFontExtract_Die("FONT payload too small for header");

	uint16_t first_char = TieFontExtract_ReadU16Le(p + 0);
	uint16_t num_chars = TieFontExtract_ReadU16Le(p + 2);
	uint16_t width = TieFontExtract_ReadU16Le(p + 4);
	uint16_t height = TieFontExtract_ReadU16Le(p + 6);
	uint16_t baseline = TieFontExtract_ReadU16Le(p + 8);
	uint16_t is_color = TieFontExtract_ReadU16Le(p + 10);
	if (is_color)
		TieFontExtract_Die("isColor=1 fonts not supported (font8 should be 1bpp)");
	if (width % 8)
		TieFontExtract_Die("FONT width must be a multiple of 8");

	uint32_t bytes_per_row = (uint32_t)width / 8;
	uint32_t char_size = bytes_per_row * (uint32_t)height;
	uint32_t need = 12u + (uint32_t)num_chars + (uint32_t)num_chars * char_size;
	if (e->size < need)
		TieFontExtract_Die("FONT payload truncated");

	const uint8_t* width_array = p + 12;
	const uint8_t* glyph_data = width_array + num_chars;

	/* Step 1: rasterize 1bpp glyphs into an RGBA grid at SOURCE
	 * resolution. Each cell is `width × height`, laid out in a
	 * 16-column grid. Alpha = 0 or 255; RGB = 255 throughout. We add
	 * ROW_GUTTER transparent rows after each cell row so the Lanczos
	 * vertical pass can't bleed adjacent-cell content across
	 * boundaries (last cell row gets a trailing gutter too — keeps
	 * the per-cell stride uniform and the metrics math clean). */
	uint32_t cols = ATLAS_COLS;
	uint32_t rows = (num_chars + cols - 1) / cols;
	uint32_t cell_stride_y = (uint32_t)height + ROW_GUTTER;
	int src_w = (int)(cols * (uint32_t)width);
	int src_h = (int)(rows * cell_stride_y);
	uint8_t* rgba = calloc((size_t)src_w * (size_t)src_h * 4, 1);
	if (!rgba)
		TieFontExtract_Die("out of memory");

	for (uint32_t g = 0; g < num_chars; g++) {
		const uint8_t* src = glyph_data + g * char_size;
		uint32_t cell_x = (g % cols) * width;
		uint32_t cell_y = (g / cols) * cell_stride_y;
		for (uint32_t row = 0; row < height; row++) {
			const uint8_t* srow = src + row * bytes_per_row;
			uint8_t* drow = rgba + ((cell_y + row) * (uint32_t)src_w + cell_x) * 4;
			for (uint32_t x = 0; x < (uint32_t)width; x++) {
				uint8_t bit = (srow[x >> 3] >> (7 - (x & 7))) & 1;
				uint8_t a = bit ? 255 : 0;
				drow[x * 4 + 0] = 255;
				drow[x * 4 + 1] = 255;
				drow[x * 4 + 2] = 255;
				drow[x * 4 + 3] = a;
			}
		}
	}

	/* Step 2: VGA → 4K upscale (9× horizontal NN, vertical Lanczos-3
	 * at 54/5). Replaces rgba with a fresh buffer at upscaled dims. */
	int atlas_w = src_w;
	int atlas_h = src_h;
	if (!atlas_vga_to_4k(&rgba, &atlas_w, &atlas_h))
		TieFontExtract_Die("upscale failed (out of memory?)");

	/* Step 2b: convert to premultiplied alpha for the PMA-blend cutscene
	 * pipeline. Source is (255, 255, 255, A) so PMA (R*A/255 etc.) just
	 * becomes (A, A, A, A). The runtime PMA blend then composites
	 * tinted glyphs onto the cutscene RT cleanly at edges. */
	{
		uint8_t* q = rgba;
		size_t n = (size_t)atlas_w * (size_t)atlas_h;
		for (size_t i = 0; i < n; i++) {
			uint8_t a = q[3];
			q[0] = a;
			q[1] = a;
			q[2] = a;
			q += 4;
		}
	}

	/* Step 3: derive POST-upscale per-glyph metrics. The upscaler is
	 * grid-aligned (every src column → 9 atlas cols; src row scaling
	 * via scale_y_4k), so we can compute cell origins exactly without
	 * referring to the actual pixel data. */
	uint32_t cell_w_out = (uint32_t)width * SCALE_X_4K;
	uint32_t cell_h_out = (uint32_t)scale_y_4k((int)height);
	/* Convention: `baseline` is the row INDEX where the baseline line
	 * is, with caps' last ink row at baseline-1 (matches stbtt /
	 * font_atlas_build). The source FONT header stores the source-row
	 * index of the baseline ROW — caps fill that row to its bottom,
	 * so the line itself sits at the bottom of source row `baseline`,
	 * = top of source row `baseline + 1`. Hence the +1 here. Without
	 * it the stored value lands at the TOP of the baseline row, ~10
	 * atlas-px above where caps actually rest, and any consumer that
	 * draws relative to it (font_tune overlay) renders the line above
	 * the visual baseline. */
	uint32_t baseline_out = (uint32_t)scale_y_4k((int)baseline + 1);
	if ((uint32_t)atlas_w > 0xFFFFu || (uint32_t)atlas_h > 0xFFFFu)
		TieFontExtract_Die("atlas too large for 16-bit metrics");

	/* Write PNG. */
	char path[1024];
	snprintf(path, sizeof(path), "%s.png", out_base);
	if (!write_png_rgba(path, atlas_w, atlas_h, rgba))
		TieFontExtract_Die("PNG write failed");
	free(rgba);

	/* Write metrics. */
	snprintf(path, sizeof(path), "%s.fnt", out_base);
	FILE* fp = fopen(path, "wb");
	if (!fp)
		TieFontExtract_Die("metrics fopen failed");

	uint8_t hdr[24];
	memset(hdr, 0, sizeof(hdr));
	uint32_t magic = FNT_MAGIC;
	memcpy(hdr + 0, &magic, 4);
	uint16_t v;
	v = FNT_VERSION;
	memcpy(hdr + 4, &v, 2);
	v = first_char;
	memcpy(hdr + 6, &v, 2);
	v = num_chars;
	memcpy(hdr + 8, &v, 2);
	v = (uint16_t)atlas_w;
	memcpy(hdr + 10, &v, 2);
	v = (uint16_t)atlas_h;
	memcpy(hdr + 12, &v, 2);
	v = (uint16_t)cell_w_out;
	memcpy(hdr + 14, &v, 2);
	v = (uint16_t)cell_h_out;
	memcpy(hdr + 16, &v, 2);
	v = (uint16_t)baseline_out;
	memcpy(hdr + 18, &v, 2);
	/* hdr+20..23 = pad already zeroed */
	if (fwrite(hdr, 1, 24, fp) != 24)
		TieFontExtract_Die("metrics header write failed");

	/* Compute per-glyph atlas (x, y, w, h). Cell row stride in source
	 * is `cell_stride_y` (= height + ROW_GUTTER); the visible content
	 * is the first `height` rows of that span, so atlas_h covers
	 * scale_y_4k(row*stride) .. scale_y_4k(row*stride + height). The
	 * gutter rows live BETWEEN those bounds in the upscaled atlas but
	 * are excluded from the per-glyph rect.
	 *
	 * Y boundary math goes through scale_y_4k of source rows (NOT a
	 * multiplied cell_h_out), because scale_y_4k is round-to-nearest
	 * and not additive. */
	for (uint32_t g = 0; g < num_chars; g++) {
		uint8_t rec[10];
		uint32_t row = g / cols;
		uint32_t col = g % cols;
		uint32_t row_top = row * cell_stride_y;
		uint32_t row_bottom = row * cell_stride_y + (uint32_t)height;
		uint16_t ax = (uint16_t)(col * cell_w_out);
		uint16_t ay = (uint16_t)scale_y_4k((int)row_top);
		uint16_t ay_next = (uint16_t)scale_y_4k((int)row_bottom);
		uint16_t aw = (uint16_t)cell_w_out;
		uint16_t ah = (uint16_t)(ay_next - ay);
		uint16_t adv = (uint16_t)((uint32_t)width_array[g] * SCALE_X_4K);
		memcpy(rec + 0, &ax, 2);
		memcpy(rec + 2, &ay, 2);
		memcpy(rec + 4, &aw, 2);
		memcpy(rec + 6, &ah, 2);
		memcpy(rec + 8, &adv, 2);
		if (fwrite(rec, 1, 10, fp) != 10)
			TieFontExtract_Die("metrics record write failed");
	}
	fclose(fp);

	fprintf(stderr,
			"font_extract: '%s' first=%u count=%u\n"
			"             src cell=%ux%u  atlas cell=%ux%u  baseline=%u\n"
			"             atlas=%dx%d  wrote %s.png, %s.fnt\n",
			font_name, first_char, num_chars, width, height, cell_w_out, cell_h_out, baseline_out, atlas_w,
			atlas_h, out_base, out_base);

	TieLfdFile_Close(&lfd);
	return 0;
}
