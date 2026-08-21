/* Translates snapshot objects into retained Aeron scene instances. Hidden or
 * off-frustum objects remain eligible as shadow-only casters. Per-frame mesh
 * tables are borrowed from TieFlightRenderer storage until scene rendering. */

#include "aeron/aeron.h"
#include "aeron/asset/opt_model.h"
#include "aeron/log.h"
#include "aeron/scene/world.h"
#include "tie_remaster/flight/mesh_common.h"
#include "tie_remaster/flight/render_math.h"
#include "tie_remaster/flight/renderer_internal.h"
#include <stdio.h>

#include "tie_runtime/snapshot/snapshot.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "tie_runtime/audio/config.h"
#include "tie_runtime/diagnostics/diagnostics.h"
#include "tie_runtime/display/classic_display.h"
#include "tie_runtime/display/classic_framebuffer.h"
#include "tie_runtime/flight_assets/model_types.h"
#include "tie_runtime/input/input.h"
#include "tie_runtime/runtime/exports.h"
#include "tie_runtime/runtime/profile.h" /* tie_species_symbolic_name */
#include "tie_runtime/storage/storage.h"

/* Filter a flight object down to "has HD mesh semantics if the cooked
 * glb cache is ready". Camera/frustum visibility is classified later
 * because hidden and off-screen objects may still cast into a visible
 * shadow cascade. */
static bool TieFlightRenderer_SceneMeshEligible(const TieFlightObjectState* fl, uint16_t* out_species_idx) {
	const uint16_t species_idx =
		(fl->genus == TIE_GENUS_DEBRIS) ? (uint16_t)fl->parent_ship_idx : (uint16_t)fl->ship_idx;
	if (species_idx >= TIE_FLIGHT_MAX_SPECIES)
		return false;

	switch (fl->genus) {
		case TIE_GENUS_FIGHTER:
		case TIE_GENUS_TRANSPORT:
		case TIE_GENUS_UTILITY:
		case TIE_GENUS_FREIGHTER:
		case TIE_GENUS_STARSHIP:
		case TIE_GENUS_PLATFORM:
		case TIE_GENUS_GATE:
		case TIE_GENUS_PROJECTILE_NPC:
		case TIE_GENUS_PROJECTILE_PLAYER:
			break;
		case TIE_GENUS_DEBRIS:
			if (fl->parent_ship_idx == TIE_SPECIES_NONE)
				return false;
			break;
		default:
			return false;
	}
	if (fl->flags & TIE_FOBJ_INVISIBLE)
		return false;

	*out_species_idx = species_idx;
	return true;
}

static bool TieFlightRenderer_SceneMeshCameraHidden(const TieFlightObjectState* fl, const TieSnapshot* curr) {
	return fl->slot == curr->camera.target_obj_slot && curr->camera.zoom_active == 0 &&
		   curr->replay_mode != 2;
}

/* Static objects use ship_class 8..11 as their mesh-rendering discriminator. */
static bool TieFlightRenderer_SceneStaticMeshEligible(const TieStaticObjectState* so,
													  uint16_t* out_species_idx) {
	const uint16_t species_idx = (uint16_t)so->species;
	if (species_idx >= TIE_FLIGHT_MAX_SPECIES)
		return false;
	if (so->ship_class < 8 || so->ship_class > 11)
		return false;
	if (!so->model_visible)
		return false;
	*out_species_idx = species_idx;
	return true;
}

static bool TieFlightRenderer_SceneSpeciesReady(TieFlightRenderer* g, uint16_t species_idx) {
	if (g->scene_ships[species_idx].ready)
		return true;
	char message[256];
	snprintf(message, sizeof message, "flight model source %s: species %u scene mesh was not prepared",
			 g->assets->name, (unsigned)species_idx);
	Aeron_RequestFatalError("Flight Asset Error", message);
	return false;
}

/* True if this craft's mesh-table payload differs from the identity
 * the scene pushes for table-less instances. Conservative: any species
 * with articulation capability + non-empty components forces custom
 * (we don't peek at rotation_angle to detect "actually static this
 * tick" — same heuristic the classic path uses). */
static bool TieFlightRenderer_CraftNeedsCustomMeshTable(const TieFlightSpeciesSceneShip* s,
														const TieFlightObjectState* fl,
														const TieSnapshot* curr) {
	if (fl->genus == TIE_GENUS_DEBRIS)
		return true;
	if (fl->component_count == 0 || !curr->flight_components)
		return false;
	/* Any hidden component → visibility mask differs from identity. */
	const uint32_t cs = fl->component_start;
	const uint32_t cc = fl->component_count;
	for (uint32_t ci = 0; ci < cc; ++ci) {
		if (!(curr->flight_components[cs + ci].flags & 0x1u))
			return true;
	}
	/* Articulated species + components → may rotate; build to be safe. */
	if (s->mesh->has_any_rotation)
		return true;
	return false;
}

/* Projectile meshes are ribbons authored along local Y with their broad-face
 * normal along +Z. Rotate that face toward the camera without changing the
 * travel axis, preserve the reflected render basis, and enforce a minimum
 * projected width. Degenerate orientations retain the quaternion basis. */
#define MIN_PROJECTILE_HALF_PX 1.25f

static void TieFlightRenderer_BuildProjectileAxialC2w(const TieFlightObjectState* fl,
													  const float position_local[3], const float cam_pos[3],
													  const float view_proj[16], const float bound_min[3],
													  const float bound_max[3], int rt_w, int rt_h,
													  float c2w[16]) {
	/* Render-basis rotation (proper rotation × col-1 negation, same
	 * basis ships use). Provides the bolt's long-axis world direction
	 * via column 1. */
	float r3[9];
	TieRenderMath_QuaternionToRenderBasis(fl->ori, r3);

	float lng[3] = { r3[1], r3[4], r3[7] };
	float lng_len = sqrtf(lng[0] * lng[0] + lng[1] * lng[1] + lng[2] * lng[2]);
	if (lng_len < 1e-6f) {
		TieRenderMath_Mat4FromQuaternionTranslation(c2w, fl->ori, position_local, AERON_OPT_UNITS_PER_METER);
		return;
	}
	lng[0] /= lng_len;
	lng[1] /= lng_len;
	lng[2] /= lng_len;

	/* Camera offset, then its component perpendicular to the long
	 * axis (= the direction the broad face should "face"). */
	const float tc[3] = {
		cam_pos[0] - position_local[0],
		cam_pos[1] - position_local[1],
		cam_pos[2] - position_local[2],
	};
	const float dot_tl = tc[0] * lng[0] + tc[1] * lng[1] + tc[2] * lng[2];
	float perp[3] = {
		tc[0] - dot_tl * lng[0],
		tc[1] - dot_tl * lng[1],
		tc[2] - dot_tl * lng[2],
	};
	const float perp_len2 = perp[0] * perp[0] + perp[1] * perp[1] + perp[2] * perp[2];

	float nrm[3];
	if (perp_len2 < 1e-8f) {
		/* Camera collinear with the bolt's long axis. Billboard
		 * direction undefined — fall back to the original local +Z
		 * world mapping (= r3 col 2). The bolt is a dot at this
		 * angle by intent, so the visible artefact is acceptable. */
		const float z_len = sqrtf(r3[2] * r3[2] + r3[5] * r3[5] + r3[8] * r3[8]);
		if (z_len > 1e-6f) {
			nrm[0] = r3[2] / z_len;
			nrm[1] = r3[5] / z_len;
			nrm[2] = r3[8] / z_len;
		} else {
			nrm[0] = 0.0f;
			nrm[1] = 0.0f;
			nrm[2] = 1.0f;
		}
	} else {
		const float pl = sqrtf(perp_len2);
		nrm[0] = perp[0] / pl;
		nrm[1] = perp[1] / pl;
		nrm[2] = perp[2] / pl;
	}

	/* Broad axis = -(lng × nrm) keeps the matrix's determinant
	 * matching the render basis (det = -1). */
	const float brd[3] = {
		-(lng[1] * nrm[2] - lng[2] * nrm[1]),
		-(lng[2] * nrm[0] - lng[0] * nrm[2]),
		-(lng[0] * nrm[1] - lng[1] * nrm[0]),
	};

	/* Clamp the bolt's two-dimensional projected half-width to preserve
	 * coverage on the single-sample flight target. This uses full target
	 * dimensions and can slightly under-inflate inside a smaller viewport. */
	float broad_scale_mult = 1.0f;
	const float half_w_local = 0.5f * (bound_max[0] - bound_min[0]);
	if (half_w_local > 0.0f) {
		const float w_center = view_proj[12] * position_local[0] + view_proj[13] * position_local[1] +
							   view_proj[14] * position_local[2] + view_proj[15];
		if (w_center > 1e-3f) {
			const float half_w_world = half_w_local * AERON_OPT_UNITS_PER_METER;
			const float clip_x_per_unit =
				view_proj[0] * brd[0] + view_proj[1] * brd[1] + view_proj[2] * brd[2];
			const float clip_y_per_unit =
				view_proj[4] * brd[0] + view_proj[5] * brd[1] + view_proj[6] * brd[2];
			const float pix_x = (clip_x_per_unit / w_center) * 0.5f * (float)rt_w * half_w_world;
			const float pix_y = (clip_y_per_unit / w_center) * 0.5f * (float)rt_h * half_w_world;
			const float pix_half_w = sqrtf(pix_x * pix_x + pix_y * pix_y);
			if (pix_half_w > 1e-6f && pix_half_w < MIN_PROJECTILE_HALF_PX) {
				broad_scale_mult = MIN_PROJECTILE_HALF_PX / pix_half_w;
			}
		}
	}

	const float scl = AERON_OPT_UNITS_PER_METER;
	const float bscl = scl * broad_scale_mult;
	c2w[0] = brd[0] * bscl;
	c2w[1] = lng[0] * scl;
	c2w[2] = nrm[0] * scl;
	c2w[3] = position_local[0];
	c2w[4] = brd[1] * bscl;
	c2w[5] = lng[1] * scl;
	c2w[6] = nrm[1] * scl;
	c2w[7] = position_local[1];
	c2w[8] = brd[2] * bscl;
	c2w[9] = lng[2] * scl;
	c2w[10] = nrm[2] * scl;
	c2w[11] = position_local[2];
	c2w[12] = 0.0f;
	c2w[13] = 0.0f;
	c2w[14] = 0.0f;
	c2w[15] = 1.0f;
}

/* Static instances retain their transform so motion vectors contain only
 * camera motion. Matched dynamic instances use the previous snapshot pose;
 * projectiles retain their current camera-facing roll and replace position
 * only. Unmatched instances explicitly produce zero velocity. */
static void TieFlightRenderer_BuildInstance(TieFlightRenderer* g, AeronSceneMeshInstance* in,
											const TieFlightSpeciesSceneShip* s,
											const TieFlightObjectState* fl, const float view_proj[16],
											const float cam_pos[3], const int32_t origin_world[3],
											const TieFlightMotionBlurPrevious* mb, uint16_t curr_index,
											bool is_static, const AeronSceneMeshTable* table) {
	memset(in, 0, sizeof *in);
	in->mesh = s->mesh;
	in->mesh_table = table;
	/* OPTs may use opposite-wound faces for distinct interior and exterior
	 * textures. Preserve the single-sided contract through remastered assets. */
	if (s->mesh->all_materials_single_sided)
		in->cull_mode = AERON_CULL_BACK;

	const bool is_projectile =
		(fl->genus == TIE_GENUS_PROJECTILE_NPC || fl->genus == TIE_GENUS_PROJECTILE_PLAYER);
	float position_local[3];
	AeronWorld_LocalI32(origin_world, fl->world_pos, position_local);
	if (is_projectile) {
		TieFlightRenderer_BuildProjectileAxialC2w(fl, position_local, cam_pos, view_proj, s->mesh->bound_min,
												  s->mesh->bound_max, g->rt_w, g->rt_h, in->transform);
	} else {
		TieRenderMath_Mat4FromQuaternionTranslation(in->transform, fl->ori, position_local,
													AERON_OPT_UNITS_PER_METER);
	}

	memcpy(in->prev_transform, in->transform, sizeof in->prev_transform);
	if (mb && mb->enabled && !is_static) {
		const int pj = mb->prev_index ? mb->prev_index[curr_index] : -1;
		if (pj >= 0) {
			const TieFlightObjectState* pfl = &mb->prev->flights[pj];
			float previous_local[3];
			AeronWorld_LocalI32(origin_world, pfl->world_pos, previous_local);
			if (is_projectile) {
				in->prev_transform[3] = previous_local[0];
				in->prev_transform[7] = previous_local[1];
				in->prev_transform[11] = previous_local[2];
			} else {
				TieRenderMath_Mat4FromQuaternionTranslation(in->prev_transform, pfl->ori, previous_local,
															AERON_OPT_UNITS_PER_METER);
			}
		} else {
			/* Spawn frame — no matched prev pose; camera-consistent
			 * "static" treatment would smear a bogus streak. */
			in->zero_velocity = 1;
		}
	}
	/* is_static with mb enabled: prev_transform == transform + the
	 * scene's previous view_proj → camera-induced velocity. */

	in->variant = s->mesh->variant_count ? (uint32_t)fl->model_variant < s->mesh->variant_count
											   ? fl->model_variant
											   : s->mesh->variant_count - 1
										 : 0;

	/* Retain the projectile material rule used by the scene model path. */
	if (is_projectile)
		in->no_local_lights = 1;
	if (is_projectile) {
		in->shadow_flags |= AERON_SCENE_INSTANCE_NO_CAST_SHADOW | AERON_SCENE_INSTANCE_NO_RECEIVE_SHADOW;
		/* Original OPT projectiles use their complete base texture as the glow source. */
		if (TieFlightAssetSource_UsesRuntimeOpt(g->assets))
			in->base_color_emissive_strength = g->assets->opt_projectile_emissive_strength;
	}
}

/* ===== Main scene submission ======================================== */

/* Submit the mesh-bearing static objects (nav buoys, mines, probes,
 * satellites, ...) through the same TieFlightRenderer_BuildInstance path the flights
 * loop uses, species-sorted so they extend the scene walk's sticky
 * per-mesh bind. Statics live in a separate snapshot pool and carry no
 * genus, components, camo or articulation, so each is synthesized into
 * a degenerate TieFlightObjectState: rigid pose, identity mesh table,
 * decal_color 0, camera-only motion blur (is_static). */
static void TieFlightRenderer_SubmitStatics(TieFlightRenderer* g, const TieSnapshot* curr,
											const float view_proj[16], const TieFlightFrustumPlanes* fr,
											const TieFlightMotionBlurPrevious* mb) {
	const bool shadows_active = g->shadows.enabled;
	const uint32_t shadow_only_bit = 0x8000u;
	uint32_t skeys[TIE_MAX_STATIC_OBJECTS];
	uint32_t skey_count = 0;
	for (uint16_t i = 0; i < curr->static_count; ++i) {
		const TieStaticObjectState* so = &curr->statics[i];
		uint16_t species_idx = 0;
		if (!TieFlightRenderer_SceneStaticMeshEligible(so, &species_idx))
			continue;

		const TieFlightSpeciesSceneShip* s = &g->scene_ships[species_idx];
		if (!TieFlightRenderer_SceneSpeciesReady(g, species_idx))
			continue;
		float position_local[3];
		AeronWorld_LocalI32(curr->camera.world_pos, so->world_pos, position_local);
		const bool shadow_only = TieFlightRenderer_SphereOutsideFrustum(
			fr, position_local, s->mesh->bound_radius * AERON_OPT_UNITS_PER_METER);
		if (shadow_only && !shadows_active)
			continue;
		skeys[skey_count++] =
			((uint32_t)species_idx << 16) | (shadow_only ? shadow_only_bit : 0u) | (uint32_t)i;
	}
	if (skey_count == 0)
		return;
	qsort(skeys, skey_count, sizeof skeys[0], TieFlightRenderer_CmpDrawKey);

	for (uint32_t k = 0; k < skey_count; ++k) {
		const bool shadow_only = (skeys[k] & shadow_only_bit) != 0;
		const uint16_t i = (uint16_t)(skeys[k] & 0x7FFFu);
		const uint16_t species_idx = (uint16_t)(skeys[k] >> 16);
		const TieStaticObjectState* so = &curr->statics[i];
		const TieFlightSpeciesSceneShip* s = &g->scene_ships[species_idx];

		/* Degenerate flight record: pose only. genus picks a non-
		 * projectile, non-debris path; everything else zero. */
		TieFlightObjectState fl = { 0 };
		fl.genus = TIE_GENUS_UTILITY;
		fl.ship_idx = (uint8_t)species_idx;
		fl.slot = so->slot;
		memcpy(fl.world_pos, so->world_pos, sizeof fl.world_pos);
		memcpy(fl.ori, so->ori, sizeof fl.ori);

		AeronSceneMeshInstance in;
		TieFlightRenderer_BuildInstance(g, &in, s, &fl, view_proj, (const float[3]) { 0.0f, 0.0f, 0.0f },
										curr->camera.world_pos, mb, /*curr_index=*/0, /*is_static=*/true,
										/*table=*/NULL);
		if (shadow_only)
			AeronScene_AddShadowCaster(g->scene, &in);
		else
			AeronScene_AddMeshInstance(g->scene, &in);
	}
}

void TieFlightRenderer_SceneSubmit(TieFlightRenderer* g, const TieSnapshot* curr, const float view_proj[16],
								   const float cam_pos[3], const TieFlightMotionBlurPrevious* mb) {
	if (!g || !g->scene || !curr)
		return;
	if (!g->scene_model_backend)
		return;

	TieFlightFrustumPlanes fr;
	TieFlightRenderer_BuildFrustumPlanes(&fr, view_proj);

	const bool shadows_active = g->shadows.enabled;
	const uint32_t shadow_only_bit = 0x8000u;

	/* Sort by species_idx so the scene walk's per-mesh binds stay sticky. */
	uint32_t keys[TIE_MAX_FLIGHT_OBJECTS];
	uint32_t key_count = 0;
	for (uint16_t i = 0; i < curr->flight_count; ++i) {
		const TieFlightObjectState* fl = &curr->flights[i];
		uint16_t species_idx = 0;
		if (!TieFlightRenderer_SceneMeshEligible(fl, &species_idx))
			continue;

		const TieFlightSpeciesSceneShip* s = &g->scene_ships[species_idx];
		if (!TieFlightRenderer_SceneSpeciesReady(g, species_idx))
			continue;
		const bool camera_hidden = TieFlightRenderer_SceneMeshCameraHidden(fl, curr);
		float position_local[3];
		AeronWorld_LocalI32(curr->camera.world_pos, fl->world_pos, position_local);
		const bool outside_frustum = TieFlightRenderer_SphereOutsideFrustum(
			&fr, position_local, s->mesh->bound_radius * AERON_OPT_UNITS_PER_METER);
		const bool projectile =
			fl->genus == TIE_GENUS_PROJECTILE_NPC || fl->genus == TIE_GENUS_PROJECTILE_PLAYER;
		const bool shadow_only = camera_hidden || outside_frustum;
		if (shadow_only && (!shadows_active || projectile))
			continue;
		keys[key_count++] =
			((uint32_t)species_idx << 16) | (shadow_only ? shadow_only_bit : 0u) | (uint32_t)i;
	}
	qsort(keys, key_count, sizeof keys[0], TieFlightRenderer_CmpDrawKey);

	int table_pool_used = 0;

	for (uint32_t k = 0; k < key_count; ++k) {
		const bool shadow_only = (keys[k] & shadow_only_bit) != 0;
		const uint16_t i = (uint16_t)(keys[k] & 0x7FFFu);
		const TieFlightObjectState* fl = &curr->flights[i];
		const uint16_t species_idx = (uint16_t)(keys[k] >> 16);
		const TieFlightSpeciesSceneShip* s = &g->scene_ships[species_idx];

		/* Mesh table: build only when the craft differs from the
		 * identity default (hidden components / articulation / debris);
		 * NULL lets the scene push its cached identity. */
		const AeronSceneMeshTable* table = NULL;
		if (TieFlightRenderer_CraftNeedsCustomMeshTable(s, fl, curr) &&
			table_pool_used < TIE_FLIGHT_SCENE_MAX_INSTANCES) {
			AeronSceneMeshTable* mtu = &g->hd_scene_tables[table_pool_used++];
			TieFlightMesh_BuildmeshTable(s->mesh->mesh_rot, fl, curr, mtu);
			table = mtu;
		}

		AeronSceneMeshInstance in;
		TieFlightRenderer_BuildInstance(g, &in, s, fl, view_proj, cam_pos, curr->camera.world_pos, mb, i,
										/*is_static=*/false, table);
		if (shadow_only)
			AeronScene_AddShadowCaster(g->scene, &in);
		else
			AeronScene_AddMeshInstance(g->scene, &in);
	}

	TieFlightRenderer_SubmitStatics(g, curr, view_proj, &fr, mb);
}

bool TieFlightRenderer_SceneAddPipInstance(TieFlightRenderer* g, AeronScene3D* scene, const TieSnapshot* snap,
										   const TieFlightObjectState* fl, const float view_proj[16],
										   const float cam_pos[3]) {
	if (!g || !scene || !snap || !fl)
		return false;
	if (!g->scene_model_backend)
		return false;
	if (fl->ship_idx >= TIE_FLIGHT_MAX_SPECIES)
		return false;

	const TieFlightSpeciesSceneShip* s = &g->scene_ships[fl->ship_idx];
	if (!TieFlightRenderer_SceneSpeciesReady(g, fl->ship_idx))
		return false;
	if (s->mesh->index_count == 0) {
		Aeron_RequestFatalError("Flight Asset Error", "prepared PIP scene mesh has no triangles");
		return false;
	}

	/* PIP draws exactly one craft — always build the per-craft mesh
	 * table (the identity shortcut only pays off across a walk). */
	TieFlightMesh_BuildmeshTable(s->mesh->mesh_rot, fl, snap, &g->pip_scene_table);

	AeronSceneMeshInstance in;
	TieFlightRenderer_BuildInstance(g, &in, s, fl, view_proj, cam_pos, fl->world_pos,
									/*mb=*/NULL, /*curr_index=*/0, /*is_static=*/false, &g->pip_scene_table);
	AeronScene_AddMeshInstance(scene, &in);
	return true;
}
