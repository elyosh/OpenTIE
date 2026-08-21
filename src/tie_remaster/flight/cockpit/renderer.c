/* 2D cockpit + HUD overlay. GLB mode loads catalog assets; original modes
 * decode the selected installation's cockpit files into the same cache. */

#include "tie_remaster/flight/cockpit/renderer.h"

#include "aeron/aeron.h"
#include "aeron/log.h"
#include "aeron/render.h"
#include "aeron/scene/draw_list2d.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "aeron/scene/image_cache.h"
#include "aeron/scene/ktx2_reader.h"
#include "aeron/scene/runtime_atlas.h"
#include "aeron/scene/world.h"
#include "tie_formats/cockpit.h"
#include "tie_formats/cockpit_font.h"
#include "tie_formats/cockpit_masks.h"
#include "tie_formats/panel.h"
#include "tie_remaster/flight/cockpit/common.h"
#include "tie_remaster/flight/cockpit/layout.h"
#include "tie_remaster/flight/cockpit/text.h"
#include "tie_remaster/flight/hud_geometry.h"
#include "tie_remaster/flight/mesh_draw.h"
#include "tie_remaster/flight/render_math.h"
#include "tie_remaster/flight/renderer.h"
#include "tie_remaster/gpu_debug.h"
#include "tie_remaster/scene2d/srgb_math.h"
#include "tie_remaster/scene2d/text.h"
#include "tie_runtime/flight_assets/assets.h"
#include "tie_runtime/flight_assets/source.h"
#include "tie_runtime/snapshot/snapshot.h"

#define COCKPIT_MAX_CACHED 8
#define COCKPIT_VIEW_NAME_MAX 16

/* The chrome cache RT keeps a real alpha channel regardless of the scene
 * rt_format: the canopy cutout is per-texel alpha within the (overflowing)
 * coverage rects, not geometry. The scene chain (color/bloom/mb/PIP) carries
 * no alpha consumer and runs at the packed HDR format for bandwidth, so the
 * two formats are deliberately decoupled. */
#define COCKPIT_CHROME_FORMAT AERON_TEXTURE_FORMAT_RGBA16_FLOAT

/* CMD CRT mask cache — 7 SVGA variants + 4 VGA variants = 11 max. */
#define COCKPIT_CRT_MASK_MAX 11

#ifndef TIE_SHADER_DIR
#define TIE_SHADER_DIR "shaders"
#endif

/* TieCockpitText_RemapColor + the COCKPIT_BG_* defines live in cockpit_text.h;
 * TieCockpitCommon_InstrumentActive + TieCockpitCommon_IsSvga live in cockpit_common.h. */

/* Atlas-cel defaults; overridden by hud_layout.yaml when present. */
#define COCKPIT_CEL_W 32
#define COCKPIT_CEL_H 32
#define COCKPIT_CELS_PER_ROW 16

/* One opaque-region rectangle in base-image pixel coordinates. */
typedef TieCockpitCoverageRect TieCockpitRendererCoverRect;

typedef struct TieCockpitRendererEntry {
	char view_name[COCKPIT_VIEW_NAME_MAX];
	uint16_t classic_w;
	uint16_t classic_h;
	AeronTexture* base_tex; /* canopy bitmap (full screen) */
	int base_w, base_h;
	/* Opaque-region rects avoid rasterizing the transparent canopy opening. */
	TieCockpitRendererCoverRect* cover_rects;
	int cover_count;
	int cover_img_w, cover_img_h;
	bool tried_cover;
	AeronTexture* parts_tex; /* HUD shape atlas */
	int parts_w, parts_h;
	AeronTexture* damage_tex; /* additive crack overlay */
	int damage_w, damage_h;
	TieCockpitLayout layout; /* per-craft HUD parts layout */
	bool tried_layout;
	bool tried_base;
	bool tried_parts;
	bool tried_damage;
	AeronRuntimeAtlas original_parts;
	bool original_assets;
} TieCockpitRendererEntry;

/* One cached PIP CRT mask: the (variant, classic_w) selector matches
 * the snapshot's TieCockpitState.mask_variant + classic_w fields; the
 * texture is the 8-bit alpha cutout cockpit_extract baked into
 * <remaster_dir>/flight/cockpits/crt_mask_<v>_<res>.ktx2. `tried`
 * memoises a failed load so the per-frame draw doesn't repeat file I/O. */
typedef struct TieCockpitRendererCrtMaskEntry {
	uint8_t variant;
	uint16_t classic_w;
	AeronTexture* tex;
	int tex_w, tex_h;
	bool tried;
} TieCockpitRendererCrtMaskEntry;

typedef struct TieCockpitRendererDrawSpace {
	int target_w, target_h;
	int coord_w, coord_h;
	float fit_x, fit_y, fit_w, fit_h;
	float scale_x, scale_y;
} TieCockpitRendererDrawSpace;

struct TieCockpitRenderer {
	const TieFlightAssetSource* assets;
	/* The flight scene RT format (flight_gpu's rt_format, R11G11B10_UFLOAT):
	 * the per-frame color RT the cockpit composites into, and the format of
	 * the PIP RT. Cockpit composite/widget/text pipelines target it so they
	 * validate against the flight scene pass. The chrome cache does NOT use
	 * this — it needs alpha for the canopy cutout and carries its own
	 * COCKPIT_CHROME_FORMAT (RGBA16F). Cockpit pixels sit in linear HDR
	 * alongside the 3D scene; bloom samples them and present tonemaps them. */
	AeronTextureFormat rt_format;
	/* Depth-stencil format of the render pass cockpit drawing happens
	 * inside. Set at init to D32_FLOAT — cockpit is always called
	 * within the flight-main pass, which has flight's depth attachment
	 * bound (even though cockpit pipelines have depth test/write off).
	 * Threaded through to gpu_pipeline_get_with_depth so the pipeline's
	 * target_info attachment count matches the actual pass. */
	AeronTextureFormat depth_format;
	/* Cached state from the last successful TieCockpitRenderer_Prepare,
	 * consumed by the matching TieCockpitRenderer_DrawInPass. Stays valid
	 * until the next prepare() call. */
	const TieCockpitRendererEntry* pending_entry;
	bool pending_ready;
	struct TieScene2dTextRenderer* text_renderer;
	struct TieFlightRenderer* flight_gpu; /* borrowed; for PIP pre-pass */
	AeronDrawList2D* chrome_bake_list;
	AeronDrawList2D* before_pip_list;
	AeronDrawList2D* after_pip_list;
	AeronDrawList2D* record_list;
	TieCockpitRendererDrawSpace pending_space;
	AeronShader* pip_vs;
	AeronShader* pip_fs;
	AeronGraphicsPipeline* pip_pipeline;
	AeronSampler* pip_color_sampler;
	AeronSampler* pip_mask_sampler; /* NEAREST — hard scanline edges */
	/* Asset reads belong to preparation. This counter makes accidental
	 * draw-phase I/O visible without changing the rendering path. */
	uint32_t draw_phase_asset_io_count;
	bool draw_phase_active;
	/* Chrome cache: the static cockpit layer (base bitmap + damage cracks
	 * + subsystem covers) pre-rendered into one alpha-bearing RT, so the
	 * per-frame path replaces 1 base blit + N crack/cover blits with a
	 * single composite. Baked in TieCockpitRenderer_Prepare (its own render pass,
	 * outside the flight-main pass) only when chrome_key changes — view /
	 * craft switch, mirror toggle, or a subsystem knockout/repair. The RT
	 * is screen-aligned (full rt_w×rt_h, fit viewport applied at bake) so
	 * the composite is a 1:1 sample, restricted to the base coverage rects
	 * to keep the canopy opening from being rasterised. rt_format carries
	 * a real alpha channel (RGBA16F) — the canopy cutout lives in the
	 * per-texel alpha within the (overflowing) coverage rects, not in the
	 * coarse rect geometry. */
	AeronRenderTarget* chrome_rt;
	int chrome_w, chrome_h;
	bool chrome_valid;
	struct {
		const TieCockpitRendererEntry* entry;
		uint16_t view_idx;
		uint16_t mirrored_view;
		uint16_t installed_subsystems;
		uint16_t working_subsystems;
		uint16_t subsystem_active;
		int coord_w, coord_h;
		int rt_w, rt_h;
	} chrome_key;
	TieCockpitRendererEntry entries[COCKPIT_MAX_CACHED];
	int entry_count;
	TieCockpitRendererCrtMaskEntry crt_masks[COCKPIT_CRT_MASK_MAX];
	int crt_mask_count;
	uint8_t original_vga_palette[576];
	bool original_vga_palette_loaded;
	uint16_t original_font_width;
	float original_font_atlas_scale;
};

static AeronTexture* TieCockpitRenderer_LoadKtx2(TieCockpitRenderer* cg, AeronCommandBuffer* cmd,
												 const char* path, int* out_w, int* out_h);
static bool TieCockpitRenderer_CockpitAssetErrorImpl(TieCockpitRenderer* cg, const char* path,
													 const char* detail);
#define TIE_COCKPIT_ASSET_ERROR(path, detail) TieCockpitRenderer_CockpitAssetErrorImpl(cg, (path), (detail))

/* Load a compiled shader by name. Application shaders are cooked into Aeron's
 * single shader root (see TIE_SHADER_BINARY_DIR); Aeron_CreateShader
 * resolves <basename>.msl there alongside Aeron's own. */
static AeronShader* TieCockpitRenderer_CompileShader(const char* basename, AeronShaderStage stage,
													 uint32_t num_samplers, uint32_t num_uniform_buffers) {
	AeronShader* sh = Aeron_CreateShader(&(AeronShaderDesc) {
		.name = basename,
		.stage = stage,
		.sampler_count = num_samplers,
		.uniform_buffer_count = num_uniform_buffers,
	});
	if (!sh)
		Aeron_LogError("tie.cockpit", "shader load failed: %s", basename);
	return sh;
}

/* PMA-blend pipeline driven by SV_VertexID for the 4-vertex trianglestrip
 * — no vertex buffer. Mask sampler is NEAREST to keep the engine's
 * scanline-hard cutout edges; color sampler borrowed at draw time. */
static bool TieCockpitRenderer_CockpitInitPipPipeline(TieCockpitRenderer* cg) {
	cg->pip_vs = TieCockpitRenderer_CompileShader("cockpit_pip_compose.vert", AERON_SHADER_STAGE_VERTEX,
												  /*samplers=*/0,
												  /*uniforms=*/1);
	cg->pip_fs = TieCockpitRenderer_CompileShader("cockpit_pip_compose.frag", AERON_SHADER_STAGE_FRAGMENT,
												  /*samplers=*/2,
												  /*uniforms=*/0);
	if (!cg->pip_vs || !cg->pip_fs) {
		Aeron_LogError("tie.cockpit", "PIP shaders unavailable (vs=%p fs=%p, shader_dir=%s)",
					   (void*)cg->pip_vs, (void*)cg->pip_fs, TIE_SHADER_DIR);
		return false;
	}

	cg->pip_mask_sampler = Aeron_CreateSampler(&(AeronSamplerDesc) {
		.min_filter = AERON_FILTER_NEAREST,
		.mag_filter = AERON_FILTER_NEAREST,
		.mip_filter = AERON_FILTER_NEAREST,
		.address_u = AERON_ADDRESS_CLAMP_TO_EDGE,
		.address_v = AERON_ADDRESS_CLAMP_TO_EDGE,
		.address_w = AERON_ADDRESS_CLAMP_TO_EDGE,
		.min_lod = 0.0f,
		.max_lod = 1000.0f,
	});
	if (!cg->pip_mask_sampler)
		return false;

	cg->pip_color_sampler = Aeron_CreateSampler(&(AeronSamplerDesc) {
		.min_filter = AERON_FILTER_LINEAR,
		.mag_filter = AERON_FILTER_LINEAR,
		.mip_filter = AERON_FILTER_LINEAR,
		.address_u = AERON_ADDRESS_CLAMP_TO_EDGE,
		.address_v = AERON_ADDRESS_CLAMP_TO_EDGE,
		.address_w = AERON_ADDRESS_CLAMP_TO_EDGE,
		.min_lod = 0.0f,
		.max_lod = 1000.0f,
	});
	if (!cg->pip_color_sampler)
		return false;

	/* Depth test/write disabled; the attachment is declared only so the
	 * pipeline's target state matches the flight-main pass we're bound
	 * inside (backend validation rule). */
	AeronGraphicsPipelineDesc info = {
        .vertex_shader   = cg->pip_vs,
        .fragment_shader = cg->pip_fs,
        .primitive_type  = AERON_PRIMITIVE_TRIANGLE_STRIP,
        .cull_mode       = AERON_CULL_NONE,
        .color_format    = cg->rt_format,
        .depth_format    = AERON_TEXTURE_FORMAT_D32_FLOAT,
        .sample_count    = AERON_SAMPLE_COUNT_1,
        .blend = {
            .enabled   = 1,
            .src_color = AERON_BLEND_ONE,
            .dst_color = AERON_BLEND_ONE_MINUS_SRC_ALPHA,
            .color_op  = AERON_BLEND_OP_ADD,
            .src_alpha = AERON_BLEND_ONE,
            .dst_alpha = AERON_BLEND_ONE_MINUS_SRC_ALPHA,
            .alpha_op  = AERON_BLEND_OP_ADD,
        },
    };
	cg->pip_pipeline = Aeron_CreateGraphicsPipeline(&info);
	if (!cg->pip_pipeline) {
		Aeron_LogError("tie.cockpit", "PIP pipeline creation failed");
		return false;
	}
	return true;
}

static const TieFlightAssetBundle* TieCockpitRenderer_Catalog(const TieCockpitRenderer* cg) {
	return cg && TieFlightAssetSource_IsRemastered(cg->assets) ? cg->assets->catalog : NULL;
}

/* Resolve a catalog path below ASSET/remaster. */
static bool TieCockpitRenderer_ComposeFlightPath(const TieCockpitRenderer* cg, const char* rel, char* out,
												 size_t out_cap) {
	const TieFlightAssetBundle* catalog = TieCockpitRenderer_Catalog(cg);
	if (!catalog || !rel || !rel[0])
		return false;
	const char* prefix = TieFlightAssets_ContentPrefix(catalog);
	int n = snprintf(out, out_cap, "%s/%s", prefix, rel);
	return n > 0 && (size_t)n < out_cap;
}

/* Resolve or load the required CRT mask before opening the render pass. */
static AeronTexture* TieCockpitRenderer_EnsureCrtMask(TieCockpitRenderer* cg, AeronCommandBuffer* cmd,
													  uint8_t variant, uint16_t classic_w) {
	if (!cg || !cg->assets)
		return NULL;
	if (!TieFlightAssetSource_IsRemastered(cg->assets)) {
		for (int index = 0; index < cg->crt_mask_count; ++index)
			if (cg->crt_masks[index].variant == variant && cg->crt_masks[index].classic_w == classic_w)
				return cg->crt_masks[index].tex;
		if (cg->crt_mask_count >= COCKPIT_CRT_MASK_MAX) {
			TIE_COCKPIT_ASSET_ERROR("compiled CRT mask", "cache capacity exceeded");
			return NULL;
		}
		const uint8_t* data = NULL;
		size_t size = 0;
		data = TieCockpitMaskData_Get(variant, classic_w, &size);
		if (!data) {
			TIE_COCKPIT_ASSET_ERROR("compiled CRT mask", "selected variant is unavailable");
			return NULL;
		}
		uint8_t* rgba = NULL;
		int width = 0, height = 0;
		TieFormatError error = { 0 };
		if (!TieCockpitCrtMask_Decode(data, size, &rgba, &width, &height, &error)) {
			TIE_COCKPIT_ASSET_ERROR("compiled CRT mask", error.message);
			return NULL;
		}
		TieCockpitRendererCrtMaskEntry* mask = &cg->crt_masks[cg->crt_mask_count++];
		mask->variant = variant;
		mask->classic_w = classic_w;
		mask->tex_w = width;
		mask->tex_h = height;
		mask->tried = true;
		mask->tex = Aeron_ImageUploadRgba8(cmd, rgba, width, height, (size_t)width * 4,
										   AERON_TEXTURE_FORMAT_RGBA8_UNORM, AERON_COLOR_SPACE_LINEAR_SRGB,
										   AERON_IMAGE_ALPHA_STRAIGHT, false, "TIE original CRT mask");
		free(rgba);
		if (!mask->tex)
			TIE_COCKPIT_ASSET_ERROR("compiled CRT mask", "GPU upload failed");
		return mask->tex;
	}
	const TieFlightAssetBundle* catalog = TieCockpitRenderer_Catalog(cg);
	if (!catalog)
		return NULL;

	/* Cached? */
	for (int i = 0; i < cg->crt_mask_count; ++i) {
		TieCockpitRendererCrtMaskEntry* mask = &cg->crt_masks[i];
		if (mask->variant == variant && mask->classic_w == classic_w) {
			if (mask->tex)
				return mask->tex;
			if (mask->tried) {
				TIE_COCKPIT_ASSET_ERROR("CRT mask", "previous upload failed");
				return NULL;
			}
			const TieFlightAssetCrtMask* cm = TieFlightAssets_CrtMask(catalog, variant, classic_w);
			char path[1024];
			if (cm && TieCockpitRenderer_ComposeFlightPath(cg, cm->path, path, sizeof path)) {
				mask->tex = TieCockpitRenderer_LoadKtx2(cg, cmd, path, &mask->tex_w, &mask->tex_h);
			}
			mask->tried = true;
			if (!mask->tex)
				TIE_COCKPIT_ASSET_ERROR(cm ? cm->path : "CRT mask", "catalog path or texture is invalid");
			return mask->tex;
		}
	}
	if (cg->crt_mask_count >= COCKPIT_CRT_MASK_MAX) {
		TIE_COCKPIT_ASSET_ERROR("CRT mask", "cache capacity exceeded");
		return NULL;
	}

	const TieFlightAssetCrtMask* cm = TieFlightAssets_CrtMask(catalog, variant, classic_w);
	if (!cm) {
		char identity[96];
		snprintf(identity, sizeof identity, "CRT mask variant %u at width %u", (unsigned)variant,
				 (unsigned)classic_w);
		TIE_COCKPIT_ASSET_ERROR(identity, "catalog has no cockpit_masks row");
		return NULL;
	}

	TieCockpitRendererCrtMaskEntry* mask = &cg->crt_masks[cg->crt_mask_count++];
	mask->variant = variant;
	mask->classic_w = classic_w;
	mask->tried = true;
	char path[1024];
	if (!TieCockpitRenderer_ComposeFlightPath(cg, cm->path, path, sizeof path))
		TIE_COCKPIT_ASSET_ERROR(cm->path, "path is invalid");
	else {
		mask->tex = TieCockpitRenderer_LoadKtx2(cg, cmd, path, &mask->tex_w, &mask->tex_h);
		if (!mask->tex)
			TIE_COCKPIT_ASSET_ERROR(path, "texture upload failed");
	}
	return mask->tex;
}

/* KTX2 → 2D Aeron texture upload. Cockpit assets are flat 2D so this
 * thin loader skips the cubemap/asset-cache machinery elsewhere. */
static AeronTexture* TieCockpitRenderer_LoadKtx2(TieCockpitRenderer* cg, AeronCommandBuffer* cmd,
												 const char* path, int* out_w, int* out_h) {
	uint8_t* bytes = NULL;
	size_t size = 0;
	if (cg && cg->draw_phase_active)
		++cg->draw_phase_asset_io_count;
	if (!cg || !cg->assets ||
		!AeronVfs_ReadAll(cg->assets->vfs, AERON_VFS_ROOT_ASSET, path, 64u * 1024u * 1024u, &bytes, &size))
		return NULL;
	Ktx2* k = ktx2_open_mem(bytes, size, path);
	if (!k) {
		free(bytes);
		return NULL;
	}
	if (ktx2_face_count(k) != 1) {
		Aeron_LogWarn("tie.assets", "%s: expected a 2D texture (faces=%d)", path, ktx2_face_count(k));
		ktx2_close(k);
		free(bytes);
		return NULL;
	}

	AeronTexture* tex = Aeron_ImageUploadKtx2(cmd, k, path);
	if (!tex)
		Aeron_RequestFatalRendererError("cockpit texture upload");
	if (tex) {
		if (out_w)
			*out_w = ktx2_width(k);
		if (out_h)
			*out_h = ktx2_height(k);
	}
	ktx2_close(k);
	free(bytes);
	return tex;
}

/* ===== Cache lifecycle ===== */

TieCockpitRenderer* TieCockpitRenderer_Init(AeronCommandBuffer* cmd, const TieFlightAssetSource* assets,
											uint32_t rt_format_u32,
											struct TieScene2dTextRenderer* text_renderer) {
	if (!cmd) {
		Aeron_RequestFatalRendererError("cockpit startup command buffer");
		return NULL;
	}

	TieCockpitRenderer* cg = (TieCockpitRenderer*)calloc(1, sizeof *cg);
	if (!cg) {
		Aeron_RequestFatalRendererError("cockpit renderer allocation");
		return NULL;
	}
	cg->assets = assets;
	cg->rt_format = (AeronTextureFormat)rt_format_u32;
	/* Cockpit always renders inside the flight-main render pass,
	 * which binds flight's D32_FLOAT depth attachment. Pipelines used
	 * by cockpit's drawers declare this depth attachment so backend
	 * validation accepts the bind. */
	cg->depth_format = AERON_TEXTURE_FORMAT_D32_FLOAT;
	cg->text_renderer = text_renderer;

	cg->chrome_bake_list = AeronDrawList_Create(512);
	cg->before_pip_list = AeronDrawList_Create(16384);
	cg->after_pip_list = AeronDrawList_Create(16384);
	if (!cg->chrome_bake_list || !cg->before_pip_list || !cg->after_pip_list) {
		Aeron_RequestFatalRendererError("cockpit draw-list creation");
		TieCockpitRenderer_Shutdown(cg);
		return NULL;
	}

	if (!TieCockpitRenderer_CockpitInitPipPipeline(cg)) {
		Aeron_RequestFatalRendererError("cockpit PIP pipeline creation");
		TieCockpitRenderer_Shutdown(cg);
		return NULL;
	}

	return cg;
}

void TieCockpitRenderer_SetFlightRenderer(TieCockpitRenderer* cg, struct TieFlightRenderer* fg) {
	if (cg)
		cg->flight_gpu = fg;
}

static void TieCockpitRenderer_ReleaseEntry(TieCockpitRenderer* cg, TieCockpitRendererEntry* entry) {
	if (entry->base_tex)
		Aeron_DestroyTexture(entry->base_tex);
	if (entry->original_assets)
		Aeron_RuntimeAtlasRelease(&entry->original_parts);
	else if (entry->parts_tex)
		Aeron_DestroyTexture(entry->parts_tex);
	if (entry->damage_tex)
		Aeron_DestroyTexture(entry->damage_tex);
	free(entry->cover_rects);
	TieCockpitLayout_Free(&entry->layout);
	memset(entry, 0, sizeof *entry);
}

/* Drop every cockpit asset so the next prepare rebuilds the active source.
 * Clears both texture caches — the per-entry atlases
 * (base/parts/damage) + layout via TieCockpitRenderer_ReleaseEntry, AND the separate
 * CRT-mask cache that TieCockpitRenderer_ReleaseEntry doesn't touch — plus the chrome
 * bake (its composite embeds base_tex). */
static void TieCockpitRenderer_CockpitReloadCaches(TieCockpitRenderer* cg) {
	for (int i = 0; i < cg->entry_count; ++i)
		TieCockpitRenderer_ReleaseEntry(cg, &cg->entries[i]);
	cg->entry_count = 0;
	cg->pending_entry = NULL;

	for (int i = 0; i < cg->crt_mask_count; ++i)
		if (cg->crt_masks[i].tex)
			Aeron_DestroyTexture(cg->crt_masks[i].tex);
	memset(cg->crt_masks, 0, sizeof cg->crt_masks);
	cg->crt_mask_count = 0;

	cg->chrome_valid = false;
	Aeron_LogInfo("tie.cockpit", "entry and CRT-mask caches cleared");
}

/* Artist-iteration reload request. Set by TieCockpitRenderer_RequestReload()
 * (debug UI) and consumed at the top of the next TieCockpitRenderer_Prepare().
 * Process-wide static: one cockpit compositor per process, matching the
 * flight_gpu tonemap/EOTF runtime-state convention. Deferring the free
 * to prepare() keeps it off the UI thread and at a frame boundary;
 * the backend defers GPU-side destruction until in-flight frames
 * retire, so clearing mid-session is safe. */
static bool s_cockpit_reload_request;

void TieCockpitRenderer_RequestReload(void) { s_cockpit_reload_request = true; }

void TieCockpitRenderer_Shutdown(TieCockpitRenderer* cg) {
	if (!cg)
		return;
	for (int i = 0; i < cg->entry_count; ++i)
		TieCockpitRenderer_ReleaseEntry(cg, &cg->entries[i]);
	for (int i = 0; i < cg->crt_mask_count; ++i)
		if (cg->crt_masks[i].tex)
			Aeron_DestroyTexture(cg->crt_masks[i].tex);
	if (cg->pip_pipeline)
		Aeron_DestroyGraphicsPipeline(cg->pip_pipeline);
	if (cg->pip_vs)
		Aeron_DestroyShader(cg->pip_vs);
	if (cg->pip_fs)
		Aeron_DestroyShader(cg->pip_fs);
	if (cg->pip_color_sampler)
		Aeron_DestroySampler(cg->pip_color_sampler);
	if (cg->pip_mask_sampler)
		Aeron_DestroySampler(cg->pip_mask_sampler);
	AeronDrawList_Destroy(cg->chrome_bake_list);
	AeronDrawList_Destroy(cg->before_pip_list);
	AeronDrawList_Destroy(cg->after_pip_list);
	if (cg->chrome_rt)
		Aeron_DestroyRenderTarget(cg->chrome_rt);
	free(cg);
}

/* Look up or LRU-evict an entry for (view_name, classic dims).
 * Returns a still-unloaded slot when assets aren't on disk yet. */
static TieCockpitRendererEntry* TieCockpitRenderer_FindOrAllocEntry(TieCockpitRenderer* cg,
																	const char* view_name, uint16_t classic_w,
																	uint16_t classic_h) {
	if (!view_name || !view_name[0])
		return NULL;
	for (int i = 0; i < cg->entry_count; ++i) {
		TieCockpitRendererEntry* entry = &cg->entries[i];
		if (strncmp(entry->view_name, view_name, COCKPIT_VIEW_NAME_MAX) == 0 &&
			entry->classic_w == classic_w && entry->classic_h == classic_h)
			return entry;
	}
	if (cg->entry_count >= COCKPIT_MAX_CACHED) {
		TieCockpitRenderer_ReleaseEntry(cg, &cg->entries[0]);
		for (int i = 1; i < cg->entry_count; ++i)
			cg->entries[i - 1] = cg->entries[i];
		cg->entry_count--;
		memset(&cg->entries[cg->entry_count], 0, sizeof cg->entries[0]);
	}
	TieCockpitRendererEntry* entry = &cg->entries[cg->entry_count++];
	memset(entry, 0, sizeof *entry);
	snprintf(entry->view_name, sizeof entry->view_name, "%s", view_name);
	entry->classic_w = classic_w;
	entry->classic_h = classic_h;
	return entry;
}

/* Fit the cockpit's display aspect inside the HD RT. Classic frames remain
 * full-height and may clip horizontally; authored frames fit completely. */
typedef struct TieCockpitRendererViewport {
	float x, y, w, h;
} TieCockpitRendererViewport;

static void TieCockpitRenderer_CockpitFitViewport(int coord_w, int coord_h, int rt_w, int rt_h,
												  TieCockpitRendererViewport* out) {
	const float cockpit_aspect = TieCockpitCommon_DisplayAspect(coord_w, coord_h);
	const float rt_aspect = (float)rt_w / (float)rt_h;
	if (TieCockpitCommon_IsClassic4x3(coord_w, coord_h) || rt_aspect >= cockpit_aspect) {
		/* Classic frame or wider RT: fit by height. */
		out->h = (float)rt_h;
		out->w = out->h * cockpit_aspect;
		out->x = ((float)rt_w - out->w) * 0.5f;
		out->y = 0.0f;
	} else {
		/* RT taller than cockpit → letterbox (fit by width). */
		out->w = (float)rt_w;
		out->h = out->w / cockpit_aspect;
		out->x = 0.0f;
		out->y = ((float)rt_h - out->h) * 0.5f;
	}
}

static TieCockpitRendererDrawSpace TieCockpitRenderer_CockpitDrawSpace(int coord_w, int coord_h, int target_w,
																	   int target_h) {
	TieCockpitRendererViewport fit;
	TieCockpitRenderer_CockpitFitViewport(coord_w, coord_h, target_w, target_h, &fit);
	return (TieCockpitRendererDrawSpace) {
		.target_w = target_w,
		.target_h = target_h,
		.coord_w = coord_w,
		.coord_h = coord_h,
		.fit_x = fit.x,
		.fit_y = fit.y,
		.fit_w = fit.w,
		.fit_h = fit.h,
		.scale_x = fit.w / (float)coord_w,
		.scale_y = fit.h / (float)coord_h,
	};
}

/* Resolve the coord frame the cockpit pass should use for `entry`. When
 * the layout YAML declared a `reference: { w, h }` block, that wins;
 * otherwise fall back to the snapshot's classic_w/h. Encapsulates the
 * "0 → 640×480 default" guard the drawers used to open-code. */
static void TieCockpitRenderer_ResolveCoordFrame(const TieCockpitRendererEntry* entry,
												 const TieSnapshot* snap, int* out_w, int* out_h) {
	int w = (int)snap->cockpit.classic_w;
	int h = (int)snap->cockpit.classic_h;
	if (w <= 0)
		w = 640;
	if (h <= 0)
		h = 480;
	TieCockpitLayout_CoordFrame(entry ? &entry->layout : NULL, w, h, &w, &h);
	*out_w = w;
	*out_h = h;
}

/* Rescale one (snap_x, snap_y) from the engine's classic frame onto
 * the layout's reference frame, picking the layout's authored anchor
 * when the YAML listed it. Drawers call this for every snapshot HUD
 * instrument position they consume. With no layout / matched frame,
 * returns the inputs verbatim (the 4:3 path is fast-and-explicit, not
 * the silent fast-path of a hidden snapshot copy). */
static inline void TieCockpitRenderer_InsAnchor(const TieCockpitRendererEntry* entry, int id, uint16_t snap_x,
												uint16_t snap_y, const TieSnapshot* snap, float* out_x,
												float* out_y) {
	TieCockpitLayout_Anchor(entry ? &entry->layout : NULL, id, (int16_t)snap_x, (int16_t)snap_y,
							(int)snap->cockpit.classic_w, (int)snap->cockpit.classic_h, out_x, out_y);
}

/* PIP rect resolved into the cockpit's coord frame. Prefers the
 * layout's authored override; otherwise piecewise-rescales the
 * supplied classic-frame rect via TieCockpitRenderer_InsAnchor. */
static inline void TieCockpitRenderer_ResolvePipRect(const TieCockpitRendererEntry* entry,
													 const TieSnapshot* snap, uint16_t src_x, uint16_t src_y,
													 uint16_t src_w, uint16_t src_h, float* out_x,
													 float* out_y, float* out_w, float* out_h) {
	if (entry && entry->layout.pip_present) {
		*out_x = (float)entry->layout.pip_x;
		*out_y = (float)entry->layout.pip_y;
		*out_w = (float)entry->layout.pip_w;
		*out_h = (float)entry->layout.pip_h;
		return;
	}
	float lo_x, lo_y, hi_x, hi_y;
	TieCockpitRenderer_InsAnchor(entry, -1, src_x, src_y, snap, &lo_x, &lo_y);
	TieCockpitRenderer_InsAnchor(entry, -1, (uint16_t)(src_x + src_w), (uint16_t)(src_y + src_h), snap, &hi_x,
								 &hi_y);
	*out_x = lo_x;
	*out_y = lo_y;
	*out_w = hi_x - lo_x;
	*out_h = hi_y - lo_y;
}

/* Cockpit-text atlas scale. `atlas_scale_x` is atlas-pixels per
 * coord-frame-pixel: glyph_coord_h = atlas_cell_h / atlas_scale.
 * MICRO64 was extracted at 4.5× SVGA (atlas cells take ~14 classic px
 * at coord_h=480). To keep glyphs the same screen fraction when coord_h
 * grows, scale DOWN proportionally — at coord_h=2160 atlas_scale=1.0
 * gives 64-px-tall ref glyphs (~3% of frame, same as 14/480 in 4:3).
 * YAML may override. */
static inline float TieCockpitRenderer_CockpitAtlasScaleFor(const TieCockpitRendererEntry* entry,
															int coord_h) {
	if (entry && entry->layout.font_atlas_scale > 0.0f)
		return entry->layout.font_atlas_scale;
	if (coord_h <= 0)
		return 4.5f;
	return 4.5f * 480.0f / (float)coord_h;
}

/* Lazy-load with retry suppression within the active generation. */
static AeronTexture* TieCockpitRenderer_EnsureAsset(TieCockpitRenderer* cg, AeronCommandBuffer* cmd,
													AeronTexture** tex, bool* tried, int* out_w, int* out_h,
													const char* path) {
	(void)cg;
	if (*tex)
		return *tex;
	if (*tried)
		return NULL;
	*tried = true;
	*tex = TieCockpitRenderer_LoadKtx2(cg, cmd, path, out_w, out_h);
	return *tex;
}

static bool TieCockpitRenderer_CockpitAssetErrorImpl(TieCockpitRenderer* cg, const char* path,
													 const char* detail) {
	char message[768];
	snprintf(message, sizeof message, "flight model source %s: required cockpit asset %s: %s",
			 cg && cg->assets ? cg->assets->name : "invalid", path ? path : "(memory)",
			 detail ? detail : "failed");
	Aeron_RequestFatalError("Flight Asset Error", message);
	return false;
}

static bool TieCockpitRenderer_ReadOriginalFile(TieCockpitRenderer* cg, const char* path, size_t maximum,
												uint8_t** out, size_t* out_size) {
	if (cg && cg->draw_phase_active)
		++cg->draw_phase_asset_io_count;
	if (!cg || !cg->assets ||
		!AeronVfs_ReadAll(cg->assets->vfs, AERON_VFS_ROOT_ASSET, path, maximum, out, out_size))
		return TIE_COCKPIT_ASSET_ERROR(path, "cannot read from ASSET");
	return true;
}

static bool TieCockpitRenderer_EnsureOriginalPalette(TieCockpitRenderer* cg) {
	if (cg->original_vga_palette_loaded)
		return true;
	uint8_t* bytes = NULL;
	size_t size = 0;
	if (!TieCockpitRenderer_ReadOriginalFile(cg, "VGA.PAC", 1024u * 1024u, &bytes, &size))
		return false;
	if (size < sizeof cg->original_vga_palette) {
		free(bytes);
		return TIE_COCKPIT_ASSET_ERROR("VGA.PAC", "flight palette is shorter than 576 bytes");
	}
	memcpy(cg->original_vga_palette, bytes, sizeof cg->original_vga_palette);
	free(bytes);
	cg->original_vga_palette_loaded = true;
	return true;
}

static bool TieCockpitRenderer_InstallOriginalFont(TieCockpitRenderer* cg, AeronCommandBuffer* cmd,
												   const char* path, uint8_t row_bytes, uint8_t slot_id) {
	uint8_t* bytes = NULL;
	size_t size = 0;
	TieCockpitFontAtlas decoded = { 0 };
	TieFormatError codec_error = { 0 };
	if (!TieCockpitRenderer_ReadOriginalFile(cg, path, 1024u * 1024u, &bytes, &size))
		return false;
	if (!TieCockpitFont_Decode(bytes, size, row_bytes, &decoded, &codec_error)) {
		free(bytes);
		return TIE_COCKPIT_ASSET_ERROR(path, codec_error.message);
	}
	free(bytes);
	int scaled_width = 0, scaled_height = 0;
	uint8_t* scaled = Aeron_ImageUpscaleNearestRgba8(decoded.rgba, decoded.width, decoded.height, 4,
													 &scaled_width, &scaled_height);
	if (!scaled) {
		TieCockpitFont_Free(&decoded);
		return TIE_COCKPIT_ASSET_ERROR(path, "4x nearest atlas expansion failed");
	}
	AeronFontGlyph* glyphs = calloc(decoded.glyph_count, sizeof *glyphs);
	if (!glyphs) {
		free(scaled);
		TieCockpitFont_Free(&decoded);
		return TIE_COCKPIT_ASSET_ERROR(path, "glyph metric allocation failed");
	}
	bool valid = decoded.width <= UINT16_MAX / 4 && decoded.height <= UINT16_MAX / 4 &&
				 decoded.cell_w <= UINT16_MAX / 4 && decoded.cell_h <= UINT16_MAX / 4 &&
				 decoded.baseline <= UINT16_MAX / 4;
	for (uint16_t index = 0; valid && index < decoded.glyph_count; ++index) {
		const TieCockpitFontGlyph* source = &decoded.glyphs[index];
		valid = source->atlas_x <= UINT16_MAX / 4 && source->atlas_y <= UINT16_MAX / 4 &&
				source->atlas_w <= UINT16_MAX / 4 && source->atlas_h <= UINT16_MAX / 4 &&
				source->advance <= UINT16_MAX / 4;
		glyphs[index] = (AeronFontGlyph) {
			.atlas_x = (uint16_t)(source->atlas_x * 4),
			.atlas_y = (uint16_t)(source->atlas_y * 4),
			.atlas_w = (uint16_t)(source->atlas_w * 4),
			.atlas_h = (uint16_t)(source->atlas_h * 4),
			.advance = (uint16_t)(source->advance * 4),
		};
	}
	if (!valid) {
		free(glyphs);
		free(scaled);
		TieCockpitFont_Free(&decoded);
		return TIE_COCKPIT_ASSET_ERROR(path, "4x font metrics exceed 16-bit limits");
	}
	const AeronFontAtlasRgba8Desc descriptor = {
		.pixels = scaled,
		.width = scaled_width,
		.height = scaled_height,
		.pitch = (size_t)scaled_width * 4,
		.first_char = decoded.first_char,
		.cell_w = (uint16_t)(decoded.cell_w * 4),
		.cell_h = (uint16_t)(decoded.cell_h * 4),
		.baseline = (uint16_t)(decoded.baseline * 4),
		.glyphs = glyphs,
		.glyph_count = decoded.glyph_count,
		.format = AERON_TEXTURE_FORMAT_RGBA8_UNORM,
		.color_space = AERON_COLOR_SPACE_LINEAR_SRGB,
		.alpha_mode = AERON_IMAGE_ALPHA_STRAIGHT,
		.generate_mips = true,
		.debug_name = path,
	};
	const bool loaded = TieScene2dTextRenderer_LoadFontRgba8(cg->text_renderer, cmd, slot_id, &descriptor);
	free(glyphs);
	free(scaled);
	TieCockpitFont_Free(&decoded);
	return loaded || TIE_COCKPIT_ASSET_ERROR(path, "font atlas GPU creation failed");
}

static bool TieCockpitRenderer_EnsureOriginalFonts(TieCockpitRenderer* cg, AeronCommandBuffer* cmd,
												   uint16_t classic_width) {
	if (cg->original_font_width == classic_width)
		return true;
	const bool svga = classic_width == 640;
	if (!svga && classic_width != 320)
		return TIE_COCKPIT_ASSET_ERROR("cockpit fonts", "unsupported classic width");
	if (!TieCockpitRenderer_InstallOriginalFont(cg, cmd, svga ? "MICRO64.FNT" : "MICRO.FNT", svga ? 4 : 1,
												TIE_SCENE2D_TEXT_RENDERER_FONT_SLOT_COCKPIT_MICRO) ||
		!TieCockpitRenderer_InstallOriginalFont(cg, cmd, svga ? "TINY64.FNT" : "TINY.FNT", svga ? 4 : 1,
												TIE_SCENE2D_TEXT_RENDERER_FONT_SLOT_COCKPIT_TINY))
		return false;
	/* InstallOriginalFont expands both source variants and their metrics by 4x. */
	cg->original_font_atlas_scale = 4.0f;
	cg->original_font_width = classic_width;
	return true;
}

static bool TieCockpitRenderer_PrepareOriginalBase(TieCockpitRenderer* cg, AeronCommandBuffer* cmd,
												   TieCockpitRendererEntry* entry, const TieSnapshot* snap,
												   uint8_t palette[768]) {
	char path[64];
	snprintf(path, sizeof path, "%s/%s.LFD", snap->cockpit.classic_w == 640 ? "CP640" : "CP320",
			 snap->cockpit.view_name);
	uint8_t* bytes = NULL;
	size_t size = 0;
	TiePanel panel = { 0 };
	TieFormatError codec_error = { 0 };
	TieRgbaFrame base = { 0 };
	TieCockpitCoverage coverage = { 0 };
	if (!TieCockpitRenderer_ReadOriginalFile(cg, path, 64u * 1024u * 1024u, &bytes, &size))
		return false;
	if (!TiePanel_Parse(bytes, size, &panel, &codec_error))
		goto failed;
	const TiePanelSection* panl = TiePanel_Find(&panel, TIE_FOURCC('P', 'A', 'N', 'L'));
	const TiePanelSection* mask = TiePanel_Find(&panel, TIE_FOURCC('M', 'A', 'S', 'K'));
	const TiePanelSection* pltt = TiePanel_Find(&panel, TIE_FOURCC('P', 'L', 'T', 'T'));
	if (!panl || !mask || !pltt) {
		snprintf(codec_error.message, sizeof codec_error.message,
				 "requires exactly one PANL, MASK, and PLTT section");
		goto failed;
	}
	int complete_rows = 0;
	if (!TieCockpitBase_Build(panl->data, panl->size, mask->data, mask->size, pltt->data, pltt->size,
							  cg->original_vga_palette, sizeof cg->original_vga_palette, snap->cockpit.view_x,
							  snap->cockpit.view_y, snap->cockpit.view_width, snap->cockpit.view_depth, &base,
							  palette, &complete_rows, &codec_error))
		goto failed;
	(void)complete_rows;
	if (!TieCockpitCoverage_Build(base.rgba, base.width, base.height, &coverage, &codec_error))
		goto failed;
	entry->base_tex = Aeron_ImageUploadRgba8(cmd, base.rgba, base.width, base.height, (size_t)base.width * 4,
											 AERON_TEXTURE_FORMAT_RGBA8_SRGB, AERON_COLOR_SPACE_SRGB,
											 AERON_IMAGE_ALPHA_STRAIGHT, true, path);
	if (!entry->base_tex) {
		snprintf(codec_error.message, sizeof codec_error.message, "base bitmap GPU upload failed");
		goto failed;
	}
	entry->base_w = base.width;
	entry->base_h = base.height;
	entry->cover_rects = coverage.rects;
	entry->cover_count = (int)coverage.count;
	entry->cover_img_w = coverage.image_width;
	entry->cover_img_h = coverage.image_height;
	entry->tried_cover = true;
	memset(&coverage, 0, sizeof coverage);
	entry->tried_base = true;
	TiePanel_Free(&panel);
	free(base.rgba);
	free(bytes);
	return true;

failed:
	TiePanel_Free(&panel);
	TieCockpitCoverage_Free(&coverage);
	free(base.rgba);
	free(bytes);
	return TIE_COCKPIT_ASSET_ERROR(path, codec_error.message);
}

static bool TieCockpitRenderer_PrepareOriginalParts(TieCockpitRenderer* cg, AeronCommandBuffer* cmd,
													TieCockpitRendererEntry* entry, const TieSnapshot* snap,
													const uint8_t palette[768]) {
	char path[64];
	snprintf(path, sizeof path, "%s/%sP.PNL", snap->cockpit.classic_w == 640 ? "CP640" : "CP320",
			 snap->cockpit.parts_basename);
	uint8_t* bytes = NULL;
	size_t size = 0;
	TieShapeList shapes = { 0 };
	TieRgbaFrames decoded = { 0 };
	TieFormatError codec_error = { 0 };
	uint16_t* skips = NULL;
	AeronRuntimeAtlasFrame* frames = NULL;
	if (!TieCockpitRenderer_ReadOriginalFile(cg, path, 64u * 1024u * 1024u, &bytes, &size))
		return false;
	if (!TieShapeList_Parse(bytes, size, snap->cockpit.parts_shape_count, &shapes, &codec_error))
		goto failed;
	skips = calloc(shapes.count, sizeof *skips);
	frames = calloc(shapes.count, sizeof *frames);
	if (!skips || !frames) {
		snprintf(codec_error.message, sizeof codec_error.message, "parts atlas allocation failed");
		goto failed;
	}
	TieCockpitPartInstrument instruments[TIE_MAX_HUD_INSTRUMENTS];
	for (uint32_t index = 0; index < TIE_MAX_HUD_INSTRUMENTS; ++index) {
		const TieHudInstrument* source = &snap->hud.instruments[index];
		instruments[index] = (TieCockpitPartInstrument) {
			.x = source->x,
			.y = source->y,
			.param1 = source->param1,
			.param2 = source->param2,
		};
	}
	if (!TieCockpitPartTransparency_Build(instruments, TIE_MAX_HUD_INSTRUMENTS, shapes.count, skips,
										  &codec_error) ||
		!TieCockpitShapeFrames_Build(&shapes, palette, skips, 253, &decoded, &codec_error))
		goto failed;
	for (uint32_t index = 0; index < shapes.count; ++index) {
		TieRgbaFrame* frame = &decoded.frames[index];
		frames[index] = (AeronRuntimeAtlasFrame) {
			.rgba = frame->rgba,
			.width = frame->width,
			.height = frame->height,
			.id = (int32_t)index,
		};
	}
	const AeronRuntimeAtlasOptions options = {
		.format = AERON_TEXTURE_FORMAT_RGBA8_SRGB,
		.color_space = AERON_COLOR_SPACE_SRGB,
		.alpha_mode = AERON_IMAGE_ALPHA_STRAIGHT,
		.generate_mips = true,
		.debug_name = path,
	};
	if (!Aeron_RuntimeAtlasBuild(&entry->original_parts, cmd, frames, (int)shapes.count, &options) ||
		entry->original_parts.layout.page_count != 1) {
		snprintf(codec_error.message, sizeof codec_error.message,
				 "parts GPU atlas creation requires exactly one page");
		goto failed;
	}
	entry->layout.reference_w = snap->cockpit.classic_w;
	entry->layout.reference_h = snap->cockpit.classic_h;
	entry->layout.atlas_w = entry->original_parts.pages[0].width;
	entry->layout.atlas_h = entry->original_parts.pages[0].height;
	entry->layout.cels = calloc(shapes.count, sizeof *entry->layout.cels);
	if (!entry->layout.cels) {
		snprintf(codec_error.message, sizeof codec_error.message, "parts layout allocation failed");
		goto failed;
	}
	entry->layout.cel_count = (int)shapes.count;
	entry->layout.font_atlas_scale = 4.0f;
	for (uint32_t index = 0; index < shapes.count; ++index) {
		const AeronSpriteRect* source = &entry->original_parts.layout.frames[index];
		entry->layout.cels[index] = (TieCockpitLayoutCel) {
			.atlas_x = (int)source->x,
			.atlas_y = (int)source->y,
			.atlas_w = (int)source->w,
			.atlas_h = (int)source->h,
			.size_w = decoded.frames[index].width,
			.size_h = decoded.frames[index].height,
		};
	}
	entry->parts_tex = entry->original_parts.pages[0].texture;
	entry->parts_w = entry->original_parts.pages[0].width;
	entry->parts_h = entry->original_parts.pages[0].height;
	entry->tried_parts = entry->tried_layout = true;
	entry->original_assets = true;
	free(frames);
	free(skips);
	TieRgbaFrames_Free(&decoded);
	TieShapeList_Free(&shapes);
	free(bytes);
	return true;

failed:
	Aeron_RuntimeAtlasRelease(&entry->original_parts);
	TieCockpitLayout_Free(&entry->layout);
	free(frames);
	free(skips);
	TieRgbaFrames_Free(&decoded);
	TieShapeList_Free(&shapes);
	free(bytes);
	return TIE_COCKPIT_ASSET_ERROR(path, codec_error.message);
}

static bool TieCockpitRenderer_EnsureOriginalCockpitAssets(TieCockpitRenderer* cg, AeronCommandBuffer* cmd,
														   TieCockpitRendererEntry* entry,
														   const TieSnapshot* snap) {
	if (!TieCockpitRenderer_EnsureOriginalPalette(cg) ||
		!TieCockpitRenderer_EnsureOriginalFonts(cg, cmd, snap->cockpit.classic_w))
		return false;
	uint8_t palette[768];
	if (!entry->base_tex) {
		if (!TieCockpitRenderer_PrepareOriginalBase(cg, cmd, entry, snap, palette))
			return false;
	} else {
		/* Parts are prepared in the same call as the base and do not need the
		 * palette again on subsequent frames. */
		return entry->parts_tex != NULL;
	}
	if (!snap->cockpit.parts_basename[0] || !snap->cockpit.parts_shape_count)
		return TIE_COCKPIT_ASSET_ERROR("cockpit parts", "snapshot has no parts identity");
	return TieCockpitRenderer_PrepareOriginalParts(cg, cmd, entry, snap, palette);
}

static bool TieCockpitRenderer_EnsureCockpitAssets(TieCockpitRenderer* cg, AeronCommandBuffer* cmd,
												   TieCockpitRendererEntry* entry, const TieSnapshot* snap) {
	if (cg->assets && !TieFlightAssetSource_IsRemastered(cg->assets))
		return TieCockpitRenderer_EnsureOriginalCockpitAssets(cg, cmd, entry, snap);
	const TieFlightAssetBundle* catalog = TieCockpitRenderer_Catalog(cg);
	if (!catalog)
		return TIE_COCKPIT_ASSET_ERROR("tie_remaster/flight/assets.yaml",
									   "GLB cockpit catalog is unavailable");
	char path[1024];

	/* Parts atlas + hud_layout.yaml — keyed by parts_basename. Loaded
	 * BEFORE the base bitmap so the layout's reference frame can
	 * select between HD and 4:3 bitmap entries. */
	const char* parts_base = snap->cockpit.parts_basename;
	if (parts_base[0]) {
		const TieFlightAssetCockpitParts* cp = TieFlightAssets_CockpitParts(catalog, parts_base);
		if (!cp)
			return TIE_COCKPIT_ASSET_ERROR(parts_base, "catalog has no cockpit_parts row");
		{
			if (TieCockpitRenderer_ComposeFlightPath(cg, cp->atlas, path, sizeof path))
				TieCockpitRenderer_EnsureAsset(cg, cmd, &entry->parts_tex, &entry->tried_parts,
											   &entry->parts_w, &entry->parts_h, path);
			if (!entry->tried_layout && cp->layout[0] &&
				TieCockpitRenderer_ComposeFlightPath(cg, cp->layout, path, sizeof path)) {
				if (!TieCockpitLayout_Load(&entry->layout, cg->assets->vfs, AERON_VFS_ROOT_ASSET, path))
					return TIE_COCKPIT_ASSET_ERROR(path, "layout decode failed");
				entry->tried_layout = true;
			}
		}
	} else
		return TIE_COCKPIT_ASSET_ERROR("cockpit parts", "snapshot has no parts basename");
	if (!entry->parts_tex)
		return TIE_COCKPIT_ASSET_ERROR(parts_base, "parts texture upload failed");

	const TieFlightAssetCockpitView* cv = TieFlightAssets_CockpitView(catalog, entry->view_name);
	if (!cv)
		return TIE_COCKPIT_ASSET_ERROR(entry->view_name, "catalog has no cockpit_views row");

	/* Select the catalog bitmap that matches the authored layout aspect. */
	if (!entry->base_tex && !entry->tried_base) {
		const bool ref_is_hd = entry->layout.reference_w > 0 && entry->layout.reference_h > 0 &&
							   !(entry->layout.reference_w == 640 && entry->layout.reference_h == 480) &&
							   !(entry->layout.reference_w == 320 && entry->layout.reference_h == 200);
		const char* selected = ref_is_hd ? cv->bitmap_hd : cv->bitmap_4_3;
		if (selected[0] && TieCockpitRenderer_ComposeFlightPath(cg, selected, path, sizeof path)) {
			TieCockpitRenderer_EnsureAsset(cg, cmd, &entry->base_tex, &entry->tried_base, &entry->base_w,
										   &entry->base_h, path);
		} else {
			entry->tried_base = true;
		}
		/* Coverage mesh for the base bitmap. `path` still holds the base
		 * bitmap path that loaded above. */
		entry->tried_cover = true;
	}
	if (!entry->base_tex)
		return TIE_COCKPIT_ASSET_ERROR(path, "base texture upload failed");

	/* Damage overlay — optional. */
	if (cv->damage[0] && TieCockpitRenderer_ComposeFlightPath(cg, cv->damage, path, sizeof path)) {
		TieCockpitRenderer_EnsureAsset(cg, cmd, &entry->damage_tex, &entry->tried_damage, &entry->damage_w,
									   &entry->damage_h, path);
		if (!entry->damage_tex)
			return TIE_COCKPIT_ASSET_ERROR(path, "damage texture upload failed");
	}
	return true;
}

/* Full-form PMA blit. Existing widget helpers emit authored cockpit
 * coordinates; the frame-local draw space maps them to target pixels. */
static void TieCockpitRenderer_BlitCockpitQuadFull(TieCockpitRenderer* cg, AeronCommandBuffer* cmd,
												   AeronRenderPass* pass, AeronTexture* tex, int coord_w,
												   int coord_h, float dst_x, float dst_y, float dst_w,
												   float dst_h, float src_u0, float src_v0, float src_u1,
												   float src_v1, float tint_r, float tint_g, float tint_b,
												   float tint_a, float bias_r, float bias_g, float bias_b,
												   float bias_a, AeronBlit2DFilter filter) {
	(void)cmd;
	(void)pass;
	(void)coord_w;
	(void)coord_h;
	if (!cg || !cg->record_list || dst_w <= 0.0f || dst_h <= 0.0f)
		return;
	const TieCockpitRendererDrawSpace* space = &cg->pending_space;
	if (!tex) {
		const float color[4] = { tint_r + bias_r, tint_g + bias_g, tint_b + bias_b, tint_a + bias_a };
		AeronDrawList_AddFill(cg->record_list, space->fit_x + dst_x * space->scale_x,
							  space->fit_y + dst_y * space->scale_y, dst_w * space->scale_x,
							  dst_h * space->scale_y, color, AERON_BLIT2D_BLEND_PMA, NULL);
		return;
	}
	AeronDrawList2DSprite sprite = {
		.texture = tex,
		.src_u0 = src_u0,
		.src_v0 = src_v0,
		.src_u1 = src_u1,
		.src_v1 = src_v1,
		.dst_x = space->fit_x + dst_x * space->scale_x,
		.dst_y = space->fit_y + dst_y * space->scale_y,
		.dst_w = dst_w * space->scale_x,
		.dst_h = dst_h * space->scale_y,
		.tint = { tint_r, tint_g, tint_b, tint_a },
		.bias = { bias_r, bias_g, bias_b, bias_a },
		.blend = AERON_BLIT2D_BLEND_PMA,
		.filter = filter,
	};
	AeronDrawList_AddSprite(cg->record_list, &sprite);
}

/* Standard PMA-tinted blit. Equivalent to TieCockpitRenderer_BlitCockpitQuadFull with
 * zero bias — the common case for the cockpit overlay. */
static inline void TieCockpitRenderer_BlitCockpitQuad(TieCockpitRenderer* cg, AeronCommandBuffer* cmd,
													  AeronRenderPass* pass, AeronTexture* tex, int coord_w,
													  int coord_h, float dst_x, float dst_y, float dst_w,
													  float dst_h, float src_u0, float src_v0, float src_u1,
													  float src_v1, float tint_r, float tint_g, float tint_b,
													  float tint_a) {
	TieCockpitRenderer_BlitCockpitQuadFull(cg, cmd, pass, tex, coord_w, coord_h, dst_x, dst_y, dst_w, dst_h,
										   src_u0, src_v0, src_u1, src_v1, tint_r, tint_g, tint_b, tint_a,
										   0.0f, 0.0f, 0.0f, 0.0f, AERON_BLIT2D_FILTER_LINEAR);
}

/* Original cockpit parts are native-resolution pixel art. Nearest sampling
 * keeps independently drawn replacement cels aligned with matching pixels
 * embedded in larger panel cels; authored remastered atlases stay linear. */
static inline AeronBlit2DFilter TieCockpitRenderer_CockpitPartFilter(const TieCockpitRendererEntry* entry) {
	return entry && entry->original_assets ? AERON_BLIT2D_FILTER_NEAREST : AERON_BLIT2D_FILTER_LINEAR;
}

static inline void TieCockpitRenderer_BlitCockpitPartQuad(TieCockpitRenderer* cg, AeronCommandBuffer* cmd,
														  AeronRenderPass* pass,
														  const TieCockpitRendererEntry* entry, int coord_w,
														  int coord_h, float dst_x, float dst_y, float dst_w,
														  float dst_h, float src_u0, float src_v0,
														  float src_u1, float src_v1, float tint_r,
														  float tint_g, float tint_b, float tint_a) {
	TieCockpitRenderer_BlitCockpitQuadFull(cg, cmd, pass, entry ? entry->parts_tex : NULL, coord_w, coord_h,
										   dst_x, dst_y, dst_w, dst_h, src_u0, src_v0, src_u1, src_v1, tint_r,
										   tint_g, tint_b, tint_a, 0.0f, 0.0f, 0.0f, 0.0f,
										   TieCockpitRenderer_CockpitPartFilter(entry));
}

/* MONO-stencil blit — `tint=(0,0,0,1) + bias=(palette_rgb, 0)` paints
 * the palette color everywhere the source's alpha is non-zero, ignoring
 * the source's baked RGB. Engine equivalent of rtsvga2_drawmonoshape:
 * the cel acts as a mask and the palette index decides the fill color.
 * Used by shield bars and beam arc-LEDs. */
static inline void TieCockpitRenderer_BlitCockpitPartQuadMono(
	TieCockpitRenderer* cg, AeronCommandBuffer* cmd, AeronRenderPass* pass,
	const TieCockpitRendererEntry* entry, int coord_w, int coord_h, float dst_x, float dst_y, float dst_w,
	float dst_h, float src_u0, float src_v0, float src_u1, float src_v1, float r, float g, float b) {
	TieCockpitRenderer_BlitCockpitQuadFull(cg, cmd, pass, entry ? entry->parts_tex : NULL, coord_w, coord_h,
										   dst_x, dst_y, dst_w, dst_h, src_u0, src_v0, src_u1, src_v1, 0.0f,
										   0.0f, 0.0f, 1.0f, r, g, b, 0.0f,
										   TieCockpitRenderer_CockpitPartFilter(entry));
}

/* Draw the base cockpit bitmap. With a coverage mesh, draws only its opaque
 * rectangles so the transparent canopy opening is never rasterized; otherwise
 * a single fullscreen quad. `mirrored` flips horizontally (the engine's
 * mirrored_view), matching the fullscreen path's u0/u1 swap. Original assets
 * retain their decoded aspect in cockpit coordinates; authored layouts use the
 * supplied cockpit-area height. */
static void TieCockpitRenderer_DrawCockpitBase(TieCockpitRenderer* cg, AeronCommandBuffer* cmd,
											   AeronRenderPass* pass, const TieCockpitRendererEntry* entry,
											   int coord_w, int coord_h, int cockpit_area_h, bool mirrored) {
	if (!entry->base_tex)
		return;
	float base_coord_h = (float)cockpit_area_h;
	if (entry->original_assets && entry->base_w > 0 && entry->base_h > 0)
		base_coord_h = (float)entry->base_h * (float)coord_w / (float)entry->base_w;

	if (entry->cover_count <= 0 || entry->cover_img_w <= 0 || entry->cover_img_h <= 0) {
		const float u0 = mirrored ? 1.0f : 0.0f;
		const float u1 = mirrored ? 0.0f : 1.0f;
		TieCockpitRenderer_BlitCockpitQuad(cg, cmd, pass, entry->base_tex, coord_w, coord_h, 0.0f, 0.0f,
										   (float)coord_w, base_coord_h, u0, 0.0f, u1, 1.0f, 1.0f, 1.0f, 1.0f,
										   1.0f);
		return;
	}

	const float iw = (float)entry->cover_img_w;
	const float ih = (float)entry->cover_img_h;
	for (int i = 0; i < entry->cover_count; i++) {
		const TieCockpitRendererCoverRect* r = &entry->cover_rects[i];
		const float fu0 = r->x / iw, fu1 = (r->x + r->w) / iw;
		const float fv0 = r->y / ih, fv1 = (r->y + r->h) / ih;
		float dst_x, u0, u1;
		if (mirrored) { /* flip horizontally, like the fullscreen path */
			dst_x = (float)coord_w * (1.0f - fu1);
			u0 = fu1;
			u1 = fu0;
		} else {
			dst_x = (float)coord_w * fu0;
			u0 = fu0;
			u1 = fu1;
		}
		TieCockpitRenderer_BlitCockpitQuad(cg, cmd, pass, entry->base_tex, coord_w, coord_h, dst_x,
										   base_coord_h * fv0, (float)coord_w * (fu1 - fu0),
										   base_coord_h * (fv1 - fv0), u0, fv0, u1, fv1, 1.0f, 1.0f, 1.0f,
										   1.0f);
	}
}

/* Fixed-grid cel UVs — legacy fallback used only when the per-craft
 * hud_layout.yaml is missing. */
static void TieCockpitRenderer_AtlasCelUv(int atlas_w, int atlas_h, int cel_w, int cel_h, int cols, int frame,
										  float* u0, float* v0, float* u1, float* v1) {
	int x = (frame % cols) * cel_w;
	int y = (frame / cols) * cel_h;
	*u0 = (float)x / (float)atlas_w;
	*v0 = (float)y / (float)atlas_h;
	*u1 = (float)(x + cel_w) / (float)atlas_w;
	*v1 = (float)(y + cel_h) / (float)atlas_h;
}

/* Resolve a parts-atlas cel via the per-craft layout; falls back to
 * the fixed-grid heuristic only when no layout is loaded. */
static bool TieCockpitRenderer_ResolveCel(const TieCockpitRendererEntry* entry, int frame, float* u0,
										  float* v0, float* u1, float* v1, int* out_w_classic,
										  int* out_h_classic) {
	if (!entry || !entry->parts_tex)
		return false;
	const TieCockpitLayoutCel* c = TieCockpitLayout_Cel(&entry->layout, frame);
	if (c) {
		*u0 = (float)c->atlas_x / (float)entry->parts_w;
		*v0 = (float)c->atlas_y / (float)entry->parts_h;
		*u1 = (float)(c->atlas_x + c->atlas_w) / (float)entry->parts_w;
		*v1 = (float)(c->atlas_y + c->atlas_h) / (float)entry->parts_h;
		*out_w_classic = c->size_w;
		*out_h_classic = c->size_h;
		return true;
	}
	if (entry->layout.cel_count > 0)
		return false;
	TieCockpitRenderer_AtlasCelUv(entry->parts_w, entry->parts_h, COCKPIT_CEL_W, COCKPIT_CEL_H,
								  COCKPIT_CELS_PER_ROW, frame, u0, v0, u1, v1);
	*out_w_classic = COCKPIT_CEL_W;
	*out_h_classic = COCKPIT_CEL_H;
	return true;
}

/* Blit one parts-atlas cel at (x, y) in classic-cockpit coords with the
 * cel's authored dimensions. `hdr_boost` multiplies the sampled RGB
 * before the PMA blend — pass 1.0 for the engine-faithful "as authored"
 * look, > 1.0 to push the cel above the bloom threshold. Alpha stays
 * neutral so cutouts behave the same regardless of boost.
 *
 * Returns false when the parts atlas is missing or the layout doesn't
 * resolve `frame`. Funnel for the widget drawers that all open-coded
 * the same `TieCockpitRenderer_ResolveCel → TieCockpitRenderer_BlitCockpitQuad` sequence. */
static bool TieCockpitRenderer_BlitInstrumentCel(TieCockpitRenderer* cg, AeronCommandBuffer* cmd,
												 AeronRenderPass* pass, const TieCockpitRendererEntry* entry,
												 int coord_w, int coord_h, float x, float y, int frame,
												 float hdr_boost) {
	if (!entry || !entry->parts_tex)
		return false;
	float u0, v0, u1, v1;
	int cw, ch;
	if (!TieCockpitRenderer_ResolveCel(entry, frame, &u0, &v0, &u1, &v1, &cw, &ch))
		return false;
	TieCockpitRenderer_BlitCockpitPartQuad(cg, cmd, pass, entry, coord_w, coord_h, x, y, (float)cw, (float)ch,
										   u0, v0, u1, v1, hdr_boost, hdr_boost, hdr_boost, 1.0f);
	return true;
}

/* HUD widget dispatch table, indexed by instrument index.
 * Unlisted indices fall through to HUD_W_NONE and skip. */
typedef enum {
	HUD_W_NONE = 0,
	HUD_W_SHAPE_LEVER,
	HUD_W_DIGIT_FIELD,
	HUD_W_VERT_SLIDER,
	HUD_W_LASER_LED_ROW,
	HUD_W_DAMAGE_CRACK,
	HUD_W_COVER,
	HUD_W_SHIELD_QUAD,
	HUD_W_BEAM_ARCBAR,
	HUD_W_RADAR_DISC,
	HUD_W_3DCRT,
	HUD_W_TEXT_FIELD,
	HUD_W_STATIC_LABEL,
	HUD_W_RECORDING_PCT,
	HUD_W_VIEW17_TITLE,
} TieCockpitRendererHudWidgetKind;

static const uint8_t hud_widget_kind[TIE_MAX_HUD_INSTRUMENTS] = {
	[TIE_HUDI_RADAR_LEFT] = HUD_W_RADAR_DISC,
	[TIE_HUDI_RADAR_RIGHT] = HUD_W_RADAR_DISC,
	[TIE_HUDI_CMD_3D_CRT] = HUD_W_3DCRT,
	[3] = HUD_W_LASER_LED_ROW,
	[4] = HUD_W_LASER_LED_ROW,
	[5] = HUD_W_LASER_LED_ROW,
	[6] = HUD_W_LASER_LED_ROW,
	[7] = HUD_W_LASER_LED_ROW,
	[8] = HUD_W_LASER_LED_ROW,
	[9] = HUD_W_LASER_LED_ROW,
	[10] = HUD_W_LASER_LED_ROW,
	/* idx 11..14 are missile-hardpoint ready levers (panel_updatehardpoint
	 * writes here via panel_updatelever(hp_idx + 11, lever_val)). */
	[TIE_HUDI_MISSILE_HP_FIRST] = HUD_W_SHAPE_LEVER,
	[TIE_HUDI_MISSILE_HP_FIRST + 1] = HUD_W_SHAPE_LEVER,
	[TIE_HUDI_MISSILE_HP_FIRST + 2] = HUD_W_SHAPE_LEVER,
	[TIE_HUDI_MISSILE_HP_LAST] = HUD_W_SHAPE_LEVER,
	/* idx 15..18 are the ammo-digit positions next to each hardpoint
	 * (panel_updatehardpoint writes via panel_updatevalue, text-pass). */
	[15] = HUD_W_DIGIT_FIELD,
	[16] = HUD_W_DIGIT_FIELD,
	[17] = HUD_W_DIGIT_FIELD,
	[18] = HUD_W_DIGIT_FIELD,
	[TIE_HUDI_SHIELD_FWD_NORMAL] = HUD_W_SHIELD_QUAD,
	[TIE_HUDI_SHIELD_FWD_OVER] = HUD_W_SHIELD_QUAD,
	[TIE_HUDI_SHIELD_REAR_NORMAL] = HUD_W_SHIELD_QUAD,
	[TIE_HUDI_SHIELD_REAR_OVER] = HUD_W_SHIELD_QUAD,
	[TIE_HUDI_HULL_DAMAGE_LEVER] = HUD_W_SHAPE_LEVER,
	[TIE_HUDI_SPEED_DIGITS] = HUD_W_DIGIT_FIELD,
	[TIE_HUDI_THROTTLE_DIGITS] = HUD_W_DIGIT_FIELD,
	[TIE_HUDI_POWER_BALANCE] = HUD_W_VERT_SLIDER,
	[TIE_HUDI_POWER_LASERS] = HUD_W_VERT_SLIDER,
	[TIE_HUDI_POWER_SHIELDS] = HUD_W_VERT_SLIDER,
	[TIE_HUDI_POWER_BEAM] = HUD_W_VERT_SLIDER,
	[TIE_HUDI_CLOCK_DIGITS] = HUD_W_DIGIT_FIELD,
	[TIE_HUDI_REC_LED] = HUD_W_SHAPE_LEVER,
	[TIE_HUDI_REC_PCT] = HUD_W_RECORDING_PCT,
	[TIE_HUDI_VIEW17_TITLE] = HUD_W_VIEW17_TITLE,
	[TIE_HUDI_BEAM_ARC] = HUD_W_BEAM_ARCBAR,
	[TIE_HUDI_GUNSIGHT] = HUD_W_SHAPE_LEVER,
	[37] = HUD_W_SHAPE_LEVER,
	[38] = HUD_W_SHAPE_LEVER,
	[39] = HUD_W_SHAPE_LEVER,
	[40] = HUD_W_SHAPE_LEVER,
	[41] = HUD_W_SHAPE_LEVER,
	[42] = HUD_W_SHAPE_LEVER,
	[43] = HUD_W_SHAPE_LEVER,
	[44] = HUD_W_SHAPE_LEVER,
	/* 45..57: 13 subsystem damage-crack overlays
	 * (panel_updatecockpitdamage). */
	[TIE_HUDI_DAMAGE_CRACK_FIRST + 0] = HUD_W_DAMAGE_CRACK,
	[TIE_HUDI_DAMAGE_CRACK_FIRST + 1] = HUD_W_DAMAGE_CRACK,
	[TIE_HUDI_DAMAGE_CRACK_FIRST + 2] = HUD_W_DAMAGE_CRACK,
	[TIE_HUDI_DAMAGE_CRACK_FIRST + 3] = HUD_W_DAMAGE_CRACK,
	[TIE_HUDI_DAMAGE_CRACK_FIRST + 4] = HUD_W_DAMAGE_CRACK,
	[TIE_HUDI_DAMAGE_CRACK_FIRST + 5] = HUD_W_DAMAGE_CRACK,
	[TIE_HUDI_DAMAGE_CRACK_FIRST + 6] = HUD_W_DAMAGE_CRACK,
	[TIE_HUDI_DAMAGE_CRACK_FIRST + 7] = HUD_W_DAMAGE_CRACK,
	[TIE_HUDI_DAMAGE_CRACK_FIRST + 8] = HUD_W_DAMAGE_CRACK,
	[TIE_HUDI_DAMAGE_CRACK_FIRST + 9] = HUD_W_DAMAGE_CRACK,
	[TIE_HUDI_DAMAGE_CRACK_FIRST + 10] = HUD_W_DAMAGE_CRACK,
	[TIE_HUDI_DAMAGE_CRACK_FIRST + 11] = HUD_W_DAMAGE_CRACK,
	[TIE_HUDI_DAMAGE_CRACK_LAST] = HUD_W_DAMAGE_CRACK,
	[TIE_HUDI_TARGET_SUBSYSTEM_PCT] = HUD_W_DIGIT_FIELD,
	[59] = HUD_W_DIGIT_FIELD,
	[60] = HUD_W_DIGIT_FIELD,
	[TIE_HUDI_TARGET_SHIELD_PCT] = HUD_W_DIGIT_FIELD,
	[TIE_HUDI_TARGET_HULL_PCT] = HUD_W_DIGIT_FIELD,
	[63] = HUD_W_TEXT_FIELD,
	[64] = HUD_W_TEXT_FIELD,
	[65] = HUD_W_TEXT_FIELD,
	[TIE_HUDI_WARN_INCOMING] = HUD_W_SHAPE_LEVER,
	[TIE_HUDI_WARN_LOCK] = HUD_W_SHAPE_LEVER,
	[TIE_HUDI_WARN_IMPACT] = HUD_W_SHAPE_LEVER,
	[69] = HUD_W_TEXT_FIELD,
	[70] = HUD_W_TEXT_FIELD,
	[71] = HUD_W_DIGIT_FIELD,
	[72] = HUD_W_DIGIT_FIELD,
	[TIE_HUDI_THREAT_ION] = HUD_W_SHAPE_LEVER,
	[TIE_HUDI_THREAT_TORP] = HUD_W_SHAPE_LEVER,
	[TIE_HUDI_THREAT_MISSILE] = HUD_W_SHAPE_LEVER,
	[TIE_HUDI_THREAT_BEAM] = HUD_W_SHAPE_LEVER,
	[TIE_HUDI_THREAT_SHIELD_PCT] = HUD_W_DIGIT_FIELD,
	[TIE_HUDI_THREAT_HULL_PCT] = HUD_W_DIGIT_FIELD,
	[79] = HUD_W_TEXT_FIELD,
	[80] = HUD_W_TEXT_FIELD,
	[81] = HUD_W_DIGIT_FIELD,
	[82] = HUD_W_DIGIT_FIELD,
	/* 83..85: drop-down covers over shield/beam LED zones
	 * (panel_updatecovers, when subsystem is inactive). */
	[TIE_HUDI_COVER_SHIELDS] = HUD_W_COVER,
	[TIE_HUDI_COVER_BEAM_UP] = HUD_W_COVER,
	[TIE_HUDI_COVER_BEAM_DOWN] = HUD_W_COVER,
	[86] = HUD_W_STATIC_LABEL,
	[87] = HUD_W_STATIC_LABEL,
	[88] = HUD_W_STATIC_LABEL,
	[89] = HUD_W_STATIC_LABEL,
	[90] = HUD_W_TEXT_FIELD,
	[TIE_HUDI_BEAM_FIRE] = HUD_W_SHAPE_LEVER,
};

/* Per-widget cel offset added to instrument.param1 by the drawers.
 * Each source is the snapshot field listed inline; the descriptors
 * walk the unusual ones. */
static int TieCockpitRenderer_HudShapeValue(int idx, const TieSnapshot* snap) {
	const int16_t v = snap->hud.instruments[idx].value;
	return v < 0 ? 0 : v;
}

/* ===== Drawer dispatch ===== */

static void TieCockpitRenderer_DrawShapeLever(TieCockpitRenderer* cg, AeronCommandBuffer* cmd,
											  AeronRenderPass* pass, const TieCockpitRendererEntry* entry,
											  int coord_w, int coord_h, int idx, const TieSnapshot* snap) {
	if (!entry->parts_tex)
		return;

	/* Per-instrument subsystem gates matching the engine's writer
	 * early-returns. Without these, levers paint frame 0 over zones the
	 * cockpit cover (instruments 83..85) is hiding. */
	const uint16_t cap = snap->hud.working_subsystems;
	if (idx == TIE_HUDI_BEAM_FIRE) {
		/* panel_updatebeam: gated on working_subsystems & 0x10. */
		if ((cap & 0x10u) == 0)
			return;
	} else if (idx == TIE_HUDI_HULL_DAMAGE_LEVER) {
		/* panel_updateshields: gated on working_subsystems & 0x20. */
		if ((cap & 0x20u) == 0)
			return;
	} else if (idx >= TIE_HUDI_WEAPON_FIRE_FIRST && idx <= TIE_HUDI_WEAPON_FIRE_LAST) {
		/* panel_updatelasers: gated on bits 0x02 AND 0x04. */
		if ((cap & 0x06u) != 0x06u)
			return;
	}

	int value = TieCockpitRenderer_HudShapeValue(idx, snap);
	const TieHudInstrument* ins = &snap->hud.instruments[idx];
	float ax, ay;
	TieCockpitRenderer_InsAnchor(entry, idx, ins->x, ins->y, snap, &ax, &ay);
	TieCockpitRenderer_BlitInstrumentCel(cg, cmd, pass, entry, coord_w, coord_h, ax, ay, ins->param1 + value,
										 TieCockpitLayout_HdrBoost(&entry->layout, idx));
}

/* Subsystem-indicator pass (HUD instruments 45..57). Mirrors
 * panel_updatecockpitdamage (panel.c:623): paint one cel per installed
 * subsystem — frame 0 = intact-state plate, frame 13 = broken-state
 * plate. The engine writes these from panel_initpanel BEFORE any live
 * widget updates, so the cels sit UNDER live widgets in the
 * framebuffer. In HD we replicate the z-order by running this pass
 * before TieCockpitRenderer_DrawWidgetsForView. View 0 only (engine: `if (pilotview)
 * return;`). */
static void TieCockpitRenderer_DrawDamageCracks(TieCockpitRenderer* cg, AeronCommandBuffer* cmd,
												AeronRenderPass* pass, const TieCockpitRendererEntry* entry,
												int coord_w, int coord_h, const TieSnapshot* snap) {
	if (!entry->parts_tex)
		return;
	if (snap->cockpit.view_idx != 0)
		return;

	const uint16_t inst_mask = snap->hud.installed_subsystems; /* installed bits */
	const uint16_t cap_mask = snap->hud.working_subsystems;    /* working bits   */

	for (int bit = 0; bit < 13; ++bit) {
		uint16_t mask = (uint16_t)(1u << bit);
		if (!(inst_mask & mask))
			continue; /* not installed */

		int idx = TIE_HUDI_DAMAGE_CRACK_FIRST + bit;
		const TieHudInstrument* ins = &snap->hud.instruments[idx];
		if (!TieCockpitCommon_InstrumentActive(ins))
			continue; /* unused slot */

		int frame = ins->param1 + ((cap_mask & mask) ? 0 : 13);
		float ax, ay;
		TieCockpitRenderer_InsAnchor(entry, idx, ins->x, ins->y, snap, &ax, &ay);
		TieCockpitRenderer_BlitInstrumentCel(cg, cmd, pass, entry, coord_w, coord_h, ax, ay, frame,
											 TieCockpitLayout_HdrBoost(&entry->layout, idx));
	}
}

/* Subsystem covers (HUD instruments 83/84/85). Mirrors panel_updatecovers
 * (panel.c:605): drop a "covered" graphic over the shield-LED zone when
 * shields are inactive (SF_SHIELDS bit clear in subsystem_active), or
 * over the beam-charge zone when the tractor beam is inactive
 * (SF_TRACTOR_BEAM bit clear). Each cover has a single cel at param1.
 * View 0 only. Drawn before live widgets so the LED drawers paint on
 * top when the subsystem is active (and skip when it's not, leaving
 * the cover visible). */
static void TieCockpitRenderer_DrawCovers(TieCockpitRenderer* cg, AeronCommandBuffer* cmd,
										  AeronRenderPass* pass, const TieCockpitRendererEntry* entry,
										  int coord_w, int coord_h, const TieSnapshot* snap) {
	if (!entry->parts_tex)
		return;
	if (snap->cockpit.view_idx != 0)
		return;

	const uint16_t active = snap->hud.subsystem_active;

	/* Triplet: (idx, gating mask). */
	const struct {
		int idx;
		uint16_t mask;
	} covers[3] = {
		{ TIE_HUDI_COVER_SHIELDS, 0x0001u }, /* SF_SHIELDS */
		{ TIE_HUDI_COVER_BEAM_UP, 0x0100u }, /* SF_TRACTOR_BEAM */
		{ TIE_HUDI_COVER_BEAM_DOWN, 0x0100u },
	};

	for (int i = 0; i < 3; ++i) {
		if (active & covers[i].mask)
			continue; /* subsystem on → no cover */
		const TieHudInstrument* ins = &snap->hud.instruments[covers[i].idx];
		if (!TieCockpitCommon_InstrumentActive(ins))
			continue; /* unused slot */
		/* engine always writes value=0 — cel = param1 verbatim */
		float ax, ay;
		TieCockpitRenderer_InsAnchor(entry, covers[i].idx, ins->x, ins->y, snap, &ax, &ay);
		TieCockpitRenderer_BlitInstrumentCel(cg, cmd, pass, entry, coord_w, coord_h, ax, ay, ins->param1,
											 TieCockpitLayout_HdrBoost(&entry->layout, covers[i].idx));
	}
}

/* ===== Chrome cache (static cockpit layer) ===== */

/* (Re)create the chrome RT at the flight RT's full resolution. Returns
 * false if creation fails (caller falls back to the per-frame direct
 * draws). A dimension change drops the old RT and invalidates the bake. */
static bool TieCockpitRenderer_EnsureChromeRt(TieCockpitRenderer* cg, int rt_w, int rt_h) {
	if (rt_w <= 0 || rt_h <= 0)
		return false;
	if (cg->chrome_rt && cg->chrome_w == rt_w && cg->chrome_h == rt_h)
		return true;
	if (cg->chrome_rt) {
		Aeron_DestroyRenderTarget(cg->chrome_rt);
		cg->chrome_rt = NULL;
	}
	cg->chrome_valid = false;
	cg->chrome_rt = Aeron_CreateRenderTarget(&(AeronRenderTargetDesc) {
		.width = rt_w,
		.height = rt_h,
		.format = COCKPIT_CHROME_FORMAT,
		.sample_count = AERON_SAMPLE_COUNT_1,
	});
	if (!cg->chrome_rt) {
		Aeron_RequestFatalRendererError("cockpit chrome target creation");
		return false;
	}
	cg->chrome_w = rt_w;
	cg->chrome_h = rt_h;
	return true;
}

/* True when the baked chrome still matches the live inputs. The static
 * layer changes only on view/craft switch, mirror toggle, a subsystem
 * knockout/repair (working_subsystems), or a resolution change — all rare,
 * so the bake amortises to ~nothing. subsystem_active (covers) only changes
 * at craft creation, which already shifts `entry`, but it is keyed anyway
 * for completeness. */
static bool TieCockpitRenderer_ChromeKeyMatches(const TieCockpitRenderer* cg,
												const TieCockpitRendererEntry* entry, const TieSnapshot* snap,
												int coord_w, int coord_h, int rt_w, int rt_h) {
	return cg->chrome_valid && cg->chrome_key.entry == entry &&
		   cg->chrome_key.view_idx == snap->cockpit.view_idx &&
		   cg->chrome_key.mirrored_view == snap->cockpit.mirrored_view &&
		   cg->chrome_key.installed_subsystems == snap->hud.installed_subsystems &&
		   cg->chrome_key.working_subsystems == snap->hud.working_subsystems &&
		   cg->chrome_key.subsystem_active == snap->hud.subsystem_active &&
		   cg->chrome_key.coord_w == coord_w && cg->chrome_key.coord_h == coord_h &&
		   cg->chrome_key.rt_w == rt_w && cg->chrome_key.rt_h == rt_h;
}

/* Bake base bitmap + damage cracks + subsystem covers into chrome_rt at
 * the flight RT's full resolution, screen-aligned via the fit viewport so
 * the per-frame composite is a 1:1 sample. Must run OUTSIDE the flight-main
 * render pass (own color-only pass, no depth attachment). The three draw
 * helpers are reused verbatim — their view-0 gating and per-instrument HDR
 * boosts come along, so the RT holds correct per-view content with the
 * boosts pre-baked (RGBA16F carries the >1.0 values and the cutout alpha). */
static bool TieCockpitRenderer_BakeCockpitChrome(TieCockpitRenderer* cg, AeronCommandBuffer* cmd,
												 const TieCockpitRendererEntry* entry,
												 const TieSnapshot* snap, int coord_w, int coord_h, int rt_w,
												 int rt_h) {
	if (!TieCockpitRenderer_EnsureChromeRt(cg, rt_w, rt_h))
		return false;

	const int cockpit_area_h = (coord_h * 19) / 20;
	static const float clear[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	cg->pending_space = TieCockpitRenderer_CockpitDrawSpace(coord_w, coord_h, rt_w, rt_h);
	cg->record_list = cg->chrome_bake_list;
	AeronDrawList_Begin(cg->chrome_bake_list, cg->chrome_rt, rt_w, rt_h, AERON_DRAWLIST2D_CLEAR, clear);
	TIE_GPU_PUSH(cmd, "Cockpit chrome bake");
	TieCockpitRenderer_DrawCockpitBase(cg, cmd, NULL, entry, coord_w, coord_h, cockpit_area_h,
									   snap->cockpit.mirrored_view);
	TieCockpitRenderer_DrawDamageCracks(cg, cmd, NULL, entry, coord_w, coord_h, snap);
	TieCockpitRenderer_DrawCovers(cg, cmd, NULL, entry, coord_w, coord_h, snap);
	if (!AeronDrawList_Prepare(cg->chrome_bake_list, cmd)) {
		TIE_GPU_POP(cmd);
		cg->record_list = NULL;
		Aeron_RequestFatalRendererError("cockpit chrome draw-list preparation");
		return false;
	}
	AeronDrawList_Render(cg->chrome_bake_list, cmd);
	TIE_GPU_POP(cmd);
	cg->record_list = NULL;

	cg->chrome_key.entry = entry;
	cg->chrome_key.view_idx = snap->cockpit.view_idx;
	cg->chrome_key.mirrored_view = snap->cockpit.mirrored_view;
	cg->chrome_key.installed_subsystems = snap->hud.installed_subsystems;
	cg->chrome_key.working_subsystems = snap->hud.working_subsystems;
	cg->chrome_key.subsystem_active = snap->hud.subsystem_active;
	cg->chrome_key.coord_w = coord_w;
	cg->chrome_key.coord_h = coord_h;
	cg->chrome_key.rt_w = rt_w;
	cg->chrome_key.rt_h = rt_h;
	cg->chrome_valid = true;
	return true;
}

/* Composite the baked chrome over the scene inside the flight-main pass.
 * The RT is screen-aligned, so this samples it 1:1 — but restricted to the
 * base coverage rects (mapped through the same fit viewport) so the
 * transparent canopy opening is never rasterised, preserving the
 * coverage-mesh fragment savings. The composite runs in full-RT viewport
 * space; the caller's cockpit fit viewport is saved and restored. Mirroring
 * and HDR boosts are already baked, so tint is identity. */
static void TieCockpitRenderer_DrawCockpitChrome(TieCockpitRenderer* cg, AeronCommandBuffer* cmd,
												 AeronRenderPass* pass, const TieCockpitRendererEntry* entry,
												 int coord_w, int coord_h, int rt_w, int rt_h,
												 bool mirrored) {
	if (!cg->chrome_rt)
		return;
	AeronTexture* chrome_tex = Aeron_RenderTargetGetTexture(cg->chrome_rt);
	if (!chrome_tex)
		return;

	(void)cmd;
	(void)pass;
	(void)entry;
	(void)coord_w;
	(void)coord_h;
	(void)mirrored;
	if (!cg->record_list)
		return;
	AeronDrawList2DSprite sprite = {
		.texture = chrome_tex,
		.src_u0 = 0.0f,
		.src_v0 = 0.0f,
		.src_u1 = 1.0f,
		.src_v1 = 1.0f,
		.dst_x = 0.0f,
		.dst_y = 0.0f,
		.dst_w = (float)rt_w,
		.dst_h = (float)rt_h,
		.tint = { 1.0f, 1.0f, 1.0f, 1.0f },
		.blend = AERON_BLIT2D_BLEND_PMA,
		.filter = AERON_BLIT2D_FILTER_LINEAR,
	};
	AeronDrawList_AddSprite(cg->record_list, &sprite);
}

/* Power-distribution sliders (HUD instruments 26..29). Mirrors tie_core's
 * panel_updatepower (panel.c:743). Each slider has 12 rungs drawn
 * upward from ins->y in `step` pixels; rung i lights when i < value.
 *   26 (0x1A) — balance / engine output (capability 0x400)
 *   27 (0x1B) — lasers (capability 0x200)
 *   28 (0x1C) — shields (capability 0x800 + subsystem 0x001)
 *   29 (0x1D) — beam   (capability 0x1000 + subsystem 0x100)
 * Capability/subsystem gates closed → engine skips drawshape; we match. */
/* Per-cell stride for a 1-D repeated-cel widget (rungs / LEDs), in the
 * layout reference frame. The layout's authored stride on the requested
 * axis (unsigned) wins, re-applying the sign of the engine `step`;
 * otherwise the engine `step` scaled by the layout's classic pixel size
 * on that axis. Shared by TieCockpitRenderer_DrawVertSlider (Y) and TieCockpitRenderer_DrawLaserLedRow
 * (X). The 2-D arc bar and the radius-scaled radar disc resolve their
 * own stride — their authored sources and defaults differ. */
static float TieCockpitRenderer_CockpitResolveStride(const TieCockpitRendererEntry* entry, int idx,
													 const TieSnapshot* snap, int step, bool axis_x) {
	int16_t stride_x_auth = 0, stride_y_auth = 0;
	TieCockpitLayout_Stride(&entry->layout, idx, &stride_x_auth, &stride_y_auth);
	const int16_t authored = axis_x ? stride_x_auth : stride_y_auth;
	if (authored != 0)
		return (step < 0) ? -(float)authored : (float)authored;
	float pixel_sx, pixel_sy;
	TieCockpitLayout_ClassicPixelSize(&entry->layout, (int)snap->cockpit.classic_w,
									  (int)snap->cockpit.classic_h, &pixel_sx, &pixel_sy);
	return (float)step * (axis_x ? pixel_sx : pixel_sy);
}

static void TieCockpitRenderer_DrawVertSlider(TieCockpitRenderer* cg, AeronCommandBuffer* cmd,
											  AeronRenderPass* pass, const TieCockpitRendererEntry* entry,
											  int coord_w, int coord_h, int idx, const TieSnapshot* snap) {
	if (!entry->parts_tex)
		return;
	const TieHudInstrument* ins = &snap->hud.instruments[idx];

	const uint16_t cap = snap->hud.working_subsystems;
	const uint16_t sub = snap->hud.subsystem_active;
	const int lp = snap->hud.laser_power;
	const int sp = snap->hud.shield_power;
	const int bp = snap->hud.beam_power;
	const bool shield_on = (sub & 0x001u) != 0;
	const bool beam_on = (sub & 0x100u) != 0;

	int value;
	switch (idx) {
		case TIE_HUDI_POWER_BALANCE:
			if (!(cap & 0x400u))
				return;
			value = (2 - lp) + 6;
			if (shield_on)
				value += (2 - sp);
			if (beam_on)
				value += (2 - bp);
			break;
		case TIE_HUDI_POWER_LASERS:
			if (!(cap & 0x200u))
				return;
			value = 3 * lp;
			break;
		case TIE_HUDI_POWER_SHIELDS:
			if (!(cap & 0x800u) || !shield_on)
				return;
			value = 3 * sp;
			break;
		case TIE_HUDI_POWER_BEAM:
			if (!(cap & 0x1000u) || !beam_on)
				return;
			value = 3 * bp;
			break;
		default:
			return;
	}
	if (value < 0)
		value = 0;
	if (value > 12)
		value = 12;

	int step = TieCockpitCommon_IsSvga(snap) ? 6 : 2;
	int unlit_frame = ins->param1;
	int lit_frame = ins->param1 + 1;
	/* HDR bloom: lit rungs pick up the instrument's per-id boost so
	 * they glow; unlit rungs stay at 1.0 so the engraved-but-dark
	 * cells in the panel chrome don't pollute the bloom band. */
	const float lit_boost = TieCockpitLayout_HdrBoost(&entry->layout, idx);

	/* Base anchor + rung stride (authored `stride_y:` wins, else engine
	 * step × classic pixel size). step is always positive here. */
	float base_x, base_y;
	TieCockpitRenderer_InsAnchor(entry, idx, ins->x, ins->y, snap, &base_x, &base_y);
	const float step_ref = TieCockpitRenderer_CockpitResolveStride(entry, idx, snap, step, /*axis_x=*/false);

	/* Engine draws bottom rung at ins->y then steps upward by `-step`. */
	for (int rung = 0; rung < 12; ++rung) {
		const bool lit = (rung < value);
		const int frame = lit ? lit_frame : unlit_frame;
		const float boost = lit ? lit_boost : 1.0f;
		TieCockpitRenderer_BlitInstrumentCel(cg, cmd, pass, entry, coord_w, coord_h, base_x,
											 base_y - (float)rung * step_ref, frame, boost);
	}
}

static void TieCockpitRenderer_DrawRadarDisc(TieCockpitRenderer* cg, AeronCommandBuffer* cmd,
											 AeronRenderPass* pass, const TieCockpitRendererEntry* entry,
											 int coord_w, int coord_h, int idx, const TieSnapshot* snap) {
	/* The disc bezel is baked into the cockpit bitmap; this drawer
	 * only emits blip pixels. Engine writes them 1×1 (VGA) or 1×2
	 * (SVGA) — we drop classic's "only-over-radar-background" check
	 * since the HD path doesn't render to a palette FB. */
	(void)idx;
	const TieHudBlip* blips = (idx == 0) ? snap->hud.blips_left : snap->hud.blips_right;
	uint16_t count = (idx == 0) ? snap->hud.blip_count_left : snap->hud.blip_count_right;
	/* Anchor-relative composition: disc anchor (authored) + offset
	 * (snapshot, classic-px) × per-radar scale (radius_ref /
	 * radar_classic_radius). Each radar disc gets its own scale,
	 * independent of any global classic→ref transform. */
	const int radar_id = (idx == 0) ? TIE_HUDI_RADAR_LEFT : TIE_HUDI_RADAR_RIGHT;
	float disc_x, disc_y;
	TieCockpitRenderer_InsAnchor(entry, radar_id, snap->hud.instruments[radar_id].x,
								 snap->hud.instruments[radar_id].y, snap, &disc_x, &disc_y);
	/* Per-radar scale. Layout's authored `radius:` wins; else fall
	 * back to the layout's classic_pixel_size × engine classic radius
	 * (matches the pre-anchor-relative behaviour). */
	const int radar_classic_r = (int)snap->hud.radar_classic_radius;
	const int radius_ref = TieCockpitLayout_RadarRadius(&entry->layout, radar_id);
	float scale_x, scale_y;
	if (radius_ref > 0 && radar_classic_r > 0) {
		scale_x = scale_y = (float)radius_ref / (float)radar_classic_r;
	} else {
		TieCockpitLayout_ClassicPixelSize(&entry->layout, (int)snap->cockpit.classic_w,
										  (int)snap->cockpit.classic_h, &scale_x, &scale_y);
	}
	const float blip_w = scale_x;
	const float blip_h = (TieCockpitCommon_IsSvga(snap) ? 2.0f : 1.0f) * scale_y;
	for (uint16_t b = 0; b < count; ++b) {
		const TieHudBlip* bl = &blips[b];
		uint32_t pal = snap->palette[bl->color & 0xFFu];
		float r, g, bC;
		TieScene2dSrgb_PalToLinearRgb(pal, &r, &g, &bC);
		const float bx = disc_x + (float)bl->offset_x * scale_x;
		const float by = disc_y + (float)bl->offset_y * scale_y;
		TieCockpitRenderer_BlitCockpitQuad(cg, cmd, pass, NULL, coord_w, coord_h, bx, by, blip_w, blip_h,
										   0.0f, 0.0f, 1.0f, 1.0f, r, g, bC, 1.0f);
	}
}

/* Engine bracket-pixel tables (rtsvga2.c:60-70). Each (dx, dy)
 * signed-byte pair offsets from (bracket_x, bracket_y); colour 0xCE.
 * 10-pt = VGA layout, 12-pt = SVGA. */
static const int8_t k_bracket_10pt[20] = { -1, +1, -2, +1, -2, 0, -2, -1, -1, -1,
										   +1, -1, +2, -1, +2, 0, +2, +1, +1, +1 };
static const int8_t k_bracket_12pt[24] = { -1, +2, -2, +2, -2, +1, -2, 0,  -2, -1, -1, -1,
										   +1, -1, +2, -1, +2, 0,  +2, +1, +2, +2, +1, +2 };

/* Engine `panel_drawboxinxtrans(left_x, top_y, w, h)` hollow rectangle
 * emitted via 4 blit_cockpit_quads in classic cockpit-coord space.
 *
 * Visible geometry (rasteriser scan-fill of 4 vertical polygon edges):
 *   width  = w + 1  (cols left_x .. left_x+w)
 *   height = h      (rows top_y  .. top_y+h-1)
 *   1-px borders on all four sides at the outermost cols/rows
 *
 * 1 classic px becomes ~2.25 swap px at 1080p / ~4.5 at 4K through the
 * cockpit-pass 4:3 fit — so the border stays visible at any HD scale
 * (single-swap-px borders would disappear at 4K). Used by both the
 * main viewport target box (w=13, h=10 SVGA) and the PIP subsystem
 * box (w=4, h=4). */
static void TieCockpitRenderer_BlitEngineHollowBox(TieCockpitRenderer* cg, AeronCommandBuffer* cmd,
												   AeronRenderPass* pass, int coord_w, int coord_h,
												   float left_x_cl, float top_y_cl, int box_w, int box_h,
												   float pixel_sx, float pixel_sy, float r, float g,
												   float b) {
	if (box_w <= 0 || box_h <= 0)
		return;
	/* Edge thickness = 1 classic-px scaled into the layout's ref
	 * frame so the border stays visible at HD ref sizes. */
	const float edge_w = pixel_sx;
	const float edge_h = pixel_sy;
	const float outer_w = (float)box_w + edge_w;
	const float side_h = (float)box_h - 2.0f * edge_h;
	const float bottom_y = top_y_cl + (float)box_h - edge_h;
	const float right_x = left_x_cl + (float)box_w;
	/* Top edge. */
	TieCockpitRenderer_BlitCockpitQuad(cg, cmd, pass, NULL, coord_w, coord_h, left_x_cl, top_y_cl, outer_w,
									   edge_h, 0, 0, 1, 1, r, g, b, 1.0f);
	/* Bottom edge. */
	TieCockpitRenderer_BlitCockpitQuad(cg, cmd, pass, NULL, coord_w, coord_h, left_x_cl, bottom_y, outer_w,
									   edge_h, 0, 0, 1, 1, r, g, b, 1.0f);
	/* Side edges. */
	if (side_h > 0.0f) {
		TieCockpitRenderer_BlitCockpitQuad(cg, cmd, pass, NULL, coord_w, coord_h, left_x_cl,
										   top_y_cl + edge_h, edge_w, side_h, 0, 0, 1, 1, r, g, b, 1.0f);
		TieCockpitRenderer_BlitCockpitQuad(cg, cmd, pass, NULL, coord_w, coord_h, right_x, top_y_cl + edge_h,
										   edge_w, side_h, 0, 0, 1, 1, r, g, b, 1.0f);
	}
}

/* 1998-look target box: four L-shaped corner brackets. Arm length =
 * clamp(dim/8, 3, dim) classic-px (scaled by pixel_sx/sy to the
 * layout ref frame); middle of every side blank. The arms' THICKNESS
 * (1 classic-px) also scales via pixel_sx/sy so single-classic-px
 * borders stay visible at HD ref sizes. */
static void TieCockpitRenderer_BlitEngineCornerBox(TieCockpitRenderer* cg, AeronCommandBuffer* cmd,
												   AeronRenderPass* pass, int coord_w, int coord_h,
												   float left_x_cl, float top_y_cl, float box_w, float box_h,
												   float pixel_sx, float pixel_sy, float r, float g,
												   float b) {
	if (box_w <= 0.0f || box_h <= 0.0f)
		return;

	/* Arm length floor = 3 classic-px scaled. */
	const float arm_min_x = 3.0f * pixel_sx;
	const float arm_min_y = 3.0f * pixel_sy;
	float bx = box_w * 0.125f;
	if (bx < arm_min_x)
		bx = arm_min_x;
	if (bx > box_w)
		bx = box_w;
	float by = box_h * 0.125f;
	if (by < arm_min_y)
		by = arm_min_y;
	if (by > box_h)
		by = box_h;

	/* Arm thickness = 1 classic-px scaled. */
	const float edge_w = pixel_sx;
	const float edge_h = pixel_sy;

	const float right_arm_x = left_x_cl + box_w - bx;
	const float bottom_y = top_y_cl + box_h - edge_h;
	const float right_col_x = left_x_cl + box_w - edge_w;
	const float bot_arm_y = top_y_cl + box_h - by;

	/* Horizontal arms: top-L, top-R, bottom-L, bottom-R. */
	TieCockpitRenderer_BlitCockpitQuad(cg, cmd, pass, NULL, coord_w, coord_h, left_x_cl, top_y_cl, bx, edge_h,
									   0, 0, 1, 1, r, g, b, 1.0f);
	TieCockpitRenderer_BlitCockpitQuad(cg, cmd, pass, NULL, coord_w, coord_h, right_arm_x, top_y_cl, bx,
									   edge_h, 0, 0, 1, 1, r, g, b, 1.0f);
	TieCockpitRenderer_BlitCockpitQuad(cg, cmd, pass, NULL, coord_w, coord_h, left_x_cl, bottom_y, bx, edge_h,
									   0, 0, 1, 1, r, g, b, 1.0f);
	TieCockpitRenderer_BlitCockpitQuad(cg, cmd, pass, NULL, coord_w, coord_h, right_arm_x, bottom_y, bx,
									   edge_h, 0, 0, 1, 1, r, g, b, 1.0f);

	/* Vertical arms (length by - edge_h), inset one row so the corner
	 * pixel is shared with the horizontal arm. */
	if (by > edge_h) {
		const float v_h = by - edge_h;
		TieCockpitRenderer_BlitCockpitQuad(cg, cmd, pass, NULL, coord_w, coord_h, left_x_cl,
										   top_y_cl + edge_h, edge_w, v_h, 0, 0, 1, 1, r, g, b, 1.0f);
		TieCockpitRenderer_BlitCockpitQuad(cg, cmd, pass, NULL, coord_w, coord_h, right_col_x,
										   top_y_cl + edge_h, edge_w, v_h, 0, 0, 1, 1, r, g, b, 1.0f);
		TieCockpitRenderer_BlitCockpitQuad(cg, cmd, pass, NULL, coord_w, coord_h, left_x_cl, bot_arm_y,
										   edge_w, v_h, 0, 0, 1, 1, r, g, b, 1.0f);
		TieCockpitRenderer_BlitCockpitQuad(cg, cmd, pass, NULL, coord_w, coord_h, right_col_x, bot_arm_y,
										   edge_w, v_h, 0, 0, 1, 1, r, g, b, 1.0f);
	}
}

/* world → clip via 4×4 view_proj, then NDC. Returns false when the
 * point is behind the camera (clip.w <= 0). Used by the target-box
 * overlays which project tie_core's world coords through HD's own
 * camera so the box lands at the right screen position regardless
 * of aspect ratio. */
static bool TieCockpitRenderer_ProjectWorldToNdc(const float view_proj[16], float wx, float wy, float wz,
												 float* out_ndc_x, float* out_ndc_y, float* out_clip_w) {
	const float cx = view_proj[0] * wx + view_proj[1] * wy + view_proj[2] * wz + view_proj[3];
	const float cy = view_proj[4] * wx + view_proj[5] * wy + view_proj[6] * wz + view_proj[7];
	const float cw = view_proj[12] * wx + view_proj[13] * wy + view_proj[14] * wz + view_proj[15];
	if (cw <= 0.0f)
		return false;
	*out_ndc_x = cx / cw;
	*out_ndc_y = cy / cw;
	*out_clip_w = cw;
	return true;
}

/* Look up a flight-object's world position by engine-side slot index
 * (== TieFlightObjectState.slot). Returns false when no flight object
 * with that slot is present in the snapshot. Linear scan over
 * flight_count — typically ≤120 entries. */
static const int32_t* TieCockpitRenderer_FlightWorldPosition(const TieSnapshot* snap, uint16_t slot) {
	for (uint16_t i = 0; i < snap->flight_count; ++i) {
		if (snap->flights[i].slot == slot)
			return snap->flights[i].world_pos;
	}
	return NULL;
}

/* Static-object world position by engine static-table index (target_obj_slot
 * - 0x3800). HD static slots are sparse; iterate to find a match. */
static const int32_t* TieCockpitRenderer_StaticWorldPosition(const TieSnapshot* snap, uint16_t static_idx) {
	for (uint16_t i = 0; i < snap->static_count; ++i) {
		if (snap->statics[i].slot == static_idx)
			return snap->statics[i].world_pos;
	}
	return NULL;
}

/* Main-viewport target indicator box (engine: USER_targetonscreen,
 * user.c:559).
 *
 * Pipeline anchor: the ship's swapchain pixel is set when flight_gpu
 * rasterises through the FLIGHT viewport; this drawer runs inside the
 * cockpit render pass, which has its own (different) viewport — the
 * 4:3 pillarbox fit of the cockpit's coord frame (see
 * TieCockpitRenderer_Draw:2887). The flight projection spans the full output,
 * so projected positions must be remapped through swapchain pixels into
 * the cockpit viewport before drawing the box. */
static void TieCockpitRenderer_DrawMainTargetBox(TieCockpitRenderer* cg, AeronCommandBuffer* cmd,
												 AeronRenderPass* pass, const TieCockpitRendererEntry* entry,
												 int rt_w, int rt_h, const TieSnapshot* snap,
												 bool look_1998) {
	/* engine_ok = 1995 gates (includes pilotview filter).
	 * inputs_ok = 1998 gates (no pilotview filter). */
	const bool gate_ok = look_1998 ? snap->hud.target_box_inputs_ok : snap->hud.target_box_engine_ok;
	if (!gate_ok)
		return;
	const uint16_t slot = snap->hud.target_obj_slot;
	if (slot == 0xFFFFu)
		return;
	if (rt_w <= 0 || rt_h <= 0)
		return;

	const int32_t* world_pos = (slot < 0x3800u)
								   ? TieCockpitRenderer_FlightWorldPosition(snap, slot)
								   : TieCockpitRenderer_StaticWorldPosition(snap, (uint16_t)(slot - 0x3800u));
	if (!world_pos)
		return;
	float local[3];
	AeronWorld_LocalI32(snap->camera.world_pos, world_pos, local);
	if (look_1998 && slot < 0x3800u) {
		local[0] += (float)snap->hud.target_box_center_offset_1998[0];
		local[1] += (float)snap->hud.target_box_center_offset_1998[1];
		local[2] += (float)snap->hud.target_box_center_offset_1998[2];
	}

	TieFlightCamera fcam;
	TieRenderMath_BuildCamera(&fcam, &snap->camera, rt_w, rt_h);
	if (fcam.fit_w <= 0.0f || fcam.fit_h <= 0.0f)
		return;

	float ndc_x, ndc_y, clip_w;
	if (!TieCockpitRenderer_ProjectWorldToNdc(fcam.view_proj, local[0], local[1], local[2], &ndc_x, &ndc_y,
											  &clip_w))
		return;

	/* 1995 rejects off-viewport targets explicitly; 1998 lets the
	 * primitive clip per-strip. */
	if (!look_1998) {
		if (ndc_x < -1.0f || ndc_x > 1.0f)
			return;
		if (ndc_y < -1.0f || ndc_y > 1.0f)
			return;
	}

	/* Apparent half-extent in NDC:
	 *   ndc_extent = (1 / tan(v_half)) * bound / depth
	 * Multiplied by halfpixelsdeep this equals retail's
	 *   v12 = perspFactor * bound / depth   (in engine pixels). */
	const uint16_t bound_raw = look_1998 ? snap->hud.target_bound_hwidth_1998 : snap->hud.target_bound_hwidth;
	const float bound_f = (float)bound_raw;
	const float inv_tan_v = 1.0f / tanf(fcam.v_half_rad);
	const float ndc_extent_y = (bound_f / clip_w) * inv_tan_v;

	/* Engine's classic 3D aperture half-height, retained for the 1998
	 * fallback below. */
	const float halfpixelsdeep_gate = snap->camera.viewport_frac_h * (float)snap->cockpit.classic_h * 0.5f;
	if (halfpixelsdeep_gate <= 0.0f)
		return;

	/* 1995 hides the box when the ship gets visibly big on screen. */
	if (!look_1998) {
		const float threshold_pixels = (snap->cockpit.classic_h >= 400) ? 10.0f : 5.0f;
		const float threshold_ndc =
			2.0f * threshold_pixels * fcam.fit_h / ((float)snap->cockpit.classic_h * fcam.flight_vp_h);
		if (ndc_extent_y > threshold_ndc)
			return;
	}

	/* Round-trip the projected position through swapchain pixels then
	 * into the cockpit pass's coord frame. coord = layout reference
	 * (16:9) or classic (4:3); cockpit_vp = matching fit on the RT
	 * (full RT or pillarbox). The TieCockpitRenderer_BlitCockpitQuad NDC math reverses
	 * the same transform so strips stay multi-swap-px-thick at 4K. */
	const float ship_px = fcam.flight_vp_x + (ndc_x + 1.0f) * 0.5f * fcam.flight_vp_w;
	const float ship_py = fcam.flight_vp_y + (1.0f - ndc_y) * 0.5f * fcam.flight_vp_h;
	int coord_w, coord_h;
	TieCockpitRenderer_ResolveCoordFrame(entry, snap, &coord_w, &coord_h);
	TieCockpitRendererViewport cockpit_vp;
	TieCockpitRenderer_CockpitFitViewport(coord_w, coord_h, rt_w, rt_h, &cockpit_vp);
	const float center_x_cl = (ship_px - cockpit_vp.x) * (float)coord_w / cockpit_vp.w;
	const float center_y_cl = (ship_py - cockpit_vp.y) * (float)coord_h / cockpit_vp.h;
	if (!look_1998) {
		if (center_x_cl < 0.0f || center_x_cl > (float)coord_w)
			return;
		if (center_y_cl < 0.0f || center_y_cl > (float)coord_h)
			return;
	}

	/* 1995 fixed palette 0xCE, 1998 caller passes 59. Raw indices, no remap. */
	const uint32_t pal = snap->palette[look_1998 ? 59u : 0xCEu];
	float r, g, b;
	TieScene2dSrgb_PalToLinearRgb(pal, &r, &g, &b);

	float pixel_sx, pixel_sy;
	TieCockpitLayout_ClassicPixelSize(entry ? &entry->layout : NULL, (int)snap->cockpit.classic_w,
									  (int)snap->cockpit.classic_h, &pixel_sx, &pixel_sy);

	if (!look_1998) {
		/* 1995: fixed-size locator. Integer halving on offset matches
		 * user.c:627-629 — odd box widths land one classic px left of
		 * true center; float halving would shift by 0.5 classic px. */
		const int box_w = coord_w / 48;
		const int box_h = coord_h / 48;
		const float left_x_cl = center_x_cl - (float)(box_w / 2);
		const float top_y_cl = center_y_cl - (float)(box_h / 2);
		TieCockpitRenderer_BlitEngineHollowBox(cg, cmd, pass, coord_w, coord_h, left_x_cl, top_y_cl, box_w,
											   box_h, pixel_sx, pixel_sy, r, g, b);
	} else {
		/* 1998: side = clamp(reticle_px, [4 VGA | 8 SVGA] .. 0.75·coord_w) + 4.
		 * halfpixelsdeep_size converts full-output NDC into cockpit-layout
		 * pixels so the box scales with the rendered ship.
		 * side_min and the +4 padding are engine classic-px constants; scale
		 * them into the layout's ref frame so the box stays visible at HD. */
		const float halfpixelsdeep_size = (fcam.fit_h > 0.0f)
											  ? (fcam.flight_vp_h / fcam.fit_h) * (float)coord_h * 0.5f
											  : halfpixelsdeep_gate;
		const float apparent_px = ndc_extent_y * halfpixelsdeep_size;
		const float side_min_classic = (snap->cockpit.classic_h >= 400) ? 8.0f : 4.0f;
		const float side_min = side_min_classic * pixel_sy;
		const float side_max = 0.75f * (float)coord_w;
		float side_f = apparent_px;
		if (side_f < side_min)
			side_f = side_min;
		if (side_f > side_max)
			side_f = side_max;
		side_f += 4.0f * pixel_sy;
		const float left_x_cl = center_x_cl - side_f * 0.5f;
		const float top_y_cl = center_y_cl - side_f * 0.5f;
		TieCockpitRenderer_BlitEngineCornerBox(cg, cmd, pass, coord_w, coord_h, left_x_cl, top_y_cl, side_f,
											   side_f, pixel_sx, pixel_sy, r, g, b);
	}
}

/* Draw the radar bracket — N 1×1 solid quads tinted palette[0xCE].
 * No-op when no target is acquired (bracket_present = 0). */
static void TieCockpitRenderer_DrawRadarBracket(TieCockpitRenderer* cg, AeronCommandBuffer* cmd,
												AeronRenderPass* pass, const TieCockpitRendererEntry* entry,
												int coord_w, int coord_h, const TieSnapshot* snap) {
	if (!snap->hud.bracket_present)
		return;

	const int8_t* def;
	int pair_count;
	if (TieCockpitCommon_IsSvga(snap)) {
		def = k_bracket_12pt;
		pair_count = 12;
	} else {
		def = k_bracket_10pt;
		pair_count = 10;
	}

	uint32_t pal = snap->palette[0xCEu]; /* engine-fixed bracket color */
	float r, g, bC;
	TieScene2dSrgb_PalToLinearRgb(pal, &r, &g, &bC);

	/* Anchor-relative composition (same as TieCockpitRenderer_DrawRadarDisc). The
	 * bracket sits on top of the targeted blip, so it lives on
	 * whichever radar disc the engine assigned it. */
	const int radar_id = (snap->hud.bracket_radar_idx == 0) ? TIE_HUDI_RADAR_LEFT : TIE_HUDI_RADAR_RIGHT;
	float disc_x, disc_y;
	TieCockpitRenderer_InsAnchor(entry, radar_id, snap->hud.instruments[radar_id].x,
								 snap->hud.instruments[radar_id].y, snap, &disc_x, &disc_y);
	const int radar_classic_r = (int)snap->hud.radar_classic_radius;
	const int radius_ref = TieCockpitLayout_RadarRadius(&entry->layout, radar_id);
	float scale_x, scale_y;
	if (radius_ref > 0 && radar_classic_r > 0) {
		scale_x = scale_y = (float)radius_ref / (float)radar_classic_r;
	} else {
		TieCockpitLayout_ClassicPixelSize(&entry->layout, (int)snap->cockpit.classic_w,
										  (int)snap->cockpit.classic_h, &scale_x, &scale_y);
	}
	const float cx = disc_x + (float)snap->hud.bracket_offset_x * scale_x;
	const float cy = disc_y + (float)snap->hud.bracket_offset_y * scale_y;

	for (int i = 0; i < pair_count; ++i) {
		int dx = def[2 * i + 0];
		int dy = def[2 * i + 1];
		TieCockpitRenderer_BlitCockpitQuad(cg, cmd, pass, NULL, coord_w, coord_h, cx + (float)dx * scale_x,
										   cy + (float)dy * scale_y, scale_x, scale_y, 0.0f, 0.0f, 1.0f, 1.0f,
										   r, g, bC, 1.0f);
	}
}

/* Message-bar text composition (pick_msg_base_color, build_msg_body_with_colors,
 * color tables, TieCockpitText_BuildmsgBarText) lives in cockpit_text.c. */

/* Logical palette index → RGB tint via snap->palette + TieCockpitText_RemapColor. */
static void TieCockpitRenderer_MsgColorToTint(const TieSnapshot* snap, uint8_t logical_color, float* r,
											  float* g, float* b) {
	const uint32_t pal = snap->palette[TieCockpitText_RemapColor(logical_color)];
	TieScene2dSrgb_PalToLinearRgb(pal, r, g, b);
}

/* setbackcolor defines (COCKPIT_BG_CMD/HUD/THREAT/AMMO) live in
 * cockpit_text.h — shared with the per-builder text composition. */

/* Paint a solid backcolor rectangle — engine equivalent of
 * festring_setbound + clearwindow. Used to repaint the dynamic-text
 * areas of the cockpit panel that classic clears once at panel_initpanel
 * and that re-expose the cockpit base bitmap's placeholder in HD because
 * we re-blit the base every frame. Logical bg goes through
 * TieCockpitText_RemapColor → snap->palette → RGB. */
static void TieCockpitRenderer_ClearPanelRect(TieCockpitRenderer* cg, AeronCommandBuffer* cmd,
											  AeronRenderPass* pass, int coord_w, int coord_h,
											  const TieSnapshot* snap, int x, int y, int w, int h,
											  uint8_t logical_bg) {
	if (w <= 0 || h <= 0)
		return;
	float r, g, b;
	TieCockpitRenderer_MsgColorToTint(snap, logical_bg, &r, &g, &b);
	TieCockpitRenderer_BlitCockpitQuad(cg, cmd, pass, NULL, coord_w, coord_h, (float)x, (float)y, (float)w,
									   (float)h, 0.0f, 0.0f, 1.0f, 1.0f, r, g, b, 1.0f);
}

/* Per-frame clearwindow pass for cockpit text areas that classic
 * paints once during panel_initpanel but that we re-blit every frame.
 * Currently covers:
 *   - rec-% counter (idx 32) — panel_updatereplaystuff backcolor 0x40,
 *     bound = (12 VGA / 18 SVGA) × fontheight(2) (= 5 VGA / 9 SVGA).
 * View 0 only (matches engine writers' camera.pilotview gate). */
static void TieCockpitRenderer_DrawClearwindows(TieCockpitRenderer* cg, AeronCommandBuffer* cmd,
												AeronRenderPass* pass, const TieCockpitRendererEntry* entry,
												int coord_w, int coord_h, const TieSnapshot* snap) {
	if (snap->cockpit.view_idx != 0)
		return;
	const bool svga = TieCockpitCommon_IsSvga(snap);
	const int micro_h = svga ? 9 : 5; /* fontheight at fontsize 2 */

	/* Rec-% counter (panel.c:657-666). */
	const TieHudInstrument* rec = &snap->hud.instruments[TIE_HUDI_REC_PCT];
	if (rec->x || rec->y) {
		const int rec_w = svga ? 18 : 12;
		float ax, ay;
		TieCockpitRenderer_InsAnchor(entry, TIE_HUDI_REC_PCT, rec->x, rec->y, snap, &ax, &ay);
		TieCockpitRenderer_ClearPanelRect(cg, cmd, pass, coord_w, coord_h, snap, (int)ax, (int)ay, rec_w,
										  micro_h, COCKPIT_BG_HUD);
	}
}

/* Message banner (msg.c::msg_messagedisplay). Paints the backcolor
 * strip + 1-px separator at top-1; the text records (body, time-warp,
 * and training bonus readouts) are composed by
 * TieCockpitText_BuildmsgBarText. Engine's invisible-drop (dropcolor
 * remaps to backcolor) isn't reproduced — the HD records carry no shadow. */
static void TieCockpitRenderer_DrawMessageBar(TieCockpitRenderer* cg, AeronCommandBuffer* cmd,
											  AeronRenderPass* pass, const TieSnapshot* snap) {
	(void)cmd;
	(void)pass;
	if (!cg || !cg->record_list)
		return;
	const TieCockpitRendererDrawSpace* target = &cg->pending_space;
	TieFlightHudMessageBarGeometry geometry;
	if (!TieFlightHud_MessageBarGeometry(snap, target->target_w, target->target_h, &geometry))
		return;

	/* Original msg_messagedisplay clears the full framebuffer-width band. */
	float r, g, b;
	TieCockpitRenderer_MsgColorToTint(snap, 0x2C, &r, &g, &b);
	const float body_color[4] = { r, g, b, 1.0f };
	AeronDrawList_AddFill(cg->record_list, 0.0f, geometry.bar_y, (float)target->target_w, geometry.bar_h,
						  body_color, AERON_BLIT2D_BLEND_PMA, NULL);
	TieCockpitRenderer_MsgColorToTint(snap, 0x2D, &r, &g, &b);
	const float separator_color[4] = { r, g, b, 1.0f };
	AeronDrawList_AddFill(cg->record_list, 0.0f, geometry.separator_y, (float)target->target_w,
						  geometry.separator_h, separator_color, AERON_BLIT2D_BLEND_PMA, NULL);

	/* Text retains the original body indent and time-warp column. */
	TieUIText recs[4];
	int n_recs = TieCockpitText_BuildmsgBarText(snap, (int)snap->hud.msg_bar.line_top, 2,
												(int)snap->hud.msg_bar.line_right, recs,
												(int)(sizeof recs / sizeof recs[0]));
	if (n_recs > 0 && cg->text_renderer) {
		const float atlas_scale = cg->original_font_atlas_scale > 0.0f
									  ? cg->original_font_atlas_scale
									  : TieCockpitRenderer_CockpitAtlasScaleFor(NULL, geometry.classic_h);
		const TieScene2dTextSpace message_space = {
			.classic_w = geometry.classic_w,
			.classic_h = geometry.classic_h,
			.atlas_scale_x = atlas_scale,
			.atlas_scale_y = atlas_scale,
			.space_between_classic = 0.0f,
			.fit = TIE_SCENE2D_TEXT_FIT_FILL,
		};
		TieScene2dTextRenderer_RecordFestringScaled(
			cg->text_renderer, cg->record_list, geometry.classic_w, geometry.classic_h, &message_space, recs,
			n_recs, snap->palette, geometry.text_fit_x, 0.0f, geometry.text_scale_x, geometry.scale_y,
			target->target_w, target->target_h);
	}
}

/* panel_updatelasers LED row (panel.c:776). One call per weapon
 * group; idx 3..14 = group 0..11. step = SVGA 6 / VGA 3, negated by
 * the param2 RTL flag. Always paints 10 cels (filled or empty) so
 * the row matches classic's first-paint behaviour. */
static void TieCockpitRenderer_DrawLaserLedRow(TieCockpitRenderer* cg, AeronCommandBuffer* cmd,
											   AeronRenderPass* pass, const TieCockpitRendererEntry* entry,
											   int coord_w, int coord_h, int idx, const TieSnapshot* snap) {
	if (!entry->parts_tex)
		return;

	int g = idx - 3;
	if (g < 0 || g >= snap->hud.weapon_group_cnt)
		return;

	const TieHudInstrument* ins = &snap->hud.instruments[idx];
	if (!TieCockpitCommon_InstrumentActive(ins))
		return;

	if (snap->cockpit.view_idx != 0)
		return;
	if (!(snap->hud.working_subsystems & 0x02u))
		return;
	if (!(snap->hud.working_subsystems & 0x04u))
		return;

	/* panel_updatelasers writes value = led_count (0..10) and color =
	 * filled_frame (1 normal, 2 overcharge, 0 = uncharged). */
	const int led_count = ins->value;
	const int filled_frame = ins->color;
	const int empty_frame = filled_frame > 0 ? filled_frame - 1 : 0;

	const int shape_base = ins->param1;
	const bool flip = (ins->param2 != 0);
	int step = TieCockpitCommon_IsSvga(snap) ? 6 : 3;
	if (flip)
		step = -step;

	/* HDR bloom: lit LEDs (filled, charged) pick up the instrument's
	 * boost so they glow. Empty cels and the uncharged-weapon base
	 * (filled_frame == 0, where filled and empty would render the same
	 * dim cel) stay at 1.0 so unlit row positions don't pollute bloom. */
	const float lit_boost = TieCockpitLayout_HdrBoost(&entry->layout, idx);
	const bool can_glow = (filled_frame > 0);

	/* Base anchor + LED stride (authored `stride_x:` wins, else engine
	 * step × classic pixel size; step may be negated above for RTL). */
	float base_x, base_y;
	TieCockpitRenderer_InsAnchor(entry, idx, ins->x, ins->y, snap, &base_x, &base_y);
	const float step_ref = TieCockpitRenderer_CockpitResolveStride(entry, idx, snap, step, /*axis_x=*/true);

	float led_x = base_x;
	for (int l = 0; l < 10; ++l) {
		const bool lit = can_glow && (l < led_count);
		int frame = shape_base + (lit ? filled_frame : empty_frame);
		float u0, v0, u1, v1;
		int cw, ch;
		if (!TieCockpitRenderer_ResolveCel(entry, frame, &u0, &v0, &u1, &v1, &cw, &ch)) {
			led_x += step_ref;
			continue;
		}
		/* RTL flip = engine drawshape's flip_x=1: cel's right edge
		 * lands at x, content mirrored. For a top-left-anchored
		 * quad: shift left by (cw-1) and swap U coords. */
		const float dx = flip ? (led_x - (float)(cw - 1)) : led_x;
		const float fu0 = flip ? u1 : u0;
		const float fu1 = flip ? u0 : u1;
		// const float boost = lit ? lit_boost : 2.0f;
		const float boost = lit_boost;
		TieCockpitRenderer_BlitCockpitPartQuad(cg, cmd, pass, entry, coord_w, coord_h, dx, base_y, (float)cw,
											   (float)ch, fu0, v0, fu1, v1, boost, boost, boost, 1.0f);
		led_x += step_ref;
	}
}

/* SHIELD_QUAD widgets (HUD instruments 19/20 = forward normal/over,
 * 21/22 = rear normal/over). Each bar is one cockpit-parts cel
 * (instruments[idx].param1) recoloured per frame to indicate fill
 * level — engine semantics from panel_updatemonolever +
 * rtsvga2_drawmonoshapeVGA. The shape acts as a stencil; every painted
 * pixel adopts the chosen palette colour. We achieve that via the
 * existing blit pipeline by zeroing `tint` and setting `bias` to the
 * resolved RGB (bias is alpha-weighted, so transparent pixels stay
 * clear). */
static void TieCockpitRenderer_DrawShieldQuad(TieCockpitRenderer* cg, AeronCommandBuffer* cmd,
											  AeronRenderPass* pass, const TieCockpitRendererEntry* entry,
											  int coord_w, int coord_h, int idx, const TieSnapshot* snap) {
	if (!entry->parts_tex)
		return;
	if ((snap->hud.working_subsystems & 0x20u) == 0)
		return; /* no shields */

	const TieHudInstrument* ins = &snap->hud.instruments[idx];
	const uint32_t pal = snap->palette[(uint8_t)ins->value];
	float r, g, b;
	TieScene2dSrgb_PalToLinearRgb(pal, &r, &g, &b);

	float u0, v0, u1, v1;
	int cw, ch;
	if (!TieCockpitRenderer_ResolveCel(entry, ins->param1, &u0, &v0, &u1, &v1, &cw, &ch))
		return;

	float ax, ay;
	TieCockpitRenderer_InsAnchor(entry, idx, ins->x, ins->y, snap, &ax, &ay);
	TieCockpitRenderer_BlitCockpitPartQuadMono(cg, cmd, pass, entry, coord_w, coord_h, ax, ay, (float)cw,
											   (float)ch, u0, v0, u1, v1, r, g, b);
}

/* BEAM_ARCBAR (HUD instrument 35). Mirrors panel_updatebeam
 * (panel.c:1052): 9 LED slots along an arc, each tinted from
 * kBeamColors[] based on whether the cumulative beam_charge fills,
 * partially-fills, or is below this bucket of 1000 charge units.
 * Each LED uses a distinct cel (param1 + i). Rendered MONO_SHAPE so the
 * cel acts as an alpha mask × the chosen palette colour. */
static void TieCockpitRenderer_DrawBeamArcbar(TieCockpitRenderer* cg, AeronCommandBuffer* cmd,
											  AeronRenderPass* pass, const TieCockpitRendererEntry* entry,
											  int coord_w, int coord_h, int idx, const TieSnapshot* snap) {
	if (!entry->parts_tex)
		return;
	if ((snap->hud.working_subsystems & 0x10u) == 0)
		return; /* no beam capability */

	const TieHudInstrument* ins = &snap->hud.instruments[idx];
	const bool svga = TieCockpitCommon_IsSvga(snap);

	/* Base anchor + arc stride. Authored `stride_x/y:` wins. */
	float base_x, base_y;
	TieCockpitRenderer_InsAnchor(entry, idx, ins->x, ins->y, snap, &base_x, &base_y);
	int16_t stride_x_auth = 0, stride_y_auth = 0;
	TieCockpitLayout_Stride(&entry->layout, idx, &stride_x_auth, &stride_y_auth);
	float x_step, y_step; /* per-i step in ref-frame px */
	if (stride_x_auth != 0 && stride_y_auth != 0) {
		x_step = (float)stride_x_auth;
		y_step = (float)stride_y_auth;
	} else {
		float x_scale, y_scale;
		TieCockpitLayout_ClassicPixelSize(&entry->layout, (int)snap->cockpit.classic_w,
										  (int)snap->cockpit.classic_h, &x_scale, &y_scale);
		x_step = (svga ? 3.0f : 2.0f) * x_scale;
		y_step = (svga ? 3.0f : 1.0f) * y_scale;
	}

	/* step_rev runs 8..0 across i = 0..8. */
	for (int i = 0, step_rev = 8; i < 9; ++i, --step_rev) {
		const float led_x = base_x + (float)step_rev * x_step;
		const float led_y = base_y + (float)step_rev * y_step;

		int frame = ins->param1 + i;
		float u0, v0, u1, v1;
		int cw, ch;
		if (!TieCockpitRenderer_ResolveCel(entry, frame, &u0, &v0, &u1, &v1, &cw, &ch))
			continue;

		const uint32_t pal = snap->palette[snap->hud.beam_arc_led_colors[i]];
		float r, g, b;
		TieScene2dSrgb_PalToLinearRgb(pal, &r, &g, &b);
		TieCockpitRenderer_BlitCockpitPartQuadMono(cg, cmd, pass, entry, coord_w, coord_h, led_x, led_y,
												   (float)cw, (float)ch, u0, v0, u1, v1, r, g, b);
	}
}

/* Lookup-only — TieCockpitRenderer_EnsureCrtMask must have run earlier this frame. */
static AeronTexture* TieCockpitRenderer_CrtMaskLookup(const TieCockpitRenderer* cg, uint8_t variant,
													  uint16_t classic_w) {
	for (int i = 0; i < cg->crt_mask_count; ++i) {
		const TieCockpitRendererCrtMaskEntry* entry = &cg->crt_masks[i];
		if (entry->variant == variant && entry->classic_w == classic_w)
			return entry->tex;
	}
	return NULL;
}

/* Matches cockpit_pip_compose.vert.hlsl's PipComposeVS cbuffer. */
typedef struct {
	float dst_rect[4]; /* NDC: (x_bl, y_bl, width, height) */
	float src_rect[4]; /* UV:  (u0, v0, u1, v1) */
} TieCockpitRendererPipComposeUniforms;

/* color_tex × mask_tex's alpha into the cockpit RT rect, in cockpit-
 * coord space. Mask was baked at the PIP RT's native resolution so
 * a full (0..1, 0..1) UV is correct. */
static void TieCockpitRenderer_DrawPipCompose(TieCockpitRenderer* cg, AeronCommandBuffer* cmd,
											  AeronRenderPass* pass, AeronTexture* color_tex,
											  AeronTexture* mask_tex, int coord_w, int coord_h, float dst_x,
											  float dst_y, float dst_w, float dst_h) {
	if (!cg->pip_pipeline || !color_tex || !mask_tex)
		return;

	const TieCockpitRendererDrawSpace* space = &cg->pending_space;
	const float px = space->fit_x + dst_x * space->scale_x;
	const float py = space->fit_y + dst_y * space->scale_y;
	const float pw = dst_w * space->scale_x;
	const float ph = dst_h * space->scale_y;
	float ndc_x = (px / (float)space->target_w) * 2.0f - 1.0f;
	float ndc_w = (pw / (float)space->target_w) * 2.0f;
	float ndc_y = 1.0f - ((py + ph) / (float)space->target_h) * 2.0f;
	float ndc_h = (ph / (float)space->target_h) * 2.0f;

	TieCockpitRendererPipComposeUniforms u = {
		.dst_rect = { ndc_x, ndc_y, ndc_w, ndc_h },
		.src_rect = { 0.0f, 0.0f, 1.0f, 1.0f },
	};

	Aeron_BindGraphicsPipeline(pass, cg->pip_pipeline);
	Aeron_BindTextureSampler(pass, AERON_SHADER_STAGE_FRAGMENT, 0, color_tex, cg->pip_color_sampler);
	Aeron_BindTextureSampler(pass, AERON_SHADER_STAGE_FRAGMENT, 1, mask_tex, cg->pip_mask_sampler);
	Aeron_BindUniformData(pass, AERON_SHADER_STAGE_VERTEX, 0, &u, sizeof u);
	Aeron_Draw(pass, 4, 0);
}

/* 3DCRT widget (idx 2). Composites the PIP pre-pass RT onto the CRT
 * rect through the per-spec cutout mask. Engine analogue:
 * panel_update3Dcrt's xtrans2 path (panel.c:2497). A snapshot without
 * a live PIP target leaves the cockpit bitmap's CRT bezel visible. */
static void TieCockpitRenderer_Draw3dcrt(TieCockpitRenderer* cg, AeronCommandBuffer* cmd,
										 AeronRenderPass* pass, const TieCockpitRendererEntry* entry,
										 int coord_w, int coord_h, const TieSnapshot* snap) {
	const TieHudInstrument* ins = &snap->hud.instruments[2];
	float dst_x, dst_y, dst_w, dst_h;
	TieCockpitRenderer_ResolvePipRect(entry, snap, ins->x, ins->y, ins->param1, ins->param2, &dst_x, &dst_y,
									  &dst_w, &dst_h);

	if (dst_w <= 0.0f || dst_h <= 0.0f)
		return;
	if (!snap->cockpit.pip_target_present)
		return;
	if (!cg->flight_gpu)
		return;

	AeronTexture* pip_texture = TieFlightRenderer_PipTexture(cg->flight_gpu);
	if (!pip_texture)
		return;

	AeronTexture* mask_tex =
		TieCockpitRenderer_CrtMaskLookup(cg, snap->cockpit.mask_variant, snap->cockpit.classic_w);
	if (!mask_tex)
		return;
	TieCockpitRenderer_DrawPipCompose(cg, cmd, pass, pip_texture, mask_tex, coord_w, coord_h, dst_x, dst_y,
									  dst_w, dst_h);
}

/* PIP subsystem target box (engine: panel.c:2683 inside
 * panel_update3Dcrt). Always 4×4 classic px (no close-range
 * threshold) centred on the world-projected position of the
 * currently-selected subsystem on the target ship. Drawn AFTER the
 * 3DCRT compose so it sits on top of the composited PIP image at the
 * cockpit instruments[2] rect. Aspect-independent: HD does its own
 * PIP-camera projection using snapshot pip_cam_ori + pip_back_step
 * (the same back-step-distance-pre-subtracted trick TieCockpitRenderer_Draw3dcrt's
 * the flight PIP camera uses for far-target precision). */
static void TieCockpitRenderer_DrawPipTargetBox(TieCockpitRenderer* cg, AeronCommandBuffer* cmd,
												AeronRenderPass* pass, const TieCockpitRendererEntry* entry,
												int coord_w, int coord_h, const TieSnapshot* snap) {
	if (!snap->hud.target_subsystem_box_engine_ok)
		return;
	if (!snap->cockpit.pip_target_present)
		return;
	/* Engine path: panel_drawboxinxtrans only fires inside
	 * panel_update3Dcrt, which panel_updatepanel calls strictly when
	 * camera.pilotview == 0 (panel.c:355). Other views never call
	 * panel_update3Dcrt, so the box never rasterises into xtrans even
	 * when the cached s_pip_subsys_* fields remain valid. HD's
	 * snapshot publishes those fields as sticky state across ticks
	 * (so the PIP image survives the engine's pause), which means
	 * the box snapshot stays "engine_ok" in non-cockpit views too.
	 * Mirror the engine gate explicitly: view_idx == 0 only. */
	if (snap->cockpit.view_idx != 0)
		return;

	const TieHudInstrument* ins = &snap->hud.instruments[TIE_HUDI_CMD_3D_CRT];
	float dst_x, dst_y, dst_w, dst_h;
	TieCockpitRenderer_ResolvePipRect(entry, snap, ins->x, ins->y, ins->param1, ins->param2, &dst_x, &dst_y,
									  &dst_w, &dst_h);
	if (dst_w <= 0.0f || dst_h <= 0.0f)
		return;

	/* Engine final gate (panel.c:2689):
	 *     radar_enable && target_obj_idx < NUM_CRAFTS  (= 32)
	 * The engine_ok flag already implies "valid subsystem mesh on the
	 * target ship" (set inside the `if (mesh)` block at panel.c:2647);
	 * the NUM_CRAFTS gate keeps the box restricted to the 32 active-
	 * fighter slots — warheads (32..79), capital ships (80..119) and
	 * statics (>= 0x3800) are excluded even when engine_ok is true. */
	const uint16_t slot = snap->hud.target_obj_slot;
	if (slot >= 32u)
		return; /* NUM_CRAFTS = 32 */

	if (!TieCockpitRenderer_FlightWorldPosition(snap, slot))
		return;

	/* PIP camera via the shared precision-preserving recipe — same
	 * cam_pos_centered + view matrix the PIP ship rendering uses, so
	 * the box and the ship share the same float32 cancellation pattern
	 * (see TieRenderMath_BuildPipCamera comment). */
	float cam_pos[3];
	float view[16];
	TieRenderMath_BuildPipCamera(cam_pos, view, snap->cockpit.pip_back_step, snap->cockpit.pip_cam_ori);

	/* PIP projection. */
	float h_half = snap->cockpit.pip_fov_h_half_rad;
	float v_half = snap->cockpit.pip_fov_v_half_rad;
	if (h_half <= 0.0f)
		h_half = 0.5586f;
	if (v_half <= 0.0f)
		v_half = 0.3f;
	float proj[16];
	TieRenderMath_Mat4PerspectiveReverseZ(proj, h_half, v_half, 1.0f);
	float view_proj[16];
	TieRenderMath_Mat4Multiply(view_proj, proj, view);

	/* The target is the PIP origin, so its integral subsystem offset is
	 * already a precise scene-local point. */
	const float sub_wx = (float)snap->hud.target_subsystem_offset[0];
	const float sub_wy = (float)snap->hud.target_subsystem_offset[1];
	const float sub_wz = (float)snap->hud.target_subsystem_offset[2];

	float ndc_x, ndc_y, clip_w;
	if (!TieCockpitRenderer_ProjectWorldToNdc(view_proj, sub_wx, sub_wy, sub_wz, &ndc_x, &ndc_y, &clip_w))
		return;
	if (ndc_x < -1.0f || ndc_x > 1.0f)
		return;
	if (ndc_y < -1.0f || ndc_y > 1.0f)
		return;

	/* Engine arithmetic (panel.c:2693-2695):
	 *     sx = transfm2_getscreencoordx(...);              // projected x
	 *     sy = transfm2_getscreencoordy(...) - 2;          // projected y - 2
	 *     panel_drawboxinxtrans(sx - 2, sy, 4, 4, 0xCE);
	 * Box dimensions (4×4) and the -2 offset are classic-px; scale
	 * into the layout's ref frame so the box stays visible at HD
	 * sizes. */
	const float pip_proj_x_cl = dst_x + (ndc_x + 1.0f) * 0.5f * dst_w;
	const float pip_proj_y_cl = dst_y + (1.0f - ndc_y) * 0.5f * dst_h;

	/* Raw palette index 0xCE — same direct lookup as the radar bracket. */
	const uint32_t pal = snap->palette[0xCEu];
	float r, g, b;
	TieScene2dSrgb_PalToLinearRgb(pal, &r, &g, &b);

	float pixel_sx, pixel_sy;
	TieCockpitLayout_ClassicPixelSize(&entry->layout, (int)snap->cockpit.classic_w,
									  (int)snap->cockpit.classic_h, &pixel_sx, &pixel_sy);
	const int box_w = (int)(4.0f * pixel_sx + 0.5f);
	const int box_h = (int)(4.0f * pixel_sy + 0.5f);
	TieCockpitRenderer_BlitEngineHollowBox(cg, cmd, pass, coord_w, coord_h, pip_proj_x_cl - 2.0f * pixel_sx,
										   pip_proj_y_cl - 2.0f * pixel_sy, box_w, box_h, pixel_sx, pixel_sy,
										   r, g, b);
}

/* Per-view widget allow-list. The engine paints different widget
 * subsets per pilotview (panel_updatepanel:339-358). The full table
 * (view 0) runs every widget. Forward (19), threat (20), and the
 * side / padlock views run subsets — encoded as (idx, kind) gates
 * relative to the dispatch table. Returns true when (view, idx, kind)
 * should draw. */
static bool TieCockpitRenderer_ViewAllowsWidget(uint8_t view_idx, int idx,
												TieCockpitRendererHudWidgetKind kind) {
	/* Threat-weapon LED slots are only painted in threat view (20).
	 * In other views, engine's panel_updatethreatweapons never runs;
	 * leaving these on would paint frame-0 "absent" cels over unrelated
	 * cockpit areas (the slots carry threat-view layout positions). */
	if (idx >= TIE_HUDI_THREAT_ION && idx <= TIE_HUDI_THREAT_BEAM && view_idx != 20)
		return false;

	switch (view_idx) {
		case 0:
			/* Full HUD — every kind allowed. */
			return true;

		case 19: {
			/* Forward view (panel.c:354-358): radar + laser LEDs + gunsight
			 * + weapon-warnings + weapon-fire status levers. */
			if (kind == HUD_W_RADAR_DISC)
				return true;
			if (kind == HUD_W_LASER_LED_ROW)
				return true;
			if (kind == HUD_W_SHAPE_LEVER) {
				if (idx == TIE_HUDI_GUNSIGHT)
					return true;
				if (idx == TIE_HUDI_WARN_INCOMING)
					return true;
				if (idx == TIE_HUDI_WARN_LOCK)
					return true;
				if (idx == TIE_HUDI_WARN_IMPACT)
					return true;
				if (idx >= TIE_HUDI_WEAPON_FIRE_FIRST && idx <= TIE_HUDI_WEAPON_FIRE_LAST)
					return true;
			}
			return false;
		}

		case 20:
			/* Threat readout view — threat-weapon LEDs + threat shield/hull
			 * digit fields + the CMD-readout text slot (idx 90). */
			if (idx >= TIE_HUDI_THREAT_ION && idx <= TIE_HUDI_THREAT_HULL_PCT)
				return true;
			if (idx == 90)
				return true;
			return false;

		default:
			/* Side / look-around views (1..16, 21..27): engine makes no
			 * HUD writes here. Title view (17) is handled by text only.
			 * View 18 (panel hidden) gates upstream. */
			return false;
	}
}

/* Pilotview dispatch. Full view runs the whole widget table;
 * forward / threat / padlock variants are subsets via TieCockpitRenderer_ViewAllowsWidget. */
static void TieCockpitRenderer_DrawWidgetsForView(TieCockpitRenderer* cg, AeronCommandBuffer* cmd,
												  AeronRenderPass* pass, const TieCockpitRendererEntry* entry,
												  int coord_w, int coord_h, int first_idx, int last_idx,
												  const TieSnapshot* snap) {
	const uint8_t v = snap->cockpit.view_idx;
	if (v == 17 || v == 18)
		return; /* title=text-only, 18=hidden */

	if (first_idx < 0)
		first_idx = 0;
	if (last_idx >= TIE_MAX_HUD_INSTRUMENTS)
		last_idx = TIE_MAX_HUD_INSTRUMENTS - 1;
	for (int idx = first_idx; idx <= last_idx; ++idx) {
		const TieHudInstrument* ins = &snap->hud.instruments[idx];
		if (!TieCockpitCommon_InstrumentActive(ins))
			continue;
		TieCockpitRendererHudWidgetKind kind = (TieCockpitRendererHudWidgetKind)hud_widget_kind[idx];
		if (!TieCockpitRenderer_ViewAllowsWidget(v, idx, kind))
			continue;

		switch (kind) {
			case HUD_W_RADAR_DISC:
				TieCockpitRenderer_DrawRadarDisc(cg, cmd, pass, entry, coord_w, coord_h, idx, snap);
				break;
			case HUD_W_LASER_LED_ROW:
				TieCockpitRenderer_DrawLaserLedRow(cg, cmd, pass, entry, coord_w, coord_h, idx, snap);
				break;
			case HUD_W_3DCRT:
				TieCockpitRenderer_Draw3dcrt(cg, cmd, pass, entry, coord_w, coord_h, snap);
				break;
			case HUD_W_VERT_SLIDER:
				TieCockpitRenderer_DrawVertSlider(cg, cmd, pass, entry, coord_w, coord_h, idx, snap);
				break;
			case HUD_W_SHIELD_QUAD:
				TieCockpitRenderer_DrawShieldQuad(cg, cmd, pass, entry, coord_w, coord_h, idx, snap);
				break;
			case HUD_W_SHAPE_LEVER:
				TieCockpitRenderer_DrawShapeLever(cg, cmd, pass, entry, coord_w, coord_h, idx, snap);
				break;
			case HUD_W_BEAM_ARCBAR:
				TieCockpitRenderer_DrawBeamArcbar(cg, cmd, pass, entry, coord_w, coord_h, idx, snap);
				break;
			case HUD_W_DAMAGE_CRACK:
			case HUD_W_COVER:
				/* Drawn by TieCockpitRenderer_DrawDamageCracks / TieCockpitRenderer_DrawCovers which run
				 * BEFORE this pass — engine z-order requires the indicator
				 * plates and covers sit UNDER live widgets. */
				break;
			default:
				break;
		}
	}
}

static bool TieCockpitRenderer_CockpitRecordLists(TieCockpitRenderer* cg, AeronCommandBuffer* cmd, int rt_w,
												  int rt_h, const TieSnapshot* snap);
static bool TieCockpitRenderer_FlightHudRecordLists(TieCockpitRenderer* cg, AeronCommandBuffer* cmd, int rt_w,
													int rt_h, const TieSnapshot* snap);

bool TieCockpitRenderer_Prepare(TieCockpitRenderer* cg, AeronCommandBuffer* cmd, const TieSnapshot* snap) {
	if (!cg)
		return false;
	cg->pending_ready = false;
	cg->pending_entry = NULL;

	/* Consume a debug-UI reload request before the entry lookup so the
	 * active cockpit re-loads from disk this frame (others lazily). */
	if (s_cockpit_reload_request) {
		TieCockpitRenderer_CockpitReloadCaches(cg);
		s_cockpit_reload_request = false;
	}

	if (!cmd || !snap)
		return false;
	if (snap->scene_kind != TIE_SCENE_FLIGHT)
		return true;
	if (snap->replay_mode == 2)
		return true;

	/* Original-installation fonts used by global flight HUD text must be
	 * available even when no cockpit assets are needed in external view. */
	const uint16_t font_classic_w = snap->cockpit.classic_w ? snap->cockpit.classic_w : 640;
	if (cg->assets && !TieFlightAssetSource_IsRemastered(cg->assets) &&
		!TieCockpitRenderer_EnsureOriginalFonts(cg, cmd, font_classic_w))
		return false;

	int rt_w = 0, rt_h = 0;
	if (cg->flight_gpu)
		TieFlightRenderer_GetRtDims(cg->flight_gpu, &rt_w, &rt_h);
	if (rt_w <= 0 || rt_h <= 0) {
		Aeron_RequestFatalRendererError("flight HUD render-target dimensions");
		return false;
	}

	/* View 18 hides the cockpit panel, not framebuffer-level flight HUD. */
	if (snap->cockpit.view_idx == 18 || !snap->cockpit.view_name[0]) {
		cg->pending_ready = TieCockpitRenderer_FlightHudRecordLists(cg, cmd, rt_w, rt_h, snap);
		return cg->pending_ready;
	}

	/* Asset loads + PIP pre-pass + CRT-mask load all need the command
	 * buffer OUTSIDE any render pass (the backend forbids overlapping
	 * copy / render passes). All of this MUST run before the caller
	 * opens the flight-main render pass. */
	TieCockpitRendererEntry* entry = TieCockpitRenderer_FindOrAllocEntry(
		cg, snap->cockpit.view_name, snap->cockpit.classic_w, snap->cockpit.classic_h);
	if (!entry) {
		cg->pending_ready = TieCockpitRenderer_FlightHudRecordLists(cg, cmd, rt_w, rt_h, snap);
		return cg->pending_ready;
	}
	if (!TieCockpitRenderer_EnsureCockpitAssets(cg, cmd, entry, snap))
		return false;

	/* Nothing to draw if neither bitmap nor parts atlas loaded. */
	if (!entry->base_tex && !entry->parts_tex) {
		cg->pending_ready = TieCockpitRenderer_FlightHudRecordLists(cg, cmd, rt_w, rt_h, snap);
		return cg->pending_ready;
	}

	if (snap->cockpit.pip_target_present)
		(void)TieCockpitRenderer_EnsureCrtMask(cg, cmd, snap->cockpit.mask_variant, snap->cockpit.classic_w);

	/* Bake the static chrome layer if any keyed input changed. Runs here,
	 * before the flight-main pass opens, because it needs its own render
	 * pass. Needs the flight RT dims — only available via flight_gpu, which
	 * the prepare call site guarantees is bound. Without it the composite
	 * path stays disabled (chrome_valid false) and draw falls back to the
	 * per-frame direct draws. */
	int coord_w, coord_h;
	TieCockpitRenderer_ResolveCoordFrame(entry, snap, &coord_w, &coord_h);
	if (!TieCockpitRenderer_ChromeKeyMatches(cg, entry, snap, coord_w, coord_h, rt_w, rt_h) &&
		!TieCockpitRenderer_BakeCockpitChrome(cg, cmd, entry, snap, coord_w, coord_h, rt_w, rt_h))
		return false;

	cg->pending_entry = entry;
	cg->pending_space = TieCockpitRenderer_CockpitDrawSpace(coord_w, coord_h, rt_w, rt_h);
	cg->pending_ready = TieCockpitRenderer_CockpitRecordLists(cg, cmd, rt_w, rt_h, snap);
	return cg->pending_ready;
}

static bool TieCockpitRenderer_FlightHudRecordLists(TieCockpitRenderer* cg, AeronCommandBuffer* cmd, int rt_w,
													int rt_h, const TieSnapshot* snap) {
	if (!cg || !cmd || !snap)
		return false;
	int coord_w, coord_h;
	TieCockpitRenderer_ResolveCoordFrame(NULL, snap, &coord_w, &coord_h);
	cg->pending_space = TieCockpitRenderer_CockpitDrawSpace(coord_w, coord_h, rt_w, rt_h);

	TIE_GPU_PUSH(cmd, "Flight HUD");
	AeronDrawList_Begin(cg->before_pip_list, NULL, rt_w, rt_h, AERON_DRAWLIST2D_LOAD, NULL);
	AeronDrawList_Begin(cg->after_pip_list, NULL, rt_w, rt_h, AERON_DRAWLIST2D_LOAD, NULL);

	cg->record_list = cg->before_pip_list;
	const bool look_1998 = (cg->flight_gpu != NULL) && TieFlightRenderer_UsesSceneModels(cg->flight_gpu);
	TIE_GPU_MARKER(cmd, "Flight target box");
	TieCockpitRenderer_DrawMainTargetBox(cg, cmd, NULL, NULL, rt_w, rt_h, snap, look_1998);

	cg->record_list = cg->after_pip_list;
	TIE_GPU_MARKER(cmd, "Flight message bar");
	TieCockpitRenderer_DrawMessageBar(cg, cmd, NULL, snap);
	cg->record_list = NULL;

	bool ok = AeronDrawList_Prepare(cg->before_pip_list, cmd) != 0;
	ok = AeronDrawList_Prepare(cg->after_pip_list, cmd) != 0 && ok;
	TIE_GPU_POP(cmd);
	if (!ok)
		Aeron_RequestFatalRendererError("flight HUD draw-list preparation");
	return ok;
}

static bool TieCockpitRenderer_CockpitRecordLists(TieCockpitRenderer* cg, AeronCommandBuffer* cmd, int rt_w,
												  int rt_h, const TieSnapshot* snap) {
	if (!cg || !cmd || !snap || !cg->pending_entry)
		return false;
	const TieCockpitRendererEntry* entry = cg->pending_entry;
	const uint32_t asset_io_before = cg->draw_phase_asset_io_count;
	cg->draw_phase_active = true;

	/* Reference coord frame: layout's reference when a YAML is loaded,
	 * snapshot's classic_w/h otherwise. Drivers, blits, and cel sizes
	 * all live in this frame; TieCockpitRenderer_InsAnchor() rescales engine instrument
	 * positions into it. */
	int coord_w, coord_h;
	TieCockpitRenderer_ResolveCoordFrame(entry, snap, &coord_w, &coord_h);

	TIE_GPU_PUSH(cmd, "Cockpit HUD");

	AeronDrawList_Begin(cg->before_pip_list, NULL, rt_w, rt_h, AERON_DRAWLIST2D_LOAD, NULL);
	AeronDrawList_Begin(cg->after_pip_list, NULL, rt_w, rt_h, AERON_DRAWLIST2D_LOAD, NULL);
	cg->record_list = cg->before_pip_list;

	/* Original PANL images retain their decoded panel-area aspect inside
	 * TieCockpitRenderer_DrawCockpitBase: 640x456 for SVGA and 320x189 for VGA. */
	const int cockpit_area_h = (coord_h * 19) / 20;

	/* Target box paints before cockpit chrome so the canopy's alpha
	 * cutout occludes it where the frame is opaque. Look toggled by
	 * TieFlightRenderer model backend: scene meshes use 1998 corner brackets,
	 * false=1995 (hollow rect, fixed coord/48). */
	const bool look_1998 = (cg->flight_gpu != NULL) && TieFlightRenderer_UsesSceneModels(cg->flight_gpu);
	TIE_GPU_MARKER(cmd, "Flight target box");
	TieCockpitRenderer_DrawMainTargetBox(cg, cmd, NULL, entry, rt_w, rt_h, snap, look_1998);

	/* Static cockpit chrome — base bitmap + damage cracks + subsystem
	 * covers. These change only on view/craft switch, mirror toggle, or a
	 * subsystem knockout/repair, so they are pre-baked into chrome_rt
	 * (TieCockpitRenderer_Prepare) and composited here as a single coverage-rect
	 * pass — replacing 1 base blit + N crack/cover blits with one. When the
	 * bake is unavailable (no flight_gpu / RT create failed) the original
	 * per-frame direct draws run instead. Damage cracks and covers are
	 * panel-area cels within the base's opaque footprint, so the base
	 * coverage rects bound all chrome content. */
	if (cg->chrome_valid) {
		TIE_GPU_MARKER(cmd, "Cockpit chrome composite");
		TieCockpitRenderer_DrawCockpitChrome(cg, cmd, NULL, entry, coord_w, coord_h, rt_w, rt_h,
											 snap->cockpit.mirrored_view);
	} else {
		TIE_GPU_MARKER(cmd, "Cockpit base");
		TieCockpitRenderer_DrawCockpitBase(cg, cmd, NULL, entry, coord_w, coord_h, cockpit_area_h,
										   snap->cockpit.mirrored_view);
		TIE_GPU_MARKER(cmd, "Cockpit damage");
		TieCockpitRenderer_DrawDamageCracks(cg, cmd, NULL, entry, coord_w, coord_h, snap);
		TIE_GPU_MARKER(cmd, "Cockpit covers");
		TieCockpitRenderer_DrawCovers(cg, cmd, NULL, entry, coord_w, coord_h, snap);
	}
	/* Per-frame clearwindow rects for dynamic-text areas the cockpit
	 * base bitmap leaves with placeholder graphics. View-0 gated. */
	TieCockpitRenderer_DrawClearwindows(cg, cmd, NULL, entry, coord_w, coord_h, snap);
	TieCockpitRenderer_DrawWidgetsForView(cg, cmd, NULL, entry, coord_w, coord_h, 0, 1, snap);

	cg->record_list = cg->after_pip_list;
	TieCockpitRenderer_DrawWidgetsForView(cg, cmd, NULL, entry, coord_w, coord_h, 3,
										  TIE_MAX_HUD_INSTRUMENTS - 1, snap);
	/* PIP subsystem indicator — runs after the 3DCRT compose inside
	 * TieCockpitRenderer_DrawWidgetsForView, paints a small 4×4 box at the world-
	 * projected position of the selected component. */
	TieCockpitRenderer_DrawPipTargetBox(cg, cmd, NULL, entry, coord_w, coord_h, snap);
	/* Radar bracket is written by panel_updateradar, which the engine
	 * only calls in views 0 and 19 (panel_updatepanel:339-358). In other
	 * views bracket_x/y are stale from the last view-0 tick; painting
	 * them would overlay the side-view cockpit bitmap. */
	if (snap->cockpit.view_idx == 0 || snap->cockpit.view_idx == 19)
		TieCockpitRenderer_DrawRadarBracket(cg, cmd, NULL, entry, coord_w, coord_h, snap);

	/* Cockpit text — clock + HUD/CMD readouts. Both builders share
	 * the same TieScene2dTextSpace so build_hud_text's measurements
	 * match what TieScene2dText_DrawFestringInSpace will paint.
	 * MICRO64 atlas extracted at 4.5× SVGA. */
	if (cg->text_renderer) {
		const float atlas_scale = TieCockpitRenderer_CockpitAtlasScaleFor(entry, coord_h);
		const TieScene2dTextSpace cockpit_space = {
			.classic_w = coord_w,
			.classic_h = coord_h,
			.atlas_scale_x = atlas_scale,
			.atlas_scale_y = atlas_scale,
			.space_between_classic = 0.0f,
			.fit = TIE_SCENE2D_TEXT_FIT_FILL,
		};

		/* Build a remapped instrument array once for both text builders.
		 * For 4:3 layouts the helper produces a verbatim copy of
		 * snap->hud.instruments; for 16:9 layouts it rescales x/y into
		 * the layout reference frame (authored anchor or piecewise). */
		TieHudInstrument instruments_remapped[TIE_MAX_HUD_INSTRUMENTS];
		for (int i = 0; i < TIE_MAX_HUD_INSTRUMENTS; ++i) {
			instruments_remapped[i] = snap->hud.instruments[i];
			float ax, ay;
			TieCockpitRenderer_InsAnchor(entry, i, snap->hud.instruments[i].x, snap->hud.instruments[i].y,
										 snap, &ax, &ay);
			if (ax < 0.0f)
				ax = 0.0f;
			if (ay < 0.0f)
				ay = 0.0f;
			if (ax > 65535.0f)
				ax = 65535.0f;
			if (ay > 65535.0f)
				ay = 65535.0f;
			instruments_remapped[i].x = (uint16_t)(ax + 0.5f);
			instruments_remapped[i].y = (uint16_t)(ay + 0.5f);
		}

		TieUIText clock_recs[2];
		int clock_n = TieCockpitText_BuildclockText(snap, instruments_remapped, clock_recs,
													(int)(sizeof clock_recs / sizeof clock_recs[0]));
		if (clock_n > 0) {
			const TieCockpitRendererDrawSpace* space = &cg->pending_space;
			TieScene2dTextRenderer_RecordFestringScaled(cg->text_renderer, cg->record_list, coord_w, coord_h,
														&cockpit_space, clock_recs, clock_n, snap->palette,
														space->fit_x, space->fit_y, space->scale_x,
														space->scale_y, space->target_w, space->target_h);
		}

		/* HUD / CMD text. 64 records is comfortably above the
		 * worst case (CMD ~13 + threat ~14 + speed/throttle/etc.). */
		TieUIText hud_recs[64];
		int hud_n = TieCockpitText_BuildHudText(cg->text_renderer, snap, instruments_remapped, &entry->layout,
												&cockpit_space, hud_recs,
												(int)(sizeof hud_recs / sizeof hud_recs[0]));
		if (hud_n > 0) {
			const TieCockpitRendererDrawSpace* space = &cg->pending_space;
			TieScene2dTextRenderer_RecordFestringScaled(cg->text_renderer, cg->record_list, coord_w, coord_h,
														&cockpit_space, hud_recs, hud_n, snap->palette,
														space->fit_x, space->fit_y, space->scale_x,
														space->scale_y, space->target_w, space->target_h);
		}
	}
	TIE_GPU_MARKER(cmd, "Flight message bar");
	TieCockpitRenderer_DrawMessageBar(cg, cmd, NULL, snap);

	cg->record_list = NULL;
	cg->draw_phase_active = false;
	if (cg->draw_phase_asset_io_count != asset_io_before)
		Aeron_LogWarn("tie.cockpit", "asset I/O occurred while recording cockpit draw commands (%u total)",
					  cg->draw_phase_asset_io_count);
	bool ok = AeronDrawList_Prepare(cg->before_pip_list, cmd) != 0;
	ok = AeronDrawList_Prepare(cg->after_pip_list, cmd) != 0 && ok;
	TIE_GPU_POP(cmd); /* "Cockpit HUD" */
	if (!ok)
		Aeron_RequestFatalRendererError("cockpit draw-list preparation");
	return ok;
}

void TieCockpitRenderer_RenderInPass(TieCockpitRenderer* cg, AeronCommandBuffer* cmd, AeronRenderPass* pass,
									 AeronRenderTarget* target, const TieSnapshot* snap) {
	if (!cg || !cmd || !pass || !target || !snap || !cg->pending_ready)
		return;

	AeronDrawList_RenderIntoPass(cg->before_pip_list, cmd, pass, target);

	const TieHudInstrument* pip = &snap->hud.instruments[2];
	TieCockpitRendererHudWidgetKind kind = (TieCockpitRendererHudWidgetKind)hud_widget_kind[2];
	if (cg->pending_entry && TieCockpitCommon_InstrumentActive(pip) &&
		TieCockpitRenderer_ViewAllowsWidget(snap->cockpit.view_idx, 2, kind)) {
		TieCockpitRenderer_Draw3dcrt(cg, cmd, pass, cg->pending_entry, cg->pending_space.coord_w,
									 cg->pending_space.coord_h, snap);
	}

	AeronDrawList_RenderIntoPass(cg->after_pip_list, cmd, pass, target);
}
