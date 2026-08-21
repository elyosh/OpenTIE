#ifndef TIE_RUNTIME_FLIGHT_ASSETS_MODEL_TYPES_H
#define TIE_RUNTIME_FLIGHT_ASSETS_MODEL_TYPES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct TieModelVec3f {
	float x;
	float y;
	float z;
} TieModelVec3f;

typedef struct TieModelBounds {
	TieModelVec3f min;
	TieModelVec3f max;
} TieModelBounds;

typedef struct TieModelRotationScale {
	TieModelVec3f pivot;
	TieModelVec3f rotation_axis;
	TieModelVec3f direction_axis;
	TieModelVec3f up_axis;
} TieModelRotationScale;

typedef struct TieModelHardpoint {
	int32_t type;
	TieModelVec3f position;
} TieModelHardpoint;

typedef struct TieModelCollisionFace {
	TieModelVec3f normal;
	int32_t vertex_indices[4];
} TieModelCollisionFace;

typedef struct TieModelCollisionVertexSet {
	TieModelBounds bounds;
	uint32_t vertex_count;
	const TieModelVec3f* vertices;
} TieModelCollisionVertexSet;

typedef struct TieModelCollisionNode {
	int16_t kind;
	int16_t rotation_index;
	int32_t first_child;
	uint16_t child_count;
	uint16_t _pad;
	int32_t reference_target;
	int32_t vertex_set;
	uint32_t first_face;
	uint32_t face_count;
} TieModelCollisionNode;

typedef struct TieModelMeshView {
	bool has_descriptor;
	int32_t mesh_type;
	int32_t explosion_type;
	/* Authored TIE98 MeshDescriptor geometry, independent of rendered vertices. */
	TieModelVec3f span;
	TieModelVec3f center;
	TieModelBounds bounds;
	int32_t target_id;
	TieModelVec3f target;
	bool has_rotation_scale;
	TieModelRotationScale rotation_scale;
	uint32_t hardpoint_count;
	const TieModelHardpoint* hardpoints;
	uint32_t vertex_count;
	const TieModelVec3f* vertices;
	int32_t collision_root;
} TieModelMeshView;

typedef struct TieFlightModelView {
	uint16_t model_type;
	uint16_t mesh_count;
	TieModelBounds bounds;
	float max_extent;
	int16_t bridge_mesh_index;
	uint16_t _pad;
	const TieModelMeshView* meshes;
	uint32_t collision_node_count;
	const TieModelCollisionNode* collision_nodes;
	uint32_t collision_vertex_set_count;
	const TieModelCollisionVertexSet* collision_vertex_sets;
	uint32_t collision_face_count;
	const TieModelCollisionFace* collision_faces;
} TieFlightModelView;

typedef struct TieFlightModelApi {
	void* context;
	bool (*begin_generation)(void* context, char* error, size_t error_capacity);
	const TieFlightModelView* (*acquire)(void* context, uint16_t model_type, char* error,
										 size_t error_capacity);
} TieFlightModelApi;

typedef enum Tie98OptNodeType {
	TIE98_OPT_NODE_NULL = -1,
	TIE98_OPT_NODE_GROUP = 0,
	TIE98_OPT_NODE_FACE_DATA = 1,
	TIE98_OPT_NODE_TRANSFORM = 2,
	TIE98_OPT_NODE_MESH_VERTICES = 3,
	TIE98_OPT_NODE_TRANSLATE = 4,
	TIE98_OPT_NODE_MATRIX = 5,
	TIE98_OPT_NODE_SCALE = 6,
	TIE98_OPT_NODE_REFERENCE = 7,
	TIE98_OPT_NODE_TYPE_10 = 10,
	TIE98_OPT_NODE_VERTEX_NORMALS = 11,
	TIE98_OPT_NODE_TEXTURE_COORDINATES = 13,
	TIE98_OPT_NODE_FACE_DATA_15 = 15,
	TIE98_OPT_NODE_FACE_DATA_16 = 16,
	TIE98_OPT_NODE_FACE_DATA_17 = 17,
	TIE98_OPT_NODE_FLAGS = 19,
	TIE98_OPT_NODE_TEXTURE = 20,
	TIE98_OPT_NODE_FACE_GROUP = 21,
	TIE98_OPT_NODE_HARDPOINT = 22,
	TIE98_OPT_NODE_ROTATION_SCALE = 23,
	TIE98_OPT_NODE_SWITCH = 24,
	TIE98_OPT_NODE_MESH_DESCRIPTOR = 25,
} Tie98OptNodeType;

typedef struct Tie98OptNode {
	const char* name;
	Tie98OptNodeType type;
	int32_t child_count;
	struct Tie98OptNode** children;
	intptr_t param1;
	const void* param2;
} Tie98OptNode;

typedef struct Tie98OptimizedPolyObject {
	uint16_t reserved;
	int32_t root_node_count;
	Tie98OptNode** root_nodes;
	const uint8_t* serialized_data;
	size_t serialized_size;
	uint32_t serialized_base;
} Tie98OptimizedPolyObject;

typedef struct Tie98OptApi {
	void* context;
	const Tie98OptimizedPolyObject* (*acquire)(void* context, uint16_t model_type, char* error,
											   size_t error_capacity);
	const Tie98OptimizedPolyObject* (*acquire_named)(void* context, const char* model_name,
													 uint16_t* out_model_type, char* error,
													 size_t error_capacity);
} Tie98OptApi;

const void* TieNativeOpt_ResolveAddress(const Tie98OptimizedPolyObject* model, uint32_t address, size_t size);

#endif
