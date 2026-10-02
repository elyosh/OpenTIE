#include "tie/math2_wide.h"
#include "tie/modelmesh.h"
#include "tie_runtime/flight_assets/service.h"

#include "tie/shell.h"
#include "tie/tie.h"
#include "tie/trig2.h"

#include <limits.h>
#include <stdio.h>

// FUNCTION: TIE98 0x423FC0
// ModelMesh_ApplyAnimatedMeshRotationToPoint; same name in OpenXWA.
void modelmesh_applyanimatedmeshrotationtopoint(int angle, uint16_t model_type, int mesh_index, int x, int y,
												int z, int* out_x, int* out_y, int* out_z) {
	const TieModelRotationScale* rotation;
	int32_t ax, ay, az;
	int32_t cosine, sine;
	uint32_t sine_axis[3];
	int32_t matrix[3][3];
	int32_t axis[3];
	int32_t point[3];
	int32_t result[3];
	int32_t clamped;
	int row, column;

	*out_x = x;
	*out_y = y;
	*out_z = z;
	rotation = modelmesh_getrotscaledata(model_type, mesh_index);
	if (!rotation)
		return;

	ax = (int32_t)rotation->rotation_axis.x;
	ay = (int32_t)rotation->rotation_axis.y;
	az = (int32_t)rotation->rotation_axis.z;
	cosine = trig2_getsignedcos((int16_t)angle);
	sine = trig2_getsignedsin((uint16_t)angle);
	axis[0] = ax;
	axis[1] = ay;
	axis[2] = az;
	/* The sine terms are truncated to Q15 before being combined with
	 * the Q30 axis products. All sums retain the original low 32 bits. */
	for (row = 0; row < 3; ++row)
		sine_axis[row] = (uint32_t)math2_mul_q15(sine, axis[row]) << 15;

	for (row = 0; row < 3; ++row) {
		for (column = 0; column < 3; ++column) {
			const int32_t product = (int32_t)((uint32_t)axis[row] * (uint32_t)axis[column]);
			uint32_t value;

			if (cosine < 0)
				value = (uint32_t)product + (uint32_t)(-cosine) * (uint32_t)(product >> 15);
			else
				value = (uint32_t)(0x7FFF - cosine) * (uint32_t)(product >> 15);
			if (row == column)
				value += (uint32_t)cosine << 15;
			if (row == 0 && column == 1)
				value -= sine_axis[2];
			if (row == 0 && column == 2)
				value += sine_axis[1];
			if (row == 1 && column == 0)
				value += sine_axis[2];
			if (row == 1 && column == 2)
				value -= sine_axis[0];
			if (row == 2 && column == 0)
				value -= sine_axis[1];
			if (row == 2 && column == 1)
				value += sine_axis[0];
			clamped = (int32_t)value;
			if (clamped >= 0x40000000)
				clamped = 0x3FFFFFFF;
			else if (clamped <= -0x40000000)
				clamped = -0x3FFF0000;
			matrix[row][column] = clamped >> 15;
		}
	}

	point[0] = (int32_t)((uint32_t)x - (uint32_t)(int32_t)rotation->pivot.x);
	point[1] = (int32_t)((uint32_t)y + (uint32_t)(int32_t)rotation->pivot.y);
	point[2] = (int32_t)((uint32_t)z - (uint32_t)(int32_t)rotation->pivot.z);
	for (row = 0; row < 3; ++row) {
		const uint32_t value = (uint32_t)matrix[row][0] * (uint32_t)point[0] +
							   (uint32_t)matrix[row][1] * (uint32_t)point[1] +
							   (uint32_t)matrix[row][2] * (uint32_t)point[2];
		clamped = (int32_t)value;
		if (clamped >= 0x40000000)
			clamped = 0x3FFFFFFF;
		else if (clamped <= -0x40000000)
			clamped = -0x3FFF0000;
		result[row] = clamped >> 15;
	}
	*out_x = (int32_t)((uint32_t)(int32_t)rotation->pivot.x + (uint32_t)result[0]);
	*out_y = (int32_t)((uint32_t)result[1] - (uint32_t)(int32_t)rotation->pivot.y);
	*out_z = (int32_t)((uint32_t)(int32_t)rotation->pivot.z + (uint32_t)result[2]);
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
int modelmesh_getcomponentmaxextent(uint16_t model_type, int mesh_index) {
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
void modelmesh_enableexplosiontype2(uint16_t model_type, int mesh_index) {
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
		TieFlightAssets_EnableMeshExplosionType(model_type, mesh_index, 2);
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
int modelmesh_findbridgeindex(uint16_t model_type) {
	if ((species_table[model_type].load_flags & 1) == 0)
		return -1;
	return modelmesh_require_model(model_type)->bridge_mesh_index;
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
