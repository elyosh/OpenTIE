#include "tie/modelmesh.h"
#include "tie_runtime/flight_assets/native_opt.h"
#include "tie_runtime/flight_assets/service.h"
#include "tie_runtime/runtime/wide_arithmetic.h"

#include "tie/shell.h"
#include "tie/tie.h"
#include "tie/trig2.h"

#include <limits.h>
#include <stdio.h>

// FUNCTION: TIE98 0x423FC0
// ModelMesh_ApplyAnimatedMeshRotationToPoint; same name in OpenXWA.
void modelmesh_applyanimatedmeshrotationtopoint(int angle, uint16_t model_type, int mesh_index, int x, int y,
												int z) {
	const TieModelRotationScale* rotation;
	int32_t ax, ay, az;
	int32_t cosine, sine;
	int32_t versine;
	int32_t rx, ry, rz;
	int32_t m00, m01, m02, m10, m11, m12, m20, m21, m22;
	int32_t value;

	rotatedx = x;
	rotatedy = y;
	rotatedz = z;
	rotation = modelmesh_getrotscaledata(model_type, mesh_index);
	if (!rotation)
		return;

	ax = (int32_t)rotation->rotation_axis.x;
	ay = (int32_t)rotation->rotation_axis.y;
	az = (int32_t)rotation->rotation_axis.z;
	cosine = trig2_getsignedcos(angle);
	sine = trig2_getsignedsin(angle);
	if (cosine >= 0) {
		int32_t term;

		versine = 0x7FFF - cosine;
		value = (int32_t)((uint32_t)(((ax * ax) >> 15) * versine) + ((uint32_t)cosine << 15));
		if (value >= 0x40000000)
			value = 0x3FFFFFFF;
		if (value <= -0x40000000)
			value = -0x3FFF0000;
		m00 = value >> 15;
		term = math2_mul_q15(sine, az);
		value = (int32_t)((uint32_t)(((ax * ay) >> 15) * versine) + ((uint32_t)term << 15));
		if (value >= 0x40000000)
			value = 0x3FFFFFFF;
		if (value <= -0x40000000)
			value = -0x3FFF0000;
		m01 = value >> 15;
		term = -math2_mul_q15(sine, ay);
		value = (int32_t)((uint32_t)(((ax * az) >> 15) * versine) + ((uint32_t)term << 15));
		if (value >= 0x40000000)
			value = 0x3FFFFFFF;
		if (value <= -0x40000000)
			value = -0x3FFF0000;
		m02 = value >> 15;
		term = -math2_mul_q15(sine, az);
		value = (int32_t)((uint32_t)(((ax * ay) >> 15) * versine) + ((uint32_t)term << 15));
		if (value >= 0x40000000)
			value = 0x3FFFFFFF;
		if (value <= -0x40000000)
			value = -0x3FFF0000;
		m10 = value >> 15;
		value = (int32_t)((uint32_t)(((ay * ay) >> 15) * versine) + ((uint32_t)cosine << 15));
		if (value >= 0x40000000)
			value = 0x3FFFFFFF;
		if (value <= -0x40000000)
			value = -0x3FFF0000;
		m11 = value >> 15;
		term = math2_mul_q15(sine, ax);
		value = (int32_t)((uint32_t)(((ay * az) >> 15) * versine) + ((uint32_t)term << 15));
		if (value >= 0x40000000)
			value = 0x3FFFFFFF;
		if (value <= -0x40000000)
			value = -0x3FFF0000;
		m12 = value >> 15;
		term = math2_mul_q15(sine, ay);
		value = (int32_t)((uint32_t)(((ax * az) >> 15) * versine) + ((uint32_t)term << 15));
		if (value >= 0x40000000)
			value = 0x3FFFFFFF;
		if (value <= -0x40000000)
			value = -0x3FFF0000;
		m20 = value >> 15;
		term = -math2_mul_q15(sine, ax);
		value = (int32_t)((uint32_t)(((ay * az) >> 15) * versine) + ((uint32_t)term << 15));
		if (value >= 0x40000000)
			value = 0x3FFFFFFF;
		if (value <= -0x40000000)
			value = -0x3FFF0000;
		m21 = value >> 15;
		value = (int32_t)((uint32_t)(((az * az) >> 15) * versine) + ((uint32_t)cosine << 15));
		if (value >= 0x40000000)
			value = 0x3FFFFFFF;
		if (value <= -0x40000000)
			value = -0x3FFF0000;
		m22 = value >> 15;
	} else {
		int32_t term;

		versine = -cosine;
		value = ax * ax;
		value = (int32_t)((uint32_t)value + (uint32_t)(value >> 15) * (uint32_t)versine +
						  ((uint32_t)cosine << 15));
		if (value >= 0x40000000)
			value = 0x3FFFFFFF;
		if (value <= -0x40000000)
			value = -0x3FFF0000;
		m00 = value >> 15;
		term = math2_mul_q15(sine, az);
		value = ax * ay;
		value =
			(int32_t)((uint32_t)value + (uint32_t)(value >> 15) * (uint32_t)versine + ((uint32_t)term << 15));
		if (value >= 0x40000000)
			value = 0x3FFFFFFF;
		if (value <= -0x40000000)
			value = -0x3FFF0000;
		m01 = value >> 15;
		term = -math2_mul_q15(sine, ay);
		value = ax * az;
		value =
			(int32_t)((uint32_t)value + (uint32_t)(value >> 15) * (uint32_t)versine + ((uint32_t)term << 15));
		if (value >= 0x40000000)
			value = 0x3FFFFFFF;
		if (value <= -0x40000000)
			value = -0x3FFF0000;
		m02 = value >> 15;
		term = -math2_mul_q15(sine, az);
		value = ax * ay;
		value =
			(int32_t)((uint32_t)value + (uint32_t)(value >> 15) * (uint32_t)versine + ((uint32_t)term << 15));
		if (value >= 0x40000000)
			value = 0x3FFFFFFF;
		if (value <= -0x40000000)
			value = -0x3FFF0000;
		m10 = value >> 15;
		value = ay * ay;
		value = (int32_t)((uint32_t)value + (uint32_t)(value >> 15) * (uint32_t)versine +
						  ((uint32_t)cosine << 15));
		if (value >= 0x40000000)
			value = 0x3FFFFFFF;
		if (value <= -0x40000000)
			value = -0x3FFF0000;
		m11 = value >> 15;
		term = math2_mul_q15(sine, ax);
		value = ay * az;
		value =
			(int32_t)((uint32_t)value + (uint32_t)(value >> 15) * (uint32_t)versine + ((uint32_t)term << 15));
		if (value >= 0x40000000)
			value = 0x3FFFFFFF;
		if (value <= -0x40000000)
			value = -0x3FFF0000;
		m12 = value >> 15;
		term = math2_mul_q15(sine, ay);
		value = ax * az;
		value =
			(int32_t)((uint32_t)value + (uint32_t)(value >> 15) * (uint32_t)versine + ((uint32_t)term << 15));
		if (value >= 0x40000000)
			value = 0x3FFFFFFF;
		if (value <= -0x40000000)
			value = -0x3FFF0000;
		m20 = value >> 15;
		term = -math2_mul_q15(sine, ax);
		value = ay * az;
		value =
			(int32_t)((uint32_t)value + (uint32_t)(value >> 15) * (uint32_t)versine + ((uint32_t)term << 15));
		if (value >= 0x40000000)
			value = 0x3FFFFFFF;
		if (value <= -0x40000000)
			value = -0x3FFF0000;
		m21 = value >> 15;
		value = az * az;
		value = (int32_t)((uint32_t)value + (uint32_t)(value >> 15) * (uint32_t)versine +
						  ((uint32_t)cosine << 15));
		if (value >= 0x40000000)
			value = 0x3FFFFFFF;
		if (value <= -0x40000000)
			value = -0x3FFF0000;
		m22 = value >> 15;
	}

	x -= (int32_t)rotation->pivot.x;
	y += (int32_t)rotation->pivot.y;
	z -= (int32_t)rotation->pivot.z;
	rx = math2_dot3_q15_clamped(x, y, z, m00, m10, m20);
	ry = math2_dot3_q15_clamped(x, y, z, m01, m11, m21);
	rz = math2_dot3_q15_clamped(x, y, z, m02, m12, m22);
	rotatedy = ry - (int32_t)rotation->pivot.y;
	rotatedz = rz + (int32_t)rotation->pivot.z;
	rotatedx = rx + (int32_t)rotation->pivot.x;
}

// FUNCTION: TIE98 0x43B5E0
// ModelMesh_GetCount; same name in OpenXWA.
int modelmesh_getcount(uint16_t model_type) {
	if (!species_table[model_type].model_handle)
		return 0;
	if ((species_table[model_type].load_flags & 1) == 0)
		return 0;
	return modelmesh_require_model(model_type)->mesh_count;
}

// FUNCTION: TIE98 0x43BB50
// ModelMesh_FindDescriptorNodeRecursive (inferred).
static const int32_t* modelmesh_finddescriptor(const Tie98OptNode* node,
											   const Tie98OptimizedPolyObject* model) {
	const int32_t* descriptor;
	int child;

	if (!node)
		return NULL;
	if (node->type == TIE98_OPT_NODE_MESH_DESCRIPTOR)
		return node->param2;
	for (child = 0; child < node->child_count; child++) {
		if (node->children[child]) {
			descriptor = modelmesh_finddescriptor(node->children[child], model);
			if (descriptor)
				return descriptor;
		}
	}
	return NULL;
}

// FUNCTION: TIE98 0x43BC40
// ModelMesh_GetType; same name in OpenXWA.
int modelmesh_gettype(uint16_t model_type, int mesh_index) {
	const TieFlightModelView* model;
	const TieModelMeshView* mesh;
	int mesh_slot;

	if (!species_table[model_type].model_handle)
		return TIE_MESH_DEFAULT;
	mesh = NULL;
	if (mesh_index >= 0 && (species_table[model_type].load_flags & 1) != 0) {
		model = modelmesh_require_model(model_type);
		mesh_slot = mesh_index;
		if (mesh_slot >= model->mesh_count)
			mesh_slot = model->mesh_count - 1;
		if (model->mesh_count)
			mesh = &model->meshes[mesh_slot];
	}
	return mesh && mesh->has_descriptor ? mesh->mesh_type : TIE_MESH_DEFAULT;
}

// FUNCTION: TIE98 0x43BCE0
// ModelMesh_GetVertexCount (inferred).
int modelmesh_getvertexcount(uint16_t model_type, int mesh_index) {
	const TieFlightModelView* model;
	const TieModelMeshView* mesh;
	int mesh_slot;

	mesh = NULL;
	if (mesh_index >= 0 && (species_table[model_type].load_flags & 1) != 0) {
		model = modelmesh_require_model(model_type);
		mesh_slot = mesh_index;
		if (mesh_slot >= model->mesh_count)
			mesh_slot = model->mesh_count - 1;
		if (model->mesh_count)
			mesh = &model->meshes[mesh_slot];
	}
	return mesh ? (int)mesh->vertex_count : 0;
}

// FUNCTION: TIE98 0x43BD60
// ModelMesh_GetVertexX; same name in OpenXWA.
int modelmesh_getvertexx(uint16_t model_type, int mesh_index, int vertex_index) {
	const TieFlightModelView* model;
	const TieModelMeshView* mesh;
	int mesh_slot;

	mesh = NULL;
	if (mesh_index >= 0 && (species_table[model_type].load_flags & 1) != 0) {
		model = modelmesh_require_model(model_type);
		mesh_slot = mesh_index;
		if (mesh_slot >= model->mesh_count)
			mesh_slot = model->mesh_count - 1;
		if (model->mesh_count)
			mesh = &model->meshes[mesh_slot];
	}
	if (!mesh || mesh->vertex_count == 0)
		return 0;
	if (vertex_index >= (int)mesh->vertex_count)
		vertex_index = (int)mesh->vertex_count - 1;
	return vertex_index >= 0 ? (int)mesh->vertices[vertex_index].x : 0;
}

// FUNCTION: TIE98 0x43BE00
// ModelMesh_GetVertexY; same name in OpenXWA.
int modelmesh_getvertexy(uint16_t model_type, int mesh_index, int vertex_index) {
	const TieFlightModelView* model;
	const TieModelMeshView* mesh;
	int mesh_slot;

	mesh = NULL;
	if (mesh_index >= 0 && (species_table[model_type].load_flags & 1) != 0) {
		model = modelmesh_require_model(model_type);
		mesh_slot = mesh_index;
		if (mesh_slot >= model->mesh_count)
			mesh_slot = model->mesh_count - 1;
		if (model->mesh_count)
			mesh = &model->meshes[mesh_slot];
	}
	if (!mesh || mesh->vertex_count == 0)
		return 0;
	if (vertex_index >= (int)mesh->vertex_count)
		vertex_index = (int)mesh->vertex_count - 1;
	return vertex_index >= 0 ? (int)mesh->vertices[vertex_index].y : 0;
}

// FUNCTION: TIE98 0x43BEA0
// ModelMesh_GetVertexZ; same name in OpenXWA.
int modelmesh_getvertexz(uint16_t model_type, int mesh_index, int vertex_index) {
	const TieFlightModelView* model;
	const TieModelMeshView* mesh;
	int mesh_slot;

	mesh = NULL;
	if (mesh_index >= 0 && (species_table[model_type].load_flags & 1) != 0) {
		model = modelmesh_require_model(model_type);
		mesh_slot = mesh_index;
		if (mesh_slot >= model->mesh_count)
			mesh_slot = model->mesh_count - 1;
		if (model->mesh_count)
			mesh = &model->meshes[mesh_slot];
	}
	if (!mesh || mesh->vertex_count == 0)
		return 0;
	if (vertex_index >= (int)mesh->vertex_count)
		vertex_index = (int)mesh->vertex_count - 1;
	return vertex_index >= 0 ? (int)mesh->vertices[vertex_index].z : 0;
}

// FUNCTION: TIE98 0x43BF40
// ModelMesh_GetCenterX
int modelmesh_getcenterx(uint16_t model_type, int mesh_index) {
	const TieFlightModelView* model;
	const TieModelMeshView* mesh;
	int center_x;
	int mesh_slot;

	if (mesh_index < 0)
		return 0;
	mesh = NULL;
	center_x = 0;
	if ((species_table[model_type].load_flags & 1) == 0)
		return 0;
	model = modelmesh_require_model(model_type);
	if (model->mesh_count) {
		mesh_slot = mesh_index;
		if (mesh_slot >= model->mesh_count)
			mesh_slot = model->mesh_count - 1;
		mesh = &model->meshes[mesh_slot];
	}
	if (mesh && mesh->has_descriptor)
		center_x = (int)mesh->center.x;
	return center_x;
}

// FUNCTION: TIE98 0x43BFE0
// ModelMesh_GetCenterY
int modelmesh_getcentery(uint16_t model_type, int mesh_index) {
	const TieFlightModelView* model;
	const TieModelMeshView* mesh;
	int center_y;

	if (mesh_index < 0)
		return 0;
	mesh = NULL;
	center_y = 0;
	if ((species_table[model_type].load_flags & 1) == 0)
		return 0;
	model = modelmesh_require_model(model_type);
	if (model->mesh_count) {
		if (mesh_index >= model->mesh_count)
			mesh_index = model->mesh_count - 1;
		mesh = &model->meshes[mesh_index];
	}
	if (mesh && mesh->has_descriptor)
		center_y = (int)mesh->center.y;
	return center_y;
}

// FUNCTION: TIE98 0x43C080
// ModelMesh_GetCenterZ
int modelmesh_getcenterz(uint16_t model_type, int mesh_index) {
	const TieFlightModelView* model;
	const TieModelMeshView* mesh;
	int center_z;

	if (mesh_index < 0)
		return 0;
	mesh = NULL;
	center_z = 0;
	if ((species_table[model_type].load_flags & 1) == 0)
		return 0;
	model = modelmesh_require_model(model_type);
	if (model->mesh_count) {
		if (mesh_index >= model->mesh_count)
			mesh_index = model->mesh_count - 1;
		mesh = &model->meshes[mesh_index];
	}
	if (mesh && mesh->has_descriptor)
		center_z = (int)mesh->center.z;
	return center_z;
}

// FUNCTION: TIE98 0x43C120
// ModelMesh_GetBoundsMinX
int modelmesh_getboundsminx(uint16_t model_type, int mesh_index) {
	const TieFlightModelView* model;
	const TieModelMeshView* mesh;
	int mesh_slot;

	mesh = NULL;
	if (mesh_index >= 0 && (species_table[model_type].load_flags & 1) != 0) {
		model = modelmesh_require_model(model_type);
		mesh_slot = mesh_index;
		if (mesh_slot >= model->mesh_count)
			mesh_slot = model->mesh_count - 1;
		if (model->mesh_count)
			mesh = &model->meshes[mesh_slot];
	}
	return mesh && mesh->has_descriptor ? (int)mesh->bounds.min.x : 0;
}

// FUNCTION: TIE98 0x43C1C0
// ModelMesh_GetBoundsMinY
int modelmesh_getboundsminy(uint16_t model_type, int mesh_index) {
	const TieFlightModelView* model;
	const TieModelMeshView* mesh;
	int mesh_slot;

	mesh = NULL;
	if (mesh_index >= 0 && (species_table[model_type].load_flags & 1) != 0) {
		model = modelmesh_require_model(model_type);
		mesh_slot = mesh_index;
		if (mesh_slot >= model->mesh_count)
			mesh_slot = model->mesh_count - 1;
		if (model->mesh_count)
			mesh = &model->meshes[mesh_slot];
	}
	return mesh && mesh->has_descriptor ? (int)mesh->bounds.min.y : 0;
}

// FUNCTION: TIE98 0x43C260
// ModelMesh_GetBoundsMinZ
int modelmesh_getboundsminz(uint16_t model_type, int mesh_index) {
	const TieFlightModelView* model;
	const TieModelMeshView* mesh;
	int mesh_slot;

	mesh = NULL;
	if (mesh_index >= 0 && (species_table[model_type].load_flags & 1) != 0) {
		model = modelmesh_require_model(model_type);
		mesh_slot = mesh_index;
		if (mesh_slot >= model->mesh_count)
			mesh_slot = model->mesh_count - 1;
		if (model->mesh_count)
			mesh = &model->meshes[mesh_slot];
	}
	return mesh && mesh->has_descriptor ? (int)mesh->bounds.min.z : 0;
}

// FUNCTION: TIE98 0x43C300
// ModelMesh_GetBoundsMaxX
int modelmesh_getboundsmaxx(uint16_t model_type, int mesh_index) {
	const TieFlightModelView* model;
	const TieModelMeshView* mesh;
	int mesh_slot;

	mesh = NULL;
	if (mesh_index >= 0 && (species_table[model_type].load_flags & 1) != 0) {
		model = modelmesh_require_model(model_type);
		mesh_slot = mesh_index;
		if (mesh_slot >= model->mesh_count)
			mesh_slot = model->mesh_count - 1;
		if (model->mesh_count)
			mesh = &model->meshes[mesh_slot];
	}
	return mesh && mesh->has_descriptor ? (int)mesh->bounds.max.x : 0;
}

// FUNCTION: TIE98 0x43C3A0
// ModelMesh_GetBoundsMaxY
int modelmesh_getboundsmaxy(uint16_t model_type, int mesh_index) {
	const TieFlightModelView* model;
	const TieModelMeshView* mesh;
	int mesh_slot;

	mesh = NULL;
	if (mesh_index >= 0 && (species_table[model_type].load_flags & 1) != 0) {
		model = modelmesh_require_model(model_type);
		mesh_slot = mesh_index;
		if (mesh_slot >= model->mesh_count)
			mesh_slot = model->mesh_count - 1;
		if (model->mesh_count)
			mesh = &model->meshes[mesh_slot];
	}
	return mesh && mesh->has_descriptor ? (int)mesh->bounds.max.y : 0;
}

// FUNCTION: TIE98 0x43C440
// ModelMesh_GetBoundsMaxZ
int modelmesh_getboundsmaxz(uint16_t model_type, int mesh_index) {
	const TieFlightModelView* model;
	const TieModelMeshView* mesh;
	int mesh_slot;

	mesh = NULL;
	if (mesh_index >= 0 && (species_table[model_type].load_flags & 1) != 0) {
		model = modelmesh_require_model(model_type);
		mesh_slot = mesh_index;
		if (mesh_slot >= model->mesh_count)
			mesh_slot = model->mesh_count - 1;
		if (model->mesh_count)
			mesh = &model->meshes[mesh_slot];
	}
	return mesh && mesh->has_descriptor ? (int)mesh->bounds.max.z : 0;
}

// FUNCTION: TIE98 0x43C4E0
// ModelMesh_GetTargetId; same name in OpenXWA.
int modelmesh_gettargetid(uint16_t model_type, int mesh_index) {
	const TieFlightModelView* model;
	const TieModelMeshView* mesh;
	int mesh_slot;

	mesh = NULL;
	if (mesh_index >= 0 && (species_table[model_type].load_flags & 1) != 0) {
		model = modelmesh_require_model(model_type);
		mesh_slot = mesh_index;
		if (mesh_slot >= model->mesh_count)
			mesh_slot = model->mesh_count - 1;
		if (model->mesh_count)
			mesh = &model->meshes[mesh_slot];
	}
	return mesh && mesh->has_descriptor ? mesh->target_id : 0;
}

// FUNCTION: TIE98 0x43C570
// ModelMesh_GetComponentFocusX
int modelmesh_getcomponentfocusx(uint16_t model_type, int mesh_index) {
	const TieFlightModelView* model;
	const TieModelMeshView* mesh;
	int mesh_slot;

	mesh = NULL;
	if (mesh_index >= 0 && (species_table[model_type].load_flags & 1) != 0) {
		model = modelmesh_require_model(model_type);
		mesh_slot = mesh_index;
		if (mesh_slot >= model->mesh_count)
			mesh_slot = model->mesh_count - 1;
		if (model->mesh_count)
			mesh = &model->meshes[mesh_slot];
	}
	if (!mesh || !mesh->has_descriptor)
		return 0;
	return mesh->target_id ? (int)mesh->target.x : (int)mesh->center.x;
}

// FUNCTION: TIE98 0x43C620
// ModelMesh_GetComponentFocusY
int modelmesh_getcomponentfocusy(uint16_t model_type, int mesh_index) {
	const TieFlightModelView* model;
	const TieModelMeshView* mesh;
	int mesh_slot;

	mesh = NULL;
	if (mesh_index >= 0 && (species_table[model_type].load_flags & 1) != 0) {
		model = modelmesh_require_model(model_type);
		mesh_slot = mesh_index;
		if (mesh_slot >= model->mesh_count)
			mesh_slot = model->mesh_count - 1;
		if (model->mesh_count)
			mesh = &model->meshes[mesh_slot];
	}
	if (!mesh || !mesh->has_descriptor)
		return 0;
	return mesh->target_id ? (int)mesh->target.y : (int)mesh->center.y;
}

// FUNCTION: TIE98 0x43C6D0
// ModelMesh_GetComponentFocusZ
int modelmesh_getcomponentfocusz(uint16_t model_type, int mesh_index) {
	const TieFlightModelView* model;
	const TieModelMeshView* mesh;
	int mesh_slot;

	mesh = NULL;
	if (mesh_index >= 0 && (species_table[model_type].load_flags & 1) != 0) {
		model = modelmesh_require_model(model_type);
		mesh_slot = mesh_index;
		if (mesh_slot >= model->mesh_count)
			mesh_slot = model->mesh_count - 1;
		if (model->mesh_count)
			mesh = &model->meshes[mesh_slot];
	}
	if (!mesh || !mesh->has_descriptor)
		return 0;
	return mesh->target_id ? (int)mesh->target.z : (int)mesh->center.z;
}

// FUNCTION: TIE98 0x43C780
// ModelMesh_GetComponentMaxExtent; same name in OpenXWA.
int modelmesh_getcomponentmaxextent(int model_type, int mesh_index) {
	const TieFlightModelView* model;
	const TieModelMeshView* mesh;
	int extent;

	mesh = NULL;
	if (mesh_index < 0)
		return 0;
	if ((species_table[model_type].load_flags & 1) == 0)
		return 0;
	model = modelmesh_require_model(model_type);
	if (model->mesh_count) {
		if (mesh_index >= model->mesh_count)
			mesh_index = model->mesh_count - 1;
		mesh = &model->meshes[mesh_index];
	}
	extent = 0;
	if (mesh && mesh->has_descriptor) {
		int extent_y;
		int extent_z;

		extent = (int)mesh->span.x;
		extent_y = (int)mesh->span.y;
		extent_z = (int)mesh->span.z;
		if (extent_y >= extent && extent_y >= extent_z)
			extent = extent_y;
		else if (extent_z >= extent && extent_z >= extent_y)
			extent = extent_z;
	}
	return extent;
}

// FUNCTION: TIE98 0x43C850
// ModelMesh_IsObjectTypeMeshDamageable; same name in OpenXWA.
int modelmesh_isobjecttypemeshdamageable(uint16_t model_type, int mesh_index) {
	const TieFlightModelView* model;
	const TieModelMeshView* mesh;
	int mesh_slot;

	if (mesh_index < 0)
		return 0;
	if ((species_table[model_type].load_flags & 1) == 0)
		return 0;
	model = modelmesh_require_model(model_type);
	if (!model->mesh_count)
		return 0;
	mesh_slot = mesh_index;
	if (mesh_slot >= model->mesh_count)
		mesh_slot = model->mesh_count - 1;
	mesh = &model->meshes[mesh_slot];
	if (!mesh->has_descriptor)
		return 0;
	return (mesh->explosion_type | TieFlightAssets_MeshExplosionTypeOverride(model_type, mesh_index)) & 2;
}

// FUNCTION: TIE98 0x43C8F0
// ModelMesh_HasExplosionType1; same name in OpenXWA.
int modelmesh_hasexplosiontype1(uint16_t model_type, int mesh_index) {
	const TieFlightModelView* model;
	const TieModelMeshView* mesh;
	int mesh_slot;

	mesh = NULL;
	if (mesh_index >= 0 && (species_table[model_type].load_flags & 1) != 0) {
		model = modelmesh_require_model(model_type);
		mesh_slot = mesh_index;
		if (mesh_slot >= model->mesh_count)
			mesh_slot = model->mesh_count - 1;
		if (model->mesh_count)
			mesh = &model->meshes[mesh_slot];
	}
	if (!mesh || !mesh->has_descriptor)
		return 0;
	return (mesh->explosion_type | TieFlightAssets_MeshExplosionTypeOverride(model_type, mesh_index)) & 1;
}

// FUNCTION: TIE98 0x43C990
// ModelMesh_EnableExplosionType2
int modelmesh_enableexplosiontype2(int model_type, int mesh_index) {
	const Tie98OptimizedPolyObject* model;
	const int32_t* descriptor;
	int root;

	if (mesh_index < 0)
		return 0;
	if ((species_table[model_type].load_flags & 1) == 0)
		return 0;
	/* PORT: the native OPT cache replaces the original handle lock, pointer
	 * relocation and unlock of g_loaded_model_handles[model_type]. */
	model = TieNativeOpt_Acquire(model_type);
#ifdef TIE_MODERN
	if (!model || !model->root_node_count || !model->root_nodes[0])
		return 0;
#endif
	root = mesh_index;
	if (model->root_nodes[0]->type == TIE98_OPT_NODE_TEXTURE)
		root++;
	if (root >= model->root_node_count)
		root = model->root_node_count - 1;
	descriptor = modelmesh_finddescriptor(model->root_nodes[root], model);
	if (descriptor)
		TieFlightAssets_EnableMeshExplosionType(model_type, mesh_index, 2);
	return 1;
}

// FUNCTION: TIE98 0x43CA20
// ModelMesh_EnableExplosionType1
void modelmesh_enableexplosiontype1(uint16_t model_type, int mesh_index) {
	const TieFlightModelView* model;
	const TieModelMeshView* mesh;
	int mesh_slot;

	mesh = NULL;
	if (mesh_index >= 0 && (species_table[model_type].load_flags & 1) != 0) {
		model = modelmesh_require_model(model_type);
		mesh_slot = mesh_index;
		if (mesh_slot >= model->mesh_count)
			mesh_slot = model->mesh_count - 1;
		if (model->mesh_count)
			mesh = &model->meshes[mesh_slot];
	}
	if (mesh && mesh->has_descriptor)
		TieFlightAssets_EnableMeshExplosionType(model_type, mesh_index, 1);
}

// FUNCTION: TIE98 0x43CAB0
// ModelMesh_GetRotScaleData; same name in OpenXWA.
const TieModelRotationScale* modelmesh_getrotscaledata(uint16_t model_type, int mesh_index) {
	const TieFlightModelView* model;
	const TieModelMeshView* mesh;
	int mesh_slot;

	mesh = NULL;
	if (mesh_index >= 0 && (species_table[model_type].load_flags & 1) != 0) {
		model = modelmesh_require_model(model_type);
		mesh_slot = mesh_index;
		if (mesh_slot >= model->mesh_count)
			mesh_slot = model->mesh_count - 1;
		if (model->mesh_count)
			mesh = &model->meshes[mesh_slot];
	}
	return mesh && mesh->has_rotation_scale ? &mesh->rotation_scale : NULL;
}

// FUNCTION: TIE98 0x43CCE0
// ModelMesh_CountHardpoints; same name in OpenXWA.
int modelmesh_counthardpoints(uint16_t model_type, int mesh_index) {
	const TieFlightModelView* model;
	const TieModelMeshView* mesh;
	int mesh_slot;

	mesh = NULL;
	if (mesh_index >= 0 && (species_table[model_type].load_flags & 1) != 0) {
		model = modelmesh_require_model(model_type);
		mesh_slot = mesh_index;
		if (mesh_slot >= model->mesh_count)
			mesh_slot = model->mesh_count - 1;
		if (model->mesh_count)
			mesh = &model->meshes[mesh_slot];
	}
	return mesh ? (int)mesh->hardpoint_count : 0;
}

// FUNCTION: TIE98 0x43CD60
// ModelMesh_GetAlternateHardpointIndex; same name in OpenXWA.
int modelmesh_getalternatehardpointindex(uint16_t model_type, int mesh_index, int hardpoint_index) {
	(void)model_type;
	(void)mesh_index;
	return hardpoint_index;
}

// FUNCTION: TIE98 0x43CF50
// ModelMesh_GetHardpoint; same name in OpenXWA.
void modelmesh_gethardpoint(uint16_t model_type, int mesh_index, int hardpoint_index, int* type, int* x,
							int* y, int* z) {
	const TieFlightModelView* model;
	const TieModelMeshView* mesh;
	int mesh_slot;
	const TieModelHardpoint* hardpoint;

	mesh = NULL;
	if (mesh_index >= 0 && (species_table[model_type].load_flags & 1) != 0) {
		model = modelmesh_require_model(model_type);
		mesh_slot = mesh_index;
		if (mesh_slot >= model->mesh_count)
			mesh_slot = model->mesh_count - 1;
		if (model->mesh_count)
			mesh = &model->meshes[mesh_slot];
	}
	if (!mesh || hardpoint_index < 0 || hardpoint_index >= (int)mesh->hardpoint_count) {
		*type = *x = *y = *z = 0;
		return;
	}
	hardpoint = &mesh->hardpoints[hardpoint_index];
	*type = hardpoint->type;
	*x = (int)hardpoint->position.x;
	*y = -(int)hardpoint->position.y;
	*z = (int)hardpoint->position.z;
}

// FUNCTION: TIE98 0x43D4A0
// ModelMesh_FindBridgeIndex; same name in OpenXWA.
int modelmesh_findbridgeindex(const Tie98OptimizedPolyObject* model) {
	const int32_t* descriptor;
	int mesh_index;
	int root;

	mesh_index = 0;
	for (root = 0; root < model->root_node_count; root++) {
#ifdef TIE_MODERN
		/* PORT: the native OPT cache leaves empty root slots NULL. */
		if (!model->root_nodes[root])
			continue;
#endif
		if (model->root_nodes[root]->type != TIE98_OPT_NODE_TEXTURE) {
			descriptor = modelmesh_finddescriptor(model->root_nodes[root], model);
			if (descriptor && *descriptor == TIE_MESH_BRIDGE)
				break;
			mesh_index++;
		}
	}
	if (root < model->root_node_count)
		return mesh_index;
	return -1;
}

/* Per-object-type mesh data snapshotted after the species models load.
 * The second per-mesh column (+0xCC) is filled from ModelMesh @ 0x43BBA0,
 * which is not recovered yet; nothing in this source reads it. */
typedef struct TieObjectTypeMeshCache {
	int mesh_count;
	int mesh_types[50];
	int mesh_unknown[50];
} TieObjectTypeMeshCache;

// GLOBAL: TIE98 0x5973C0
static TieObjectTypeMeshCache g_object_type_mesh_cache[NUM_SPEC];

// FUNCTION: TIE98 0x43D500
// ModelMesh_BuildObjectTypeMeshCache (inferred).
void modelmesh_buildobjecttypemeshcache(void) {
	int model_type;
	int mesh_count;
	int mesh_index;

	for (model_type = 0; model_type < NUM_SPEC; model_type++) {
		mesh_count = modelmesh_getcount((uint16_t)model_type);
#ifdef TIE_MODERN
		/* PORT: keep oversized host models inside the fixed cache row. */
		if (mesh_count > 50)
			mesh_count = 50;
#endif
		g_object_type_mesh_cache[model_type].mesh_count = mesh_count;
		for (mesh_index = 0; mesh_index < mesh_count; mesh_index++)
			g_object_type_mesh_cache[model_type].mesh_types[mesh_index] =
				modelmesh_gettype((uint16_t)model_type, mesh_index);
	}
}

// FUNCTION: TIE98 0x43D580
// ModelMesh_GetObjectTypeMeshCount; same name in OpenXWA.
int modelmesh_getobjecttypemeshcount(int model_type) {
	if (model_type < NUM_SPEC)
		return g_object_type_mesh_cache[model_type].mesh_count;
	return modelmesh_getcount((uint16_t)model_type);
}

// FUNCTION: TIE98 0x43D5B0
// ModelMesh_GetObjectTypeMeshType; same name in OpenXWA.
int modelmesh_getobjecttypemeshtype(int model_type, int mesh_index) {
	if (model_type < NUM_SPEC) {
		if (mesh_index < 0)
			return 0;
#ifdef TIE_MODERN
		/* PORT: an empty row read its own zero count through index -1. */
		if (g_object_type_mesh_cache[model_type].mesh_count == 0)
			return 0;
#endif
		if (mesh_index >= g_object_type_mesh_cache[model_type].mesh_count)
			mesh_index = g_object_type_mesh_cache[model_type].mesh_count - 1;
		return g_object_type_mesh_cache[model_type].mesh_types[mesh_index];
	}
	return modelmesh_gettype((uint16_t)model_type, mesh_index);
}
