/*
 * Classic vertices use half a world unit per raw coordinate because the
 * Q15 transform shifts products by 16 bits. Mesh transforms also negate the
 * forward basis column, reproducing the classic (side, -forward, up) reflected
 * basis. Positive classic eye Y points down, so the projection negates Y when
 * mapping to NDC.
 */

#include "tie_remaster/flight/renderer.h"

#include "aeron/log.h"
#include "aeron/render.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "aeron/asset/opt_model.h"
#include "aeron/scene/bloom.h"
#include "aeron/scene/ktx2_reader.h"
#include "tie_remaster/flight/backdrops.h"
#include "tie_remaster/flight/billboards.h"
#include "tie_remaster/flight/hud_geometry.h"
#include "tie_remaster/flight/hyperstars.h"
#include "tie_remaster/flight/pbr.h"
#include "tie_remaster/flight/point_lights.h"
#include "tie_remaster/flight/render_math.h"
#include "tie_remaster/flight/renderer_internal.h"
#include "tie_remaster/flight/sprite_cache.h"
#include "tie_remaster/flight/stars.h"
#include "tie_remaster/gpu_debug.h"
#include "tie_runtime/audio/config.h"
#include "tie_runtime/diagnostics/diagnostics.h"
#include "tie_runtime/display/classic_display.h"
#include "tie_runtime/display/classic_framebuffer.h"
#include "tie_runtime/flight_assets/assets.h"
#include "tie_runtime/flight_assets/model_types.h"
#include "tie_runtime/flight_assets/ship_model_converter.h"
#include "tie_runtime/input/input.h"
#include "tie_runtime/runtime/exports.h"
#include "tie_runtime/runtime/profile.h"
#include "tie_runtime/snapshot/snapshot.h"
#include "tie_runtime/storage/storage.h"

#ifndef TIE_SHADER_DIR
#define TIE_SHADER_DIR "shaders"
#endif

/* Single-instance back-pointer for tools that aren't on the TieFlightRenderer*
 * call path (the ImGui debug inspector). The host only ever creates
 * one TieFlightRenderer at a time, so a static pointer is sufficient. Set on
 * the way out of TieFlightRenderer_Init and cleared by TieFlightRenderer_Shutdown. */
static TieFlightRenderer* s_current_flight_gpu = NULL;

TieFlightRenderer* TieFlightRenderer_Current(void) { return s_current_flight_gpu; }

static AeronTemporalMode TieFlightRenderer_TemporalMode(TieFlightTemporalMode mode) {
	switch (mode) {
		case TIE_FLIGHT_TEMPORAL_OFF:
			return AERON_TEMPORAL_OFF;
		case TIE_FLIGHT_TEMPORAL_NATIVE_AA:
			return AERON_TEMPORAL_NATIVE_AA;
		case TIE_FLIGHT_TEMPORAL_QUALITY:
			return AERON_TEMPORAL_QUALITY;
		case TIE_FLIGHT_TEMPORAL_BALANCED:
			return AERON_TEMPORAL_BALANCED;
		case TIE_FLIGHT_TEMPORAL_PERFORMANCE:
			return AERON_TEMPORAL_PERFORMANCE;
		default:
			return AERON_TEMPORAL_NATIVE_AA;
	}
}

/* Linear+REPEAT sampler with mipmap LINEAR — used by the OPT atlas and
 * the glTF channel atlases. Anisotropy is configurable via the bundle's
 * `render:` block; passing 1.0 here disables it. The backend clamps to its
 * own's max behind the scenes, so passing 16 on a backend that caps
 * lower is harmless. */
static AeronSampler* TieFlightRenderer_CreateMeshSampler(float anisotropy) {
	if (anisotropy < 1.0f)
		anisotropy = 1.0f;
	if (anisotropy > 16.0f)
		anisotropy = 16.0f;
	return Aeron_CreateSampler(&(AeronSamplerDesc) {
		.min_filter = AERON_FILTER_LINEAR,
		.mag_filter = AERON_FILTER_LINEAR,
		.mip_filter = AERON_FILTER_LINEAR,
		.address_u = AERON_ADDRESS_REPEAT,
		.address_v = AERON_ADDRESS_REPEAT,
		.address_w = AERON_ADDRESS_CLAMP_TO_EDGE,
		.min_lod = 0.0f,
		.max_lod = 1000.0f,
		.enable_anisotropy = anisotropy > 1.0f,
		.max_anisotropy = anisotropy,
	});
}

/* ===== Lifecycle ===================================================== */

static void TieFlightRenderer_DestroySizedResources(TieFlightRenderer* g) {
	if (!g)
		return;
	g->scene_rt = NULL;
	g->mb_pose_host_valid = false;
	g->mb_velocity_span_us = 0;
	if (g->bloom)
		AeronSceneBloom_Destroy(g->bloom);
	if (g->scene)
		AeronScene_Destroy(g->scene);
	g->bloom = NULL;
	g->scene = NULL;
	g->rt_w = 0;
	g->rt_h = 0;
}

static bool TieFlightRenderer_CreateSizedResources(TieFlightRenderer* g, int rt_w, int rt_h) {
	if (!g || rt_w <= 0 || rt_h <= 0)
		return false;

	g->scene = AeronScene_Create(&(AeronScene3DDesc) {
		.rt_width = rt_w,
		.rt_height = rt_h,
		.color_format = g->rt_format,
		.with_normal_rt = 1,
		.sample_count = g->requested_sample_count,
		.temporal_mode = g->requested_sample_count > AERON_SAMPLE_COUNT_1 ? AERON_TEMPORAL_OFF : g->fsr_mode,
		.temporal_sharpness = g->fsr_sharpness,
		.view_space_to_meters = AERON_OPT_METERS_PER_UNIT,
	});
	if (!g->scene)
		return false;
	g->sample_count = AeronScene_SampleCount(g->scene);
	g->bloom = AeronSceneBloom_Create(rt_w, rt_h);
	if (!g->bloom) {
		AeronScene_Destroy(g->scene);
		g->scene = NULL;
		return false;
	}

	g->scene_rt = AeronScene_ColorRt(g->scene);
	g->rt_w = rt_w;
	g->rt_h = rt_h;
	g->last_bloom_bar_y_uv = 1.0f;
	g->fsr_reset_pending = true;
	return true;
}

TieFlightRenderer* TieFlightRenderer_Init(int rt_w, int rt_h, AeronTextureFormat rt_format_u32,
										  AeronTextureFormat target_format_u32, const char* remaster_dir,
										  const TieFlightAssetSource* assets,
										  const TieFlightRenderConfig* render, const TieFlightPbrConfig* pbr,
										  const TieFlightPointLightParams* point_lights) {
	if (!render || !pbr || !point_lights || rt_w <= 0 || rt_h <= 0)
		return NULL;

	TieFlightRenderer* g = (TieFlightRenderer*)calloc(1, sizeof *g);
	if (!g) {
		Aeron_RequestFatalRendererError("flight renderer allocation");
		return NULL;
	}
	g->rt_format = (AeronTextureFormat)rt_format_u32;
	g->target_format = (AeronTextureFormat)target_format_u32;
	g->requested_sample_count = render->msaa_samples == 8   ? AERON_SAMPLE_COUNT_8
								: render->msaa_samples == 4 ? AERON_SAMPLE_COUNT_4
								: render->msaa_samples == 2 ? AERON_SAMPLE_COUNT_2
															: AERON_SAMPLE_COUNT_1;
	g->sample_count = g->requested_sample_count;
	g->fsr_mode = g->requested_sample_count > AERON_SAMPLE_COUNT_1
					  ? AERON_TEMPORAL_OFF
					  : TieFlightRenderer_TemporalMode(render->temporal_mode);
	g->fsr_sharpness = render->temporal_sharpness;
	g->starfield_style = render->starfield_style;
	g->assets = assets;
	g->last_bloom_bar_y_uv = 1.0f; /* identity gate until first apply */

	g->scene_model_backend = assets && !TieFlightAssetSource_IsTie95(assets);
	g->scene_reload_one_idx = 0xFFFFu;        /* sentinel = no per-species reload pending */
	g->last_warmed_mission_gen = 0xFFFFFFFFu; /* sentinel = warm on first prep */

	/* Scene renderer — owns the HDR color RT, the sampled D32 depth
	 * RT, the octahedral normal G-buffer (SSAO input), the pbr mesh
	 * pipelines, and the SSAO / motion-blur post chains. The classic /
	 * billboard / hyperstar / cockpit pipelines below stay TIE-side and
	 * enter its passes via hooks, so they keep targeting rt_format +
	 * the same normal/depth formats. */
	if (!TieFlightRenderer_CreateSizedResources(g, rt_w, rt_h)) {
		Aeron_RequestFatalRendererError("flight scene creation");
		TieFlightRenderer_Shutdown(g);
		return NULL;
	}
	g->pip_scene = AeronScene_Create(&(AeronScene3DDesc) {
		.rt_width = TIE_FLIGHT_PIP_W,
		.rt_height = TIE_FLIGHT_PIP_H,
		.color_format = g->rt_format,
		.with_normal_rt = 0,
		.sample_count = AERON_SAMPLE_COUNT_1,
		.temporal_mode = AERON_TEMPORAL_OFF,
		.view_space_to_meters = AERON_OPT_METERS_PER_UNIT,
	});
	g->classic_pip_color_rt = Aeron_CreateRenderTarget(&(AeronRenderTargetDesc) {
		.width = TIE_FLIGHT_PIP_W,
		.height = TIE_FLIGHT_PIP_H,
		.format = g->rt_format,
		.sample_count = AERON_SAMPLE_COUNT_1,
	});
	g->classic_pip_depth_rt = Aeron_CreateDepthTarget(&(AeronDepthTargetDesc) {
		.width = TIE_FLIGHT_PIP_W,
		.height = TIE_FLIGHT_PIP_H,
		.format = AERON_TEXTURE_FORMAT_D32_FLOAT,
		.sample_count = AERON_SAMPLE_COUNT_1,
	});
	if (!g->pip_scene || !g->classic_pip_color_rt || !g->classic_pip_depth_rt) {
		Aeron_RequestFatalRendererError("flight PIP resource creation");
		TieFlightRenderer_Shutdown(g);
		return NULL;
	}
	g->classic_mesh_tables.buffer = Aeron_CreateBuffer(&(AeronBufferDesc) {
		.usage = AERON_BUFFER_USAGE_STORAGE,
		.size = sizeof g->classic_mesh_tables.tables,
		.memory_usage = AERON_MEMORY_USAGE_DYNAMIC,
	});
	if (!g->classic_mesh_tables.buffer) {
		Aeron_RequestFatalRendererError("classic mesh-table buffer creation");
		TieFlightRenderer_Shutdown(g);
		return NULL;
	}

	/* Shader bindings per the HLSL register layouts:
	 *   mesh_vs   — TieFlightMeshVertexUniforms (slot 0), mesh-table storage (slot 0),
	 *               and LightBuffer (slot 1) for the Gouraud local-light
	 *               accumulator.
	 *   mesh_ps   — 2 samplers (materialcolors index LUT + palette),
	 *               1 uniform buffer (TieFlightMeshPixelUniforms), 2 storage
	 *               buffers (decal records + verts). */
	g->mesh_vs = TieFlightRenderer_CompileShader("flight_mesh.vert", AERON_SHADER_STAGE_VERTEX, 0, 2, 1);
	g->mesh_classic_lut_ps =
		TieFlightRenderer_CompileShader("flight_mesh_classic_lut.frag", AERON_SHADER_STAGE_FRAGMENT, 2, 1, 2);
	/* line_vs binds TieFlightMeshVertexUniforms (slot 0), TieFlightLineVertexUniforms (slot 1),
	 * and mesh-table storage (slot 0). */
	g->line_vs = TieFlightRenderer_CompileShader("flight_line.vert", AERON_SHADER_STAGE_VERTEX, 0, 2, 1);
	if (g->mesh_vs && g->mesh_classic_lut_ps) {
		g->mesh_pipeline = TieFlightRenderer_CreateMeshPipeline(g->mesh_vs, g->mesh_classic_lut_ps,
																g->rt_format, g->sample_count);
		if (!g->mesh_pipeline)
			Aeron_LogError("tie.flight", "mesh pipeline creation failed");
	}
	if (g->line_vs && g->mesh_classic_lut_ps) {
		g->line_pipeline = TieFlightRenderer_CreateLinePipeline(g->line_vs, g->mesh_classic_lut_ps,
																g->rt_format, g->sample_count);
		if (!g->line_pipeline)
			Aeron_LogError("tie.flight", "line pipeline creation failed");
		g->bolt_line_pipeline = TieFlightRenderer_CreateBoltLinePipeline(g->line_vs, g->mesh_classic_lut_ps,
																		 g->rt_format, g->sample_count);
		if (!g->bolt_line_pipeline)
			Aeron_LogError("tie.flight", "bolt-line pipeline creation failed");
	}
	if (g->mesh_vs && g->mesh_classic_lut_ps)
		g->classic_pip_mesh_pipeline = TieFlightRenderer_CreateMeshPipeline(
			g->mesh_vs, g->mesh_classic_lut_ps, g->rt_format, AERON_SAMPLE_COUNT_1);
	if (g->line_vs && g->mesh_classic_lut_ps) {
		g->classic_pip_line_pipeline = TieFlightRenderer_CreateLinePipeline(
			g->line_vs, g->mesh_classic_lut_ps, g->rt_format, AERON_SAMPLE_COUNT_1);
		g->classic_pip_bolt_line_pipeline = TieFlightRenderer_CreateBoltLinePipeline(
			g->line_vs, g->mesh_classic_lut_ps, g->rt_format, AERON_SAMPLE_COUNT_1);
	}

	/* PBR shading state — global tuning uniforms and the per-mission
	 * world-ambient cube consumed by Aeron's cooked-glTF mesh shader. */
	TieFlightPbr_Init(remaster_dir, pbr);
	TieFlightPointLights_Init(point_lights);

	/* Mesh-atlas sampler shared by both scene-model providers. */
	g->opt_sampler = TieFlightRenderer_CreateMeshSampler(render->anisotropy);

	/* The cooked-glb (pbr) mesh / prepass / forward pipelines and their
	 * shaders live in aeron_scene's pbr class now (lazy-init on the
	 * first AeronScene_Render); the 1x1 white placeholder texture moved
	 * with them. The sky-cube draw moved to AeronScene_SetSkyCube —
	 * this application only selects + loads the cube textures. */

	/* Aeron owns the SDR/HDR tonemap shaders and pipelines. Create chains for
	 * the application target and current swapchain formats. */
	g->present_chain = AeronScenePresentChain_Create(g->target_format);
	if (!g->present_chain)
		Aeron_LogError("tie.flight", "present-chain creation failed");
	g->swapchain_format = Aeron_SwapchainFormat();
	g->swapchain_present_chain = AeronScenePresentChain_Create(g->swapchain_format);
	if (!g->swapchain_present_chain)
		Aeron_LogError("tie.flight", "swapchain present-chain creation failed");

	/* SSAO chain + motion-blur velocity generation / resolve moved to
	 * aeron_scene's post stack (lazy-init on first use, gated by the
	 * AeronScenePostDesc that TieFlightRenderer_Prep forwards each frame).
	 * Only the runtime knobs remain here. */
	g->mb_quality = MB_OFF;
	g->mb_shutter = 0.5f;
	g->mb_velocity_viz = false;
	/* Keep the blur on a paused frame by default — a paused frame of a
	 * moving scene legitimately carries motion blur. */
	g->mb_pause_keep_blur = true;
	/* Blur camera motion by default; the inspector can disable it to keep
	 * only object motion blur (panning a static scene then stays crisp). */
	g->mb_camera_blur = true;
	g->fsr_mode = AERON_TEMPORAL_NATIVE_AA;
	g->fsr_sharpness = 0.0f;
	g->fsr_reset_pending = true;
	/* Scene RT defaults to the scene's HDR color RT; TieFlightRenderer_Prep
	 * repoints it at the scene's mb_rt on frames where the motion-blur
	 * resolve runs (AeronScene_SceneRt). */
	g->scene_rt = AeronScene_ColorRt(g->scene);

	g->ssao = render->ssao;
	g->fsr_mode = g->requested_sample_count > AERON_SAMPLE_COUNT_1
					  ? AERON_TEMPORAL_OFF
					  : TieFlightRenderer_TemporalMode(render->temporal_mode);
	g->shadows = render->shadows;

	/* materialcolors LUT — 16 wide (light steps) × 45 tall (rows).
	 * R8_UNORM (not R8_UINT): SDL_GPU forbids USAGE_SAMPLER on
	 * integer-format textures, and we bind through the sampler
	 * register slot. The shader's `.Load()` returns float [0..1]; it
	 * decodes back to the original byte by *255 + 0.5 rounding. */
	g->materialcolors_tex = TieFlightRenderer_CreateDataTex(AERON_TEXTURE_FORMAT_R8_UNORM, 16, 45, 1);
	TIE_GPU_NAME_TEXTURE(dev, g->materialcolors_tex, "flight.materialcolors_lut");

	/* Snapshot palette — 256×1 8-bits-per-channel. */
	g->palette_tex = TieFlightRenderer_CreateDataTex(AERON_TEXTURE_FORMAT_BGRA8_SRGB, 256, 1, 4);
	TIE_GPU_NAME_TEXTURE(dev, g->palette_tex, "flight.palette_lut");

	g->sampler = Aeron_CreateSampler(&(AeronSamplerDesc) {
		.min_filter = AERON_FILTER_NEAREST,
		.mag_filter = AERON_FILTER_NEAREST,
		.mip_filter = AERON_FILTER_NEAREST,
		.address_u = AERON_ADDRESS_CLAMP_TO_EDGE,
		.address_v = AERON_ADDRESS_CLAMP_TO_EDGE,
		.address_w = AERON_ADDRESS_CLAMP_TO_EDGE,
		.min_lod = 0.0f,
		.max_lod = 0.0f,
	});

	/* Linear-filter sampler (palette/materialcolors LUT + present RT
	 * binds; the sky-cube sample uses the scene's own sampler now). */
	g->sampler_linear = Aeron_CreateSampler(&(AeronSamplerDesc) {
		.min_filter = AERON_FILTER_LINEAR,
		.mag_filter = AERON_FILTER_LINEAR,
		.mip_filter = AERON_FILTER_LINEAR,
		.address_u = AERON_ADDRESS_CLAMP_TO_EDGE,
		.address_v = AERON_ADDRESS_CLAMP_TO_EDGE,
		.address_w = AERON_ADDRESS_CLAMP_TO_EDGE,
		.min_lod = 0.0f,
		.max_lod = 1000.0f,
	});

	g->skybox_loaded_battle = -1;

	/* Billboards and backdrops borrow one mission sprite cache. */
	g->sprites = TieFlightSpriteCache_Create(assets);
	g->billboards = TieFlightBillboards_Create(g->sprites);
	g->backdrop = TieFlightBackdrop_Create(g->sprites);
	g->stars = TieFlightStars_Create(g->rt_format);

	/* HD hyperspace-streak pass. No asset-bundle dependency. */
	g->hyperstars = TieFlightHyperstars_Create(g->rt_format);

	if (!g->mesh_vs || !g->mesh_classic_lut_ps || !g->line_vs || !g->mesh_pipeline || !g->line_pipeline ||
		!g->bolt_line_pipeline || !g->classic_pip_mesh_pipeline || !g->classic_pip_line_pipeline ||
		!g->classic_pip_bolt_line_pipeline || !g->opt_sampler || !g->present_chain ||
		!g->materialcolors_tex || !g->swapchain_present_chain || !g->palette_tex || !g->sampler ||
		!g->sampler_linear || !g->sprites || !g->billboards || !g->backdrop || !g->stars || !g->hyperstars ||
		!g->bloom) {
		Aeron_RequestFatalRendererError("flight renderer resource creation");
		TieFlightRenderer_Shutdown(g);
		return NULL;
	}

	/* Motion blur on by default at the High tier. The scene allocates
	 * the velocity / tile / resolve RTs lazily on first use. Still
	 * Scene-model-only at draw time.
	 * Toggle off / down via the Motion Blur inspector. */
	g->mb_quality = (MbQuality)render->motion_blur_quality;
	g->mb_shutter = render->motion_blur_shutter;

	s_current_flight_gpu = g;
	return g;
}

void TieFlightRenderer_ReleaseMissionAssets(TieFlightRenderer* g) {
	if (!g)
		return;
	for (int i = 0; i < TIE_FLIGHT_MAX_SPECIES; ++i) {
		TieFlightRenderer_ReleaseSpeciesMesh(g, &g->meshes[i]);
		TieFlightRenderer_ReleaseSceneSpeciesShip(g, &g->scene_ships[i]);
	}
	TieFlightSpriteCache_ReleaseMissionAssets(g->sprites);
	if (g->skybox_cube) {
		Aeron_DestroyTexture(g->skybox_cube);
		g->skybox_cube = NULL;
	}
	g->skybox_loaded_battle = -1;
	g->last_warmed_mission_gen = UINT32_MAX;
	g->fsr_reset_pending = true;
	g->scene_reload_all = false;
	g->scene_reload_one_idx = 0xFFFFu;
}

void TieFlightRenderer_Shutdown(TieFlightRenderer* g) {
	if (!g)
		return;
	TieFlightRenderer_ReleaseMissionAssets(g);
	if (g->opt_sampler)
		Aeron_DestroySampler(g->opt_sampler);
	if (g->billboards)
		TieFlightBillboards_Destroy(g->billboards);
	if (g->backdrop)
		TieFlightBackdrop_Destroy(g->backdrop);
	if (g->sprites)
		TieFlightSpriteCache_Destroy(g->sprites);
	if (g->stars)
		TieFlightStars_Destroy(g->stars);
	if (g->hyperstars)
		TieFlightHyperstars_Destroy(g->hyperstars);
	TieFlightRenderer_DestroySizedResources(g);
	if (g->mesh_pipeline)
		Aeron_DestroyGraphicsPipeline(g->mesh_pipeline);
	if (g->line_pipeline)
		Aeron_DestroyGraphicsPipeline(g->line_pipeline);
	if (g->bolt_line_pipeline)
		Aeron_DestroyGraphicsPipeline(g->bolt_line_pipeline);
	if (g->classic_pip_mesh_pipeline)
		Aeron_DestroyGraphicsPipeline(g->classic_pip_mesh_pipeline);
	if (g->classic_pip_line_pipeline)
		Aeron_DestroyGraphicsPipeline(g->classic_pip_line_pipeline);
	if (g->classic_pip_bolt_line_pipeline)
		Aeron_DestroyGraphicsPipeline(g->classic_pip_bolt_line_pipeline);
	if (g->present_chain)
		AeronScenePresentChain_Destroy(g->present_chain);
	if (g->swapchain_present_chain)
		AeronScenePresentChain_Destroy(g->swapchain_present_chain);
	TieFlightPbr_Shutdown();
	if (g->mesh_vs)
		Aeron_DestroyShader(g->mesh_vs);
	if (g->mesh_classic_lut_ps)
		Aeron_DestroyShader(g->mesh_classic_lut_ps);
	if (g->line_vs)
		Aeron_DestroyShader(g->line_vs);
	if (g->sampler)
		Aeron_DestroySampler(g->sampler);
	if (g->sampler_linear)
		Aeron_DestroySampler(g->sampler_linear);
	if (g->materialcolors_tex)
		Aeron_DestroyTexture(g->materialcolors_tex);
	if (g->palette_tex)
		Aeron_DestroyTexture(g->palette_tex);
	if (g->pip_scene)
		AeronScene_Destroy(g->pip_scene);
	if (g->classic_pip_color_rt)
		Aeron_DestroyRenderTarget(g->classic_pip_color_rt);
	if (g->classic_pip_depth_rt)
		Aeron_DestroyDepthTarget(g->classic_pip_depth_rt);
	if (g->classic_mesh_tables.buffer)
		Aeron_DestroyBuffer(g->classic_mesh_tables.buffer);
	if (s_current_flight_gpu == g)
		s_current_flight_gpu = NULL;
	free(g);
}

void TieFlightRenderer_InvalidateHistory(TieFlightRenderer* g) {
	if (!g)
		return;
	g->mb_pose_host_valid = false;
	g->mb_velocity_span_us = 0;
	g->fsr_reset_pending = true;
}

bool TieFlightRenderer_EnsureOutputSize(TieFlightRenderer* g, int rt_w, int rt_h) {
	if (!g || rt_w <= 0 || rt_h <= 0)
		return false;
	if (g->rt_w == rt_w && g->rt_h == rt_h)
		return true;
	TieFlightRenderer_DestroySizedResources(g);
	if (!TieFlightRenderer_CreateSizedResources(g, rt_w, rt_h)) {
		Aeron_RequestFatalRendererError("flight sized-resource recreation");
		return false;
	}
	Aeron_LogInfo("tie.flight", "render targets resized to %dx%d", rt_w, rt_h);
	return true;
}

static bool TieFlightRenderer_RebuildClassicPipelines(TieFlightRenderer* g) {
	if (g->mesh_pipeline)
		Aeron_DestroyGraphicsPipeline(g->mesh_pipeline);
	if (g->line_pipeline)
		Aeron_DestroyGraphicsPipeline(g->line_pipeline);
	if (g->bolt_line_pipeline)
		Aeron_DestroyGraphicsPipeline(g->bolt_line_pipeline);
	g->mesh_pipeline = TieFlightRenderer_CreateMeshPipeline(g->mesh_vs, g->mesh_classic_lut_ps, g->rt_format,
															g->sample_count);
	g->line_pipeline = TieFlightRenderer_CreateLinePipeline(g->line_vs, g->mesh_classic_lut_ps, g->rt_format,
															g->sample_count);
	g->bolt_line_pipeline = TieFlightRenderer_CreateBoltLinePipeline(g->line_vs, g->mesh_classic_lut_ps,
																	 g->rt_format, g->sample_count);
	return g->mesh_pipeline && g->line_pipeline && g->bolt_line_pipeline;
}

bool TieFlightRenderer_ApplyQuality(TieFlightRenderer* g, int ssao_quality, bool shadows_enabled,
									int shadow_atlas_size, int fsr_mode, float fsr_sharpness,
									int motion_blur_quality, float motion_blur_shutter, int msaa_samples) {
	if (!g)
		return false;
	const AeronTextureFormat swapchain_format = Aeron_SwapchainFormat();
	if (swapchain_format != g->swapchain_format) {
		AeronScenePresentChain* replacement = AeronScenePresentChain_Create(swapchain_format);
		if (!replacement) {
			Aeron_RequestFatalRendererError("flight swapchain present-chain rebuild");
			return false;
		}
		AeronScenePresentChain_Destroy(g->swapchain_present_chain);
		g->swapchain_present_chain = replacement;
		g->swapchain_format = swapchain_format;
	}
	const AeronSampleCount requested_samples = msaa_samples == 8   ? AERON_SAMPLE_COUNT_8
											   : msaa_samples == 4 ? AERON_SAMPLE_COUNT_4
											   : msaa_samples == 2 ? AERON_SAMPLE_COUNT_2
																   : AERON_SAMPLE_COUNT_1;
	TieFlightRenderer_SsaoSetQuality(g, (SsaoQuality)ssao_quality);
	TieFlightShadowSettings shadows = g->shadows;
	shadows.enabled = shadows_enabled;
	shadows.atlas_size = shadow_atlas_size;
	TieFlightRenderer_ShadowsSet(g, &shadows);
	g->fsr_mode = requested_samples > AERON_SAMPLE_COUNT_1
					  ? AERON_TEMPORAL_OFF
					  : TieFlightRenderer_TemporalMode((TieFlightTemporalMode)fsr_mode);
	g->fsr_sharpness = fsr_sharpness;
	g->fsr_reset_pending = true;
	TieFlightRenderer_MbSetQuality(g, (MbQuality)motion_blur_quality);
	TieFlightRenderer_MbSetShutter(g, motion_blur_shutter);
	if (requested_samples == g->requested_sample_count)
		return true;
	const int width = g->rt_w;
	const int height = g->rt_h;
	g->requested_sample_count = requested_samples;
	TieFlightRenderer_DestroySizedResources(g);
	if (!TieFlightRenderer_CreateSizedResources(g, width, height) ||
		!TieFlightRenderer_RebuildClassicPipelines(g)) {
		Aeron_RequestFatalRendererError("flight MSAA resource rebuild");
		return false;
	}
	return true;
}

void TieFlightRenderer_SetStarfieldStyle(TieFlightRenderer* g, int style) {
	if (g && style >= TIE_FLIGHT_STARFIELD_STYLE_TIE95 && style <= TIE_FLIGHT_STARFIELD_STYLE_TIE98)
		g->starfield_style = (TieFlightStarfieldStyle)style;
}

void TieFlightRenderer_SetCockpit(TieFlightRenderer* g, struct TieCockpitRenderer* cockpit) {
	if (g)
		g->cockpit = cockpit;
}

/* ===== Directional-shadow inspector accessors ===================== */

void TieFlightRenderer_ShadowsGet(const TieFlightRenderer* g, TieFlightShadowSettings* out) {
	if (g && out)
		*out = g->shadows;
}

static float TieFlightRenderer_ClampShadowValue(float value, float minimum, float maximum) {
	if (value < minimum)
		return minimum;
	if (value > maximum)
		return maximum;
	return value;
}

static void TieFlightRenderer_SanitizeShadowSettings(TieFlightShadowSettings* settings) {
	const float gap = 0.001f;
	if (settings->atlas_size != 1024 && settings->atlas_size != 2048 && settings->atlas_size != 4096 &&
		settings->atlas_size != 8192)
		settings->atlas_size = 4096;
	if (settings->cascade_count < 1)
		settings->cascade_count = 1;
	if (settings->cascade_count > 4)
		settings->cascade_count = 4;
	if (settings->fit_mode > AERON_SCENE_SHADOW_FIT_SCENE_DEPENDENT)
		settings->fit_mode = AERON_SCENE_SHADOW_FIT_SCENE_DEPENDENT;
	settings->max_distance = TieFlightRenderer_ClampShadowValue(settings->max_distance, 1.001f, 1048576.0f);
	settings->split_lambda = TieFlightRenderer_ClampShadowValue(settings->split_lambda, 0.0f, 1.0f);
	settings->split_positions[0] =
		TieFlightRenderer_ClampShadowValue(settings->split_positions[0], gap, 1.0f - 3.0f * gap);
	settings->split_positions[1] = TieFlightRenderer_ClampShadowValue(
		settings->split_positions[1], settings->split_positions[0] + gap, 1.0f - 2.0f * gap);
	settings->split_positions[2] = TieFlightRenderer_ClampShadowValue(
		settings->split_positions[2], settings->split_positions[1] + gap, 1.0f - gap);
	if (settings->filter_quality > 3)
		settings->filter_quality = 3;
	settings->filter_radius = TieFlightRenderer_ClampShadowValue(settings->filter_radius, 0.5f, 3.0f);
	settings->light_angular_radius_degrees =
		TieFlightRenderer_ClampShadowValue(settings->light_angular_radius_degrees, 0.0f, 5.0f);
	settings->max_filter_radius =
		TieFlightRenderer_ClampShadowValue(settings->max_filter_radius, settings->filter_radius, 16.0f);
	settings->pcss_min_filter_radius =
		TieFlightRenderer_ClampShadowValue(settings->pcss_min_filter_radius, 0.5f, settings->filter_radius);
	settings->normal_bias_texels =
		TieFlightRenderer_ClampShadowValue(settings->normal_bias_texels, 0.0f, 4.0f);
	settings->depth_bias_texels = TieFlightRenderer_ClampShadowValue(settings->depth_bias_texels, 0.0f, 4.0f);
	settings->transition_fraction =
		TieFlightRenderer_ClampShadowValue(settings->transition_fraction, 0.0f, 0.5f);
	settings->distance_fade_fraction =
		TieFlightRenderer_ClampShadowValue(settings->distance_fade_fraction, 0.0f, 0.5f);
}

void TieFlightRenderer_ShadowsSet(TieFlightRenderer* g, const TieFlightShadowSettings* settings) {
	if (!g || !settings)
		return;
	g->shadows = *settings;
	TieFlightRenderer_SanitizeShadowSettings(&g->shadows);
}

bool TieFlightRenderer_ShadowsDebugCascades(const TieFlightRenderer* g) {
	return g && g->shadows.debug_cascades;
}

void TieFlightRenderer_ShadowsSetDebugCascades(TieFlightRenderer* g, bool enabled) {
	if (g)
		g->shadows.debug_cascades = enabled;
}

void TieFlightRenderer_ShadowsGetStats(const TieFlightRenderer* g, AeronSceneDirectionalShadowStats* out) {
	if (!out)
		return;
	memset(out, 0, sizeof *out);
	if (g && g->scene)
		AeronScene_GetDirectionalShadowStats(g->scene, out);
}

/* ===== SSAO inspector accessors ==================================== */

void TieFlightRenderer_SsaoGet(const TieFlightRenderer* g, AeronSceneSsaoSettings* out) {
	if (g && out)
		*out = g->ssao;
}

void TieFlightRenderer_SsaoSet(TieFlightRenderer* g, const AeronSceneSsaoSettings* settings) {
	if (!g || !settings)
		return;
	g->ssao = *settings;
	TieFlightRenderer_SsaoSetQuality(g, (SsaoQuality)settings->ssao_quality);
	TieFlightRenderer_SsaoSetIntensity(g, settings->ssao_intensity);
	TieFlightRenderer_SsaoSetRadiusView(g, settings->ssao_radius_view);
	TieFlightRenderer_SsaoSetBiasView(g, settings->ssao_bias_view);
	TieFlightRenderer_SsaoSetPower(g, settings->ssao_power);
	TieFlightRenderer_SsaoSetDirect(g, settings->ssao_direct);
	g->ssao.ssao_min_screen_frac =
		settings->ssao_min_screen_frac < 0.0f ? 0.0f : settings->ssao_min_screen_frac;
	g->ssao.ssao_max_screen_frac = settings->ssao_max_screen_frac < g->ssao.ssao_min_screen_frac
									   ? g->ssao.ssao_min_screen_frac
									   : settings->ssao_max_screen_frac;
	g->ssao.ssao_sample_jitter = TieFlightRenderer_ClampShadowValue(settings->ssao_sample_jitter, 0.0f, 1.0f);
}

SsaoQuality TieFlightRenderer_SsaoGetQuality(const TieFlightRenderer* g) {
	return g ? (SsaoQuality)g->ssao.ssao_quality : SSAO_OFF;
}

void TieFlightRenderer_SsaoSetQuality(TieFlightRenderer* g, SsaoQuality q) {
	if (!g)
		return;
	if (q < SSAO_OFF)
		q = SSAO_OFF;
	if (q > SSAO_HIGH)
		q = SSAO_HIGH;
	g->ssao.ssao_quality = q;
}

float TieFlightRenderer_SsaoGetIntensity(const TieFlightRenderer* g) {
	return g ? g->ssao.ssao_intensity : 0.0f;
}

void TieFlightRenderer_SsaoSetIntensity(TieFlightRenderer* g, float v) {
	if (!g)
		return;
	if (v < 0.0f)
		v = 0.0f;
	if (v > 1.0f)
		v = 1.0f;
	g->ssao.ssao_intensity = v;
}

float TieFlightRenderer_SsaoGetRadiusView(const TieFlightRenderer* g) {
	return g ? g->ssao.ssao_radius_view : 0.0f;
}

void TieFlightRenderer_SsaoSetRadiusView(TieFlightRenderer* g, float v) {
	if (!g)
		return;
	if (v < 0.0f)
		v = 0.0f;
	g->ssao.ssao_radius_view = v;
}

float TieFlightRenderer_SsaoGetBiasView(const TieFlightRenderer* g) {
	return g ? g->ssao.ssao_bias_view : 0.0f;
}

void TieFlightRenderer_SsaoSetBiasView(TieFlightRenderer* g, float v) {
	if (!g)
		return;
	if (v < 0.0f)
		v = 0.0f;
	g->ssao.ssao_bias_view = v;
}

float TieFlightRenderer_SsaoGetPower(const TieFlightRenderer* g) { return g ? g->ssao.ssao_power : 1.0f; }

void TieFlightRenderer_SsaoSetPower(TieFlightRenderer* g, float v) {
	if (!g)
		return;
	if (v < 0.1f)
		v = 0.1f;
	if (v > 8.0f)
		v = 8.0f;
	g->ssao.ssao_power = v;
}

float TieFlightRenderer_SsaoGetDirect(const TieFlightRenderer* g) { return g ? g->ssao.ssao_direct : 0.0f; }

void TieFlightRenderer_SsaoSetDirect(TieFlightRenderer* g, float v) {
	if (!g)
		return;
	if (v < 0.0f)
		v = 0.0f;
	if (v > 1.0f)
		v = 1.0f;
	g->ssao.ssao_direct = v;
}

bool TieFlightRenderer_SsaoGetDebugViz(const TieFlightRenderer* g) { return g && g->ssao.ssao_debug_viz; }

void TieFlightRenderer_SsaoSetDebugViz(TieFlightRenderer* g, bool on) {
	if (g)
		g->ssao.ssao_debug_viz = on;
}

/* ===== Motion-blur accessors ======================================= */

MbQuality TieFlightRenderer_MbGetQuality(const TieFlightRenderer* g) { return g ? g->mb_quality : MB_OFF; }

void TieFlightRenderer_MbSetQuality(TieFlightRenderer* g, MbQuality q) {
	if (!g)
		return;
	if (q < MB_OFF)
		q = MB_OFF;
	if (q > MB_HIGH)
		q = MB_HIGH;
	if (q == g->mb_quality)
		return;
	g->mb_quality = q;
	/* The velocity / tile / resolve RTs live in aeron_scene and are
	 * (lazily) allocated on first use; the scene RT pointer resets on
	 * the next prep. Nothing to (de)allocate here. */
	if (q == MB_OFF && g->scene)
		g->scene_rt = AeronScene_ColorRt(g->scene);
}

float TieFlightRenderer_MbGetShutter(const TieFlightRenderer* g) { return g ? g->mb_shutter : 0.0f; }

void TieFlightRenderer_MbSetShutter(TieFlightRenderer* g, float s) {
	if (!g)
		return;
	if (s < 0.0f)
		s = 0.0f;
	/* Ceiling above 1 is non-physical but useful for exaggerating the
	 * effect when verifying it; the reconstruct's max_radius still
	 * bounds the actual blur length. */
	if (s > 8.0f)
		s = 8.0f;
	g->mb_shutter = s;
}

bool TieFlightRenderer_MbGetVelocityViz(const TieFlightRenderer* g) { return g ? g->mb_velocity_viz : false; }

void TieFlightRenderer_MbSetVelocityViz(TieFlightRenderer* g, bool on) {
	if (g)
		g->mb_velocity_viz = on;
}

bool TieFlightRenderer_MbGetPauseKeepBlur(const TieFlightRenderer* g) {
	return g ? g->mb_pause_keep_blur : false;
}

void TieFlightRenderer_MbSetPauseKeepBlur(TieFlightRenderer* g, bool on) {
	if (g)
		g->mb_pause_keep_blur = on;
}

bool TieFlightRenderer_MbGetCameraBlur(const TieFlightRenderer* g) { return g ? g->mb_camera_blur : false; }

void TieFlightRenderer_MbSetCameraBlur(TieFlightRenderer* g, bool on) {
	if (g)
		g->mb_camera_blur = on;
}

/* Tonemap / bloom-kernel / EOTF / ACES knobs moved to aeron_scene
 * (aeron/scene/present.h). */

AeronRenderTarget* TieFlightRenderer_SceneRt(const TieFlightRenderer* g) {
	/* Returns the HDR scene RT — the canonical "diegetic frame" target.
	 * Scene + cockpit + HUD + PIP all draw into it in linear HDR; bloom
	 * samples from it; the final present pass tonemaps it onto the
	 * swapchain. Exposed so the cockpit module can probe the canonical
	 * size when caching cockpit-resolution-dependent resources. */
	return g ? AeronScene_ColorRt(g->scene) : NULL;
}

AeronTexture* TieFlightRenderer_PipTexture(const TieFlightRenderer* g) { return g ? g->pip_texture : NULL; }

/* Return the first flight-RT row occupied by the global message bar. */
static int TieFlightRenderer_BloomMessageBarTopRt(const TieFlightRenderer* g, const TieSnapshot* snap) {
	TieFlightHudMessageBarGeometry geometry;
	if (!TieFlightHud_MessageBarGeometry(snap, g->rt_w, g->rt_h, &geometry))
		return g->rt_h;
	int bar_top_rt = (int)geometry.separator_y;
	if (bar_top_rt < 0)
		bar_top_rt = 0;
	if (bar_top_rt > g->rt_h)
		bar_top_rt = g->rt_h;
	return bar_top_rt;
}

bool TieFlightRenderer_ApplyBloom(TieFlightRenderer* g, AeronCommandBuffer* cmd, const TieSnapshot* snap) {
	if (!g || !cmd || !snap)
		return false;
	if (!g->bloom || snap->scene_kind != TIE_SCENE_FLIGHT)
		return true;

	int max_y = TieFlightRenderer_BloomMessageBarTopRt(g, snap);
	/* Cache the bar's UV.y for the swapchain composite — it gates the
	 * bloom contribution destination-side (the chain's bright-pass
	 * scissor handles the source side). When the snapshot has no bar
	 * this view, max_y == rt_h → bar_y_uv == 1.0 (identity gate). */
	g->last_bloom_bar_y_uv = (float)max_y / (float)g->rt_h;

	/* Bloom samples the scene RT — the scene's mb_rt when the motion-
	 * blur resolve ran this frame, its color RT otherwise. */
	AeronRenderTarget* scene = g->scene_rt ? g->scene_rt : AeronScene_ColorRt(g->scene);
	if (!scene)
		return false;
	return AeronSceneBloom_Apply(g->bloom, cmd, Aeron_RenderTargetGetTexture(scene), g->rt_w, g->rt_h,
								 max_y) != 0;
}

AeronRenderTarget* TieFlightRenderer_BloomRt(const TieFlightRenderer* g) {
	if (!g || !g->bloom)
		return NULL;
	return AeronSceneBloom_ColorRt(g->bloom);
}

void TieFlightRenderer_BloomParams(const TieFlightRenderer* g, TieFlightRendererBloomParams* out) {
	if (!out)
		return;
	if (!g || g->rt_w <= 0 || g->rt_h <= 0) {
		out->intensity = 0.0f;
		out->texel_x = 0.0f;
		out->texel_y = 0.0f;
		out->bar_y_uv = 1.0f;
		return;
	}
	out->intensity = AeronSceneBloom_Intensity();
	/* Composite kernel reach: ±1 bloom-mip0 texel = ±2 flight-RT texels
	 * (bloom mip0 is half-res). ±1 flight-texel offsets all four taps
	 * within a single mip0 texel, which adds no spread — at ±2 the taps
	 * land on neighbouring mip0 texels and the kernel actually softens
	 * the halo edge. */
	out->texel_x = 2.0f / (float)g->rt_w;
	out->texel_y = 2.0f / (float)g->rt_h;
	out->bar_y_uv = g->last_bloom_bar_y_uv;
}

void TieFlightRenderer_GetRtDims(const TieFlightRenderer* g, int* out_w, int* out_h) {
	if (out_w)
		*out_w = g ? g->rt_w : 0;
	if (out_h)
		*out_h = g ? g->rt_h : 0;
}

void TieFlightRenderer_Present(TieFlightRenderer* g, AeronCommandBuffer* cmd, AeronRenderPass* pass,
							   const float present_tint[4]) {
	if (!g || !cmd || !pass || !g->present_chain)
		return;

	TIE_GPU_MARKER(cmd, "Flight present (tonemap+bloom)");

	/* Scene RT = mb_rt when the motion-blur resolve ran this frame (it
	 * holds the blurred scene + cockpit), else color_rt. Bloom runs only
	 * in the full-fidelity HD style; classic and XvT skip the pass, so
	 * mip0 holds a stale frame — force zero contribution there. */
	AeronRenderTarget* scene_target = g->scene_rt ? g->scene_rt : AeronScene_ColorRt(g->scene);
	if (!scene_target)
		return;
	AeronRenderTarget* bloom_target = TieFlightRenderer_BloomRt(g);
	const bool bloom_active = bloom_target && g->scene_model_backend;
	AeronScenePresentChain_Draw(
		g->present_chain, pass, Aeron_RenderTargetGetTexture(scene_target), g->sampler_linear,
		bloom_active ? Aeron_RenderTargetGetTexture(bloom_target) : NULL, AeronSceneBloom_Intensity(),
		g->rt_w, g->rt_h, g->last_bloom_bar_y_uv, present_tint,
		/*src_coverage=*/0);
}

void TieFlightRenderer_PresentSwapchain(TieFlightRenderer* g, AeronCommandBuffer* cmd,
										AeronRenderPass* pass) {
	if (!g || !cmd || !pass || !g->swapchain_present_chain)
		return;
	AeronRenderTarget* scene_target = g->scene_rt ? g->scene_rt : AeronScene_ColorRt(g->scene);
	if (!scene_target)
		return;
	AeronRenderTarget* bloom_target = TieFlightRenderer_BloomRt(g);
	const bool bloom_active = bloom_target && g->scene_model_backend;
	static const float tint[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
	AeronScenePresentChain_Draw(
		g->swapchain_present_chain, pass, Aeron_RenderTargetGetTexture(scene_target), g->sampler_linear,
		bloom_active ? Aeron_RenderTargetGetTexture(bloom_target) : NULL, AeronSceneBloom_Intensity(),
		g->rt_w, g->rt_h, g->last_bloom_bar_y_uv, tint, 0);
}
