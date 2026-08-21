/* Extracts cockpit bitmaps and parts atlases from CP640 data. Output view names
 * must match the lowercase PanelViewDef names emitted in snapshots. */
#include "imgbake/ktx2_writer.h"
#include "imgbake/png_write.h"
#include "imgbake/upscale.h"
#include "panel_int.h"
#include "tie_formats/cockpit.h"
#include "tie_formats/panel.h"
/* CMD-CRT occlusion masks shared with the engine and runtime decoder. */
#include "tie_formats/cockpit_masks.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

/* ----- small utilities ------------------------------------------- */

static void TieCockpitExtract_Die(const char* fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	fputs("cockpit_extract: ", stderr);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fputc('\n', stderr);
	exit(1);
}

static int TieCockpitExtract_MakePath(const char* path) {
	char buf[1024];
	size_t n = strlen(path);
	if (n + 1 > sizeof buf)
		return -1;
	memcpy(buf, path, n + 1);
	for (size_t i = 1; i < n; ++i) {
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

static void TieCockpitExtract_LowerStr(char* s) {
	for (; *s; ++s)
		*s = (char)tolower((unsigned char)*s);
}

static int TieCockpitExtract_EqualIgnoreCase(const char* left, const char* right) {
	while (*left && *right) {
		if (tolower((unsigned char)*left) != tolower((unsigned char)*right))
			return 0;
		++left;
		++right;
	}
	return *left == *right;
}

static uint8_t* TieCockpitExtract_LoadFile(const char* path, long* out_size) {
	FILE* fp = fopen(path, "rb");
	if (!fp)
		return NULL;
	if (fseek(fp, 0, SEEK_END) != 0) {
		fclose(fp);
		return NULL;
	}
	long sz = ftell(fp);
	if (sz < 0 || fseek(fp, 0, SEEK_SET) != 0) {
		fclose(fp);
		return NULL;
	}
	uint8_t* buf = (uint8_t*)malloc(sz > 0 ? (size_t)sz : 1);
	if (!buf || fread(buf, 1, (size_t)sz, fp) != (size_t)sz) {
		free(buf);
		fclose(fp);
		return NULL;
	}
	fclose(fp);
	if (out_size)
		*out_size = sz;
	return buf;
}

typedef struct {
	uint8_t* bytes;
	TiePanel panel;
} TieCockpitExtractPanelFile;

static bool TieCockpitExtractPanelFile_Open(TieCockpitExtractPanelFile* file, const char* path, char* error,
											size_t error_capacity) {
	memset(file, 0, sizeof *file);
	long size = 0;
	file->bytes = TieCockpitExtract_LoadFile(path, &size);
	if (!file->bytes) {
		snprintf(error, error_capacity, "cannot read %s", path);
		return false;
	}
	TieFormatError format_error = { 0 };
	if (!TiePanel_Parse(file->bytes, (size_t)size, &file->panel, &format_error)) {
		snprintf(error, error_capacity, "%s: %s", path, format_error.message);
		free(file->bytes);
		memset(file, 0, sizeof *file);
		return false;
	}
	return true;
}

static void TieCockpitExtractPanelFile_Close(TieCockpitExtractPanelFile* file) {
	TiePanel_Free(&file->panel);
	free(file->bytes);
	memset(file, 0, sizeof *file);
}

static const TiePanelSection* TieCockpitExtractPanelFile_Find(const TieCockpitExtractPanelFile* file,
															  uint32_t type) {
	return TiePanel_Find(&file->panel, type);
}

typedef struct {
	uint8_t* bytes;
	TieShapeList list;
} TieCockpitExtractShapeFile;

static bool TieCockpitExtractShapeFile_Open(TieCockpitExtractShapeFile* file, const char* path,
											uint32_t declared_count, char* error, size_t error_capacity) {
	memset(file, 0, sizeof *file);
	long size = 0;
	file->bytes = TieCockpitExtract_LoadFile(path, &size);
	if (!file->bytes) {
		snprintf(error, error_capacity, "cannot read %s", path);
		return false;
	}
	TieFormatError format_error = { 0 };
	if (!TieShapeList_Parse(file->bytes, (size_t)size, declared_count, &file->list, &format_error)) {
		snprintf(error, error_capacity, "%s: %s", path, format_error.message);
		free(file->bytes);
		memset(file, 0, sizeof *file);
		return false;
	}
	return true;
}

static void TieCockpitExtractShapeFile_Close(TieCockpitExtractShapeFile* file) {
	TieShapeList_Free(&file->list);
	free(file->bytes);
	memset(file, 0, sizeof *file);
}

#define COCKPIT_FOURCC_PANL TIE_FOURCC('P', 'A', 'N', 'L')
#define COCKPIT_FOURCC_MASK TIE_FOURCC('M', 'A', 'S', 'K')
#define COCKPIT_FOURCC_PLTT TIE_FOURCC('P', 'L', 'T', 'T')

/* ----- CLI options ----------------------------------------------- */

typedef struct {
	const char* cp640_dir;
	const char* vga_pac_path;
	const char* out_dir;
	const char* only_craft; /* optional NULL = all */
	bool rgba_only;         /* skip BC7 */
	bool no_upscale;        /* skip 4K scaling */
	bool skip_png;          /* default: emit parallel PNG for hand-editing */
	bool validate_mask;     /* cross-check shape alpha against MASK data */
	/* --16x9: pillarbox the 4:3 cockpit into a 16:9 reference frame and
	 * emit the new HD-cockpit YAML schema (reference 1920×1080, aspect,
	 * per-instrument anchors compensating for the pillar offset). Smoke
	 * test for the runtime's HD cockpit path without authoring new art.
	 * Outputs go to <out>/remaster/flight-16x9/cockpits/ so the 4:3 set isn't
	 * touched. */
	bool pillarbox_16x9;
} TieCockpitExtractOptions;

/* 16:9 reference frame the pillarbox mode targets. Picked to match the
 * post-upscale texture pixel dims (atlas_svga_to_4k turns 640×480 into
 * 2880×2160, pillarbox to 3840×2160) so reading the YAML, the
 * reference and atlas_size numbers line up — `atlas: { x, y, w, h }`
 * rects are in the same units as `reference: { w, h }`. Note: this is
 * convention only; hand-authored cockpits can pick any 16:9 reference
 * (ins_anchor + the renderer's NDC math do the right thing). */
#define HD_REF_W 3840
#define HD_REF_H 2160
#define HD_COCKPIT_W 2880    /* 4:3 portion: 2160 × 4/3 */
#define HD_PILLAR_OFFSET 480 /* (3840 - 2880) / 2 */
#define HD_PILLAR_SCALE_X9 9 /* numerator   of 2160/480 = 9/2 */
#define HD_PILLAR_SCALE_X2 2 /* denominator                    */

static inline int TieCockpitExtract_HdXFromClassic(int classic_x) {
	return HD_PILLAR_OFFSET + (classic_x * HD_PILLAR_SCALE_X9) / HD_PILLAR_SCALE_X2;
}

static inline int TieCockpitExtract_HdYFromClassic(int classic_y) {
	return (classic_y * HD_PILLAR_SCALE_X9) / HD_PILLAR_SCALE_X2;
}

static inline int TieCockpitExtract_HdScaleClassic(int v) {
	return (v * HD_PILLAR_SCALE_X9) / HD_PILLAR_SCALE_X2;
}

/* Track view_names already emitted so shared views (TIEFTRCM/TIEFTRTD
 * referenced by every craft) don't get re-rasterized + re-BC7-encoded
 * once per consuming craft. Capacity is plenty for the ~40 unique
 * view-LFDs across all CP640 crafts. */
typedef struct {
	char names[128][16];
	int count;
} TieCockpitExtractSeenSet;

static bool TieCockpitExtract_SeenContains(const TieCockpitExtractSeenSet* s, const char* name) {
	for (int i = 0; i < s->count; ++i)
		if (!strcmp(s->names[i], name))
			return true;
	return false;
}

static void TieCockpitExtract_SeenAdd(TieCockpitExtractSeenSet* s, const char* name) {
	if (s->count >= (int)(sizeof s->names / sizeof s->names[0]))
		return;
	snprintf(s->names[s->count], sizeof s->names[0], "%s", name);
	++s->count;
}

/* Sparse id → symbolic name table for cockpit_layout YAML emit.
 * Mirrors hud_id_resolve in src/tie_remaster/flight/cockpit/layout.c —
 * keep in sync when TieHudInstrumentId gains new entries. */
static const char* TieCockpitExtract_HudIdSymbolicName(int id) {
	switch (id) {
		case 0:
			return "RADAR_LEFT";
		case 1:
			return "RADAR_RIGHT";
		case 2:
			return "CMD_3D_CRT";
		case 3:
			return "LASER_LED_FIRST";
		case 10:
			return "LASER_LED_LAST";
		case 11:
			return "MISSILE_HP_FIRST";
		case 14:
			return "MISSILE_HP_LAST";
		case 15:
			return "MISSILE_AMMO_FIRST";
		case 18:
			return "MISSILE_AMMO_LAST";
		case 19:
			return "SHIELD_FWD_NORMAL";
		case 20:
			return "SHIELD_FWD_OVER";
		case 21:
			return "SHIELD_REAR_NORMAL";
		case 22:
			return "SHIELD_REAR_OVER";
		case 23:
			return "HULL_DAMAGE_LEVER";
		case 24:
			return "SPEED_DIGITS";
		case 25:
			return "THROTTLE_DIGITS";
		case 26:
			return "POWER_BALANCE";
		case 27:
			return "POWER_LASERS";
		case 28:
			return "POWER_SHIELDS";
		case 29:
			return "POWER_BEAM";
		case 30:
			return "CLOCK_DIGITS";
		case 31:
			return "REC_LED";
		case 32:
			return "REC_PCT";
		case 33:
			return "VIEW17_TITLE";
		case 35:
			return "BEAM_ARC";
		case 36:
			return "GUNSIGHT";
		case 37:
			return "WEAPON_FIRE_FIRST";
		case 44:
			return "WEAPON_FIRE_LAST";
		case 45:
			return "DAMAGE_CRACK_FIRST";
		case 57:
			return "DAMAGE_CRACK_LAST";
		case 58:
			return "TARGET_SUBSYSTEM_PCT";
		case 59:
			return "TARGET_DIST_KM_INT";
		case 60:
			return "TARGET_DIST_KM_FRAC";
		case 61:
			return "TARGET_SHIELD_PCT";
		case 62:
			return "TARGET_HULL_PCT";
		case 63:
			return "TARGET_CARGO";
		case 65:
			return "TARGET_SUBSYSTEM_FOCUS";
		case 66:
			return "WARN_INCOMING";
		case 67:
			return "WARN_LOCK";
		case 68:
			return "WARN_IMPACT";
		case 71:
			return "THREAT_DIST_KM_INT";
		case 72:
			return "THREAT_DIST_KM_FRAC";
		case 73:
			return "THREAT_ION";
		case 74:
			return "THREAT_TORP";
		case 75:
			return "THREAT_MISSILE";
		case 76:
			return "THREAT_BEAM";
		case 77:
			return "THREAT_SHIELD_PCT";
		case 78:
			return "THREAT_HULL_PCT";
		case 83:
			return "COVER_SHIELDS";
		case 84:
			return "COVER_BEAM_UP";
		case 85:
			return "COVER_BEAM_DOWN";
		case 91:
			return "BEAM_FIRE";
		default:
			return NULL;
	}
}

/* Print "id: NAME" when the index has a known symbolic name; else
 * "id: N". Used as the `%s` placeholder in the instrument emit lines
 * below. Returns a static buffer (caller must not retain). */
static const char* TieCockpitExtract_HudIdField(int id) {
	static char buf[24];
	const char* name = TieCockpitExtract_HudIdSymbolicName(id);
	if (name) {
		snprintf(buf, sizeof buf, "id: %s", name);
	} else {
		snprintf(buf, sizeof buf, "id: %d", id);
	}
	return buf;
}

/* Collector for the cockpit rows emitted into the remaster catalog snippet.
 * Paths are relative to ASSET/remaster. */
typedef struct {
	char view[16];
	char bitmap_hd[256];
	char bitmap_4_3[256];
	char damage[256];
} TieCockpitExtractView;

typedef struct {
	char parts[16];
	char atlas[256];
	char layout[256];
} TieCockpitExtractParts;

typedef struct {
	int variant;
	int classic_w;
	char path[256];
} TieCockpitExtractMask;

typedef struct {
	TieCockpitExtractView views[128];
	int view_count;
	TieCockpitExtractParts parts[32];
	int parts_count;
	TieCockpitExtractMask masks[32];
	int mask_count;
} TieCockpitExtractLog;

/* Find an existing view entry by name (case-sensitive — the rest of
 * the tool keeps names lowercased) so the same view extracted via
 * both per-craft + standalone passes merges into one entry. */
static TieCockpitExtractView* TieCockpitExtract_EmitLogViewLookup(TieCockpitExtractLog* log,
																  const char* view) {
	for (int i = 0; i < log->view_count; ++i)
		if (!strcmp(log->views[i].view, view))
			return &log->views[i];
	return NULL;
}

static TieCockpitExtractView* TieCockpitExtract_EmitLogViewGetOrAdd(TieCockpitExtractLog* log,
																	const char* view) {
	TieCockpitExtractView* v = TieCockpitExtract_EmitLogViewLookup(log, view);
	if (v)
		return v;
	if (log->view_count >= (int)(sizeof log->views / sizeof log->views[0]))
		return NULL;
	v = &log->views[log->view_count++];
	memset(v, 0, sizeof *v);
	snprintf(v->view, sizeof v->view, "%s", view);
	return v;
}

static void TieCockpitExtract_EmitLogAddParts(TieCockpitExtractLog* log, const char* parts,
											  const char* atlas_rel, const char* layout_rel) {
	if (log->parts_count >= (int)(sizeof log->parts / sizeof log->parts[0]))
		return;
	TieCockpitExtractParts* p = &log->parts[log->parts_count++];
	snprintf(p->parts, sizeof p->parts, "%s", parts);
	snprintf(p->atlas, sizeof p->atlas, "%s", atlas_rel);
	snprintf(p->layout, sizeof p->layout, "%s", layout_rel);
}

static void TieCockpitExtract_EmitLogAddMask(TieCockpitExtractLog* log, int variant, int classic_w,
											 const char* rel) {
	if (log->mask_count >= (int)(sizeof log->masks / sizeof log->masks[0]))
		return;
	TieCockpitExtractMask* m = &log->masks[log->mask_count++];
	m->variant = variant;
	m->classic_w = classic_w;
	snprintf(m->path, sizeof m->path, "%s", rel);
}

static void TieCockpitExtract_Usage(const char* prog) {
	fprintf(stderr,
			"Usage: %s <cp640-dir> <vga.pac> <output-dir>\n"
			"       [--craft NAME] [--rgba] [--no-upscale] [--no-png]\n"
			"       [--validate-mask] [--16x9]\n"
			"\n"
			"Default output: <output-dir>/remaster/flight/cockpits/<view>_640.{ktx2,png}\n"
			"                                            /<craft>_parts.{ktx2,png}\n"
			"                                            /<craft>_hud_layout.yaml\n"
			"PNGs ship alongside KTX2 so the art team can hand-edit them.\n"
			"Use --no-png to suppress PNGs (smaller output, KTX2-only).\n"
			"\n"
			"--16x9: pillarbox the 4:3 cockpit into a 1920×1080 reference\n"
			"        frame and emit the HD-cockpit YAML schema. Outputs land\n"
			"        in <output-dir>/remaster/flight-16x9/cockpits/. Lets you smoke-\n"
			"        test the runtime's 16:9 cockpit path with stock assets.\n",
			prog);
	exit(2);
}

static void TieCockpitExtract_ParseOpts(int argc, char** argv, TieCockpitExtractOptions* o) {
	memset(o, 0, sizeof *o);
	if (argc < 4)
		TieCockpitExtract_Usage(argv[0]);
	o->cp640_dir = argv[1];
	o->vga_pac_path = argv[2];
	o->out_dir = argv[3];
	for (int i = 4; i < argc; ++i) {
		if (!strcmp(argv[i], "--craft") && i + 1 < argc)
			o->only_craft = argv[++i];
		else if (!strcmp(argv[i], "--rgba"))
			o->rgba_only = true;
		else if (!strcmp(argv[i], "--no-upscale"))
			o->no_upscale = true;
		else if (!strcmp(argv[i], "--no-png"))
			o->skip_png = true;
		else if (!strcmp(argv[i], "--validate-mask"))
			o->validate_mask = true;
		else if (!strcmp(argv[i], "--16x9"))
			o->pillarbox_16x9 = true;
		else
			TieCockpitExtract_Usage(argv[0]);
	}
}

/* ----- per-view extraction --------------------------------------- */

/* Render the cockpit base for one view and emit ktx2 + optional PNG.
 * Returns false (and logs) on any failure for this view; callers
 * continue with the next view. */
static bool TieCockpitExtract_EmitCockpitView(const TieCockpitExtractOptions* o,
											  const char* cockpit_dir,            /* CP640 dir */
											  const char* view_name,              /* "TIEFTR12" */
											  const TieCockpitPanelIntView* view, /* viewport rect */
											  const uint8_t* vga_pac,
											  const char* parts_dir,     /* output cockpits/ */
											  TieCockpitExtractLog* log, /* may be NULL */
											  char* err, size_t errsz) {
	char lfd_path[1024];
	snprintf(lfd_path, sizeof lfd_path, "%s/%s.LFD", cockpit_dir, view_name);

	TieCockpitExtractPanelFile lfd;
	if (!TieCockpitExtractPanelFile_Open(&lfd, lfd_path, err, errsz))
		return false;

	const TiePanelSection* panl = TieCockpitExtractPanelFile_Find(&lfd, COCKPIT_FOURCC_PANL);
	const TiePanelSection* pltt = TieCockpitExtractPanelFile_Find(&lfd, COCKPIT_FOURCC_PLTT);
	const TiePanelSection* mask = TieCockpitExtractPanelFile_Find(&lfd, COCKPIT_FOURCC_MASK);
	if (!panl || !pltt || !mask) {
		snprintf(err, errsz, "%s: missing PANL, MASK, or PLTT", lfd_path);
		TieCockpitExtractPanelFile_Close(&lfd);
		return false;
	}

	uint8_t palette[768];
	int measured_w = 0, measured_h = 0;
	TieFormatError codec_error = { 0 };
	if (!TieShape_Measure(panl->data, panl->size, &measured_w, &measured_h, NULL, &codec_error)) {
		snprintf(err, errsz, "%s", codec_error.message);
		TieCockpitExtractPanelFile_Close(&lfd);
		return false;
	}
	const int vx = view ? (int)view->pos_x : 0;
	const int vy = view ? (int)view->pos_y : 0;
	const int vw = view ? (int)view->width : measured_w;
	const int vh = view ? (int)view->depth : measured_h;
	TieRgbaFrame decoded_base = { 0 };
	int rows = 0;
	if (!TieCockpitBase_Build(panl->data, panl->size, mask->data, mask->size, pltt->data, pltt->size, vga_pac,
							  576, vx, vy, vw, vh, &decoded_base, palette, &rows, &codec_error)) {
		snprintf(err, errsz, "%s: %s", lfd_path, codec_error.message);
		TieCockpitExtractPanelFile_Close(&lfd);
		return false;
	}
	uint8_t* rgba = decoded_base.rgba;
	int w = decoded_base.width, h = decoded_base.height;
	if (rows < vh && rows > 0)
		fprintf(stderr, "  warn: %s mask covers %d/%d rows (rest opaque)\n", view_name, rows, vh);

	/* Upscale unless --no-upscale. */
	int out_w = w, out_h = h;
	if (!o->no_upscale) {
		if (!atlas_svga_to_4k(&rgba, &out_w, &out_h)) {
			snprintf(err, errsz, "upscale failed (%dx%d)", w, h);
			free(rgba);
			TieCockpitExtractPanelFile_Close(&lfd);
			return false;
		}
	}

	/* --16x9: pillarbox the 4:3 bitmap into a 16:9-aspect texture.
	 * Gutters are alpha=0 so the 3D scene shows through; the runtime
	 * blits the whole texture across the layout's 1920×1080 reference
	 * frame and the cockpit lands centred at (240..1680). */
	if (o->pillarbox_16x9) {
		/* Pad to HD_REF_W wide so the texture's X axis maps 1:1 to
		 * the runtime's destination rect (coord_w × cockpit_area_h).
		 * Padding to 16:9 aspect (out_h × 16/9) would mismatch the
		 * destination's 1.871 aspect (cockpit_area_h is 19/20 of
		 * coord_h, not 9/16 of coord_w) and stretch the texture ~5%
		 * wider at blit time. */
		const int pb_w = HD_REF_W;
		const int pad_w = (pb_w - out_w) / 2;
		const int pb_h = out_h;
		if (pad_w < 0) {
			snprintf(err, errsz, "pillarbox: source %dx%d wider than 16:9", out_w, out_h);
			free(rgba);
			TieCockpitExtractPanelFile_Close(&lfd);
			return false;
		}
		uint8_t* pb = (uint8_t*)calloc((size_t)pb_w * (size_t)pb_h * 4u, 1);
		if (!pb) {
			snprintf(err, errsz, "OOM pillarbox (%dx%d)", pb_w, pb_h);
			free(rgba);
			TieCockpitExtractPanelFile_Close(&lfd);
			return false;
		}
		for (int y = 0; y < out_h; ++y) {
			memcpy(pb + ((size_t)y * (size_t)pb_w + (size_t)pad_w) * 4u,
				   rgba + (size_t)y * (size_t)out_w * 4u, (size_t)out_w * 4u);
		}
		free(rgba);
		rgba = pb;
		out_w = pb_w;
		out_h = pb_h;
	}

	/* Output filename: lowercased view name (matches what
	 * TieCockpitState.view_name carries from the engine). 4:3 mode
	 * uses the classic _640 suffix; --16x9 uses _<REF_W>x<REF_H>
	 * so the runtime loader picks it via the layout's reference
	 * frame. */
	char base[16];
	snprintf(base, sizeof base, "%s", view_name);
	TieCockpitExtract_LowerStr(base);

	char out_path[1024];
	if (o->pillarbox_16x9)
		snprintf(out_path, sizeof out_path, "%s/%s_%dx%d.ktx2", parts_dir, base, HD_REF_W, HD_REF_H);
	else
		snprintf(out_path, sizeof out_path, "%s/%s_640.ktx2", parts_dir, base);

	bool ok = false;
	/* Cockpit panel = palette-colour artwork (sRGB-authored). */
	if (o->rgba_only) {
		ok = write_ktx2_rgba_with_generated_mips(out_path, out_w, out_h, rgba, KTX2_TF_SRGB, /*zstd=*/true);
	} else {
		ok = write_ktx2_bc7_with_generated_mips(out_path, out_w, out_h, rgba, KTX2_BC7_QUALITY_MED,
												KTX2_TF_SRGB, /*zstd=*/true);
	}
	if (!ok)
		snprintf(err, errsz, "KTX2 write failed: %s", out_path);

	if (ok && !o->skip_png) {
		char png_path[1024];
		if (o->pillarbox_16x9)
			snprintf(png_path, sizeof png_path, "%s/%s_%dx%d.png", parts_dir, base, HD_REF_W, HD_REF_H);
		else
			snprintf(png_path, sizeof png_path, "%s/%s_640.png", parts_dir, base);
		write_png_rgba(png_path, out_w, out_h, rgba);
	}

	/* Record paths relative to ASSET/remaster. */
	if (ok && log) {
		TieCockpitExtractView* ve = TieCockpitExtract_EmitLogViewGetOrAdd(log, base);
		if (ve) {
			char rel[256];
			if (o->pillarbox_16x9) {
				snprintf(rel, sizeof rel, "flight-16x9/cockpits/%s_%dx%d.ktx2", base, HD_REF_W, HD_REF_H);
				snprintf(ve->bitmap_hd, sizeof ve->bitmap_hd, "%s", rel);
			} else {
				snprintf(rel, sizeof rel, "flight/cockpits/%s_640.ktx2", base);
				snprintf(ve->bitmap_4_3, sizeof ve->bitmap_4_3, "%s", rel);
			}
		}
	}

	free(rgba);
	TieCockpitExtractPanelFile_Close(&lfd);
	return ok;
}

/* Render every shape in <X>P.PNL into an RGBA atlas, pack via the
 * skyline algorithm and emit the atlas plus its authored GLB-mode layout. */
static bool TieCockpitExtract_EmitPartsAtlas(const TieCockpitExtractOptions* o, const char* cockpit_dir,
											 const TieCockpitPanelInt* pi,
											 const uint8_t* vga_pac_unused_for_now, const char* parts_dir,
											 TieCockpitExtractLog* log, char* err, size_t errsz) {
	(void)vga_pac_unused_for_now;
	char pnl_path[1024];
	snprintf(pnl_path, sizeof pnl_path, "%s/%s.PNL", cockpit_dir, pi->parts_basename);

	TieCockpitExtractShapeFile pnl;
	if (!TieCockpitExtractShapeFile_Open(&pnl, pnl_path, pi->parts_shape_count, err, errsz))
		return false;

	/* Resolve per-shape dimensions to drive the skyline pack. Shapes
	 * use any cockpit's palette for color values, but for atlas
	 * geometry only the bbox matters — we measure without rasterizing,
	 * pack, then rasterize directly into the final atlas. */
	typedef struct {
		int w, h, ax, ay;
	} CelInfo;
	CelInfo* cels = (CelInfo*)calloc(pnl.list.count, sizeof *cels);
	if (!cels) {
		TieCockpitExtractShapeFile_Close(&pnl);
		snprintf(err, errsz, "OOM");
		return false;
	}

	int max_w = 0;
	for (uint32_t i = 0; i < pnl.list.count; ++i) {
		int w = 0, h = 0;
		TieFormatError measure_error = { 0 };
		if (!TieShape_Measure(pnl.list.shapes[i].data, pnl.list.shapes[i].size, &w, &h, NULL,
							  &measure_error)) {
			fprintf(stderr, "  warn: parts shape %u measure failed (%s)\n", i, measure_error.message);
			cels[i].w = 0;
			cels[i].h = 0;
			continue;
		}
		cels[i].w = w;
		cels[i].h = h;
		if (w > max_w)
			max_w = w;
	}

	/* Heterogeneous cockpit parts use bottom-left skyline packing. */
	int atlas_w = max_w * 4; /* heuristic: 4× the widest cel */
	if (atlas_w < 256)
		atlas_w = 256;
	int* height = (int*)calloc((size_t)atlas_w, sizeof(int));
	if (!height) {
		free(cels);
		TieCockpitExtractShapeFile_Close(&pnl);
		snprintf(err, errsz, "OOM");
		return false;
	}
	int atlas_h = 4;
	const int pad = 4;
	for (uint32_t i = 0; i < pnl.list.count; ++i) {
		int fw = cels[i].w + pad;
		int fh = cels[i].h + pad;
		if (cels[i].w == 0 || cels[i].h == 0) {
			cels[i].ax = -1;
			cels[i].ay = -1;
			continue;
		}
		int last_x = atlas_w - fw;
		if (last_x < 0) {
			cels[i].ax = -1;
			cels[i].ay = -1;
			continue;
		}
		int best_x = 0, best_y = INT32_MAX;
		for (int x = 0; x <= last_x; ++x) {
			int y = 0;
			for (int j = 0; j < fw; ++j)
				if (height[x + j] > y)
					y = height[x + j];
			if (y < best_y) {
				best_y = y;
				best_x = x;
			}
		}
		cels[i].ax = best_x + pad / 2;
		cels[i].ay = best_y + pad / 2;
		int top = best_y + fh;
		for (int j = 0; j < fw; ++j)
			height[best_x + j] = top;
		if (top + pad > atlas_h)
			atlas_h = top + pad;
	}
	free(height);

	/* Rasterize each cel into the atlas. The first PLTT-bearing
	 * cockpit view of this craft supplies the cel palette (parts
	 * are always drawn with the cockpit's PLTT slot 0..63 active +
	 * VGA.PAC for slots 64..255). Resolve that by reading the
	 * primary view (idx 0) and reading its PLTT. */
	uint8_t palette[768];
	{
		const char* vname = NULL;
		for (int i = 0; i < PANEL_INT_NUM_VIEWS && !vname; ++i)
			if (pi->views[i].flags == 1)
				vname = pi->views[i].name;
		if (!vname) {
			free(cels);
			TieCockpitExtractShapeFile_Close(&pnl);
			snprintf(err, errsz, "no own-LFD view for parts palette");
			return false;
		}
		char path[1024];
		snprintf(path, sizeof path, "%s/%s.LFD", cockpit_dir, vname);
		TieCockpitExtractPanelFile lfd0;
		if (!TieCockpitExtractPanelFile_Open(&lfd0, path, err, errsz)) {
			free(cels);
			TieCockpitExtractShapeFile_Close(&pnl);
			return false;
		}
		const TiePanelSection* p0 = TieCockpitExtractPanelFile_Find(&lfd0, COCKPIT_FOURCC_PLTT);
		long vga_sz = 0;
		uint8_t* vga_pac = TieCockpitExtract_LoadFile(o->vga_pac_path, &vga_sz);
		TieFormatError palette_error = { 0 };
		if (!vga_pac || vga_sz < 576 || !p0 || p0->size < 194 ||
			!TieCockpitPalette_Build(vga_pac, (size_t)vga_sz, p0->data, p0->size, palette, &palette_error)) {
			free(vga_pac);
			TieCockpitExtractPanelFile_Close(&lfd0);
			free(cels);
			TieCockpitExtractShapeFile_Close(&pnl);
			snprintf(err, errsz, "parts palette inputs are incomplete");
			return false;
		}
		free(vga_pac);
		TieCockpitExtractPanelFile_Close(&lfd0);
	}

	uint8_t* atlas = (uint8_t*)calloc((size_t)atlas_w * (size_t)atlas_h * 4u, 1);
	if (!atlas) {
		free(cels);
		TieCockpitExtractShapeFile_Close(&pnl);
		snprintf(err, errsz, "OOM atlas %dx%d", atlas_w, atlas_h);
		return false;
	}

	uint16_t* cel_skip = (uint16_t*)calloc(pnl.list.count, sizeof *cel_skip);
	TieShape* codec_shapes = calloc(pnl.list.count, sizeof *codec_shapes);
	if (!cel_skip || !codec_shapes) {
		free(codec_shapes);
		free(cel_skip);
		free(atlas);
		free(cels);
		TieCockpitExtractShapeFile_Close(&pnl);
		snprintf(err, errsz, "OOM");
		return false;
	}
	TieCockpitPartInstrument instruments[PANEL_INT_NUM_INSTRUMENTS];
	for (uint32_t i = 0; i < PANEL_INT_NUM_INSTRUMENTS; ++i) {
		instruments[i] = (TieCockpitPartInstrument) {
			.x = pi->instruments[i].x,
			.y = pi->instruments[i].y,
			.param1 = pi->instruments[i].param1,
			.param2 = pi->instruments[i].param2,
		};
	}
	for (uint32_t i = 0; i < pnl.list.count; ++i) {
		codec_shapes[i].data = pnl.list.shapes[i].data;
		codec_shapes[i].size = pnl.list.shapes[i].size;
	}
	TieRgbaFrames decoded = { 0 };
	TieFormatError codec_error = { 0 };
	const TieShapeList shape_list = {
		.shapes = codec_shapes,
		.count = pnl.list.count,
	};
	if (!TieCockpitPartTransparency_Build(instruments, PANEL_INT_NUM_INSTRUMENTS, pnl.list.count, cel_skip,
										  &codec_error) ||
		!TieCockpitShapeFrames_Build(&shape_list, palette, cel_skip, 253, &decoded, &codec_error)) {
		free(codec_shapes);
		free(cel_skip);
		free(atlas);
		free(cels);
		TieCockpitExtractShapeFile_Close(&pnl);
		snprintf(err, errsz, "%s", codec_error.message);
		return false;
	}

	for (uint32_t i = 0; i < pnl.list.count; ++i) {
		if (cels[i].ax < 0 || cels[i].w == 0)
			continue;
		const TieRgbaFrame* frame = &decoded.frames[i];
		for (int y = 0; y < frame->height; ++y) {
			memcpy(atlas + (((size_t)(cels[i].ay + y)) * (size_t)atlas_w + (size_t)cels[i].ax) * 4u,
				   frame->rgba + (size_t)y * (size_t)frame->width * 4u, (size_t)frame->width * 4u);
		}
	}

	TieRgbaFrames_Free(&decoded);
	free(codec_shapes);
	free(cel_skip);

	int out_w = atlas_w, out_h = atlas_h;
	if (!o->no_upscale) {
		if (!atlas_svga_to_4k(&atlas, &out_w, &out_h)) {
			free(cels);
			free(atlas);
			TieCockpitExtractShapeFile_Close(&pnl);
			snprintf(err, errsz, "atlas upscale failed");
			return false;
		}
	}

	char base[16];
	snprintf(base, sizeof base, "%s", pi->parts_basename);
	TieCockpitExtract_LowerStr(base);
	/* strip trailing 'p' so TIEFTRP → tieftr (matches view_name family). */
	size_t bl = strlen(base);
	if (bl > 0 && base[bl - 1] == 'p')
		base[bl - 1] = '\0';

	char out_path[1024];
	snprintf(out_path, sizeof out_path, "%s/%s_parts.ktx2", parts_dir, base);

	bool ok = false;
	/* HUD parts atlas = palette-colour artwork (sRGB-authored). */
	if (o->rgba_only) {
		ok = write_ktx2_rgba_with_generated_mips(out_path, out_w, out_h, atlas, KTX2_TF_SRGB, /*zstd=*/true);
	} else {
		ok = write_ktx2_bc7_with_generated_mips(out_path, out_w, out_h, atlas, KTX2_BC7_QUALITY_MED,
												KTX2_TF_SRGB, /*zstd=*/true);
	}
	if (!ok)
		snprintf(err, errsz, "parts ktx2 write failed: %s", out_path);

	if (ok && !o->skip_png) {
		char png_path[1024];
		snprintf(png_path, sizeof png_path, "%s/%s_parts.png", parts_dir, base);
		write_png_rgba(png_path, out_w, out_h, atlas);
	}

	if (ok && log) {
		char atlas_rel[256];
		char layout_rel[256];
		const char* flight_prefix = o->pillarbox_16x9 ? "flight-16x9" : "flight";
		snprintf(atlas_rel, sizeof atlas_rel, "%s/cockpits/%s_parts.ktx2", flight_prefix, base);
		snprintf(layout_rel, sizeof layout_rel, "%s/cockpits/%s_hud_layout.yaml", flight_prefix, base);
		TieCockpitExtract_EmitLogAddParts(log, base, atlas_rel, layout_rel);
	}

	/* Emit hud_layout.yaml — generic layout descriptor: per-cel
	 * on-screen size (in the reference coord frame the renderer treats
	 * HUD instrument positions in) + atlas pixel rect (where to sample
	 * in the shipped KTX2). The format makes no assumption about VGA
	 * vs 4K vs 16:9 — a hand-authored 1080p cockpit would just declare
	 * `reference: { w: 1920, h: 1080 }` with its own atlas + cel rects.
	 *
	 * Atlas rect derivation: cels[i].ax/ay are pre-upscale skyline
	 * coordinates. When upscaling SVGA → 4K we apply scale_svga_xy_to_4k
	 * to both top-left and bottom-right corners separately (the helper
	 * is non-additive — see upscale.h note); width/height come from
	 * differenced corner pairs to stay pixel-exact. */
	if (ok) {
		char yaml_path[1024];
		snprintf(yaml_path, sizeof yaml_path, "%s/%s_hud_layout.yaml", parts_dir, base);
		FILE* yf = fopen(yaml_path, "w");
		if (yf) {
			/* Default 4:3 reference (SVGA = 640×480). --16x9 pillarboxes
			 * onto 1920×1080. */
			const int ref_w = o->pillarbox_16x9 ? HD_REF_W : 640;
			const int ref_h = o->pillarbox_16x9 ? HD_REF_H : 480;
			fprintf(yf,
					"# Auto-generated by cockpit_extract from %s.PNL.\n"
					"# %u cels, indexed by farbufferptrs[] slot.\n"
					"#\n"
					"# reference: coord frame for `size` rects. The renderer\n"
					"#            also reads HUD instrument positions in this\n"
					"#            frame, then scales the composite to the\n"
					"#            flight RT dim.\n"
					"# atlas:     pixel rect inside %s_parts.ktx2.\n"
					"#            atlas_size is the shipped-texture dim;\n"
					"#            the renderer sanity-checks against it.\n"
					"reference: { w: %d, h: %d }\n",
					pi->parts_basename, pnl.list.count, base, ref_w, ref_h);
			fprintf(yf, "atlas_size: { w: %d, h: %d }\n", out_w, out_h);

			/* 16:9 mode: per-instrument anchors compensate for the
			 * pillar offset + 2.25× scale. Engine ships instrument
			 * x/y in 4:3 coords; the runtime's piecewise fallback would
			 * stretch X 3× without the pillar offset, landing widgets
			 * over the transparent gutters. PIP rect derives the same
			 * way from instruments[CMD_3D_CRT]. */
			if (o->pillarbox_16x9) {
				const TieCockpitPanelIntInstruction* crt = &pi->instruments[2];
				if (crt->param1 > 0 && crt->param2 > 0) {
					fprintf(yf, "pip_rect: { x: %d, y: %d, w: %d, h: %d }\n",
							TieCockpitExtract_HdXFromClassic((int)crt->x),
							TieCockpitExtract_HdYFromClassic((int)crt->y),
							TieCockpitExtract_HdScaleClassic((int)crt->param1),
							TieCockpitExtract_HdScaleClassic((int)crt->param2));
				}
				fprintf(yf, "instruments:\n");
				/* Engine constants the snapshot ABI doc nails down:
				 *   radar disc radius   : 44 SVGA classic-px
				 *   slider rung step    : 6 SVGA classic-px (Y)
				 *   LED row step        : 6 SVGA classic-px (X)
				 *   beam-arc step       : 3 SVGA classic-px (both axes)
				 * Scaled into the 16:9 reference via HD_PILLAR_SCALE. */
				const int RADAR_R_CLASSIC = 44;
				const int SLIDER_STEP_CLASSIC = 6;
				const int LED_STEP_CLASSIC = 6;
				const int ARC_STEP_CLASSIC = 3;
				/* CMD/threat text column constants (panel.c). For
				 * right-aligned and centered text instruments,
				 * the artist sees the column edge / center
				 * directly in ref-frame px. */
				const int CMD_TEXT_W_CLASSIC = 70;
				const int CMD_NAME_W_CLASSIC = 160;
				const int THREAT_LABEL_X_CLASSIC = 132;
				const int THREAT_NAME_W_CLASSIC = 114;
				for (int i = 0; i < PANEL_INT_NUM_INSTRUMENTS; ++i) {
					const TieCockpitPanelIntInstruction* ins = &pi->instruments[i];
					if ((ins->x | ins->y) == 0)
						continue;
					const int ax = TieCockpitExtract_HdXFromClassic((int)ins->x);
					const int ay = TieCockpitExtract_HdYFromClassic((int)ins->y);
					const char* id_field = TieCockpitExtract_HudIdField(i);
					if (i == 0 /*RADAR_LEFT*/ || i == 1 /*RADAR_RIGHT*/) {
						fprintf(yf, "  - { %s, x: %d, y: %d, radius: %d }\n", id_field, ax, ay,
								TieCockpitExtract_HdScaleClassic(RADAR_R_CLASSIC));
					} else if (i >= 3 && i <= 10 /*LASER_LED row*/) {
						fprintf(yf, "  - { %s, x: %d, y: %d, stride_x: %d }\n", id_field, ax, ay,
								TieCockpitExtract_HdScaleClassic(LED_STEP_CLASSIC));
					} else if (i >= 26 && i <= 29 /*POWER sliders*/) {
						fprintf(yf, "  - { %s, x: %d, y: %d, stride_y: %d }\n", id_field, ax, ay,
								TieCockpitExtract_HdScaleClassic(SLIDER_STEP_CLASSIC));
					} else if (i == 35 /*BEAM_ARC*/) {
						const int s = TieCockpitExtract_HdScaleClassic(ARC_STEP_CLASSIC);
						fprintf(yf,
								"  - { %s, x: %d, y: %d, "
								"stride_x: %d, stride_y: %d }\n",
								id_field, ax, ay, s, s);
					} else if (i == 63 /*TARGET_CARGO*/ || i == 65 /*TARGET_SUBSYSTEM_FOCUS*/) {
						const int right_at =
							TieCockpitExtract_HdXFromClassic((int)ins->x + CMD_TEXT_W_CLASSIC);
						fprintf(yf, "  - { %s, x: %d, y: %d, right_at: %d }\n", id_field, ax, ay, right_at);
					} else if (i == 90 /*CMD TARGET name*/) {
						const int center_at =
							TieCockpitExtract_HdXFromClassic((int)ins->x + CMD_NAME_W_CLASSIC / 2);
						fprintf(yf, "  - { %s, x: %d, y: %d, center_at: %d }\n", id_field, ax, ay, center_at);
					} else if (i == 69 /*THREAT TARGET name*/) {
						const int center_at =
							TieCockpitExtract_HdXFromClassic((int)ins->x + THREAT_NAME_W_CLASSIC / 2);
						fprintf(yf, "  - { %s, x: %d, y: %d, center_at: %d }\n", id_field, ax, ay, center_at);
					} else if (i >= 79 && i <= 82 /*THREAT label/value*/) {
						const int label_at = TieCockpitExtract_HdXFromClassic(THREAT_LABEL_X_CLASSIC);
						fprintf(yf, "  - { %s, x: %d, y: %d, label_at: %d }\n", id_field, ax, ay, label_at);
					} else {
						fprintf(yf, "  - { %s, x: %d, y: %d }\n", id_field, ax, ay);
					}
				}
			}

			fprintf(yf, "cels:\n");
			for (uint32_t i = 0; i < pnl.list.count; ++i) {
				int cx = cels[i].ax, cy = cels[i].ay;
				int cw = cels[i].w, ch = cels[i].h;
				int ax = cx, ay = cy, aw = cw, ah = ch;
				if (!o->no_upscale && cw > 0 && ch > 0) {
					int x0 = scale_svga_xy_to_4k(cx);
					int x1 = scale_svga_xy_to_4k(cx + cw);
					int y0 = scale_svga_xy_to_4k(cy);
					int y1 = scale_svga_xy_to_4k(cy + ch);
					ax = x0;
					aw = x1 - x0;
					ay = y0;
					ah = y1 - y0;
				}
				/* `size` is in reference-frame units; 16:9 mode scales
				 * it by 2.25× to keep cels in lockstep with the new
				 * ref. */
				int sw = cw, sh = ch;
				if (o->pillarbox_16x9) {
					sw = TieCockpitExtract_HdScaleClassic(cw);
					sh = TieCockpitExtract_HdScaleClassic(ch);
				}
				fprintf(yf,
						"  - size:  { w: %d, h: %d }\n"
						"    atlas: { x: %d, y: %d, w: %d, h: %d }\n",
						sw, sh, ax, ay, aw, ah);
			}
			fclose(yf);
		}
	}

	free(atlas);
	free(cels);
	TieCockpitExtractShapeFile_Close(&pnl);
	return ok;
}

/* ----- post-pass: standalone panel LFDs not in any .INT --------- */

/* Some panel LFDs aren't referenced by a craft .INT — CAMERA.LFD,
 * CRT.LFD, FILM.LFD are loaded directly by replayio's viewer code
 * (load_viewer_panel) and by panel_update3Dcrt's PIP path. They
 * have the same PANL+MASK+PLTT layout the per-craft views use, so
 * the same emit_cockpit_view path works as long as we pass view=NULL
 * (mask gets applied at viewport (0,0,w,h) — see emit_cockpit_view). */
static void TieCockpitExtract_ScanStandaloneLfds(const TieCockpitExtractOptions* o, const uint8_t* vga_pac,
												 const char* parts_dir, TieCockpitExtractSeenSet* seen_views,
												 TieCockpitExtractLog* emit_log) {
	DIR* d = opendir(o->cp640_dir);
	if (!d)
		return;
	int found = 0;
	struct dirent* de;
	while ((de = readdir(d))) {
		size_t nl = strlen(de->d_name);
		if (nl < 5)
			continue;
		const char* ext = de->d_name + nl - 4;
		if (!TieCockpitExtract_EqualIgnoreCase(ext, ".LFD"))
			continue;

		char base[16] = { 0 };
		size_t bl = (size_t)(ext - de->d_name);
		if (bl >= sizeof base)
			continue;
		memcpy(base, de->d_name, bl);
		if (TieCockpitExtract_SeenContains(seen_views, base))
			continue;

		/* Check this is a panel-LFD (PANL + MASK + PLTT) and not some
		 * other LFD file. Filter by opening + finding the three
		 * sections — cheap, file is mmap'd once. */
		char path[1024];
		snprintf(path, sizeof path, "%s/%s", o->cp640_dir, de->d_name);
		TieCockpitExtractPanelFile lfd;
		char err[256] = { 0 };
		if (!TieCockpitExtractPanelFile_Open(&lfd, path, err, sizeof err))
			continue;
		bool is_panel = TieCockpitExtractPanelFile_Find(&lfd, COCKPIT_FOURCC_PANL) &&
						TieCockpitExtractPanelFile_Find(&lfd, COCKPIT_FOURCC_MASK) &&
						TieCockpitExtractPanelFile_Find(&lfd, COCKPIT_FOURCC_PLTT);
		TieCockpitExtractPanelFile_Close(&lfd);
		if (!is_panel)
			continue;

		if (found == 0)
			printf("  [standalone LFDs]\n");
		char verr[256] = { 0 };
		if (TieCockpitExtract_EmitCockpitView(o, o->cp640_dir, base, /*view=*/NULL, vga_pac, parts_dir,
											  emit_log, verr, sizeof verr)) {
			printf("    %s_640.ktx2\n", base);
			TieCockpitExtract_SeenAdd(seen_views, base);
			++found;
		} else {
			fprintf(stderr, "    %s: %s\n", base, verr);
		}
	}
	closedir(d);
}

/* ----- per-craft driver ------------------------------------------ */

static int TieCockpitExtract_ProcessCraft(const TieCockpitExtractOptions* o, const char* int_path,
										  const uint8_t* vga_pac, const char* parts_dir,
										  TieCockpitExtractSeenSet* seen_views,
										  TieCockpitExtractSeenSet* seen_parts,
										  TieCockpitExtractLog* emit_log) {
	const char* base = strrchr(int_path, '/');
	base = base ? base + 1 : int_path;
	char craft[16] = { 0 };
	const char* dot = strchr(base, '.');
	size_t n = dot ? (size_t)(dot - base) : strlen(base);
	if (n >= sizeof craft)
		n = sizeof craft - 1;
	memcpy(craft, base, n);

	TieCockpitPanelInt pi;
	char err[256] = { 0 };
	if (!TieCockpitPanelInt_Open(&pi, int_path, err, sizeof err)) {
		fprintf(stderr, "  skip %s: %s\n", craft, err);
		return 0;
	}

	int views_ok = 0;
	for (int i = 0; i < PANEL_INT_NUM_VIEWS; ++i) {
		if (pi.views[i].flags != 1)
			continue; /* skip inherited/mirrored */
		const char* vname = pi.views[i].name;

		/* Shared inflight overlay views (MISSION/INFLIGHT/RADIO/DAMAGE/
		 * WINGMAN/KEYBOARD/FLTOPTN) appear in every craft's .INT with
		 * flags=1 and a 640×480 viewport. They're not craft-specific —
		 * the runtime loads the same .LFD file across all cockpits.
		 * The dedup table makes processing them once-per-run correct;
		 * the per-craft loop just re-encounters the same view_name and
		 * hits the `cached` path. */
		if (TieCockpitExtract_SeenContains(seen_views, vname)) {
			printf("    %s_640.ktx2 (cached)\n", vname);
			views_ok++;
			continue;
		}
		char verr[256] = { 0 };
		if (TieCockpitExtract_EmitCockpitView(o, o->cp640_dir, vname, &pi.views[i], vga_pac, parts_dir,
											  emit_log, verr, sizeof verr)) {
			printf("    %s_640.ktx2\n", vname);
			TieCockpitExtract_SeenAdd(seen_views, vname);
			views_ok++;
		} else {
			fprintf(stderr, "    %s: %s\n", vname, verr);
		}
	}

	/* Parts atlas — same dedup, multiple craft can share a .PNL. */
	if (TieCockpitExtract_SeenContains(seen_parts, pi.parts_basename)) {
		printf("    %s_parts.ktx2 (cached)\n", pi.parts_basename);
	} else {
		char perr[256] = { 0 };
		if (TieCockpitExtract_EmitPartsAtlas(o, o->cp640_dir, &pi, vga_pac, parts_dir, emit_log, perr,
											 sizeof perr)) {
			printf("    %s_parts.ktx2\n", pi.parts_basename);
			TieCockpitExtract_SeenAdd(seen_parts, pi.parts_basename);
		} else {
			fprintf(stderr, "    %s: %s\n", pi.parts_basename, perr);
		}
	}

	return views_ok;
}

/* ----- main ------------------------------------------------------ */

/* ----- CMD CRT occlusion mask bake -----------------------------------
 *
 * panel_update3Dcrt's per-spec scanline-RLE mask (panel.c:2497+, 7 SVGA
 * variants + 4 VGA variants) is copied at runtime into xtransdata's
 * mask buffer. xtrans2's rasterizer reads it via mask_read_delta (with
 * its +255 / +511 extended-delta encoding) and clips PIP scanlines to
 * the OPEN runs.
 *
 * The HD compositor doesn't have an xtrans2 to clip; it samples a
 * pre-baked 8-bit alpha texture and multiplies it into the PIP blit so
 * silhouette pixels outside the CRT bezel are erased. This pass walks
 * each mask table once at extract time and emits one KTX2 per
 * (variant, resolution) — alpha=255 inside the OPEN run, alpha=0
 * elsewhere; RGB unused (zeroed). Native PIP resolution preserves the
 * scanline-hard cutout when sampled NEAREST; the HD path handles
 * up-sampling to whatever fraction of the cockpit RT the
 * instruments[2] rect lands at.
 *
 * Filenames: crt_mask_<variant>_<res>.ktx2 — <variant> matches the
 * TieCockpitState.mask_variant integer (0..6) TieHudSnapshot_Capture
 * already publishes; <res> is 320 (VGA) or 640 (SVGA). Mask data not
 * present for a given (variant, res) (e.g. VGA has no spec4/spec5
 * variants — those fall back to default in panel_update3Dcrt) is
 * skipped. */

/* One bake-table row — (variant, res) → mask blob + size. */
typedef struct {
	int variant; /* mask_variant 0..6 */
	int res;     /* 320 (VGA) or 640 (SVGA) */
	const uint8_t* blob;
	size_t size;
} TieCockpitExtractCrtMaskRow;

static bool TieCockpitExtract_EmitOneCrtMask(const TieCockpitExtractOptions* o, const char* parts_dir,
											 const TieCockpitExtractCrtMaskRow* row,
											 TieCockpitExtractLog* log) {
	int pip_w = 0, pip_h = 0;
	uint8_t* rgba = NULL;
	TieFormatError codec_error = { 0 };
	if (!TieCockpitCrtMask_Decode(row->blob, row->size, &rgba, &pip_w, &pip_h, &codec_error)) {
		fprintf(stderr, "  warn: crt_mask v%d/%d decode failed: %s\n", row->variant, row->res,
				codec_error.message);
		return false;
	}

	/* Native PIP-resolution texture — the HD blit samples this with
	 * NEAREST so the scanline-hard cutout reproduces classic exactly.
	 * Upscaling here would add zero precision but ~20× the file size. */
	char ktx2_path[1024];
	snprintf(ktx2_path, sizeof ktx2_path, "%s/crt_mask_%d_%d.ktx2", parts_dir, row->variant, row->res);

	bool ok = false;
	/* CRT cutout mask = coverage / alpha data (NOT colour). LINEAR. */
	if (o->rgba_only) {
		ok =
			write_ktx2_rgba_with_generated_mips(ktx2_path, pip_w, pip_h, rgba, KTX2_TF_LINEAR, /*zstd=*/true);
	} else {
		ok = write_ktx2_bc7_with_generated_mips(ktx2_path, pip_w, pip_h, rgba, KTX2_BC7_QUALITY_MED,
												KTX2_TF_LINEAR, /*zstd=*/true);
	}
	if (!ok) {
		fprintf(stderr, "  warn: KTX2 write failed: %s\n", ktx2_path);
		free(rgba);
		return false;
	}
	if (!o->skip_png) {
		char png_path[1024];
		snprintf(png_path, sizeof png_path, "%s/crt_mask_%d_%d.png", parts_dir, row->variant, row->res);
		write_png_rgba(png_path, pip_w, pip_h, rgba);
	}
	printf("    crt_mask_%d_%d.ktx2 (%dx%d)\n", row->variant, row->res, pip_w, pip_h);
	if (log) {
		char rel[256];
		snprintf(rel, sizeof rel, "%s/cockpits/crt_mask_%d_%d.ktx2",
				 o->pillarbox_16x9 ? "flight-16x9" : "flight", row->variant, row->res);
		TieCockpitExtract_EmitLogAddMask(log, row->variant, row->res, rel);
	}
	free(rgba);
	return true;
}

/* Drive the 11 (variant, resolution) entries panel_update3Dcrt
 * selects between. Variant indices mirror TieCockpitState.mask_variant
 * (0=default, 1=gunboat, 2=tieadv7, 3=tieadv8, 4=spec4, 5=spec5,
 * 6=missileboat); VGA collapses tieadv7 and tieadv8 onto variant 2 and
 * has no spec4/spec5 entries, matching the binary's VGA dispatch. */
static int TieCockpitExtract_EmitCrtMasks(const TieCockpitExtractOptions* o, const char* parts_dir,
										  TieCockpitExtractLog* log) {
	const TieCockpitExtractCrtMaskRow rows[] = {
		{ 0, 640, cmd640maskdata, sizeof cmd640maskdata },
		{ 1, 640, gunboatcmd640maskdata, sizeof gunboatcmd640maskdata },
		{ 2, 640, tieadv7cmd640maskdata, sizeof tieadv7cmd640maskdata },
		{ 3, 640, tieadv8cmd640maskdata, sizeof tieadv8cmd640maskdata },
		{ 4, 640, spec4cmd640maskdata, sizeof spec4cmd640maskdata },
		{ 5, 640, spec5cmd640maskdata, sizeof spec5cmd640maskdata },
		{ 6, 640, missileboatcmd640maskdata, sizeof missileboatcmd640maskdata },
		{ 0, 320, cmdmaskdata, sizeof cmdmaskdata },
		{ 1, 320, gunboatcmdmaskdata, sizeof gunboatcmdmaskdata },
		{ 2, 320, tieadvcmdmaskdata, sizeof tieadvcmdmaskdata },
		{ 6, 320, missileboatcmdmaskdata, sizeof missileboatcmdmaskdata },
	};
	int ok = 0;
	printf("  [CMD CRT masks]\n");
	for (size_t i = 0; i < sizeof rows / sizeof rows[0]; ++i)
		if (TieCockpitExtract_EmitOneCrtMask(o, parts_dir, &rows[i], log))
			++ok;
	return ok;
}

int main(int argc, char** argv) {
	TieCockpitExtractOptions o;
	TieCockpitExtract_ParseOpts(argc, argv, &o);

	long vga_sz = 0;
	uint8_t* vga_pac = TieCockpitExtract_LoadFile(o.vga_pac_path, &vga_sz);
	if (!vga_pac || vga_sz < 576)
		TieCockpitExtract_Die("cannot load VGA.PAC at %s (got %ld bytes, need ≥576)", o.vga_pac_path, vga_sz);

	char parts_dir[1024];
	/* --16x9 emits to a sibling tree so the 4:3 set isn't disturbed. */
	const char* flight_sub = o.pillarbox_16x9 ? "tie_remaster/flight-16x9" : "tie_remaster/flight";
	snprintf(parts_dir, sizeof parts_dir, "%s/%s/cockpits", o.out_dir, flight_sub);
	if (TieCockpitExtract_MakePath(parts_dir) != 0)
		TieCockpitExtract_Die("cannot create %s: %s", parts_dir, strerror(errno));

	DIR* d = opendir(o.cp640_dir);
	if (!d)
		TieCockpitExtract_Die("cannot open %s: %s", o.cp640_dir, strerror(errno));

	TieCockpitExtractSeenSet seen_views = { { { 0 } }, 0 };
	TieCockpitExtractSeenSet seen_parts = { { { 0 } }, 0 };
	TieCockpitExtractLog* emit_log = (TieCockpitExtractLog*)calloc(1, sizeof *emit_log);

	int crafts = 0;
	struct dirent* de;
	while ((de = readdir(d))) {
		size_t nl = strlen(de->d_name);
		if (nl < 5)
			continue;
		const char* ext = de->d_name + nl - 4;
		if (!TieCockpitExtract_EqualIgnoreCase(ext, ".INT"))
			continue;

		char craft_basename[16] = { 0 };
		size_t bl = (size_t)(ext - de->d_name);
		if (bl >= sizeof craft_basename)
			continue;
		memcpy(craft_basename, de->d_name, bl);

		if (o.only_craft && !TieCockpitExtract_EqualIgnoreCase(craft_basename, o.only_craft))
			continue;

		char int_path[1024];
		snprintf(int_path, sizeof int_path, "%s/%s", o.cp640_dir, de->d_name);
		printf("  %s\n", craft_basename);
		int n = TieCockpitExtract_ProcessCraft(&o, int_path, vga_pac, parts_dir, &seen_views, &seen_parts,
											   emit_log);
		if (n > 0)
			++crafts;
	}
	closedir(d);

	/* Post-pass: any panel-shaped LFD (PANL+MASK+PLTT) we haven't
	 * already extracted via the .INT loop. Catches the replay /
	 * film / CRT viewer panels that aren't referenced from any
	 * craft's .INT but ship in CP640. */
	if (!o.only_craft)
		TieCockpitExtract_ScanStandaloneLfds(&o, vga_pac, parts_dir, &seen_views, emit_log);

	/* Post-pass: emit the CMD CRT occlusion masks once. Variant
	 * selection at draw time keys on snap->cockpit.mask_variant
	 * (player_spec_num — not the cockpit identity), so we emit all
	 * 11 (variant, res) entries on EVERY run, including when --craft
	 * narrows the cockpit-LFD list. Idempotent / cheap so always-on
	 * is fine. */
	TieCockpitExtract_EmitCrtMasks(&o, parts_dir, emit_log);

	/* Catalog snippet for remaster/flight/assets.yaml. */
	if (emit_log) {
		char snippet_path[1024];
		snprintf(snippet_path, sizeof snippet_path, "%s/%s/cockpits-block.yaml", o.out_dir, flight_sub);
		FILE* sf = fopen(snippet_path, "w");
		if (sf) {
			fprintf(sf, "# Auto-generated by cockpit_extract.\n"
						"# Merge these top-level rows into remaster/flight/assets.yaml.\n"
						"cockpit_views:\n");
			for (int i = 0; i < emit_log->view_count; ++i) {
				const TieCockpitExtractView* v = &emit_log->views[i];
				fputs("    - { view: \"", sf);
				fputs(v->view, sf);
				fputs("\"", sf);
				if (v->bitmap_hd[0])
					fprintf(sf, ", bitmap_hd: \"%s\"", v->bitmap_hd);
				if (v->bitmap_4_3[0])
					fprintf(sf, ", bitmap_4_3: \"%s\"", v->bitmap_4_3);
				if (v->damage[0])
					fprintf(sf, ", damage: \"%s\"", v->damage);
				fputs(" }\n", sf);
			}
			fprintf(sf, "\ncockpit_parts:\n");
			for (int i = 0; i < emit_log->parts_count; ++i) {
				const TieCockpitExtractParts* p = &emit_log->parts[i];
				fprintf(sf,
						"    - { parts: \"%s\", atlas: \"%s\", "
						"layout: \"%s\" }\n",
						p->parts, p->atlas, p->layout);
			}
			fprintf(sf, "\ncockpit_masks:\n");
			for (int i = 0; i < emit_log->mask_count; ++i) {
				const TieCockpitExtractMask* m = &emit_log->masks[i];
				fprintf(sf,
						"    - { variant: %d, classic_w: %d, "
						"path: \"%s\" }\n",
						m->variant, m->classic_w, m->path);
			}
			fclose(sf);
			printf("Wrote cockpits-block snippet: %s\n", snippet_path);
		} else {
			fprintf(stderr, "cockpit_extract: failed to write snippet %s: %s\n", snippet_path,
					strerror(errno));
		}
		free(emit_log);
	}

	free(vga_pac);

	printf("Done: %d crafts processed.\n", crafts);
	return crafts ? 0 : 1;
}
