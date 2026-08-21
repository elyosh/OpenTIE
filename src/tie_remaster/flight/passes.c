/* Per-frame flight rendering and post-processing orchestration. */

#include "tie_remaster/flight/backdrops.h"
#include "tie_remaster/flight/billboards.h"
#include "tie_remaster/flight/classic_draw.h"
#include "tie_remaster/flight/cockpit/renderer.h"
#include "tie_remaster/flight/hyperstars.h"
#include "tie_remaster/flight/line_draw.h"
#include "tie_remaster/flight/mesh_common.h"
#include "tie_remaster/flight/pbr.h"
#include "tie_remaster/flight/point_lights.h"
#include "tie_remaster/flight/render_math.h"
#include "tie_remaster/flight/renderer_internal.h"
#include "tie_remaster/flight/sprite_cache.h"
#include "tie_remaster/flight/stars.h"
#include "tie_remaster/gpu_debug.h"
#include "tie_remaster/scene2d/srgb_math.h"

#include "aeron/aeron.h"
#include "aeron/log.h"
#include "aeron/scene/world.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "aeron/scene/image_cache.h"
#include "tie_runtime/audio/config.h"
#include "tie_runtime/diagnostics/diagnostics.h"
#include "tie_runtime/display/classic_display.h"
#include "tie_runtime/display/classic_framebuffer.h"
#include "tie_runtime/flight_assets/model_types.h"
#include "tie_runtime/input/input.h"
#include "tie_runtime/runtime/exports.h"
#include "tie_runtime/runtime/profile.h"
#include "tie_runtime/snapshot/snapshot.h"
#include "tie_runtime/storage/storage.h"

#define TIE_FLIGHT_DEEP_SPACE_PALETTE_INDEX 0xFBu
#define TIE_FLIGHT_MB_REFERENCE_FRAME_US 32000u

/* Round a float viewport rect to the HAL's integer viewport. */
static AeronRectI TieFlightPasses_ViewportRect(float x, float y, float w, float h) {
	return (AeronRectI) {
		.x = (int)lrintf(x),
		.y = (int)lrintf(y),
		.width = (int)lrintf(w),
		.height = (int)lrintf(h),
	};
}

static void TieFlightRenderer_ConfigureTemporal(TieFlightRenderer* g, int32_t delta_us,
												AeronTemporalMode mode, bool history_valid) {
	if (mode == AERON_TEMPORAL_OFF) {
		AeronScene_SetTemporal(g->scene, &(AeronSceneTemporalDesc) { .mode = AERON_TEMPORAL_OFF });
		g->fsr_reset_pending = true;
		return;
	}

	float frame_delta_ms = 16.6667f;
	if (delta_us > 0 && delta_us <= 250000) {
		frame_delta_ms = (float)delta_us / 1000.0f;
	} else {
		g->fsr_reset_pending = true;
	}
	if (!history_valid)
		g->fsr_reset_pending = true;
	AeronScene_SetTemporal(g->scene, &(AeronSceneTemporalDesc) {
										 .mode = mode,
										 .frame_time_delta_ms = frame_delta_ms,
										 .sharpness = g->fsr_sharpness,
										 .reset_history = g->fsr_reset_pending,
									 });
	g->fsr_reset_pending = false;
}

static void TieFlightRenderer_MotionBlurResetTiming(TieFlightRenderer* g, uint64_t host_time_us) {
	g->mb_pose_host_us = host_time_us;
	g->mb_velocity_span_us = 0;
	g->mb_pose_host_valid = true;
}

static void TieFlightRenderer_MotionBlurRecordPose(TieFlightRenderer* g, uint64_t host_time_us) {
	g->mb_velocity_span_us =
		g->mb_pose_host_valid && host_time_us > g->mb_pose_host_us ? host_time_us - g->mb_pose_host_us : 0;
	g->mb_pose_host_us = host_time_us;
	g->mb_pose_host_valid = true;
}

static float TieFlightRenderer_MotionBlurLogicalFrameShutter(const TieFlightRenderer* g) {
	if (g->mb_velocity_span_us == 0)
		return 0.0f;
	return (float)((double)g->mb_shutter * (double)TIE_FLIGHT_MB_REFERENCE_FRAME_US /
				   (double)g->mb_velocity_span_us);
}

/* The GLB package has exactly one required catalog skybox. */
/* Scene submissions queue the PbrLightFS block before AeronScene_Render. */

static AeronTexture* TieFlightRenderer_LoadSkyboxCube(TieFlightRenderer* g, AeronCommandBuffer* cmd,
													  const char* full_path) {
	TIE_GPU_PUSH(cmd, "Flight skybox upload");
	AeronTexture* tex = Aeron_ImageLoadCubemapKtx2Vfs(cmd, g->assets->vfs, AERON_VFS_ROOT_ASSET, full_path,
													  64u * 1024u * 1024u);
	TIE_GPU_POP(cmd);
	if (!tex) {
		char error[384];
		snprintf(error, sizeof error, "cannot load required ASSET/%s", full_path);
		Aeron_RequestFatalError("Flight Asset Error", error);
	}
	return tex;
}

/* ===== Per-frame driver ============================================== */

static bool TieFlightRenderer_RefreshPaletteLut(TieFlightRenderer* g, AeronCommandBuffer* cmd,
												const uint32_t snap_palette[256]) {
	/* HD mode never samples the palette texture — only the classic
	 * mesh / line FS reads it. The engine cycles palette slots
	 * 0xF8/0xF9/0xFA at ~7-14 Hz (gamesnd_drive_palette_cycle), which
	 * otherwise produces a per-cycle GPU upload here for no observable
	 * effect. Skip while HD is active; the cached snapshot goes stale
	 * but the memcmp below catches that on the next toggle to classic
	 * and brings the texture back in sync. */
	if (g->scene_model_backend)
		return true;
	/* Skip the upload if the palette didn't change. The 1KB compare
	 * is cheap relative to a copy pass. */
	if (memcmp(g->palette_cached, snap_palette, sizeof g->palette_cached) == 0)
		return true;
	memcpy(g->palette_cached, snap_palette, sizeof g->palette_cached);
	if (!TieFlightRenderer_UploadToTexture(cmd, g->palette_tex, 256, 1, 4, snap_palette)) {
		Aeron_RequestFatalRendererError("flight palette upload");
		return false;
	}
	return true;
}

static bool TieFlightRenderer_RefreshMaterialcolors(TieFlightRenderer* g, AeronCommandBuffer* cmd) {
	/* Same rationale as TieFlightRenderer_RefreshPaletteLut — HD mode doesn't sample
	 * the materialcolors LUT; only the classic mesh FS does. The cache
	 * may go stale during an HD session but the memcmp on toggle-to-
	 * classic re-syncs cleanly. */
	if (g->scene_model_backend)
		return true;
	const uint8_t* mc = TieRecoveredData_MaterialColors();
	if (!mc)
		return true;
	/* Skip the upload when the table hasn't changed. Engine row-13
	 * mutation via drawpol_setmarkingcolors is rare (per-mission
	 * setup, not per-tick), so a 720-byte memcmp here lets us avoid
	 * a copy-pass + 720-byte transfer-buffer alloc every frame. */
	if (g->materialcolors_have_cache &&
		memcmp(g->materialcolors_cached, mc, sizeof g->materialcolors_cached) == 0) {
		return true;
	}
	memcpy(g->materialcolors_cached, mc, sizeof g->materialcolors_cached);
	g->materialcolors_have_cache = true;
	if (!TieFlightRenderer_UploadToTexture(cmd, g->materialcolors_tex, 16, 45, 1, mc)) {
		Aeron_RequestFatalRendererError("flight material-color upload");
		return false;
	}
	return true;
}

/* Load the configured GLB-mode skybox once for each battle transition. */
static void TieFlightRenderer_RefreshSkybox(TieFlightRenderer* g, AeronCommandBuffer* cmd,
											uint8_t battle_id) {
	/* Swap the live world-ambient cube alongside the skybox so PBR
	 * shading and sky pixels stay in sync. The call is cheap when
	 * the battle hasn't changed (own same-battle bail). */
	TieFlightPbr_RefreshForBattle(battle_id);

	if (g->skybox_loaded_battle == (int)battle_id) {
		/* Already loaded for this battle. */
		return;
	}
	/* Battle changed — release any previous cube. */
	if (g->skybox_cube) {
		Aeron_DestroyTexture(g->skybox_cube);
		g->skybox_cube = NULL;
	}
	g->skybox_loaded_battle = (int)battle_id;

	const TieFlightAssetBundle* catalog =
		TieFlightAssetSource_IsRemastered(g->assets) ? g->assets->catalog : NULL;
	if (!catalog)
		return;
	const char* root = TieFlightAssets_ContentPrefix(catalog);
	const char* rel = TieFlightAssets_Skybox(catalog);
	if (!rel)
		return;

	char full[1024];
	snprintf(full, sizeof full, "%s/%s", root, rel);
	g->skybox_cube = TieFlightRenderer_LoadSkyboxCube(g, cmd, full);
	if (g->skybox_cube)
		Aeron_LogInfo("tie.flight", "skybox loaded: %s (battle=%u)", full, (unsigned)battle_id);
}

/* ===== Per-craft state batching helpers ============================
 * The main flight pass iterates a sorted, frustum-culled list of
 * draw-eligible craft so that:
 *  - Visible craft of the same species draw consecutively → species
 *    VBO/IBO/decal-SSBO bindings change only between species, not
 *    between craft (with the same-species rebind-skip in the loop).
 *  - Off-screen craft are skipped before any per-craft uniform push.
 * Wins are biggest on bandwidth-constrained targets (Switch, PS4)
 * where descriptor-set updates and command-buffer recording dominate.
 * --------------------------------------------------------------- */

/* Shared frustum helpers for classic and scene-model draws. */
void TieFlightRenderer_BuildFrustumPlanes(TieFlightFrustumPlanes* fp, const float view_proj[16]) {
	const float* r0 = &view_proj[0];
	const float* r1 = &view_proj[4];
	const float* r3 = &view_proj[12];
	/* Plane = row3 ± row_k. Inside half-space: dot(plane, p4) ≥ 0. */
	float raw[4][4];
	for (int j = 0; j < 4; ++j)
		raw[0][j] = r3[j] + r0[j]; /* Left */
	for (int j = 0; j < 4; ++j)
		raw[1][j] = r3[j] - r0[j]; /* Right */
	for (int j = 0; j < 4; ++j)
		raw[2][j] = r3[j] + r1[j]; /* Bottom */
	for (int j = 0; j < 4; ++j)
		raw[3][j] = r3[j] - r1[j]; /* Top */
	for (int k = 0; k < 4; ++k) {
		const float nx = raw[k][0], ny = raw[k][1], nz = raw[k][2];
		const float len = sqrtf(nx * nx + ny * ny + nz * nz);
		const float inv = (len > 1e-9f) ? (1.0f / len) : 0.0f;
		fp->p[k][0] = nx * inv;
		fp->p[k][1] = ny * inv;
		fp->p[k][2] = nz * inv;
		fp->p[k][3] = raw[k][3] * inv;
	}
}

/* True when the sphere lies entirely outside at least one side plane.
 * Conservative: false-negatives (cull misses) impossible; false-
 * positives (rendering an off-screen craft) impossible only for the
 * 4 sides — corners can still slip through. Good enough for our use
 * case where ships are roughly sphere-shaped. */
bool TieFlightRenderer_SphereOutsideFrustum(const TieFlightFrustumPlanes* fp, const float center[3],
											float radius) {
	for (int k = 0; k < 4; ++k) {
		const float d =
			fp->p[k][0] * center[0] + fp->p[k][1] * center[1] + fp->p[k][2] * center[2] + fp->p[k][3];
		if (d < -radius)
			return true;
	}
	return false;
}

/* Sort key: ship_idx in high bits, draw-index in low bits. Sorting
 * uint32 ascending groups craft by species. */
int TieFlightRenderer_CmpDrawKey(const void* a, const void* b) {
	const uint32_t ka = *(const uint32_t*)a;
	const uint32_t kb = *(const uint32_t*)b;
	return (ka > kb) - (ka < kb);
}

/* ===== Skybox cube selection =====================================
 *
 * Picks the cube map for the current hyperspace phase / backdrop
 * state; the draw itself is the first operation in Aeron's color pass.
 * Returns NULL for the sky-less phases. */
static AeronTexture* TieFlightRenderer_SelectSkyboxCube(TieFlightRenderer* g, const TieSnapshot* curr) {
	/* Per-phase cubemap selection:
	 *   phase 3/5  cockpit-view streak phases — no skybox (engine
	 *              gates rtsvga2_drawstars off here).
	 *   phase 4    external mid-jump — configured GLB skybox.
	 *   phase 6    external arrival — per-battle cube unconditionally.
	 *   any other  per-battle cube, gated on backdrops.draw_enabled. */
	switch (curr->hyperspace.phase) {
		case 3:
		case 5:
			return NULL;
		case 4:
			return g->skybox_cube;
		case 6:
			return g->skybox_cube;
		default:
			return curr->backdrops.draw_enabled ? g->skybox_cube : NULL;
	}
}

/* TIE-world → cube-sampling basis (90° about +X, proper rotation,
 * det = +1):
 *     TIE +X (right)   → cube +X (right)
 *     TIE +Y (forward) → cube -Z (forward, per cube convention)
 *     TIE +Z (up)      → cube +Y (up)
 * so cubemaps can be authored in the standard tool convention
 * (+X right, +Y up, +Z back; each face viewed from outside the
 * cube) and render un-mirrored to the player inside the cube. */
static const float k_tie_world_to_cube[9] = {
	1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, -1.0f, 0.0f,
};

/* Scene hooks read `g->frame`, which is valid only during AeronScene_Render. */

/* BEFORE_OPAQUE — early draws of the color pass. In classic mode,
 * where no instances are submitted, this records the classic
 * palette mesh + line passes. The sky cube draws scene-side just
 * before this hook (AeronScene_SetSkyCube, pushed by prep). */
static void TieFlightRenderer_SceneHookBeforeOpaque(AeronCommandBuffer* cmd, AeronRenderPass* pass, int rt_w,
													int rt_h, void* user) {
	TieFlightRenderer* g = (TieFlightRenderer*)user;
	const TieFlightSceneFrame* fr = &g->frame;
	if (!fr->curr || !pass)
		return;

	/* Backdrop planets follow as batched SKY scene billboards, drawn
	 * by the scene right after this hook returns. */

	(void)rt_w;
	(void)rt_h;

	if (fr->stars_ready) {
		const float* view_proj = AeronScene_JitteredViewProj(g->scene);
		TieFlightStars_DrawInPass(g->stars, cmd, pass, fr->curr, &fr->fcam, view_proj);
	}

	if (!g->scene_model_backend) {
		TieFlightRenderer_ClassicDrawPass(g, cmd, pass, fr->curr, fr->fcam.view_proj);
		TieFlightRenderer_LinesDrawPass(g, cmd, pass, fr->curr, fr->fcam.view_proj);
	}
}

/* AFTER_OPAQUE — hyperspace streaks over the meshes (billboards moved
 * to the scene's batched OVERLAY stage, drawn just before this hook;
 * their velocity stamping moved into the scene's prepass), before the
 * velocity viz / motion-blur resolve / cockpit tail. */
static void TieFlightRenderer_SceneHookAfterOpaque(AeronCommandBuffer* cmd, AeronRenderPass* pass, int rt_w,
												   int rt_h, void* user) {
	(void)rt_w;
	(void)rt_h;
	TieFlightRenderer* g = (TieFlightRenderer*)user;
	const TieFlightSceneFrame* fr = &g->frame;
	if (!fr->curr || !pass)
		return;

	if (fr->hyperstars_ready) {
		TIE_GPU_MARKER(cmd, "Hyperstars");
		TieFlightHyperstars_DrawInPass(g->hyperstars, cmd, pass, &fr->vp, &fr->fcam);
	}
}

/* AFTER_UPSCALE — cockpit / HUD / PIP overlay, drawn last into the
 * output-resolution HDR target so it stays sharp over the reconstructed
 * scene. Cockpit pipelines target rt_format (HDR), so authored LDR
 * pixels sit in linear HDR alongside the 3D scene: bloom sees the
 * combined frame and bright HUD elements bloom diegetically, and the
 * present-time tonemap applies to cockpit pixels along with the rest
 * of the frame. */
static void TieFlightRenderer_SceneHookAfterUpscale(AeronCommandBuffer* cmd, AeronRenderPass* pass, int rt_w,
													int rt_h, void* user) {
	(void)rt_w;
	(void)rt_h;
	TieFlightRenderer* g = (TieFlightRenderer*)user;
	const TieFlightSceneFrame* fr = &g->frame;
	if (!fr->curr || !pass)
		return;
	if (g->cockpit) {
		TIE_GPU_MARKER(cmd, "Cockpit and flight HUD");
		TieCockpitRenderer_RenderInPass(g->cockpit, cmd, pass, AeronScene_SceneRt(g->scene), fr->curr);
	}
}

static bool TieFlightRenderer_PrepareClassicMeshTables(TieFlightRenderer* g, AeronCommandBuffer* cmd,
													   const TieSnapshot* snapshot,
													   const TieFlightObjectState* pip_flight) {
	if (snapshot->flight_count > TIE_MAX_FLIGHT_OBJECTS)
		return false;
	for (uint32_t i = 0; i < snapshot->flight_count; ++i) {
		const TieFlightObjectState* flight = &snapshot->flights[i];
		uint16_t species = flight->genus == TIE_GENUS_DEBRIS ? flight->parent_ship_idx : flight->ship_idx;
		const TieFlightSpeciesMesh* mesh =
			species < TIE_FLIGHT_MAX_SPECIES && g->meshes[species].ready ? &g->meshes[species] : NULL;
		TieFlightMesh_BuildclassicMeshTable(mesh, flight, snapshot, TIE_FLIGHT_MESH_TABLE_MAIN,
											&g->classic_mesh_tables.tables[i]);
	}
	g->classic_pip_table_index = snapshot->flight_count;
	TieFlightObjectState empty = { 0 };
	const TieFlightObjectState* flight = pip_flight ? pip_flight : &empty;
	const TieFlightSpeciesMesh* mesh = NULL;
	if (pip_flight && pip_flight->ship_idx < TIE_FLIGHT_MAX_SPECIES && g->meshes[pip_flight->ship_idx].ready)
		mesh = &g->meshes[pip_flight->ship_idx];
	TieFlightMesh_BuildclassicMeshTable(mesh, flight, snapshot, TIE_FLIGHT_MESH_TABLE_PIP,
										&g->classic_mesh_tables.tables[g->classic_pip_table_index]);
	g->classic_mesh_tables.count = snapshot->flight_count + 1;
	return Aeron_UploadBufferDataCmd(cmd, g->classic_mesh_tables.buffer, 0, g->classic_mesh_tables.tables,
									 g->classic_mesh_tables.count * sizeof(AeronSceneMeshTable)) != 0;
}

static bool TieFlightRenderer_PreparePip(TieFlightRenderer* g, AeronCommandBuffer* cmd,
										 const TieSnapshot* snap) {
	if (!g || !cmd || !snap || snap->scene_kind != TIE_SCENE_FLIGHT)
		return false;
	g->pip_texture = NULL;
	const TieFlightObjectState* fl = NULL;
	TieFlightObjectState synth_static = { 0 };
	const uint16_t target_slot = snap->cockpit.pip_target_slot;
	if (snap->cockpit.pip_target_present && target_slot >= 0x3800u) {
		const uint16_t static_idx = (uint16_t)(target_slot - 0x3800u);
		for (uint16_t i = 0; i < snap->static_count; ++i) {
			const TieStaticObjectState* so = &snap->statics[i];
			if (so->slot != static_idx)
				continue;
			synth_static.genus = TIE_GENUS_UTILITY;
			synth_static.ship_idx = so->species;
			synth_static.slot = so->slot;
			memcpy(synth_static.world_pos, so->world_pos, sizeof synth_static.world_pos);
			memcpy(synth_static.ori, so->ori, sizeof synth_static.ori);
			fl = &synth_static;
			break;
		}
	} else if (snap->cockpit.pip_target_present) {
		for (uint16_t i = 0; i < snap->flight_count; ++i) {
			if (snap->flights[i].slot == target_slot) {
				fl = &snap->flights[i];
				break;
			}
		}
	}
	if (!TieFlightRenderer_PrepareClassicMeshTables(g, cmd, snap, fl))
		return false;

	float h_half = snap->cockpit.pip_fov_h_half_rad;
	float v_half = snap->cockpit.pip_fov_v_half_rad;
	if (h_half <= 0.0f || v_half <= 0.0f) {
		h_half = snap->camera.fov_h_half_rad > 0.0f ? snap->camera.fov_h_half_rad : 0.5586f;
		v_half = snap->camera.fov_v_half_rad > 0.0f ? snap->camera.fov_v_half_rad : 0.3f;
	}

	float cam_pos[3] = { 0.0f, 0.0f, 0.0f };
	float view[16];
	float view_proj[16];
	float proj[16];
	TieRenderMath_Mat4PerspectiveReverseZ(proj, h_half, v_half, 1.0f);
	if (fl) {
		TieRenderMath_BuildPipCamera(cam_pos, view, snap->cockpit.pip_back_step, snap->cockpit.pip_cam_ori);
		TieRenderMath_Mat4Multiply(view_proj, proj, view);
	}

	float clear[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
	TieScene2dSrgb_PalToLinearRgb(snap->palette[48], &clear[0], &clear[1], &clear[2]);

	const bool hd_ready = g->scene_model_backend && fl && fl->ship_idx < TIE_FLIGHT_MAX_SPECIES &&
						  g->scene_ships[fl->ship_idx].ready;
	const bool clear_hd = g->scene_model_backend && !snap->cockpit.pip_target_present;
	if (hd_ready || clear_hd) {
		AeronSceneCamera camera = {
				.pos = {cam_pos[0], cam_pos[1], cam_pos[2]},
				.ori = {
						fl ? snap->cockpit.pip_cam_ori[0] : snap->camera.ori[0],
						fl ? snap->cockpit.pip_cam_ori[1] : snap->camera.ori[1],
						fl ? snap->cockpit.pip_cam_ori[2] : snap->camera.ori[2],
						fl ? snap->cockpit.pip_cam_ori[3] : snap->camera.ori[3],
				},
				.h_half_rad = h_half,
				.v_half_rad = v_half,
				.near_z = 1.0f,
				.viewport = {0, 0, TIE_FLIGHT_PIP_W, TIE_FLIGHT_PIP_H},
		};
		if (!AeronScene_Begin(g->pip_scene, &camera))
			return false;
		AeronScene_SetClearColor(g->pip_scene, clear);
		if (!AeronScene_SetMeshSampler(g->pip_scene, g->opt_sampler))
			return false;
		TieFlightPointLightFrame point_frame = { 0 };
		if (fl)
			TieFlightPointLights_Derive(&point_frame, snap, fl->world_pos);
		TieFlightPointLights_Submit(g->pip_scene, &point_frame, false);
		if (!TieFlightPointLights_ConfigureScene(g->pip_scene))
			return false;
		TieFlightPointLightParams point_params;
		TieFlightPointLights_GetParams(&point_params);
		TieFlightPbrPassUniforms pbr;
		TieFlightPbr_BuildPassUniforms(&pbr, cam_pos, snap->directional_dir, false, NULL, &point_params);
		AeronScene_SetFrameUniformData(g->pip_scene, AERON_SHADER_STAGE_FRAGMENT, 1, &pbr, sizeof pbr);
		AeronScene_SetPbrDebugViews(g->pip_scene,
									pbr.debug_isolate_term != 0.0f || pbr.spec_geom_adapt == 0.0f);
		if (fl && !TieFlightRenderer_SceneAddPipInstance(g, g->pip_scene, snap, fl, view_proj, cam_pos))
			return false;
		if (!AeronScene_Render(g->pip_scene, cmd))
			return false;
		g->pip_texture = AeronScene_ColorTexture(g->pip_scene);
		return g->pip_texture != NULL;
	}

	if (!g->classic_pip_mesh_pipeline || !g->classic_pip_color_rt || !g->classic_pip_depth_rt)
		return false;
	TIE_GPU_PUSH(cmd, "Flight PIP (CMD CRT)");
	AeronRenderPass* pass = Aeron_BeginRenderPass(&(AeronRenderPassDesc) {
		.color_target = g->classic_pip_color_rt,
		.depth_target = g->classic_pip_depth_rt,
		.viewport = { 0, 0, TIE_FLIGHT_PIP_W, TIE_FLIGHT_PIP_H },
		.clear_color = 1,
		.clear_color_rgba = { clear[0], clear[1], clear[2], clear[3] },
		.clear_depth = 1,
		.clear_depth_value = 0.0f,
		.command_buffer = cmd,
	});
	if (!pass) {
		TIE_GPU_POP(cmd);
		return false;
	}
	if (fl && fl->ship_idx < TIE_FLIGHT_MAX_SPECIES && g->meshes[fl->ship_idx].ready) {
		TieFlightRenderer_ClassicDrawSingle(g, cmd, pass, snap, fl, view_proj, TIE_FLIGHT_PIP_W,
											TIE_FLIGHT_PIP_H);
		TieFlightRenderer_LinesDrawSingle(g, cmd, pass, snap, fl, view_proj, TIE_FLIGHT_PIP_W,
										  TIE_FLIGHT_PIP_H);
	}
	Aeron_EndRenderPass(pass);
	TIE_GPU_POP(cmd);
	g->pip_texture = Aeron_RenderTargetGetTexture(g->classic_pip_color_rt);
	return g->pip_texture != NULL;
}

static bool TieFlightRenderer_SpeciesLfdLocationsEqual(const TieSpeciesLfdLocation* left,
													   const TieSpeciesLfdLocation* right) {
	return left->entry == right->entry && left->resource_set == right->resource_set &&
		   left->lfd_file == right->lfd_file;
}

static bool TieFlightRenderer_PrepareDosModel(TieFlightRenderer* g, AeronCommandBuffer* cmd,
											  uint16_t species) {
	uint8_t* bytes = NULL;
	size_t size = 0;
	char error[768];
	if (!TieFlightAssetSource_ReadModel(g->assets, species, &bytes, &size, NULL, error, sizeof error)) {
		Aeron_RequestFatalError("Flight Asset Error", error);
		return false;
	}
	const bool prepared = TieFlightRenderer_EnsureSpeciesMesh(g, cmd, species, bytes, size, false);
	free(bytes);
	if (!prepared && !Aeron_FatalErrorRequested()) {
		snprintf(error, sizeof error,
				 "TIE95 original flight models: ShipModelData conversion failed "
				 "for species %u",
				 species);
		Aeron_RequestFatalError("Flight Asset Error", error);
	}
	return prepared;
}

static bool TieFlightRenderer_PrepareDosGeneration(TieFlightRenderer* g, AeronCommandBuffer* cmd,
												   const TieSnapshot* snapshot) {
	TieSpeciesLfdLocation locations[TIE_SPECIES_COUNT];
	for (uint16_t model_index = 0; model_index < snapshot->required_model_species_count; ++model_index) {
		const uint16_t species = snapshot->required_model_species[model_index];
		TieSpeciesLfdLocation* location = &locations[model_index];
		if (!TieRecoveredData_SpeciesDosModelLocation(species, location)) {
			char error[256];
			snprintf(error, sizeof error,
					 "TIE95 original flight models: required model species %u has no "
					 "loaded ShipModelData source",
					 species);
			Aeron_RequestFatalError("Flight Asset Error", error);
			return false;
		}
		uint16_t owner_species = UINT16_MAX;
		for (uint16_t preceding = 0; preceding < model_index; ++preceding) {
			if (TieFlightRenderer_SpeciesLfdLocationsEqual(location, &locations[preceding])) {
				owner_species = snapshot->required_model_species[preceding];
				break;
			}
		}
		if (owner_species != UINT16_MAX) {
			g->meshes[species] = g->meshes[owner_species];
			g->meshes[species].owns_resources = false;
		} else if (!TieFlightRenderer_PrepareDosModel(g, cmd, species)) {
			return false;
		}
	}
	for (uint16_t species = TIE_SPECIES_PROJECTILE_FIRST; species <= TIE_SPECIES_PROJECTILE_LAST; ++species) {
		size_t size = 0;
		const void* bytes = tie_laser_species_poly(species, &size);
		/* Retail intentionally has no polygon for the space bomb. */
		if (species == TIE_SPECIES_PROJECTILE_SPACE_BOMB && !bytes && size == 0)
			continue;
		if (!bytes || !size || !TieFlightRenderer_EnsureSpeciesMesh(g, cmd, species, bytes, size, true)) {
			char error[256];
			snprintf(error, sizeof error,
					 "TIE95 original flight models: built-in projectile species %u "
					 "is invalid",
					 species);
			Aeron_RequestFatalError("Flight Asset Error", error);
			return false;
		}
	}
	return true;
}

static bool TieFlightRenderer_PrepareSceneGeneration(TieFlightRenderer* g, AeronCommandBuffer* cmd,
													 const TieSnapshot* snapshot) {
	for (uint16_t index = 0; index < snapshot->required_model_species_count; ++index)
		if (!TieFlightRenderer_EnsureSceneSpeciesShip(g, cmd, snapshot->required_model_species[index]))
			return false;
	/* Scene modes source the immutable projectile models from their catalog. */
	for (uint16_t species = TIE_SPECIES_PROJECTILE_FIRST; species <= TIE_SPECIES_PROJECTILE_LAST; ++species)
		if (!TieFlightRenderer_EnsureSceneSpeciesShip(g, cmd, species))
			return false;
	return true;
}

static bool TieFlightRenderer_WarmFlightSpecies(TieFlightRenderer* g, AeronCommandBuffer* cmd,
												const TieSnapshot* snapshot) {
	(void)TieFlightRenderer_SceneConsumeReload(g);
	const uint32_t mission_gen = snapshot->mission_load_generation;
	if (mission_gen != g->last_warmed_mission_gen) {
		AeronCommandBufferUploadUsage upload_before = { 0 };
		AeronCommandBufferUploadUsage upload_after = { 0 };
		(void)Aeron_CommandBufferGetUploadUsage(cmd, &upload_before);
		for (uint16_t species = 0; species < TIE_FLIGHT_MAX_SPECIES; ++species) {
			TieFlightRenderer_ReleaseSpeciesMesh(g, &g->meshes[species]);
			TieFlightRenderer_ReleaseSceneSpeciesShip(g, &g->scene_ships[species]);
		}
		char error[768];
		TieFlightSpriteCache_Reset(g->sprites);
		if (!TieFlightSpriteCache_Prepare(g->sprites, cmd, snapshot->required_sprite_species,
										  snapshot->required_sprite_species_count, error, sizeof error)) {
			Aeron_RequestFatalError("Flight Asset Error", error);
			return false;
		}
		const bool prepared = TieFlightAssetSource_IsTie95(g->assets)
								  ? TieFlightRenderer_PrepareDosGeneration(g, cmd, snapshot)
								  : TieFlightRenderer_PrepareSceneGeneration(g, cmd, snapshot);
		if (!prepared)
			return false;
		(void)Aeron_CommandBufferGetUploadUsage(cmd, &upload_after);
		Aeron_LogInfo("tie.assets", "mission generation %u warmup: %llu staged bytes, %u copies", mission_gen,
					  (unsigned long long)(upload_after.staged_bytes - upload_before.staged_bytes),
					  upload_after.copy_count - upload_before.copy_count);
		g->last_warmed_mission_gen = mission_gen;
	}
	return !Aeron_FatalErrorRequested();
}

bool TieFlightRenderer_PrepareMissionAssets(TieFlightRenderer* g, AeronCommandBuffer* cmd,
											const TieSnapshot* snapshot) {
	if (!g || !cmd || !snapshot)
		return false;
	if (snapshot->scene_kind != TIE_SCENE_FLIGHT_LOADING && snapshot->scene_kind != TIE_SCENE_FLIGHT)
		return true;

	TieFlightRenderer_RefreshSkybox(g, cmd, snapshot->battle_id);
	if (Aeron_FatalErrorRequested())
		return false;
	return TieFlightRenderer_WarmFlightSpecies(g, cmd, snapshot);
}

bool TieFlightRenderer_PrepareFrame(TieFlightRenderer* g, AeronCommandBuffer* cmd, const TieSnapshot* prev,
									const TieSnapshot* curr, int32_t delta_us, uint64_t host_time_us,
									bool paused) {
	if (!g || !cmd)
		return false;
	if (!curr || curr->scene_kind != TIE_SCENE_FLIGHT) {
		/* Outside flight: no work, no pass. */
		return true;
	}

	/* --- Per-tick uploads (must precede the render pass: copy passes
	 * cannot run inside an active render pass). --- */
	if (!TieFlightRenderer_RefreshMaterialcolors(g, cmd) ||
		!TieFlightRenderer_RefreshPaletteLut(g, cmd, curr->palette)) {
		return false;
	}
	/* The procedural sky has no background primitive: classic fills every
	 * uncovered main-viewport pixel with deepspacecolor (0xFB), then draws
	 * palette-indexed stars over it. Keep the HDR scene clear tied to the
	 * same live palette entry instead of Aeron's generic scene default. */
	float deep_space_clear[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
	TieScene2dSrgb_PalToLinearRgb(curr->palette[TIE_FLIGHT_DEEP_SPACE_PALETTE_INDEX], &deep_space_clear[0],
								  &deep_space_clear[1], &deep_space_clear[2]);
	AeronScene_SetClearColor(g->scene, deep_space_clear);

	if (!TieFlightRenderer_PrepareMissionAssets(g, cmd, curr))
		return false;

	/* Build view-projection + viewport rects. The helper preserves the
	 * captured classic aperture's focal scale while extending the
	 * projection over the full output, and aligns the projection center
	 * with the classic aperture and reticle. The same helper is used by
	 * HD overlays that project world coordinates (e.g. target box).
	 * The scene recomputes the identical view_proj from the camera
	 * scalars below (same transplanted math). */
	TieFlightCamera fcam;
	TieRenderMath_BuildCamera(&fcam, &curr->camera, g->rt_w, g->rt_h);

	const AeronRectI vp =
		TieFlightPasses_ViewportRect(fcam.flight_vp_x, fcam.flight_vp_y, fcam.flight_vp_w, fcam.flight_vp_h);
	fcam.camera.viewport = vp;

	const float* view_proj = fcam.view_proj;

	/* Post-stack gating. SSAO and motion blur run only in the
	 * full-fidelity HD style — not classic, not XvT. Whether the
	 * scene-side chains actually built is the scene's own concern; a
	 * failed chain degrades to the monolithic pass (SSAO) or no blur
	 * (MB) exactly like the pre-switch pipeline-gate checks did. */
	const bool hd_full = g->scene_model_backend;
	const bool mb_on = g->mb_quality != MB_OFF && hd_full;
	const bool fsr_on = g->fsr_mode != AERON_TEMPORAL_OFF && hd_full && curr->hyperspace.phase == 0;
	const bool ssao_on = g->ssao.ssao_quality != SSAO_OFF && g->ssao.ssao_intensity > 0.0f && hd_full;

	/* Build the previous-frame transform context for velocity. A valid
	 * prev requires the same scene / battle / hyperspace phase —
	 * otherwise the camera teleported or the scene was re-entered and any
	 * velocity would be a garbage smear, so we fall back to
	 * prev == current (zero velocity). */
	TieFlightMotionBlurPrevious mb = { 0 };
	int mb_prev_index[TIE_MAX_FLIGHT_OBJECTS];
	if (mb_on || fsr_on) {
		const bool prev_ok = prev && prev->scene_kind == TIE_SCENE_FLIGHT &&
							 prev->battle_id == curr->battle_id &&
							 prev->hyperspace.phase == curr->hyperspace.phase &&
							 prev->camera.pilotview == curr->camera.pilotview &&
							 prev->camera.target_obj_slot == curr->camera.target_obj_slot &&
							 prev->camera.zoom_active == curr->camera.zoom_active;
		if (prev_ok) {
			TieFlightCamera fcam_prev;
			TieRenderMath_BuildCameraAtOrigin(&fcam_prev, &prev->camera, fcam.origin_world, g->rt_w, g->rt_h);
			memcpy(mb.prev_view_proj, fcam_prev.view_proj, sizeof mb.prev_view_proj);
			/* IDs are unique for ships but absent or shared by projectiles,
			 * explosions, debris, and training gates. Match those classes by
			 * slot, guarded by genus and species to reject slot reuse. */
			for (uint16_t i = 0; i < curr->flight_count; ++i) {
				mb_prev_index[i] = -1;
				const TieFlightObjectState* cf = &curr->flights[i];
				const bool by_slot = (cf->id == 0) || cf->genus == TIE_GENUS_EXPLOSION ||
									 cf->genus == TIE_GENUS_DEBRIS || cf->genus == TIE_GENUS_GATE;
				for (uint16_t j = 0; j < prev->flight_count; ++j) {
					const TieFlightObjectState* pf = &prev->flights[j];
					if (pf->genus != cf->genus || pf->ship_idx != cf->ship_idx)
						continue;
					const bool match = by_slot ? (pf->slot == cf->slot) : (pf->id == cf->id);
					if (match) {
						mb_prev_index[i] = (int)j;
						break;
					}
				}
			}
			mb.prev = prev;
			mb.prev_index = mb_prev_index;
			mb.enabled = true;
		} else {
			/* Prev invalid → prev = current view_proj (zero velocity);
			 * the camera fill then also emits zero. */
			memcpy(mb.prev_view_proj, view_proj, sizeof mb.prev_view_proj);
			mb.enabled = false;
		}
	}
	const bool mb_regen = (mb_on || fsr_on) && mb.enabled && curr->flight_frame != prev->flight_frame;
	if (!mb.enabled || !g->mb_pose_host_valid)
		TieFlightRenderer_MotionBlurResetTiming(g, host_time_us);
	else if (mb_regen)
		TieFlightRenderer_MotionBlurRecordPose(g, host_time_us);

	/* Hyperspace-streak prepare — vertex + index build, copy-pass
	 * uploads. No-op outside hyperspace phases 3 & 5. */
	bool hyperstars_ready = g->hyperstars && TieFlightHyperstars_Prepare(g->hyperstars, cmd, curr, &fcam);
	bool stars_ready = g->stars && !TieFlightAssetSource_IsRemastered(g->assets) &&
					   TieFlightStars_Prepare(g->stars, cmd, curr, g->starfield_style);

	/* === Scene frame ===================================================
	 *
	 * Latch the camera (the scene rebuilds the same view_proj), forward
	 * the post knobs, set the motion context, register the hooks, and
	 * translate the snapshot into mesh instances. The scene then owns
	 * the whole pass topology: prepass (+ camera velocity fill) → SSAO
	 * → forward color with AO → billboards/hyperstars via hooks → MB
	 * resolve → cockpit tail. */
	TieFlightRenderer_ConfigureTemporal(g, delta_us, fsr_on ? g->fsr_mode : AERON_TEMPORAL_OFF, mb.enabled);
	if (!AeronScene_Begin(g->scene, &fcam.camera))
		return false;
	int render_w, render_h;
	AeronScene_RenderDims(g->scene, &render_w, &render_h);
	TieFlightPointLightFrame point_frame = { 0 };
	if (g->scene_model_backend)
		TieFlightPointLights_Derive(&point_frame, curr, curr->camera.world_pos);

	/* The classic and XvT styles intentionally retain their original
	 * unshadowed presentation. Full HD uses the snapshot's normalized
	 * surface-to-light direction. Scene instances are local to the current
	 * camera origin while cascade snapping retains its absolute position. */
	if (hd_full && g->shadows.enabled) {
		const TieFlightShadowSettings* settings = &g->shadows;
		const AeronSceneDirectionalShadowDesc shadow = {
				.enabled = 1,
				.light_dir = {
						curr->directional_dir[0],
						curr->directional_dir[1],
						curr->directional_dir[2],
				},
				.world_origin = { (double)fcam.origin_world[0], (double)fcam.origin_world[1],
								  (double)fcam.origin_world[2] },
				.atlas_size = (uint32_t) settings->atlas_size,
				.cascade_count = (uint32_t) settings->cascade_count,
				.fit_mode = settings->fit_mode,
				.max_distance = settings->max_distance,
				.split_lambda = settings->split_lambda,
				.explicit_splits = settings->explicit_splits,
				.split_positions = {
						settings->split_positions[0],
						settings->split_positions[1],
						settings->split_positions[2],
				},
				.filter_quality = (uint32_t) settings->filter_quality,
				.filter_radius = settings->filter_radius,
				.contact_hardening = settings->contact_hardening,
				.light_angular_radius_degrees = settings->light_angular_radius_degrees,
				.max_filter_radius = settings->max_filter_radius,
				.pcss_min_filter_radius = settings->pcss_min_filter_radius,
				.normal_bias_texels = settings->normal_bias_texels,
				.depth_bias_texels = settings->depth_bias_texels,
				.transition_fraction = settings->transition_fraction,
				.distance_fade_fraction = settings->distance_fade_fraction,
				.debug_cascades = settings->debug_cascades,
		};
		AeronScene_SetDirectionalShadow(g->scene, &shadow);
	}

	/* Sky cube — phase/backdrop selection stays game-side; the draw
	 * (first of the color pass, under SKY billboards + meshes) is
	 * scene-owned. Exposure 1.0 = identity for the LDR/BC6H cubes the
	 * bundles ship. NULL cube = no sky drawn this frame. */
	AeronScene_SetSkyCube(g->scene, TieFlightRenderer_SelectSkyboxCube(g, curr), k_tie_world_to_cube, 1.0f);

	/* Normalize the retained snapshot displacement to OpenXWA's 32 ms
	 * reference frame so a configured shutter has the same exposure at every
	 * unlocked update rate. */
	const bool mb_allow = !paused || g->mb_pause_keep_blur;
	const float mb_shutter = mb_allow ? TieFlightRenderer_MotionBlurLogicalFrameShutter(g) : 0.0f;
	AeronScenePostDesc post = {
		.ssao_quality = ssao_on ? (g->ssao.ssao_quality == SSAO_LOW ? 1 : 2) : 0,
		.ssao_intensity = g->ssao.ssao_intensity,
		.ssao_power = g->ssao.ssao_power,
		.ssao_radius_view = g->ssao.ssao_radius_view,
		.ssao_bias_view = g->ssao.ssao_bias_view,
		.ssao_direct = g->ssao.ssao_direct,
		.ssao_debug_viz = g->ssao.ssao_debug_viz,
		.ssao_min_screen_frac = g->ssao.ssao_min_screen_frac,
		.ssao_max_screen_frac = g->ssao.ssao_max_screen_frac,
		.ssao_sample_jitter = g->ssao.ssao_sample_jitter,
		.mb_quality = mb_on ? (g->mb_quality == MB_HIGH ? 2 : 1) : 0,
		.mb_shutter = mb_shutter,
		.mb_camera_blur = g->mb_camera_blur,
		.mb_velocity_viz = g->mb_velocity_viz,
	};
	AeronScene_SetPost(g->scene, &post);

	/* Regenerate velocity only on host ticks where the flight sim
	 * actually advanced — the snapshot is emitted every host tick, but
	 * the sim advances on its own time-gated cadence, so consecutive
	 * host-tick snapshots are often identical (→ zero pos delta). On
	 * duplicate ticks the scene HOLDS its velocity buffer; regenerating
	 * would stamp a spurious zero and drop the blur on that frame. An
	 * invalid prev (scene re-entry) invalidates the held velocity
	 * outright (NULL prev_view_proj). */
	AeronScene_SetMotionContext(g->scene, mb.enabled ? mb.prev_view_proj : NULL, mb_regen);

	/* Atlas sampler tracks the render style (XvT = point-sampled). */
	if (!AeronScene_SetMeshSampler(g->scene, g->opt_sampler))
		return false;
	/* Hook context + registration (idempotent). */
	g->frame.curr = curr;
	g->frame.fcam = fcam;
	g->frame.vp = vp;
	g->frame.stars_ready = stars_ready;
	g->frame.hyperstars_ready = hyperstars_ready;
	g->frame.hd_full = hd_full;
	g->frame.ssao_on = ssao_on;
	AeronScene_SetPassHook(g->scene, AERON_SCENE_HOOK_BEFORE_OPAQUE, TieFlightRenderer_SceneHookBeforeOpaque,
						   g);
	AeronScene_SetPassHook(g->scene, AERON_SCENE_HOOK_AFTER_OPAQUE, TieFlightRenderer_SceneHookAfterOpaque,
						   g);
	AeronScene_SetPassHook(g->scene, AERON_SCENE_HOOK_AFTER_UPSCALE, TieFlightRenderer_SceneHookAfterUpscale,
						   g);

	/* Backdrop planets and billboard sprites are submitted in every render
	 * style through the scene's SKY and OVERLAY stages. */
	const TieFlightLegacyRenderConvention legacy_render = curr->legacy_render_convention;
	if (g->backdrop)
		TieFlightBackdrop_Submit(g->backdrop, g->scene, cmd, curr, &fcam, legacy_render);
	if (g->billboards)
		TieFlightBillboards_Submit(g->billboards, g->scene, cmd, curr, &fcam, mb.enabled ? &mb : NULL,
								   legacy_render);

	/* Snapshot → mesh instances (HD only; classic geometry draws via
	 * the BEFORE_OPAQUE hook instead). */
	TieFlightRenderer_SceneSubmit(g, curr, view_proj, fcam.camera.pos, &mb);
	TieFlightPointLights_Submit(g->scene, &point_frame, true);
	if (!TieFlightPointLights_ConfigureScene(g->scene))
		return false;
	TieFlightPointLightParams point_params;
	TieFlightPointLights_GetParams(&point_params);
	TieFlightPbrAoParams frame_ao = {
		.intensity = ssao_on ? g->ssao.ssao_intensity : 0.0f,
		.power = g->ssao.ssao_power,
		.rt_w = (float)render_w,
		.rt_h = (float)render_h,
		.direct = g->ssao.ssao_direct,
	};
	TieFlightPbrPassUniforms frame_pbr;
	TieFlightPbr_BuildPassUniforms(&frame_pbr, fcam.camera.pos, curr->directional_dir, false, &frame_ao,
								   &point_params);
	AeronScene_SetFrameUniformData(g->scene, AERON_SHADER_STAGE_FRAGMENT, 1, &frame_pbr, sizeof frame_pbr);
	AeronScene_SetPbrDebugViews(g->scene,
								frame_pbr.debug_isolate_term != 0.0f || frame_pbr.spec_geom_adapt == 0.0f);
	return TieFlightRenderer_PreparePip(g, cmd, curr);
}

bool TieFlightRenderer_RenderFrame(TieFlightRenderer* g, AeronCommandBuffer* cmd) {
	if (!g || !cmd || !g->frame.curr)
		return false;
	const TieSnapshot* curr = g->frame.curr;
	TIE_GPU_PUSH(cmd, "Flight scene (HDR)");
	bool ok = AeronScene_Render(g->scene, cmd) != 0;
	TIE_GPU_POP(cmd);
	if (!ok) {
		g->frame.curr = NULL;
		return false;
	}
	TieFlightPointLights_CaptureSceneStats(g->scene);

	/* Scene RT for bloom + present: the scene's mb_rt when the motion-
	 * blur resolve ran this frame, its color RT otherwise. */
	g->scene_rt = AeronScene_SceneRt(g->scene);
	g->frame.curr = NULL; /* hooks must not outlive the snapshot */

	/* === Bloom (operates on the scene RT → bloom chain) ===
	 * Internal sequence of small render passes on the bloom-chain
	 * mips. Computes the message-bar scissor along the way and
	 * caches the bar_y_uv on the TieFlightRenderer instance for the present
	 * pass to consume. Bloom runs only in the full-fidelity HD style —
	 * the classic and XvT styles are post-free. */
	return !g->frame.hd_full || TieFlightRenderer_ApplyBloom(g, cmd, curr);
}
