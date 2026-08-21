#ifndef TIE_REMASTER_FLIGHT_RENDERER_INTERNAL_H
#define TIE_REMASTER_FLIGHT_RENDERER_INTERNAL_H

#include "aeron/render.h"
#include <stdbool.h>
#include <stdint.h>

#include "aeron/scene/gltf_mesh.h"
#include "aeron/scene/mesh.h"
#include "aeron/scene/present.h"
#include "aeron/scene/scene3d.h"
#include "tie_remaster/flight/motion_blur.h"
#include "tie_remaster/flight/render_config.h"
#include "tie_remaster/flight/render_math.h"
#include "tie_remaster/flight/renderer.h"
#include "tie_remaster/flight/shadows.h"
#include "tie_remaster/flight/ssao.h"
#include "tie_runtime/flight_assets/assets.h"
#include "tie_runtime/flight_assets/ship_model_converter.h"
#include "tie_runtime/flight_assets/source.h"
#include "tie_runtime/snapshot/snapshot_types.h"

/* Snapshot caps the species table at 161 entries; we cache one
 * GPU mesh entry per species. Both the classic TieFlightSpeciesMesh and the
 * retained-scene TieFlightSpeciesSceneShip arrays use this size — every renderer
 * cache indexes on species_idx (the snapshot's ship_idx). */
#define TIE_FLIGHT_MAX_SPECIES 161

/* The ShipModelData path scales raw int16 vertices into native world units.
 *
 * Derivation from src/tie/transfm2.c:
 *   - Basis vectors (worldeyeA1..C3, rotworldeyeA1..C3) are Q15:
 *     raw value = basis_unit × 32768.
 *   - Eye-space contribution from a vertex is
 *       eye += (rotworldeye_Q15 × vertex_raw_int16) >> 16
 *           = (basis_unit × vertex_raw × 32768) >> 16
 *           = (basis_unit × vertex_raw) / 2
 *     So 1 raw vertex-local unit equals 0.5 raw world-axis units.
 * One raw classic vertex unit therefore equals one half native world unit. */
#define TIE_CLASSIC_VERTEX_TO_WORLD_UNITS 0.5f
#define TIE_NATIVE_MODEL_TO_WORLD_UNITS 1.0f

/* AERON_MAX_MESH_SLOTS is the shared retained-scene table capacity. */

/* ----- Per-species retained-scene cache ----- */

typedef struct TieFlightSpeciesSceneShip {
	/* GPU-resident model owned by AeronScene. NULL until loaded. */
	AeronSceneMesh* mesh;

	bool tried;
	bool ready;
} TieFlightSpeciesSceneShip;

/* ----- Per-species cache ----- */

typedef struct TieFlightSpeciesMesh {
	AeronBuffer* vbo;
	AeronBuffer* ibo;
	/* Per-species line geometry: dedicated VBO with the
	 * TieFlightLineVertex format (each engine line emits 4 vertices
	 * forming a screen-facing thin-quad input the line vertex
	 * shader expands) plus a TRIANGLELIST IBO (6 indices per line).
	 * NULL when the ship has no line faces. */
	AeronBuffer* line_vbo;
	AeronBuffer* line_ibo;
	uint32_t line_index_count;
	/* Line-geometry LOD segments. NULL/0 means "draw whole line_ibo"
	 * (the antenna/strut path on ship meshes). Lasers carry multiple
	 * entries — `draw_getdetailptr` in the engine picks one based on
	 * the bolt's eye-z; the renderer mirrors that walk per-bolt and
	 * selects the matching index range. Lifetime: owned copy of the
	 * converter's line_lods table, freed in TieFlightRenderer_ReleaseMesh. */
	TieFlightShipModelLineLod* line_lods;
	uint32_t line_lod_count;
	uint32_t index_count;
	uint32_t vertex_count;
	/* Per-face decal overlay — graphics-stage storage buffers consumed
	 * by the mesh fragment shader. `decal_records_sb` holds TieFlightDecal
	 * entries (vert_offset + vert_count + colour + material);
	 * `decal_verts_sb` holds the (u, v) vertex pool the records index
	 * into. NULL when this species has no markings. */
	AeronBuffer* decal_records_sb;
	AeronBuffer* decal_verts_sb;
	uint32_t decal_count;
	uint32_t decal_vert_count;
	/* Per-mesh static rotation data — owned copy of TieFlightShipModel
	 * .mesh_rot[]. Source bytes are freed after upload but the
	 * renderer needs this on every per-craft draw to build the
	 * fview_componentrotation transform. has_any_rotation lets the
	 * per-craft draw skip articulation work for fully
	 * static ships. */
	AeronMeshRot* mesh_rot;
	uint32_t mesh_count;
	bool has_any_rotation;
	bool owns_resources;
	bool tried; /* set once we've attempted a build for this slot */
	bool ready; /* set if both buffers are populated */
	/* Mirrors TieFlightShipModel.model_scale_shift — see flight_shipmodel_converter.h.
	 * Read by the per-craft draw to bias craft_to_world for the
	 * model_scale_shift == 2 S2 path. */
	uint8_t model_scale_shift;
	/* Conservative bounding-sphere radius in WORLD units, derived from
	 * converted AABB (`bound_min/max`) × classic half scale × ms_factor.
	 * Used by per-craft frustum culling. Conservative = farthest AABB
	 * corner from the craft origin; over-estimates for asymmetric
	 * hulls, which is fine — false positives just render a craft we
	 * could have skipped. */
	float bound_radius_world;
} TieFlightSpeciesMesh;

/* ----- Per-frame scene-submission context ----- */

/* Cap on scene instances submitted per frame. Flights (120) + statics
 * (64) from the snapshot caps; the scene's own cap is 256. */
#define TIE_FLIGHT_SCENE_MAX_INSTANCES 192
#define TIE_FLIGHT_PIP_W 512
#define TIE_FLIGHT_PIP_H 384

/* Hook context: populated by TieFlightRenderer_Prep before AeronScene_Render;
 * the scene pass hooks (skybox / classic geometry / hyperstars /
 * cockpit) read it via the TieFlightRenderer they get as userdata — backdrops
 * and billboards are batched scene billboards now, not hook draws.
 * Valid only during the Render call. */
typedef struct TieFlightSceneFrame {
	const struct TieSnapshot* curr;
	TieFlightCamera fcam;
	AeronRectI vp;
	bool stars_ready;
	bool hyperstars_ready;
	bool hd_full;
	/* SSAO requested this frame — the BEFORE_OPAQUE hook pushes the
	 * pbr AO params with the live intensity; if the scene-side SSAO
	 * chain failed to build, the AO texture stays white and the FS
	 * term is a no-op, so this needs no feedback from the scene. */
	bool ssao_on;
} TieFlightSceneFrame;

typedef struct TieFlightClassicMeshTables {
	AeronBuffer* buffer;
	AeronSceneMeshTable tables[TIE_MAX_FLIGHT_OBJECTS + 1];
	uint32_t count;
} TieFlightClassicMeshTables;

_Static_assert(sizeof(AeronSceneMeshTable) == AERON_MESH_TABLE_STRIDE_VEC4 * sizeof(float[4]),
			   "classic mesh table must match the shared shader layout");

/* ----- TieFlightRenderer state ----- */

struct TieFlightRenderer {
	/* Scene renderer (aeron_scene). Owns the HDR color RT
	 * (R11G11B10_UFLOAT), the sampled D32 depth RT, the octahedral
	 * normal G-buffer, the SSAO / motion-blur post chains, and the pbr
	 * instance walk. The cockpit / billboard / hyperstar / classic
	 * draws enter its passes through the registered hooks. */
	AeronScene3D* scene;
	AeronScene3D* pip_scene;
	AeronRenderTarget* classic_pip_color_rt;
	AeronDepthTarget* classic_pip_depth_rt;
	AeronTexture* pip_texture;
	AeronSceneMeshTable pip_scene_table;
	TieFlightSceneFrame frame;
	/* Per-frame custom mesh tables borrowed by scene instances until Render. */
	AeronSceneMeshTable hd_scene_tables[TIE_FLIGHT_SCENE_MAX_INSTANCES];
	TieFlightClassicMeshTables classic_mesh_tables;
	uint32_t classic_pip_table_index;
	int rt_w;
	int rt_h;
	/* HDR format used by `color_rt`, the bloom chain, and every cockpit
	 * pipeline that draws into the same scene pass. */
	AeronTextureFormat rt_format;
	/* Swapchain colour format (final present-pass target). */
	AeronTextureFormat target_format;
	AeronSampleCount requested_sample_count;
	AeronSampleCount sample_count;
	TieFlightStarfieldStyle starfield_style;

	/* Shader objects loaded from precompiled MSL blobs. */
	AeronShader* mesh_vs;
	AeronShader* mesh_classic_lut_ps;
	AeronShader* line_vs;
	/* Tonemap + present FS pair. The fullscreen-quad VS is retained
	 * (`present_vs`) so the pipelines can be rebuilt on swapchain
	 * composition flips without recompiling shaders. Two FS variants
	 * coexist:
	 *   - present_ps_sdr: writes linear-sRGB display values (clamped,
	 *     gamma-encoded via the swapchain's _SRGB store).
	 *   - present_ps_hdr: writes scRGB-linear values >1.0 for HDR
	 *     display headroom; no clamp, no sRGB encode. */

	/* Pipelines (created lazily once the shaders + RT format are
	 * known). */
	AeronGraphicsPipeline* mesh_pipeline;
	AeronGraphicsPipeline* line_pipeline;
	/* Same vertex+fragment shader as line_pipeline but with depth
	 * write disabled. Used for laser-bolt / missile-warhead line
	 * geometry, where the engine's per-edge painter's order matters
	 * AND multiple edges share the same eye-Z plane. Depth_write off
	 * stops the 5 (or 7) edges of one bolt from fighting each other
	 * for the z-buffer; depth_test stays GREATER_OR_EQUAL so the
	 * bolt is still occluded by ships in front of it. */
	AeronGraphicsPipeline* bolt_line_pipeline;
	/* The fixed-size classic PiP renders at 1x regardless of main-scene
	 * MSAA, so its pipelines must carry the matching sample count. */
	AeronGraphicsPipeline* classic_pip_mesh_pipeline;
	AeronGraphicsPipeline* classic_pip_line_pipeline;
	AeronGraphicsPipeline* classic_pip_bolt_line_pipeline;
	/* Final present pipeline pair — fullscreen quad, PMA-over blend
	 * so the tonemapped flight overlay cross-fades onto the swapchain
	 * (over the classic FB underneath). Both pipelines target the
	 * current swapchain format; rebuilt on swapchain composition flip
	 * via the GpuSwapchainFormatChangedFn callback. TieFlightRenderer_Present
	 * picks _sdr or _hdr based on hdr_state.hdr_active.
	 *
	 * Sampler bindings: t0 = color_rt (HDR scene+cockpit), t1 = bloom
	 * mip0. Uniforms b0: bloom_params, tonemap_params (with
	 * hdr_peak_scale + sdr_to_scrgb), present_tint. */
	AeronScenePresentChain* present_chain;
	AeronScenePresentChain* swapchain_present_chain;
	AeronTextureFormat swapchain_format;

	/* Static materialcolors LUT (16×45 = 720 bytes). The engine can
	 * mutate row 13 at runtime via drawpol_setmarkingcolors, so we
	 * snapshot the table each tick and only re-upload on change.
	 * `materialcolors_have_cache` distinguishes "never seen" from
	 * "matches the all-zero default" on the first frame. */
	AeronTexture* materialcolors_tex;
	uint8_t materialcolors_cached[16 * 45];
	bool materialcolors_have_cache;

	/* Snapshot palette — 256×1 8bpp, refreshed when contents change. */
	AeronTexture* palette_tex;
	uint32_t palette_cached[256];

	/* Samplers — nearest for the integer LUTs, linear for the
	 * skybox cube map. */
	AeronSampler* sampler;
	AeronSampler* sampler_linear;

	/* Selected flight asset provider. */
	const TieFlightAssetSource* assets;
	struct TieFlightSpriteCache* sprites;

	/* Source-neutral debris / explosion / lightning sprites. */
	struct TieFlightBillboards* billboards;

	/* At-infinity skybox-backdrop planet pass (tie_core BACKDRP2). Drawn
	 * over the base cubemap, before scene geometry. NULL if init failed. */
	struct TieFlightBackdrop* backdrop;
	struct TieFlightStars* stars;

	/* HD hyperspace-streak pass — drawn only during cinematic phases
	 * 3 & 5. NULL on shader / pipeline init failure; the flight render
	 * then skips the streak pass. */
	struct TieFlightHyperstars* hyperstars;

	/* Borrowed cockpit and flight-HUD compositor. NULL outside flight scenes.
	 * Set by TieFlightRenderer_SetCockpit so TieFlightRenderer_Prep can fold the
	 * cockpit's in-pass draws into its own render encoder. */
	struct TieCockpitRenderer* cockpit;

	/* HD bloom post-process — owned. NULL on shader / pipeline / mip
	 * create failure; TieFlightRenderer_ApplyBloom is a safe no-op in that
	 * case. See flight_bloom.h. */
	struct AeronSceneBloom* bloom;

	/* Cached message-bar top in flight-RT UV.y, updated each
	 * TieFlightRenderer_ApplyBloom call and consumed by the swapchain
	 * composite (which gates bloom contribution below this Y).
	 * Defaults to 1.0 (no exclusion). */
	float last_bloom_bar_y_uv;

	/* Per-species mesh caches. Both indexed on species_idx (the
	 * snapshot's ship_idx), so the per-craft loop dispatches between
	 * them without translation. Most slots are unused for any given
	 * mission — 95 species_table[] rows are non-ship sentinels. */
	TieFlightSpeciesMesh meshes[TIE_FLIGHT_MAX_SPECIES];
	TieFlightSpeciesSceneShip scene_ships[TIE_FLIGHT_MAX_SPECIES];

	/* Snapshot generation for which model and sprite caches were prepared.
	 * UINT32_MAX forces preparation for the first flight snapshot. */
	uint32_t last_warmed_mission_gen;

	/* Linear+anisotropic scene-mesh atlas sampler. */
	AeronSampler* opt_sampler;

	/* Motion-blur runtime settings forwarded to AeronScene. */
	MbQuality mb_quality;    /* MB_OFF default */
	float mb_shutter;        /* ≈0.5 (180°) */
	bool mb_velocity_viz;    /* debug: show velocity_rt */
	bool mb_pause_keep_blur; /* debug: blur while paused */
	bool mb_camera_blur;     /* blur camera motion (default on) */
	/* Host-time span represented by the retained velocity buffer. */
	uint64_t mb_pose_host_us;
	uint64_t mb_velocity_span_us;
	bool mb_pose_host_valid;
	/* FSR 3.1.4 fixed-mode configuration and temporal reset state.
	 * Aeron owns all GPU resources and dispatch sequencing. */
	AeronTemporalMode fsr_mode;
	float fsr_sharpness;
	bool fsr_reset_pending;
	/* Per-frame scene RT: the scene's color RT normally, its mb_rt when
	 * the motion-blur resolve ran (AeronScene_SceneRt). Read by bloom +
	 * present so they operate on the blurred frame. Set each prep. */
	AeronRenderTarget* scene_rt;

	/* Shared spatial post settings, initialized from the canonical Aeron
	 * profile and mutable through the debug inspectors. */
	AeronSceneSsaoSettings ssao;

	/* Directional-shadow settings loaded from application config.yaml and
	 * mutable through the debug inspector. Aeron owns the atlas,
	 * pipelines, cascade fitting, and sampling resources. */
	TieFlightShadowSettings shadows;

	bool scene_model_backend;
	/* Inspector requests are acknowledged during flight and take effect at the
	 * next mission generation, when simulation and rendering rebuild together.
	 * `scene_reload_one_idx` uses 0xFFFF as its sentinel. */
	bool scene_reload_all;
	uint16_t scene_reload_one_idx;

	/* Active cube map. */
	AeronTexture* skybox_cube;
	int skybox_loaded_battle; /* -1 = none */
};

/* ----- Uniform layouts (must mirror HLSL cbuffer declarations) ----- */

typedef struct TieFlightMeshVertexUniforms {
	float view_proj[16];
	float craft_to_world[16];
	/* When non-zero, the fragment shader's lambert is computed against
	 * the craft-local normal (skipping the craft_to_world rotation on
	 * the normal output). Mirrors classic's lightflag=0 path, set by
	 * tie.c:1876 for genus=GENUS_GATE only — lighting follows each
	 * gate's local frame rather than staying world-fixed. */
	float light_local_frame;
	/* Engine `gouraudflag` toggle, from snapshot.gouraudflag.
	 * 0.0 = flat (face normal); 1.0 = per-vertex Gouraud on
	 * faces whose flag_byte has bit 0x40 set. */
	float gouraud_enabled;
	float _pad_gv[2];
	/* Directional light direction in world space. Q15-derived, NOT
	 * normalised — matches the engine's `light_world / 32768`. */
	float directional_dir[3];
	float _pad_dir;
	/* World→craft-local rotation, pre-divided by per-craft total
	 * scale (classic half scale × ms_factor) so the result lives in
	 * the same int16-as-float craft-local frame as v.position.
	 * Row-major 3x3 with each row padded to a 4-float vec to match
	 * HLSL cbuffer packing. */
	float world_to_craft[3][4];
	/* Craft origin in scene-local native units — subtracted from each light's
	 * world pos before applying world_to_craft. */
	float craft_world_pos[3];
	float _pad_cwp;
	uint32_t mesh_table_index;
	uint32_t _pad_mesh_table[3];
} TieFlightMeshVertexUniforms;

/* The cooked-glb per-instance VS cbuffer (view_proj + transforms +
 * variant + camera_mb) moved to aeron_scene's pbr class along with the
 * per-craft prim→material LUT; nothing glb-specific remains here. */

typedef struct TieFlightLineVertexUniforms {
	float pixel_to_clip_xy[2];
	float thickness_mul;
	/* Tunable runtime knob: floor multiplier in engine-pixel units.
	 * Final HD-pixel floor = thickness_mul × line_floor_mul. Default
	 * 0.5 = engine-SVGA-faithful (1 SVGA px of screen coverage). Raise
	 * toward 1.0–1.2 for thicker VGA-style bolts; lower below 0.5 for
	 * a thinner floor. Overridable via env TIE_LINE_FLOOR_MUL. */
	float line_floor_mul;
} TieFlightLineVertexUniforms;

typedef struct TieFlightMeshPixelUniforms {
	/* markcoloroffset[14] (drawpol.c:248): signed -1/0/+1 shift driving
	 * the marking-state animation. Applied only to face material 14. */
	float marking_state_offset;
	/* Engine `thicknessMultiple` ported to HD scale, consumed by the
	 * line-marking branch in the FS (parity with the surface-line
	 * pipeline's TieFlightLineVertexUniforms.thickness_mul). FS pixel width =
	 * `base × line_thickness_mul / (eye_z_world / 256) + 1`,
	 * mirroring drawpol_drawmarkings + drawpol_drawlineface
	 * (drawpol.c:438-440, 776-779). */
	float line_thickness_mul;
	/* Same runtime knob as TieFlightLineVertexUniforms.line_floor_mul (env
	 * TIE_LINE_FLOOR_MUL). FS line-marking branch reads it too so
	 * markings track lasers/antennas. */
	float line_floor_mul;
	float _pad;
} TieFlightMeshPixelUniforms;

typedef struct TieFlightLightGpu {
	float pos[3];
	float range;
	float color[3];
	float falloff_sq;
	/* falloff_sq = (falloff_radius_engine)² — pre-squared host-side
	 * so the VS does one divide instead of a square + divide per
	 * vertex × per light. The VS formula reads
	 *     contrib = rg * gain / (d² / falloff_sq + 1)
	 * so falloff_sq directly takes the place the engine had a
	 * hardcoded 4096 in. */
} TieFlightLightGpu;

typedef struct TieFlightLightBufferGpu {
	uint32_t light_count;
	uint32_t _pad[3];
	TieFlightLightGpu lights[16];
} TieFlightLightBufferGpu;

/* ----- Cross-TU helpers (implementations in their owning .c) ----- */

/* pipelines.c */

/* Shared graphics-pipeline state factories. Flight-domain pipelines use
 * the same fill / non-cull / CCW rasterizer and one of two blends: replace
 * with a per-target write mask, or premultiplied-alpha "over". */
AeronBlendStateDesc TieFlightRenderer_BlendOpaque(uint8_t write_mask);
AeronBlendStateDesc TieFlightRenderer_BlendPmaOver(void);

AeronShader* TieFlightRenderer_CompileShader(const char* basename, AeronShaderStage stage,
											 uint32_t num_samplers, uint32_t num_uniform_buffers,
											 uint32_t num_storage_buffers);
AeronGraphicsPipeline* TieFlightRenderer_CreateMeshPipeline(AeronShader* vs, AeronShader* ps,
															AeronTextureFormat rt_fmt,
															AeronSampleCount sample_count);
AeronGraphicsPipeline* TieFlightRenderer_CreateLinePipeline(AeronShader* vs, AeronShader* ps,
															AeronTextureFormat rt_fmt,
															AeronSampleCount sample_count);
AeronGraphicsPipeline* TieFlightRenderer_CreateBoltLinePipeline(AeronShader* vs, AeronShader* ps,
																AeronTextureFormat rt_fmt,
																AeronSampleCount sample_count);
/* classic_mesh.c */
bool TieFlightRenderer_EnsureSpeciesMesh(struct TieFlightRenderer* g, AeronCommandBuffer* cmd,
										 uint16_t species_idx, const void* blob, size_t blob_size,
										 bool is_laser);
void TieFlightRenderer_ReleaseSpeciesMesh(struct TieFlightRenderer* g, TieFlightSpeciesMesh* m);

/* scene_mesh.c: prepare a retained scene mesh from the selected provider. */
bool TieFlightRenderer_EnsureSceneSpeciesShip(struct TieFlightRenderer* g, AeronCommandBuffer* cmd,
											  uint16_t species_idx);
void TieFlightRenderer_ReleaseSceneSpeciesShip(struct TieFlightRenderer* g, TieFlightSpeciesSceneShip* s);

/* Acknowledge inspector reload requests without evicting the immutable
 * current-mission model generation. */
bool TieFlightRenderer_SceneConsumeReload(struct TieFlightRenderer* g);

struct TieSnapshot;
struct TieFlightObjectState;

/* Previous-frame transform context for motion-blur velocity. Built once
 * per frame in TieFlightRenderer_Prep and threaded to the scene submission so
 * each craft's instance gets its prev transform (and to the billboard
 * prepare, which bakes per-quad velocity from it). When `enabled` is
 * false (MB off, or prev snapshot invalid) instances get prev = current
 * → zero velocity. `prev_index[i]` maps a current flight index to the
 * matching prev flight index (id-matched), or -1 when unmatched. */
typedef struct TieFlightMotionBlurPrevious {
	bool enabled;
	float prev_view_proj[16];
	const struct TieSnapshot* prev;
	const int* prev_index; /* curr flight idx → prev idx, -1 none */
} TieFlightMotionBlurPrevious;

/* Submit snapshot flights and statics to a begun Aeron scene. The submission
 * applies eligibility, sorting, culling, current and previous transforms,
 * and custom mesh tables. It is a no-op when scene models are disabled. */
void TieFlightRenderer_SceneSubmit(struct TieFlightRenderer* g, const struct TieSnapshot* curr,
								   const float view_proj[16], const float cam_pos[3],
								   const TieFlightMotionBlurPrevious* mb);

/* Add the selected craft to a begun retained PIP scene. The instance table
 * remains owned by TieFlightRenderer through AeronScene_Render. */
bool TieFlightRenderer_SceneAddPipInstance(struct TieFlightRenderer* g, AeronScene3D* scene,
										   const struct TieSnapshot* snap,
										   const struct TieFlightObjectState* fl, const float view_proj[16],
										   const float cam_pos[3]);

/* Frustum-cull helpers shared by the classic and scene-model passes. */
typedef struct TieFlightFrustumPlanes {
	float p[4][4]; /* (nx, ny, nz, d), normalised — 4 side planes */
} TieFlightFrustumPlanes;

void TieFlightRenderer_BuildFrustumPlanes(TieFlightFrustumPlanes* fp, const float view_proj[16]);
bool TieFlightRenderer_SphereOutsideFrustum(const TieFlightFrustumPlanes* fp, const float center[3],
											float radius);
int TieFlightRenderer_CmpDrawKey(const void* a, const void* b);

/* Render-target, buffer, and texture-upload helpers. */
AeronRenderTarget* TieFlightRenderer_CreateColorRt(AeronTextureFormat fmt, int w, int h);
AeronDepthTarget* TieFlightRenderer_CreateDepthRt(int w, int h);
AeronTexture* TieFlightRenderer_CreateDataTex(AeronTextureFormat fmt, int w, int h, int bpp);
AeronBuffer* TieFlightRenderer_CreateBuffer(uint32_t usage, uint32_t size);
/* Ensure *buf can hold `need` bytes, reallocating (release + create,
 * capacity doubled from 4096) when it can't. Names the new buffer
 * `dbg_name` for the debug HUD. Returns false (and zeroes *cap) on
 * allocation failure. Contents are not preserved across a grow. */
bool TieFlightRenderer_GrowBuffer(AeronBuffer** buf, uint32_t* cap, uint32_t need, uint32_t usage,
								  const char* dbg_name);
bool TieFlightRenderer_UploadToTexture(AeronCommandBuffer* cmd, AeronTexture* tex, int w, int h, int bpp,
									   const void* data);
bool TieFlightRenderer_UploadToBuffer(AeronCommandBuffer* cmd, AeronBuffer* buf, const void* data,
									  uint32_t size);

#endif
