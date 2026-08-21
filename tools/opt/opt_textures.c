/*
 * opt_textures - extract textures from an OPT file to PNG.
 * Mirrors opt_textures.py: one PNG per texture at the primary lighting shade
 * by default, plus optional all-shades / mip-chain / palette-grid outputs.
 */

#include "opt.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define PALETTE_GRID_CELL 8 /* pixels per palette cell in the grid PNG */

static int TieOptTextures_EnsureDirectory(const char* path) {
	struct stat st;
	if (stat(path, &st) == 0)
		return S_ISDIR(st.st_mode);
	if (mkdir(path, 0755) == 0)
		return 1;
	return errno == EEXIST;
}

/* Replace path separators / special chars with underscores, like the Python tool. */
static void TieOptTextures_SafeName(char* dst, size_t dst_size, const char* src) {
	size_t j = 0;
	for (size_t i = 0; src[i] && j + 1 < dst_size; i++) {
		char ch = src[i];
		int ok = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') ||
				 ch == '.' || ch == '_' || ch == '-';
		dst[j++] = ok ? ch : '_';
	}
	if (j == 0 && j + 1 < dst_size)
		dst[j++] = 'x';
	dst[j] = 0;
}

/* Strip directory and extension from a path, in-place via dst. */
static void TieOptTextures_BasenameNoExt(char* dst, size_t dst_size, const char* path) {
	const char* base = strrchr(path, '/');
	base = base ? base + 1 : path;
	size_t n = strlen(base);
	const char* dot = strrchr(base, '.');
	if (dot && dot > base)
		n = (size_t)(dot - base);
	if (n >= dst_size)
		n = dst_size - 1;
	memcpy(dst, base, n);
	dst[n] = 0;
}

/* Decode a full mip level's worth of palette indices to packed RGB888. */
static uint8_t* TieOptTextures_DecodeToRgb(const opt_texture_t* t, int shade, const uint8_t* pixels, int w,
										   int h) {
	size_t n = (size_t)w * (size_t)h;
	uint8_t* rgb = (uint8_t*)malloc(n * 3);
	if (!rgb)
		return NULL;
	/* Precompute the 256-entry RGB LUT for this shade once per image. */
	uint8_t lut[256][3];
	for (int i = 0; i < 256; i++) {
		opt_palette_rgb(t->palette, shade, i, &lut[i][0], &lut[i][1], &lut[i][2]);
	}
	for (size_t i = 0; i < n; i++) {
		uint8_t idx = pixels[i];
		rgb[i * 3 + 0] = lut[idx][0];
		rgb[i * 3 + 1] = lut[idx][1];
		rgb[i * 3 + 2] = lut[idx][2];
	}
	return rgb;
}

/* Returns 1 if every entry in the shade is 0xCD across both bytes - the
 * MSVC heap pattern left over from the original build tool for unused shades. */
static int TieOptTextures_ShadeIsUnused(const opt_texture_t* t, int shade) {
	const uint8_t* p = t->palette + (size_t)shade * OPT_PALETTE_COLORS * OPT_PALETTE_BPP;
	for (size_t i = 0; i < OPT_PALETTE_COLORS * OPT_PALETTE_BPP; i++) {
		if (p[i] != 0xCD)
			return 0;
	}
	return 1;
}

static int TieOptTextures_WritePngRgb(const char* path, int w, int h, const uint8_t* rgb) {
	if (stbi_write_png(path, w, h, 3, rgb, w * 3) == 0) {
		fprintf(stderr, "failed to write %s\n", path);
		return 0;
	}
	return 1;
}

/* Write a 256-wide x 16-tall palette atlas (one row per shade level), scaled
 * by PALETTE_GRID_CELL on each axis. Empty shades come out solid 0xCDCDCD. */
static int TieOptTextures_WritePaletteGrid(const opt_texture_t* t, const char* path) {
	int w = OPT_PALETTE_COLORS * PALETTE_GRID_CELL;
	int h = OPT_PALETTE_SHADES * PALETTE_GRID_CELL;
	uint8_t* rgb = (uint8_t*)malloc((size_t)w * (size_t)h * 3);
	if (!rgb)
		return 0;
	for (int shade = 0; shade < OPT_PALETTE_SHADES; shade++) {
		for (int i = 0; i < OPT_PALETTE_COLORS; i++) {
			uint8_t r, g, b;
			opt_palette_rgb(t->palette, shade, i, &r, &g, &b);
			for (int dy = 0; dy < PALETTE_GRID_CELL; dy++) {
				int y = shade * PALETTE_GRID_CELL + dy;
				for (int dx = 0; dx < PALETTE_GRID_CELL; dx++) {
					int x = i * PALETTE_GRID_CELL + dx;
					rgb[(y * w + x) * 3 + 0] = r;
					rgb[(y * w + x) * 3 + 1] = g;
					rgb[(y * w + x) * 3 + 2] = b;
				}
			}
		}
	}
	int ok = TieOptTextures_WritePngRgb(path, w, h, rgb);
	free(rgb);
	return ok;
}

typedef struct {
	const char* outdir;
	int shade;
	int all_shades;
	int mips;
	int palette_grid;
} TieOptTexturesOptions;

static void TieOptTextures_ExtractOne(const opt_texture_t* t, const char* basename,
									  const TieOptTexturesOptions* o) {
	char name[OPT_TEXTURE_NAME_MAX];
	TieOptTextures_SafeName(name, sizeof name, t->name[0] ? t->name : "unnamed");

	if (!t->pixels || t->width <= 0 || t->height <= 0) {
		fprintf(stderr, "  [skip] %s: no pixels\n", name);
		return;
	}

	char path[1024];

	/* Base mip at primary shade. */
	uint8_t* rgb = TieOptTextures_DecodeToRgb(t, o->shade, t->pixels, t->width, t->height);
	if (rgb) {
		snprintf(path, sizeof path, "%s/%s__%s.png", o->outdir, basename, name);
		TieOptTextures_WritePngRgb(path, t->width, t->height, rgb);
		printf("  %s__%s.png  (%dx%d, shade=%d)\n", basename, name, t->width, t->height, o->shade);
		free(rgb);
	}

	if (o->all_shades) {
		for (int s = 0; s < OPT_PALETTE_SHADES; s++) {
			if (TieOptTextures_ShadeIsUnused(t, s))
				continue;
			uint8_t* r = TieOptTextures_DecodeToRgb(t, s, t->pixels, t->width, t->height);
			if (!r)
				continue;
			snprintf(path, sizeof path, "%s/%s__%s__shade%02d.png", o->outdir, basename, name, s);
			TieOptTextures_WritePngRgb(path, t->width, t->height, r);
			free(r);
		}
	}

	if (o->mips && t->mip_count > 1) {
		for (int level = 1; level < t->mip_count; level++) {
			int32_t mw = 0, mh = 0;
			size_t off = opt_texture_mip_offset(t, level, &mw, &mh);
			if (mw <= 0 || mh <= 0)
				continue;
			uint8_t* r = TieOptTextures_DecodeToRgb(t, o->shade, t->pixels + off, mw, mh);
			if (!r)
				continue;
			snprintf(path, sizeof path, "%s/%s__%s__mip%d.png", o->outdir, basename, name, level);
			TieOptTextures_WritePngRgb(path, mw, mh, r);
			free(r);
		}
	}

	if (o->palette_grid) {
		snprintf(path, sizeof path, "%s/%s__%s__palette.png", o->outdir, basename, name);
		TieOptTextures_WritePaletteGrid(t, path);
	}
}

static void TieOptTextures_Usage(const char* argv0) {
	fprintf(stderr,
			"Usage: %s <file.opt> [options]\n"
			"options:\n"
			"  -o <dir>           output directory (default: <basename>_textures/)\n"
			"  --shade <N>        primary shade 0..15 (default: %d = fully lit)\n"
			"  --all-shades       export every non-empty shading level\n"
			"  --mips             export each mip level beyond the base\n"
			"  --palette-grid     export a 256x16 grid of all shades\n",
			argv0, OPT_PRIMARY_SHADE);
}

int main(int argc, char** argv) {
	const char* opt_path = NULL;
	TieOptTexturesOptions o = { .outdir = NULL, .shade = OPT_PRIMARY_SHADE };

	for (int i = 1; i < argc; i++) {
		const char* a = argv[i];
		if (strcmp(a, "-h") == 0 || strcmp(a, "--help") == 0) {
			TieOptTextures_Usage(argv[0]);
			return 0;
		} else if (strcmp(a, "-o") == 0 && i + 1 < argc) {
			o.outdir = argv[++i];
		} else if (strcmp(a, "--shade") == 0 && i + 1 < argc) {
			o.shade = atoi(argv[++i]);
			if (o.shade < 0 || o.shade >= OPT_PALETTE_SHADES) {
				fprintf(stderr, "--shade out of range 0..15\n");
				return 1;
			}
		} else if (strcmp(a, "--all-shades") == 0) {
			o.all_shades = 1;
		} else if (strcmp(a, "--mips") == 0) {
			o.mips = 1;
		} else if (strcmp(a, "--palette-grid") == 0) {
			o.palette_grid = 1;
		} else if (a[0] != '-' && !opt_path) {
			opt_path = a;
		} else {
			fprintf(stderr, "unknown argument: %s\n", a);
			TieOptTextures_Usage(argv[0]);
			return 1;
		}
	}
	if (!opt_path) {
		TieOptTextures_Usage(argv[0]);
		return 1;
	}

	opt_error_t err;
	opt_file_t* opt = opt_load_file(opt_path, &err);
	if (!opt) {
		fprintf(stderr, "%s: %s\n", opt_path, err.msg);
		return 1;
	}

	/* Default output directory: <basename>_textures/ next to the OPT file. */
	char basename[256];
	TieOptTextures_BasenameNoExt(basename, sizeof basename, opt_path);
	char outdir_default[1024];
	if (!o.outdir) {
		char dir[512] = ".";
		const char* slash = strrchr(opt_path, '/');
		if (slash) {
			size_t n = (size_t)(slash - opt_path);
			if (n >= sizeof dir)
				n = sizeof dir - 1;
			memcpy(dir, opt_path, n);
			dir[n] = 0;
		}
		snprintf(outdir_default, sizeof outdir_default, "%s/%s_textures", dir, basename);
		o.outdir = outdir_default;
	}
	if (!TieOptTextures_EnsureDirectory(o.outdir)) {
		fprintf(stderr, "cannot create '%s': %s\n", o.outdir, strerror(errno));
		opt_free(opt);
		return 1;
	}

	printf("%s: %d texture nodes -> %s\n", opt_path, opt->texture_count, o.outdir);
	for (int i = 0; i < opt->texture_count; i++) {
		TieOptTextures_ExtractOne(&opt->textures[i], basename, &o);
	}

	opt_free(opt);
	return 0;
}
