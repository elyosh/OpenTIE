#include "tie/collide_opt.h"
#include "tie/fview.h"
#include "tie/gate.h"
#include "tie/modelmesh.h"
#include "tie/tie.h"

#include <math.h>
#include <string.h>

// GLOBAL: TIE98 0x589C70
static TieModelVec3f g_opt_collision_segment_start;
// GLOBAL: TIE98 0x589C80
static TieModelVec3f g_opt_collision_segment_end;
// GLOBAL: TIE98 0x589C8C
static int g_opt_collision_hit_mesh_one_based;
// GLOBAL: TIE98 0x589C90
static TieModelVec3f g_opt_collision_segment_start_saved;
// GLOBAL: TIE98 0x589CA0
static TieModelVec3f g_opt_collision_segment_end_saved;
// GLOBAL: TIE98 0x589CAC
static int g_opt_collision_current_mesh_one_based;
// GLOBAL: TIE98 0x589CB0
static float g_opt_collision_mesh_rotation_radians;
// GLOBAL: TIE98 0x589CB4
static float g_opt_collision_nearest_fraction;
/* PORT: the original keeps the last vertex-set node; the host collision
 * view keeps the vertex set it resolves to, NULL when none or invalid. */
// GLOBAL: TIE98 0x589CB8
static const TieModelCollisionVertexSet* g_opt_collision_bounds_node;

static int collide_testsweepagainstoptnode(const TieFlightModelView* model, int node_index);
static int collide_intersectsegmentwithfaceplane(const TieModelVec3f* normal,
												 const TieModelVec3f* face_vertex, const TieModelVec3f* start,
												 const TieModelVec3f* end, float* out_fraction);
static int collide_pointinfacepolygon(const TieModelVec3f* normal, const TieModelVec3f* vertices,
									  const int32_t indices[4], TieModelVec3f* point);

// FUNCTION: TIE98 0x485E30
// COLLIDE_checksweptmodelcollision; OpenXWA counterpart
// collide_CheckSweptModelCollision.
uint16_t collide_checksweptmodelcollision(uint16_t source_object_index, uint16_t target_object_index) {
	FlightObject* target = &objects[target_object_index];
	int32_t current[3], previous[3];
	TieModelVec3f local_start, local_end;
	const TieFlightModelView* model;
	uint16_t mesh_index;

	craftptr = target->craft_ptr;
	if (target->orient_dirty) {
		fview_calcrotatemove(target->pitch, target->heading, target);
		fview_calcrotateorient(target->roll, 0, target);
	}

	current[0] = laserx - target->world_x;
	current[1] = lasery - target->world_y;
	current[2] = laserz - target->world_z;
	previous[0] = laserxold - target->world_x;
	previous[1] = laseryold - target->world_y;
	previous[2] = laserzold - target->world_z;
	local_end.x =
		(float)(((int64_t)current[0] * target->side_x >> 15) + ((int64_t)current[1] * target->side_y >> 15) +
				((int64_t)current[2] * target->side_z >> 15));
	local_end.y =
		(float)-(((int64_t)current[0] * target->fwd_x >> 15) + ((int64_t)current[1] * target->fwd_y >> 15) +
				 ((int64_t)current[2] * target->fwd_z >> 15));
	local_end.z =
		(float)(((int64_t)current[0] * target->up_x >> 15) + ((int64_t)current[1] * target->up_y >> 15) +
				((int64_t)current[2] * target->up_z >> 15));
	local_start.x = (float)(((int64_t)previous[0] * target->side_x >> 15) +
							((int64_t)previous[1] * target->side_y >> 15) +
							((int64_t)previous[2] * target->side_z >> 15));
	local_start.y =
		(float)-(((int64_t)previous[0] * target->fwd_x >> 15) + ((int64_t)previous[1] * target->fwd_y >> 15) +
				 ((int64_t)previous[2] * target->fwd_z >> 15));
	local_start.z =
		(float)(((int64_t)previous[0] * target->up_x >> 15) + ((int64_t)previous[1] * target->up_y >> 15) +
				((int64_t)previous[2] * target->up_z >> 15));

	model = modelmesh_require_model(target->ship_idx);
	g_opt_collision_segment_start_saved = local_start;
	g_opt_collision_segment_end_saved = local_end;
	g_opt_collision_nearest_fraction = 2.0f;
	g_opt_collision_hit_mesh_one_based = 0;
	g_opt_collision_bounds_node = NULL;
	for (mesh_index = 0; mesh_index < model->mesh_count; ++mesh_index) {
		int mesh_type;
		uint8_t rotation;
		const TieModelMeshView* mesh;

		g_opt_collision_current_mesh_one_based = mesh_index + 1;
		g_opt_collision_mesh_rotation_radians = 0.0f;
		if (craftptr->mesh_component_hp[mesh_index] == 0)
			continue;
		mesh_type = modelmesh_gettype(target->ship_idx, mesh_index);
		if (source_object_index == target_object_index &&
			(mesh_type == TIE_MESH_GUN_TURRET || mesh_type == TIE_MESH_SMALL_GUN ||
			 mesh_type == TIE_MESH_ROTARY_GUN_TURRET))
			continue;

		rotation = craftptr->mesh_rotation[mesh_index];
		if (rotation)
			g_opt_collision_mesh_rotation_radians = rotation * 0.024543693f;
		mesh = &model->meshes[mesh_index];
		if (!rotation && mesh->has_descriptor) {
			if ((local_start.x < mesh->bounds.min.x && local_end.x < mesh->bounds.min.x) ||
				(local_start.y < mesh->bounds.min.y && local_end.y < mesh->bounds.min.y) ||
				(local_start.z < mesh->bounds.min.z && local_end.z < mesh->bounds.min.z) ||
				(local_start.x > mesh->bounds.max.x && local_end.x > mesh->bounds.max.x) ||
				(local_start.y > mesh->bounds.max.y && local_end.y > mesh->bounds.max.y) ||
				(local_start.z > mesh->bounds.max.z && local_end.z > mesh->bounds.max.z))
				continue;
		}

		if (mission.train_craft_type && source_object_index == pstate.object_idx &&
			mesh_type == TIE_MESH_MAIN_HULL && (target->ship_idx == 98 || target->ship_idx == 99))
			gate_setrenderreferenceobject(target_object_index);
		g_opt_collision_segment_start = g_opt_collision_segment_start_saved;
		g_opt_collision_segment_end = g_opt_collision_segment_end_saved;
		g_opt_collision_bounds_node = NULL;
		collide_testsweepagainstoptnode(model, mesh->collision_root);
	}
	if (g_opt_collision_hit_mesh_one_based) {
		g_opt_collision_nearest_fraction -= 0.1f;
		if (g_opt_collision_nearest_fraction < 0.0f)
			g_opt_collision_nearest_fraction = 0.0f;
		collidexoff = (int32_t)((float)(laserx - laserxold) * g_opt_collision_nearest_fraction);
		collideyoff = (int32_t)((float)(lasery - laseryold) * g_opt_collision_nearest_fraction);
		collidezoff = (int32_t)((float)(laserz - laserzold) * g_opt_collision_nearest_fraction);
	}
	return g_opt_collision_hit_mesh_one_based;
}

// FUNCTION: TIE98 0x486440
// COLLIDE_checksweptmodelmeshcollision (inferred)
int collide_checksweptmodelmeshcollision(int model_type, int mesh_index, int32_t start_x, int32_t start_y,
										 int32_t start_z, int32_t end_x, int32_t end_y, int32_t end_z) {
	const TieFlightModelView* model;
	unsigned int i;

	g_opt_collision_segment_start.x = (float)start_x;
	g_opt_collision_segment_start_saved.x = g_opt_collision_segment_start.x;
	g_opt_collision_segment_start.y = (float)start_y;
	g_opt_collision_segment_start_saved.y = g_opt_collision_segment_start.y;
	g_opt_collision_segment_start.z = (float)start_z;
	g_opt_collision_segment_start_saved.z = g_opt_collision_segment_start.z;
	g_opt_collision_segment_end.x = (float)end_x;
	g_opt_collision_segment_end_saved.x = g_opt_collision_segment_end.x;
	g_opt_collision_segment_end.y = (float)end_y;
	g_opt_collision_segment_end_saved.y = g_opt_collision_segment_end.y;
	g_opt_collision_segment_end.z = (float)end_z;
	g_opt_collision_segment_end_saved.z = g_opt_collision_segment_end.z;
	g_opt_collision_nearest_fraction = 2.0f;
	g_opt_collision_hit_mesh_one_based = 0;
	model = modelmesh_require_model((uint16_t)model_type);
	if (!model)
		return 0;
	g_opt_collision_current_mesh_one_based = 0;
	g_opt_collision_bounds_node = NULL;
	for (i = 0; i < model->mesh_count; ++i) {
		int collision_root;

		g_opt_collision_mesh_rotation_radians = 0.0f;
		collision_root = model->meshes[i].collision_root;
		if (g_opt_collision_current_mesh_one_based++ == mesh_index) {
			g_opt_collision_mesh_rotation_radians = 0.0f;
			collide_testsweepagainstoptnode(model, collision_root);
			g_opt_collision_segment_start = g_opt_collision_segment_start_saved;
			g_opt_collision_segment_end = g_opt_collision_segment_end_saved;
		}
	}
	if (g_opt_collision_hit_mesh_one_based) {
		g_opt_collision_nearest_fraction -= 0.1f;
		if (g_opt_collision_nearest_fraction < 0.0f)
			g_opt_collision_nearest_fraction = 0.0f;
		collidexoff = (int32_t)((float)(laserx - laserxold) * g_opt_collision_nearest_fraction);
		collideyoff = (int32_t)((float)(lasery - laseryold) * g_opt_collision_nearest_fraction);
		collidezoff = (int32_t)((float)(laserz - laserzold) * g_opt_collision_nearest_fraction);
	}
	return g_opt_collision_hit_mesh_one_based;
}

// FUNCTION: TIE98 0x486670
// COLLIDE_testsweepagainstoptnode; OpenXWA counterpart
// collide_TestSweepAgainstOptNode.
static int collide_testsweepagainstoptnode(const TieFlightModelView* model, int node_index) {
	const TieModelCollisionNode* node;
	uint16_t child;

	if (node_index < 0 || (uint32_t)node_index >= model->collision_node_count)
		return 0;
	node = &model->collision_nodes[node_index];
	if (node->kind == 7)
		return collide_testsweepagainstoptnode(model, node->reference_target);

	if (node->kind == 23 && g_opt_collision_mesh_rotation_radians != 0.0f) {
		int rotation_mesh = node->rotation_index;
		if (rotation_mesh < 0)
			rotation_mesh = g_opt_collision_current_mesh_one_based - 1;
		if ((uint16_t)rotation_mesh < model->mesh_count) {
			const TieModelMeshView* mesh = &model->meshes[rotation_mesh];
			if (mesh->has_rotation_scale) {
				/* Rotate both swept-segment endpoints around the payload
				 * pivot/axis by the current mesh rotation. */
				const TieModelRotationScale* rotation = &mesh->rotation_scale;
				const float inverse_q15 = 1.0f / 32768.0f;
				const float axis_x = rotation->rotation_axis.x * inverse_q15;
				const float axis_y = rotation->rotation_axis.y * inverse_q15;
				const float axis_z = rotation->rotation_axis.z * inverse_q15;
				const float cosine = cosf(g_opt_collision_mesh_rotation_radians);
				const float sine = sinf(g_opt_collision_mesh_rotation_radians);
				const float one_minus_cosine = 1.0f - cosine;
				TieModelVec3f* point;
				int endpoint;
				for (endpoint = 0; endpoint < 2; ++endpoint) {
					float x, y, z, dot;
					point = endpoint == 0 ? &g_opt_collision_segment_start : &g_opt_collision_segment_end;
					x = point->x - rotation->pivot.x;
					y = point->y - rotation->pivot.y;
					z = point->z - rotation->pivot.z;
					dot = axis_x * x + axis_y * y + axis_z * z;
					point->x = rotation->pivot.x + x * cosine + (axis_y * z - axis_z * y) * sine +
							   axis_x * dot * one_minus_cosine;
					point->y = rotation->pivot.y + y * cosine + (axis_z * x - axis_x * z) * sine +
							   axis_y * dot * one_minus_cosine;
					point->z = rotation->pivot.z + z * cosine + (axis_x * y - axis_y * x) * sine +
							   axis_z * dot * one_minus_cosine;
				}
			}
		}
		g_opt_collision_mesh_rotation_radians = 0.0f;
	} else if (node->kind == 3) {
		g_opt_collision_bounds_node =
			node->vertex_set >= 0 && (uint32_t)node->vertex_set < model->collision_vertex_set_count
				? &model->collision_vertex_sets[node->vertex_set]
				: NULL;
		if (g_opt_collision_mesh_rotation_radians == 0.0f && g_opt_collision_bounds_node) {
			const TieModelBounds* bounds = &g_opt_collision_bounds_node->bounds;
			if ((g_opt_collision_segment_start.x < bounds->min.x &&
				 g_opt_collision_segment_end.x < bounds->min.x) ||
				(g_opt_collision_segment_start.y < bounds->min.y &&
				 g_opt_collision_segment_end.y < bounds->min.y) ||
				(g_opt_collision_segment_start.z < bounds->min.z &&
				 g_opt_collision_segment_end.z < bounds->min.z) ||
				(g_opt_collision_segment_start.x > bounds->max.x &&
				 g_opt_collision_segment_end.x > bounds->max.x) ||
				(g_opt_collision_segment_start.y > bounds->max.y &&
				 g_opt_collision_segment_end.y > bounds->max.y) ||
				(g_opt_collision_segment_start.z > bounds->max.z &&
				 g_opt_collision_segment_end.z > bounds->max.z))
				return 1;
		}
	} else if ((node->kind == 1 || node->kind == 15 || node->kind == 16 || node->kind == 17) &&
			   g_opt_collision_bounds_node) {
		const TieModelCollisionVertexSet* set = g_opt_collision_bounds_node;
		uint32_t i;
		for (i = 0; i < node->face_count; ++i) {
			const uint32_t face_index = node->first_face + i;
			const TieModelCollisionFace* face;
			float fraction;
			TieModelVec3f point;
			if (face_index >= model->collision_face_count)
				break;
			face = &model->collision_faces[face_index];
			if (face->vertex_indices[0] < 0 || (uint32_t)face->vertex_indices[0] >= set->vertex_count ||
				face->vertex_indices[1] < 0 || (uint32_t)face->vertex_indices[1] >= set->vertex_count ||
				face->vertex_indices[2] < 0 || (uint32_t)face->vertex_indices[2] >= set->vertex_count ||
				(face->vertex_indices[3] != -1 &&
				 (face->vertex_indices[3] < 0 || (uint32_t)face->vertex_indices[3] >= set->vertex_count)))
				continue;
			if (!collide_intersectsegmentwithfaceplane(&face->normal, &set->vertices[face->vertex_indices[0]],
													   &g_opt_collision_segment_start,
													   &g_opt_collision_segment_end, &fraction) ||
				fraction >= g_opt_collision_nearest_fraction)
				continue;
			point.x = g_opt_collision_segment_start.x +
					  (g_opt_collision_segment_end.x - g_opt_collision_segment_start.x) * fraction;
			point.y = g_opt_collision_segment_start.y +
					  (g_opt_collision_segment_end.y - g_opt_collision_segment_start.y) * fraction;
			point.z = g_opt_collision_segment_start.z +
					  (g_opt_collision_segment_end.z - g_opt_collision_segment_start.z) * fraction;
			if (collide_pointinfacepolygon(&face->normal, set->vertices, face->vertex_indices, &point)) {
				g_opt_collision_nearest_fraction = fraction;
				g_opt_collision_hit_mesh_one_based = g_opt_collision_current_mesh_one_based;
			}
		}
	}

	if (node->child_count == 0)
		return 0;
	if (node->kind == 21) {
		return collide_testsweepagainstoptnode(model, node->first_child);
	}
	for (child = 0; child < node->child_count; ++child) {
		if (collide_testsweepagainstoptnode(model, node->first_child + child))
			return 1;
	}
	return 0;
}

// FUNCTION: TIE98 0x486B70
// COLLIDE_intersectsegmentwithfaceplane; OpenXWA counterpart
// collide_IntersectSegmentWithFacePlane.
static int collide_intersectsegmentwithfaceplane(const TieModelVec3f* normal,
												 const TieModelVec3f* face_vertex, const TieModelVec3f* start,
												 const TieModelVec3f* end, float* out_fraction) {
	float start_distance = (start->x - face_vertex->x) * normal->x + (start->y - face_vertex->y) * normal->y +
						   (start->z - face_vertex->z) * normal->z;
	float end_distance = (end->x - face_vertex->x) * normal->x + (end->y - face_vertex->y) * normal->y +
						 (end->z - face_vertex->z) * normal->z;
	if (start_distance < 10.0f && start_distance > -10.0f)
		start_distance = 0.0f;
	if (end_distance < 10.0f && end_distance > -10.0f)
		end_distance = 0.0f;
	if (start_distance == 0.0f) {
		*out_fraction = 0.0f;
		return 1;
	}
	if (end_distance == 0.0f) {
		*out_fraction = 1.0f;
		return 1;
	}
	if (start_distance < 0.0f && end_distance > 0.0f) {
		*out_fraction = start_distance / (end_distance - start_distance);
		if (*out_fraction < 0.0f)
			*out_fraction = -*out_fraction;
		return 1;
	}
	if (end_distance < 0.0f && start_distance > 0.0f) {
		*out_fraction = start_distance / (start_distance - end_distance);
		if (*out_fraction < 0.0f)
			*out_fraction = -*out_fraction;
		return 1;
	}
	return 0;
}

// FUNCTION: TIE98 0x486D20
// COLLIDE_pointinfacepolygon; OpenXWA counterpart collide_PointInFacePolygon.
static int collide_pointinfacepolygon(const TieModelVec3f* normal, const TieModelVec3f* vertices,
									  const int32_t indices[4], TieModelVec3f* point) {
	const float* coords = (const float*)vertices;
	float abs_x = normal->x;
	float abs_y = normal->y;
	float abs_z = normal->z;
	int axis_u;
	int axis_v;
	int negative;
	float cross;
	float prev_u;
	float prev_v;
	float next_u;
	float next_v;

	if (abs_x < 0.0f)
		abs_x = -abs_x;
	if (abs_y < 0.0f)
		abs_y = -abs_y;
	if (abs_z < 0.0f)
		abs_z = -abs_z;
	if (abs_z >= abs_y && abs_z >= abs_x) {
		axis_u = 0;
		axis_v = 1;
		point->z = point->y;
		point->y = point->x;
	} else if (abs_y >= abs_x && abs_y >= abs_z) {
		axis_u = 0;
		axis_v = 2;
		point->y = point->x;
	} else {
		axis_u = 1;
		axis_v = 2;
	}

	prev_u = coords[indices[0] * 3 + axis_u];
	prev_v = coords[indices[0] * 3 + axis_v];
	next_u = coords[indices[1] * 3 + axis_u];
	next_v = coords[indices[1] * 3 + axis_v];
	cross = (point->y - prev_u) * (next_v - prev_v) - (point->z - prev_v) * (next_u - prev_u);
	negative = cross < 0.0f;

	prev_u = next_u;
	prev_v = next_v;
	next_u = coords[indices[2] * 3 + axis_u];
	next_v = coords[indices[2] * 3 + axis_v];
	cross = (point->y - prev_u) * (next_v - prev_v) - (point->z - prev_v) * (next_u - prev_u);
	if (cross < 0.0f && !negative)
		return 0;
	if (cross >= 0.0f && negative)
		return 0;

	if (indices[3] != -1) {
		prev_u = next_u;
		prev_v = next_v;
		next_u = coords[indices[3] * 3 + axis_u];
		next_v = coords[indices[3] * 3 + axis_v];
		cross = (point->y - prev_u) * (next_v - prev_v) - (point->z - prev_v) * (next_u - prev_u);
		if (cross < 0.0f && !negative)
			return 0;
		if (cross >= 0.0f && negative)
			return 0;
	}

	prev_u = next_u;
	prev_v = next_v;
	next_u = coords[indices[0] * 3 + axis_u];
	next_v = coords[indices[0] * 3 + axis_v];
	cross = (point->y - prev_u) * (next_v - prev_v) - (point->z - prev_v) * (next_u - prev_u);
	if (cross < 0.0f && !negative)
		return 0;
	if (cross >= 0.0f && negative)
		return 0;
	return 1;
}
