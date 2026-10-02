#include "tie/modelbounds.h"
#include "tie/modelmesh.h"
#include "tie/tie.h"
#include "tie_runtime/flight_assets/native_opt.h"

// GLOBAL: TIE98 0x583420
static TieModelVec3f g_model_bounds_min[NUM_SPECIES];
// GLOBAL: TIE98 0x583BB0
static int g_model_bounds_cached[NUM_SPECIES];
// GLOBAL: TIE98 0x583E38
static TieModelVec3f g_model_bounds_max[NUM_SPECIES];

// FUNCTION: TIE98 0x43B660
// ModelMesh_FindFirstMeshVertsNode
static const Tie98OptNode* modelmesh_findfirstmeshvertsnode(const Tie98OptNode* node) {
	const Tie98OptNode* found;
	int index;

	if (!node)
		return NULL;
	if (node->type == TIE98_OPT_NODE_MESH_VERTICES)
		return node;
	for (index = 0; index < node->child_count; ++index) {
		if (node->children[index]) {
			found = modelmesh_findfirstmeshvertsnode(node->children[index]);
			if (found)
				return found;
		}
	}
	return NULL;
}

/* Each mesh vertex list ends with its bounding box: the second-to-last
 * vertex is the minimum corner and the last vertex the maximum corner. */
// FUNCTION: TIE98 0x43B700
// ModelBounds_EnsureCached
void modelbounds_ensurecached(int model_type) {
	const Tie98OptimizedPolyObject* model;
	const Tie98OptNode* node;
	const TieModelVec3f* corner;
	TieModelVec3f min;
	TieModelVec3f max;
	int index;

	min.z = 1073741824.0f;
	min.y = 1073741824.0f;
	min.x = 1073741824.0f;
	max.z = -1073741824.0f;
	max.y = -1073741824.0f;
	max.x = -1073741824.0f;
	if (model_type != 0 && (species_table[model_type].load_flags & 1) == 0)
		return;
	/* PORT: the native OPT cache replaces the original handle lock, pointer
	 * relocation and unlock of g_loaded_model_handles[model_type]. */
	model = TieNativeOpt_Acquire((uint16_t)model_type);
#ifdef TIE_MODERN
	if (!model)
		return;
#endif
	for (index = 0; index < model->root_node_count; ++index) {
		if (model->root_nodes[index] && model->root_nodes[index]->type != TIE98_OPT_NODE_TEXTURE) {
			node = modelmesh_findfirstmeshvertsnode(model->root_nodes[index]);
			if (node) {
				corner = (const TieModelVec3f*)node->param2;
				if (node->param1 < 2)
					continue;
				corner += node->param1 - 2;
				if (corner->x < min.x)
					min.x = corner->x;
				if (corner->y < min.y)
					min.y = corner->y;
				if (corner->z < min.z)
					min.z = corner->z;
				++corner;
				if (corner->x > max.x)
					max.x = corner->x;
				if (corner->y > max.y)
					max.y = corner->y;
				if (corner->z > max.z)
					max.z = corner->z;
			}
		}
	}
	g_model_bounds_min[model_type].x = min.x;
	g_model_bounds_min[model_type].y = min.y;
	g_model_bounds_min[model_type].z = min.z;
	g_model_bounds_max[model_type].x = max.x;
	g_model_bounds_max[model_type].y = max.y;
	g_model_bounds_max[model_type].z = max.z;
	if (model_type != 0)
		g_model_bounds_cached[model_type] = 1;
}

// FUNCTION: TIE98 0x43B8C0
// ModelBounds_GetMaxExtent; same name in OpenXWA.
int modelbounds_getmaxextent(int model_type) {
	TieModelVec3f size;

	if (!g_model_bounds_cached[model_type])
		modelbounds_ensurecached(model_type);
	size.x = g_model_bounds_max[model_type].x - g_model_bounds_min[model_type].x;
	size.y = g_model_bounds_max[model_type].y - g_model_bounds_min[model_type].y;
	size.z = g_model_bounds_max[model_type].z - g_model_bounds_min[model_type].z;
	if (size.x >= size.y && size.x >= size.z)
		size.y = size.x;
	else if (size.z >= size.y && size.z >= size.x)
		size.y = size.z;
	return (int)size.y;
}

// FUNCTION: TIE98 0x43B970
// ModelBounds_GetMinX
int modelbounds_getminx(int model_type) {
	if (!g_model_bounds_cached[model_type])
		modelbounds_ensurecached(model_type);
	return (int)g_model_bounds_min[model_type].x;
}
// FUNCTION: TIE98 0x43B9A0
// ModelBounds_GetMinY
int modelbounds_getminy(int model_type) {
	if (!g_model_bounds_cached[model_type])
		modelbounds_ensurecached(model_type);
	return (int)g_model_bounds_min[model_type].y;
}
// FUNCTION: TIE98 0x43B9D0
// ModelBounds_GetMinZ
int modelbounds_getminz(int model_type) {
	if (!g_model_bounds_cached[model_type])
		modelbounds_ensurecached(model_type);
	return (int)g_model_bounds_min[model_type].z;
}
// FUNCTION: TIE98 0x43BA00
// ModelBounds_GetMaxX
int modelbounds_getmaxx(int model_type) {
	if (!g_model_bounds_cached[model_type])
		modelbounds_ensurecached(model_type);
	return (int)g_model_bounds_max[model_type].x;
}
// FUNCTION: TIE98 0x43BA30
// ModelBounds_GetMaxY
int modelbounds_getmaxy(int model_type) {
	if (!g_model_bounds_cached[model_type])
		modelbounds_ensurecached(model_type);
	return (int)g_model_bounds_max[model_type].y;
}
// FUNCTION: TIE98 0x43BA60
// ModelBounds_GetMaxZ
int modelbounds_getmaxz(int model_type) {
	if (!g_model_bounds_cached[model_type])
		modelbounds_ensurecached(model_type);
	return (int)g_model_bounds_max[model_type].z;
}

// FUNCTION: TIE98 0x43BA90
// ModelBounds_GetSizeX
int modelbounds_getsizex(int model_type) {
	if (!g_model_bounds_cached[model_type])
		modelbounds_ensurecached(model_type);
	return (int)(g_model_bounds_max[model_type].x - g_model_bounds_min[model_type].x);
}
// FUNCTION: TIE98 0x43BAD0
// ModelBounds_GetSizeY
int modelbounds_getsizey(uint16_t m) {
	const TieModelBounds* value;

	if (m != 0 && (species_table[m].load_flags & 1) == 0)
		return 0;
	value = &modelmesh_require_model(m)->bounds;
	return (int)(value->max.y - value->min.y);
}
// FUNCTION: TIE98 0x43BB10
// ModelBounds_GetSizeZ
int modelbounds_getsizez(int model_type) {
	if (!g_model_bounds_cached[model_type])
		modelbounds_ensurecached(model_type);
	return (int)(g_model_bounds_max[model_type].z - g_model_bounds_min[model_type].z);
}
