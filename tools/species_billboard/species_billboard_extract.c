/*
 * species_billboard_extract — extract species billboard sprites from
 * a SPECIES.LFD into (PNG, YAML, KTX2) atlas triples.
 *
 * Source: every XACT chunk in the LFD (debris1..4, planet*, star*,
 * galaxy/cluster, xplo*, nxpl*, surfhi*, spark*, ember*, hit*, zap*).
 * Each XACT carries its own 24-bit RGB palette inside the blob — the
 * engine quantises it to vgapalette[0x40..0xFF] at load time, but for
 * extraction we read the original RGB directly. Output PNGs are
 * therefore independent of any in-game VGA palette state (no cockpit
 * PLTT or VGA.PAC needed).
 *
 * Per-frame layout uses the same atlas_pack helper filmextract --atlas
 * uses, so consumers (the in-tree AeronSpriteAtlas loader, filmview's atlas
 * editor) parse the YAML unchanged.
 */

#include "aeron/atlas_pack.h"
#include "imgbake/anim.h"
#include "imgbake/atlas_pack.h"
#include "imgbake/ktx2_writer.h"
#include "imgbake/png_write.h"
#include "imgbake/upscale.h"
#include "lfd_file.h"
#include "species_bb.h"
#include "tie_formats/common.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define SPECIES_FOURCC_XACT TIE_FOURCC('X', 'A', 'C', 'T')

static void TieSpeciesBillboardExtract_WriteKtx2Sibling(const char* png_path, int w, int h,
														const uint8_t* rgba) {
	size_t n = strlen(png_path);
	if (n < 4 || strcmp(png_path + n - 4, ".png") != 0)
		return;
	char ktx2_path[1600];
	snprintf(ktx2_path, sizeof ktx2_path, "%.*s.ktx2", (int)(n - 4), png_path);
	/* Species billboards are palette-colour artwork (sRGB-authored). */
	(void)write_ktx2_bc7_with_generated_mips(ktx2_path, w, h, rgba, KTX2_BC7_QUALITY_FAST, KTX2_TF_SRGB,
											 /*zstd=*/true);
}

/* Map decoded Image8 pixels to RGBA using the per-frame embedded
 * palette. SPECIES_BB_TRANSPARENT pixels emit alpha=0; everything else
 * is opaque with R/G/B from `pal->rgb[idx]`. */
static uint8_t* TieSpeciesBillboardExtract_RenderRgba(const Image8* img,
													  const TieSpeciesBillboardPalette* pal) {
	size_t px = (size_t)img->width * (size_t)img->height;
	uint8_t* rgba = (uint8_t*)malloc(px * 4);
	if (!rgba)
		return NULL;
	for (size_t i = 0; i < px; ++i) {
		uint8_t idx = img->pixels[i];
		if (idx == SPECIES_BB_TRANSPARENT || idx >= (uint8_t)pal->count) {
			rgba[i * 4 + 0] = 0;
			rgba[i * 4 + 1] = 0;
			rgba[i * 4 + 2] = 0;
			rgba[i * 4 + 3] = 0;
			continue;
		}
		rgba[i * 4 + 0] = pal->rgb[idx][0];
		rgba[i * 4 + 1] = pal->rgb[idx][1];
		rgba[i * 4 + 2] = pal->rgb[idx][2];
		rgba[i * 4 + 3] = 255;
	}
	return rgba;
}

static void TieSpeciesBillboardExtract_EnsureDirectory(const char* path) { mkdir(path, 0755); }

static void TieSpeciesBillboardExtract_SanitizeName(char* s) {
	for (; *s; ++s) {
		unsigned char c = (unsigned char)*s;
		if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))
			continue;
		if (c >= 'a' && c <= 'z') {
			*s = (char)(c - 32);
			continue;
		}
		*s = '_';
	}
}

static bool TieSpeciesBillboardExtract_ExtractOne(const TieLfdFile* lfd, const TieLfdFileEntry* e,
												  const char* out_dir, bool scale, bool svga_mode) {
	const uint8_t* blob = TieLfdFile_Data(lfd, e);

	AnimImage anim = { 0 };
	TieSpeciesBillboardPalette* pals = NULL;
	char err[256] = { 0 };
	if (!TieSpeciesBillboard_Decode(&anim, &pals, blob, e->size, err, sizeof err)) {
		fprintf(stderr, "  %s: decode failed: %s\n", e->name, err);
		return false;
	}

	AtlasPack pack;
	if (!atlas_pack_compute(&anim, &pack)) {
		fprintf(stderr, "  %s: atlas_pack failed\n", e->name);
		free(pals);
		TieSpeciesBillboard_Free(&anim);
		return false;
	}
	int atlas_w = pack.classic_atlas_w;
	int atlas_h = pack.classic_atlas_h;
	uint8_t* atlas = (uint8_t*)calloc((size_t)atlas_w * (size_t)atlas_h * 4, 1);
	if (!atlas) {
		atlas_pack_free(&pack);
		free(pals);
		TieSpeciesBillboard_Free(&anim);
		return false;
	}
	for (int i = 0; i < anim.count; ++i) {
		const Image8* img = &anim.frames[i];
		if (!img->pixels)
			continue;
		uint8_t* rgba = TieSpeciesBillboardExtract_RenderRgba(img, &pals[i]);
		if (!rgba)
			continue;
		int ax = pack.ax[i], ay = pack.ay[i];
		if (!Aeron_AtlasBlitRgba8(atlas, atlas_w, atlas_h, rgba, img->width, img->height, ax, ay,
								  IMGBAKE_ATLAS_GUTTER, AERON_ATLAS_ADDRESS_CLAMP)) {
			free(rgba);
			free(atlas);
			atlas_pack_free(&pack);
			free(pals);
			TieSpeciesBillboard_Free(&anim);
			return false;
		}
		free(rgba);
	}

	int out_w = atlas_w, out_h = atlas_h;
	if (scale) {
		bool ok = svga_mode ? atlas_svga_to_4k(&atlas, &atlas_w, &atlas_h)
							: atlas_vga_to_4k(&atlas, &atlas_w, &atlas_h);
		if (!ok) {
			fprintf(stderr, "  %s: upscale failed\n", e->name);
			free(atlas);
			atlas_pack_free(&pack);
			free(pals);
			TieSpeciesBillboard_Free(&anim);
			return false;
		}
		out_w = atlas_w;
		out_h = atlas_h;
	}

	int padded_w = (out_w + 3) & ~3;
	int padded_h = (out_h + 3) & ~3;
	if (padded_w != out_w || padded_h != out_h) {
		uint8_t* padded = (uint8_t*)calloc((size_t)padded_w * (size_t)padded_h * 4u, 1);
		if (!padded) {
			free(atlas);
			atlas_pack_free(&pack);
			free(pals);
			TieSpeciesBillboard_Free(&anim);
			return false;
		}
		for (int y = 0; y < out_h; ++y) {
			memcpy(padded + (size_t)y * (size_t)padded_w * 4u, atlas + (size_t)y * (size_t)out_w * 4u,
				   (size_t)out_w * 4u);
		}
		free(atlas);
		atlas = padded;
		out_w = padded_w;
		out_h = padded_h;
		atlas_w = padded_w;
		atlas_h = padded_h;
	}

	char safe[16];
	snprintf(safe, sizeof safe, "%s", e->name);
	TieSpeciesBillboardExtract_SanitizeName(safe);

	char png_path[1536], yaml_path[1536];
	snprintf(png_path, sizeof png_path, "%s/%s.png", out_dir, safe);
	snprintf(yaml_path, sizeof yaml_path, "%s/%s.yaml", out_dir, safe);

	bool png_ok = write_png_rgba(png_path, out_w, out_h, atlas);
	if (png_ok)
		TieSpeciesBillboardExtract_WriteKtx2Sibling(png_path, out_w, out_h, atlas);
	free(atlas);

	char yerr[256] = { 0 };
	if (!atlas_emit_yaml(yaml_path, &anim, &pack, out_w, out_h, scale, svga_mode, yerr, sizeof yerr)) {
		fprintf(stderr, "  %s: yaml write failed: %s\n", e->name, yerr);
	}

	fprintf(stdout, "  %-12s %2d frames -> %s (%dx%d)\n", e->name, anim.count, png_path, out_w, out_h);

	atlas_pack_free(&pack);
	free(pals);
	TieSpeciesBillboard_Free(&anim);
	return png_ok;
}

static void TieSpeciesBillboardExtract_Usage(const char* argv0) {
	fprintf(stderr,
			"Usage: %s <species.lfd> <out_dir> [--scale [--svga]]\n"
			"\n"
			"Extracts every XACT resource from species.lfd into an\n"
			"(.png, .yaml, .ktx2) atlas triple under <out_dir>. Colours\n"
			"come from each XACT's embedded 24-bit RGB palette — no VGA.PAC\n"
			"or cockpit-PLTT input needed.\n",
			argv0);
}

int main(int argc, char** argv) {
	if (argc < 3) {
		TieSpeciesBillboardExtract_Usage(argv[0]);
		return 1;
	}
	const char* species_lfd_path = argv[1];
	const char* out_dir = argv[2];

	bool scale = false, svga_mode = false;
	for (int i = 3; i < argc; ++i) {
		if (!strcmp(argv[i], "--scale"))
			scale = true;
		else if (!strcmp(argv[i], "--svga"))
			svga_mode = true;
		else {
			TieSpeciesBillboardExtract_Usage(argv[0]);
			return 1;
		}
	}

	TieSpeciesBillboardExtract_EnsureDirectory(out_dir);

	TieLfdFile sp;
	char lfd_error[512];
	if (!TieLfdFile_Open(&sp, species_lfd_path, lfd_error, sizeof lfd_error)) {
		fprintf(stderr, "%s\n", lfd_error);
		return 1;
	}
	int n_ok = 0, n_skip = 0;
	for (uint32_t i = 0; i < sp.count; ++i) {
		const TieLfdFileEntry* e = &sp.entries[i];
		if (e->type != SPECIES_FOURCC_XACT) {
			++n_skip;
			continue;
		}
		if (TieSpeciesBillboardExtract_ExtractOne(&sp, e, out_dir, scale, svga_mode))
			++n_ok;
	}
	fprintf(stdout, "extracted %d XACT atlases (%d non-XACT chunks skipped)\n", n_ok, n_skip);

	TieLfdFile_Close(&sp);
	return 0;
}
