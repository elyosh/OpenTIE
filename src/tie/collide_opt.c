#include "tie/collide_opt.h"

#include "tie/fview.h"
#include "tie/gate.h"
#include "tie/modelmesh.h"
#include "tie/tie.h"

#include <math.h>
#include <string.h>

typedef struct OptCollisionContext {
	const TieFlightModelView* model;
	TieModelVec3f segment_start;
	TieModelVec3f segment_end;
	TieModelVec3f saved_start;
	TieModelVec3f saved_end;
	float nearest_fraction;
	float rotation_radians;
	int current_vertex_set;
	uint16_t current_mesh_one_based;
	uint16_t hit_mesh_one_based;
} OptCollisionContext;

// FUNCTION: TIE98 0x486B70 COLLIDE_intersectsegmentwithfaceplane; OpenXWA counterpart
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

// FUNCTION: TIE98 0x486D20 COLLIDE_pointinfacepolygon; OpenXWA counterpart collide_PointInFacePolygon.
static int collide_pointinfacepolygon(const TieModelVec3f* normal, const TieModelVec3f* vertices,
									  const int32_t indices[4], const TieModelVec3f* point) {
	const float absolute[3] = { fabsf(normal->x), fabsf(normal->y), fabsf(normal->z) };
	int axis_u;
	int axis_v;
	if (absolute[2] >= absolute[1] && absolute[2] >= absolute[0]) {
		axis_u = 0;
		axis_v = 1;
	} else if (absolute[1] >= absolute[0] && absolute[1] >= absolute[2]) {
		axis_u = 0;
		axis_v = 2;
	} else {
		axis_u = 1;
		axis_v = 2;
	}

	const float projected[3] = { point->x, point->y, point->z };
	const int vertex_count = indices[3] == -1 ? 3 : 4;
	float first_cross = 0.0f;
	for (int edge = 0; edge < vertex_count; ++edge) {
		const TieModelVec3f* a = &vertices[indices[edge]];
		const TieModelVec3f* b = &vertices[indices[(edge + 1) % vertex_count]];
		const float av[3] = { a->x, a->y, a->z };
		const float bv[3] = { b->x, b->y, b->z };
		const float cross = (projected[axis_u] - av[axis_u]) * (bv[axis_v] - av[axis_v]) -
							(projected[axis_v] - av[axis_v]) * (bv[axis_u] - av[axis_u]);
		if (edge == 0)
			first_cross = cross;
		else if ((cross < 0.0f) != (first_cross < 0.0f))
			return 0;
	}
	return 1;
}

static void rotate_point(TieModelVec3f* point, const TieModelRotationScale* rotation, float angle) {
	const float inverse_q15 = 1.0f / 32768.0f;
	const float axis_x = rotation->rotation_axis.x * inverse_q15;
	const float axis_y = rotation->rotation_axis.y * inverse_q15;
	const float axis_z = rotation->rotation_axis.z * inverse_q15;
	const float x = point->x - rotation->pivot.x;
	const float y = point->y - rotation->pivot.y;
	const float z = point->z - rotation->pivot.z;
	const float cosine = cosf(angle);
	const float sine = sinf(angle);
	const float dot = axis_x * x + axis_y * y + axis_z * z;
	const float one_minus_cosine = 1.0f - cosine;
	point->x =
		rotation->pivot.x + x * cosine + (axis_y * z - axis_z * y) * sine + axis_x * dot * one_minus_cosine;
	point->y =
		rotation->pivot.y + y * cosine + (axis_z * x - axis_x * z) * sine + axis_y * dot * one_minus_cosine;
	point->z =
		rotation->pivot.z + z * cosine + (axis_x * y - axis_y * x) * sine + axis_z * dot * one_minus_cosine;
}

static int collision_face_indices_valid(const TieModelCollisionFace* face, uint32_t vertex_count) {
	for (int i = 0; i < 3; ++i) {
		if (face->vertex_indices[i] < 0 || (uint32_t)face->vertex_indices[i] >= vertex_count)
			return 0;
	}
	return face->vertex_indices[3] == -1 ||
		   (face->vertex_indices[3] >= 0 && (uint32_t)face->vertex_indices[3] < vertex_count);
}

// FUNCTION: TIE98 0x486670 COLLIDE_testsweepagainstoptnode; OpenXWA counterpart
// collide_TestSweepAgainstOptNode.
static int collide_testsweepagainstoptnode(OptCollisionContext* context, int node_index) {
	if (node_index < 0 || (uint32_t)node_index >= context->model->collision_node_count)
		return 0;
	const TieModelCollisionNode* node = &context->model->collision_nodes[node_index];
	if (node->kind == 7)
		return collide_testsweepagainstoptnode(context, node->reference_target);

	if (node->kind == 23 && context->rotation_radians != 0.0f) {
		int rotation_mesh = node->rotation_index;
		if (rotation_mesh < 0)
			rotation_mesh = context->current_mesh_one_based - 1;
		if ((uint16_t)rotation_mesh < context->model->mesh_count) {
			const TieModelMeshView* mesh = &context->model->meshes[rotation_mesh];
			if (mesh->has_rotation_scale) {
				rotate_point(&context->segment_start, &mesh->rotation_scale, context->rotation_radians);
				rotate_point(&context->segment_end, &mesh->rotation_scale, context->rotation_radians);
			}
		}
		context->rotation_radians = 0.0f;
	} else if (node->kind == 3) {
		context->current_vertex_set = node->vertex_set;
		if (context->rotation_radians == 0.0f && node->vertex_set >= 0 &&
			(uint32_t)node->vertex_set < context->model->collision_vertex_set_count) {
			const TieModelBounds* bounds = &context->model->collision_vertex_sets[node->vertex_set].bounds;
			if ((context->segment_start.x < bounds->min.x && context->segment_end.x < bounds->min.x) ||
				(context->segment_start.y < bounds->min.y && context->segment_end.y < bounds->min.y) ||
				(context->segment_start.z < bounds->min.z && context->segment_end.z < bounds->min.z) ||
				(context->segment_start.x > bounds->max.x && context->segment_end.x > bounds->max.x) ||
				(context->segment_start.y > bounds->max.y && context->segment_end.y > bounds->max.y) ||
				(context->segment_start.z > bounds->max.z && context->segment_end.z > bounds->max.z))
				return 1;
		}
	} else if ((node->kind == 1 || node->kind == 15 || node->kind == 16 || node->kind == 17) &&
			   context->current_vertex_set >= 0 &&
			   (uint32_t)context->current_vertex_set < context->model->collision_vertex_set_count) {
		const TieModelCollisionVertexSet* set =
			&context->model->collision_vertex_sets[context->current_vertex_set];
		for (uint32_t i = 0; i < node->face_count; ++i) {
			const uint32_t face_index = node->first_face + i;
			if (face_index >= context->model->collision_face_count)
				break;
			const TieModelCollisionFace* face = &context->model->collision_faces[face_index];
			if (!collision_face_indices_valid(face, set->vertex_count))
				continue;
			float fraction;
			if (!collide_intersectsegmentwithfaceplane(&face->normal, &set->vertices[face->vertex_indices[0]],
													   &context->segment_start, &context->segment_end,
													   &fraction) ||
				fraction >= context->nearest_fraction)
				continue;
			TieModelVec3f point = {
				.x =
					context->segment_start.x + (context->segment_end.x - context->segment_start.x) * fraction,
				.y =
					context->segment_start.y + (context->segment_end.y - context->segment_start.y) * fraction,
				.z =
					context->segment_start.z + (context->segment_end.z - context->segment_start.z) * fraction,
			};
			if (collide_pointinfacepolygon(&face->normal, set->vertices, face->vertex_indices, &point)) {
				context->nearest_fraction = fraction;
				context->hit_mesh_one_based = context->current_mesh_one_based;
			}
		}
	}

	if (node->child_count == 0)
		return 0;
	if (node->kind == 21) {
		return collide_testsweepagainstoptnode(context, node->first_child);
	}
	for (uint16_t child = 0; child < node->child_count; ++child) {
		if (collide_testsweepagainstoptnode(context, node->first_child + child))
			return 1;
	}
	return 0;
}

static void set_collision_offsets(float fraction) {
	fraction -= 0.1f;
	if (fraction < 0.0f)
		fraction = 0.0f;
	collidexoff = (int32_t)((float)(laserx - laserxold) * fraction);
	collideyoff = (int32_t)((float)(lasery - laseryold) * fraction);
	collidezoff = (int32_t)((float)(laserz - laserzold) * fraction);
}

// FUNCTION: TIE98 0x485E30 COLLIDE_checksweptmodelcollision; OpenXWA counterpart
// collide_CheckSweptModelCollision.
uint16_t collide_checksweptmodelcollision(uint16_t source_object_index, uint16_t target_object_index) {
	FlightObject* target = &objects[target_object_index];
	craftptr = target->craft_ptr;
	if (target->orient_dirty) {
		fview_calcrotatemove(target->heading, target->pitch, target);
		fview_calcrotateorient(target->roll, 0, target);
	}

	const int32_t current[3] = {
		laserx - target->world_x,
		lasery - target->world_y,
		laserz - target->world_z,
	};
	const int32_t previous[3] = {
		laserxold - target->world_x,
		laseryold - target->world_y,
		laserzold - target->world_z,
	};
	const TieModelVec3f local_end = {
		.x = (float)(((int64_t)current[0] * target->side_x >> 15) +
					 ((int64_t)current[1] * target->side_y >> 15) +
					 ((int64_t)current[2] * target->side_z >> 15)),
		.y = (float)-(((int64_t)current[0] * target->fwd_x >> 15) +
					  ((int64_t)current[1] * target->fwd_y >> 15) +
					  ((int64_t)current[2] * target->fwd_z >> 15)),
		.z = (float)(((int64_t)current[0] * target->up_x >> 15) + ((int64_t)current[1] * target->up_y >> 15) +
					 ((int64_t)current[2] * target->up_z >> 15)),
	};
	const TieModelVec3f local_start = {
		.x = (float)(((int64_t)previous[0] * target->side_x >> 15) +
					 ((int64_t)previous[1] * target->side_y >> 15) +
					 ((int64_t)previous[2] * target->side_z >> 15)),
		.y = (float)-(((int64_t)previous[0] * target->fwd_x >> 15) +
					  ((int64_t)previous[1] * target->fwd_y >> 15) +
					  ((int64_t)previous[2] * target->fwd_z >> 15)),
		.z = (float)(((int64_t)previous[0] * target->up_x >> 15) +
					 ((int64_t)previous[1] * target->up_y >> 15) +
					 ((int64_t)previous[2] * target->up_z >> 15)),
	};

	OptCollisionContext context = {
		.model = modelmesh_require_model(target->ship_idx),
		.saved_start = local_start,
		.saved_end = local_end,
		.nearest_fraction = 2.0f,
		.current_vertex_set = -1,
	};
	for (uint16_t mesh_index = 0; mesh_index < context.model->mesh_count; ++mesh_index) {
		context.current_mesh_one_based = mesh_index + 1;
		context.rotation_radians = 0.0f;
		if (craftptr->mesh_component_hp[mesh_index] == 0)
			continue;
		const int mesh_type = modelmesh_gettype(target->ship_idx, mesh_index);
		if (source_object_index == target_object_index &&
			(mesh_type == TIE_MESH_GUN_TURRET || mesh_type == TIE_MESH_SMALL_GUN ||
			 mesh_type == TIE_MESH_ROTARY_GUN_TURRET))
			continue;

		const uint8_t rotation = craftptr->mesh_rotation[mesh_index];
		if (rotation)
			context.rotation_radians = rotation * 0.024543693f;
		const TieModelMeshView* mesh = &context.model->meshes[mesh_index];
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
		context.segment_start = context.saved_start;
		context.segment_end = context.saved_end;
		context.current_vertex_set = -1;
		collide_testsweepagainstoptnode(&context, mesh->collision_root);
	}
	if (context.hit_mesh_one_based)
		set_collision_offsets(context.nearest_fraction);
	return context.hit_mesh_one_based;
}

// FUNCTION: TIE98 0x486440 COLLIDE_checksweptmodelmeshcollision (inferred)
uint16_t collide_checksweptmodelmeshcollision(uint8_t model_type, uint16_t mesh_index, int32_t start_x,
											  int32_t start_y, int32_t start_z, int32_t end_x, int32_t end_y,
											  int32_t end_z) {
	const TieFlightModelView* model = modelmesh_require_model(model_type);
	OptCollisionContext context = {
		.model = model,
		.segment_start = { (float)start_x, (float)start_y, (float)start_z },
		.segment_end = { (float)end_x, (float)end_y, (float)end_z },
		.nearest_fraction = 2.0f,
		.current_vertex_set = -1,
		.current_mesh_one_based = mesh_index + 1,
	};
	context.saved_start = context.segment_start;
	context.saved_end = context.segment_end;
	if (mesh_index < model->mesh_count)
		collide_testsweepagainstoptnode(&context, model->meshes[mesh_index].collision_root);
	if (context.hit_mesh_one_based)
		set_collision_offsets(context.nearest_fraction);
	return context.hit_mesh_one_based;
}
