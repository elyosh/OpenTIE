/*
 * bc7enc — standalone PNG → BC7 KTX2 converter for the art team.
 *
 * Decodes any RGBA8 PNG with stb_image, generates a 2× box-downsample
 * mip chain, encodes each level to BC7 with the vendored bc7enc, and
 * writes a KTX2 file. Useful for ad-hoc conversion outside the
 * filmextract pipeline (e.g. checking how a hand-painted backdrop
 * looks once block-compressed).
 *
 * Usage: bc7enc <input.png> <output.ktx2> [--quality {fast|med|uber}]
 *               [--no-mips]
 */

#include "imgbake/bc7_codec.h"
#include "imgbake/ktx2_writer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Host stb_image's implementation locally while consuming Aeron's vendor. */
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include "stb_image.h"

static void TieBc7Encoder_Usage(void) {
	fprintf(stderr, "Usage: bc7enc <input.png> <output.ktx2> [options]\n"
					"  --linear  Tag the KTX2 as linear data (use for masks /\n"
					"            coverage / non-colour). Default is sRGB so the\n"
					"            runtime HW decodes palette-colour artwork to\n"
					"            linear at sample time.\n"
					"\n"
					"Decodes <input.png> as RGBA8, encodes to BC7 KTX2 with a\n"
					"generated mip chain, writes to <output.ktx2>.\n"
					"\n"
					"Options:\n"
					"  --quality {fast|med|uber}\n"
					"            BC7 encoder preset (default: fast). fast ≈ ms/block,\n"
					"            uber ≈ seconds/block, near-lossless.\n"
					"  --no-mips Skip mip-chain generation; emit a single base level.\n"
					"  --rgba    Emit uncompressed RGBA8 KTX2 instead of BC7 (debug).\n"
					"  --no-zstd Skip zstd supercompression on the level data.\n"
					"  -h, --help  Show this help.\n");
	exit(2);
}

/* Premultiply RGB by alpha into a fresh malloc'd buffer (caller frees).
 * Mirrors the helper inside ktx2_writer.c — duplicated here because
 * --no-mips bypasses the with_generated_mips path that handles it
 * internally. Same rule applies: KTX2 levels must be PMA so the
 * runtime's premultiplied-alpha blend produces correct compositing. */
static uint8_t* TieBc7encCli_PremultiplyDup(const uint8_t* rgba, int w, int h) {
	size_t n = (size_t)w * (size_t)h;
	uint8_t* out = (uint8_t*)malloc(n * 4u);
	if (!out)
		return NULL;
	for (size_t i = 0; i < n; i++) {
		uint8_t a = rgba[i * 4u + 3];
		out[i * 4u + 0] = (uint8_t)((rgba[i * 4u + 0] * a + 127) / 255);
		out[i * 4u + 1] = (uint8_t)((rgba[i * 4u + 1] * a + 127) / 255);
		out[i * 4u + 2] = (uint8_t)((rgba[i * 4u + 2] * a + 127) / 255);
		out[i * 4u + 3] = a;
	}
	return out;
}

/* Single-level KTX2 writer wrapping the existing helpers — we need
 * this only when --no-mips is set. The general writers always
 * generate a chain. Using the public Ktx2WriteLevel struct keeps the
 * encoding logic identical for the single-level case. */
static bool TieBc7encCli_WriteSingleLevel(const char* path, int w, int h, const uint8_t* rgba, bool bc7,
										  Ktx2Bc7Quality q, Ktx2TransferFn tf, bool zstd) {
	uint8_t* pma = TieBc7encCli_PremultiplyDup(rgba, w, h);
	if (!pma)
		return false;

	bool ok = false;
	if (!bc7) {
		Ktx2WriteLevel l = {
			.width = w,
			.height = h,
			.data = pma,
			.size = (size_t)w * (size_t)h * 4u,
		};
		ok = write_ktx2_rgba_mips(path, &l, 1, tf, zstd);
	} else {
		bc7_codec_init();
		Bc7Quality bcq = (q == KTX2_BC7_QUALITY_UBER)  ? BC7_QUALITY_UBER
						 : (q == KTX2_BC7_QUALITY_MED) ? BC7_QUALITY_MED
													   : BC7_QUALITY_FAST;
		size_t bc_bytes = bc7_codec_image_size(w, h);
		uint8_t* bc7_data = (uint8_t*)malloc(bc_bytes);
		if (bc7_data) {
			size_t got = bc7_codec_encode_image(bc7_data, bc_bytes, pma, w, h, bcq);
			if (got == bc_bytes) {
				Ktx2WriteLevel l = {
					.width = w,
					.height = h,
					.data = bc7_data,
					.size = bc_bytes,
				};
				ok = write_ktx2_bc7_mips(path, &l, 1, tf, zstd);
			}
			free(bc7_data);
		}
	}
	free(pma);
	return ok;
}

int main(int argc, char** argv) {
	const char* in_path = NULL;
	const char* out_path = NULL;
	Ktx2Bc7Quality quality = KTX2_BC7_QUALITY_FAST;
	bool gen_mips = true;
	bool emit_rgba = false;
	bool zstd = true;                 /* --no-zstd to skip supercompression */
	Ktx2TransferFn tf = KTX2_TF_SRGB; /* --linear to override */

	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--quality") == 0 && i + 1 < argc) {
			const char* q = argv[++i];
			if (strcmp(q, "fast") == 0)
				quality = KTX2_BC7_QUALITY_FAST;
			else if (strcmp(q, "med") == 0)
				quality = KTX2_BC7_QUALITY_MED;
			else if (strcmp(q, "uber") == 0)
				quality = KTX2_BC7_QUALITY_UBER;
			else {
				fprintf(stderr, "bc7enc: unknown quality '%s'\n", q);
				TieBc7Encoder_Usage();
			}
		} else if (strcmp(argv[i], "--no-mips") == 0) {
			gen_mips = false;
		} else if (strcmp(argv[i], "--no-zstd") == 0) {
			zstd = false;
		} else if (strcmp(argv[i], "--rgba") == 0) {
			emit_rgba = true;
		} else if (strcmp(argv[i], "--linear") == 0) {
			tf = KTX2_TF_LINEAR;
		} else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
			TieBc7Encoder_Usage();
		} else if (argv[i][0] == '-') {
			fprintf(stderr, "bc7enc: unknown option '%s'\n", argv[i]);
			TieBc7Encoder_Usage();
		} else if (!in_path) {
			in_path = argv[i];
		} else if (!out_path) {
			out_path = argv[i];
		} else {
			TieBc7Encoder_Usage();
		}
	}
	if (!in_path || !out_path)
		TieBc7Encoder_Usage();

	int w = 0, h = 0, comp = 0;
	uint8_t* rgba = stbi_load(in_path, &w, &h, &comp, 4);
	if (!rgba) {
		fprintf(stderr, "bc7enc: decode %s failed: %s\n", in_path, stbi_failure_reason());
		return 1;
	}
	fprintf(stderr, "bc7enc: %dx%d %s, %s, %s mips\n", w, h, emit_rgba ? "RGBA8" : "BC7",
			quality == KTX2_BC7_QUALITY_UBER  ? "uber"
			: quality == KTX2_BC7_QUALITY_MED ? "med"
											  : "fast",
			gen_mips ? "with" : "without");

	bool ok;
	if (gen_mips) {
		if (emit_rgba)
			ok = write_ktx2_rgba_with_generated_mips(out_path, w, h, rgba, tf, zstd);
		else
			ok = write_ktx2_bc7_with_generated_mips(out_path, w, h, rgba, quality, tf, zstd);
	} else {
		ok = TieBc7encCli_WriteSingleLevel(out_path, w, h, rgba, !emit_rgba, quality, tf, zstd);
	}

	stbi_image_free(rgba);
	if (!ok) {
		fprintf(stderr, "bc7enc: write %s failed\n", out_path);
		return 1;
	}
	fprintf(stderr, "bc7enc: wrote %s\n", out_path);
	return 0;
}
