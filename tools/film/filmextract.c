/* Extracts FILM sprite frames to RGBA PNG. Palette overlays accumulate in
 * argument order, and unresolved resources are searched in extra LFDs. */

#include "aeron/atlas_pack.h"
#include "film.h"
#include "fourcc.h"
#include "imgbake/anim.h"
#include "imgbake/atlas_pack.h"
#include "imgbake/byteio.h"
#include "imgbake/delt.h"
#include "imgbake/palette.h"
#include "imgbake/png_write.h"
#include "imgbake/upscale.h"
#include "lfd_file.h"
#include "util.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

/* ---------- mkdir -p ---------- */

static int TieFilmExtract_MakePath(const char* path) {
	char buf[1024];
	size_t n = strlen(path);
	if (n + 1 > sizeof(buf))
		return -1;
	memcpy(buf, path, n + 1);
	for (size_t i = 1; i < n; i++) {
		if (buf[i] == '/') {
			buf[i] = '\0';
			if (mkdir(buf, 0777) != 0 && errno != EEXIST)
				return -1;
			buf[i] = '/';
		}
	}
	if (mkdir(buf, 0777) != 0 && errno != EEXIST)
		return -1;
	return 0;
}

/* ---------- palette-effect dedup ----------
 *
 * The 4K-extraction reference render uses the engine's `standard`
 * palette, but films apply PLTT swaps that can recolour sprites
 * before showing them. When such a swap touches indices the actor
 * actually uses, the on-screen pixels differ from the canonical
 * render — and that's the version the artist sees in-game.
 *
 * The fingerprint workflow:
 *   1. mark_indices: collect a 256-bit bitmap of indices the asset
 *      actually paints with.
 *   2. palette_used_hash: FNV-1a 64 over the palette's RGBA at those
 *      indices (and only those). Two palettes that differ only in
 *      slots the asset never touches produce the same hash, hence
 *      the same render — no need to emit a variant.
 *   3. Compare each per-film palette's hash to the canonical
 *      `standard`-palette hash for this asset; emit a variant only
 *      when the on-screen pixels would actually differ. */

static void TieFilmExtract_MarkIndices(const Image8* img, uint8_t used[32]) {
	if (!img->pixels)
		return;
	size_t n = (size_t)img->width * (size_t)img->height;
	for (size_t i = 0; i < n; i++) {
		uint8_t v = img->pixels[i];
		used[v >> 3] |= (uint8_t)(1u << (v & 7));
	}
}

static bool TieFilmExtract_SpriteUsedIndices(const TieLfdFileChain* chain, uint32_t res_type,
											 const char* res_name, uint8_t used[32]) {
	memset(used, 0, 32);
	const TieLfdFile* owner = NULL;
	if (res_type == FCC_DELT || res_type == FCC_RAW) {
		const TieLfdFileEntry* e = TieLfdFileChain_Find(chain, FCC_DELT, res_name, &owner);
		if (!e)
			e = TieLfdFileChain_Find(chain, FCC_RAW, res_name, &owner);
		if (!e)
			return false;
		Image8 img;
		if (!decode_delt(&img, TieLfdFile_Data(owner, e), e->size))
			return false;
		TieFilmExtract_MarkIndices(&img, used);
		image_free(&img);
		return true;
	}
	if (res_type == FCC_ANIM) {
		const TieLfdFileEntry* e = TieLfdFileChain_Find(chain, FCC_ANIM, res_name, &owner);
		if (!e)
			return false;
		AnimImage anim;
		if (!decode_anim(&anim, TieLfdFile_Data(owner, e), e->size))
			return false;
		for (int i = 0; i < anim.count; i++)
			TieFilmExtract_MarkIndices(&anim.frames[i], used);
		anim_free(&anim);
		return true;
	}
	return false;
}

static uint64_t TieFilmExtract_PaletteUsedHash(const Palette* pal, const uint8_t used[32]) {
	uint64_t h = 0xcbf29ce484222325ull;
	for (int idx = 0; idx < 256; idx++) {
		if (!(used[idx >> 3] & (1u << (idx & 7))))
			continue;
		for (int c = 0; c < 4; c++) {
			h ^= pal->rgba[idx][c];
			h *= 0x100000001b3ull;
		}
	}
	return h;
}

/* ---------- indexed → RGBA ---------- */

static uint8_t* TieFilmExtract_RenderRgba(const Image8* img, const Palette* pal) {
	size_t pixels = (size_t)img->width * (size_t)img->height;
	uint8_t* rgba = (uint8_t*)malloc(pixels * 4);
	if (!rgba)
		return NULL;
	for (size_t i = 0; i < pixels; i++) {
		uint8_t idx = img->pixels[i];
		rgba[i * 4 + 0] = pal->rgba[idx][0];
		rgba[i * 4 + 1] = pal->rgba[idx][1];
		rgba[i * 4 + 2] = pal->rgba[idx][2];
		rgba[i * 4 + 3] = pal->rgba[idx][3];
	}
	return rgba;
}

/* ---------- FILM walk ----------
 *
 * Films script the engine's screen palette at runtime: each PLTT
 * entry's record stream issues a PALETTE_SET at its TIMESTAMP cel,
 * which writes the PLTT's slot range into the screen palette. PLTTs
 * may overlap entirely (e.g. SCENE1's PLTTs all cover slots 32-255)
 * — at any cel only the LATEST applied PLTT defines those slots.
 *
 * For static extraction we resolve each actor's palette context: find
 * when it first becomes visible (first ACTOR_SHOW show=1 timestamp),
 * then look up which PLTT was last applied at or before that cel.
 * That single PLTT, layered on top of EMPIRE's `standard`, produces
 * exactly the colors the engine paints when the actor first appears. */

typedef struct {
	uint32_t res_type;
	char res_name[9];
	int show_cel;    /* first ACTOR_SHOW(show=1) cel; -1 if never shown */
	int hide_cel;    /* first ACTOR_SHOW(show=0) AFTER show_cel; INT_MAX
					  * if the actor stays shown to the film's end */
	int entry_index; /* FilmObject array index (used for stable ordering) */
} TieFilmExtractActorInstance;

typedef struct {
	TieFilmExtractActorInstance* items;
	int count, cap;
} TieFilmExtractActorList;

typedef struct {
	char res_name[9];
	int swap_cel; /* cel of PALETTE_SET; -1 if entry has no PALETTE_SET */
	int order;    /* FilmObject array index — engine-applies in this order */
} TieFilmExtractPaletteSwap;

typedef struct {
	TieFilmExtractPaletteSwap* items;
	int count, cap;
} TieFilmExtractSwapList;

static void TieFilmExtract_ActorListGrow(TieFilmExtractActorList* l) {
	if (l->count == l->cap) {
		l->cap = l->cap ? l->cap * 2 : 16;
		l->items =
			(TieFilmExtractActorInstance*)realloc(l->items, sizeof(TieFilmExtractActorInstance) * l->cap);
		if (!l->items)
			TieFilmUtil_Die("out of memory");
	}
}
static void TieFilmExtract_SwapListGrow(TieFilmExtractSwapList* l) {
	if (l->count == l->cap) {
		l->cap = l->cap ? l->cap * 2 : 16;
		l->items = (TieFilmExtractPaletteSwap*)realloc(l->items, sizeof(TieFilmExtractPaletteSwap) * l->cap);
		if (!l->items)
			TieFilmUtil_Die("out of memory");
	}
}
static void TieFilmExtract_ActorListFree(TieFilmExtractActorList* l) {
	free(l->items);
	memset(l, 0, sizeof(*l));
}
static void TieFilmExtract_SwapListFree(TieFilmExtractSwapList* l) {
	free(l->items);
	memset(l, 0, sizeof(*l));
}

/* Walk a FILM and collect every ACTOR FilmObject (with its first-show
   cel) plus every PLTT swap event (cel + array order). Returns true
   on success. */
static bool TieFilmExtract_FilmCollect(const uint8_t* data, uint32_t size, TieFilmExtractActorList* actors,
									   TieFilmExtractSwapList* swaps) {
	TieFilmHeader h;
	if (!TieFilm_Parse(&h, data, size))
		return false;
	if (h.version != 4)
		return false;

	uint32_t iter = 0;
	for (uint16_t i = 0; i < h.array_size; i++) {
		TieFilmEntry e;
		if (!TieFilm_EntryNext(&h, &iter, &e))
			return false;
		if (e.records_size < e.data_size)
			return false;

		int cur_cel = -1;
		int first_show_cel = -1;
		int first_hide_cel = INT_MAX;
		int swap_cel = -1;

		uint16_t roff = 0;
		TieFilmRecord r;
		bool zero = false;
		while (TieFilm_RecordNext(&e, &roff, &r, &zero)) {
			switch (r.cmd) {
				case FILM_CMD_TIMESTAMP:
					if (r.payload_size >= 2)
						cur_cel = rd_u16(r.payload);
					break;
				case FILM_CMD_ACTOR_SHOW:
					/* Payload byte 0: 1=show, 0=hide. */
					if (r.payload_size >= 1 && e.type_code == FILM_TC_ACTOR) {
						if (first_show_cel < 0 && r.payload[0] == 1)
							first_show_cel = cur_cel;
						else if (first_show_cel >= 0 && first_hide_cel == INT_MAX && r.payload[0] == 0)
							first_hide_cel = cur_cel;
					}
					break;
				case FILM_CMD_PALETTE_SET:
					if (e.type_code == FILM_TC_PALETTE && swap_cel < 0)
						swap_cel = cur_cel;
					break;
				default:
					break;
			}
		}

		if (e.type_code == FILM_TC_ACTOR) {
			TieFilmExtract_ActorListGrow(actors);
			TieFilmExtractActorInstance* a = &actors->items[actors->count++];
			a->res_type = e.res_type;
			memcpy(a->res_name, e.res_name, 9);
			a->show_cel = first_show_cel;
			a->hide_cel = first_hide_cel;
			a->entry_index = i;
		} else if (e.type_code == FILM_TC_PALETTE) {
			TieFilmExtract_SwapListGrow(swaps);
			TieFilmExtractPaletteSwap* s = &swaps->items[swaps->count++];
			memcpy(s->res_name, e.res_name, 9);
			s->swap_cel = swap_cel;
			s->order = i;
		}
	}
	return true;
}

/* ---------- output paths ---------- */

static void TieFilmExtract_PathBasenameNoExt(const char* path, char* out, size_t cap) {
	const char* base = strrchr(path, '/');
	base = base ? base + 1 : path;
	const char* dot = strrchr(base, '.');
	size_t n = dot ? (size_t)(dot - base) : strlen(base);
	if (n >= cap)
		n = cap - 1;
	memcpy(out, base, n);
	out[n] = '\0';
	for (size_t i = 0; i < n; i++)
		out[i] = (char)toupper((unsigned char)out[i]);
}

static void TieFilmExtract_SanitizeName(char* s) {
	/* Replace anything outside [A-Za-z0-9_-] with '_'. */
	for (; *s; s++) {
		unsigned char c = (unsigned char)*s;
		if (!(isalnum(c) || c == '_' || c == '-'))
			*s = '_';
	}
}

/* ---------- extraction core ---------- */

typedef struct {
	const TieLfdFileChain* chain;
	const char* out_root;
	const char* lfd_basename;
	bool atlas_mode; /* ANIM as one strip + YAML layout instead of per-frame PNGs */
	bool scale;      /* `--scale`: source→4K-aspect-corrected upscale of every PNG (sprites + atlas frames) */
	bool svga_mode;  /* `--svga`: source LFDs are 640×480 SVGA build (default = 320×200 VGA) */
} TieFilmExtractContext;

#include "imgbake/ktx2_writer.h"

/* Phase-4 KTX2 emission flags — set once in main. We keep them out
 * of TieFilmExtractContext and the function-signature thread-through because
 * the only thing they change at every PNG-write site is "also emit
 * a `.ktx2` sibling," which is a leaf operation. TieFilmExtract_WriteKtx2Sibling
 * reads them and is a no-op when neither --ktx2 nor --bc7 is set. */
static bool s_ktx2_enabled = false;
static bool s_bc7_enabled = false;
static Ktx2Bc7Quality s_bc7_quality = KTX2_BC7_QUALITY_FAST;
static bool s_zstd_enabled = true; /* default on; --no-zstd to skip */

/* Derive `<png_basename>.ktx2` and write the same RGBA pixels to it
 * as a KTX2 with a generated mip chain. No-op when neither --ktx2
 * nor --bc7 is set. --bc7 wins when both are passed (BC7 is the
 * shipping target; --ktx2 is the dev fallback). Logs failures but
 * doesn't propagate — the PNG remains the canonical artefact even
 * if the KTX2 sibling fails. */
static void TieFilmExtract_WriteKtx2Sibling(const char* png_path, int w, int h, const uint8_t* rgba) {
	if (!s_ktx2_enabled && !s_bc7_enabled)
		return;
	char ktx2_path[2048];
	size_t n = strlen(png_path);
	const char* dot = strrchr(png_path, '.');
	if (dot && (size_t)(dot - png_path) > 0)
		n = (size_t)(dot - png_path);
	if (n + 6 >= sizeof ktx2_path)
		return;
	memcpy(ktx2_path, png_path, n);
	memcpy(ktx2_path + n, ".ktx2", 6); /* includes NUL */
	bool ok;
	/* Film bitmaps (DELT, ANIM, CUST, RAW) are palette-colour artwork
	 * authored on a CRT — sRGB-encoded bytes. The runtime samples
	 * these via _SRGB texture format so HW decodes to linear before
	 * the compositor's linear math. */
	if (s_bc7_enabled)
		ok = write_ktx2_bc7_with_generated_mips(ktx2_path, w, h, rgba, s_bc7_quality, KTX2_TF_SRGB,
												s_zstd_enabled);
	else
		ok = write_ktx2_rgba_with_generated_mips(ktx2_path, w, h, rgba, KTX2_TF_SRGB, s_zstd_enabled);
	if (!ok)
		fprintf(stderr, "  ktx2 sibling failed: %s\n", ktx2_path);
}

static const char* TieFilmExtract_TypeName(uint32_t fcc) {
	if (fcc == FCC_DELT)
		return "DELT";
	if (fcc == FCC_ANIM)
		return "ANIM";
	if (fcc == FCC_RAW)
		return "RAW ";
	if (fcc == FCC_CUST)
		return "CUST";
	return "????";
}

/* Dispatch the source-mode-appropriate upscale to the canonical 4K
 * 4:3 region (2880×2160). Same in/out contract as atlas_*_to_4k. */
static bool TieFilmExtract_AtlasTo4k(uint8_t** inout_pixels, int* inout_w, int* inout_h, bool svga_mode) {
	return svga_mode ? atlas_svga_to_4k(inout_pixels, inout_w, inout_h)
					 : atlas_vga_to_4k(inout_pixels, inout_w, inout_h);
}

/* Per-LFD asset root: <out_root>/<lfd_basename>/. DELT/RAW renders go
 * under sprites/, ANIMs under atlas/. Each asset is extracted at most
 * once per LFD regardless of how many films reference it. */
static void TieFilmExtract_CtxLfdDir(const TieFilmExtractContext* ctx, char* out, size_t cap) {
	snprintf(out, cap, "%s/%s", ctx->out_root, ctx->lfd_basename);
}

/* Render a DELT/RAW into <root>/<lfd>/sprites/<name>.png. Single
 * canonical extraction per LFD; films reference by short name and
 * the runtime resolver dedups across films. */
static bool TieFilmExtract_ExtractDeltOrRaw(TieFilmExtractContext* ctx, uint32_t res_type,
											const char* res_name, const Palette* pal, const char* pal_label) {
	(void)pal_label;
	const TieLfdFile* owner = NULL;
	const TieLfdFileEntry* e = TieLfdFileChain_Find(ctx->chain, FCC_DELT, res_name, &owner);
	if (!e)
		e = TieLfdFileChain_Find(ctx->chain, FCC_RAW, res_name, &owner);
	if (!e) {
		fprintf(stderr, "  %s %s: resource missing in LFD chain, skipping\n",
				TieFilmExtract_TypeName(res_type), res_name);
		return false;
	}

	Image8 img;
	if (!decode_delt(&img, TieLfdFile_Data(owner, e), e->size)) {
		fprintf(stderr, "  %s %s: decode failed\n", TieFilmExtract_TypeName(res_type), res_name);
		return false;
	}

	uint8_t* rgba = TieFilmExtract_RenderRgba(&img, pal);
	if (!rgba) {
		image_free(&img);
		return false;
	}

	int out_w = img.width, out_h = img.height;
	if (ctx->scale && !TieFilmExtract_AtlasTo4k(&rgba, &out_w, &out_h, ctx->svga_mode)) {
		fprintf(stderr, "  %s %s: upscale failed (out of memory?)\n", TieFilmExtract_TypeName(res_type),
				res_name);
		free(rgba);
		image_free(&img);
		return false;
	}

	char dir[1024];
	TieFilmExtract_CtxLfdDir(ctx, dir, sizeof(dir));
	char sprdir[1280];
	snprintf(sprdir, sizeof(sprdir), "%s/sprites", dir);
	TieFilmExtract_MakePath(sprdir);

	char safe[16];
	memset(safe, 0, sizeof(safe));
	memcpy(safe, res_name, 8);
	TieFilmExtract_SanitizeName(safe);

	char outpath[1536];
	snprintf(outpath, sizeof(outpath), "%s/%s.png", sprdir, safe);
	bool ok = write_png_rgba(outpath, out_w, out_h, rgba);
	if (ok)
		TieFilmExtract_WriteKtx2Sibling(outpath, out_w, out_h, rgba);
	free(rgba);
	image_free(&img);
	return ok;
}

/* Render a DELT/RAW into <consumer_lfd_dir>/sprites/. When `suffix` is
 * non-NULL and non-empty, the filename gets a `__<suffix>` tag; else
 * it's the bare canonical `<name>.png`. Used for palette-context
 * variants the manifest generator emits when a film's PLTT swap
 * recolours the asset away from the standard render. */
static bool TieFilmExtract_ExtractDeltOrRawVariant(const TieLfdFileChain* chain, uint32_t res_type,
												   const char* res_name, const Palette* pal,
												   const char* consumer_lfd_dir, const char* suffix,
												   bool scale, bool svga_mode) {
	(void)res_type;
	const TieLfdFile* owner = NULL;
	const TieLfdFileEntry* e = TieLfdFileChain_Find(chain, FCC_DELT, res_name, &owner);
	if (!e)
		e = TieLfdFileChain_Find(chain, FCC_RAW, res_name, &owner);
	if (!e)
		return false;

	Image8 img;
	if (!decode_delt(&img, TieLfdFile_Data(owner, e), e->size))
		return false;
	uint8_t* rgba = TieFilmExtract_RenderRgba(&img, pal);
	if (!rgba) {
		image_free(&img);
		return false;
	}

	int out_w = img.width, out_h = img.height;
	if (scale && !TieFilmExtract_AtlasTo4k(&rgba, &out_w, &out_h, svga_mode)) {
		fprintf(stderr, "  %s %s: upscale failed (out of memory?)\n", TieFilmExtract_TypeName(res_type),
				res_name);
		free(rgba);
		image_free(&img);
		return false;
	}

	char sprdir[1280];
	snprintf(sprdir, sizeof sprdir, "%s/sprites", consumer_lfd_dir);
	TieFilmExtract_MakePath(sprdir);

	char safe[16] = { 0 };
	memcpy(safe, res_name, 8);
	TieFilmExtract_SanitizeName(safe);

	char outpath[1536];
	if (suffix && suffix[0])
		snprintf(outpath, sizeof outpath, "%s/%s__%s.png", sprdir, safe, suffix);
	else
		snprintf(outpath, sizeof outpath, "%s/%s.png", sprdir, safe);
	bool ok = write_png_rgba(outpath, out_w, out_h, rgba);
	if (ok)
		TieFilmExtract_WriteKtx2Sibling(outpath, out_w, out_h, rgba);
	free(rgba);
	image_free(&img);
	return ok;
}

/* Aeron's shared packer keeps filmextract and filmview on the same layout. */

static bool TieFilmExtract_EmitAtlasStrip(TieFilmExtractContext* ctx, const char* res_name,
										  const Palette* pal, const AnimImage* anim, const char* atlasdir,
										  const char* safe) {
	(void)res_name;
	AtlasPack pack;
	if (!atlas_pack_compute(anim, &pack))
		return false;

	int atlas_w = pack.classic_atlas_w;
	int atlas_h = pack.classic_atlas_h;
	int* frame_ax = pack.ax;
	int* frame_ay = pack.ay;

	uint8_t* atlas = (uint8_t*)calloc((size_t)atlas_w * (size_t)atlas_h * 4, 1);
	if (!atlas) {
		atlas_pack_free(&pack);
		return false;
	}
	for (int i = 0; i < anim->count; i++) {
		const Image8* img = &anim->frames[i];
		if (!img->pixels)
			continue;
		uint8_t* rgba = TieFilmExtract_RenderRgba(img, pal);
		if (!rgba)
			continue;
		int ax = frame_ax[i], ay = frame_ay[i];
		if (!Aeron_AtlasBlitRgba8(atlas, atlas_w, atlas_h, rgba, img->width, img->height, ax, ay,
								  IMGBAKE_ATLAS_GUTTER, AERON_ATLAS_ADDRESS_CLAMP)) {
			free(rgba);
			free(atlas);
			atlas_pack_free(&pack);
			return false;
		}
		free(rgba);
	}

	char png_path[1536], yaml_path[1536];
	snprintf(png_path, sizeof png_path, "%s/%s.png", atlasdir, safe);
	snprintf(yaml_path, sizeof yaml_path, "%s/%s.yaml", atlasdir, safe);

	/* `--atlas-scale`: VGA→4K aspect-corrected upscale of the just-
	 * packed atlas. Done here (after packing, before write) so the
	 * grid layout, padding semantics, and per-frame state ordering
	 * are identical to the un-scaled output — only the pixel dims
	 * change. atlas_to_4k mutates atlas_w/h in place; capture the
	 * pre-upscale (classic VGA) dims first so the YAML emit can
	 * report them alongside the upscaled values. */
	int classic_atlas_w = atlas_w;
	int classic_atlas_h = atlas_h;
	int out_atlas_w = atlas_w, out_atlas_h = atlas_h;
	if (ctx->scale) {
		if (!TieFilmExtract_AtlasTo4k(&atlas, &atlas_w, &atlas_h, ctx->svga_mode)) {
			fprintf(stderr, "[atlas-scale] %s: upscale failed (out of memory?)\n", res_name);
			free(atlas);
			atlas_pack_free(&pack);
			return false;
		}
		out_atlas_w = atlas_w;
		out_atlas_h = atlas_h;
	}

	/* GPU-friendly final dim: round both axes up to a multiple of 4
	 * so the BC7 encoder doesn't waste bits on a partial-block edge.
	 * Hard-cap at 4096 px per axis — that's the floor for "ships
	 * everywhere" (mobile / WebGL2 baselines). Anything bigger
	 * means the asset team needs to split the ANIM into multiple
	 * atlases or shrink the source frames; we abort so the breakage
	 * is loud rather than silently emitting a texture some targets
	 * can't sample. */
	int padded_w = (out_atlas_w + 3) & ~3;
	int padded_h = (out_atlas_h + 3) & ~3;
	if (padded_w > 4096 || padded_h > 4096) {
		fprintf(stderr,
				"atlas %s would be %dx%d, exceeds 4096 px limit. "
				"Split the ANIM or reduce source-frame dimensions\n",
				png_path, padded_w, padded_h);
	}
	if (padded_w != out_atlas_w || padded_h != out_atlas_h) {
		uint8_t* padded = (uint8_t*)calloc((size_t)padded_w * (size_t)padded_h * 4u, 1);
		if (!padded) {
			free(atlas);
			atlas_pack_free(&pack);
			return false;
		}
		for (int y = 0; y < out_atlas_h; y++) {
			memcpy(padded + (size_t)y * (size_t)padded_w * 4u, atlas + (size_t)y * (size_t)out_atlas_w * 4u,
				   (size_t)out_atlas_w * 4u);
		}
		free(atlas);
		atlas = padded;
		out_atlas_w = padded_w;
		out_atlas_h = padded_h;
		atlas_w = padded_w;
		atlas_h = padded_h;
	}

	bool png_ok = write_png_rgba(png_path, out_atlas_w, out_atlas_h, atlas);
	if (png_ok)
		TieFilmExtract_WriteKtx2Sibling(png_path, out_atlas_w, out_atlas_h, atlas);
	free(atlas);

	/* YAML layout via the shared atlas_pack module — same schema, same
	 * coordinate scaling, same byte layout filmview's regenerate
	 * button writes. Atomic tmp+rename inside. */
	{
		char yerr[256] = { 0 };
		if (!atlas_emit_yaml(yaml_path, anim, &pack, out_atlas_w, out_atlas_h, ctx->scale, ctx->svga_mode,
							 yerr, sizeof yerr)) {
			fprintf(stderr, "  yaml write failed: %s\n", yerr);
		}
	}

	atlas_pack_free(&pack);
	return png_ok;
}

/* Render an ANIM under the given palette into <consumer_dir>/atlas/.
 * `consumer_dir` is the LFD root (`<out>/<lfd_basename>`); the
 * function appends `atlas/`. When `suffix` is non-NULL & non-empty,
 * the output uses `<name>__<suffix>` (frames-dir name in per-frame
 * mode, or atlas-strip basename in atlas mode); else bare `<name>`. */
static bool TieFilmExtract_ExtractAnimTo(const TieLfdFileChain* chain, const char* res_name,
										 const Palette* pal, const char* consumer_dir, const char* suffix,
										 bool atlas_mode, bool scale, bool svga_mode) {
	const TieLfdFile* owner = NULL;
	const TieLfdFileEntry* e = TieLfdFileChain_Find(chain, FCC_ANIM, res_name, &owner);
	if (!e) {
		fprintf(stderr, "  ANIM %s: resource missing in LFD chain, skipping\n", res_name);
		return false;
	}

	AnimImage anim;
	if (!decode_anim(&anim, TieLfdFile_Data(owner, e), e->size)) {
		fprintf(stderr, "  ANIM %s: decode failed\n", res_name);
		return false;
	}

	char safe[16] = { 0 };
	memcpy(safe, res_name, 8);
	TieFilmExtract_SanitizeName(safe);
	char outname[24];
	if (suffix && suffix[0])
		snprintf(outname, sizeof outname, "%s__%s", safe, suffix);
	else
		snprintf(outname, sizeof outname, "%s", safe);

	bool ok;
	if (atlas_mode) {
		char atlasdir[1280];
		snprintf(atlasdir, sizeof atlasdir, "%s/atlas", consumer_dir);
		TieFilmExtract_MakePath(atlasdir);
		TieFilmExtractContext tmp = { 0 };
		tmp.chain = chain;
		tmp.atlas_mode = true;
		tmp.scale = scale;
		tmp.svga_mode = svga_mode;
		ok = TieFilmExtract_EmitAtlasStrip(&tmp, res_name, pal, &anim, atlasdir, outname);
	} else {
		char atlasdir[1280];
		snprintf(atlasdir, sizeof atlasdir, "%s/atlas/%s", consumer_dir, outname);
		TieFilmExtract_MakePath(atlasdir);
		int written = 0;
		for (int i = 0; i < anim.count; i++) {
			if (!anim.frames[i].pixels)
				continue;
			uint8_t* rgba = TieFilmExtract_RenderRgba(&anim.frames[i], pal);
			if (!rgba)
				continue;
			int fw = anim.frames[i].width;
			int fh = anim.frames[i].height;
			if (scale && !TieFilmExtract_AtlasTo4k(&rgba, &fw, &fh, svga_mode)) {
				free(rgba);
				continue;
			}
			char path[1536];
			snprintf(path, sizeof path, "%s/frame_%02d.png", atlasdir, i);
			if (write_png_rgba(path, fw, fh, rgba)) {
				TieFilmExtract_WriteKtx2Sibling(path, fw, fh, rgba);
				written++;
			}
			free(rgba);
		}
		ok = (written > 0);
	}
	anim_free(&anim);
	return ok;
}

/* Wrapper for the canonical (no-suffix) emit from extract_lfd_assets. */
static bool TieFilmExtract_ExtractAnim(TieFilmExtractContext* ctx, const char* res_name, const Palette* pal,
									   const char* pal_label) {
	(void)pal_label;
	char dir[1024];
	TieFilmExtract_CtxLfdDir(ctx, dir, sizeof dir);
	return TieFilmExtract_ExtractAnimTo(ctx->chain, res_name, pal, dir, NULL, ctx->atlas_mode, ctx->scale,
										ctx->svga_mode);
}

/* ---------- film-palette walk ----------
 *
 * Build the palette in effect when actor X first becomes visible at
 * `show_cel`. Engine model: black 256-buffer → `standard` (engine
 * default in EMPIRE.LFD) → every film PLTT whose swap_cel <=
 * show_cel, applied in (cel ascending, then FilmObject array order
 * ascending) sequence. Two PLTTs swapping at the same cel both
 * affect the screen palette; the later array-order one wins in any
 * overlapping slot range, but disjoint ranges accumulate.
 *
 * `label` receives a short tag identifying the last PLTT applied —
 * used as the variant filename suffix when palette context produces
 * pixels different from the canonical render. */
static int TieFilmExtract_ActiveSwapOrder(const TieFilmExtractSwapList* swaps, int show_cel, int* out_idx) {
	int target = (show_cel >= 0) ? show_cel : 0x7FFFFFFF;
	int n = 0;
	for (int i = 0; i < swaps->count; i++) {
		if (swaps->items[i].swap_cel < 0)
			continue;
		if (swaps->items[i].swap_cel > target)
			continue;
		out_idx[n++] = i;
	}
	for (int i = 1; i < n; i++) {
		int v = out_idx[i];
		const TieFilmExtractPaletteSwap* vs = &swaps->items[v];
		int j = i - 1;
		while (j >= 0) {
			const TieFilmExtractPaletteSwap* ps = &swaps->items[out_idx[j]];
			bool gt =
				(ps->swap_cel > vs->swap_cel) || (ps->swap_cel == vs->swap_cel && ps->order > vs->order);
			if (!gt)
				break;
			out_idx[j + 1] = out_idx[j];
			j--;
		}
		out_idx[j + 1] = v;
	}
	if (n == 0 && show_cel < 0 && swaps->count > 0)
		out_idx[n++] = 0;
	return n;
}

static void TieFilmExtract_BuildActorPalette(const TieLfdFileChain* chain,
											 const TieFilmExtractSwapList* swaps, int show_cel, Palette* out,
											 char* label, size_t label_cap) {
	palette_black(out);

	const TieLfdFile* std_owner = NULL;
	const TieLfdFileEntry* std_entry = TieLfdFileChain_Find(chain, FCC_PLTT, "standard", &std_owner);
	if (std_entry)
		palette_overlay(out, TieLfdFile_Data(std_owner, std_entry), std_entry->size);

	int* order = (int*)calloc((size_t)(swaps->count + 1), sizeof(int));
	int n = TieFilmExtract_ActiveSwapOrder(swaps, show_cel, order);
	const char* last_name = "standard";
	for (int i = 0; i < n; i++) {
		const TieFilmExtractPaletteSwap* s = &swaps->items[order[i]];
		const TieLfdFile* o = NULL;
		const TieLfdFileEntry* pe = TieLfdFileChain_Find(chain, FCC_PLTT, s->res_name, &o);
		if (pe) {
			palette_overlay(out, TieLfdFile_Data(o, pe), pe->size);
			last_name = s->res_name;
		}
	}
	free(order);
	snprintf(label, label_cap, "%s", last_name);
	TieFilmExtract_SanitizeName(label);
}

/* One step of an actor's palette timeline: from `from_cel` onward (until
 * the next entry, or the actor hides), the asset renders against this
 * palette state. `label` is the last applied PLTT's name (for filename
 * suffix when this state isn't the bare variant). */
typedef struct {
	int from_cel;
	Palette pal;
	char label[16];
} TieFilmExtractPaletteState;

typedef struct {
	TieFilmExtractPaletteState* items;
	int count, cap;
} TieFilmExtractPaletteStateList;

static void TieFilmExtract_PalstatelistGrow(TieFilmExtractPaletteStateList* l) {
	if (l->count == l->cap) {
		l->cap = l->cap ? l->cap * 2 : 4;
		l->items =
			(TieFilmExtractPaletteState*)realloc(l->items, sizeof(TieFilmExtractPaletteState) * l->cap);
		if (!l->items)
			TieFilmUtil_Die("out of memory");
	}
}
static void TieFilmExtract_PalstatelistFree(TieFilmExtractPaletteStateList* l) {
	free(l->items);
	memset(l, 0, sizeof(*l));
}

/* Walk the actor's [show_cel, hide_cel) lifetime and emit one TieFilmExtractPaletteState
 * per distinct visible appearance. The first entry's palette is the
 * state at show_cel; each subsequent entry is the cumulative palette
 * after a PLTT swap whose effect on the asset's `used` indices changes
 * the hash. Swaps that don't touch any used index are silently absorbed.
 *
 * Multiple PLTT swaps at the same cel are processed in array-order
 * (engine semantics) and collapsed into one timeline entry — only the
 * post-cel cumulative state matters. */
static void TieFilmExtract_WalkActorLifetime(const TieLfdFileChain* chain,
											 const TieFilmExtractSwapList* swaps, const uint8_t used[32],
											 int show_cel, int hide_cel,
											 TieFilmExtractPaletteStateList* out) {
	out->count = 0;
	if (show_cel < 0)
		return;

	/* Seed with the palette at show_cel (existing snapshot logic). */
	TieFilmExtract_PalstatelistGrow(out);
	TieFilmExtractPaletteState* cur = &out->items[out->count++];
	cur->from_cel = show_cel;
	TieFilmExtract_BuildActorPalette(chain, swaps, show_cel, &cur->pal, cur->label, sizeof cur->label);
	uint64_t cur_hash = TieFilmExtract_PaletteUsedHash(&cur->pal, used);

	/* Collect in-lifetime swaps in order. Range is (show_cel, hide_cel)
	 * — the show_cel snapshot already absorbed any PLTTs at show_cel
	 * itself, so the streaming walk picks up from cel > show_cel. */
	int* order = (int*)calloc((size_t)(swaps->count + 1), sizeof(int));
	int nall = 0;
	for (int i = 0; i < swaps->count; i++) {
		int c = swaps->items[i].swap_cel;
		if (c < 0 || c <= show_cel || c >= hide_cel)
			continue;
		order[nall++] = i;
	}
	for (int i = 1; i < nall; i++) {
		int v = order[i];
		const TieFilmExtractPaletteSwap* vs = &swaps->items[v];
		int j = i - 1;
		while (j >= 0) {
			const TieFilmExtractPaletteSwap* ps = &swaps->items[order[j]];
			bool gt =
				(ps->swap_cel > vs->swap_cel) || (ps->swap_cel == vs->swap_cel && ps->order > vs->order);
			if (!gt)
				break;
			order[j + 1] = order[j];
			j--;
		}
		order[j + 1] = v;
	}

	/* Stream the swaps in same-cel batches. Each batch overlays every
	 * PLTT at that cel into `running`, then checks the final hash —
	 * intermediate hashes inside the batch are irrelevant. Skipping the
	 * per-swap hash check is also why "two same-cel swaps whose net
	 * effect leaves the asset unchanged" produces no timeline entry:
	 * the cur_hash comparison sees the post-batch state, not transient
	 * intermediate states. */
	Palette running = cur->pal;
	int k = 0;
	while (k < nall) {
		int cel = swaps->items[order[k]].swap_cel;
		const char* batch_label = NULL;
		while (k < nall && swaps->items[order[k]].swap_cel == cel) {
			const TieFilmExtractPaletteSwap* s = &swaps->items[order[k]];
			const TieLfdFile* o = NULL;
			const TieLfdFileEntry* pe = TieLfdFileChain_Find(chain, FCC_PLTT, s->res_name, &o);
			if (pe) {
				palette_overlay(&running, TieLfdFile_Data(o, pe), pe->size);
				batch_label = s->res_name;
			}
			k++;
		}
		uint64_t new_hash = TieFilmExtract_PaletteUsedHash(&running, used);
		if (new_hash == cur_hash)
			continue;
		TieFilmExtract_PalstatelistGrow(out);
		TieFilmExtractPaletteState* ns = &out->items[out->count++];
		ns->from_cel = cel;
		ns->pal = running;
		snprintf(ns->label, sizeof ns->label, "%s", batch_label ? batch_label : "");
		TieFilmExtract_SanitizeName(ns->label);
		cur_hash = new_hash;
	}

	free(order);
}

/* ---------- palette-aware variant analysis ----------
 *
 * Two-pass design avoids the "broken canonical" problem where an
 * asset like dbridge — only ever shown with its own PLTT swap — was
 * also extracted under `standard`, producing a wrong-coloured PNG
 * the artist would never use.
 *
 * Pass 1 (analyze_films): walk every primary-LFD film, compute each
 * actor's effective palette + hash over its used indices. Aggregate
 * into a list of (asset, hash) variants with film-use counts.
 *
 * Pass 2 (emit_assets, generate_film_manifest): emit one render per
 * unique (asset, hash) — the most-common hash for each asset gets
 * the bare canonical name, others get `__<pal_label>` suffixes.
 * For chain-owned assets, skip emission when the hash matches the
 * standard-palette canonical that the owning LFD's filmextract
 * already produced (manifest references bare name → resolves
 * through chain). Manifests reference the right name per actor.
 *
 * Assets in primary LFD that aren't used by ANY primary film fall
 * through to a default standard render — they may be referenced by
 * downstream consumer LFDs. */

#define MAX_VARIANTS 256

typedef struct {
	char res_name[9];
	uint32_t res_type;
	uint64_t hash;       /* palette_used_hash over used indices */
	char pal_label[16];  /* used as filename suffix when !is_bare */
	Palette pal;         /* palette object for re-rendering */
	int film_uses;       /* tiebreak for picking bare-name */
	bool is_bare;        /* this variant gets the bare canonical name */
	bool emit_locally;   /* render & write locally (vs. resolve to chain) */
	bool standard_match; /* hash equals standard-palette hash for this asset */
} TieScene2dAssetVariant;

typedef struct {
	TieScene2dAssetVariant items[MAX_VARIANTS];
	int count;
} TieFilmExtractVariantTable;

static int TieFilmExtract_VariantLookup(const TieFilmExtractVariantTable* t, const char* name,
										uint64_t hash) {
	for (int i = 0; i < t->count; i++) {
		if (t->items[i].hash == hash && memcmp(t->items[i].res_name, name, 8) == 0)
			return i;
	}
	return -1;
}

static int TieFilmExtract_VariantAdd(TieFilmExtractVariantTable* t, const char* name, uint32_t type,
									 uint64_t hash, const Palette* pal, const char* label) {
	int i = TieFilmExtract_VariantLookup(t, name, hash);
	if (i >= 0) {
		t->items[i].film_uses++;
		return i;
	}
	if (t->count >= MAX_VARIANTS)
		return -1;
	TieScene2dAssetVariant* v = &t->items[t->count++];
	memset(v, 0, sizeof *v);
	memcpy(v->res_name, name, 8);
	v->res_type = type;
	v->hash = hash;
	v->pal = *pal;
	snprintf(v->pal_label, sizeof v->pal_label, "%s", label);
	v->film_uses = 1;
	return t->count - 1;
}

/* True when at least one other variant for this res_name exists with
 * a different hash. Drives whether the bare-name variant gets a
 * filename suffix (it doesn't when there's only one variant). */
static bool TieFilmExtract_AssetHasMultipleVariants(const TieFilmExtractVariantTable* t,
													const char* res_name) {
	int hash_count = 0;
	uint64_t first = 0;
	for (int i = 0; i < t->count; i++) {
		if (memcmp(t->items[i].res_name, res_name, 8) != 0)
			continue;
		if (hash_count == 0) {
			first = t->items[i].hash;
			hash_count = 1;
		} else if (t->items[i].hash != first) {
			return true;
		}
	}
	return false;
}

/* Walk every FILM in the primary LFD, compute each DELT/RAW actor's
 * effective palette context, hash it against the actor's used indices,
 * and aggregate into the variant table. Builds the dataset that
 * extract_lfd_assets and generate_film_manifest both consult. */
static void TieFilmExtract_AnalyzeFilms(const TieLfdFileChain* chain, const TieLfdFile* primary,
										const char* film_filter, TieFilmExtractVariantTable* out) {
	out->count = 0;
	for (uint32_t i = 0; i < primary->count; i++) {
		const TieLfdFileEntry* fe = &primary->entries[i];
		if (fe->type != FCC_FILM)
			continue;
		if (film_filter && strncmp(film_filter, fe->name, 8) != 0)
			continue;

		TieFilmExtractActorList actors = { 0 };
		TieFilmExtractSwapList swaps = { 0 };
		if (!TieFilmExtract_FilmCollect(TieLfdFile_Data(primary, fe), fe->size, &actors, &swaps)) {
			TieFilmExtract_ActorListFree(&actors);
			TieFilmExtract_SwapListFree(&swaps);
			continue;
		}
		/* Group instances by res_name; walk the per-name union
		 * lifetime once. Mirrors generate_film_manifest's emit-time
		 * walk so both passes produce the same hash sequence — keeps
		 * variant_lookup hits in the manifest writer. */
		for (int a = 0; a < actors.count; a++) {
			const TieFilmExtractActorInstance* ai = &actors.items[a];
			if (ai->res_type != FCC_DELT && ai->res_type != FCC_RAW && ai->res_type != FCC_ANIM)
				continue;
			bool first = true;
			for (int j = 0; j < a; j++) {
				const TieFilmExtractActorInstance* aj = &actors.items[j];
				if ((aj->res_type == FCC_DELT || aj->res_type == FCC_RAW || aj->res_type == FCC_ANIM) &&
					memcmp(aj->res_name, ai->res_name, 8) == 0) {
					first = false;
					break;
				}
			}
			if (!first)
				continue;

			uint8_t used[32];
			if (!TieFilmExtract_SpriteUsedIndices(chain, ai->res_type, ai->res_name, used))
				continue;

			int union_show = INT_MAX, union_hide = -1;
			for (int j = a; j < actors.count; j++) {
				const TieFilmExtractActorInstance* aj = &actors.items[j];
				if (memcmp(aj->res_name, ai->res_name, 8) != 0)
					continue;
				if (aj->show_cel < 0)
					continue;
				if (aj->show_cel < union_show)
					union_show = aj->show_cel;
				if (aj->hide_cel > union_hide)
					union_hide = aj->hide_cel;
			}

			TieFilmExtractPaletteStateList states = { 0 };
			if (union_show == INT_MAX) {
				/* No SHOW record in this film. Two cases:
				 *   (a) Engine-driven actor — declared in the FILM
				 *       array so its asset preloads, but visibility
				 *       is flipped imperatively from C code (e.g.
				 *       reg-dora in REGISTER.LFD, animated by
				 *       register.c::user_Door when the player
				 *       triggers a delete/protected/exit prompt).
				 *   (b) Dead leftover entry (e.g. tbridg14 in
				 *       BRIDGE/brdg1c_f's hide-only copy-paste).
				 *
				 * The FILM script can't distinguish these — both
				 * look the same. Register a single synthetic state
				 * at cel 0 (= the post-rewind palette: standard +
				 * every cel-0 PLTT_SET) so case (a) gets a
				 * correctly-coloured extract. Case (b) gets one
				 * unused render — same disk cost as the previous
				 * standard-only render via extract_lfd_assets, but
				 * the colours are right if anyone ever looks. */
				TieFilmExtract_WalkActorLifetime(chain, &swaps, used,
												 /*show_cel=*/0,
												 /*hide_cel=*/1, &states);
			} else {
				TieFilmExtract_WalkActorLifetime(chain, &swaps, used, union_show, union_hide, &states);
			}
			for (int s = 0; s < states.count; s++) {
				const TieFilmExtractPaletteState* ps = &states.items[s];
				uint64_t hash = TieFilmExtract_PaletteUsedHash(&ps->pal, used);
				TieFilmExtract_VariantAdd(out, ai->res_name, ai->res_type, hash, &ps->pal, ps->label);
			}
			TieFilmExtract_PalstatelistFree(&states);
		}
		TieFilmExtract_ActorListFree(&actors);
		TieFilmExtract_SwapListFree(&swaps);
	}
}

/* For each variant, decide:
 *  - is_bare: most-common hash among siblings (highest film_uses)
 *  - standard_match: does this variant's hash equal the standard-
 *    palette hash for the same asset's used indices?
 *  - emit_locally: should we render & write under the primary LFD?
 *    (Always true when asset is owned by primary; for chain-owned,
 *    only when standard_match is false — otherwise the chain's
 *    canonical is identical and bare-name reference suffices.) */
static void TieFilmExtract_DecideVariants(const TieLfdFileChain* chain, const TieLfdFile* primary,
										  TieFilmExtractVariantTable* t) {
	/* Build standard-palette object once for hash comparison. */
	Palette standard;
	palette_black(&standard);
	const TieLfdFile* std_owner = NULL;
	const TieLfdFileEntry* std_entry = TieLfdFileChain_Find(chain, FCC_PLTT, "standard", &std_owner);
	if (std_entry)
		palette_overlay(&standard, TieLfdFile_Data(std_owner, std_entry), std_entry->size);

	/* Pick the most-used hash as the bare asset, with first-seen tie breaking.
	 * Scan siblings in both directions so exactly one receives the bare name. */
	for (int i = 0; i < t->count; i++) {
		TieScene2dAssetVariant* v = &t->items[i];
		int best = i;
		int best_uses = v->film_uses;
		bool sibling_already_bare = false;
		for (int j = 0; j < t->count; j++) {
			if (j == i)
				continue;
			TieScene2dAssetVariant* w = &t->items[j];
			if (memcmp(w->res_name, v->res_name, 8) != 0)
				continue;
			if (w->is_bare)
				sibling_already_bare = true;
			if (w->film_uses > best_uses) {
				best = j;
				best_uses = w->film_uses;
			}
		}
		if (sibling_already_bare || best != i)
			continue;
		v->is_bare = true;
	}

	/* Standard-match + emit_locally per variant. */
	for (int i = 0; i < t->count; i++) {
		TieScene2dAssetVariant* v = &t->items[i];
		uint8_t used[32];
		if (!TieFilmExtract_SpriteUsedIndices(chain, v->res_type, v->res_name, used))
			continue;
		uint64_t standard_hash = TieFilmExtract_PaletteUsedHash(&standard, used);
		v->standard_match = (v->hash == standard_hash);

		const TieLfdFile* owner = NULL;
		(void)TieLfdFileChain_Find(chain, v->res_type == FCC_RAW ? FCC_RAW : FCC_DELT, v->res_name, &owner);
		bool owned_by_primary = (owner == primary);

		if (owned_by_primary) {
			/* Primary owns it — must emit locally with whichever
			 * palette is active, else there's no rendered file at
			 * all (we suppress the asset's standard-palette emit
			 * in extract_lfd_assets when the asset is in the
			 * variant table). */
			v->emit_locally = true;
		} else {
			/* Chain owns it. The owning LFD's filmextract emits
			 * the standard render. Emit locally only when this
			 * variant's hash differs (the standard render in chain
			 * is wrong for this film context). */
			v->emit_locally = !v->standard_match;
		}
	}
}

/* True when this asset name appears in the variant table at all
 * (i.e. some primary film references it). Used by extract_lfd_assets
 * to skip the standard-palette render for assets that the manifest
 * pass will emit with the right per-film palette instead. */
static bool TieFilmExtract_VariantTableHasAsset(const TieFilmExtractVariantTable* t, const char* name) {
	for (int i = 0; i < t->count; i++)
		if (memcmp(t->items[i].res_name, name, 8) == 0)
			return true;
	return false;
}

/* Render and write every emit_locally variant into the primary LFD's
 * sprites/ (DELT/RAW) or atlas/ (ANIM). Filename: bare `<name>` for
 * the asset's most-common hash, `<name>__<pal_label>` for others. */
static void TieFilmExtract_EmitVariants(const TieLfdFileChain* chain, const char* out_root,
										const char* lfd_basename, const TieFilmExtractVariantTable* t,
										bool atlas_mode, bool scale, bool svga_mode) {
	char lfd_dir[1024];
	snprintf(lfd_dir, sizeof lfd_dir, "%s/%s", out_root, lfd_basename);
	int emitted = 0;
	for (int i = 0; i < t->count; i++) {
		const TieScene2dAssetVariant* v = &t->items[i];
		if (!v->emit_locally)
			continue;
		const char* suffix = v->is_bare ? NULL : v->pal_label;
		bool ok = false;
		if (v->res_type == FCC_DELT || v->res_type == FCC_RAW)
			ok = TieFilmExtract_ExtractDeltOrRawVariant(chain, v->res_type, v->res_name, &v->pal, lfd_dir,
														suffix, scale, svga_mode);
		else if (v->res_type == FCC_ANIM)
			ok = TieFilmExtract_ExtractAnimTo(chain, v->res_name, &v->pal, lfd_dir, suffix, atlas_mode, scale,
											  svga_mode);
		if (ok)
			emitted++;
	}
	if (emitted > 0)
		printf("LFD %s: %d palette-context render(s) emitted\n", lfd_basename, emitted);
}

/* ---------- per-LFD asset extraction ----------
 *
 * Emits one canonical render per DELT/RAW/ANIM that's NOT covered by
 * the variant table (i.e. not used in any primary film, or an ANIM
 * since variant analysis is DELT/RAW-only). Uses the engine's
 * `standard` palette (or whatever --palette names). Variants for
 * used DELT/RAW are emitted by emit_variants instead. */
static void TieFilmExtract_ExtractLfdAssets(const TieLfdFileChain* chain, const TieLfdFile* lfd,
											const char* out_root, const char* lfd_basename,
											const char* const* forced_palettes, int forced_palettes_count,
											bool atlas_mode, bool scale, bool svga_mode,
											const TieFilmExtractVariantTable* handled_by_variants) {
	Palette pal;
	palette_black(&pal);

	const TieLfdFile* std_owner = NULL;
	const TieLfdFileEntry* std_entry = TieLfdFileChain_Find(chain, FCC_PLTT, "standard", &std_owner);
	if (std_entry)
		palette_overlay(&pal, TieLfdFile_Data(std_owner, std_entry), std_entry->size);

	/* Overlay every --palette argument in argv order on top of
	 * "standard". Mirrors the engine's PLTT accumulator: each PLTT
	 * writes its own slot range, leaving other slots untouched. */
	char pal_label[64];
	if (forced_palettes_count > 0) {
		size_t lpos = 0;
		pal_label[0] = '\0';
		for (int i = 0; i < forced_palettes_count; i++) {
			const TieLfdFile* o = NULL;
			const TieLfdFileEntry* pe = TieLfdFileChain_Find(chain, FCC_PLTT, forced_palettes[i], &o);
			if (!pe)
				TieFilmUtil_Die("palette '%s' not found in any LFD", forced_palettes[i]);
			palette_overlay(&pal, TieLfdFile_Data(o, pe), pe->size);
			int n = snprintf(pal_label + lpos, sizeof pal_label - lpos, "%s%s", lpos ? "_" : "",
							 forced_palettes[i]);
			if (n < 0 || (size_t)n >= sizeof pal_label - lpos)
				break;
			lpos += (size_t)n;
		}
	} else {
		snprintf(pal_label, sizeof pal_label, "standard");
	}
	TieFilmExtract_SanitizeName(pal_label);

	TieFilmExtractContext ctx = { 0 };
	ctx.chain = chain;
	ctx.out_root = out_root;
	ctx.lfd_basename = lfd_basename;
	ctx.atlas_mode = atlas_mode;
	ctx.scale = scale;
	ctx.svga_mode = svga_mode;

	int extracted = 0;
	for (uint32_t i = 0; i < lfd->count; i++) {
		const TieLfdFileEntry* e = &lfd->entries[i];
		char name[9] = { 0 };
		memcpy(name, e->name, 8);
		bool ok = false;
		if (e->type == FCC_DELT || e->type == FCC_RAW) {
			/* Skip when a primary-LFD film references this asset:
			 * emit_variants will write the per-film palette
			 * render(s) instead. The standard render here would
			 * be the wrong palette context — broken for the
			 * artist to use as a reference. */
			if (handled_by_variants && TieFilmExtract_VariantTableHasAsset(handled_by_variants, name))
				continue;
			ok = TieFilmExtract_ExtractDeltOrRaw(&ctx, e->type, name, &pal, pal_label);
		} else if (e->type == FCC_ANIM) {
			/* Same skip rule as DELT/RAW: when a primary film
			 * references this ANIM, its per-context render comes
			 * from emit_variants. The standard-palette extract
			 * here would otherwise be all-black for any LFD whose
			 * ANIMs use indices not defined in `standard` (e.g.
			 * EMPEROR.LFD, where every actor uses scene-PLTT
			 * colours). */
			if (handled_by_variants && TieFilmExtract_VariantTableHasAsset(handled_by_variants, name))
				continue;
			ok = TieFilmExtract_ExtractAnim(&ctx, name, &pal, pal_label);
		}
		if (ok)
			extracted++;
	}
	printf("LFD %s: %d asset(s) → %s/%s/\n", lfd_basename, extracted, out_root, lfd_basename);
}

/* ---------- per-FILM manifest generation ----------
 *
 * Generates <out_root>/<lfd_basename>/films/<film>/manifest.yaml.
 * Manifests reference shared assets by short name; the LFD chain
 * (extras list) tells the runtime resolver where to look.
 *
 * Per-actor name lookup goes through the variant table built by
 * analyze_films. For each DELT/RAW actor, we recompute its palette
 * context (cheap), hash it, and look up the matching variant's
 * filename — bare or `__<pal_label>` — that emit_variants wrote
 * earlier. ANIMs always reference the bare canonical (variant
 * detection isn't applied to ANIMs — see analyze_films). */
static void TieFilmExtract_GenerateFilmManifest(const TieLfdFileChain* chain, const TieLfdFile* primary,
												const TieLfdFileEntry* film_entry, const char* out_root,
												const char* lfd_basename, const char** extras_basenames,
												int n_extras, bool atlas_mode, bool scale, bool svga_mode,
												const TieFilmExtractVariantTable* variants) {
	(void)svga_mode; /* Manifests reference asset filenames, not pixel coords. */
	char film_name[9] = { 0 };
	memcpy(film_name, film_entry->name, 8);

	TieFilmExtractActorList actors = { 0 };
	TieFilmExtractSwapList swaps = { 0 };
	if (!TieFilmExtract_FilmCollect(TieLfdFile_Data(primary, film_entry), film_entry->size, &actors,
									&swaps)) {
		fprintf(stderr, "FILM %s: parse failed\n", film_name);
		TieFilmExtract_ActorListFree(&actors);
		TieFilmExtract_SwapListFree(&swaps);
		return;
	}

	char dir[1024];
	snprintf(dir, sizeof dir, "%s/%s/films/%s", out_root, lfd_basename, film_name);
	TieFilmExtract_MakePath(dir);

	char manifest_path[1280];
	snprintf(manifest_path, sizeof manifest_path, "%s/manifest.yaml", dir);
	FILE* f = fopen(manifest_path, "w");
	if (!f) {
		fprintf(stderr, "FILM %s: cannot open %s for write: %s\n", film_name, manifest_path, strerror(errno));
		TieFilmExtract_ActorListFree(&actors);
		TieFilmExtract_SwapListFree(&swaps);
		return;
	}

	fprintf(f, "# Film manifest — generated by filmextract.\n"
			   "# Edit per-actor entries to override defaults; assets resolve\n"
			   "# through the LFD chain (this LFD's dir, then `extras` in order).\n"
			   "complete: true\n");
	if (n_extras > 0) {
		fprintf(f, "extras: [");
		for (int i = 0; i < n_extras; i++)
			fprintf(f, "%s%s", i ? ", " : "", extras_basenames[i]);
		fprintf(f, "]\n");
	}
	fprintf(f, "\nactors:\n");

	/* One bare-name actor entry per unique res_name. Multi-instance
	 * disambiguation (`<name>#<idx>`) is left for the asset team to
	 * add by hand when behaviour needs to differ between instances. */
	for (int i = 0; i < actors.count; i++) {
		const TieFilmExtractActorInstance* a = &actors.items[i];
		if (a->res_type == FCC_CUST)
			continue; /* procedural actors — no static asset */
		if (a->res_type != FCC_DELT && a->res_type != FCC_RAW && a->res_type != FCC_ANIM)
			continue;

		bool first = true;
		for (int j = 0; j < i; j++) {
			if (memcmp(actors.items[j].res_name, a->res_name, 8) == 0) {
				first = false;
				break;
			}
		}
		if (!first)
			continue;

		/* Per-name union lifetime: earliest show among all instances of
		 * this res_name to latest hide. The walker streams every PLTT
		 * swap inside that window that changes the asset's hash, so the
		 * timeline covers all instances naturally. Per-instance variant
		 * splits aren't needed because the engine drives one global
		 * play cursor — every visible instance at cel C sees the same
		 * palette, hence the same variant. */
		int union_show = INT_MAX;
		int union_hide = -1;
		for (int j = i; j < actors.count; j++) {
			const TieFilmExtractActorInstance* b = &actors.items[j];
			if (memcmp(b->res_name, a->res_name, 8) != 0)
				continue;
			if (b->show_cel < 0)
				continue;
			if (b->show_cel < union_show)
				union_show = b->show_cel;
			if (b->hide_cel > union_hide)
				union_hide = b->hide_cel;
		}
		bool ever_shown = (union_show != INT_MAX);

		bool is_anim = (a->res_type == FCC_ANIM);
		const char* kind_field = is_anim ? (atlas_mode ? "atlas" : "frames") : "sprite";

		char safe[16];
		memset(safe, 0, sizeof safe);
		memcpy(safe, a->res_name, 8);
		TieFilmExtract_SanitizeName(safe);

		/* Build the timeline. Each entry → look up the matching
		 * variant table row → produce its filename (bare or
		 * `__<pal_label>` suffix). Skip the lifetime walk for
		 * never-shown actors — emit a bare scalar so the manifest
		 * stays well-formed (the asset team can still override) but
		 * don't fabricate a timeline from palette swaps the actor
		 * never witnesses. */
		TieFilmExtractPaletteStateList states = { 0 };
		uint8_t used[32];
		bool have_used = TieFilmExtract_SpriteUsedIndices(chain, a->res_type, a->res_name, used);
		if (have_used && variants && ever_shown) {
			TieFilmExtract_WalkActorLifetime(chain, &swaps, used, union_show, union_hide, &states);
		}

		fprintf(f, "  %s:\n", safe);
		if (states.count <= 1) {
			/* Common case — emit scalar. */
			const char* ref_name = safe;
			char variant_name[24];
			if (states.count == 1 && variants) {
				uint64_t hash = TieFilmExtract_PaletteUsedHash(&states.items[0].pal, used);
				int vi = TieFilmExtract_VariantLookup(variants, a->res_name, hash);
				if (vi >= 0 && !variants->items[vi].is_bare) {
					snprintf(variant_name, sizeof variant_name, "%s__%s", safe,
							 variants->items[vi].pal_label);
					ref_name = variant_name;
				}
			}
			fprintf(f, "    %s: %s\n", kind_field, ref_name);
			if (is_anim && atlas_mode)
				fprintf(f, "    layout: %s\n", ref_name);
		} else {
			/* Multi-state timeline — emit sequence. Layout is
			 * implied to track name (each variant's <name>.yaml
			 * sibling), so no separate `layout:` field. */
			fprintf(f, "    %s:\n", kind_field);
			for (int s = 0; s < states.count; s++) {
				const TieFilmExtractPaletteState* ps = &states.items[s];
				uint64_t hash = TieFilmExtract_PaletteUsedHash(&ps->pal, used);
				int vi = TieFilmExtract_VariantLookup(variants, a->res_name, hash);
				char variant_name[24];
				const char* ref_name = safe;
				if (vi >= 0 && !variants->items[vi].is_bare) {
					snprintf(variant_name, sizeof variant_name, "%s__%s", safe,
							 variants->items[vi].pal_label);
					ref_name = variant_name;
				}
				fprintf(f, "      - { from_cel: %d, name: %s }\n", ps->from_cel, ref_name);
			}
		}
		/* Scaled assets ship at 4K target dim and almost always render
		 * smaller (sub-4K window). Default-emit `filter: linear` so
		 * SDL bilinear-samples the downscale; otherwise NEAREST
		 * scaling produces visible stairstepping. The asset team can
		 * still override per-actor by editing the manifest. */
		if (scale)
			fprintf(f, "    filter: linear\n");
		TieFilmExtract_PalstatelistFree(&states);
	}

	fclose(f);
	printf("FILM %s → %s\n", film_name, manifest_path + strlen(out_root) + 1);
	TieFilmExtract_ActorListFree(&actors);
	TieFilmExtract_SwapListFree(&swaps);
}

/* ---------- main ---------- */

static void TieFilmExtract_Usage(void) {
	fprintf(stderr, "Usage: filmextract <primary.lfd> <output-dir>\n"
					"                   [--film <name>] [--palette <pltt-name>]...\n"
					"                   [--atlas] [--scale] [--svga]\n"
					"                   [--ktx2 | --bc7 [--bc7-quality {fast|med|uber}]]\n"
					"                   [--no-zstd]\n"
					"                   [--extra <lfd>]...\n"
					"\n"
					"Extracts every DELT/RAW/ANIM in <primary.lfd> ONCE into\n"
					"  <output-dir>/<LFD_BASENAME>/{sprites,atlas}/\n"
					"and generates per-film manifests at\n"
					"  <output-dir>/<LFD_BASENAME>/films/<film>/manifest.yaml\n"
					"Each manifest's `extras: [...]` lists the --extra LFDs so the\n"
					"runtime resolver walks the same chain when looking up assets.\n"
					"\n"
					"Run filmextract once per LFD: each invocation extracts that\n"
					"LFD's owned assets. Films in the primary LFD reference shared\n"
					"assets in --extra LFDs by name; those assets must have been\n"
					"extracted by a separate filmextract run on each --extra LFD.\n"
					"\n"
					"Options:\n"
					"  --film <name>: only generate manifest for this film\n"
					"                 (assets still extracted in full).\n"
					"  --palette <pltt-name>: overlay this PLTT on top of\n"
					"                         'standard' when rendering. Repeatable:\n"
					"                         each occurrence overlays in argv order,\n"
					"                         matching the engine's PLTT accumulator\n"
					"                         (each PLTT writes only its slot range).\n"
					"                         Use this for asset-only LFDs whose\n"
					"                         sprites span multiple PLTTs' slots.\n"
					"  --atlas: ANIM frames packed into one strip PNG + YAML\n"
					"           layout; default emits one PNG per frame.\n"
					"  --scale: source-art→4K aspect-corrected upscale of every\n"
					"           emitted PNG (sprites + atlas frames + per-frame\n"
					"           ANIM). VGA mode (default): NN ×(9,11) then Lanczos-3\n"
					"           vertical → (orig_w×9, round(orig_h×10.8)).\n"
					"           SVGA mode (--svga): NN ×5 in both axes then\n"
					"           Lanczos-3 downscale → round(orig×4.5) per axis.\n"
					"  --svga:  source LFDs are the 640×480 SVGA build (default is\n"
					"           320×200 VGA). Selects the SVGA upscale formula\n"
					"           when --scale is also set; otherwise affects\n"
					"           nothing (the source dims travel through every\n"
					"           DELT/RAW/ANIM decoder unchanged).\n"
					"  --ktx2:  also write a `<name>.ktx2` sibling for every emitted\n"
					"           PNG. Uncompressed RGBA8 with a generated mip chain;\n"
					"           the runtime Aeron loader prefers .ktx2 over .png.\n"
					"  --bc7:   like --ktx2 but BC7-compressed (4× VRAM reduction\n"
					"           vs. RGBA8). Mutually compatible with --ktx2; if both\n"
					"           are passed, --bc7 wins.\n"
					"  --bc7-quality {fast|med|uber}:\n"
					"           BC7 encoder preset. fast (default) ≈ ms per 4K block;\n"
					"           uber ≈ seconds per 4K block, near-lossless.\n"
					"  --no-zstd: skip the zstd supercompression pass on the KTX2\n"
					"           level data. KTX2 files are larger; useful when a\n"
					"           downstream tool can't handle supercompressionScheme=2.\n"
					"  --extra <lfd>: chain LFD for resource lookup during manifest\n"
					"                 generation; recorded in manifest's extras:.\n");
	exit(2);
}

int main(int argc, char** argv) {
	const char* lfd_path = NULL;
	const char* out_dir = NULL;
	const char* film_filter = NULL;
	bool atlas_mode = false;
	bool scale = false;
	bool svga_mode = false;

	const char** extras = NULL;
	int extras_count = 0;
	int extras_cap = 0;

	/* Repeatable --palette: each occurrence overlays its PLTT on top
	 * of "standard" (and on top of prior --palette values), in argv
	 * order. Same accumulator semantics as the engine's PLTT swap. */
	const char** forced_palettes = NULL;
	int forced_palettes_count = 0;
	int forced_palettes_cap = 0;

	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--film") == 0 && i + 1 < argc) {
			film_filter = argv[++i];
		} else if (strcmp(argv[i], "--palette") == 0 && i + 1 < argc) {
			if (forced_palettes_count == forced_palettes_cap) {
				forced_palettes_cap = forced_palettes_cap ? forced_palettes_cap * 2 : 4;
				forced_palettes = (const char**)realloc(forced_palettes, sizeof(*forced_palettes) *
																			 (size_t)forced_palettes_cap);
				if (!forced_palettes)
					TieFilmUtil_Die("out of memory");
			}
			forced_palettes[forced_palettes_count++] = argv[++i];
		} else if (strcmp(argv[i], "--atlas") == 0) {
			atlas_mode = true;
		} else if (strcmp(argv[i], "--scale") == 0) {
			scale = true;
		} else if (strcmp(argv[i], "--svga") == 0) {
			svga_mode = true;
		} else if (strcmp(argv[i], "--ktx2") == 0) {
			/* Emit a `<name>.ktx2` sibling for every PNG we write,
			 * carrying the same RGBA8 pixels with a generated mip
			 * chain. The runtime Aeron loader prefers .ktx2 when
			 * present, otherwise falls back to .png — see
			 * cutscene_assets_gpu.c. */
			s_ktx2_enabled = true;
		} else if (strcmp(argv[i], "--bc7") == 0) {
			/* Emit a `<name>.ktx2` sibling encoded as BC7 by Aeron's
			 * shared image-baking backend. The
			 * runtime loader maps SDL_GPU_TEXTUREFORMAT_BC7_RGBA_UNORM
			 * directly. Implies --ktx2 — at runtime there's a single
			 * `.ktx2` sibling either way. */
			s_bc7_enabled = true;
		} else if (strcmp(argv[i], "--no-zstd") == 0) {
			/* Skip the zstd supercompression pass. KTX2 files are
			 * larger but readable by every generic image viewer
			 * that handles raw BC7. Useful for pipeline debugging. */
			s_zstd_enabled = false;
		} else if (strcmp(argv[i], "--bc7-quality") == 0 && i + 1 < argc) {
			const char* q = argv[++i];
			if (strcmp(q, "fast") == 0)
				s_bc7_quality = KTX2_BC7_QUALITY_FAST;
			else if (strcmp(q, "med") == 0)
				s_bc7_quality = KTX2_BC7_QUALITY_MED;
			else if (strcmp(q, "uber") == 0)
				s_bc7_quality = KTX2_BC7_QUALITY_UBER;
			else {
				fprintf(stderr,
						"filmextract: unknown --bc7-quality '%s' "
						"(expected fast | med | uber)\n",
						q);
				free(extras);
				free(forced_palettes);
				return 2;
			}
		} else if (strcmp(argv[i], "--extra") == 0 && i + 1 < argc) {
			if (extras_count == extras_cap) {
				extras_cap = extras_cap ? extras_cap * 2 : 4;
				extras = (const char**)realloc(extras, sizeof(*extras) * (size_t)extras_cap);
				if (!extras)
					TieFilmUtil_Die("out of memory");
			}
			extras[extras_count++] = argv[++i];
		} else if (argv[i][0] == '-') {
			TieFilmExtract_Usage();
		} else if (!lfd_path) {
			lfd_path = argv[i];
		} else if (!out_dir) {
			out_dir = argv[i];
		} else {
			TieFilmExtract_Usage();
		}
	}
	if (!lfd_path || !out_dir)
		TieFilmExtract_Usage();

	TieLfdFile primary;
	TieFilmUtil_OpenLfd(&primary, lfd_path);

	TieLfdFile* extra_lfds = NULL;
	if (extras_count > 0) {
		extra_lfds = (TieLfdFile*)calloc((size_t)extras_count, sizeof(TieLfdFile));
		if (!extra_lfds)
			TieFilmUtil_Die("out of memory");
		for (int i = 0; i < extras_count; i++)
			TieFilmUtil_OpenLfd(&extra_lfds[i], extras[i]);
	}

	const TieLfdFile** chain_arr =
		(const TieLfdFile**)calloc((size_t)(1 + extras_count), sizeof(const TieLfdFile*));
	if (!chain_arr)
		TieFilmUtil_Die("out of memory");
	chain_arr[0] = &primary;
	for (int i = 0; i < extras_count; i++)
		chain_arr[1 + i] = &extra_lfds[i];
	TieLfdFileChain chain = { .files = chain_arr, .count = 1 + extras_count };

	char base[64];
	TieFilmExtract_PathBasenameNoExt(lfd_path, base, sizeof(base));
	/* Uppercase to match the runtime resolver's LFD-chain convention
	 * (cutscene_snapshot writes the same form into snapshot tags). */
	for (char* p = base; *p; p++)
		*p = (char)toupper((unsigned char)*p);

	/* Parallel array of basenames for the --extra LFDs, recorded as
	 * the manifest's `extras: [...]` field so the runtime resolver
	 * walks the same chain that filmextract used. */
	const char** extras_basenames = NULL;
	char (*extras_base_buf)[64] = NULL;
	if (extras_count > 0) {
		extras_basenames = (const char**)calloc((size_t)extras_count, sizeof(*extras_basenames));
		extras_base_buf = (char (*)[64])calloc((size_t)extras_count, sizeof(*extras_base_buf));
		if (!extras_basenames || !extras_base_buf)
			TieFilmUtil_Die("out of memory");
		for (int i = 0; i < extras_count; i++) {
			TieFilmExtract_PathBasenameNoExt(extras[i], extras_base_buf[i], 64);
			for (char* p = extras_base_buf[i]; *p; p++)
				*p = (char)toupper((unsigned char)*p);
			extras_basenames[i] = extras_base_buf[i];
		}
	}

	/* Phase 1: walk every primary FILM, build per-asset palette
	 * TieFilmExtract_Usage table. Tells phase 2 which standard renders to skip
	 * (we'd produce wrong-coloured PNGs for any DELT only ever
	 * shown under a non-standard PLTT swap), and tells phase 3+4
	 * how to name the per-film palette renders. */
	TieFilmExtractVariantTable variants = { 0 };
	TieFilmExtract_AnalyzeFilms(&chain, &primary, film_filter, &variants);
	TieFilmExtract_DecideVariants(&chain, &primary, &variants);

	/* Phase 2: extract every DELT/RAW/ANIM in primary that the
	 * variant table doesn't cover (assets unused by primary's films,
	 * plus all ANIMs). Uses standard + every --palette overlay (or
	 * `standard` alone). */
	TieFilmExtract_ExtractLfdAssets(&chain, &primary, out_dir, base, forced_palettes, forced_palettes_count,
									atlas_mode, scale, svga_mode, &variants);

	/* Phase 3: emit the per-palette-context renders the variant
	 * table planned. These are the in-game-correct versions of
	 * assets used by primary's films. */
	TieFilmExtract_EmitVariants(&chain, out_dir, base, &variants, atlas_mode, scale, svga_mode);

	/* Phase 4: per-FILM manifest generation. References each actor
	 * by either bare canonical or `__<pal_label>` variant per the
	 * variant table; the runtime resolver finds them in primary
	 * LFD or chain accordingly. Skipped when --film picks none. */
	int film_count = 0, manifests = 0;
	for (uint32_t i = 0; i < primary.count; i++) {
		const TieLfdFileEntry* e = &primary.entries[i];
		if (e->type != FCC_FILM)
			continue;
		film_count++;
		if (film_filter && strncmp(film_filter, e->name, 8) != 0)
			continue;
		TieFilmExtract_GenerateFilmManifest(&chain, &primary, e, out_dir, base, extras_basenames,
											extras_count, atlas_mode, scale, svga_mode, &variants);
		manifests++;
	}

	if (film_count == 0 && manifests == 0)
		fprintf(stderr, "%s: no FILM resources found (asset-only LFD)\n", lfd_path);
	else if (film_filter && manifests == 0)
		fprintf(stderr, "%s: film '%s' not found\n", lfd_path, film_filter);

	free(extras_basenames);
	free(extras_base_buf);

	free(chain_arr);
	for (int i = 0; i < extras_count; i++)
		TieLfdFile_Close(&extra_lfds[i]);
	free(extra_lfds);
	free(extras);
	free(forced_palettes);
	TieLfdFile_Close(&primary);
	return 0;
}
