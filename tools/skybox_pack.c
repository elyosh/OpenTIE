/* Converts an equirectangular PNG or HDR image to a mipmapped KTX2 cubemap.
 * Resampling is performed in linear light. */

#include "imgbake/ktx2_writer.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_HDR
#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h> /* strcasecmp */

/* KTX2 face order: +X, -X, +Y, -Y, +Z, -Z. */
enum { FACE_PX = 0, FACE_NX, FACE_PY, FACE_NY, FACE_PZ, FACE_NZ };

typedef enum {
	TONEMAP_NONE = 0,
	TONEMAP_REINHARD = 1,
	TONEMAP_ACES = 2,
} TieSkyboxTonemapOperation;

typedef enum {
	OUTFMT_BC7 = 0,  /* LDR: sRGB-encoded uint8 → BC7. */
	OUTFMT_BC6H = 1, /* HDR: linear float RGB → BC6H_UFLOAT. */
} TieSkyboxOutputFormat;

/* sRGB → linear LUT for fast u8 decode in the bilinear gather hot
 * loop. Reverse direction (linear → sRGB) is per-pixel float and
 * doesn't LUT well without quantisation, so we keep the powf there. */
static float SRGB_TO_LINEAR[256];

static void TieSkyboxPack_InitSrgbLut(void) {
	for (int i = 0; i < 256; ++i) {
		float s = (float)i / 255.0f;
		SRGB_TO_LINEAR[i] = (s <= 0.04045f) ? (s / 12.92f) : powf((s + 0.055f) / 1.055f, 2.4f);
	}
}

static uint8_t TieSkyboxPack_LinearToSrgbU8(float l) {
	if (l <= 0.0f)
		return 0;
	if (l >= 1.0f)
		return 255;
	float s = (l <= 0.0031308f) ? (l * 12.92f) : (1.055f * powf(l, 1.0f / 2.4f) - 0.055f);
	int v = (int)(s * 255.0f + 0.5f);
	return (uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v));
}

/* ACES filmic approximation (Krzysztof Narkowicz, 2015). Operates per
 * channel on linear input; saturate at the call site. */
static void TieSkyboxPack_AcesTonemap(float* r, float* g, float* b) {
	static const float A = 2.51f, B = 0.03f, C = 2.43f, D = 0.59f, E = 0.14f;
	*r = (*r * (A * *r + B)) / (*r * (C * *r + D) + E);
	*g = (*g * (A * *g + B)) / (*g * (C * *g + D) + E);
	*b = (*b * (A * *b + B)) / (*b * (C * *b + D) + E);
}

static float TieSkyboxPack_SaturateF(float x) { return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x); }

/* TieSkyboxEquirectangularImage source — discriminated by `is_hdr`. Both representations
 * are 4-channel tightly packed (stb_image fills missing channels with
 * 1.0 for HDR / 255 for u8). Alpha is unused by the skybox shader. */
typedef struct {
	int width, height;
	bool is_hdr;
	union {
		uint8_t* u8;
		float* f32;
	} px;
} TieSkyboxEquirectangularImage;

static bool TieSkyboxPack_PathIsHdr(const char* path) {
	const char* dot = strrchr(path, '.');
	return dot && strcasecmp(dot, ".hdr") == 0;
}

static bool TieSkyboxPack_EquirectLoad(const char* path, TieSkyboxEquirectangularImage* out) {
	int w = 0, h = 0, comp = 0;
	if (TieSkyboxPack_PathIsHdr(path)) {
		float* d = stbi_loadf(path, &w, &h, &comp, 4);
		if (!d) {
			fprintf(stderr, "skybox_pack: load %s failed: %s\n", path, stbi_failure_reason());
			return false;
		}
		out->width = w;
		out->height = h;
		out->is_hdr = true;
		out->px.f32 = d;
	} else {
		uint8_t* d = stbi_load(path, &w, &h, &comp, 4);
		if (!d) {
			fprintf(stderr, "skybox_pack: load %s failed: %s\n", path, stbi_failure_reason());
			return false;
		}
		out->width = w;
		out->height = h;
		out->is_hdr = false;
		out->px.u8 = d;
	}
	if (w != 2 * h)
		fprintf(stderr, "skybox_pack: warning: input %dx%d is not 2:1 — output may distort\n", w, h);
	return true;
}

static void TieSkyboxPack_EquirectFree(TieSkyboxEquirectangularImage* e) {
	if (e->is_hdr)
		stbi_image_free(e->px.f32);
	else
		stbi_image_free(e->px.u8);
	e->px.f32 = NULL;
	e->px.u8 = NULL;
}

static void TieSkyboxPack_EquirectTexelLinear(const TieSkyboxEquirectangularImage* e, int x, int y,
											  float out[3]) {
	size_t off = ((size_t)y * (size_t)e->width + (size_t)x) * 4u;
	if (e->is_hdr) {
		const float* p = e->px.f32 + off;
		out[0] = p[0];
		out[1] = p[1];
		out[2] = p[2];
	} else {
		const uint8_t* p = e->px.u8 + off;
		out[0] = SRGB_TO_LINEAR[p[0]];
		out[1] = SRGB_TO_LINEAR[p[1]];
		out[2] = SRGB_TO_LINEAR[p[2]];
	}
}

/* Bilinear gather at world direction (need not be normalised — we
 * normalise here). Wraps longitude (the panorama seam at u=0/u=1),
 * clamps latitude (the pole rows). Convention: longitude=0 at -Z
 * (forward), increasing toward +X; v=0 at +Y (zenith). Matches what
 * Blender / Substance / HDRI Haven panoramas emit. */
static void TieSkyboxPack_EquirectSampleLinear(const TieSkyboxEquirectangularImage* e, float dx, float dy,
											   float dz, float out[3]) {
	float inv = 1.0f / sqrtf(dx * dx + dy * dy + dz * dz);
	dx *= inv;
	dy *= inv;
	dz *= inv;

	static const float TWO_PI = 6.28318530717958647692f;
	static const float PI = 3.14159265358979323846f;
	float lon = atan2f(dx, -dz);
	float dyc = dy < -1.f ? -1.f : (dy > 1.f ? 1.f : dy);
	float lat = asinf(dyc);
	float u = lon / TWO_PI + 0.5f;
	float v = 0.5f - lat / PI;

	int W = e->width, H = e->height;
	float fx = u * (float)W - 0.5f;
	float fy = v * (float)H - 0.5f;
	int x0 = (int)floorf(fx), y0 = (int)floorf(fy);
	int x1 = x0 + 1, y1 = y0 + 1;
	float tx = fx - (float)x0, ty = fy - (float)y0;
	/* Modulo for wrap; C's `%` keeps sign of dividend so add W first. */
	x0 = ((x0 % W) + W) % W;
	x1 = ((x1 % W) + W) % W;
	y0 = y0 < 0 ? 0 : (y0 > H - 1 ? H - 1 : y0);
	y1 = y1 < 0 ? 0 : (y1 > H - 1 ? H - 1 : y1);

	float p00[3], p10[3], p01[3], p11[3];
	TieSkyboxPack_EquirectTexelLinear(e, x0, y0, p00);
	TieSkyboxPack_EquirectTexelLinear(e, x1, y0, p10);
	TieSkyboxPack_EquirectTexelLinear(e, x0, y1, p01);
	TieSkyboxPack_EquirectTexelLinear(e, x1, y1, p11);
	for (int c = 0; c < 3; ++c) {
		float a = p00[c] * (1.f - tx) + p10[c] * tx;
		float b = p01[c] * (1.f - tx) + p11[c] * tx;
		out[c] = a * (1.f - ty) + b * ty;
	}
}

/* Cube face (px, py) → world direction. Standard KTX2 / Vulkan / D3D
 * cubemap convention: top-left pixel = face_uv (0, 0). */
static void TieSkyboxPack_FaceDirection(int face_id, int px, int py, int face_size, float* dx, float* dy,
										float* dz) {
	float uc = ((float)px + 0.5f) / (float)face_size * 2.f - 1.f;
	float vc = ((float)py + 0.5f) / (float)face_size * 2.f - 1.f;
	switch (face_id) {
		case FACE_PX:
			*dx = 1.f;
			*dy = -vc;
			*dz = -uc;
			break;
		case FACE_NX:
			*dx = -1.f;
			*dy = -vc;
			*dz = uc;
			break;
		case FACE_PY:
			*dx = uc;
			*dy = 1.f;
			*dz = vc;
			break;
		case FACE_NY:
			*dx = uc;
			*dy = -1.f;
			*dz = -vc;
			break;
		case FACE_PZ:
			*dx = uc;
			*dy = -vc;
			*dz = 1.f;
			break;
		case FACE_NZ:
			*dx = -uc;
			*dy = -vc;
			*dz = -1.f;
			break;
		default:
			*dx = 0;
			*dy = 0;
			*dz = 0;
			break;
	}
}

/* LDR path: render face to sRGB-encoded uint8 RGBA. Applies exposure
 * + tonemap for HDR sources to fit 0..1 before sRGB-encode. */
static void TieSkyboxPack_RenderFaceLdr(const TieSkyboxEquirectangularImage* e, int face_id, int face_size,
										float exposure, TieSkyboxTonemapOperation tonemap,
										uint8_t* out_rgba) {
	for (int y = 0; y < face_size; ++y) {
		for (int x = 0; x < face_size; ++x) {
			float dx, dy, dz;
			TieSkyboxPack_FaceDirection(face_id, x, y, face_size, &dx, &dy, &dz);

			float rgb[3];
			TieSkyboxPack_EquirectSampleLinear(e, dx, dy, dz, rgb);

			if (e->is_hdr) {
				rgb[0] *= exposure;
				rgb[1] *= exposure;
				rgb[2] *= exposure;
				if (tonemap == TONEMAP_ACES) {
					TieSkyboxPack_AcesTonemap(&rgb[0], &rgb[1], &rgb[2]);
				} else if (tonemap == TONEMAP_REINHARD) {
					rgb[0] = rgb[0] / (1.f + rgb[0]);
					rgb[1] = rgb[1] / (1.f + rgb[1]);
					rgb[2] = rgb[2] / (1.f + rgb[2]);
				}
			}

			uint8_t* o = out_rgba + ((size_t)y * (size_t)face_size + (size_t)x) * 4u;
			o[0] = TieSkyboxPack_LinearToSrgbU8(TieSkyboxPack_SaturateF(rgb[0]));
			o[1] = TieSkyboxPack_LinearToSrgbU8(TieSkyboxPack_SaturateF(rgb[1]));
			o[2] = TieSkyboxPack_LinearToSrgbU8(TieSkyboxPack_SaturateF(rgb[2]));
			o[3] = 255;
		}
	}
}

/* HDR path: render face to linear-RGB float32 (3 channels, no alpha).
 * Applies the exposure scale and exits — no tonemap, no saturate, no
 * sRGB-encode. The BC6H encoder consumes this directly. */
static void TieSkyboxPack_RenderFaceHdr(const TieSkyboxEquirectangularImage* e, int face_id, int face_size,
										float exposure, float* out_rgb) {
	for (int y = 0; y < face_size; ++y) {
		for (int x = 0; x < face_size; ++x) {
			float dx, dy, dz;
			TieSkyboxPack_FaceDirection(face_id, x, y, face_size, &dx, &dy, &dz);

			float rgb[3];
			TieSkyboxPack_EquirectSampleLinear(e, dx, dy, dz, rgb);
			rgb[0] *= exposure;
			rgb[1] *= exposure;
			rgb[2] *= exposure;
			/* Clamp negative values — BC6H_UFLOAT can only encode
			 * non-negative half-floats. Some HDR sources have small
			 * sub-zero noise; pin to 0 rather than letting BC6H see
			 * them as NaN-adjacent corners. */
			if (rgb[0] < 0.0f)
				rgb[0] = 0.0f;
			if (rgb[1] < 0.0f)
				rgb[1] = 0.0f;
			if (rgb[2] < 0.0f)
				rgb[2] = 0.0f;

			float* o = out_rgb + ((size_t)y * (size_t)face_size + (size_t)x) * 3u;
			o[0] = rgb[0];
			o[1] = rgb[1];
			o[2] = rgb[2];
		}
	}
}

static void TieSkyboxPack_Usage(void) {
	fprintf(stderr, "Usage: skybox_pack <input> <output.ktx2> [options]\n"
					"\n"
					"Convert an equirectangular panorama (PNG or .hdr) into a KTX2\n"
					"cubemap for the TIE flight skybox loader.\n"
					"\n"
					"Options:\n"
					"  --format {bc7|bc6h}\n"
					"                   Output codec. bc7 (default) = LDR sRGB; bc6h\n"
					"                   = linear HDR (requires .hdr / .exr input).\n"
					"  --face-size N    Per-face dim. Default: min(input_w/4, 2048)\n"
					"                   rounded down to a multiple of 4.\n"
					"  --quality {fast|med|uber}\n"
					"                   BC7 quality preset (default: fast). Accepted\n"
					"                   for bc6h but currently ignored.\n"
					"  --no-zstd        Skip zstd supercompression on level data.\n"
					"  --exposure E     Linear exposure scale (default 1.0). Applied\n"
					"                   to HDR sources for both bc7 and bc6h paths.\n"
					"  --tonemap {aces|reinhard|none}\n"
					"                   HDR tonemap operator (default aces). bc7 only;\n"
					"                   ignored for bc6h (no LDR mapping needed).\n"
					"  --dump-faces DIR\n"
					"                   Also write px/nx/py/ny/pz/nz.png into DIR\n"
					"                   (post-tonemap, pre-BC7) for visual inspection.\n"
					"                   bc7 path only.\n"
					"  -h, --help       Show this help.\n");
	exit(2);
}

int main(int argc, char** argv) {
	const char* in_path = NULL;
	const char* out_path = NULL;
	const char* dump_dir = NULL;
	int face_size = 0;
	TieSkyboxOutputFormat outfmt = OUTFMT_BC7;
	Ktx2Bc7Quality quality = KTX2_BC7_QUALITY_FAST;
	bool zstd = true;
	float exposure = 1.0f;
	TieSkyboxTonemapOperation tonemap = TONEMAP_ACES;

	for (int i = 1; i < argc; ++i) {
		const char* a = argv[i];
		if (strcmp(a, "--format") == 0 && i + 1 < argc) {
			const char* fmt = argv[++i];
			if (strcmp(fmt, "bc7") == 0)
				outfmt = OUTFMT_BC7;
			else if (strcmp(fmt, "bc6h") == 0)
				outfmt = OUTFMT_BC6H;
			else {
				fprintf(stderr, "skybox_pack: unknown format '%s'\n", fmt);
				TieSkyboxPack_Usage();
			}
		} else if (strcmp(a, "--face-size") == 0 && i + 1 < argc) {
			face_size = atoi(argv[++i]);
		} else if (strcmp(a, "--quality") == 0 && i + 1 < argc) {
			const char* q = argv[++i];
			if (strcmp(q, "fast") == 0)
				quality = KTX2_BC7_QUALITY_FAST;
			else if (strcmp(q, "med") == 0)
				quality = KTX2_BC7_QUALITY_MED;
			else if (strcmp(q, "uber") == 0)
				quality = KTX2_BC7_QUALITY_UBER;
			else {
				fprintf(stderr, "skybox_pack: unknown quality '%s'\n", q);
				TieSkyboxPack_Usage();
			}
		} else if (strcmp(a, "--no-zstd") == 0) {
			zstd = false;
		} else if (strcmp(a, "--exposure") == 0 && i + 1 < argc) {
			exposure = (float)atof(argv[++i]);
		} else if (strcmp(a, "--tonemap") == 0 && i + 1 < argc) {
			const char* t = argv[++i];
			if (strcmp(t, "aces") == 0)
				tonemap = TONEMAP_ACES;
			else if (strcmp(t, "reinhard") == 0)
				tonemap = TONEMAP_REINHARD;
			else if (strcmp(t, "none") == 0)
				tonemap = TONEMAP_NONE;
			else {
				fprintf(stderr, "skybox_pack: unknown tonemap '%s'\n", t);
				TieSkyboxPack_Usage();
			}
		} else if (strcmp(a, "--dump-faces") == 0 && i + 1 < argc) {
			dump_dir = argv[++i];
		} else if (strcmp(a, "-h") == 0 || strcmp(a, "--help") == 0) {
			TieSkyboxPack_Usage();
		} else if (a[0] == '-') {
			fprintf(stderr, "skybox_pack: unknown option '%s'\n", a);
			TieSkyboxPack_Usage();
		} else if (!in_path)
			in_path = a;
		else if (!out_path)
			out_path = a;
		else
			TieSkyboxPack_Usage();
	}
	if (!in_path || !out_path)
		TieSkyboxPack_Usage();

	TieSkyboxPack_InitSrgbLut();

	TieSkyboxEquirectangularImage eq = { 0 };
	if (!TieSkyboxPack_EquirectLoad(in_path, &eq))
		return 1;

	/* bc6h requires an HDR source — LDR PNG has no HDR information
	 * to preserve and the resulting cube would just be a linear
	 * upcast of sRGB-decoded bytes. */
	if (outfmt == OUTFMT_BC6H && !eq.is_hdr) {
		fprintf(stderr,
				"skybox_pack: --format bc6h requires an HDR input "
				"(.hdr); got LDR '%s'\n",
				in_path);
		TieSkyboxPack_EquirectFree(&eq);
		return 1;
	}

	if (face_size <= 0) {
		face_size = eq.width / 4;
		if (face_size > 2048)
			face_size = 2048;
		face_size &= ~3; /* multiple of 4 — both BC7 and BC6H use 4×4 blocks */
		if (face_size < 4) {
			fprintf(stderr, "skybox_pack: input too small (%d px wide) for a usable cubemap\n", eq.width);
			TieSkyboxPack_EquirectFree(&eq);
			return 1;
		}
	}

	fprintf(stderr, "skybox_pack: %dx%d %s -> %d^2 cubemap, %s %s, %s\n", eq.width, eq.height,
			eq.is_hdr ? "HDR" : "LDR", face_size, outfmt == OUTFMT_BC6H ? "BC6H_UFLOAT" : "BC7",
			outfmt == OUTFMT_BC6H              ? "(linear HDR)"
			: quality == KTX2_BC7_QUALITY_UBER ? "uber"
			: quality == KTX2_BC7_QUALITY_MED  ? "med"
											   : "fast",
			zstd ? "zstd" : "raw");
	if (eq.is_hdr) {
		if (outfmt == OUTFMT_BC6H) {
			fprintf(stderr, "          exposure=%.3f (tonemap ignored)\n", exposure);
		} else {
			fprintf(stderr, "          exposure=%.3f tonemap=%s\n", exposure,
					tonemap == TONEMAP_ACES       ? "aces"
					: tonemap == TONEMAP_REINHARD ? "reinhard"
												  : "none");
		}
	}
	if (outfmt == OUTFMT_BC6H && dump_dir) {
		fprintf(stderr, "skybox_pack: --dump-faces ignored on bc6h path (HDR float, no PNG dump)\n");
		dump_dir = NULL;
	}

	bool ok = true;
	if (outfmt == OUTFMT_BC7) {
		size_t face_bytes = (size_t)face_size * (size_t)face_size * 4u;
		uint8_t* faces[6] = { 0 };
		for (int f = 0; f < 6; ++f) {
			faces[f] = (uint8_t*)malloc(face_bytes);
			if (!faces[f]) {
				ok = false;
				break;
			}
			TieSkyboxPack_RenderFaceLdr(&eq, f, face_size, exposure, tonemap, faces[f]);
		}

		if (ok && dump_dir) {
			static const char* kFaceName[6] = { "px", "nx", "py", "ny", "pz", "nz" };
			for (int f = 0; f < 6; ++f) {
				char path[1024];
				snprintf(path, sizeof path, "%s/%s.png", dump_dir, kFaceName[f]);
				if (!stbi_write_png(path, face_size, face_size, 4, faces[f], face_size * 4)) {
					fprintf(stderr, "skybox_pack: write %s failed\n", path);
					ok = false;
					break;
				}
				fprintf(stderr, "skybox_pack: dumped %s\n", path);
			}
		}

		if (ok) {
			const uint8_t* cf[6] = { faces[0], faces[1], faces[2], faces[3], faces[4], faces[5] };
			ok = write_ktx2_bc7_cubemap_with_generated_mips(out_path, face_size, cf, quality, KTX2_TF_LINEAR,
															zstd);
		}
		for (int f = 0; f < 6; ++f)
			free(faces[f]);
	} else {
		/* OUTFMT_BC6H — linear HDR float RGB per face. */
		size_t face_floats = (size_t)face_size * (size_t)face_size * 3u;
		float* faces[6] = { 0 };
		for (int f = 0; f < 6; ++f) {
			faces[f] = (float*)malloc(face_floats * sizeof(float));
			if (!faces[f]) {
				ok = false;
				break;
			}
			TieSkyboxPack_RenderFaceHdr(&eq, f, face_size, exposure, faces[f]);
		}

		if (ok) {
			const float* cf[6] = { faces[0], faces[1], faces[2], faces[3], faces[4], faces[5] };
			ok = write_ktx2_bc6h_cubemap_with_generated_mips(out_path, face_size, cf, KTX2_BC6H_QUALITY_FAST,
															 zstd);
		}
		for (int f = 0; f < 6; ++f)
			free(faces[f]);
	}

	TieSkyboxPack_EquirectFree(&eq);

	if (!ok) {
		fprintf(stderr, "skybox_pack: write %s failed\n", out_path);
		return 1;
	}
	fprintf(stderr, "skybox_pack: wrote %s\n", out_path);
	return 0;
}
