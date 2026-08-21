/* Per-mission world-ambient authored document loader and serializer. */

#include "tie_remaster/flight/world_ambient.h"

#include "aeron/config_file.h"
#include "aeron/log.h"

#include <string.h>

static int TieFlightWorldAmbientYaml_ResolveSlot(const AeronConfigNode* node) {
	const char* text = AeronConfigNode_String(node, NULL);
	int64_t value;
	if (text && strcmp(text, "default") == 0)
		return 0;
	if (AeronConfigNode_Type(node) != AERON_CONFIG_INT)
		return -1;
	value = AeronConfigNode_Int(node, -1);
	return value >= 0 && value < WORLD_AMBIENT_BATTLE_MAX ? (int)value + 1 : -1;
}
static void TieFlightWorldAmbientYaml_ParseVec3(const AeronConfigNode* node, float out[3]) {
	int index;
	if (AeronConfigNode_Type(node) != AERON_CONFIG_SEQUENCE || AeronConfigNode_SequenceCount(node) != 3)
		return;
	for (index = 0; index < 3; ++index) {
		out[index] =
			(float)AeronConfigNode_Float(AeronConfigNode_SequenceGet(node, (size_t)index), out[index]);
	}
}
static void TieFlightWorldAmbientYaml_ParseBattleEntry(const AeronConfigNode* map,
													   TieWorldAmbientLibrary* library) {
	int slot = TieFlightWorldAmbientYaml_ResolveSlot(AeronConfigNode_MapGet(map, "id"));
	TieWorldAmbientCube candidate;
	if (slot < 0)
		return;
	candidate = library->slot[slot];
	TieFlightWorldAmbientYaml_ParseVec3(AeronConfigNode_MapGet(map, "pos_x"), candidate.pos_x);
	TieFlightWorldAmbientYaml_ParseVec3(AeronConfigNode_MapGet(map, "neg_x"), candidate.neg_x);
	TieFlightWorldAmbientYaml_ParseVec3(AeronConfigNode_MapGet(map, "pos_y"), candidate.pos_y);
	TieFlightWorldAmbientYaml_ParseVec3(AeronConfigNode_MapGet(map, "neg_y"), candidate.neg_y);
	TieFlightWorldAmbientYaml_ParseVec3(AeronConfigNode_MapGet(map, "pos_z"), candidate.pos_z);
	TieFlightWorldAmbientYaml_ParseVec3(AeronConfigNode_MapGet(map, "neg_z"), candidate.neg_z);
	TieFlightWorldAmbientYaml_ParseVec3(AeronConfigNode_MapGet(map, "sun_color"), candidate.sun_color);
	library->slot[slot] = candidate;
	library->authored_mask |= UINT64_C(1) << slot;
}

bool TieWorldAmbient_LoadYaml(AeronVfs* vfs, AeronVfsRoot root, const char* yaml_path,
							  TieWorldAmbientLibrary* inout) {
	AeronConfigFile* document = NULL;
	AeronConfigError error = { 0 };
	const AeronConfigNode* root_node;
	const AeronConfigNode* battles;
	TieWorldAmbientLibrary candidate;
	size_t index;

	if (!vfs || !yaml_path || !inout || !AeronConfigFile_LoadYamlEx(vfs, root, yaml_path, &document, &error))
		return false;
	root_node = AeronConfigFile_Root(document);
	if (AeronConfigNode_Type(root_node) != AERON_CONFIG_MAP) {
		Aeron_LogWarn("tie.assets", "%s: world ambient root is not a mapping", yaml_path);
		AeronConfigFile_Destroy(document);
		return false;
	}
	candidate = *inout;
	battles = AeronConfigNode_MapGet(root_node, "battles");
	if (AeronConfigNode_Type(battles) == AERON_CONFIG_SEQUENCE) {
		for (index = 0; index < AeronConfigNode_SequenceCount(battles); ++index) {
			const AeronConfigNode* entry = AeronConfigNode_SequenceGet(battles, index);
			if (AeronConfigNode_Type(entry) == AERON_CONFIG_MAP)
				TieFlightWorldAmbientYaml_ParseBattleEntry(entry, &candidate);
		}
	}
	AeronConfigFile_Destroy(document);
	*inout = candidate;
	return true;
}

static void TieFlightWorldAmbientYaml_MakeFloat(float value, AeronConfigValue* out) {
	out->type = AERON_CONFIG_FLOAT;
	out->value.float_value = value;
}

static void TieFlightWorldAmbientYaml_MakeVec3(const float source[3], AeronConfigValue components[3],
											   AeronConfigValue* out) {
	int index;
	for (index = 0; index < 3; ++index)
		TieFlightWorldAmbientYaml_MakeFloat(source[index], &components[index]);
	out->type = AERON_CONFIG_SEQUENCE;
	out->value.sequence.values = components;
	out->value.sequence.count = 3;
}

size_t TieWorldAmbient_EmitYaml(const TieWorldAmbientLibrary* in, char* out_buf, size_t out_cap) {
	static const char* const vector_names[] = { "pos_x", "neg_x", "pos_y",    "neg_y",
												"pos_z", "neg_z", "sun_color" };
	AeronConfigFile* document = NULL;
	AeronConfigError error = { 0 };
	AeronConfigValue battle_values[WORLD_AMBIENT_BATTLE_MAX + 1];
	AeronConfigMapValue battle_entries[WORLD_AMBIENT_BATTLE_MAX + 1][8];
	AeronConfigValue ids[WORLD_AMBIENT_BATTLE_MAX + 1];
	AeronConfigValue vectors[WORLD_AMBIENT_BATTLE_MAX + 1][7];
	AeronConfigValue components[WORLD_AMBIENT_BATTLE_MAX + 1][7][3];
	AeronConfigValue sequence = { .type = AERON_CONFIG_SEQUENCE };
	char* serialized = NULL;
	size_t serialized_size = 0;
	size_t count = 0;
	int slot;

	if (!in || !out_buf || out_cap == 0 ||
		!AeronConfigFile_CreateMap(AERON_VFS_ROOT_TEMP, "world_ambient.yaml", &document, &error))
		return 0;
	for (slot = 0; slot <= WORLD_AMBIENT_BATTLE_MAX; ++slot) {
		const TieWorldAmbientCube* cube;
		const float* sources[7];
		int vector_index;
		if (!(in->authored_mask & (UINT64_C(1) << slot)))
			continue;
		cube = &in->slot[slot];
		sources[0] = cube->pos_x;
		sources[1] = cube->neg_x;
		sources[2] = cube->pos_y;
		sources[3] = cube->neg_y;
		sources[4] = cube->pos_z;
		sources[5] = cube->neg_z;
		sources[6] = cube->sun_color;
		if (slot == 0) {
			ids[count].type = AERON_CONFIG_STRING;
			ids[count].value.string_value = "default";
		} else {
			ids[count].type = AERON_CONFIG_INT;
			ids[count].value.int_value = slot - 1;
		}
		battle_entries[count][0] = (AeronConfigMapValue) { "id", &ids[count] };
		for (vector_index = 0; vector_index < 7; ++vector_index) {
			TieFlightWorldAmbientYaml_MakeVec3(sources[vector_index], components[count][vector_index],
											   &vectors[count][vector_index]);
			battle_entries[count][vector_index + 1] =
				(AeronConfigMapValue) { vector_names[vector_index], &vectors[count][vector_index] };
		}
		battle_values[count].type = AERON_CONFIG_MAP;
		battle_values[count].value.map.entries = battle_entries[count];
		battle_values[count].value.map.count = 8;
		++count;
	}
	sequence.value.sequence.values = battle_values;
	sequence.value.sequence.count = count;
	if (!AeronConfigFile_SetValue(document, "battles", &sequence, &error) ||
		!AeronConfigFile_SerializeYaml(document, &serialized, &serialized_size, &error) ||
		serialized_size >= out_cap) {
		AeronConfigFile_FreeSerialized(serialized);
		AeronConfigFile_Destroy(document);
		return 0;
	}
	memcpy(out_buf, serialized, serialized_size + 1);
	AeronConfigFile_FreeSerialized(serialized);
	AeronConfigFile_Destroy(document);
	return serialized_size;
}
