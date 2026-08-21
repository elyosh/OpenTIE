#include "imgui.h"

#include <SDL3/SDL.h>
#include "aeron/aeron.h"
#include "aeron/scene/draw_list2d.h"
#include "aeron/scene/image_cache.h"

#include "film.h"
#include "film_player.h"
#include "fourcc.h"
#include "lfd_file.h"
#include "imgbake/png_read.h"
#include "imgbake/png_write.h"

extern "C" {
#include "tie_remaster/scene2d/manifest.h"          /* manifest types (TieScene2dManifest, ActorView) */
#include "tie_remaster/scene2d/cutscene.h"
#include "play1_streams.h"     /* play1_stream_for_film auto-lookup */
#include "util.h"
#include "tie_remaster/scene2d/viewport.h"          /* TieScene2dViewportTransform — classic→4K pillarbox math */
#include "imgbake/ktx2_writer.h"       /* write_ktx2_bc7_with_generated_mips */
#include "imgbake/atlas_pack.h"        /* atlas_pack_compute / _post_dims / _emit_yaml */
}

#include <algorithm>
#include <climits>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <map>
#include <mutex>
#include <string>
#include <unistd.h>
#include <vector>

namespace {

struct TieFilmViewApp {
	/* LFD chain (primary + extras). Owned. */
	TieLfdFile  primary{};
	std::vector<TieLfdFile> extras;
	std::vector<const TieLfdFile *> chain_storage;
	TieLfdFileChain chain{};

	/* Indices into primary.entries[] of every FILM resource. */
	std::vector<uint32_t> film_entry_indices;

	int   current_film = -1;
	TieFilmPlayer player{};
	bool  player_loaded = false;

	/* Framebuffer dimensions. (320, 200) for the VGA build (default);
	 * (640, 480) when --svga is on the command line. The player and
	 * the display path both honor these. svga_mode also disables the
	 * 1.2× vertical pixel-aspect correction (SVGA is square pixels).*/
	int   fb_w = PLAYER_FB_W;
	int   fb_h = PLAYER_FB_H;
	bool  svga_mode = false;

	/* Display state. Heap-allocated; sized at startup to fb_w*fb_h*4. */
	std::vector<uint8_t> rgba;
	/* Re-composited whenever the user steps a cel and uploaded to the
	 * Aeron-owned display target before the next UI frame. */
	bool                   fb_dirty = true;
	uint32_t               fb_generation = 0;
	/* Aspect-corrected display path: the CPU framebuffer is uploaded into
	   a render target at an integer N×N
	   prescale with NEAREST filtering, then ImGui::Image bilinearly
	   stretches that to the 4:3 display size. The integer prescale
	   preserves crisp pixel boundaries; the small final stretch
	   (≤1.2× in one axis) is the only blur introduced. In SVGA mode
	   the display is square-pixel so there's no final stretch — the
	   prescale lands at the exact display size. */
	AeronRenderTarget *fb_scaled_target = nullptr;
	int             fb_scaled_factor = 0;

	/* UI state. */
	int   zoom = 3;          /* 1..6, integer pixel-doubling */
	bool  show_bbox = true;
	bool  show_clip = false;
	bool  show_labels = true;
	bool  playing = false;
	float fps = 14.0f;       /* common cutscene cel rate */
	double play_accum_s = 0.0;
	int   selected_actor = -1;

	/* Where to drop screenshot files. Filled from CWD on init. */
	std::string screenshot_dir;
	std::string last_screenshot;

	/* "type 'name'" entries for any FilmObject whose resource didn't
	   resolve in the LFD chain. Repopulated on each film load. Driving
	   a banner in the Actors window so the user immediately notices
	   they forgot --extra <empire-or-similar>.lfd. */
	std::vector<std::string> missing_resources;

	/* remaster preview state. Populated when --remaster <dir> is on
	   the command line and at least one manifest under that root
	   parses cleanly. The preview render target sits at a fixed 1920x
	   1080 — the cutscene compositor reads SDL_GetCurrentRenderOutputSize
	   for its viewport math, so a stable RT means the displayed
	   composition is invariant to the filmview window size. */
	TieScene2dCutscene *cutscene = nullptr;
	AeronRenderTarget *cutscene_target = nullptr;
	AeronDrawList2D *cutscene_draw_list = nullptr;
	int            cutscene_w = 1920;
	int            cutscene_h = 1080;
	bool           remaster_enabled = false;     /* user toggle (Tab / checkbox) */
	std::string    remaster_dir;
	std::string    lfd_basename;                 /* uppercase, no extension */

	/* File-dialog plumbing. SDL_ShowOpenFileDialog's callback may run on
	   a worker thread; results are stashed here under a mutex and drained
	   from the main loop before the next ImGui frame. */
	enum class FileOp { None, OpenPrimary, AddExtra, BindStream,
	                    BindBakeSource };
	std::mutex   file_mutex;
	FileOp       pending_op   = FileOp::None;
	std::string  pending_path;
	/* Path of the currently-loaded primary, kept so the dialog can
	   default to the same directory each time it's reopened. */
	std::string  primary_path;
	/* Banner shown briefly after a successful open (Films panel). */
	std::string  status_msg;

	/* WRK bindings prefer per-film overrides, then the default path, then
	 * lookup under stream_dir. ASTREAM selects the retail lookup table. */
	std::map<std::string, std::string> stream_overrides;
	std::string                         stream_default_path;
	std::string                         stream_dir;
	bool                                stream_dir_is_retail = true;
	/* Path of the .WRK currently bound to the active film's stream
	 * actor, for display in the Films panel. Empty if unbound. */
	std::string                         current_stream_path;
	/* Last error from a failed bind attempt, shown in the Films panel
	 * until the next successful load. */
	std::string                         stream_error;

	/* Lazily captured YAML rectangles support per-frame reset and dirty state. */
	std::map<std::string, std::vector<TieScene2dRect>> atlas_baseline;
	std::map<std::string, bool>                      atlas_dirty;
	std::string                                      atlas_status;

	/* Baking maps a pillarboxed 4K reference crop into an ANIM atlas slot while
	 * retaining the atlas alpha mask. The app owns all override textures. */
	std::string                                bake_source_path;
	/* Session-scoped directory of the last successful "Pick source PNG"
	 * pick, so the dialog reopens at the same location. Reset to empty
	 * at startup; never written to disk. */
	std::string                                bake_source_dir;
	int                                        bake_source_w = 0;
	int                                        bake_source_h = 0;
	std::vector<uint8_t>                       bake_source_rgba;
	std::string                                bake_status;

	/* Optional dilation and box blur preprocess the baseline alpha mask. */
	int                                        bake_dilate_px = 0;
	int                                        bake_blur_px   = 0;

	struct AtlasPixelEdit {
		int                  w = 0;
		int                  h = 0;
		std::vector<uint8_t> rgba;          /* current RGBA8, w*h*4 */
		std::vector<uint8_t> baseline_rgba; /* on-load snapshot */
		std::string          png_path;      /* <atlas>.png */
		std::string          asset_path;    /* <atlas>.ktx2 (override key) */
		AeronTexture        *override_tex = nullptr;
		bool                 dirty = false;
	};
	std::map<std::string, AtlasPixelEdit>      atlas_pixels;
	bool quit_requested = false;
};

/* ---------- helpers ---------- */

static void TieFilmView_ShowError(const char *fmt, ...) {
	char buf[1024];
	va_list ap;
	va_start(ap, fmt);
	std::vsnprintf(buf, sizeof buf, fmt, ap);
	va_end(ap);
	std::fprintf(stderr, "filmview: %s\n", buf);
	const AeronMessageBoxButton button { .id = 0, .label = "OK", .is_default = 1 };
	AeronMessageBoxOptions options {
		.kind = AERON_MESSAGE_BOX_ERROR,
		.title = "filmview",
		.message = buf,
		.buttons = &button,
		.button_count = 1,
	};
	Aeron_ShowMessageBox(&options, nullptr);
}

static std::string TieFilmView_FourccString(uint32_t v) {
	char tmp[5];
	TieFilmFourcc_Str(v, tmp);
	return std::string(tmp);
}

static void TieFilmView_RecomposeIntoTexture(TieFilmViewApp &app) {
	if (!app.player_loaded) return;
	TieFilmPlayer_Composite(&app.player);
	TieFilmPlayer_RenderRgba(&app.player, app.rgba.data());
	/* Upload happens before the next hosted UI frame. */
	app.fb_generation++;
	app.fb_dirty = true;
}

/* ============================================================
 * remaster preview integration
 *
 * filmview links the same Aeron cutscene compositor as the game and
 * drives it against an
 * actor list synthesised from TieFilmPlayerObject. This lets the asset
 * team verify remaster asset alignment without launching the engine.
 * ============================================================
 */

/* Adapter: walk filmview's TieFilmPlayerObject array and produce a flat
 * TieScene2dActorView[] in z-order back-to-front. Filmview's
 * `gui_hidden` per-actor toggle is honoured here so the preview
 * matches what the user sees in the Original render. */
static int TieFilmView_BuildActorViews(const TieFilmViewApp &app, TieScene2dActorView *out, int cap) {
	int n = 0;
	for (int i = 0; i < app.player.object_count && n < cap; i++) {
		const TieFilmPlayerObject *o = &app.player.objects[i];
		if (o->type_code != FILM_TC_ACTOR) continue;
		if (o->gui_hidden)                 continue;

		int sl, st, sr, sb;
		if (!TieFilmPlayer_ActorScreenRect(o, &sl, &st, &sr, &sb))
			continue;

		TieScene2dActorView *v = &out[n++];
		v->res_type = o->res_type;
		std::memcpy(v->res_name, o->res_name, 8);
		v->film_entry_index = (int16_t)o->entry_index;
		v->x = (int16_t)sl;
		v->y = (int16_t)st;
		v->w = (int16_t)(sr - sl);
		v->h = (int16_t)(sb - st);
		/* TieFilmPlayerObject doesn't simulate engine scaling — the preview
		 * shows assets at their authored canonical size. Identity
		 * scale; `fit: extend` actors render at full natural size. */
		v->xscale = 256;
		v->yscale = 256;
		v->zplane = (int16_t)o->zplane;
		v->state  = (int16_t)o->state;
		bool effectively_shown = o->show || o->force_show;
		v->flags  = (uint16_t)((effectively_shown ? ACTOR2D_VISIBLE : 0) |
		                       (o->hflip ? ACTOR2D_HFLIP   : 0) |
		                       (o->vflip ? ACTOR2D_VFLIP   : 0));
		/* TieFilmPlayerObject doesn't track per-cel ACTOR_CLIP — default to
		 * the canvas-wide no-op rect. The compositor's default-anchor
		 * path intersects against this; the cockpit-window actors
		 * that use ACTOR_CLIP in the engine will draw without that
		 * narrowing in the preview, which is fine for layout review
		 * (the dbridge 4K overlay's alpha cutouts handle masking). */
		v->clip_left = 0;
		v->clip_top  = 0;
		v->clip_right  = (int16_t)app.fb_w;
		v->clip_bottom = (int16_t)app.fb_h;
		/* Filmview previews render at integer cel granularity — no
		 * sub-cel motion. Pin prev to current and zero the velocity
		 * so the compositor's lerp degenerates to a snap, matching
		 * what filmview already showed before the smoothing channel
		 * was added. */
		v->prev_x  = v->x; v->prev_y  = v->y;
		v->prev_xv = 0;    v->prev_yv = 0;
		v->xv = 0; v->yv = 0; v->xvf = 0; v->yvf = 0;
		v->prev_xscale = v->xscale;
		v->prev_yscale = v->yscale;
		v->frame_progress = 0.0f;
		v->cel_period_us  = 0;
		v->z_order = -1;
	}
	/* Sort back-to-front: largest zplane first. Matches the engine's
	 * lactor_Sort_Actor_ZPlanes ordering and player_composite's draw
	 * walk. Stable sort tiebreak via entry_index (lower drawn first
	 * within the same zplane). */
	std::sort(out, out + n,
	          [](const TieScene2dActorView &a, const TieScene2dActorView &b) {
		          if (a.zplane != b.zplane) return a.zplane > b.zplane;
		          return a.film_entry_index < b.film_entry_index;
	          });
	return n;
}

/* Derive the LFD basename in the form the cutscene compositor's
 * bundle map expects (uppercase, no path/extension). e.g.
 * "/path/BRIDGE.LFD" → "BRIDGE". */
static std::string TieFilmView_LfdBasenameFrom(const char *path) {
	const char *base = std::strrchr(path, '/');
	base = base ? base + 1 : path;
	const char *dot = std::strrchr(base, '.');
	std::string s = dot ? std::string(base, (size_t)(dot - base))
	                    : std::string(base);
	for (auto &c : s) c = (char)std::toupper((unsigned char)c);
	return s;
}

/* ============================================================
 * atlas-bake mode
 *
 * Crops the actor's classic-coord screen rect from the user's
 * 3840×2160 reference PNG (16:9 with 4:3 classic content
 * pillarboxed) through TieScene2dViewport_ComputeXform's letterbox math,
 * resamples bilinearly into the atlas frame's pixel slot, masks
 * by the existing atlas alpha so transparent regions stay
 * transparent, and uploads the result as an Aeron texture
 * override. The compositor samples the
 * override on the next frame — no KTX2 re-bake required for live
 * preview (see Q4 (c) in the design discussion). Saving writes
 * the modified RGBA back to the sibling PNG via tmp+rename;
 * regenerating the BC7 KTX2 is a separate, decoupled step the
 * asset team triggers manually.
 * ============================================================
 */

/* Separable max filter over the alpha plane in `buf` (rw × rh, 1
 * byte/pixel). `radius` is Chebyshev — the dilated value at (x, y)
 * is max(buf[x±r, y±r]). Border samples clamp to the rect edge.
 * O(rw × rh × radius); naive but the affected rect is a single ANIM
 * frame slot (typically <500×400) and radii are <16, so this runs
 * in microseconds. `tmp` is caller-owned scratch sized rw × rh. */
static void TieFilmView_DilateAlphaSeparable(uint8_t *buf, uint8_t *tmp,
                                   int rw, int rh, int radius) {
	if (radius <= 0) return;
	for (int y = 0; y < rh; y++) {
		const uint8_t *src = buf + y * rw;
		uint8_t       *dst = tmp + y * rw;
		for (int x = 0; x < rw; x++) {
			int x0 = x - radius; if (x0 < 0)        x0 = 0;
			int x1 = x + radius; if (x1 >= rw)      x1 = rw - 1;
			uint8_t m = 0;
			for (int xx = x0; xx <= x1; xx++)
				if (src[xx] > m) m = src[xx];
			dst[x] = m;
		}
	}
	for (int x = 0; x < rw; x++) {
		for (int y = 0; y < rh; y++) {
			int y0 = y - radius; if (y0 < 0)   y0 = 0;
			int y1 = y + radius; if (y1 >= rh) y1 = rh - 1;
			uint8_t m = 0;
			for (int yy = y0; yy <= y1; yy++) {
				uint8_t v = tmp[yy * rw + x];
				if (v > m) m = v;
			}
			buf[y * rw + x] = m;
		}
	}
}

/* Separable box blur over `buf` (rw × rh, 1 byte/pixel). Same
 * cost profile as dilate_alpha_separable. `tmp` is caller-owned
 * scratch sized rw × rh × sizeof(uint16_t) for the horizontal
 * intermediate (16 bits to hold sums up to 255 × (2*radius+1)). */
static void TieFilmView_BlurAlphaSeparable(uint8_t *buf, uint16_t *tmp,
                                 int rw, int rh, int radius) {
	if (radius <= 0) return;
	for (int y = 0; y < rh; y++) {
		const uint8_t *src = buf + y * rw;
		uint16_t      *dst = tmp + y * rw;
		for (int x = 0; x < rw; x++) {
			int x0 = x - radius; if (x0 < 0)   x0 = 0;
			int x1 = x + radius; if (x1 >= rw) x1 = rw - 1;
			int sum = 0, count = 0;
			for (int xx = x0; xx <= x1; xx++) { sum += src[xx]; count++; }
			dst[x] = (uint16_t)(sum / count);
		}
	}
	for (int x = 0; x < rw; x++) {
		for (int y = 0; y < rh; y++) {
			int y0 = y - radius; if (y0 < 0)   y0 = 0;
			int y1 = y + radius; if (y1 >= rh) y1 = rh - 1;
			int sum = 0, count = 0;
			for (int yy = y0; yy <= y1; yy++) {
				sum += tmp[yy * rw + x];
				count++;
			}
			buf[y * rw + x] = (uint8_t)(sum / count);
		}
	}
}

/* Build the per-frame mask used by the bake. Reads alpha from
 * `baseline` (the on-load atlas snapshot — keeps the operation
 * idempotent across re-bakes), applies dilate then blur with the
 * caller's radii, and writes the result into `out_alpha`
 * (rect_w × rect_h bytes). Both radii of 0 reproduces the original
 * "mask by existing atlas alpha" behavior. */
static void TieFilmView_ComputeBakeAlphaMask(const std::vector<uint8_t> &baseline,
                                    int atlas_w, int atlas_h,
                                    int rx, int ry, int rw, int rh,
                                    int dilate_px, int blur_px,
                                    std::vector<uint8_t> *out_alpha) {
	(void)atlas_h;
	out_alpha->assign((size_t)rw * (size_t)rh, 0);
	for (int y = 0; y < rh; y++) {
		for (int x = 0; x < rw; x++) {
			(*out_alpha)[y * rw + x] =
			    baseline[((size_t)(ry + y) * atlas_w + (rx + x)) * 4 + 3];
		}
	}
	if (dilate_px > 0) {
		std::vector<uint8_t> tmp((size_t)rw * (size_t)rh);
		TieFilmView_DilateAlphaSeparable(out_alpha->data(), tmp.data(),
		                       rw, rh, dilate_px);
	}
	if (blur_px > 0) {
		std::vector<uint16_t> tmp((size_t)rw * (size_t)rh);
		TieFilmView_BlurAlphaSeparable(out_alpha->data(), tmp.data(),
		                     rw, rh, blur_px);
	}
}

/* Bilinear sample of an RGB triple from `src` (W×H, 4 bytes/pixel)
 * at fractional pixel position (sx, sy). Pixels outside the image
 * clamp to the edge; alpha channel is ignored. The reference image
 * is opaque by definition, so alpha doesn't matter for the source. */
static inline void TieFilmView_SampleBilinearRgb(const uint8_t *src, int W, int H,
                                       float sx, float sy,
                                       uint8_t *r, uint8_t *g, uint8_t *b) {
	if (sx < 0.0f) sx = 0.0f; else if (sx > (float)(W - 1)) sx = (float)(W - 1);
	if (sy < 0.0f) sy = 0.0f; else if (sy > (float)(H - 1)) sy = (float)(H - 1);
	int x0 = (int)sx;       int y0 = (int)sy;
	int x1 = x0 + 1; if (x1 >= W) x1 = W - 1;
	int y1 = y0 + 1; if (y1 >= H) y1 = H - 1;
	float fx = sx - (float)x0;
	float fy = sy - (float)y0;
	const uint8_t *p00 = src + (y0 * W + x0) * 4;
	const uint8_t *p10 = src + (y0 * W + x1) * 4;
	const uint8_t *p01 = src + (y1 * W + x0) * 4;
	const uint8_t *p11 = src + (y1 * W + x1) * 4;
	float w00 = (1.0f - fx) * (1.0f - fy);
	float w10 =        fx  * (1.0f - fy);
	float w01 = (1.0f - fx) *        fy;
	float w11 =        fx  *        fy;
	float fr = p00[0]*w00 + p10[0]*w10 + p01[0]*w01 + p11[0]*w11;
	float fg = p00[1]*w00 + p10[1]*w10 + p01[1]*w01 + p11[1]*w11;
	float fb = p00[2]*w00 + p10[2]*w10 + p01[2]*w01 + p11[2]*w11;
	*r = (uint8_t)(fr + 0.5f);
	*g = (uint8_t)(fg + 0.5f);
	*b = (uint8_t)(fb + 0.5f);
}

/* Sibling-PNG path: replace ".ktx2" with ".png" on the resolved
 * asset_path. The remaster asset pipeline always writes both files
 * to the same directory with the same stem (filmextract `--atlas`
 * emits .png + .yaml + .ktx2 together). */
static std::string TieFilmView_AtlasPngFromKtx2(const char *ktx2_path) {
	if (!ktx2_path) return std::string();
	std::string s = ktx2_path;
	size_t dot = s.rfind('.');
	if (dot != std::string::npos &&
	    s.compare(dot, std::string::npos, ".ktx2") == 0)
		s.replace(dot, std::string::npos, ".png");
	else
		s.append(".png");
	return s;
}

/* Upload an editor-owned RGBA texture through Aeron. */
static AeronTexture *TieFilmView_UploadRgbaTexture(const uint8_t *rgba, int w, int h) {
	if (!rgba || w <= 0 || h <= 0) return nullptr;
	AeronCommandBuffer *cmd = Aeron_AcquireCommandBuffer();
	if (!cmd) return nullptr;
	AeronTexture *texture = Aeron_ImageUploadRgba8(
		cmd, rgba, w, h, (size_t)w * 4u, AERON_TEXTURE_FORMAT_RGBA8_SRGB,
		AERON_COLOR_SPACE_SRGB, AERON_IMAGE_ALPHA_STRAIGHT, false,
		"filmview atlas override");
	if (!texture || !Aeron_SubmitCommandBuffer(cmd)) {
		if (texture) Aeron_DestroyTexture(texture);
		else Aeron_CancelCommandBuffer(cmd);
		return nullptr;
	}
	return texture;
}

/* Free every override-owned Aeron texture and clear the per-atlas
 * map. Also tells the cutscene cache to drop its override pointers
 * so the next compose pass falls back to the on-disk KTX2. Called
 * on remaster reload + filmview shutdown. */
static void TieFilmView_ClearAllAtlasOverrides(TieFilmViewApp &app) {
	if (app.cutscene)
		TieScene2dCutscene_TextureUnoverrideAll(app.cutscene);
	for (auto &kv : app.atlas_pixels) {
		if (kv.second.override_tex)
			Aeron_DestroyTexture(kv.second.override_tex);
		kv.second.override_tex = nullptr;
	}
	app.atlas_pixels.clear();
}

/* Lazy-load the atlas's sibling PNG into memory on first edit. The
 * baseline copy is taken at the same time so Reset can revert to
 * pre-edit pixels without re-reading the file. */
static TieFilmViewApp::AtlasPixelEdit *TieFilmView_EnsureAtlasPixels(TieFilmViewApp &app,
                                                const char *asset_path,
                                                int expected_w,
                                                int expected_h) {
	if (!asset_path || !asset_path[0]) return nullptr;
	auto it = app.atlas_pixels.find(asset_path);
	if (it != app.atlas_pixels.end()) return &it->second;

	std::string png = TieFilmView_AtlasPngFromKtx2(asset_path);
	uint8_t *raw = nullptr;
	int w = 0, h = 0;
	char err[256] = {0};
	if (!read_png_rgba(png.c_str(), &raw, &w, &h, err, sizeof err)) {
		app.bake_status = std::string("PNG read failed: ") + err;
		return nullptr;
	}
	if (expected_w > 0 && expected_h > 0 &&
	    (w != expected_w || h != expected_h)) {
		app.bake_status = "PNG dims " + std::to_string(w) + "×" +
		                  std::to_string(h) +
		                  " do not match atlas " +
		                  std::to_string(expected_w) + "×" +
		                  std::to_string(expected_h);
		free(raw);
		return nullptr;
	}
	TieFilmViewApp::AtlasPixelEdit edit;
	edit.w = w; edit.h = h;
	edit.rgba.assign(raw, raw + (size_t)w * (size_t)h * 4u);
	edit.baseline_rgba = edit.rgba;
	edit.png_path   = png;
	edit.asset_path = asset_path;
	free(raw);
	auto ins = app.atlas_pixels.emplace(asset_path, std::move(edit));
	return &ins.first->second;
}

/* Push the current edited RGBA bytes to the GPU and register the
 * override against asset_path. Replaces any previous override
 * texture for the same atlas (the old Aeron texture is released). */
static bool TieFilmView_PushOverrideForAtlas(TieFilmViewApp &app, TieFilmViewApp::AtlasPixelEdit &edit) {
	AeronTexture *tex = TieFilmView_UploadRgbaTexture(edit.rgba.data(), edit.w, edit.h);
	if (!tex) {
		app.bake_status = "GPU texture upload failed";
		return false;
	}
	if (edit.override_tex)
		Aeron_DestroyTexture(edit.override_tex);
	edit.override_tex = tex;
	TieScene2dCutscene_TextureOverride(app.cutscene, edit.asset_path.c_str(),
	                              tex, edit.w, edit.h);
	return true;
}

/* Bake the source PNG into the selected actor's current frame. Handles
 * both ANIM (writes the cel-frame sub-rect inside the atlas PNG) and
 * DELT/RAW (writes the whole sprite PNG). Returns false (with
 * bake_status set) when prerequisites aren't met; on success
 * bake_status holds a one-line summary. */
static bool TieFilmView_BakeSelectedActorFrame(TieFilmViewApp &app) {
	app.bake_status.clear();

	if (!app.player_loaded) {
		app.bake_status = "no film loaded";
		return false;
	}
	if (!app.cutscene) {
		app.bake_status = "remaster mode disabled (--remaster <dir> required)";
		return false;
	}
	if (app.bake_source_rgba.empty()) {
		app.bake_status = "no source PNG bound — pick one first";
		return false;
	}
	if (app.selected_actor < 0 ||
	    app.selected_actor >= app.player.object_count) {
		app.bake_status = "no actor selected";
		return false;
	}
	TieFilmPlayerObject *o = &app.player.objects[app.selected_actor];
	const bool is_anim   = (o->res_type == FCC_ANIM && o->anim.count > 0);
	const bool is_sprite = (o->res_type == FCC_DELT || o->res_type == FCC_RAW);
	if (!is_anim && !is_sprite) {
		app.bake_status = "selected actor is not an ANIM/DELT/RAW";
		return false;
	}

	int sl, st, sr, sb;
	if (!TieFilmPlayer_ActorScreenRect(o, &sl, &st, &sr, &sb)) {
		app.bake_status = "actor has no on-screen rect (no sprite at this cel)";
		return false;
	}

	TieScene2dManifest *cs = TieScene2dCutscene_ManifestMut(app.cutscene);
	int cur_cel = TieFilmPlayer_DisplayedCel(&app.player);

	/* Resolve the target asset. ANIM uses the atlas's cel-frame sub-rect
	 * within a packed atlas PNG; DELT/RAW writes the whole sprite PNG.
	 * The mid-stage shared variables (asset_path, expected_w/h, dst rect,
	 * frame_idx) hold what the common bake loop below needs. frame_idx
	 * stays -1 for sprites so the status line can skip the "frame N"
	 * label. expected_w/h is 0 for sprites: ensure_atlas_pixels then
	 * skips its dim-mismatch check and we recover the dims from the
	 * loaded edit (the PNG is the source of truth). */
	const char *asset_path = nullptr;
	int expected_w = 0, expected_h = 0;
	int dx0 = 0, dy0 = 0, dw = 0, dh = 0;
	int frame_idx = -1;

	if (is_anim) {
		int s = o->state;
		while (s < 0)              s += o->anim.count;
		while (s >= o->anim.count) s -= o->anim.count;
		TieScene2dRect frame_rect{};
		int atlas_w = 0, atlas_h = 0, frame_count = 0;
		const char *yaml_path = nullptr;
		if (!TieScene2dManifest_AtlasGet(cs, app.lfd_basename.c_str(),
		                                  app.player.film_name, o->res_name,
		                                  (int16_t)o->entry_index, cur_cel,
		                                  /*frame_idx=*/0, nullptr, &atlas_w,
		                                  &atlas_h, &frame_count, &yaml_path,
		                                  &asset_path)) {
			app.bake_status = "no atlas variant resolves for this actor at this cel";
			return false;
		}
		frame_idx = s;
		if (frame_idx >= frame_count) frame_idx = frame_count - 1;
		TieScene2dManifest_AtlasGet(cs, app.lfd_basename.c_str(),
		                             app.player.film_name, o->res_name,
		                             (int16_t)o->entry_index, cur_cel,
		                             frame_idx, &frame_rect, &atlas_w, &atlas_h,
		                             &frame_count, &yaml_path, &asset_path);
		expected_w = atlas_w;
		expected_h = atlas_h;
		dx0 = (int)frame_rect.x;
		dy0 = (int)frame_rect.y;
		dw  = (int)frame_rect.w;
		dh  = (int)frame_rect.h;
		if (dw <= 0 || dh <= 0) {
			app.bake_status = "atlas frame rect is empty";
			return false;
		}
		if (dx0 < 0 || dy0 < 0 ||
		    dx0 + dw > atlas_w || dy0 + dh > atlas_h) {
			app.bake_status = "atlas frame rect runs off the atlas — fix the YAML first";
			return false;
		}
	} else {
		if (!TieScene2dManifest_SpriteGet(cs, app.lfd_basename.c_str(),
		                                   app.player.film_name, o->res_name,
		                                   (int16_t)o->entry_index, cur_cel,
		                                   &asset_path)) {
			app.bake_status = "no sprite variant resolves for this actor at this cel";
			return false;
		}
		/* expected_w/h stay 0; dst rect populated from edit->w/h once the
		 * PNG is loaded below. */
	}

	TieFilmViewApp::AtlasPixelEdit *edit =
	    TieFilmView_EnsureAtlasPixels(app, asset_path, expected_w, expected_h);
	if (!edit) return false;       /* bake_status set by ensure_atlas_pixels */

	const int atlas_w = edit->w;
	const int atlas_h = edit->h;
	if (is_sprite) {
		dx0 = 0; dy0 = 0;
		dw = atlas_w; dh = atlas_h;
		if (dw <= 0 || dh <= 0) {
			app.bake_status = "sprite PNG is empty";
			return false;
		}
	}

	/* Classic→source PNG xform. The compositor's viewport math
	 * letterboxes the 4:3 classic content inside a 16:9 viewport;
	 * with a 3840×2160 viewport that's region (480, 0, 2880, 2160)
	 * with scale (9, 10.8). Filmview's SVGA mode (fb 640×480)
	 * drives TieScene2dActorView in 640×480 coords; rescale to 320×200-
	 * equivalent before applying the xform so the math stays the
	 * same as the cutscene compositor's classic-VGA assumption. */
	TieScene2dViewportTransform vx;
	TieScene2dViewport_ComputeXform(app.bake_source_w, app.bake_source_h, &vx);
	float fb_to_classic_x = (float)CLASSIC_FB_W / (float)app.fb_w;
	float fb_to_classic_y = (float)CLASSIC_FB_H / (float)app.fb_h;
	float src_l = (float)vx.region_x +
	              (float)sl * fb_to_classic_x * vx.scale_x;
	float src_t = (float)vx.region_y +
	              (float)st * fb_to_classic_y * vx.scale_y;
	float src_r = (float)vx.region_x +
	              (float)sr * fb_to_classic_x * vx.scale_x;
	float src_b = (float)vx.region_y +
	              (float)sb * fb_to_classic_y * vx.scale_y;

	/* Build the mask from baseline alpha + the user's dilate/blur
	 * preprocessing. Reading from baseline keeps the operation
	 * idempotent: re-baking with different (dilate, blur) values
	 * produces the same result regardless of order, since each pass
	 * starts from the same on-load alpha shape. */
	std::vector<uint8_t> mask;
	TieFilmView_ComputeBakeAlphaMask(edit->baseline_rgba, atlas_w, atlas_h,
	                        dx0, dy0, dw, dh,
	                        app.bake_dilate_px, app.bake_blur_px,
	                        &mask);

	/* Bilinear-resample source crop into the dst slot, masking +
	 * blending by the preprocessed alpha. For dst pixel (dx, dy) within
	 * the dst rect, sample at the corresponding interpolated position
	 * inside the source rect. (dst_x + 0.5) / dw maps the pixel center
	 * to a [0,1] parameter; same for y. The alpha channel is overwritten
	 * with the preprocessed mask value so the silhouette expands /
	 * softens to match (otherwise dilation would have no visible
	 * effect). */
	const float src_dx = src_r - src_l;
	const float src_dy = src_b - src_t;
	uint8_t *atlas = edit->rgba.data();
	const uint8_t *src = app.bake_source_rgba.data();
	int W = app.bake_source_w, H = app.bake_source_h;
	for (int dy = 0; dy < dh; dy++) {
		float v = ((float)dy + 0.5f) / (float)dh;
		float sy = src_t + v * src_dy;
		for (int dx = 0; dx < dw; dx++) {
			uint8_t a = mask[(size_t)dy * (size_t)dw + (size_t)dx];
			if (a == 0) continue;       /* preserve fully-transparent regions */
			float u = ((float)dx + 0.5f) / (float)dw;
			float sx = src_l + u * src_dx;
			uint8_t *px = atlas + (((dy0 + dy) * atlas_w) + (dx0 + dx)) * 4;
			uint8_t r, g, b;
			TieFilmView_SampleBilinearRgb(src, W, H, sx, sy, &r, &g, &b);
			px[0] = r; px[1] = g; px[2] = b;
			px[3] = a;
		}
	}

	edit->dirty = true;
	if (!TieFilmView_PushOverrideForAtlas(app, *edit))
		return false;

	char buf[256];
	if (frame_idx >= 0) {
		std::snprintf(buf, sizeof buf,
		              "baked frame %d → atlas (%d,%d %dx%d)  "
		              "src (%.0f,%.0f %.0fx%.0f)",
		              frame_idx, dx0, dy0, dw, dh,
		              src_l, src_t, src_dx, src_dy);
	} else {
		std::snprintf(buf, sizeof buf,
		              "baked sprite (%dx%d)  src (%.0f,%.0f %.0fx%.0f)",
		              dw, dh, src_l, src_t, src_dx, src_dy);
	}
	app.bake_status = buf;
	return true;
}

/* Reset the atlas's pixels to the on-load baseline, drop the GPU
 * override so compose falls back to the on-disk KTX2. */
static void TieFilmView_ResetAtlasPixels(TieFilmViewApp &app, const std::string &asset_key) {
	auto it = app.atlas_pixels.find(asset_key);
	if (it == app.atlas_pixels.end()) return;
	if (it->second.override_tex) {
		Aeron_DestroyTexture(it->second.override_tex);
		it->second.override_tex = nullptr;
	}
	if (app.cutscene)
		TieScene2dCutscene_TextureUnoverride(app.cutscene, asset_key.c_str());
	app.atlas_pixels.erase(it);
	app.bake_status = "reset " + asset_key;
}

/* Persist the atlas's edited RGBA back to the sibling PNG. The
 * KTX2 stays stale until the user explicitly re-bakes (Bake KTX2
 * button below or `bc7enc <png> <ktx2>` from a terminal); surface
 * the hint in the status line. */
static bool TieFilmView_SaveAtlasPixels(TieFilmViewApp &app, TieFilmViewApp::AtlasPixelEdit &edit) {
	char err[256] = {0};
	if (!write_png_rgba_atomic(edit.png_path.c_str(), edit.w, edit.h,
	                            edit.rgba.data(), err, sizeof err)) {
		app.bake_status = std::string("save failed: ") + err;
		return false;
	}
	edit.baseline_rgba = edit.rgba;
	edit.dirty = false;
	app.bake_status = "saved " + edit.png_path +
	                  "  (KTX2 stale — click Bake KTX2 to re-encode)";
	return true;
}

/* Rebuild only atlas coordinates from the source ANIM. Scaling mode is inferred
 * from the existing atlas dimensions; image data is left unchanged. */
static bool TieFilmView_RegenerateYamlFromAnim(TieFilmViewApp &app, const char *res_name,
                                       const char *yaml_path,
                                       const char *lfd_basename,
                                       const char *film_name,
                                       int16_t entry_index,
                                       int cur_cel) {
	const TieLfdFile *owner = nullptr;
	const TieLfdFileEntry *e = TieLfdFileChain_Find(&app.chain, FCC_ANIM, res_name, &owner);
	if (!e || !owner) {
		app.atlas_status = std::string("ANIM '") + res_name +
		                   "' not in LFD chain — load the right --extra LFD first";
		return false;
	}

	AnimImage anim{};
	if (!decode_anim(&anim, TieLfdFile_Data(owner, e), e->size)) {
		app.atlas_status = std::string("ANIM '") + res_name + "' decode failed";
		return false;
	}

	AtlasPack pack{};
	if (!atlas_pack_compute(&anim, &pack)) {
		app.atlas_status = "atlas pack failed (empty / OOM?)";
		anim_free(&anim);
		return false;
	}

	/* Infer (scale, svga) from the in-memory SpriteAtlas dims the
	 * manifest already loaded. classic_atlas_w/h being non-zero
	 * means filmextract was run with --scale; an exact 4.5× match
	 * on both axes (within ±1 px BC7 padding) means SVGA, otherwise
	 * VGA's (9×, 10.8×). */
	int atlas_w = 0, atlas_h = 0;
	const char *yp = nullptr, *ap = nullptr;
	TieScene2dManifest *cs = TieScene2dCutscene_ManifestMut(app.cutscene);
	int frame_count = 0;
	TieScene2dRect r0{};
	if (!TieScene2dManifest_AtlasGet(cs, lfd_basename, film_name, res_name,
	                                  entry_index, cur_cel, 0, &r0,
	                                  &atlas_w, &atlas_h, &frame_count,
	                                  &yp, &ap)) {
		app.atlas_status = "could not resolve atlas to infer scale flags";
		atlas_pack_free(&pack);
		anim_free(&anim);
		return false;
	}
	bool scale = false;
	bool svga  = false;
	if (atlas_w >= pack.classic_atlas_w * 4 ||
	    atlas_h >= pack.classic_atlas_h * 4) {
		scale = true;
		/* Heuristic: VGA produces atlas_h ≈ classic_h × 10.8. SVGA
		 * is ~4.5×. Pick whichever is closer. */
		double r_vga  = (double)atlas_h / (double)pack.classic_atlas_h - 10.8;
		double r_svga = (double)atlas_h / (double)pack.classic_atlas_h - 4.5;
		if (r_vga < 0) r_vga = -r_vga;
		if (r_svga < 0) r_svga = -r_svga;
		svga = (r_svga < r_vga);
	}

	int post_w = 0, post_h = 0;
	atlas_pack_post_dims(&pack, scale, svga, &post_w, &post_h);

	char err[256] = {0};
	bool ok = atlas_emit_yaml(yaml_path, &anim, &pack,
	                          post_w, post_h, scale, svga,
	                          err, sizeof err);
	atlas_pack_free(&pack);
	anim_free(&anim);
	if (!ok) {
		app.atlas_status = std::string("regenerate failed: ") + err;
		return false;
	}

	/* Re-parse so the editor + compose path see the regenerated
	 * coords without a full manifest reload. */
	if (!TieScene2dManifest_AtlasReload(cs, lfd_basename, film_name,
	                                     res_name, entry_index, cur_cel)) {
		app.atlas_status = "regenerated yaml on disk, but reload failed — "
		                   "click Reload manifest";
		return true;
	}
	/* The in-memory rects changed — drop any per-yaml editor baseline
	 * + dirty bit so subsequent edits start from the fresh data. */
	std::string yaml_key = yaml_path;
	app.atlas_baseline.erase(yaml_key);
	app.atlas_dirty.erase(yaml_key);
	app.atlas_status = "regenerated " + yaml_key +
	                   " from source ANIM (scale=" +
	                   (scale ? (svga ? "svga" : "vga") : "none") + ")";
	return true;
}

/* Re-encode the atlas's current in-memory RGBA to BC7 KTX2 at medium
 * quality + zstd, write to the on-disk asset_path, then drop the
 * override + cache entry so the next compose pass loads the fresh
 * KTX2. Synchronous — blocks the UI thread for the duration of the
 * encode (a few seconds for a 2880×2160 atlas at MED). */
static bool TieFilmView_RebakeKtx2ForAtlas(TieFilmViewApp &app, TieFilmViewApp::AtlasPixelEdit &edit) {
	if (edit.asset_path.empty()) {
		app.bake_status = "no KTX2 path for this atlas";
		return false;
	}
	/* Filmview edits palette-colour artwork (atlases) — sRGB. */
	if (!write_ktx2_bc7_with_generated_mips(edit.asset_path.c_str(),
	                                         edit.w, edit.h,
	                                         edit.rgba.data(),
	                                         KTX2_BC7_QUALITY_MED,
	                                         KTX2_TF_SRGB,
	                                         /*zstd=*/true)) {
		app.bake_status = "KTX2 re-encode failed: " + edit.asset_path;
		return false;
	}
	/* Drop the override AND any cached KTX2 entry so the next compose
	 * load reads the fresh on-disk bytes. The override texture is
	 * filmview-owned; release it here. */
	if (app.cutscene)
		TieScene2dCutscene_TextureInvalidate(app.cutscene,
		                                edit.asset_path.c_str());
	if (edit.override_tex) {
		Aeron_DestroyTexture(edit.override_tex);
		edit.override_tex = nullptr;
	}
	app.bake_status = "re-encoded KTX2: " + edit.asset_path;
	return true;
}

static void TieFilmView_RenderCutscenePreview(TieFilmViewApp &app) {
	if (!app.remaster_enabled || !app.cutscene || !app.cutscene_target ||
	    !app.cutscene_draw_list || !app.player_loaded) return;
	AeronCommandBuffer *cmd = Aeron_AcquireCommandBuffer();
	if (!cmd) return;
	constexpr int kMaxActors = 256;
	TieScene2dActorView views[kMaxActors];
	int n = TieFilmView_BuildActorViews(app, views, kMaxActors);
	TieScene2dCutscene_PrepUploads(app.cutscene, cmd, app.lfd_basename.c_str(),
		app.player.film_name, TieFilmPlayer_DisplayedCel(&app.player), views, n);
	const float clear[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
	AeronDrawList_Begin(app.cutscene_draw_list, app.cutscene_target,
		app.cutscene_w, app.cutscene_h, AERON_DRAWLIST2D_CLEAR, clear);
	TieScene2dCutscene_RecordActors(app.cutscene, app.cutscene_draw_list,
		app.cutscene_w, app.cutscene_h, app.lfd_basename.c_str(),
		app.player.film_name, TieFilmPlayer_DisplayedCel(&app.player), views, n);
	AeronDrawList_Render(app.cutscene_draw_list, cmd);
	if (!Aeron_SubmitCommandBuffer(cmd))
		Aeron_RequestFatalRendererError("filmview cutscene rendering");
}

/* Display geometry. VGA mode: 320×200 source pixels are non-square,
   intended monitor aspect 4:3 — each pixel is 1.2× taller in display
   space than its source unit (PIXEL_ASPECT_Y_VGA = 1.2). SVGA mode:
   640×480 source is already 4:3 with square pixels — no correction
   needed (pixel_aspect_y = 1.0). `zoom` is the X scale; Y scale is
   `zoom * pixel_aspect_y`. */
static constexpr float PIXEL_ASPECT_Y_VGA  = 1.2f;
static constexpr float PIXEL_ASPECT_Y_SVGA = 1.0f;

static inline float TieFilmView_PixelAspectY(const TieFilmViewApp &app) {
	return app.svga_mode ? PIXEL_ASPECT_Y_SVGA : PIXEL_ASPECT_Y_VGA;
}

static inline ImVec2 TieFilmView_DisplaySize(const TieFilmViewApp &app, int zoom) {
	return ImVec2((float)app.fb_w * (float)zoom,
	              (float)app.fb_h * (float)zoom * TieFilmView_PixelAspectY(app));
}

/* (Re)allocate the framebuffer display target if the prescale factor changed. Picks a
 * prescale that brackets the current display height — in VGA mode
 * the final ImGui::Image stretch is then ≤1.2× in one axis, keeping
 * pixel boundaries effectively sharp; in SVGA mode the prescale
 * lands at the exact display size with no residual stretch. */
static void TieFilmView_EnsureScaledTexture(TieFilmViewApp &app) {
	ImVec2 disp = TieFilmView_DisplaySize(app, app.zoom);
	int prescale = (int)((disp.y + (float)app.fb_h - 1) / (float)app.fb_h);
	if (prescale < 1) prescale = 1;
	if (prescale == app.fb_scaled_factor && app.fb_scaled_target) return;

	if (app.fb_scaled_target)
		Aeron_DestroyRenderTarget(app.fb_scaled_target);
	AeronRenderTargetDesc target_desc {
		.width = app.fb_w * prescale,
		.height = app.fb_h * prescale,
		.format = AERON_TEXTURE_FORMAT_RGBA8_UNORM,
		.debug_name = "filmview classic framebuffer",
	};
	app.fb_scaled_target = Aeron_CreateRenderTarget(&target_desc);
	if (app.fb_scaled_target) {
		app.fb_scaled_factor = prescale;
		app.fb_dirty = true;
	}
}

static void TieFilmView_UpdateScaledTexture(TieFilmViewApp &app) {
	if (!app.fb_dirty || !app.fb_scaled_target) return;
	const float clear[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
	AeronPixelLayerDesc layer{};
	layer.frame = (AeronPixelFrameView) {
		.pixels = app.rgba.data(),
		.width = app.fb_w,
		.height = app.fb_h,
		.pitch = app.fb_w * 4,
		.format = AERON_PIXEL_FORMAT_RGBA8888,
		.color_space = AERON_COLOR_SPACE_SRGB,
		.generation = app.fb_generation,
	};
	layer.logical_rect = (AeronRectI) {
		.x = 0, .y = 0,
		.width = app.fb_w * app.fb_scaled_factor,
		.height = app.fb_h * app.fb_scaled_factor,
	};
	layer.sampling = AERON_PIXEL_SAMPLING_NEAREST;
	if (Aeron_ComposePixelLayerToRenderTarget(app.fb_scaled_target, &layer, 1, clear))
		app.fb_dirty = false;
}

static void TieFilmView_UnloadFilm(TieFilmViewApp &app) {
	if (app.player_loaded) {
		TieFilmPlayer_Free(&app.player);
		app.player_loaded = false;
	}
	app.selected_actor = -1;
}

/* Translate the original Watcom backslashed relative path
 * (e.g. "astream\\os1-v3.wrk") into a host-resolvable path under
 * `app.stream_dir`. Returns "" if the input is empty or stream_dir
 * is unset. POSIX hosts need / separators; Windows tolerates either.
 *
 * Strips the leading "astream\\" or "stream\\" component since
 * stream_dir already points at the equivalent directory the user
 * passed on the command line. */
static std::string TieFilmView_ResolveStreamPath(const TieFilmViewApp &app, const char *rel) {
	if (!rel || !*rel)              return std::string();
	if (app.stream_dir.empty())     return std::string();

	const char *tail = rel;
	const char *slash = std::strpbrk(rel, "\\/");
	if (slash) tail = slash + 1;

	std::string out = app.stream_dir;
	if (!out.empty() && out.back() != '/' && out.back() != '\\')
		out.push_back('/');
	out.append(tail);
	/* Normalise the separator inside the appended tail too (some WRK
	 * paths use double slashes after the prefix in the original). */
	for (size_t i = app.stream_dir.size(); i < out.size(); i++)
		if (out[i] == '\\') out[i] = '/';
	return out;
}

/* Decide which WRK to bind for the just-loaded film. Walks the layer
 * priority described on TieFilmViewApp::stream_overrides. Returns "" when no
 * binding applies. The caller then invokes player_bind_stream and
 * surfaces success/failure in the UI. */
static std::string TieFilmView_PickStreamPathFor(const TieFilmViewApp &app,
                                        const char *film_name) {
	if (!film_name || !*film_name) return std::string();

	auto it = app.stream_overrides.find(film_name);
	if (it != app.stream_overrides.end() && !it->second.empty())
		return it->second;

	if (!app.stream_default_path.empty())
		return app.stream_default_path;

	if (!app.stream_dir.empty()) {
		TieFilmPlay1DataSet ds = app.stream_dir_is_retail
		                  ? PLAY1_DATA_RETAIL
		                  : PLAY1_DATA_DEMO;
		const char *rel = TieFilmPlay1Streams_Play1StreamForFilm(film_name, ds);
		if (rel) return TieFilmView_ResolveStreamPath(app, rel);
	}

	return std::string();
}

/* Apply whatever bind layer hits for the currently-loaded film.
 * Idempotent — safe to call after load_film_at. */
static void TieFilmView_ApplyStreamBinding(TieFilmViewApp &app) {
	app.current_stream_path.clear();
	app.stream_error.clear();
	if (!app.player_loaded) return;

	int idx = TieFilmPlayer_FindStreamActor(&app.player);
	if (idx < 0) return;   /* film has no stream actor */

	std::string path = TieFilmView_PickStreamPathFor(app, app.player.film_name);
	if (path.empty()) {
		/* Drop any previously-bound session — the priority layers
		 * resolved to "no binding" (e.g. after Unbind clicked). */
		TieFilmPlayer_UnbindStream(&app.player, idx);
		return;
	}

	if (!TieFilmPlayer_BindStream(&app.player, idx, path.c_str())) {
		app.stream_error = "Failed to open stream: " + path;
		std::fprintf(stderr, "filmview: %s\n", app.stream_error.c_str());
		return;
	}
	app.current_stream_path = path;
	/* Re-rewind so cur_frame=0 of the stream lines up with cel-0 of
	 * the FILM, then re-step to whatever cel was displayed. The
	 * fresh load is at cel 0 but be defensive in case bind happens
	 * after the first user step. */
	int displayed = TieFilmPlayer_DisplayedCel(&app.player);
	TieFilmPlayer_Rewind(&app.player);
	if (displayed > 0)
		TieFilmPlayer_Seek(&app.player, displayed);
}

static bool TieFilmView_LoadFilmAt(TieFilmViewApp &app, int film_index) {
	if (film_index < 0 || film_index >= (int)app.film_entry_indices.size())
		return false;
	TieFilmView_UnloadFilm(app);
	const TieLfdFileEntry *e =
	    &app.primary.entries[app.film_entry_indices[(size_t)film_index]];
	if (!TieFilmPlayer_InitWithDims(&app.player, &app.chain, &app.primary, e,
	                            app.fb_w, app.fb_h)) {
		TieFilmView_ShowError("Failed to load FILM '%.*s'", 8, e->name);
		return false;
	}
	app.player_loaded = true;
	app.current_film = film_index;
	app.play_accum_s = 0.0;

	/* Stash any missing-resource diagnostics so the GUI can flag them
	   loudly. The same warnings already went to stderr at player_init. */
	app.missing_resources.clear();
	for (int i = 0; i < app.player.object_count; i++) {
		const TieFilmPlayerObject *o = &app.player.objects[i];
		if (o->resource_loaded) continue;
		char buf[48];
		std::snprintf(buf, sizeof buf, "%s '%s'",
		              TieFilmView_FourccString(o->res_type).c_str(), o->res_name);
		app.missing_resources.push_back(buf);
	}

	/* Resolve and bind the stream-actor WRK (if any). Matches the
	 * engine: play1_film_Callback fires once at film creation, after
	 * cel-0 records have populated var1. */
	TieFilmView_ApplyStreamBinding(app);

	TieFilmView_RecomposeIntoTexture(app);
	return true;
}

/* Rebuild the TieLfdFileChain after the primary or extras vector changed.
   Storage is owned by `app`; pointers are reseated on every call. */
static void TieFilmView_RebuildChain(TieFilmViewApp &app) {
	app.chain_storage.clear();
	app.chain_storage.push_back(&app.primary);
	for (auto &lfd : app.extras)
		app.chain_storage.push_back(&lfd);
	app.chain.files = app.chain_storage.data();
	app.chain.count = (int)app.chain_storage.size();
}

/* Repopulate `film_entry_indices` from the current primary. */
static void TieFilmView_RescanFilms(TieFilmViewApp &app) {
	app.film_entry_indices.clear();
	for (uint32_t i = 0; i < app.primary.count; i++)
		if (app.primary.entries[i].type == FCC_FILM)
			app.film_entry_indices.push_back(i);
	app.current_film = -1;
}

/* Replace the primary LFD with the file at `path`. On failure the
   current state is left untouched (new file is opened first; old is
   only torn down once the new one parses cleanly). Extras are kept —
   typical use is "swap mission LFD, keep EMPIRE.LFD". */
static bool TieFilmView_OpenPrimary(TieFilmViewApp &app, const std::string &path) {
	TieLfdFile staged{};
	char err[512] = {0};
	if (!TieLfdFile_Open(&staged, path.c_str(), err, sizeof err)) {
		TieFilmView_ShowError("%s", err);
		return false;
	}

	TieFilmView_UnloadFilm(app);
	TieLfdFile_Close(&app.primary);
	app.primary = staged;
	app.primary_path = path;
	app.lfd_basename = TieFilmView_LfdBasenameFrom(path.c_str());
	TieFilmView_RebuildChain(app);
	TieFilmView_RescanFilms(app);

	if (app.film_entry_indices.empty()) {
		app.status_msg = path + ": no FILM resources";
		app.missing_resources.clear();
		/* Viewport panel falls back to its "select a FILM" placeholder
		   when player_loaded is false, so the stale framebuffer contents
		   from the previously-loaded film are never sampled. No blit
		   needed. */
	} else {
		TieFilmView_LoadFilmAt(app, 0);
		app.status_msg = "Loaded " + path;
	}
	return true;
}

/* Append `path` to extras and re-link the chain. Films aren't reloaded;
   the new resources become visible when the user steps the next cel
   (or re-selects the current FILM). */
static bool TieFilmView_AddExtra(TieFilmViewApp &app, const std::string &path) {
	TieLfdFile staged{};
	char err[512] = {0};
	if (!TieLfdFile_Open(&staged, path.c_str(), err, sizeof err)) {
		TieFilmView_ShowError("%s", err);
		return false;
	}
	app.extras.push_back(staged);
	TieFilmView_RebuildChain(app);
	/* Reload the current film so any previously-missing resources
	   that the new LFD provides resolve immediately, instead of
	   waiting for the user to re-select the FILM. */
	if (app.current_film >= 0)
		TieFilmView_LoadFilmAt(app, app.current_film);
	app.status_msg = "Added extra: " + path;
	return true;
}

/* SDL3 file-dialog callback. May run on a worker thread, so it only
   stashes the pick under the application mutex. */
static void TieFilmView_DialogPickCb(void *userdata, const char * const *filelist,
                           int /*filter*/) {
	TieFilmViewApp *app = static_cast<TieFilmViewApp *>(userdata);
	if (!filelist || !filelist[0]) return;        /* error or canceled */
	std::lock_guard<std::mutex> lock(app->file_mutex);
	app->pending_path = filelist[0];
}

static void TieFilmView_OpenDialogFor(TieFilmViewApp &app, TieFilmViewApp::FileOp op) {
	/* Mark the requested op atomically; the path arrives via
	   dialog_pick_cb on the same mutex. */
	{
		std::lock_guard<std::mutex> lock(app.file_mutex);
		app.pending_op = op;
		app.pending_path.clear();
	}

	static const SDL_DialogFileFilter lfd_filters[] = {
		{ "LucasArts LFD bundles", "lfd" },
		{ "All files",             "*"   },
	};
	static const SDL_DialogFileFilter wrk_filters[] = {
		{ "TIE FMV streams", "wrk" },
		{ "All files",       "*"   },
	};
	static const SDL_DialogFileFilter png_filters[] = {
		{ "PNG images", "png" },
		{ "All files",  "*"   },
	};
	const SDL_DialogFileFilter *filters = lfd_filters;
	int num_filters = (int)(sizeof lfd_filters / sizeof lfd_filters[0]);
	if (op == TieFilmViewApp::FileOp::BindStream) {
		filters = wrk_filters;
		num_filters = (int)(sizeof wrk_filters / sizeof wrk_filters[0]);
	} else if (op == TieFilmViewApp::FileOp::BindBakeSource) {
		filters = png_filters;
		num_filters = (int)(sizeof png_filters / sizeof png_filters[0]);
	}

	/* Default directory:
	 *   - WRK picks (BindStream): user's --stream-dir if set;
	 *   - PNG picks (BindBakeSource): the dir of the previous pick
	 *     this session, falling back to the primary LFD's dir;
	 *   - LFD picks: primary LFD's dir.
	 * The bake-source case lives apart because the user typically
	 * keeps reference renders in a separate tree from the LFDs. */
	std::string default_dir;
	if (op == TieFilmViewApp::FileOp::BindStream && !app.stream_dir.empty()) {
		default_dir = app.stream_dir;
	} else if (op == TieFilmViewApp::FileOp::BindBakeSource &&
	           !app.bake_source_dir.empty()) {
		default_dir = app.bake_source_dir;
	} else {
		default_dir = app.primary_path;
		auto slash = default_dir.find_last_of("/\\");
		if (slash != std::string::npos)
			default_dir.resize(slash);
	}
	/* SDL_ShowOpenFileDialog's `default_location` is treated as a file
	 * path unless it ends with a separator, in which case it's used as
	 * a directory. Append one so the dialog opens *inside* default_dir
	 * rather than its parent. */
	if (!default_dir.empty() &&
	    default_dir.back() != '/' && default_dir.back() != '\\')
		default_dir.push_back('/');
	SDL_ShowOpenFileDialog(TieFilmView_DialogPickCb, &app, nullptr,
	                       filters, num_filters,
	                       default_dir.empty() ? nullptr
	                                           : default_dir.c_str(),
	                       /*allow_many=*/false);
}

/* Apply a bake-source pick: load the PNG into bake_source_rgba.
 * Warns (but does not refuse) if dims aren't 3840×2160 — the
 * pillarbox xform assumes that aspect, but the user may legitimately
 * be working with a custom-sized canvas; surface the size in the
 * status line so they can sanity-check. */
static void TieFilmView_BindBakeSource(TieFilmViewApp &app, const std::string &path) {
	if (path.empty()) return;
	uint8_t *raw = nullptr;
	int w = 0, h = 0;
	char err[256] = {0};
	if (!read_png_rgba(path.c_str(), &raw, &w, &h, err, sizeof err)) {
		app.bake_status = std::string("source PNG failed: ") + err;
		return;
	}
	app.bake_source_path = path;
	app.bake_source_w = w;
	app.bake_source_h = h;
	app.bake_source_rgba.assign(raw, raw + (size_t)w * (size_t)h * 4u);
	free(raw);
	/* Stash the parent dir so the next "Pick source PNG…" reopens
	 * here. Trim everything from the last separator on. */
	{
		auto slash = path.find_last_of("/\\");
		app.bake_source_dir = (slash == std::string::npos)
		                       ? std::string()
		                       : path.substr(0, slash);
	}
	if (w != 3840 || h != 2160) {
		char buf[256];
		std::snprintf(buf, sizeof buf,
		              "source bound %d×%d (expected 3840×2160; xform "
		              "still uses 4:3 pillarbox math against this dim)",
		              w, h);
		app.bake_status = buf;
	} else {
		app.bake_status = "source bound: " + path;
	}
}

/* Apply a runtime [Bind…] result: store as a per-film override and
 * re-bind the active film. Stays sticky across re-selects of the
 * same film (overrides_map persists for the session). */
static void TieFilmView_BindStreamRuntime(TieFilmViewApp &app, const std::string &path) {
	if (!app.player_loaded || path.empty()) return;
	int idx = TieFilmPlayer_FindStreamActor(&app.player);
	if (idx < 0) {
		app.stream_error = "Active film has no stream actor (CUST/var1=123)";
		return;
	}
	app.stream_overrides[app.player.film_name] = path;
	TieFilmView_ApplyStreamBinding(app);
	if (!app.current_stream_path.empty()) {
		app.status_msg = "Bound stream: " + path;
		TieFilmView_RecomposeIntoTexture(app);
	}
}

/* Drain a queued dialog pick. Called once per frame from the main loop
   before NewFrame so any state mutation lands before ImGui paints. */
static void TieFilmView_DrainPendingFileOp(TieFilmViewApp &app) {
	TieFilmViewApp::FileOp op = TieFilmViewApp::FileOp::None;
	std::string path;
	{
		std::lock_guard<std::mutex> lock(app.file_mutex);
		if (!app.pending_path.empty() && app.pending_op != TieFilmViewApp::FileOp::None) {
			op = app.pending_op;
			path = std::move(app.pending_path);
			app.pending_op = TieFilmViewApp::FileOp::None;
			app.pending_path.clear();
		}
	}

	switch (op) {
	case TieFilmViewApp::FileOp::OpenPrimary:    TieFilmView_OpenPrimary(app, path);        break;
	case TieFilmViewApp::FileOp::AddExtra:       TieFilmView_AddExtra(app, path);           break;
	case TieFilmViewApp::FileOp::BindStream:     TieFilmView_BindStreamRuntime(app, path); break;
	case TieFilmViewApp::FileOp::BindBakeSource: TieFilmView_BindBakeSource(app, path);    break;
	case TieFilmViewApp::FileOp::None:                                            break;
	}
}

/* Save the *current* composited frame to PNG at the native FB
   resolution (320×200 in VGA mode, 640×480 in SVGA mode).
   Re-renders against the current palette and indexed buffer; does NOT
   include any GUI overlays. */
static bool TieFilmView_SaveScreenshot(TieFilmViewApp &app) {
	if (!app.player_loaded) return false;
	TieFilmPlayer_Composite(&app.player);
	std::vector<uint8_t> rgba((size_t)app.fb_w * (size_t)app.fb_h * 4u);
	TieFilmPlayer_RenderRgba(&app.player, rgba.data());

	char filename[512];
	std::snprintf(filename, sizeof filename,
	              "%s/%s_cel%03d.png",
	              app.screenshot_dir.c_str(),
	              app.player.film_name,
	              TieFilmPlayer_DisplayedCel(&app.player));
	if (!write_png_rgba(filename, app.fb_w, app.fb_h, rgba.data())) {
		TieFilmView_ShowError("Could not write %s", filename);
		return false;
	}
	app.last_screenshot = filename;
	return true;
}

/* ---------- UI panels ---------- */

/* Top main menu bar. The dialog itself is fired here but its result
   arrives asynchronously through dialog_pick_cb; processing happens in
   drain_pending_file_op at the start of the next frame. `quit_req` is
   the main-loop quit flag — set on File→Quit. */
static void TieFilmView_UiMainMenu(TieFilmViewApp &app, bool &quit_req) {
	if (!ImGui::BeginMainMenuBar()) return;
	if (ImGui::BeginMenu("File")) {
		if (ImGui::MenuItem("Open LFD...", "Ctrl+O"))
			TieFilmView_OpenDialogFor(app, TieFilmViewApp::FileOp::OpenPrimary);
		if (ImGui::MenuItem("Add Extra LFD..."))
			TieFilmView_OpenDialogFor(app, TieFilmViewApp::FileOp::AddExtra);
		ImGui::Separator();
		if (ImGui::MenuItem("Quit", "Ctrl+Q"))
			quit_req = true;
		ImGui::EndMenu();
	}
	/* Right-side status: chain summary. Useful when juggling multiple
	   extras — the main viewport doesn't otherwise list them. */
	{
		char buf[256];
		std::snprintf(buf, sizeof buf,
		    "primary: %s   |   extras: %d",
		    app.lfd_basename.empty() ? "—" : app.lfd_basename.c_str(),
		    (int)app.extras.size());
		float w = ImGui::CalcTextSize(buf).x +
		          ImGui::GetStyle().ItemSpacing.x * 2.0f;
		ImGui::SameLine(ImGui::GetWindowWidth() - w);
		ImGui::TextDisabled("%s", buf);
	}
	ImGui::EndMainMenuBar();
}

static void TieFilmView_UiFilmsWindow(TieFilmViewApp &app) {
	if (!ImGui::Begin("Films")) { ImGui::End(); return; }

	if (!app.status_msg.empty()) {
		ImGui::TextColored(ImVec4(0.6f, 0.9f, 0.6f, 1.0f),
		                   "%s", app.status_msg.c_str());
		ImGui::Separator();
	}

	ImGui::Text("%s  (%d FILM%s)",
	            app.lfd_basename.empty() ? "(none)"
	                                     : app.lfd_basename.c_str(),
	            (int)app.film_entry_indices.size(),
	            app.film_entry_indices.size() == 1 ? "" : "s");
	ImGui::Separator();

	for (size_t i = 0; i < app.film_entry_indices.size(); i++) {
		const TieLfdFileEntry *e =
		    &app.primary.entries[app.film_entry_indices[i]];
		char label[32];
		std::snprintf(label, sizeof label, "%s##film%zu", e->name, i);
		bool selected = ((int)i == app.current_film);
		if (ImGui::Selectable(label, selected))
			TieFilmView_LoadFilmAt(app, (int)i);
	}

	/* Stream-actor status. Only relevant when the active film has a
	 * CUST/var1=123 actor — for everything else the section just
	 * stays hidden, no nag. */
	if (app.player_loaded &&
	    TieFilmPlayer_FindStreamActor(&app.player) >= 0) {
		ImGui::Separator();
		ImGui::TextDisabled("Stream actor (FMV)");

		if (!app.current_stream_path.empty()) {
			ImGui::TextColored(ImVec4(0.6f, 0.9f, 0.6f, 1.0f),
			                   "Bound: %s",
			                   app.current_stream_path.c_str());
		} else if (!app.stream_error.empty()) {
			ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f),
			                   "%s", app.stream_error.c_str());
		} else {
			ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.4f, 1.0f),
			                   "Unbound — pass --stream <path> or "
			                   "click [Bind .WRK…]");
		}
		if (ImGui::Button("Bind .WRK..."))
			TieFilmView_OpenDialogFor(app, TieFilmViewApp::FileOp::BindStream);
		ImGui::SameLine();
		if (ImGui::Button("Unbind") &&
		    !app.current_stream_path.empty()) {
			/* Drop the per-film override and re-apply: falls back
			 * through the priority layers, which often resolves to
			 * "no binding" (clearing the screen of stream pixels on
			 * the next composite). */
			app.stream_overrides.erase(app.player.film_name);
			TieFilmView_ApplyStreamBinding(app);
			TieFilmView_RecomposeIntoTexture(app);
		}
	}

	ImGui::End();
}

static void TieFilmView_UiControlsWindow(TieFilmViewApp &app) {
	if (!ImGui::Begin("Controls")) { ImGui::End(); return; }

	if (!app.player_loaded) {
		ImGui::TextDisabled("(no film loaded)");
		ImGui::End();
		return;
	}

	int displayed = TieFilmPlayer_DisplayedCel(&app.player);
	int total = TieFilmPlayer_TotalCels(&app.player);

	ImGui::Text("FILM: %s", app.player.film_name);
	ImGui::Text("Cel:  %d / %d", displayed, total > 0 ? total - 1 : 0);

	if (ImGui::Button("|<##first")) TieFilmPlayer_Seek(&app.player, 0);
	ImGui::SameLine();
	if (ImGui::Button("<##prev"))
		TieFilmPlayer_Seek(&app.player, displayed - 1);
	ImGui::SameLine();
	if (ImGui::Button(app.playing ? "Pause" : "Play"))
		app.playing = !app.playing;
	ImGui::SameLine();
	if (ImGui::Button(">##next"))
		TieFilmPlayer_Seek(&app.player, displayed + 1);
	ImGui::SameLine();
	if (ImGui::Button(">|##last"))
		TieFilmPlayer_Seek(&app.player, total - 1);

	int scrub = displayed;
	int max_cel = total > 0 ? total - 1 : 0;
	if (ImGui::SliderInt("##scrub", &scrub, 0, max_cel)) {
		TieFilmPlayer_Seek(&app.player, scrub);
		app.play_accum_s = 0.0;
	}

	ImGui::SliderFloat("FPS", &app.fps, 1.0f, 60.0f, "%.1f");
	ImGui::SliderInt("Zoom", &app.zoom, 1, 6, "%dx");
	ImGui::Checkbox("Show actor bbox",  &app.show_bbox);
	ImGui::SameLine();
	ImGui::Checkbox("Show clip rect",   &app.show_clip);
	ImGui::Checkbox("Show coord labels", &app.show_labels);

	ImGui::Separator();
	if (ImGui::Button("Screenshot PNG"))
		TieFilmView_SaveScreenshot(app);
	ImGui::SameLine();
	ImGui::TextDisabled("→ %s/", app.screenshot_dir.c_str());
	if (!app.last_screenshot.empty())
		ImGui::TextWrapped("Last: %s", app.last_screenshot.c_str());

	if (displayed < 0 || displayed != TieFilmPlayer_DisplayedCel(&app.player))
		TieFilmView_RecomposeIntoTexture(app);

	ImGui::End();
}

static void TieFilmView_DrawOverlays(const TieFilmViewApp &app, ImVec2 origin,
                          float scale_x, float scale_y) {
	if (!app.player_loaded) return;
	ImDrawList *dl = ImGui::GetWindowDrawList();

	for (int i = 0; i < app.player.object_count; i++) {
		const TieFilmPlayerObject *o = &app.player.objects[i];
		if (o->type_code != FILM_TC_ACTOR) continue;
		if (o->gui_hidden) continue;
		if (!o->show && !o->force_show) continue;
		if (o->res_type == FCC_CUST) continue;

		int l, t, r, b;
		if (!TieFilmPlayer_ActorScreenRect(o, &l, &t, &r, &b)) continue;

		bool is_sel = (i == app.selected_actor);
		ImU32 col_bbox = is_sel ? IM_COL32(255, 220, 80, 255)
		                        : IM_COL32(120, 200, 255, 220);

		if (app.show_bbox) {
			ImVec2 p0(origin.x + l * scale_x, origin.y + t * scale_y);
			ImVec2 p1(origin.x + r * scale_x, origin.y + b * scale_y);
			dl->AddRect(p0, p1, col_bbox, 0.0f, 0, is_sel ? 2.0f : 1.0f);
		}

		if (app.show_clip && (o->frame_l != 0 || o->frame_t != 0 ||
		                      o->frame_r != app.fb_w ||
		                      o->frame_b != app.fb_h)) {
			ImVec2 c0(origin.x + o->frame_l * scale_x, origin.y + o->frame_t * scale_y);
			ImVec2 c1(origin.x + o->frame_r * scale_x, origin.y + o->frame_b * scale_y);
			dl->AddRect(c0, c1, IM_COL32(255, 80, 80, 200),
			            0.0f, 0, 1.0f);
		}

		if (app.show_labels) {
			/* Show both numbers — they track different things:
			    scr=(l,t)   on-screen top-left after bbox + offset
			                + flip mirror; this is where the eye
			                actually sees the actor.
			    ofs=(x,y)   engine-internal offset (FCMD_ACTOR_POS,
			                ticked by FCMD_ACTOR_VEL). For ANIMs whose
			                visible motion comes from per-frame bboxes
			                (e.g. city1_f's shuttle2: POS set once,
			                STATEV ticks the frame), ofs stays
			                constant while scr moves — both are
			                correct, they just measure different
			                things. */
			char label[80];
			std::snprintf(label, sizeof label,
			              "%s  scr=(%d,%d)  ofs=(%d,%d)",
			              o->res_name, l, t, o->x, o->y);
			ImVec2 lp(origin.x + l * scale_x, origin.y + t * scale_y - 14.0f);
			dl->AddText(ImVec2(lp.x + 1, lp.y + 1),
			            IM_COL32(0, 0, 0, 200), label);
			dl->AddText(lp, col_bbox, label);
		}
	}
}

static void TieFilmView_UiViewportWindow(TieFilmViewApp &app) {
	ImVec2 disp_4_3 = TieFilmView_DisplaySize(app, app.zoom);
	/* In remaster mode the cutscene RT is 16:9 (1920x1080); the 4:3
	   display rect would squish it horizontally by 4/3. Use a wider
	   rect with the same height so the 4:3 sub-region inside the
	   16:9 image lands at the same scale as the un-remastered view
	   (lets the asset team eyeball alignment between the two modes
	   directly). */
	ImVec2 disp = (app.remaster_enabled && app.cutscene_target)
	              ? ImVec2(disp_4_3.y * 16.0f / 9.0f, disp_4_3.y)
	              : disp_4_3;
	/* Min size matches the image footprint plus the headroom strip we
	   add below for top-row actor labels and the readout line. */
	ImGui::SetNextWindowSizeConstraints(
	    ImVec2(disp.x + 20.0f, disp.y + 80.0f), ImVec2(FLT_MAX, FLT_MAX));
	if (!ImGui::Begin("Viewport")) { ImGui::End(); return; }

	if (!app.player_loaded || !app.fb_scaled_target) {
		ImGui::TextDisabled("Select a FILM in the Films window.");
		ImGui::End();
		return;
	}

	/* Mode toggle row — only shown when a remaster compositor was
	   stood up (--remaster). Reload re-parses every manifest under
	   the remaster root, picking up edits without restart. */
	if (app.cutscene && app.cutscene_target) {
		ImGui::Checkbox("Remastered", &app.remaster_enabled);
		ImGui::SameLine();
		/* Reload re-parses every manifest under the remaster root,
		 * picking up filesystem edits without restart. Held back when
		 * the atlas-rect editor has unsaved in-memory edits — the
		 * reload would silently discard them. The user must Save or
		 * explicitly Discard first. */
		bool any_dirty = false;
		for (const auto &kv : app.atlas_dirty)
			if (kv.second) { any_dirty = true; break; }
		ImGui::BeginDisabled(any_dirty);
		if (ImGui::SmallButton("Reload manifest")) {
			/* Free any pixel-edit override textures BEFORE shutting
			 * down the cutscene compositor — TieScene2dCutscene_Shutdown
			 * destroys the assets cache, which holds borrowed
			 * pointers to those textures. After TieScene2dCutscene_Init
			 * returns a fresh compositor, the override map is empty
			 * by construction. */
			TieFilmView_ClearAllAtlasOverrides(app);
			TieScene2dCutscene_Shutdown(app.cutscene);
			app.cutscene = TieScene2dCutscene_Init(app.remaster_dir.c_str(), nullptr);
			app.atlas_baseline.clear();
			app.atlas_dirty.clear();
			app.atlas_status.clear();
		}
		ImGui::EndDisabled();
		if (any_dirty) {
			ImGui::SameLine();
			if (ImGui::SmallButton("Discard atlas edits")) {
				TieFilmView_ClearAllAtlasOverrides(app);
				TieScene2dCutscene_Shutdown(app.cutscene);
				app.cutscene = TieScene2dCutscene_Init(app.remaster_dir.c_str(), nullptr);
				app.atlas_baseline.clear();
				app.atlas_dirty.clear();
				app.atlas_status = "discarded unsaved atlas edits";
			}
		}
		ImGui::SameLine();
		ImGui::TextDisabled("(Tab toggles)");
		if (app.remaster_enabled &&
		    !TieScene2dCutscene_HasCompleteBundle(app.cutscene,
		                                       app.lfd_basename.c_str(),
		                                       app.player.film_name)) {
			ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.4f, 1.0f),
			    "no manifest bundle for %s/%s — preview will be empty",
			    app.lfd_basename.c_str(), app.player.film_name);
		}
	}

	/* Reserve just enough headroom above the framebuffer for one line
	   of label text, so coord labels for actors at screen y≈0 aren't
	   clipped. GetTextLineHeight matches the label offset used in
	   draw_overlays without the extra inter-line spacing. */
	ImGui::Dummy(ImVec2(1.0f, ImGui::GetTextLineHeight()));

	float scale_x = (float)app.zoom;
	float scale_y = scale_x * TieFilmView_PixelAspectY(app);
	ImVec2 cursor = ImGui::GetCursorScreenPos();
	/* Display either the original VGA composite or the 4K remaster
	   preview. The overlay origin coincides with the top-left of the
	   4:3 sub-region: that's the cursor for the un-remastered view,
	   shifted right by the pillarbox width for 16:9 remaster. */
	AeronTexture *display_tex = (app.remaster_enabled && app.cutscene_target)
	    ? Aeron_RenderTargetGetTexture(app.cutscene_target)
	    : Aeron_RenderTargetGetTexture(app.fb_scaled_target);
	Aeron_DebugImage(display_tex, disp.x, disp.y);

	/* Click-to-select on the viewport image. ImGui treats the Image
	 * as the most-recent item, so IsItemHovered() refers to it. The
	 * click point is converted to classic (FB) coordinates using the
	 * same overlay_origin + scale the bbox overlay draws against, so
	 * a click on the visible bbox reliably picks that actor.
	 *
	 * Pick rule for overlapping actors: select the one drawn topmost.
	 * Composite order is back-to-front in zplane DESC, with same-z
	 * actors broken by entry_index ASC (see player_composite). The
	 * topmost is therefore the actor whose `(zplane, -entry_index)`
	 * pair compares smallest among hit actors — i.e. lowest zplane,
	 * tie-broken by highest entry_index. Clicking on an empty area
	 * deselects (matches "click to clear" UX).
	 *
	 * The hit list mirrors draw_overlays / player_composite
	 * eligibility: ACTOR-type only, show + !gui_hidden, non-CUST. */
	ImVec2 overlay_origin = cursor;
	if (app.remaster_enabled && app.cutscene_target)
		overlay_origin.x += (disp.x - disp_4_3.x) * 0.5f;

	if (ImGui::IsItemHovered() &&
	    ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
		ImVec2 mp = ImGui::GetMousePos();
		float cx = (mp.x - overlay_origin.x) / scale_x;
		float cy = (mp.y - overlay_origin.y) / scale_y;
		int picked    = -1;
		int best_z    = INT_MAX;
		int best_ent  = -1;
		for (int i = 0; i < app.player.object_count; i++) {
			const TieFilmPlayerObject *o = &app.player.objects[i];
			if (o->type_code != FILM_TC_ACTOR) continue;
			if (o->gui_hidden)                 continue;
			if (!o->show && !o->force_show)    continue;
			if (o->res_type == FCC_CUST)       continue;
			int l, t, r, b;
			if (!TieFilmPlayer_ActorScreenRect(o, &l, &t, &r, &b)) continue;
			if (cx < (float)l || cx >= (float)r ||
			    cy < (float)t || cy >= (float)b) continue;
			/* Topmost: lowest zplane wins; tie-break highest entry_index. */
			if (o->zplane < best_z ||
			    (o->zplane == best_z &&
			     (int)o->entry_index > best_ent)) {
				picked   = i;
				best_z   = o->zplane;
				best_ent = (int)o->entry_index;
			}
		}
		app.selected_actor = picked;
	}

	TieFilmView_DrawOverlays(app, overlay_origin, scale_x, scale_y);

	if (app.remaster_enabled && app.cutscene_target) {
		ImGui::Text("remaster preview %dx%d (16:9) → display %dx%d "
		            "(zoom %dx)",
		            app.cutscene_w, app.cutscene_h,
		            (int)disp.x, (int)disp.y, app.zoom);
	} else if (app.svga_mode) {
		ImGui::Text("Native %dx%d (square px, SVGA) → display %dx%d "
		            "(zoom %dx, prescale %dx)",
		            app.fb_w, app.fb_h,
		            (int)disp.x, (int)disp.y, app.zoom,
		            app.fb_scaled_factor);
	} else {
		ImGui::Text("Native %dx%d (1.2:1 px, VGA) → 4:3 display %dx%d "
		            "(zoom %dx, prescale %dx)",
		            app.fb_w, app.fb_h,
		            (int)disp.x, (int)disp.y, app.zoom,
		            app.fb_scaled_factor);
	}
	ImGui::End();
}

static void TieFilmView_UiActorsWindow(TieFilmViewApp &app) {
	if (!ImGui::Begin("Actors")) { ImGui::End(); return; }

	if (!app.player_loaded) {
		ImGui::TextDisabled("(no film loaded)");
		ImGui::End();
		return;
	}

	ImGui::TextDisabled("Toggle ☑ to render an actor; ✓ shows engine show flag.");
	ImGui::TextDisabled(
	    "screen = bbox + ofs (HFLIP/VFLIP mirror). bbox is fixed in the "
	    "DELT/RAW resource or per-frame in ANIM; ofs is FCMD_ACTOR_POS "
	    "ticked by FCMD_ACTOR_VEL.");

	if (!app.missing_resources.empty()) {
		ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 120, 80, 255));
		ImGui::TextWrapped(
		    "%zu resource(s) NOT FOUND in the LFD chain — these actors "
		    "composite to nothing. Add the LFD that owns them via "
		    "--extra <file.lfd> (typically EMPIRE.LFD for shared assets):",
		    app.missing_resources.size());
		for (const auto &m : app.missing_resources)
			ImGui::BulletText("%s", m.c_str());
		ImGui::PopStyleColor();
		ImGui::Separator();
	}

	ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
	                        ImGuiTableFlags_Resizable;
	const bool have_remaster = (app.cutscene != nullptr);
	const int n_cols = have_remaster ? 11 : 10;
	if (ImGui::BeginTable("actors_tbl", n_cols, flags)) {
		ImGui::TableSetupColumn("draw");
		/* FilmObject array index — the only stable per-instance
		 * identifier in the FILM format. Same value cutscene
		 * manifests reference via the `<res_name>#<idx>` key. */
		ImGui::TableSetupColumn("idx");
		ImGui::TableSetupColumn("type");
		ImGui::TableSetupColumn("name");
		ImGui::TableSetupColumn("z");
		ImGui::TableSetupColumn("ofs");
		ImGui::TableSetupColumn("screen");
		ImGui::TableSetupColumn("size");
		ImGui::TableSetupColumn("state");
		ImGui::TableSetupColumn("flags");
		/* Cutscene-manifest hit indicator — only when --remaster
		 * supplied a bundle root. Lets the asset team see at a
		 * glance which actor instances the compositor has a
		 * sprite/atlas mapping for. */
		if (have_remaster) ImGui::TableSetupColumn("manifest");
		ImGui::TableHeadersRow();

		for (int i = 0; i < app.player.object_count; i++) {
			TieFilmPlayerObject *o = &app.player.objects[i];
			if (o->type_code != FILM_TC_ACTOR) continue;
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);

			char id_buf[32];
			std::snprintf(id_buf, sizeof id_buf, "##draw%d", i);
			bool drawn = !o->gui_hidden;
			if (ImGui::Checkbox(id_buf, &drawn)) {
				o->gui_hidden = !drawn;
				TieFilmView_RecomposeIntoTexture(app);
			}
			ImGui::SameLine();
			std::snprintf(id_buf, sizeof id_buf, "##sel%d", i);
			bool sel = (app.selected_actor == i);
			if (ImGui::Selectable(id_buf, sel,
			                      ImGuiSelectableFlags_SpanAllColumns))
				app.selected_actor = sel ? -1 : i;

			ImGui::TableSetColumnIndex(1);
			ImGui::Text("%d", o->entry_index);
			ImGui::TableSetColumnIndex(2);
			ImGui::Text("%s", TieFilmView_FourccString(o->res_type).c_str());
			ImGui::TableSetColumnIndex(3);
			if (!o->resource_loaded) {
				ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f),
				                   "%s ← MISSING", o->res_name);
			} else {
				ImGui::Text("%s", o->res_name);
			}
			ImGui::TableSetColumnIndex(4);
			ImGui::Text("%d", o->zplane);
			ImGui::TableSetColumnIndex(5);
			ImGui::Text("%d, %d", o->x, o->y);
			ImGui::TableSetColumnIndex(6);
			{
				int sl, st, sr, sb;
				if (TieFilmPlayer_ActorScreenRect(o, &sl, &st, &sr, &sb))
					ImGui::Text("%d, %d", sl, st);
				else
					ImGui::TextDisabled("—");
			}
			ImGui::TableSetColumnIndex(7);
			ImGui::Text("%dx%d", o->w, o->h);
			ImGui::TableSetColumnIndex(8);
			ImGui::Text("%d", o->state);
			ImGui::TableSetColumnIndex(9);
			ImGui::Text("%s%s%s",
			            o->show  ? "S" : "·",
			            o->hflip ? "H" : "·",
			            o->vflip ? "V" : "·");
			if (have_remaster) {
				ImGui::TableSetColumnIndex(10);
				bool hit = TieScene2dCutscene_ActorHasManifest(
				    app.cutscene, app.lfd_basename.c_str(),
				    app.player.film_name, o->res_name,
				    (int16_t)o->entry_index);
				if (hit)
					ImGui::TextColored(ImVec4(0.5f, 0.9f, 0.5f, 1.0f), "✓");
				else
					ImGui::TextColored(ImVec4(0.9f, 0.5f, 0.4f, 1.0f), "—");
			}
		}
		ImGui::EndTable();
	}

	if (app.selected_actor >= 0 &&
	    app.selected_actor < app.player.object_count) {
		TieFilmPlayerObject *o = &app.player.objects[app.selected_actor];
		ImGui::Separator();
		ImGui::Text("Selected: %s %s", TieFilmView_FourccString(o->res_type).c_str(),
		            o->res_name);

		/* Debug force-show toggle. The FILM script's `show` flag is
		 * the engine-driven visibility; some actors (REGISTER's
		 * reg-dora, COMPUTER's similar engine-driven entries, …)
		 * have no ACTOR_SHOW record because the runtime C code
		 * flips visibility imperatively (e.g. register.c's
		 * user_Door reacting to button presses). Filmview doesn't
		 * simulate those callbacks, so the actor stays hidden by
		 * default. Check this to override and inspect the asset.
		 *
		 * The override is per-actor and does NOT modify FILM-script
		 * state — stepping a cel that has an explicit ACTOR_SHOW
		 * still flips o->show as usual; force_show just relaxes the
		 * "must have show=true" gate at composite time. */
		bool force = o->force_show;
		if (ImGui::Checkbox("Force show (debug)", &force)) {
			o->force_show = force;
			TieFilmView_RecomposeIntoTexture(app);
		}
		ImGui::SameLine();
		ImGui::TextDisabled("(reveals engine-driven actors with no "
		                    "ACTOR_SHOW; FILM-script `show` = %s)",
		                    o->show ? "true" : "false");

		/* Bake-from-source mode.
		 *
		 * Crops the actor's classic-coord screen rect from a
		 * 3840×2160 reference PNG (16:9 with 4:3 classic content
		 * pillarboxed) and lands it in the target asset's pixels —
		 * for ANIM the current cel-frame sub-rect within the atlas
		 * PNG, for DELT/RAW the whole sprite PNG. The override
		 * texture path keeps the live compose preview in sync without
		 * re-encoding KTX2; Save writes the modified RGBA to the
		 * sibling .png on disk; Bake KTX2 re-encodes the BC7 KTX2
		 * from RAM. */
		const bool bake_is_anim   = (o->res_type == FCC_ANIM &&
		                             o->anim.count > 0);
		const bool bake_is_sprite = (o->res_type == FCC_DELT ||
		                             o->res_type == FCC_RAW);
		if (app.cutscene && (bake_is_anim || bake_is_sprite)) {
			ImGui::Separator();
			ImGui::TextDisabled("Bake from 3840×2160 reference PNG:");

			/* Lightweight lookup for whether the actor resolves to a
			 * remaster variant — gates the Bake button + the
			 * Save/KTX2/Reset row. Doesn't pull rect data; the bake
			 * function does its own resolution at click time. ANIM
			 * targets an atlas (manifest atlas variant); DELT/RAW
			 * targets a single sprite PNG. */
			TieScene2dManifest *bcs =
			    TieScene2dCutscene_ManifestMut(app.cutscene);
			int bcur_cel = TieFilmPlayer_DisplayedCel(&app.player);
			const char *bake_asset_path = nullptr;
			bool bake_have_target = false;
			if (bake_is_anim) {
				bake_have_target = TieScene2dManifest_AtlasGet(
				    bcs, app.lfd_basename.c_str(), app.player.film_name,
				    o->res_name, (int16_t)o->entry_index, bcur_cel,
				    /*frame_idx=*/0, nullptr, nullptr, nullptr, nullptr,
				    nullptr, &bake_asset_path);
			} else {
				bake_have_target = TieScene2dManifest_SpriteGet(
				    bcs, app.lfd_basename.c_str(), app.player.film_name,
				    o->res_name, (int16_t)o->entry_index, bcur_cel,
				    &bake_asset_path);
			}

			if (app.bake_source_path.empty()) {
				ImGui::TextDisabled("source: (none)");
			} else {
				ImGui::Text("source: %s  (%d×%d)",
				            app.bake_source_path.c_str(),
				            app.bake_source_w, app.bake_source_h);
			}
			char bid[40];
			std::snprintf(bid, sizeof bid, "Pick source PNG…##bake_pick%d",
			              app.selected_actor);
			if (ImGui::Button(bid))
				TieFilmView_OpenDialogFor(app, TieFilmViewApp::FileOp::BindBakeSource);

			ImGui::SameLine();
			bool can_bake =
			    !app.bake_source_rgba.empty() && bake_have_target;
			ImGui::BeginDisabled(!can_bake);
			std::snprintf(bid, sizeof bid,
			              "%s##bake_run%d",
			              bake_is_anim ? "Bake current frame"
			                            : "Bake sprite",
			              app.selected_actor);
			if (ImGui::Button(bid))
				TieFilmView_BakeSelectedActorFrame(app);
			ImGui::EndDisabled();

			/* Mask preprocessing — see TieFilmViewApp::bake_dilate_px /
			 * bake_blur_px for what each does. Defaults of 0 reproduce
			 * the original "mask by existing alpha" behavior; bump
			 * dilate to capture more of the new 4K frame past the
			 * VGA-upscale silhouette boundary, and feather to smooth
			 * the resulting stair-step into a gradient. Settings
			 * apply on the next Bake click. */
			ImGui::SetNextItemWidth(160.0f);
			ImGui::SliderInt("Dilate alpha (px)##bake_dilate",
			                 &app.bake_dilate_px, 0, 32);
			ImGui::SameLine();
			ImGui::SetNextItemWidth(160.0f);
			ImGui::SliderInt("Feather alpha (px)##bake_blur",
			                 &app.bake_blur_px,   0, 32);

			/* Per-asset Save / KTX2 / Reset row, only when this actor
			 * has pending pixel edits. Keyed off the same map atlas
			 * and sprite edits both live in. */
			if (bake_have_target && bake_asset_path) {
				auto pit = app.atlas_pixels.find(bake_asset_path);
				if (pit != app.atlas_pixels.end()) {
					TieFilmViewApp::AtlasPixelEdit &edit = pit->second;
					ImGui::Text("%s pixels: %s%s",
					            bake_is_anim ? "atlas" : "sprite",
					            edit.png_path.c_str(),
					            edit.dirty ? "  [modified]" : "");

					ImGui::BeginDisabled(!edit.dirty);
					std::snprintf(bid, sizeof bid,
					              "Save %s PNG##bake_save%d",
					              bake_is_anim ? "atlas" : "sprite",
					              app.selected_actor);
					if (ImGui::Button(bid))
						TieFilmView_SaveAtlasPixels(app, edit);
					ImGui::EndDisabled();
					ImGui::SameLine();
					std::snprintf(bid, sizeof bid,
					              "Bake KTX2 (medium)##bake_ktx2%d",
					              app.selected_actor);
					if (ImGui::Button(bid))
						TieFilmView_RebakeKtx2ForAtlas(app, edit);
					ImGui::SameLine();
					std::snprintf(bid, sizeof bid,
					              "Reset %s pixels##bake_reset%d",
					              bake_is_anim ? "atlas" : "sprite",
					              app.selected_actor);
					if (ImGui::Button(bid))
						TieFilmView_ResetAtlasPixels(app, bake_asset_path);
				}
			}

			if (!app.bake_status.empty())
				ImGui::TextWrapped("%s", app.bake_status.c_str());
		}

		ImGui::Separator();
		ImGui::TextDisabled("Engine offset (FCMD_ACTOR_POS / VEL):");
		ImGui::Text("  ofs: %d,%d  frac: %d,%d", o->x, o->y, o->xf, o->yf);
		ImGui::Text("  vel: %d,%d  vfrac: %d,%d", o->xv, o->yv,
		            o->xvf, o->yvf);

		ImGui::TextDisabled(
		    "Sprite bbox. For ANIMs the engine picks frame[state] each "
		    "cel — its bbox typically shifts between frames, which is "
		    "where ANIM motion comes from. The 'enclosing bounds' is the "
		    "union over all frames, used as the HFLIP/VFLIP mirror axis "
		    "(it's CONSTANT by construction).");
		ImGui::Text("  enclosing bounds: (%d,%d)-(%d,%d)  %dx%d",
		            o->bounds_l, o->bounds_t, o->bounds_r, o->bounds_b,
		            o->bounds_r - o->bounds_l, o->bounds_b - o->bounds_t);

		/* Per-frame bbox — for DELT/RAW this equals the sprite header
		   rect; for ANIM it's the rect of the currently-displayed
		   frame and changes as `state` advances. */
		int fl = 0, ft = 0, fr = 0, fb = 0;
		bool have_frame = false;
		if (o->res_type == FCC_ANIM && o->anim.count > 0) {
			int s = o->state;
			while (s < 0)            s += o->anim.count;
			while (s >= o->anim.count) s -= o->anim.count;
			const Image8 *img = &o->anim.frames[s];
			if (img->pixels) {
				fl = img->left;
				ft = img->top;
				fr = img->left + img->width;
				fb = img->top + img->height;
				have_frame = true;
			}
		} else if (o->sprite.pixels) {
			fl = o->sprite.left;
			ft = o->sprite.top;
			fr = o->sprite.left + o->sprite.width;
			fb = o->sprite.top + o->sprite.height;
			have_frame = true;
		}
		if (have_frame) {
			ImGui::Text("  current frame bbox: (%d,%d)-(%d,%d)  %dx%d",
			            fl, ft, fr, fb, fr - fl, fb - ft);
		} else {
			ImGui::TextDisabled("  current frame bbox: (no sprite)");
		}

		int l, t, r, b;
		if (TieFilmPlayer_ActorScreenRect(o, &l, &t, &r, &b)) {
			ImGui::TextDisabled("Screen position (= bbox + ofs, with flip):");
			ImGui::Text("  rect: (%d,%d)-(%d,%d)  size %dx%d",
			            l, t, r, b, r - l, b - t);
		}

		ImGui::TextDisabled("ANIM frame state (FCMD_ACTOR_STATE / STATEV):");
		ImGui::Text("  state: %d (frac %d)  vel: %d (frac %d)",
		            o->state, o->state_f, o->state_v, o->state_vf);

		/* Manual frame override for ANIM actors. The slider writes
		 * directly into o->state — the next composite picks
		 * frame[state] (with the same modulo wrap the engine uses).
		 * Stepping a cel can immediately overwrite this when the
		 * FILM script issues an ACTOR_STATE / ACTOR_STATEV record;
		 * the override is for inspection, not script replacement. */
		if (o->res_type == FCC_ANIM && o->anim.count > 0) {
			int max_state = o->anim.count - 1;
			int s = o->state;
			while (s < 0)             s += o->anim.count;
			while (s >= o->anim.count) s -= o->anim.count;
			char id_buf[32];
			std::snprintf(id_buf, sizeof id_buf, "frame##state_ovr%d",
			              app.selected_actor);
			char fmt_buf[32];
			std::snprintf(fmt_buf, sizeof fmt_buf, "%%d / %d", max_state);
			if (ImGui::SliderInt(id_buf, &s, 0, max_state, fmt_buf,
			                     ImGuiSliderFlags_AlwaysClamp)) {
				o->state   = (int16_t)s;
				o->state_f = 0;          /* cancel any sub-frame fraction */
				TieFilmView_RecomposeIntoTexture(app);
			}
			ImGui::SameLine();
			ImGui::TextDisabled("(%d frame%s; reset on next cel step "
			                    "if FILM overrides)",
			                    o->anim.count,
			                    o->anim.count == 1 ? "" : "s");
		}

		ImGui::TextDisabled("ACTOR_CLIP rect (default = canvas):");
		ImGui::Text("  clip: (%d,%d)-(%d,%d)  vel: (%d,%d)-(%d,%d)",
		            o->frame_l, o->frame_t, o->frame_r, o->frame_b,
		            o->frame_vl, o->frame_vt, o->frame_vr, o->frame_vb);

		ImGui::Text("var1=%d  var2=%d  zplane=%d  show=%s  flip=%s%s",
		            o->var1, o->var2, o->zplane,
		            o->show ? "true" : "false",
		            o->hflip ? "H" : "-", o->vflip ? "V" : "-");

		/* Atlas-rect editor.
		 *
		 * Only meaningful when the cutscene compositor is up AND the
		 * selected ANIM resolves to an ASSET_KIND_ATLAS variant at the
		 * current cel. The compose path reads av->atlas.frames[state]
		 * each draw, so writes via TieScene2dManifest_AtlasSet are
		 * visible on the next frame without GPU cache invalidation.
		 *
		 * The displayed frame index mirrors the resolver's clamp:
		 * actor resolution caps state at frame_count-1,
		 * so we edit the same frame the compositor draws. */
		if (app.cutscene && o->res_type == FCC_ANIM && o->anim.count > 0) {
			TieScene2dManifest *cs = TieScene2dCutscene_ManifestMut(app.cutscene);
			int cur_cel = TieFilmPlayer_DisplayedCel(&app.player);
			/* Mirror the engine's modulo wrap on state, then
			 * resolve_actor's clamp-to-last-frame. */
			int s = o->state;
			while (s < 0)              s += o->anim.count;
			while (s >= o->anim.count) s -= o->anim.count;

			TieScene2dRect rect{};
			int atlas_w = 0, atlas_h = 0, frame_count = 0;
			const char *yaml_path = nullptr;
			/* Probe at frame 0 to learn frame_count + dims; clamp s to
			 * frame_count-1 and re-fetch the actual rect. */
			const char *asset_path = nullptr;
			bool have_atlas = TieScene2dManifest_AtlasGet(
			    cs, app.lfd_basename.c_str(), app.player.film_name,
			    o->res_name, (int16_t)o->entry_index, cur_cel,
			    /*frame_idx=*/0, &rect, &atlas_w, &atlas_h,
			    &frame_count, &yaml_path, &asset_path);

			ImGui::Separator();
			if (!have_atlas) {
				ImGui::TextDisabled(
				    "Atlas editor: no atlas variant for %s in %s/%s "
				    "at cel %d", o->res_name, app.lfd_basename.c_str(),
				    app.player.film_name, cur_cel);
			} else {
				int frame_idx = s;
				if (frame_idx >= frame_count) frame_idx = frame_count - 1;
				TieScene2dManifest_AtlasGet(
				    cs, app.lfd_basename.c_str(), app.player.film_name,
				    o->res_name, (int16_t)o->entry_index, cur_cel,
				    frame_idx, &rect, &atlas_w, &atlas_h,
				    &frame_count, &yaml_path, &asset_path);

				std::string yaml_key = yaml_path ? yaml_path : "";
				bool dirty = app.atlas_dirty[yaml_key];

				ImGui::Text("Atlas frame %d / %d  (atlas %d×%d)%s",
				            frame_idx, frame_count - 1,
				            atlas_w, atlas_h,
				            dirty ? "  [modified]" : "");
				if (!yaml_key.empty())
					ImGui::TextDisabled("%s", yaml_key.c_str());

				/* Four DragInts. Step=1, fast=10. Clamp x/y to atlas
				 * extents and w/h to (extent - origin) so a stale
				 * value can't run the rect off the texture and feed
				 * sampler garbage into the compositor. */
				int xi = (int)rect.x, yi = (int)rect.y;
				int wi = (int)rect.w, hi = (int)rect.h;
				int max_x = atlas_w > 0 ? atlas_w : INT_MAX;
				int max_y = atlas_h > 0 ? atlas_h : INT_MAX;
				int max_w = atlas_w > xi ? atlas_w - xi : 0;
				int max_h = atlas_h > yi ? atlas_h - yi : 0;
				bool changed = false;
				char id[40];
				std::snprintf(id, sizeof id, "x##atlas_x%d",
				              app.selected_actor);
				changed |= ImGui::DragInt(id, &xi, 1.0f, 0, max_x);
				std::snprintf(id, sizeof id, "y##atlas_y%d",
				              app.selected_actor);
				changed |= ImGui::DragInt(id, &yi, 1.0f, 0, max_y);
				std::snprintf(id, sizeof id, "w##atlas_w%d",
				              app.selected_actor);
				changed |= ImGui::DragInt(id, &wi, 1.0f, 0, max_w);
				std::snprintf(id, sizeof id, "h##atlas_h%d",
				              app.selected_actor);
				changed |= ImGui::DragInt(id, &hi, 1.0f, 0, max_h);

				if (changed) {
					/* Lazily snapshot the entire frame array on the
					 * first edit per yaml_path so Reset can revert
					 * any frame without re-parsing the YAML. */
					auto baseline_it = app.atlas_baseline.find(yaml_key);
					if (baseline_it == app.atlas_baseline.end()) {
						std::vector<TieScene2dRect> snap(frame_count);
						for (int i = 0; i < frame_count; i++) {
							TieScene2dRect r{};
							TieScene2dManifest_AtlasGet(
							    cs, app.lfd_basename.c_str(),
							    app.player.film_name, o->res_name,
							    (int16_t)o->entry_index, cur_cel,
							    i, &r, nullptr, nullptr, nullptr,
							    nullptr, nullptr);
							snap[i] = r;
						}
						app.atlas_baseline[yaml_key] = std::move(snap);
					}
					TieScene2dRect nr{ (float)xi, (float)yi,
					                 (float)wi, (float)hi };
					TieScene2dManifest_AtlasSet(
					    cs, app.lfd_basename.c_str(),
					    app.player.film_name, o->res_name,
					    (int16_t)o->entry_index, cur_cel,
					    frame_idx, nr);
					app.atlas_dirty[yaml_key] = true;
					app.atlas_status.clear();
				}

				/* Save / Reset row. Save is gated on the dirty flag to
				 * avoid pointless I/O; Reset reverts only the current
				 * frame, leaving any other dirty frame untouched. */
				ImGui::BeginDisabled(!dirty);
				std::snprintf(id, sizeof id, "Save##atlas_save%d",
				              app.selected_actor);
				if (ImGui::Button(id)) {
					char err[256] = {0};
					bool ok = TieScene2dManifest_AtlasSave(
					    cs, app.lfd_basename.c_str(),
					    app.player.film_name, o->res_name,
					    (int16_t)o->entry_index, cur_cel,
					    err, sizeof err);
					if (ok) {
						app.atlas_dirty[yaml_key] = false;
						app.atlas_baseline.erase(yaml_key);
						app.atlas_status =
						    std::string("saved ") + yaml_key;
					} else {
						app.atlas_status =
						    std::string("save failed: ") + err;
					}
				}
				ImGui::EndDisabled();
				ImGui::SameLine();

				auto baseline_it = app.atlas_baseline.find(yaml_key);
				bool have_baseline =
				    (baseline_it != app.atlas_baseline.end() &&
				     frame_idx < (int)baseline_it->second.size());
				ImGui::BeginDisabled(!have_baseline);
				std::snprintf(id, sizeof id, "Reset frame##atlas_rst%d",
				              app.selected_actor);
				if (ImGui::Button(id)) {
					TieScene2dManifest_AtlasSet(
					    cs, app.lfd_basename.c_str(),
					    app.player.film_name, o->res_name,
					    (int16_t)o->entry_index, cur_cel,
					    frame_idx, baseline_it->second[frame_idx]);
					/* Clear the dirty bit only when every frame is
					 * back to baseline — full-array compare. */
					bool all_clean = true;
					for (int i = 0; i < frame_count && all_clean; i++) {
						TieScene2dRect r{};
						TieScene2dManifest_AtlasGet(
						    cs, app.lfd_basename.c_str(),
						    app.player.film_name, o->res_name,
						    (int16_t)o->entry_index, cur_cel,
						    i, &r, nullptr, nullptr, nullptr, nullptr,
						    nullptr);
						const TieScene2dRect &b = baseline_it->second[i];
						if (r.x != b.x || r.y != b.y ||
						    r.w != b.w || r.h != b.h)
							all_clean = false;
					}
					if (all_clean) {
						app.atlas_dirty[yaml_key] = false;
						app.atlas_baseline.erase(yaml_key);
					}
					app.atlas_status.clear();
				}
				ImGui::EndDisabled();

				/* Regenerate from source ANIM. Recovers from a bad
				 * save (e.g. someone dragged a frame off its real
				 * upscaled-pixel position) by recomputing the YAML
				 * the same way filmextract would. The atlas PNG/KTX2
				 * are not touched — they were correct already; this
				 * only fixes the rect coordinates that index into
				 * them. After write, the manifest's in-memory atlas
				 * is reloaded from disk so the change is live. */
				ImGui::SameLine();
				char rid[64];
				std::snprintf(rid, sizeof rid,
				              "Regenerate from source ANIM##atlas_regen%d",
				              app.selected_actor);
				if (ImGui::Button(rid))
					TieFilmView_RegenerateYamlFromAnim(app, o->res_name,
					                          yaml_path,
					                          app.lfd_basename.c_str(),
					                          app.player.film_name,
					                          (int16_t)o->entry_index,
					                          cur_cel);

				if (!app.atlas_status.empty())
					ImGui::TextWrapped("%s", app.atlas_status.c_str());
			}

		}
	}

	ImGui::End();
}

static void TieFilmView_UiPaletteWindow(const TieFilmViewApp &app) {
	if (!ImGui::Begin("Palette")) { ImGui::End(); return; }

	if (!app.player_loaded) {
		ImGui::TextDisabled("(no film loaded)");
		ImGui::End();
		return;
	}

	ImDrawList *dl = ImGui::GetWindowDrawList();
	ImVec2 origin = ImGui::GetCursorScreenPos();
	const float cell = 12.0f;
	for (int i = 0; i < 256; i++) {
		int row = i / 16, col = i % 16;
		ImVec2 p0(origin.x + col * cell, origin.y + row * cell);
		ImVec2 p1(p0.x + cell - 1, p0.y + cell - 1);
		const auto &c = app.player.palette.rgba[i];
		dl->AddRectFilled(p0, p1, IM_COL32(c[0], c[1], c[2], 255));
	}
	ImGui::Dummy(ImVec2(cell * 16, cell * 16));
	ImGui::TextDisabled("256 slots, 16x16 grid (slot 0 transparent)");
	ImGui::End();
}

/* ---------- main loop ---------- */

static void TieFilmView_ProcessKeys(TieFilmViewApp &app, bool &quit_req) {
	ImGuiIO &io = ImGui::GetIO();
	/* Only swallow our shortcuts when the user is actually typing
	   in a text field. Plain WantCaptureKeyboard would also block
	   us whenever ImGui's keyboard nav was active (which is true any
	   time a window has nav focus), so arrows wouldn't reach us. */
	if (io.WantTextInput) return;

	/* Global shortcuts (active even with no film loaded). */
	if (ImGui::IsKeyPressed(ImGuiKey_O, false) &&
	    (io.KeyCtrl || io.KeySuper))
		TieFilmView_OpenDialogFor(app, TieFilmViewApp::FileOp::OpenPrimary);
	if (ImGui::IsKeyPressed(ImGuiKey_Q, false) &&
	    (io.KeyCtrl || io.KeySuper))
		quit_req = true;

	if (!app.player_loaded) return;

	int displayed = TieFilmPlayer_DisplayedCel(&app.player);
	int total = TieFilmPlayer_TotalCels(&app.player);

	if (ImGui::IsKeyPressed(ImGuiKey_Space, false))
		app.playing = !app.playing;
	if (ImGui::IsKeyPressed(ImGuiKey_RightArrow, true))
		TieFilmPlayer_Seek(&app.player, std::min(displayed + 1, total - 1));
	if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow, true))
		TieFilmPlayer_Seek(&app.player, std::max(displayed - 1, 0));
	if (ImGui::IsKeyPressed(ImGuiKey_Home, false))
		TieFilmPlayer_Seek(&app.player, 0);
	if (ImGui::IsKeyPressed(ImGuiKey_End, false))
		TieFilmPlayer_Seek(&app.player, total - 1);
	if (ImGui::IsKeyPressed(ImGuiKey_S, false) &&
	    (io.KeyCtrl || io.KeySuper))
		TieFilmView_SaveScreenshot(app);
	if (ImGui::IsKeyPressed(ImGuiKey_Tab, false) && app.cutscene)
		app.remaster_enabled = !app.remaster_enabled;

	if (TieFilmPlayer_DisplayedCel(&app.player) != displayed)
		TieFilmView_RecomposeIntoTexture(app);
}

static void TieFilmView_DrawApplication(void *userdata) {
	TieFilmViewApp &app = *static_cast<TieFilmViewApp *>(userdata);
	TieFilmView_ProcessKeys(app, app.quit_requested);
	TieFilmView_UiMainMenu(app, app.quit_requested);
	TieFilmView_UiFilmsWindow(app);
	TieFilmView_UiViewportWindow(app);
	TieFilmView_UiControlsWindow(app);
	TieFilmView_UiActorsWindow(app);
	TieFilmView_UiPaletteWindow(app);
	if (app.quit_requested)
		Aeron_RequestQuit();
}

static void TieFilmView_Usage(const char *argv0) {
	std::fprintf(stderr,
	    "Usage: %s <primary.lfd> [--extra <lfd>]... [--svga] "
	    "[--remaster <dir>]\n"
	    "          [--stream <path>]... [--stream-dir <dir>]\n"
	    "  --extra:      additional LFDs searched for shared assets\n"
	    "                (typically EMPIRE.LFD).\n"
	    "  --svga:       interpret assets as the 640x480 SVGA build\n"
	    "                (newer edition; same data formats, larger\n"
	    "                sprites). Composite framebuffer, display, and\n"
	    "                screenshot land at 640x480 with no aspect-\n"
	    "                ratio correction. Default is 320x200 VGA.\n"
	    "  --remaster:   cutscene-asset bundle root. Enables a\n"
	    "                Remastered preview toggle (Tab to flip).\n"
	    "  --stream:     bind a .WRK FMV file to the stream actor of\n"
	    "                the loaded film. Two forms:\n"
	    "                  --stream <path>            (any film)\n"
	    "                  --stream <film_name>=<path>(per-film)\n"
	    "                Stream actors are CUST resources whose FILM\n"
	    "                script sets ACTOR_VAR1=123 — visible in the\n"
	    "                filmdump output of e.g. STARDEST.LFD/stard_f.\n"
	    "  --stream-dir: directory holding the .WRK FMV files. Used to\n"
	    "                auto-resolve streams via the baked play1\n"
	    "                lookup table when no --stream override\n"
	    "                applies. Pass tie-collector/ASTREAM (retail)\n"
	    "                or tie-1998/STREAM (demo); the data set is\n"
	    "                inferred from the directory name.\n",
	    argv0);
	std::exit(2);
}

/* ---------- window size persistence ----------
 *
 * Remembered across runs in `<SDL_GetPrefPath>/state.yaml`. Per the
 * project's YAML-config preference. Reader is line-based to avoid
 * pulling in a YAML library for two ints. Pref path is platform-
 * native (e.g. ~/Library/Application Support/tie/filmview/ on mac,
 * ~/.local/share/tie/filmview/ on Linux). */

static std::string TieFilmView_StateFilePath() {
	char *p = SDL_GetPrefPath("tie", "filmview");
	if (!p) return std::string();
	std::string s = std::string(p) + "state.yaml";
	SDL_free(p);
	return s;
}

static bool TieFilmView_LoadWindowSize(int *out_w, int *out_h) {
	std::string path = TieFilmView_StateFilePath();
	if (path.empty()) return false;
	FILE *f = std::fopen(path.c_str(), "r");
	if (!f) return false;
	int w = 0, h = 0;
	char line[256];
	while (std::fgets(line, sizeof line, f)) {
		std::sscanf(line, " width: %d",  &w);
		std::sscanf(line, " height: %d", &h);
	}
	std::fclose(f);
	/* Sanity-clamp: refuse implausible sizes (corrupt file, monitor
	   change since last save, …). Caller falls back to default. */
	if (w < 200 || h < 200 || w > 16384 || h > 16384) return false;
	*out_w = w;
	*out_h = h;
	return true;
}

static void TieFilmView_SaveWindowSize() {
	std::string path = TieFilmView_StateFilePath();
	if (path.empty()) return;
	int w = 0, h = 0;
	Aeron_GetWindowSize(&w, &h);
	if (w <= 0 || h <= 0) return;
	FILE *f = std::fopen(path.c_str(), "w");
	if (!f) return;
	std::fprintf(f,
	    "# filmview window state — auto-managed, regenerated on quit.\n"
	    "window:\n"
	    "  width: %d\n"
	    "  height: %d\n", w, h);
	std::fclose(f);
}

} /* namespace */

int main(int argc, char **argv) {
	const char *primary_path = nullptr;
	const char *remaster_dir = nullptr;
	bool        svga_mode = false;
	std::vector<const char *> extra_paths;
	std::vector<const char *> stream_args;     /* deferred until TieFilmViewApp exists */
	const char *stream_dir = nullptr;
	for (int i = 1; i < argc; i++) {
		if (std::strcmp(argv[i], "--extra") == 0 && i + 1 < argc)
			extra_paths.push_back(argv[++i]);
		else if (std::strcmp(argv[i], "--remaster") == 0 && i + 1 < argc)
			remaster_dir = argv[++i];
		else if (std::strcmp(argv[i], "--stream") == 0 && i + 1 < argc)
			stream_args.push_back(argv[++i]);
		else if (std::strcmp(argv[i], "--stream-dir") == 0 && i + 1 < argc)
			stream_dir = argv[++i];
		else if (std::strcmp(argv[i], "--svga") == 0)
			svga_mode = true;
		else if (argv[i][0] == '-')
			TieFilmView_Usage(argv[0]);
		else if (!primary_path)
			primary_path = argv[i];
		else
			TieFilmView_Usage(argv[0]);
	}
	if (!primary_path) TieFilmView_Usage(argv[0]);

	int win_w = 1280;
	int win_h = 800;
	TieFilmView_LoadWindowSize(&win_w, &win_h);
	AeronConfig config{};
	config.org_name = "tie";
	config.app_name = "filmview";
	config.resource_root = remaster_dir;
	config.shader_path = TIE_SHADER_RELATIVE_DIR;
	config.window_title = "filmview";
	config.window_width = win_w;
	config.window_height = win_h;
	config.logical_width = win_w;
	config.logical_height = win_h;
	config.presentation_mode = AERON_PRESENTATION_STRETCH;
	config.clear_color_enabled = 1;
	config.clear_color_rgba[0] = 30.0f / 255.0f;
	config.clear_color_rgba[1] = 30.0f / 255.0f;
	config.clear_color_rgba[2] = 30.0f / 255.0f;
	config.clear_color_rgba[3] = 1.0f;
	if (!Aeron_Init(&config)) {
		std::fprintf(stderr, "filmview: Aeron_Init failed\n");
		return 1;
	}
	if (!Aeron_DebugUiAvailable()) {
		std::fprintf(stderr, "filmview: Aeron debug UI support is required\n");
		Aeron_Shutdown();
		return 1;
	}

	TieFilmViewApp app;
	app.svga_mode = svga_mode;
	app.fb_w = svga_mode ? PLAYER_SVGA_FB_W : PLAYER_FB_W;
	app.fb_h = svga_mode ? PLAYER_SVGA_FB_H : PLAYER_FB_H;
	app.rgba.assign((size_t)app.fb_w * (size_t)app.fb_h * 4u, 0);
	Aeron_DebugSetApplication(TieFilmView_DrawApplication, &app);
	Aeron_DebugUiSetVisible(1);

	/* Translate --stream / --stream-dir into TieFilmViewApp state before the
	 * first load_film_at: load_film_at calls apply_stream_binding
	 * which consults these tables. */
	for (const char *arg : stream_args) {
		const char *eq = std::strchr(arg, '=');
		if (eq && eq != arg) {
			std::string name(arg, (size_t)(eq - arg));
			std::string path(eq + 1);
			app.stream_overrides[name] = path;
		} else {
			app.stream_default_path = arg;
		}
	}
	if (stream_dir) {
		app.stream_dir = stream_dir;
		/* Infer data set from the directory name's basename. ASTREAM
		 * (any case) is the Collector's CD layout; everything else is
		 * treated as the demo's STREAM/. The lookup itself is
		 * resilient to a wrong choice — at worst the auto-bind misses
		 * for late-game scenes that diverge between data sets. */
		std::string base = stream_dir;
		auto slash = base.find_last_of("/\\");
		if (slash != std::string::npos) base = base.substr(slash + 1);
		while (!base.empty() && (base.back() == '/' || base.back() == '\\'))
			base.pop_back();
		std::string lower = base;
		for (auto &c : lower) c = (char)std::tolower((unsigned char)c);
		app.stream_dir_is_retail = (lower == "astream");
	}

	TieFilmUtil_OpenLfd(&app.primary, primary_path);
	app.primary_path = primary_path;
	app.lfd_basename = TieFilmView_LfdBasenameFrom(primary_path);
	app.extras.resize(extra_paths.size());
	for (size_t i = 0; i < extra_paths.size(); i++)
		TieFilmUtil_OpenLfd(&app.extras[i], extra_paths[i]);

	TieFilmView_RebuildChain(app);
	TieFilmView_RescanFilms(app);

	if (app.film_entry_indices.empty()) {
		TieFilmView_ShowError("%s contains no FILM resources.", primary_path);
	} else {
		TieFilmView_LoadFilmAt(app, 0);
	}

	if (app.player_loaded)
		TieFilmView_RecomposeIntoTexture(app);
	TieFilmView_EnsureScaledTexture(app);
	TieFilmView_UpdateScaledTexture(app);

	/* remaster preview: stand it up if --remaster <dir> resolved to
	 * a directory containing at least one parseable manifest. The
	 * 1920×1080 RT is bilinear-sampled when ImGui::Image stretches
	 * it to the display rect. */
	if (remaster_dir) {
		app.remaster_dir   = remaster_dir;
		app.cutscene = TieScene2dCutscene_Init(remaster_dir, nullptr);
		if (app.cutscene) {
			AeronRenderTargetDesc target_desc {
				.width = app.cutscene_w,
				.height = app.cutscene_h,
				.format = AERON_TEXTURE_FORMAT_RGBA8_SRGB,
				.debug_name = "filmview remastered cutscene",
			};
			app.cutscene_target = Aeron_CreateRenderTarget(&target_desc);
			app.cutscene_draw_list = AeronDrawList_Create(4096);
		}
	}

	{
		char cwd[1024];
		if (getcwd(cwd, sizeof cwd))
			app.screenshot_dir = cwd;
		else
			app.screenshot_dir = ".";
	}

	while (!Aeron_QuitRequested() && !Aeron_FatalErrorRequested()) {
		const int32_t delta_us = Aeron_BeginFrame();
		double dt = (double)delta_us * 1e-6;

		if (app.playing && app.player_loaded) {
			app.play_accum_s += dt;
			double cel_dur = (app.fps > 0.0f) ? 1.0 / app.fps : 1.0;
			int prev = TieFilmPlayer_DisplayedCel(&app.player);
			while (app.play_accum_s >= cel_dur) {
				app.play_accum_s -= cel_dur;
				int d = TieFilmPlayer_DisplayedCel(&app.player);
				int total = TieFilmPlayer_TotalCels(&app.player);
				if (d + 1 >= total) {
					app.playing = false;
					break;
				}
				TieFilmPlayer_Step(&app.player);
			}
			if (TieFilmPlayer_DisplayedCel(&app.player) != prev)
				TieFilmView_RecomposeIntoTexture(app);
		}

		TieFilmView_DrainPendingFileOp(app);
		TieFilmView_EnsureScaledTexture(app);
		TieFilmView_UpdateScaledTexture(app);
		TieFilmView_RenderCutscenePreview(app);
		if (!Aeron_Present())
			Aeron_RequestFatalRendererError("filmview presentation");
	}

	/* Persist the current window size so the next launch picks up
	   where the user left off. Done before destroying the window. */
	TieFilmView_SaveWindowSize();

	/* Pixel-edit override textures must be released BEFORE
	 * TieScene2dCutscene_Shutdown destroys the asset cache that borrows them. */
	TieFilmView_ClearAllAtlasOverrides(app);
	if (app.cutscene_draw_list) AeronDrawList_Destroy(app.cutscene_draw_list);
	if (app.cutscene_target) Aeron_DestroyRenderTarget(app.cutscene_target);
	if (app.cutscene) TieScene2dCutscene_Shutdown(app.cutscene);
	if (app.fb_scaled_target) Aeron_DestroyRenderTarget(app.fb_scaled_target);
	TieFilmView_UnloadFilm(app);
	for (auto &lfd : app.extras) TieLfdFile_Close(&lfd);
	TieLfdFile_Close(&app.primary);
	const int exit_status = Aeron_FatalErrorRequested() ? 1 : 0;
	Aeron_DebugSetApplication(nullptr, nullptr);
	Aeron_Shutdown();
	return exit_status;
}
