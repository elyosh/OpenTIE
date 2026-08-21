#include "tie_runtime/flight_assets/model_cache.h"

#include "aeron/asset/opt_model.h"
#include "aeron/log.h"
#include "tie_runtime/flight_assets/store.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TIE_MODEL_COUNT 161
#define TIE_MODEL_COMPONENT_CAPACITY 39
#define TIE_Q15_SCALE 32768.0f

typedef struct TieFlightModelSlot {
	bool loaded;
	AeronFlightModel model;
	TieFlightModelView view;
	TieModelMeshView* meshes;
	TieModelVec3f* positions;
	TieModelHardpoint* hardpoints;
	TieModelCollisionFace* faces;
	TieModelCollisionVertexSet* vertex_sets;
	TieModelCollisionNode* nodes;
} TieFlightModelSlot;

struct TieFlightModelCache {
	TieFlightAssetStore* store;
	TieFlightModelSlot slots[TIE_MODEL_COUNT];
};

static bool TieModelCache_CacheError(TieFlightModelCache* cache, char* error, size_t capacity,
									 uint16_t model_type, const char* message) {
	if (error && capacity)
		snprintf(error, capacity, "flight model source %s: model %u: %s", cache->store->source.name,
				 (unsigned)model_type, message);
	return false;
}

static void TieModelCache_ReleaseSlot(TieFlightModelSlot* slot) {
	if (!slot)
		return;
	Aeron_FlightModelFree(&slot->model);
	free(slot->meshes);
	free(slot->positions);
	free(slot->hardpoints);
	free(slot->faces);
	free(slot->vertex_sets);
	free(slot->nodes);
	memset(slot, 0, sizeof *slot);
}

static bool TieModelCache_BeginGeneration(void* context, char* error, size_t error_capacity) {
	TieFlightModelCache* cache = context;
	if (error && error_capacity)
		error[0] = '\0';
	if (!cache)
		return false;
	TieFlightModelCache_Clear(cache);
	return true;
}

static TieModelVec3f TieModelCache_TiePosition(AeronFlightVec3 source) {
	return (TieModelVec3f) {
		source.x * AERON_OPT_UNITS_PER_METER,
		source.y * AERON_OPT_UNITS_PER_METER,
		source.z * AERON_OPT_UNITS_PER_METER,
	};
}

static TieModelVec3f TieModelCache_TieQ15Direction(AeronFlightVec3 source) {
	return (TieModelVec3f) {
		source.x * TIE_Q15_SCALE,
		source.y * TIE_Q15_SCALE,
		source.z * TIE_Q15_SCALE,
	};
}

static TieModelBounds TieModelCache_TieBounds(AeronFlightBounds source) {
	return (TieModelBounds) {
		.min = TieModelCache_TiePosition(source.min),
		.max = TieModelCache_TiePosition(source.max),
	};
}

static bool TieModelCache_AllocateView(TieFlightModelCache* cache, TieFlightModelSlot* slot,
									   uint16_t model_type) {
	const AeronFlightModel* model = &slot->model;
	if (model->component_count > TIE_MODEL_COMPONENT_CAPACITY)
		return TieModelCache_CacheError(cache, NULL, 0, model_type,
										"model exceeds the recovered 39-component craft-state capacity");
	uint32_t position_count = 0;
	uint32_t hardpoint_count = 0;
	uint32_t face_count = 0;
	uint32_t node_count = 0;
	for (uint32_t index = 0; index < model->component_count; ++index) {
		const AeronFlightComponent* component = &model->components[index];
		position_count += component->topology.position_count;
		hardpoint_count += component->hardpoint_count;
		face_count += component->topology.face_count;
		node_count += component->has_rotation ? 4u : 3u;
	}
	slot->meshes = calloc(model->component_count, sizeof *slot->meshes);
	slot->positions = calloc(position_count, sizeof *slot->positions);
	slot->hardpoints = calloc(hardpoint_count, sizeof *slot->hardpoints);
	slot->faces = calloc(face_count, sizeof *slot->faces);
	slot->vertex_sets = calloc(model->component_count, sizeof *slot->vertex_sets);
	slot->nodes = calloc(node_count, sizeof *slot->nodes);
	if ((model->component_count && (!slot->meshes || !slot->vertex_sets)) ||
		(position_count && !slot->positions) || (hardpoint_count && !slot->hardpoints) ||
		(face_count && !slot->faces) || (node_count && !slot->nodes))
		return TieModelCache_CacheError(cache, NULL, 0, model_type, "out of memory for TIE model view");
	return true;
}

static void TieModelCache_BuildCollisionNodes(TieFlightModelSlot* slot, uint32_t component_index,
											  uint32_t* node_write, uint32_t first_face) {
	const AeronFlightComponent* component = &slot->model.components[component_index];
	TieModelMeshView* mesh = &slot->meshes[component_index];
	const uint32_t root = *node_write;
	const uint32_t child_count = component->has_rotation ? 3u : 2u;
	slot->nodes[root] = (TieModelCollisionNode) {
		.kind = 0,
		.first_child = (int32_t)(root + 1),
		.child_count = (uint16_t)child_count,
		.vertex_set = -1,
	};
	uint32_t write = root + 1;
	if (component->has_rotation) {
		slot->nodes[write++] = (TieModelCollisionNode) {
			.kind = 23,
			.rotation_index = (int16_t)component_index,
			.vertex_set = -1,
		};
	}
	slot->nodes[write++] = (TieModelCollisionNode) {
		.kind = 3,
		.vertex_set = (int32_t)component_index,
	};
	slot->nodes[write++] = (TieModelCollisionNode) {
		.kind = 1,
		.vertex_set = (int32_t)component_index,
		.first_face = first_face,
		.face_count = component->topology.face_count,
	};
	mesh->collision_root = (int32_t)root;
	*node_write = write;
}

static bool TieModelCache_BuildView(TieFlightModelCache* cache, TieFlightModelSlot* slot, uint16_t model_type,
									char* error, size_t error_capacity) {
	if (!TieModelCache_AllocateView(cache, slot, model_type))
		return TieModelCache_CacheError(cache, error, error_capacity, model_type,
										"could not allocate TIE model view");
	uint32_t position_write = 0;
	uint32_t hardpoint_write = 0;
	uint32_t face_write = 0;
	uint32_t node_write = 0;
	for (uint32_t index = 0; index < slot->model.component_count; ++index) {
		const AeronFlightComponent* source = &slot->model.components[index];
		TieModelMeshView* mesh = &slot->meshes[index];
		mesh->has_descriptor = source->has_descriptor;
		mesh->mesh_type = source->mesh_type;
		mesh->explosion_type = (int32_t)source->explosion_flags;
		mesh->bounds = TieModelCache_TieBounds(source->descriptor_bounds);
		mesh->span = TieModelCache_TiePosition(source->descriptor_span);
		mesh->center = TieModelCache_TiePosition(source->descriptor_center);
		mesh->target_id = source->target_id;
		mesh->target = TieModelCache_TiePosition(source->target);
		mesh->has_rotation_scale = source->has_rotation;
		if (source->has_rotation) {
			mesh->rotation_scale.pivot = TieModelCache_TiePosition(source->rotation.pivot);
			mesh->rotation_scale.rotation_axis =
				TieModelCache_TieQ15Direction(source->rotation.rotation_axis);
			mesh->rotation_scale.direction_axis =
				TieModelCache_TieQ15Direction(source->rotation.direction_axis);
			mesh->rotation_scale.up_axis = TieModelCache_TieQ15Direction(source->rotation.up_axis);
		}
		mesh->vertex_count = source->topology.position_count;
		mesh->vertices = source->topology.position_count ? &slot->positions[position_write] : NULL;
		for (uint32_t vertex = 0; vertex < source->topology.position_count; ++vertex)
			slot->positions[position_write + vertex] =
				TieModelCache_TiePosition(source->topology.positions[vertex]);
		slot->vertex_sets[index] = (TieModelCollisionVertexSet) {
			.bounds = TieModelCache_TieBounds(source->bounds),
			.vertex_count = mesh->vertex_count,
			.vertices = mesh->vertices,
		};
		mesh->hardpoint_count = source->hardpoint_count;
		mesh->hardpoints = source->hardpoint_count ? &slot->hardpoints[hardpoint_write] : NULL;
		for (uint32_t hardpoint = 0; hardpoint < source->hardpoint_count; ++hardpoint) {
			slot->hardpoints[hardpoint_write + hardpoint].type = source->hardpoints[hardpoint].type;
			slot->hardpoints[hardpoint_write + hardpoint].position =
				TieModelCache_TiePosition(source->hardpoints[hardpoint].position);
		}
		for (uint32_t face = 0; face < source->topology.face_count; ++face) {
			const AeronFlightFace* source_face = &source->topology.faces[face];
			TieModelCollisionFace* target = &slot->faces[face_write + face];
			/* OPT collision normals remain unit floats; Q15 applies only to
			 * RotationScale direction vectors. */
			target->normal = (TieModelVec3f) {
				.x = source_face->normal.x,
				.y = source_face->normal.y,
				.z = source_face->normal.z,
			};
			for (uint32_t corner = 0; corner < 3; ++corner)
				target->vertex_indices[corner] = (int32_t)source_face->indices[corner];
			target->vertex_indices[3] = -1;
		}
		TieModelCache_BuildCollisionNodes(slot, index, &node_write, face_write);
		position_write += source->topology.position_count;
		hardpoint_write += source->hardpoint_count;
		face_write += source->topology.face_count;
	}
	slot->view = (TieFlightModelView) {
		.model_type = model_type,
		.mesh_count = (uint16_t)slot->model.component_count,
		.bounds = TieModelCache_TieBounds(slot->model.bounds),
		.max_extent = slot->model.max_extent * AERON_OPT_UNITS_PER_METER,
		.bridge_mesh_index = (int16_t)slot->model.bridge_component,
		.meshes = slot->meshes,
		.collision_node_count = node_write,
		.collision_nodes = slot->nodes,
		.collision_vertex_set_count = slot->model.component_count,
		.collision_vertex_sets = slot->vertex_sets,
		.collision_face_count = face_write,
		.collision_faces = slot->faces,
	};
	return true;
}

static bool TieModelCache_LoadModelFromStore(TieFlightModelCache* cache, TieFlightAssetStore* store,
											 uint16_t model_type, TieFlightModelSlot* slot, char* error,
											 size_t error_capacity) {
	uint8_t* bytes = NULL;
	size_t size = 0;
	const TieFlightAssetEntry* entry = NULL;
	if (!TieFlightAssetStore_ReadModel(store, model_type, &bytes, &size, &entry, error, error_capacity))
		return false;
	bool built = false;
	if (TieFlightAssetConfig_IsRemastered(&store->config)) {
		built = Aeron_FlightModelBuildMemory(bytes, size, entry->path, &slot->model);
	} else {
		AeronOptModelError opt_error = { 0 };
		built = Aeron_OptModelBuildMemory(bytes, size, entry->path,
										  &(AeronOptModelBuildOptions) {
											  .smooth_angle_degrees = store->config.smooth_angle_degrees,
											  .emissive_strength = store->config.opt_emissive_strength,
											  .emissive = true,
										  },
										  &slot->model, &opt_error);
		if (!built)
			TieModelCache_CacheError(cache, error, error_capacity, model_type,
									 opt_error.message[0] ? opt_error.message : "OPT construction failed");
	}
	free(bytes);
	if (!built) {
		if (TieFlightAssetConfig_IsRemastered(&store->config))
			TieModelCache_CacheError(cache, error, error_capacity, model_type, "invalid cooked flight GLB");
		return false;
	}
	if (!TieModelCache_BuildView(cache, slot, model_type, error, error_capacity))
		return false;
	slot->loaded = true;
	return true;
}

static bool TieModelCache_LoadModel(TieFlightModelCache* cache, uint16_t model_type, TieFlightModelSlot* slot,
									char* error, size_t error_capacity) {
	const bool primary_absent = TieFlightAssetConfig_IsRemastered(&cache->store->config) &&
								!TieFlightAssetStore_HasModel(cache->store, model_type);
	if (!primary_absent &&
		TieModelCache_LoadModelFromStore(cache, cache->store, model_type, slot, error, error_capacity)) {
		Aeron_LogInfo("tie.assets", "species %u model source: %s", model_type, cache->store->source.name);
		return true;
	}
	TieModelCache_ReleaseSlot(slot);
	/* Invalid content is an error. Only an absent remastered model may
	 * select the original runtime-OPT provider. */
	if (!primary_absent)
		return false;
	if (!cache->store->fallback) {
		TieModelCache_CacheError(cache, error, error_capacity, model_type,
								 "remastered model is absent and no original fallback is available");
		return false;
	}
	if (error && error_capacity)
		error[0] = '\0';
	if (!TieModelCache_LoadModelFromStore(cache, cache->store->fallback, model_type, slot, error,
										  error_capacity))
		return false;
	Aeron_LogInfo("tie.assets", "species %u model source: %s (fallback)", model_type,
				  cache->store->fallback->source.name);
	return true;
}

static const TieFlightModelView* TieFlightModelCache_Acquire(void* context, uint16_t model_type, char* error,
															 size_t error_capacity) {
	TieFlightModelCache* cache = context;
	if (error && error_capacity)
		error[0] = '\0';
	if (!cache || model_type >= TIE_MODEL_COUNT)
		return NULL;
	TieFlightModelSlot* slot = &cache->slots[model_type];
	if (!slot->loaded && !TieModelCache_LoadModel(cache, model_type, slot, error, error_capacity)) {
		TieModelCache_ReleaseSlot(slot);
		return NULL;
	}
	return &slot->view;
}

TieFlightModelCache* TieFlightModelCache_Create(TieFlightAssetStore* store, char* error,
												size_t error_capacity) {
	if (error && error_capacity)
		error[0] = '\0';
	if (!store)
		return NULL;
	TieFlightModelCache* cache = calloc(1, sizeof *cache);
	if (!cache)
		return NULL;
	cache->store = store;
	return cache;
}

void TieFlightModelCache_Clear(TieFlightModelCache* cache) {
	if (!cache)
		return;
	for (uint16_t index = 0; index < TIE_MODEL_COUNT; ++index)
		TieModelCache_ReleaseSlot(&cache->slots[index]);
}

void TieFlightModelCache_Destroy(TieFlightModelCache* cache) {
	if (!cache)
		return;
	TieFlightModelCache_Clear(cache);
	free(cache);
}

TieFlightModelApi TieFlightModelCache_Api(TieFlightModelCache* cache) {
	return (TieFlightModelApi) {
		.context = cache,
		.begin_generation = TieModelCache_BeginGeneration,
		.acquire = TieFlightModelCache_Acquire,
	};
}

const AeronFlightModel* TieFlightModelCache_AcquireModel(TieFlightModelCache* cache, uint16_t model_type,
														 char* error, size_t error_capacity) {
	if (!TieFlightModelCache_Acquire(cache, model_type, error, error_capacity))
		return NULL;
	return &cache->slots[model_type].model;
}

void TieFlightModelCache_ReleaseRenderData(TieFlightModelCache* cache, uint16_t model_type) {
	if (cache && model_type < TIE_MODEL_COUNT && cache->slots[model_type].loaded)
		Aeron_FlightModelReleaseRenderData(&cache->slots[model_type].model);
}
