/*
 * starfield — CLI baker for the procedural HDR star cubemap.
 *
 * Fills a TieStarfieldParams (from defaults, an optional --preset
 * .tune.yaml, then flag overrides), renders the 6 cube faces via
 * starfield_core, and writes a BC6H_UFLOAT KTX2 cubemap for the TIE
 * flight skybox loader. The look is authored interactively in the
 * starfield_tune ImGui tool, which writes the same preset format; the
 * advanced distribution/colour knobs come in through --preset.
 *
 * Usage: starfield <output.ktx2> [options]   (see --help)
 */

#include "imgbake/ktx2_writer.h"
#include "starfield_core.h"
#include "starfield_preset.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ---------- face dump (optional, tonemapped preview) ---------- */

static uint8_t TieStarfield_ToSrgbU8(float l) {
	if (l <= 0.0f)
		return 0;
	if (l >= 1.0f)
		return 255;
	float s = (l <= 0.0031308f) ? (l * 12.92f) : (1.055f * powf(l, 1.0f / 2.4f) - 0.055f);
	int v = (int)(s * 255.0f + 0.5f);
	return (uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v));
}

/* Write the 6 faces as Reinhard-tonemapped sRGB PNGs for eyeballing
 * the exact bake input. HDR cores clip to white in the preview. */
static bool TieStarfield_DumpFaces(const char* dir, float* faces[6], int fs) {
	static const char* kName[6] = { "px", "nx", "py", "ny", "pz", "nz" };
	uint8_t* rgb = (uint8_t*)malloc((size_t)fs * (size_t)fs * 3u);
	if (!rgb)
		return false;
	bool ok = true;
	for (int f = 0; f < 6 && ok; ++f) {
		for (size_t p = 0; p < (size_t)fs * (size_t)fs; ++p) {
			for (int c = 0; c < 3; ++c) {
				float v = faces[f][p * 3u + c];
				rgb[p * 3u + c] = TieStarfield_ToSrgbU8(v / (1.0f + v));
			}
		}
		char path[1024];
		snprintf(path, sizeof path, "%s/%s.png", dir, kName[f]);
		if (!stbi_write_png(path, fs, fs, 3, rgb, fs * 3)) {
			fprintf(stderr, "starfield: write %s failed\n", path);
			ok = false;
		} else {
			fprintf(stderr, "starfield: dumped %s\n", path);
		}
	}
	free(rgb);
	return ok;
}

static void TieStarfield_Usage(void) {
	fprintf(stderr, "Usage: starfield <output.ktx2> [options]\n"
					"\n"
					"Bake a procedural HDR star cubemap (BC6H_UFLOAT KTX2) for the\n"
					"TIE flight skybox loader. Author the full look interactively in\n"
					"starfield_tune and save a preset; advanced distribution/colour\n"
					"knobs are loaded via --preset.\n"
					"\n"
					"Options:\n"
					"  --preset FILE    Load a .tune.yaml first; flags below override it.\n"
					"  --face-size N    Per-face dim (default 1024). Rounded to a\n"
					"                   multiple of 4 (BC6H uses 4x4 blocks).\n"
					"  --num-stars N    Total stars over the whole sphere (default 6000).\n"
					"  --seed N         RNG seed (default 0 = time-based).\n"
					"  --intensity F    Linear radiance scale for star brightness.\n"
					"  --no-zstd        Skip zstd supercompression on level data.\n"
					"  --dump-faces DIR Also write px/nx/py/ny/pz/nz.png (Reinhard\n"
					"                   tonemapped) into DIR for visual inspection.\n"
					"  -h, --help       Show this help.\n");
	exit(2);
}

int main(int argc, char** argv) {
	TieStarfieldParams p;
	TieStarfieldCore_StarfieldDefaultParams(&p);
	TieStarfieldElement els[STARFIELD_MAX_ELEMENTS];
	int n_els = 0;

	/* Pre-scan so --preset applies before flag overrides regardless of
	 * argv order. */
	for (int i = 1; i < argc; ++i) {
		if (strcmp(argv[i], "--preset") == 0 && i + 1 < argc) {
			char err[256];
			if (!TieStarfieldPreset_Load(argv[i + 1], &p, els, &n_els, STARFIELD_MAX_ELEMENTS, err,
										 sizeof err)) {
				fprintf(stderr, "starfield: --preset %s: %s\n", argv[i + 1], err);
				return 1;
			}
		}
	}

	const char* out_path = NULL;
	const char* dump_dir = NULL;
	bool seed_from_flag = false;

	for (int i = 1; i < argc; ++i) {
		const char* a = argv[i];
		if (strcmp(a, "--preset") == 0 && i + 1 < argc) {
			++i; /* handled in pre-scan */
		} else if (strcmp(a, "--face-size") == 0 && i + 1 < argc) {
			p.face_size = atoi(argv[++i]);
		} else if (strcmp(a, "--num-stars") == 0 && i + 1 < argc) {
			p.num_stars = atoi(argv[++i]);
		} else if (strcmp(a, "--seed") == 0 && i + 1 < argc) {
			p.seed = strtoull(argv[++i], NULL, 10);
			seed_from_flag = true;
		} else if (strcmp(a, "--intensity") == 0 && i + 1 < argc) {
			p.intensity = (float)atof(argv[++i]);
		} else if (strcmp(a, "--no-zstd") == 0) {
			p.zstd = false;
		} else if (strcmp(a, "--dump-faces") == 0 && i + 1 < argc) {
			dump_dir = argv[++i];
		} else if (strcmp(a, "-h") == 0 || strcmp(a, "--help") == 0) {
			TieStarfield_Usage();
		} else if (a[0] == '-') {
			fprintf(stderr, "starfield: unknown option '%s'\n", a);
			TieStarfield_Usage();
		} else if (!out_path) {
			out_path = a;
		} else {
			TieStarfield_Usage();
		}
	}
	if (!out_path)
		TieStarfield_Usage();

	p.face_size &= ~3; /* multiple of 4 for BC6H blocks */
	if (p.face_size < 4) {
		fprintf(stderr, "starfield: face-size too small\n");
		return 1;
	}
	if (p.num_stars <= 0) {
		fprintf(stderr, "starfield: num-stars must be positive\n");
		return 1;
	}
	if (p.intensity <= 0.0f) {
		fprintf(stderr, "starfield: intensity must be positive\n");
		return 1;
	}
	if (p.seed == 0 && !seed_from_flag)
		p.seed = (uint64_t)time(NULL);

	fprintf(stderr, "starfield: %d^2 cubemap, %d stars, seed=%llu, intensity=%.3f, %s\n", p.face_size,
			p.num_stars, (unsigned long long)p.seed, (double)p.intensity, p.zstd ? "zstd" : "raw");

	/* Render once; dump and bake share the exact same faces so the dump
	 * is the bake input, not a re-roll. */
	size_t face_floats = (size_t)p.face_size * (size_t)p.face_size * 3u;
	float* faces[6] = { 0 };
	bool ok = true;
	for (int f = 0; f < 6; ++f) {
		faces[f] = (float*)calloc(face_floats, sizeof(float));
		if (!faces[f]) {
			ok = false;
			break;
		}
	}

	if (ok) {
		TieStarfieldCore_StarfieldRenderScene(&p, els, n_els, faces);
		/* Dump the readable (natural) faces for inspection, THEN mirror
		 * into the engine's GPU cube convention for the KTX2. */
		if (dump_dir)
			ok = TieStarfield_DumpFaces(dump_dir, faces, p.face_size);
		TieStarfieldCore_StarfieldMirrorCubeX(faces, p.face_size);
	}
	if (ok) {
		const float* cf[6] = { faces[0], faces[1], faces[2], faces[3], faces[4], faces[5] };
		ok = write_ktx2_bc6h_cubemap_with_generated_mips(out_path, p.face_size, cf, KTX2_BC6H_QUALITY_FAST,
														 p.zstd);
	}
	for (int f = 0; f < 6; ++f)
		free(faces[f]);

	if (!ok) {
		fprintf(stderr, "starfield: write %s failed\n", out_path);
		return 1;
	}
	fprintf(stderr, "starfield: wrote %s\n", out_path);
	return 0;
}
