#include "tie/math2_wide.h"
#include "tie/modelmesh.h"
#include "tie_runtime/flight_assets/service.h"

#include "tie/shell.h"
#include "tie/tie.h"
#include "tie/trig2.h"

#include <limits.h>
#include <stdio.h>

enum {
	MODEL_MESH_CRAFT_SLOTS = 40,
};

/* The original setters mutate the loaded OPT descriptor. Host model views are
 * immutable, so retain those process-lifetime descriptor bits alongside them. */
static uint8_t explosion_type_overrides[TIE_SPECIES_COUNT][MODEL_MESH_CRAFT_SLOTS];

// PORT: host model view replaces the original TIE98 OPT registry pointer.
const TieFlightModelView* modelmesh_require_model(uint16_t model_type) {
	TieFlightModelApi models = TieFlightAssets_ModelApi();
	char error[768];
	const TieFlightModelView* model = models.acquire(models.context, model_type, error, sizeof error);
	if (!model)
		shell_programexit(error);
	return model;
}

// PORT: the retained TIE95 CraftData has 39 component slots plus lightning.
void modelmesh_require_craft_capacity(uint16_t model_type) {
	if (modelmesh_require_model(model_type)->mesh_count > 39)
		shell_programexit("TIE98 OPT craft has more than 39 meshes");
}

// PORT: replaces the original inline species[].load_flags test.
static bool modelmesh_has_opt(uint16_t model_type) { return (species_table[model_type].load_flags & 1) != 0; }

static const TieModelMeshView* mesh_view(uint16_t model_type, int mesh_index) {
	const TieFlightModelView* model;

	if (mesh_index < 0)
		return NULL;
	if (!modelmesh_has_opt(model_type))
		return NULL;
	model = modelmesh_require_model(model_type);
	if (mesh_index >= model->mesh_count)
		mesh_index = model->mesh_count - 1;
	return model->mesh_count ? &model->meshes[mesh_index] : NULL;
}

// FUNCTION: TIE98 0x43B5E0
// ModelMesh_GetCount; same name in OpenXWA.
int modelmesh_getcount(uint16_t model_type) {
	if (!modelmesh_has_opt(model_type))
		return 0;
	return modelmesh_require_model(model_type)->mesh_count;
}

// FUNCTION: TIE98 0x43BC40
// ModelMesh_GetType; same name in OpenXWA.
int modelmesh_gettype(uint16_t model_type, int mesh_index) {
	const TieModelMeshView* mesh = mesh_view(model_type, mesh_index);
	return mesh && mesh->has_descriptor ? mesh->mesh_type : TIE_MESH_DEFAULT;
}

// FUNCTION: TIE98 0x43D5B0
// ModelMesh_GetObjectTypeMeshType; same name in OpenXWA.
int modelmesh_getobjecttypemeshtype(uint16_t model_type, int mesh_index) {
	// PORT: the host model view is the authoritative object-type mesh cache.
	return modelmesh_gettype(model_type, mesh_index);
}

// FUNCTION: TIE98 0x43BCE0
// ModelMesh_GetVertexCount (inferred).
int modelmesh_getvertexcount(uint16_t model_type, int mesh_index) {
	const TieModelMeshView* mesh = mesh_view(model_type, mesh_index);
	return mesh ? (int)mesh->vertex_count : 0;
}

static const TieModelVec3f* mesh_vertex(uint16_t model_type, int mesh_index, int vertex_index) {
	const TieModelMeshView* mesh = mesh_view(model_type, mesh_index);
	if (!mesh || mesh->vertex_count == 0)
		return NULL;
	if (vertex_index >= (int)mesh->vertex_count)
		vertex_index = (int)mesh->vertex_count - 1;
	return vertex_index >= 0 ? &mesh->vertices[vertex_index] : NULL;
}

// FUNCTION: TIE98 0x43BD60
// ModelMesh_GetVertexX; same name in OpenXWA.
int modelmesh_getvertexx(uint16_t model_type, int mesh_index, int vertex_index) {
	const TieModelVec3f* vertex = mesh_vertex(model_type, mesh_index, vertex_index);
	return vertex ? (int)vertex->x : 0;
}

// FUNCTION: TIE98 0x43BE00
// ModelMesh_GetVertexY; same name in OpenXWA.
int modelmesh_getvertexy(uint16_t model_type, int mesh_index, int vertex_index) {
	const TieModelVec3f* vertex = mesh_vertex(model_type, mesh_index, vertex_index);
	return vertex ? (int)vertex->y : 0;
}

// FUNCTION: TIE98 0x43BEA0
// ModelMesh_GetVertexZ; same name in OpenXWA.
int modelmesh_getvertexz(uint16_t model_type, int mesh_index, int vertex_index) {
	const TieModelVec3f* vertex = mesh_vertex(model_type, mesh_index, vertex_index);
	return vertex ? (int)vertex->z : 0;
}

static int descriptor_value(uint16_t model_type, int mesh_index, int field) {
	const TieModelMeshView* mesh = mesh_view(model_type, mesh_index);
	if (!mesh || !mesh->has_descriptor)
		return 0;
	switch (field) {
		case 0:
			return (int)mesh->center.x;
		case 1:
			return (int)mesh->center.y;
		case 2:
			return (int)mesh->center.z;
		case 3:
			return (int)mesh->bounds.min.x;
		case 4:
			return (int)mesh->bounds.min.y;
		case 5:
			return (int)mesh->bounds.min.z;
		case 6:
			return (int)mesh->bounds.max.x;
		case 7:
			return (int)mesh->bounds.max.y;
		default:
			return (int)mesh->bounds.max.z;
	}
}

// FUNCTION: TIE98 0x43BF40
// ModelMesh_GetCenterX
int modelmesh_getcenterx(uint16_t m, int i) { return descriptor_value(m, i, 0); }
// FUNCTION: TIE98 0x43BFE0
// ModelMesh_GetCenterY
int modelmesh_getcentery(uint16_t m, int i) { return descriptor_value(m, i, 1); }
// FUNCTION: TIE98 0x43C080
// ModelMesh_GetCenterZ
int modelmesh_getcenterz(uint16_t m, int i) { return descriptor_value(m, i, 2); }

// FUNCTION: TIE98 0x43C120
// ModelMesh_GetBoundsMinX
int modelmesh_getboundsminx(uint16_t m, int i) { return descriptor_value(m, i, 3); }
// FUNCTION: TIE98 0x43C1C0
// ModelMesh_GetBoundsMinY
int modelmesh_getboundsminy(uint16_t m, int i) { return descriptor_value(m, i, 4); }
// FUNCTION: TIE98 0x43C260
// ModelMesh_GetBoundsMinZ
int modelmesh_getboundsminz(uint16_t m, int i) { return descriptor_value(m, i, 5); }
// FUNCTION: TIE98 0x43C300
// ModelMesh_GetBoundsMaxX
int modelmesh_getboundsmaxx(uint16_t m, int i) { return descriptor_value(m, i, 6); }
// FUNCTION: TIE98 0x43C3A0
// ModelMesh_GetBoundsMaxY
int modelmesh_getboundsmaxy(uint16_t m, int i) { return descriptor_value(m, i, 7); }
// FUNCTION: TIE98 0x43C440
// ModelMesh_GetBoundsMaxZ
int modelmesh_getboundsmaxz(uint16_t m, int i) { return descriptor_value(m, i, 8); }

// FUNCTION: TIE98 0x43C4E0
// ModelMesh_GetTargetId; same name in OpenXWA.
int modelmesh_gettargetid(uint16_t model_type, int mesh_index) {
	const TieModelMeshView* mesh = mesh_view(model_type, mesh_index);
	return mesh && mesh->has_descriptor ? mesh->target_id : 0;
}

static int component_focus(uint16_t model_type, int mesh_index, int axis) {
	const TieModelMeshView* mesh = mesh_view(model_type, mesh_index);
	const TieModelVec3f* point;

	if (!mesh || !mesh->has_descriptor)
		return 0;
	point = mesh->target_id ? &mesh->target : &mesh->center;
	return axis == 0 ? (int)point->x : axis == 1 ? (int)point->y : (int)point->z;
}

// FUNCTION: TIE98 0x43C570
// ModelMesh_GetComponentFocusX
int modelmesh_getcomponentfocusx(uint16_t m, int i) { return component_focus(m, i, 0); }
// FUNCTION: TIE98 0x43C620
// ModelMesh_GetComponentFocusY
int modelmesh_getcomponentfocusy(uint16_t m, int i) { return component_focus(m, i, 1); }
// FUNCTION: TIE98 0x43C6D0
// ModelMesh_GetComponentFocusZ
int modelmesh_getcomponentfocusz(uint16_t m, int i) { return component_focus(m, i, 2); }

// FUNCTION: TIE98 0x43C780
// ModelMesh_GetComponentMaxExtent; same name in OpenXWA.
int modelmesh_getcomponentmaxextent(uint16_t model_type, int mesh_index) {
	const TieModelMeshView* mesh = mesh_view(model_type, mesh_index);
	float extent;

	if (!mesh || !mesh->has_descriptor)
		return 0;
	extent = mesh->span.x;
	if (mesh->span.y > extent)
		extent = mesh->span.y;
	if (mesh->span.z > extent)
		extent = mesh->span.z;
	return (int)extent;
}

static int explosion_type(uint16_t model_type, int mesh_index) {
	const TieModelMeshView* mesh = mesh_view(model_type, mesh_index);
	int value;

	if (!mesh || !mesh->has_descriptor)
		return 0;
	value = mesh->explosion_type;
	if (model_type < TIE_SPECIES_COUNT && mesh_index >= 0 && mesh_index < MODEL_MESH_CRAFT_SLOTS)
		value |= explosion_type_overrides[model_type][mesh_index];
	return value;
}

// FUNCTION: TIE98 0x43C850
// ModelMesh_IsObjectTypeMeshDamageable; same name in OpenXWA.
int modelmesh_isobjecttypemeshdamageable(uint16_t model_type, int mesh_index) {
	return explosion_type(model_type, mesh_index) & 2;
}

// FUNCTION: TIE98 0x43C8F0
// ModelMesh_HasExplosionType1; same name in OpenXWA.
int modelmesh_hasexplosiontype1(uint16_t model_type, int mesh_index) {
	return explosion_type(model_type, mesh_index) & 1;
}

static void enable_explosion_type(uint16_t model_type, int mesh_index, uint8_t flag) {
	const TieModelMeshView* mesh = mesh_view(model_type, mesh_index);
	if (!mesh || !mesh->has_descriptor || model_type >= TIE_SPECIES_COUNT || mesh_index < 0 ||
		mesh_index >= MODEL_MESH_CRAFT_SLOTS)
		return;
	explosion_type_overrides[model_type][mesh_index] |= flag;
}

void modelmesh_enableexplosiontype1(uint16_t model_type, int mesh_index) {
	enable_explosion_type(model_type, mesh_index, 1);
}

void modelmesh_enableexplosiontype2(uint16_t model_type, int mesh_index) {
	enable_explosion_type(model_type, mesh_index, 2);
}

// FUNCTION: TIE98 0x43CAB0
// ModelMesh_GetRotScaleData; same name in OpenXWA.
const TieModelRotationScale* modelmesh_getrotscaledata(uint16_t model_type, int mesh_index) {
	const TieModelMeshView* mesh = mesh_view(model_type, mesh_index);
	return mesh && mesh->has_rotation_scale ? &mesh->rotation_scale : NULL;
}

// FUNCTION: TIE98 0x43CCE0
// ModelMesh_CountHardpoints; same name in OpenXWA.
int modelmesh_counthardpoints(uint16_t model_type, int mesh_index) {
	const TieModelMeshView* mesh = mesh_view(model_type, mesh_index);
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
	const TieModelMeshView* mesh = mesh_view(model_type, mesh_index);
	const TieModelHardpoint* hardpoint;

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
	if (!modelmesh_has_opt(model_type))
		return -1;
	return modelmesh_require_model(model_type)->bridge_mesh_index;
}

static int32_t clamp_q30(int32_t value) {
	if (value >= 0x40000000)
		return 0x3FFFFFFF;
	if (value <= -0x40000000)
		return -0x3FFF0000;
	return (int32_t)value;
}

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
			matrix[row][column] = clamp_q30((int32_t)value) >> 15;
		}
	}

	point[0] = (int32_t)((uint32_t)x - (uint32_t)(int32_t)rotation->pivot.x);
	point[1] = (int32_t)((uint32_t)y + (uint32_t)(int32_t)rotation->pivot.y);
	point[2] = (int32_t)((uint32_t)z - (uint32_t)(int32_t)rotation->pivot.z);
	for (row = 0; row < 3; ++row) {
		const uint32_t value = (uint32_t)matrix[row][0] * (uint32_t)point[0] +
							   (uint32_t)matrix[row][1] * (uint32_t)point[1] +
							   (uint32_t)matrix[row][2] * (uint32_t)point[2];
		result[row] = clamp_q30((int32_t)value) >> 15;
	}
	*out_x = (int32_t)((uint32_t)(int32_t)rotation->pivot.x + (uint32_t)result[0]);
	*out_y = (int32_t)((uint32_t)result[1] - (uint32_t)(int32_t)rotation->pivot.y);
	*out_z = (int32_t)((uint32_t)(int32_t)rotation->pivot.z + (uint32_t)result[2]);
}
