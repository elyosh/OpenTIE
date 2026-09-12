#include "tie_app/config/controller_config.h"
#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool TieAppConfig_ConfigError(char* error, size_t capacity, const char* format, ...) {
	va_list arguments;
	if (error && capacity) {
		va_start(arguments, format);
		vsnprintf(error, capacity, format, arguments);
		va_end(arguments);
	}
	return false;
}

static const AeronConfigNode* TieAppConfig_RequiredNode(const AeronConfigFile* document, const char* path,
														AeronConfigNodeType type, char* error,
														size_t capacity) {
	const AeronConfigNode* node = AeronConfigFile_GetNode(document, path);
	if (!node)
		TieAppConfig_ConfigError(error, capacity, "missing required setting '%s'", path);
	else if (AeronConfigNode_Type(node) != type)
		TieAppConfig_ConfigError(error, capacity, "invalid setting '%s' at %s:%d:%d", path,
								 AeronConfigNode_SourcePath(node), AeronConfigNode_Line(node),
								 AeronConfigNode_Column(node));
	else
		return node;
	return NULL;
}

static bool TieAppConfig_ReadBool(const AeronConfigFile* document, const char* path, bool* out, char* error,
								  size_t capacity) {
	const AeronConfigNode* node =
		TieAppConfig_RequiredNode(document, path, AERON_CONFIG_BOOL, error, capacity);
	if (!node)
		return false;
	*out = AeronConfigNode_Bool(node, 0) != 0;
	return true;
}

static bool TieAppConfig_ReadFloat(const AeronConfigFile* document, const char* path, double minimum,
								   double maximum, float* out, char* error, size_t capacity) {
	const AeronConfigNode* node = AeronConfigFile_GetNode(document, path);
	double value;
	if (!node)
		return TieAppConfig_ConfigError(error, capacity, "missing required setting '%s'", path);
	if (AeronConfigNode_Type(node) != AERON_CONFIG_INT && AeronConfigNode_Type(node) != AERON_CONFIG_FLOAT)
		return TieAppConfig_ConfigError(error, capacity, "setting '%s' must be numeric", path);
	value = AeronConfigNode_Float(node, NAN);
	if (!isfinite(value) || value < minimum || value > maximum)
		return TieAppConfig_ConfigError(error, capacity, "setting '%s' is outside [%g, %g]", path, minimum,
										maximum);
	*out = (float)value;
	return true;
}

static bool TieAppConfig_ReadString(const AeronConfigFile* document, const char* path, char* out,
									size_t out_capacity, char* error, size_t error_capacity) {
	const AeronConfigNode* node =
		TieAppConfig_RequiredNode(document, path, AERON_CONFIG_STRING, error, error_capacity);
	const char* value;
	if (!node)
		return false;
	value = AeronConfigNode_String(node, "");
	if (strlen(value) >= out_capacity)
		return TieAppConfig_ConfigError(error, error_capacity, "setting '%s' is too long", path);
	snprintf(out, out_capacity, "%s", value);
	return true;
}

static bool TieAppConfig_ParseAxisMapping(const AeronConfigFile* document, const char* domain, bool gamepad,
										  TieControllerProfile* profile, char* error, size_t capacity) {
	static const char* const names[] = { "yaw", "pitch", "roll", "throttle" };
	size_t index;
	for (index = 0; index < TIE_INPUT_AXIS_COUNT; ++index) {
		char path[128];
		const AeronConfigNode* node;
		TieInputAxisBinding* binding = &profile->mapping.axes[index];
		int source;
		snprintf(path, sizeof path, "%s.axes.%s", domain, names[index]);
		node = TieAppConfig_RequiredNode(document, path, AERON_CONFIG_MAP, error, capacity);
		if (!node)
			return false;
		snprintf(path, sizeof path, "%s.axes.%s.source", domain, names[index]);
		node = AeronConfigFile_GetNode(document, path);
		if (gamepad) {
			const char* name = AeronConfigNode_String(node, NULL);
			if (!name)
				return TieAppConfig_ConfigError(error, capacity, "'%s' must be a gamepad axis name", path);
			if (strcmp(name, "none") == 0)
				source = -1;
			else {
				source = (int)Aeron_GamepadAxisFromName(name);
				if (source >= AERON_GAMEPAD_AXIS_COUNT)
					return TieAppConfig_ConfigError(error, capacity, "unknown gamepad axis '%s'", name);
			}
		} else if (AeronConfigNode_Type(node) == AERON_CONFIG_STRING &&
				   strcmp(AeronConfigNode_String(node, ""), "none") == 0) {
			source = -1;
		} else if (AeronConfigNode_Type(node) == AERON_CONFIG_INT) {
			int64_t value = AeronConfigNode_Int(node, -1);
			if (value < 0 || value >= AERON_CONTROLLER_AXIS_MAX)
				return TieAppConfig_ConfigError(error, capacity, "raw axis in '%s' is out of range", path);
			source = (int)value;
		} else {
			return TieAppConfig_ConfigError(error, capacity, "'%s' must be an axis index or none", path);
		}
		binding->source = (int8_t)source;
		snprintf(path, sizeof path, "%s.axes.%s.invert", domain, names[index]);
		if (!TieAppConfig_ReadBool(document, path, &binding->invert, error, capacity))
			return false;
		snprintf(path, sizeof path, "%s.axes.%s.deadzone", domain, names[index]);
		if (index != TIE_INPUT_AXIS_THROTTLE || AeronConfigFile_Has(document, path)) {
			if (!TieAppConfig_ReadFloat(document, path, 0.0, 1.0, &binding->deadzone, error, capacity))
				return false;
		}
		if (index == TIE_INPUT_AXIS_THROTTLE)
			binding->deadzone = 0.0f;
	}
	return true;
}

static bool TieAppConfig_SameSource(const AeronControllerDigitalSource* left,
									const AeronControllerDigitalSource* right) {
	return left->kind == right->kind && left->index == right->index &&
		   (left->kind != AERON_CONTROLLER_DIGITAL_HAT || left->hat_direction == right->hat_direction);
}

static bool TieAppConfig_ParseDigitalSource(const AeronConfigNode* node, bool gamepad,
											AeronControllerDigitalSource* out, char* error, size_t capacity) {
	memset(out, 0, sizeof *out);
	out->threshold = 0.5f;
	if (gamepad && AeronConfigNode_Type(node) == AERON_CONFIG_STRING) {
		AeronGamepadButton button = Aeron_GamepadButtonFromName(AeronConfigNode_String(node, ""));
		if (button >= AERON_GAMEPAD_BUTTON_COUNT)
			return TieAppConfig_ConfigError(error, capacity, "unknown gamepad button '%s'",
											AeronConfigNode_String(node, ""));
		out->kind = AERON_CONTROLLER_DIGITAL_BUTTON;
		out->index = (uint8_t)button;
		return true;
	}
	if (AeronConfigNode_Type(node) != AERON_CONFIG_MAP)
		return TieAppConfig_ConfigError(error, capacity, "controller binding must be a source mapping");
	{
		const AeronConfigNode* button = AeronConfigNode_MapGet(node, "button");
		const AeronConfigNode* axis = AeronConfigNode_MapGet(node, "axis");
		const AeronConfigNode* hat = AeronConfigNode_MapGet(node, "hat");
		const AeronConfigNode* direction = AeronConfigNode_MapGet(node, "direction");
		const AeronConfigNode* threshold = AeronConfigNode_MapGet(node, "threshold");
		const char* direction_name;
		int64_t index;
		if (button) {
			if (gamepad || AeronConfigNode_MapCount(node) != 1 ||
				AeronConfigNode_Type(button) != AERON_CONFIG_INT)
				return TieAppConfig_ConfigError(error, capacity, "malformed raw button source");
			index = AeronConfigNode_Int(button, -1);
			if (index < 0 || index >= AERON_CONTROLLER_BUTTON_MAX)
				return TieAppConfig_ConfigError(error, capacity, "raw button index is out of range");
			out->kind = AERON_CONTROLLER_DIGITAL_BUTTON;
			out->index = (uint8_t)index;
			return true;
		}
		if (axis) {
			if (!direction || (AeronConfigNode_MapCount(node) != 2 && AeronConfigNode_MapCount(node) != 3))
				return TieAppConfig_ConfigError(error, capacity, "malformed digital axis source");
			if (gamepad) {
				AeronGamepadAxis gamepad_axis;
				const char* name = AeronConfigNode_String(axis, NULL);
				if (!name)
					return TieAppConfig_ConfigError(error, capacity, "gamepad axis source must be named");
				gamepad_axis = Aeron_GamepadAxisFromName(name);
				if (gamepad_axis >= AERON_GAMEPAD_AXIS_COUNT)
					return TieAppConfig_ConfigError(error, capacity, "unknown gamepad axis '%s'", name);
				out->index = (uint8_t)gamepad_axis;
			} else {
				index = AeronConfigNode_Int(axis, -1);
				if (AeronConfigNode_Type(axis) != AERON_CONFIG_INT || index < 0 ||
					index >= AERON_CONTROLLER_AXIS_MAX)
					return TieAppConfig_ConfigError(error, capacity, "raw axis index is out of range");
				out->index = (uint8_t)index;
			}
			direction_name = AeronConfigNode_String(direction, NULL);
			if (direction_name && strcmp(direction_name, "positive") == 0)
				out->kind = AERON_CONTROLLER_DIGITAL_AXIS_POSITIVE;
			else if (direction_name && strcmp(direction_name, "negative") == 0)
				out->kind = AERON_CONTROLLER_DIGITAL_AXIS_NEGATIVE;
			else
				return TieAppConfig_ConfigError(error, capacity,
												"axis direction must be positive or negative");
			if (threshold) {
				double value = AeronConfigNode_Float(threshold, NAN);
				if (!isfinite(value) || value <= 0.0 || value > 1.0)
					return TieAppConfig_ConfigError(error, capacity, "axis threshold must be in (0, 1]");
				out->threshold = (float)value;
			}
			return true;
		}
		if (hat) {
			if (gamepad || !direction || AeronConfigNode_MapCount(node) != 2 ||
				AeronConfigNode_Type(hat) != AERON_CONFIG_INT)
				return TieAppConfig_ConfigError(error, capacity, "malformed raw hat source");
			index = AeronConfigNode_Int(hat, -1);
			if (index < 0 || index >= AERON_CONTROLLER_HAT_MAX)
				return TieAppConfig_ConfigError(error, capacity, "raw hat index is out of range");
			direction_name = AeronConfigNode_String(direction, NULL);
			if (direction_name && strcmp(direction_name, "up") == 0)
				out->hat_direction = AERON_CONTROLLER_HAT_UP;
			else if (direction_name && strcmp(direction_name, "right") == 0)
				out->hat_direction = AERON_CONTROLLER_HAT_RIGHT;
			else if (direction_name && strcmp(direction_name, "down") == 0)
				out->hat_direction = AERON_CONTROLLER_HAT_DOWN;
			else if (direction_name && strcmp(direction_name, "left") == 0)
				out->hat_direction = AERON_CONTROLLER_HAT_LEFT;
			else
				return TieAppConfig_ConfigError(error, capacity, "hat direction must be up/right/down/left");
			out->kind = AERON_CONTROLLER_DIGITAL_HAT;
			out->index = (uint8_t)index;
			return true;
		}
	}
	return TieAppConfig_ConfigError(error, capacity, "unknown controller source form");
}

static bool TieAppConfig_AddBinding(TieInputActionBinding* bindings, size_t* count, size_t maximum,
									TieInputAction action, const AeronControllerDigitalSource* source,
									char* error, size_t capacity) {
	size_t index;
	for (index = 0; index < *count; ++index) {
		if (TieAppConfig_SameSource(&bindings[index].source, source)) {
			if (bindings[index].action == action)
				return true;
			return TieAppConfig_ConfigError(error, capacity, "physical source is bound to multiple actions");
		}
	}
	if (*count >= maximum)
		return TieAppConfig_ConfigError(error, capacity, "controller binding capacity exceeded");
	bindings[*count].source = *source;
	bindings[*count].action = action;
	++*count;
	return true;
}

static bool TieAppConfig_ParseBindingValue(const AeronConfigNode* node, bool gamepad, TieInputAction action,
										   TieInputActionBinding* bindings, size_t* count, size_t maximum,
										   char* error, size_t capacity) {
	const bool sequence = AeronConfigNode_Type(node) == AERON_CONFIG_SEQUENCE;
	const size_t length = sequence ? AeronConfigNode_SequenceCount(node) : 1;
	if (length > maximum)
		return TieAppConfig_ConfigError(error, capacity, "controller binding capacity exceeded");
	for (size_t i = 0; i < length; ++i) {
		const AeronConfigNode* item = sequence ? AeronConfigNode_SequenceGet(node, i) : node;
		AeronControllerDigitalSource source;
		if (!TieAppConfig_ParseDigitalSource(item, gamepad, &source, error, capacity) ||
			!TieAppConfig_AddBinding(bindings, count, maximum, action, &source, error, capacity))
			return false;
	}
	return true;
}

static bool Keys(const AeronConfigNode* node, const char* const* keys, size_t count, char* error,
				 size_t capacity) {
	if (AeronConfigNode_Type(node) != AERON_CONFIG_MAP)
		return TieAppConfig_ConfigError(error, capacity, "expected controller mapping");
	for (size_t i = 0; i < AeronConfigNode_MapCount(node); ++i) {
		const char* name = AeronConfigNode_MapKeyAt(node, i);
		bool known = false;
		for (size_t j = 0; j < count; ++j)
			if (!strcmp(name, keys[j]))
				known = true;
		if (!known)
			return TieAppConfig_ConfigError(error, capacity, "unknown controller field '%s'", name);
	}
	return true;
}

bool TieControllerConfig_ReadProfile(const AeronConfigFile* document, const char* path,
									 AeronControllerKind kind, TieControllerProfile* profile, char* error,
									 size_t capacity) {
	static const char* const axis_names[] = { "yaw", "pitch", "roll", "throttle" };
	static const char* const fields[] = { "source", "invert", "deadzone" };
	if (kind != AERON_CONTROLLER_KIND_GAMEPAD && kind != AERON_CONTROLLER_KIND_JOYSTICK)
		return TieAppConfig_ConfigError(error, capacity, "layout must be gamepad or joystick");
	if (!strcmp(path, "input.gamepad_defaults")) {
		static const char* const keys[] = { "axes", "buttons" };
		if (!Keys(AeronConfigFile_GetNode(document, path), keys, 2, error, capacity))
			return false;
	}
	char sub[192];
	TieControllerMapping_ClearProfile(profile, kind);
	snprintf(sub, sizeof sub, "%s.axes", path);
	if (!Keys(AeronConfigFile_GetNode(document, sub), axis_names, 4, error, capacity))
		return false;
	for (int i = 0; i < 4; ++i) {
		snprintf(sub, sizeof sub, "%s.axes.%s", path, axis_names[i]);
		if (!Keys(AeronConfigFile_GetNode(document, sub), fields, 3, error, capacity))
			return false;
	}
	if (!TieAppConfig_ParseAxisMapping(document, path, kind == AERON_CONTROLLER_KIND_GAMEPAD, profile, error,
									   capacity))
		return false;
	snprintf(sub, sizeof sub, "%s.buttons", path);
	const AeronConfigNode* map = TieAppConfig_RequiredNode(document, sub, AERON_CONFIG_MAP, error, capacity);
	if (!map)
		return false;
	for (size_t i = 0; i < AeronConfigNode_MapCount(map); ++i) {
		const char* name = AeronConfigNode_MapKeyAt(map, i);
		TieInputAction action = TieInputActions_FromName(name);
		if (action == TIE_INPUT_ACTION_NONE)
			return TieAppConfig_ConfigError(error, capacity, "unknown action '%s'", name);
		if (!TieAppConfig_ParseBindingValue(
				AeronConfigNode_MapValueAt(map, i), kind == AERON_CONTROLLER_KIND_GAMEPAD, action,
				profile->bindings, &profile->binding_count, TIE_CONTROLLER_BINDING_CAP, error, capacity))
			return false;
	}
	return TieControllerMapping_Profilevalidate(profile, kind, error, capacity);
}

bool TieControllerConfig_Read(const AeronConfigFile* document, TieControllerOptions* options, char* error,
							  size_t capacity) {
	static const char* const keys[] = { "guid", "name", "layout", "axes", "buttons" };
	const AeronConfigNode* list =
		TieAppConfig_RequiredNode(document, "input.controllers", AERON_CONFIG_SEQUENCE, error, capacity);
	if (!list)
		return false;
	memset(options, 0, sizeof *options);
	options->count = AeronConfigNode_SequenceCount(list);
	if (options->count > TIE_CONTROLLER_MODEL_CAP)
		return TieAppConfig_ConfigError(error, capacity, "controller model capacity exceeded");
	for (size_t i = 0; i < options->count; ++i) {
		const AeronConfigNode* node = AeronConfigNode_SequenceGet(list, i);
		if (!Keys(node, keys, 5, error, capacity))
			return false;
		TieControllerModel* m = &options->models[i];
		char path[128], field[160];
		snprintf(path, sizeof path, "input.controllers[%zu]", i);
		snprintf(field, sizeof field, "%s.guid", path);
		if (!TieAppConfig_ReadString(document, field, m->guid, sizeof m->guid, error, capacity))
			return false;
		for (size_t j = 0; m->guid[j]; ++j)
			m->guid[j] = (char)tolower((unsigned char)m->guid[j]);
		snprintf(field, sizeof field, "%s.name", path);
		if (!TieAppConfig_ReadString(document, field, m->name, sizeof m->name, error, capacity))
			return false;
		const char* layout = AeronConfigNode_String(AeronConfigNode_MapGet(node, "layout"), "");
		m->kind = !strcmp(layout, "joystick")  ? AERON_CONTROLLER_KIND_JOYSTICK
				  : !strcmp(layout, "gamepad") ? AERON_CONTROLLER_KIND_GAMEPAD
											   : AERON_CONTROLLER_KIND_NONE;
		if (!TieControllerConfig_ReadProfile(document, path, m->kind, &m->profile, error, capacity))
			return false;
	}
	return TieControllerMapping_OptionsValid(options, error, capacity);
}

typedef struct TieAppConfigControllerYamlScratch {
	AeronConfigValue sources[TIE_CONTROLLER_BINDING_CAP];
	AeronConfigValue fields[TIE_CONTROLLER_BINDING_CAP][3];
	AeronConfigMapValue source_maps[TIE_CONTROLLER_BINDING_CAP][3];
	AeronConfigValue action_values[TIE_INPUT_ACTION_COUNT - 1];
	AeronConfigMapValue action_map[TIE_INPUT_ACTION_COUNT - 1];
} TieAppConfigControllerYamlScratch;

static void TieAppConfig_ControllerSourceYaml(const AeronControllerDigitalSource* source, bool gamepad,
											  TieAppConfigControllerYamlScratch* scratch, size_t slot) {
	AeronConfigValue* value = &scratch->sources[slot];
	AeronConfigValue* fields = scratch->fields[slot];
	AeronConfigMapValue* map = scratch->source_maps[slot];
	if (gamepad && source->kind == AERON_CONTROLLER_DIGITAL_BUTTON) {
		value->type = AERON_CONFIG_STRING;
		value->value.string_value = Aeron_GamepadButtonName((AeronGamepadButton)source->index);
		return;
	}
	value->type = AERON_CONFIG_MAP;
	value->value.map.entries = map;
	if (source->kind == AERON_CONTROLLER_DIGITAL_BUTTON) {
		fields[0].type = AERON_CONFIG_INT;
		fields[0].value.int_value = source->index;
		map[0] = (AeronConfigMapValue) { "button", &fields[0] };
		value->value.map.count = 1;
		return;
	}
	if (source->kind == AERON_CONTROLLER_DIGITAL_AXIS_POSITIVE ||
		source->kind == AERON_CONTROLLER_DIGITAL_AXIS_NEGATIVE) {
		fields[0].type = gamepad ? AERON_CONFIG_STRING : AERON_CONFIG_INT;
		if (gamepad)
			fields[0].value.string_value = Aeron_GamepadAxisName((AeronGamepadAxis)source->index);
		else
			fields[0].value.int_value = source->index;
		fields[1].type = AERON_CONFIG_STRING;
		fields[1].value.string_value =
			source->kind == AERON_CONTROLLER_DIGITAL_AXIS_POSITIVE ? "positive" : "negative";
		fields[2].type = AERON_CONFIG_FLOAT;
		fields[2].value.float_value = source->threshold;
		map[0] = (AeronConfigMapValue) { "axis", &fields[0] };
		map[1] = (AeronConfigMapValue) { "direction", &fields[1] };
		map[2] = (AeronConfigMapValue) { "threshold", &fields[2] };
		value->value.map.count = 3;
		return;
	}
	fields[0].type = AERON_CONFIG_INT;
	fields[0].value.int_value = source->index;
	fields[1].type = AERON_CONFIG_STRING;
	switch (source->hat_direction) {
		case AERON_CONTROLLER_HAT_UP:
			fields[1].value.string_value = "up";
			break;
		case AERON_CONTROLLER_HAT_RIGHT:
			fields[1].value.string_value = "right";
			break;
		case AERON_CONTROLLER_HAT_DOWN:
			fields[1].value.string_value = "down";
			break;
		default:
			fields[1].value.string_value = "left";
			break;
	}
	map[0] = (AeronConfigMapValue) { "hat", &fields[0] };
	map[1] = (AeronConfigMapValue) { "direction", &fields[1] };
	value->value.map.count = 2;
}

typedef struct ModelYaml {
	TieAppConfigControllerYamlScratch digital;
	AeronConfigValue axis_fields[4][3], axes[4], axes_map, buttons;
	AeronConfigMapValue axis_maps[4][3], axis_entries[4], fields[5];
	AeronConfigValue guid, name, layout;
} ModelYaml;

static int BindingCompare(const void* left, const void* right) {
	const TieInputActionBinding *a = left, *b = right;
	if (a->action != b->action)
		return (int)a->action - (int)b->action;
	if (a->source.kind != b->source.kind)
		return (int)a->source.kind - (int)b->source.kind;
	if (a->source.index != b->source.index)
		return (int)a->source.index - (int)b->source.index;
	return (int)a->source.hat_direction - (int)b->source.hat_direction;
}
static void ModelValue(const TieControllerModel* model, ModelYaml* scratch, AeronConfigValue* value) {
	static const char* const names[] = { "yaw", "pitch", "roll", "throttle" };
	const bool gamepad = model->kind == AERON_CONTROLLER_KIND_GAMEPAD;
	scratch->guid = (AeronConfigValue) { .type = AERON_CONFIG_STRING, .value.string_value = model->guid };
	scratch->name = (AeronConfigValue) { .type = AERON_CONFIG_STRING, .value.string_value = model->name };
	scratch->layout = (AeronConfigValue) { .type = AERON_CONFIG_STRING,
										   .value.string_value = gamepad ? "gamepad" : "joystick" };
	for (int i = 0; i < 4; ++i) {
		const TieInputAxisBinding* b = &model->profile.mapping.axes[i];
		AeronConfigValue* f = scratch->axis_fields[i];
		f[0].type = b->source < 0 || gamepad ? AERON_CONFIG_STRING : AERON_CONFIG_INT;
		if (b->source < 0)
			f[0].value.string_value = "none";
		else if (gamepad)
			f[0].value.string_value = Aeron_GamepadAxisName((AeronGamepadAxis)b->source);
		else
			f[0].value.int_value = b->source;
		f[1] = (AeronConfigValue) { .type = AERON_CONFIG_BOOL, .value.bool_value = b->invert };
		f[2] = (AeronConfigValue) { .type = AERON_CONFIG_FLOAT, .value.float_value = b->deadzone };
		scratch->axis_maps[i][0] = (AeronConfigMapValue) { "source", &f[0] };
		scratch->axis_maps[i][1] = (AeronConfigMapValue) { "invert", &f[1] };
		scratch->axis_maps[i][2] = (AeronConfigMapValue) { "deadzone", &f[2] };
		scratch->axes[i] = (AeronConfigValue) { .type = AERON_CONFIG_MAP,
												.value.map = { scratch->axis_maps[i],
															   i == TIE_INPUT_AXIS_THROTTLE ? 2 : 3 } };
		scratch->axis_entries[i] = (AeronConfigMapValue) { names[i], &scratch->axes[i] };
	}
	scratch->axes_map =
		(AeronConfigValue) { .type = AERON_CONFIG_MAP, .value.map = { scratch->axis_entries, 4 } };
	TieInputActionBinding bindings[TIE_CONTROLLER_BINDING_CAP];
	memcpy(bindings, model->profile.bindings, model->profile.binding_count * sizeof bindings[0]);
	qsort(bindings, model->profile.binding_count, sizeof bindings[0], BindingCompare);
	size_t actions = 0, slot = 0;
	while (slot < model->profile.binding_count) {
		size_t first = slot;
		TieInputAction action = bindings[slot].action;
		while (slot < model->profile.binding_count && bindings[slot].action == action) {
			TieAppConfig_ControllerSourceYaml(&bindings[slot].source, gamepad, &scratch->digital, slot);
			++slot;
		}
		scratch->digital.action_values[actions] =
			(AeronConfigValue) { .type = AERON_CONFIG_SEQUENCE,
								 .value.sequence = { &scratch->digital.sources[first], slot - first } };
		scratch->digital.action_map[actions] =
			(AeronConfigMapValue) { TieInputActions_ToName(action),
									&scratch->digital.action_values[actions] };
		++actions;
	}
	scratch->buttons = (AeronConfigValue) { .type = AERON_CONFIG_MAP,
											.value.map = { scratch->digital.action_map, actions } };
	scratch->fields[0] = (AeronConfigMapValue) { "guid", &scratch->guid };
	scratch->fields[1] = (AeronConfigMapValue) { "name", &scratch->name };
	scratch->fields[2] = (AeronConfigMapValue) { "layout", &scratch->layout };
	scratch->fields[3] = (AeronConfigMapValue) { "axes", &scratch->axes_map };
	scratch->fields[4] = (AeronConfigMapValue) { "buttons", &scratch->buttons };
	*value = (AeronConfigValue) { .type = AERON_CONFIG_MAP, .value.map = { scratch->fields, 5 } };
}

bool TieControllerConfig_Write(AeronConfigFile* document, const TieControllerOptions* options,
							   AeronConfigError* error) {
	ModelYaml* scratch = calloc(options->count ? options->count : 1, sizeof *scratch);
	if (!scratch) {
		snprintf(error->message, sizeof error->message, "controller serialization allocation failed");
		return false;
	}
	AeronConfigValue models[TIE_CONTROLLER_MODEL_CAP];
	for (size_t i = 0; i < options->count; ++i)
		ModelValue(&options->models[i], &scratch[i], &models[i]);
	AeronConfigValue list = { .type = AERON_CONFIG_SEQUENCE, .value.sequence = { models, options->count } };
	bool ok = AeronConfigFile_SetValue(document, "input.controllers", &list, error) != 0;
	free(scratch);
	return ok;
}
