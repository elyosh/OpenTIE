/* Mesh-table and transform helpers shared by the classic and scene-model
 * draw paths. */

#include "tie_remaster/flight/mesh_common.h"

#include "tie_remaster/flight/pbr.h"
#include "tie_remaster/flight/render_math.h"

#include "aeron/numeric.h"
#include "tie_runtime/snapshot/snapshot.h"

#include <stdlib.h>
#include <string.h>

static void TieFlightRenderer_AffineMultiply(float out[3][4], const float left[3][4],
											 const float right[3][4]) {
	float result[3][4];
	for (int row = 0; row < 3; ++row) {
		for (int column = 0; column < 3; ++column) {
			result[row][column] = left[row][0] * right[0][column] + left[row][1] * right[1][column] +
								  left[row][2] * right[2][column];
		}
		result[row][3] = left[row][0] * right[0][3] + left[row][1] * right[1][3] +
						 left[row][2] * right[2][3] + left[row][3];
	}
	memcpy(out, result, sizeof result);
}

static void TieFlightRenderer_ApplyTie98BwingBridge(const AeronMeshRot* mesh_rot,
													const TieFlightObjectState* flight,
													const TieSnapshot* snapshot, AeronSceneMeshTable* table) {
	if (flight->ship_idx != TIE_SPECIES_B_WING || !snapshot->flight_components)
		return;
	int bridge = -1;
	for (int mesh = 0; mesh < AERON_MAX_MESH_SLOTS; ++mesh) {
		if (mesh_rot[mesh].mesh_type == 7) {
			bridge = mesh;
			break;
		}
	}
	if (bridge < 0)
		return;
	int16_t rotation = 0;
	for (uint32_t index = 0; index < flight->component_count; ++index) {
		const TieFlightObjectComponent* component =
			&snapshot->flight_components[flight->component_start + index];
		if (component->mesh_idx == bridge && (component->flags & 1)) {
			rotation = component->rotation_angle;
			break;
		}
	}
	if (!rotation)
		return;
	const float axis[3] = { 0.0f, -1.0f, 0.0f };
	const float pivot[3] = { 0.0f, 0.0f, 0.0f };
	float parent[3][4];
	TieRenderMath_Mat3x4RotationAboutPivot(
		parent, axis, pivot, (float)(uint16_t)rotation * (2.0f * 3.14159265358979323846f / 65536.0f));
	for (int mesh = 0; mesh < AERON_MAX_MESH_SLOTS; ++mesh)
		TieFlightRenderer_AffineMultiply(table->rows[mesh], parent, table->rows[mesh]);
}

void TieFlightMesh_BuildmeshTable(const AeronMeshRot* mesh_rot, const TieFlightObjectState* fl,
								  const TieSnapshot* curr, AeronSceneMeshTable* mtu) {
	for (uint32_t mi = 0; mi < AERON_MAX_MESH_SLOTS; ++mi)
		TieRenderMath_Mat3x4Identity(mtu->rows[mi]);
	for (uint32_t mi = 0; mi < AERON_MAX_MESH_SLOTS; ++mi) {
		mtu->visibility_packed[mi >> 2][mi & 3] = 1.0f;
		mtu->highlight_packed[mi >> 2][mi & 3] = 0.0f;
		mtu->markings_packed[mi >> 2][mi & 3] = 0.0f;
		mtu->emissive_packed[mi >> 2][mi & 3] = 1.0f;
	}

	/* Debris: hide every mesh of the parent species and re-enable only
	 * the one submesh that detached. Matches the classic mesh table below.
	 * `submesh_idx` is in engine mesh-index space; translate through
	 * the same permutation the component loop below uses so the glTF
	 * per-vertex `mesh_index` (opt-space) lookup hits the right slot. */
	if (fl->genus == TIE_GENUS_DEBRIS) {
		for (uint32_t mi = 0; mi < AERON_MAX_MESH_SLOTS; ++mi)
			mtu->visibility_packed[mi >> 2][mi & 3] = 0.0f;
		if (fl->submesh_idx < AERON_MAX_MESH_SLOTS)
			mtu->visibility_packed[fl->submesh_idx >> 2][fl->submesh_idx & 3] = 1.0f;
	}

	if (fl->component_count == 0 || !curr->flight_components)
		return;

	const uint32_t cs = fl->component_start;
	const uint32_t cc = fl->component_count;
	for (uint32_t ci = 0; ci < cc; ++ci) {
		const TieFlightObjectComponent* c = &curr->flight_components[cs + ci];

		/* TIE98 state and the common model share component ordinals. */
		const uint32_t opt_mi = c->mesh_idx;
		if (opt_mi >= AERON_MAX_MESH_SLOTS)
			continue;

		/* component flag bit 0 = visible; clearing it hides this mesh. */
		if (!(c->flags & 0x1u))
			mtu->visibility_packed[opt_mi >> 2][opt_mi & 3] = 0.0f;

		/* component flag bit 1 = articulated; if not set, no rotation. */
		if (!(c->flags & 0x2u))
			continue;
		const AeronMeshRot* r = &mesh_rot[opt_mi];
		if (!r->has_rotation)
			continue;

		/* Snapshot angle uses the u16 → 0..2π BAM convention. */
		const float angle = (float)(uint16_t)c->rotation_angle * (2.0f * 3.14159265358979323846f / 65536.0f);
		TieRenderMath_Mat3x4RotationAboutPivot(mtu->rows[opt_mi], r->axis, r->pivot, angle);
	}
	TieFlightRenderer_ApplyTie98BwingBridge(mesh_rot, fl, curr, mtu);
}

void TieFlightMesh_BuildclassicMeshTable(const TieFlightSpeciesMesh* mesh, const TieFlightObjectState* flight,
										 const TieSnapshot* snapshot, TieFlightMeshTablePurpose purpose,
										 AeronSceneMeshTable* out) {
	const bool markings = snapshot->drawmarkingsflag != 0;
	const uint16_t species = flight->genus == TIE_GENUS_DEBRIS ? flight->parent_ship_idx : flight->ship_idx;
	for (uint32_t i = 0; i < AERON_MAX_MESH_SLOTS; ++i) {
		TieRenderMath_Mat3x4Identity(out->rows[i]);
		out->visibility_packed[i >> 2][i & 3] = 1.0f;
		out->highlight_packed[i >> 2][i & 3] = 0.0f;
		uint8_t mesh_type = 0;
		if (mesh && mesh->mesh_rot && i < mesh->mesh_count)
			mesh_type = mesh->mesh_rot[i].mesh_type;
		bool draw_marking = markings;
		if (draw_marking && species == TIE_SPECIES_SHUTTLE && flight->side != 0 && mesh_type == 2)
			draw_marking = false;
		out->markings_packed[i >> 2][i & 3] = draw_marking ? 1.0f : 0.0f;
		out->emissive_packed[i >> 2][i & 3] = TieFlightMesh_EmissiveforMesh(flight->genus, mesh_type);
	}

	if (flight->genus == TIE_GENUS_DEBRIS) {
		for (uint32_t i = 0; i < AERON_MAX_MESH_SLOTS; ++i)
			out->visibility_packed[i >> 2][i & 3] = 0.0f;
		if (flight->submesh_idx < AERON_MAX_MESH_SLOTS)
			out->visibility_packed[flight->submesh_idx >> 2][flight->submesh_idx & 3] = 1.0f;
	}

	if (purpose == TIE_FLIGHT_MESH_TABLE_MAIN && flight->highlight == 1) {
		for (uint32_t i = 0; i < AERON_MAX_MESH_SLOTS; ++i)
			out->highlight_packed[i >> 2][i & 3] = 1.0f;
	}
	if (!mesh || !mesh->mesh_rot || flight->component_count == 0 || !snapshot->flight_components)
		return;

	uint32_t mesh_count = mesh->mesh_count;
	if (mesh_count > AERON_MAX_MESH_SLOTS)
		mesh_count = AERON_MAX_MESH_SLOTS;
	for (uint32_t i = 0; i < flight->component_count; ++i) {
		const TieFlightObjectComponent* component = &snapshot->flight_components[flight->component_start + i];
		uint32_t mesh_index = component->mesh_idx;
		if (mesh_index >= mesh_count)
			continue;
		if (!(component->flags & 0x1u))
			out->visibility_packed[mesh_index >> 2][mesh_index & 3] = 0.0f;
		if (component->flags & 0x4u) {
			if ((purpose == TIE_FLIGHT_MESH_TABLE_MAIN && flight->highlight == 3) ||
				(purpose == TIE_FLIGHT_MESH_TABLE_PIP && snapshot->cockpit.pip_subsys_idx != 0xFFu))
				out->highlight_packed[mesh_index >> 2][mesh_index & 3] = 2.0f;
		}
		if (!(component->flags & 0x2u))
			continue;
		const AeronMeshRot* rotation = &mesh->mesh_rot[mesh_index];
		if (!rotation->has_rotation)
			continue;
		float angle =
			(float)(uint16_t)component->rotation_angle * (2.0f * 3.14159265358979323846f / 65536.0f);
		TieRenderMath_Mat3x4RotationAboutPivot(out->rows[mesh_index], rotation->axis, rotation->pivot, angle);
	}
	if (purpose == TIE_FLIGHT_MESH_TABLE_MAIN && flight->highlight == 3) {
		for (uint32_t i = 0; i < AERON_MAX_MESH_SLOTS; ++i) {
			if (out->highlight_packed[i >> 2][i & 3] == 0.0f)
				out->highlight_packed[i >> 2][i & 3] = 3.0f;
		}
	}
}

float TieFlightMesh_EmissiveforMesh(uint8_t genus, uint8_t mesh_type) {
	(void)mesh_type;
	if (genus == TIE_GENUS_PROJECTILE_NPC || genus == TIE_GENUS_PROJECTILE_PLAYER)
		return 2.0f;
	return 1.0f;
}

void TieFlightMesh_BuildmeshVs(TieFlightMeshVertexUniforms* out, const TieFlightSpeciesMesh* sm,
							   const TieFlightObjectState* fl, const TieSnapshot* curr,
							   const float position_local[3], const float view_proj[16]) {
	memset(out, 0, sizeof *out);
	memcpy(out->view_proj, view_proj, sizeof out->view_proj);
	/* model_scale_shift == 2 mirrors transfm2_geteyecoordsS2. */
	const float craft_scale =
		TIE_CLASSIC_VERTEX_TO_WORLD_UNITS * ((sm->model_scale_shift == 2) ? 4.0f : 1.0f);
	TieRenderMath_Mat4FromQuaternionTranslation(out->craft_to_world, fl->ori, position_local, craft_scale);
	/* Engine lightflag=0 for gates (tie.c:1876). */
	out->light_local_frame = (fl->genus == TIE_GENUS_GATE) ? 1.0f : 0.0f;
	/* Engine gouraudflag (snapshot.gouraudflag): 0 or 0x40. */
	out->gouraud_enabled = (curr->gouraudflag != 0) ? 1.0f : 0.0f;
	out->directional_dir[0] = curr->directional_dir[0];
	out->directional_dir[1] = curr->directional_dir[1];
	out->directional_dir[2] = curr->directional_dir[2];
	/* world_to_craft = transpose(rotation) / scale² — the inverse of the
	 * uniformly-scaled rigid craft_to_world, used to bring world-space
	 * normals back into craft space. The [i][3] column stays 0 (memset). */
	const float inv_scale2 = 1.0f / (craft_scale * craft_scale);
	out->world_to_craft[0][0] = out->craft_to_world[0] * inv_scale2;
	out->world_to_craft[0][1] = out->craft_to_world[4] * inv_scale2;
	out->world_to_craft[0][2] = out->craft_to_world[8] * inv_scale2;
	out->world_to_craft[1][0] = out->craft_to_world[1] * inv_scale2;
	out->world_to_craft[1][1] = out->craft_to_world[5] * inv_scale2;
	out->world_to_craft[1][2] = out->craft_to_world[9] * inv_scale2;
	out->world_to_craft[2][0] = out->craft_to_world[2] * inv_scale2;
	out->world_to_craft[2][1] = out->craft_to_world[6] * inv_scale2;
	out->world_to_craft[2][2] = out->craft_to_world[10] * inv_scale2;
	out->craft_world_pos[0] = position_local[0];
	out->craft_world_pos[1] = position_local[1];
	out->craft_world_pos[2] = position_local[2];
}

const TieFlightShipModelLineLod* TieFlightMeshCommon_PickLineLod(const TieFlightSpeciesMesh* sm,
																 float eye_z) {
	if (!sm->line_lods || sm->line_lod_count == 0)
		return NULL;
	for (uint32_t i = 0; i < sm->line_lod_count; ++i) {
		if (eye_z <= sm->line_lods[i].distance_view)
			return &sm->line_lods[i];
	}
	return &sm->line_lods[sm->line_lod_count - 1];
}

float TieFlightMesh_LineFloorMultiplier(void) {
	static float cached = -1.0f;
	if (cached < 0.0f) {
		const char* env = getenv("TIE_LINE_FLOOR_MUL");
		double parsed;
		float v = 0.5f;
		if (env && env[0] && Aeron_ParseAsciiDouble(env, strlen(env), &parsed))
			v = (float)parsed;
		if (v < 0.0f)
			v = 0.0f;
		if (v > 4.0f)
			v = 4.0f;
		cached = v;
	}
	return cached;
}
