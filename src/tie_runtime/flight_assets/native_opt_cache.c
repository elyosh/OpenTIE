#include "tie_runtime/flight_assets/native_opt_cache.h"

#include "aeron/aeron.h"
#include "opt.h"
#include "tie_runtime/flight_assets/store.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* PORT: host-owned native OPT loading and lifetime. Recovered renderer code
 * consumes the resulting source-shaped node graph through Tie98OptApi. */

#define TIE98_OPT_MODEL_COUNT 161

typedef struct Tie98NativeOptNode {
	Tie98OptNode node;
	uint32_t serialized_address;
} Tie98NativeOptNode;

typedef struct Tie98NativeOpt {
	Tie98OptimizedPolyObject model;
	uint8_t* storage;
	Tie98NativeOptNode** nodes;
	int node_count;
	int node_capacity;
	uint32_t root_nodes_address;
} Tie98NativeOpt;

struct Tie98NativeOptCache {
	TieFlightAssetStore* store;
	Tie98NativeOpt* models[TIE98_OPT_MODEL_COUNT];
	char* failures[TIE98_OPT_MODEL_COUNT];
};

static void TieNativeOptCache_SetError(char* error, size_t capacity, const char* format, ...) {
	if (!error || capacity == 0)
		return;
	va_list arguments;
	va_start(arguments, format);
	vsnprintf(error, capacity, format, arguments);
	va_end(arguments);
}

static uint16_t TieNativeOptCache_ReadU16(const uint8_t* data) {
	return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static uint32_t TieNativeOptCache_ReadU32(const uint8_t* data) {
	return (uint32_t)data[0] | ((uint32_t)data[1] << 8) | ((uint32_t)data[2] << 16) |
		   ((uint32_t)data[3] << 24);
}

static int32_t TieNativeOptCache_ReadI32(const uint8_t* data) {
	return (int32_t)TieNativeOptCache_ReadU32(data);
}

static void TieNativeOptCache_WriteU32(uint8_t* data, uint32_t value) {
	data[0] = (uint8_t)value;
	data[1] = (uint8_t)(value >> 8);
	data[2] = (uint8_t)(value >> 16);
	data[3] = (uint8_t)(value >> 24);
}

static int TieNativeOptCache_RepairModifiedCorvetteTail(uint16_t model_type, uint8_t** bytes, size_t* size) {
	/* The shipped CORVTA.OPT ends at gradient1.y of its final two-face block.
	 * Reconstruct gradient1.z from that face's geometry and texture coordinates. */
	if (model_type != TIE_SPECIES_MODIFIED_CORVETTE || !bytes || !size || !*bytes || *size != 404973u)
		return 1;

	uint8_t* data = *bytes;
	const size_t node_offset = *size - 224u;
	const uint32_t serialized_base = TieNativeOptCache_ReadU32(data + 8);
	const uint32_t face_data_address = TieNativeOptCache_ReadU32(data + node_offset + 20);
	if (TieNativeOptCache_ReadI32(data) != -2 ||
		TieNativeOptCache_ReadI32(data + 4) != (int32_t)(*size - 8u) ||
		TieNativeOptCache_ReadI32(data + node_offset + 4) != TIE98_OPT_NODE_FACE_DATA ||
		TieNativeOptCache_ReadI32(data + node_offset + 16) != 2 || face_data_address < serialized_base ||
		8u + (size_t)(face_data_address - serialized_base) != *size - 200u ||
		TieNativeOptCache_ReadU32(data + *size - 8u) != 0x40edd8b1u ||
		TieNativeOptCache_ReadU32(data + *size - 4u) != 0x4142f8b0u)
		return 1;

	data = (uint8_t*)realloc(data, *size + sizeof(uint32_t));
	if (!data)
		return 0;
	TieNativeOptCache_WriteU32(data + *size, 0xc34af29cu);
	TieNativeOptCache_WriteU32(data + 4, (uint32_t)(*size - 8u + sizeof(uint32_t)));
	*bytes = data;
	*size += sizeof(uint32_t);
	return 1;
}

static const Tie98OptimizedPolyObject*
TieNativeOptCache_Failure(Tie98NativeOptCache* cache, uint16_t model_type, const TieFlightAssetEntry* entry,
						  const char* detail, char* error, size_t error_capacity) {
	char message[768];
	const char* symbol = tie_species_symbolic_name(model_type);
	const char* path = entry ? entry->path : "unknown path";
	snprintf(message, sizeof message, "TIE98 native OPT species %u (%s, %s): %s", model_type,
			 symbol ? symbol : "unknown species", path,
			 detail && detail[0] ? detail : "model loading failed");
	if (!cache->failures[model_type]) {
		const size_t length = strlen(message) + 1;
		cache->failures[model_type] = (char*)malloc(length);
		if (cache->failures[model_type])
			memcpy(cache->failures[model_type], message, length);
		Aeron_RequestFatalError("Flight Asset Error", message);
	}
	TieNativeOptCache_SetError(error, error_capacity, "%s",
							   cache->failures[model_type] ? cache->failures[model_type] : message);
	return NULL;
}

static void TieNativeOptCache_NativeOptFree(Tie98NativeOpt* native) {
	if (!native)
		return;
	for (int i = 0; i < native->node_count; ++i) {
		free(native->nodes[i]->node.children);
		free(native->nodes[i]);
	}
	free(native->nodes);
	free(native->model.root_nodes);
	free(native->storage);
	free(native);
}

static Tie98NativeOptNode* TieNativeOptCache_FindNode(const Tie98NativeOpt* native, uint32_t address) {
	for (int i = 0; i < native->node_count; ++i)
		if (native->nodes[i]->serialized_address == address)
			return native->nodes[i];
	return NULL;
}

static int TieNativeOptCache_AppendNode(Tie98NativeOpt* native, Tie98NativeOptNode* node) {
	if (native->node_count == native->node_capacity) {
		const int capacity = native->node_capacity ? native->node_capacity * 2 : 64;
		Tie98NativeOptNode** nodes =
			(Tie98NativeOptNode**)realloc(native->nodes, (size_t)capacity * sizeof *nodes);
		if (!nodes)
			return 0;
		native->nodes = nodes;
		native->node_capacity = capacity;
	}
	native->nodes[native->node_count++] = node;
	return 1;
}

static Tie98NativeOptNode* TieNativeOptCache_ParseNode(Tie98NativeOpt* native, uint32_t address) {
	Tie98NativeOptNode* existing = TieNativeOptCache_FindNode(native, address);
	if (existing)
		return existing;

	const uint8_t* raw = (const uint8_t*)TieNativeOpt_ResolveAddress(&native->model, address, 24);
	if (!raw)
		return NULL;

	const uint32_t name_address = TieNativeOptCache_ReadU32(raw);
	const int32_t child_count = TieNativeOptCache_ReadI32(raw + 8);
	const uint32_t children_address = TieNativeOptCache_ReadU32(raw + 12);
	const int32_t param1 = TieNativeOptCache_ReadI32(raw + 16);
	const uint32_t param2_address = TieNativeOptCache_ReadU32(raw + 20);
	if (child_count < 0)
		return NULL;

	Tie98NativeOptNode* result = (Tie98NativeOptNode*)calloc(1, sizeof *result);
	if (!result)
		return NULL;
	result->serialized_address = address;
	result->node.name = (const char*)TieNativeOpt_ResolveAddress(&native->model, name_address, 1);
	result->node.type = (Tie98OptNodeType)TieNativeOptCache_ReadI32(raw + 4);
	result->node.child_count = child_count;
	result->node.param1 = param1;
	result->node.param2 = TieNativeOpt_ResolveAddress(&native->model, param2_address, 1);
	if (!TieNativeOptCache_AppendNode(native, result)) {
		free(result);
		return NULL;
	}

	if (result->node.type == TIE98_OPT_NODE_REFERENCE && param1 != 0) {
		/* Non-address values leave the reference unresolved for the original
		 * renderer's param2 name lookup. */
		result->node.param1 = 0;
		if (TieNativeOpt_ResolveAddress(&native->model, (uint32_t)param1, 24)) {
			Tie98NativeOptNode* reference = TieNativeOptCache_ParseNode(native, (uint32_t)param1);
			if (reference)
				result->node.param1 = (intptr_t)&reference->node;
		}
	}

	if (child_count > 0) {
		const uint8_t* child_table = (const uint8_t*)TieNativeOpt_ResolveAddress(
			&native->model, children_address, (size_t)child_count * 4);
		if (!child_table)
			return NULL;
		result->node.children = (Tie98OptNode**)calloc((size_t)child_count, sizeof *result->node.children);
		if (!result->node.children)
			return NULL;
		for (int i = 0; i < child_count; ++i) {
			const uint32_t child_address = TieNativeOptCache_ReadU32(child_table + (size_t)i * 4);
			if (!child_address)
				continue;
			Tie98NativeOptNode* child = TieNativeOptCache_ParseNode(native, child_address);
			if (!child)
				return NULL;
			result->node.children[i] = &child->node;
		}
	}
	return result;
}

static Tie98NativeOpt* TieNativeOptCache_Parse(uint8_t* bytes, size_t size, char* error,
											   size_t error_capacity) {
	if (size < 18) {
		TieNativeOptCache_SetError(error, error_capacity, "OPT file is too short");
		free(bytes);
		return NULL;
	}

	const int32_t marker = TieNativeOptCache_ReadI32(bytes);
	const size_t header_size = marker > 0 ? 4u : 8u;
	const int version = marker > 0 ? 0 : -marker;
	const int32_t payload_size = marker > 0 ? marker : TieNativeOptCache_ReadI32(bytes + 4);
	if (version != 2 || payload_size < 14 || (size_t)payload_size > size - header_size) {
		TieNativeOptCache_SetError(error, error_capacity, "unsupported OPT header: version %d, payload %d",
								   version, payload_size);
		free(bytes);
		return NULL;
	}

	Tie98NativeOpt* native = (Tie98NativeOpt*)calloc(1, sizeof *native);
	if (!native) {
		TieNativeOptCache_SetError(error, error_capacity, "out of memory allocating native OPT");
		free(bytes);
		return NULL;
	}
	native->storage = bytes;
	native->model.serialized_data = bytes + header_size;
	native->model.serialized_size = (size_t)payload_size;
	native->model.serialized_base = TieNativeOptCache_ReadU32(native->model.serialized_data);
	native->model.reserved = TieNativeOptCache_ReadU16(native->model.serialized_data + 4);
	native->model.root_node_count = TieNativeOptCache_ReadI32(native->model.serialized_data + 6);
	native->root_nodes_address = TieNativeOptCache_ReadU32(native->model.serialized_data + 10);
	if (native->model.root_node_count < 0) {
		TieNativeOptCache_SetError(error, error_capacity, "negative OPT root-node count");
		TieNativeOptCache_NativeOptFree(native);
		return NULL;
	}

	const uint8_t* root_table = (const uint8_t*)TieNativeOpt_ResolveAddress(
		&native->model, native->root_nodes_address, (size_t)native->model.root_node_count * sizeof(uint32_t));
	if (native->model.root_node_count && !root_table) {
		TieNativeOptCache_SetError(error, error_capacity, "invalid OPT root-node table");
		TieNativeOptCache_NativeOptFree(native);
		return NULL;
	}
	native->model.root_nodes =
		(Tie98OptNode**)calloc((size_t)native->model.root_node_count, sizeof *native->model.root_nodes);
	if (native->model.root_node_count && !native->model.root_nodes) {
		TieNativeOptCache_SetError(error, error_capacity, "out of memory allocating OPT roots");
		TieNativeOptCache_NativeOptFree(native);
		return NULL;
	}
	for (int i = 0; i < native->model.root_node_count; ++i) {
		const uint32_t address = TieNativeOptCache_ReadU32(root_table + (size_t)i * 4);
		if (!address)
			continue;
		Tie98NativeOptNode* node = TieNativeOptCache_ParseNode(native, address);
		if (!node) {
			TieNativeOptCache_SetError(error, error_capacity, "invalid OPT node graph at root %d", i);
			TieNativeOptCache_NativeOptFree(native);
			return NULL;
		}
		native->model.root_nodes[i] = &node->node;
	}
	return native;
}

static const Tie98OptimizedPolyObject* TieNativeOptCache_Acquire(void* context, uint16_t model_type,
																 char* error, size_t error_capacity) {
	Tie98NativeOptCache* cache = (Tie98NativeOptCache*)context;
	if (!cache || model_type >= TIE98_OPT_MODEL_COUNT) {
		TieNativeOptCache_SetError(error, error_capacity, "invalid native OPT model %u", model_type);
		Aeron_RequestFatalError("Flight Asset Error",
								"The game requested an invalid TIE98 native OPT model.");
		return NULL;
	}
	if (cache->models[model_type])
		return &cache->models[model_type]->model;
	if (cache->failures[model_type]) {
		TieNativeOptCache_SetError(error, error_capacity, "%s", cache->failures[model_type]);
		return NULL;
	}

	uint8_t* bytes = NULL;
	size_t size = 0;
	const TieFlightAssetEntry* entry = TieFlightAssets_Find(cache->store->catalog, model_type);
	char detail[512];
	if (!TieFlightAssetStore_ReadModel(cache->store, model_type, &bytes, &size, NULL, detail, sizeof detail))
		return TieNativeOptCache_Failure(cache, model_type, entry, detail, error, error_capacity);

	if (size >= 8 && TieNativeOptCache_ReadI32(bytes) == -1) {
		uint8_t* upgraded = NULL;
		size_t upgraded_size = 0;
		opt_error_t upgrade_error = { { 0 } };
		if (!opt_upgrade_v1_memory(bytes, size, &upgraded, &upgraded_size, &upgrade_error)) {
			free(bytes);
			return TieNativeOptCache_Failure(cache, model_type, entry, upgrade_error.msg, error,
											 error_capacity);
		}
		free(bytes);
		bytes = upgraded;
		size = upgraded_size;
	}
	if (!TieNativeOptCache_RepairModifiedCorvetteTail(model_type, &bytes, &size)) {
		free(bytes);
		return TieNativeOptCache_Failure(cache, model_type, entry,
										 "out of memory repairing the CORVTA.OPT face-data tail", error,
										 error_capacity);
	}

	Tie98NativeOpt* native = TieNativeOptCache_Parse(bytes, size, detail, sizeof detail);
	if (!native)
		return TieNativeOptCache_Failure(cache, model_type, entry, detail, error, error_capacity);
	cache->models[model_type] = native;
	return &native->model;
}

static const Tie98OptimizedPolyObject* TieNativeOptCache_AcquireNamed(void* context, const char* model_name,
																	  uint16_t* out_model_type, char* error,
																	  size_t error_capacity) {
	Tie98NativeOptCache* cache = (Tie98NativeOptCache*)context;
	if (!cache || !model_name || !model_name[0]) {
		TieNativeOptCache_SetError(error, error_capacity, "invalid named native OPT request");
		return NULL;
	}

	const char* requested = model_name;
	for (const char* cursor = model_name; *cursor; ++cursor)
		if (*cursor == '/' || *cursor == '\\')
			requested = cursor + 1;
	size_t requested_length = strlen(requested);
	if (requested_length > 4 && requested[requested_length - 4] == '.' &&
		(requested[requested_length - 3] == 'o' || requested[requested_length - 3] == 'O') &&
		(requested[requested_length - 2] == 'p' || requested[requested_length - 2] == 'P') &&
		(requested[requested_length - 1] == 't' || requested[requested_length - 1] == 'T'))
		requested_length -= 4;

	for (uint16_t model_type = 0; model_type < TIE98_OPT_MODEL_COUNT; ++model_type) {
		const TieFlightAssetEntry* entry = TieFlightAssets_Find(cache->store->catalog, model_type);
		if (!entry)
			continue;
		const char* candidate = entry->path;
		for (const char* cursor = entry->path; *cursor; ++cursor)
			if (*cursor == '/' || *cursor == '\\')
				candidate = cursor + 1;
		size_t candidate_length = strlen(candidate);
		if (candidate_length > 4 && candidate[candidate_length - 4] == '.')
			candidate_length -= 4;
		if (candidate_length != requested_length)
			continue;
		size_t index = 0;
		while (index < requested_length) {
			char left = requested[index];
			char right = candidate[index];
			if (left >= 'a' && left <= 'z')
				left = (char)(left - 'a' + 'A');
			if (right >= 'a' && right <= 'z')
				right = (char)(right - 'a' + 'A');
			if (left != right)
				break;
			++index;
		}
		if (index != requested_length)
			continue;
		if (out_model_type)
			*out_model_type = model_type;
		return TieNativeOptCache_Acquire(cache, model_type, error, error_capacity);
	}

	char message[512];
	snprintf(message, sizeof message, "catalog has no model named '%s'", model_name);
	TieNativeOptCache_SetError(error, error_capacity, "%s", message);
	Aeron_RequestFatalError("Flight Asset Error", message);
	return NULL;
}

Tie98NativeOptCache* Tie98NativeOptCache_Create(TieFlightAssetStore* store, char* error,
												size_t error_capacity) {
	if (!store || TieFlightAssetConfig_IsTie95(&store->config) ||
		TieFlightAssetConfig_IsRemastered(&store->config)) {
		if (error && error_capacity)
			TieNativeOptCache_SetError(error, error_capacity,
									   "native TIE98 OPT cache requires tie98 flight assets");
		return NULL;
	}
	Tie98NativeOptCache* cache = (Tie98NativeOptCache*)calloc(1, sizeof *cache);
	if (!cache) {
		if (error && error_capacity)
			TieNativeOptCache_SetError(error, error_capacity,
									   "out of memory allocating native TIE98 OPT cache");
		return NULL;
	}
	cache->store = store;
	return cache;
}

void Tie98NativeOptCache_Clear(Tie98NativeOptCache* cache) {
	if (!cache)
		return;
	for (int i = 0; i < TIE98_OPT_MODEL_COUNT; ++i) {
		TieNativeOptCache_NativeOptFree(cache->models[i]);
		cache->models[i] = NULL;
		free(cache->failures[i]);
		cache->failures[i] = NULL;
	}
}

void Tie98NativeOptCache_Destroy(Tie98NativeOptCache* cache) {
	if (!cache)
		return;
	Tie98NativeOptCache_Clear(cache);
	free(cache);
}

Tie98OptApi Tie98NativeOptCache_Api(Tie98NativeOptCache* cache) {
	return cache ? (Tie98OptApi) { .context = cache,
								   .acquire = TieNativeOptCache_Acquire,
								   .acquire_named = TieNativeOptCache_AcquireNamed }
				 : (Tie98OptApi) { 0 };
}
