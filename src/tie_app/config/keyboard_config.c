#include "tie_app/config/keyboard_config.h"

#include "aeron/aeron.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool ConfigError(char* error, size_t capacity, const char* format, ...) {
	if (error && capacity) {
		va_list args;
		va_start(args, format);
		vsnprintf(error, capacity, format, args);
		va_end(args);
	}
	return false;
}

static const char* const modifier_names[] = { "shift", "ctrl", "alt", "gui" };

static bool ReadSource(const AeronConfigNode* node, AeronKeyChord* source, char* error, size_t capacity) {
	if (AeronConfigNode_Type(node) != AERON_CONFIG_MAP)
		return ConfigError(error, capacity, "keyboard source must be a key mapping");
	for (size_t i = 0; i < AeronConfigNode_MapCount(node); ++i) {
		const char* name = AeronConfigNode_MapKeyAt(node, i);
		if (strcmp(name, "key") && strcmp(name, "modifiers"))
			return ConfigError(error, capacity, "unknown keyboard source field '%s'", name);
	}
	const char* key = AeronConfigNode_String(AeronConfigNode_MapGet(node, "key"), NULL);
	AeronKey parsed;
	if (!key || !AeronKey_FromName(key, &parsed))
		return ConfigError(error, capacity, "unknown keyboard key '%s'", key ? key : "");
	*source = (AeronKeyChord) { .key = (uint16_t)parsed };
	const AeronConfigNode* mods = AeronConfigNode_MapGet(node, "modifiers");
	if (mods && AeronConfigNode_Type(mods) != AERON_CONFIG_SEQUENCE)
		return ConfigError(error, capacity, "keyboard modifiers must be a sequence");
	for (size_t i = 0; i < AeronConfigNode_SequenceCount(mods); ++i) {
		const char* name = AeronConfigNode_String(AeronConfigNode_SequenceGet(mods, i), NULL);
		int bit = 0;
		for (; bit < 4; ++bit)
			if (name && !strcmp(name, modifier_names[bit]))
				break;
		if (bit == 4 || (source->modifiers & (1u << bit)))
			return ConfigError(error, capacity, "invalid or repeated keyboard modifier '%s'",
							   name ? name : "");
		source->modifiers |= (uint8_t)(1u << bit);
	}
	return true;
}

static bool AddSource(TieKeyboardBindings* profile, TieInputAction action, AeronKeyChord source, char* error,
					  size_t capacity) {
	if (!TieKeyboardMapping_SourceValid(source))
		return ConfigError(error, capacity, "keyboard source '%s' is reserved or unsupported",
						   AeronKey_Name((AeronKey)source.key));
	if (TieKeyboardMapping_Find(profile, source) != SIZE_MAX)
		return ConfigError(error, capacity, "keyboard source '%s' is bound twice",
						   AeronKey_Name((AeronKey)source.key));
	if (profile->count == TIE_KEYBOARD_BINDING_CAP)
		return ConfigError(error, capacity, "keyboard binding capacity exceeded");
	profile->bindings[profile->count++] = (TieKeyboardBinding) { source, action };
	return true;
}

bool TieKeyboardConfig_Read(const AeronConfigFile* document, TieKeyboardBindings* profile, char* error,
							size_t capacity) {
	memset(profile, 0, sizeof *profile);
	const AeronConfigNode* map = AeronConfigFile_GetNode(document, "input.keyboard");
	if (AeronConfigNode_Type(map) != AERON_CONFIG_MAP)
		return ConfigError(error, capacity, "input.keyboard must be a mapping");
	for (size_t i = 0; i < AeronConfigNode_MapCount(map); ++i) {
		const char* name = AeronConfigNode_MapKeyAt(map, i);
		const TieInputAction action = TieInputActions_FromName(name);
		const AeronConfigNode* value = AeronConfigNode_MapValueAt(map, i);
		if (action == TIE_INPUT_ACTION_NONE)
			return ConfigError(error, capacity, "unknown keyboard action '%s'", name);
		if (AeronConfigNode_Type(value) == AERON_CONFIG_STRING) {
			AeronKey key;
			const char* label = AeronConfigNode_String(value, NULL);
			if (!AeronKey_FromName(label, &key))
				return ConfigError(error, capacity, "unknown keyboard key '%s'", label);
			if (!AddSource(profile, action, (AeronKeyChord) { .key = (uint16_t)key }, error, capacity))
				return false;
		} else if (AeronConfigNode_Type(value) == AERON_CONFIG_SEQUENCE) {
			for (size_t j = 0; j < AeronConfigNode_SequenceCount(value); ++j) {
				AeronKeyChord source;
				if (!ReadSource(AeronConfigNode_SequenceGet(value, j), &source, error, capacity) ||
					!AddSource(profile, action, source, error, capacity))
					return false;
			}
		} else {
			return ConfigError(error, capacity, "keyboard action '%s' requires a key name or sequence", name);
		}
	}
	TieKeyboardMapping_Sort(profile);
	return true;
}

typedef struct SourceYaml {
	AeronConfigMapValue fields[2];
	AeronConfigValue key;
	AeronConfigValue modifiers;
	AeronConfigValue modifier_values[4];
} SourceYaml;

bool TieKeyboardConfig_Write(AeronConfigFile* document, const TieKeyboardBindings* profile,
							 AeronConfigError* error) {
	if (profile->count > TIE_KEYBOARD_BINDING_CAP) {
		snprintf(error->message, sizeof error->message, "keyboard binding capacity exceeded");
		return false;
	}
	for (size_t i = 0; i < profile->count; ++i) {
		if (profile->bindings[i].action <= TIE_INPUT_ACTION_NONE ||
			profile->bindings[i].action >= TIE_INPUT_ACTION_COUNT) {
			snprintf(error->message, sizeof error->message, "invalid keyboard action");
			return false;
		}
	}
	SourceYaml* scratch = calloc(TIE_KEYBOARD_BINDING_CAP, sizeof *scratch);
	if (!scratch) {
		snprintf(error->message, sizeof error->message, "keyboard serialization allocation failed");
		return false;
	}
	AeronConfigValue sources[TIE_KEYBOARD_BINDING_CAP];
	AeronConfigValue actions[TIE_INPUT_ACTION_COUNT - 1];
	AeronConfigMapValue entries[TIE_INPUT_ACTION_COUNT - 1];
	size_t used = 0;
	for (int action = TIE_INPUT_ACTION_NONE + 1; action < TIE_INPUT_ACTION_COUNT; ++action) {
		size_t first = used;
		for (size_t i = 0; i < profile->count; ++i) {
			const TieKeyboardBinding* b = &profile->bindings[i];
			if ((int)b->action != action)
				continue;
			if (used == TIE_KEYBOARD_BINDING_CAP) {
				free(scratch);
				snprintf(error->message, sizeof error->message, "keyboard binding capacity exceeded");
				return false;
			}
			SourceYaml* s = &scratch[used];
			s->key = (AeronConfigValue) { .type = AERON_CONFIG_STRING,
										  .value.string_value = AeronKey_Name((AeronKey)b->source.key) };
			size_t mods = 0;
			for (int bit = 0; bit < 4; ++bit)
				if (b->source.modifiers & (1u << bit))
					s->modifier_values[mods++] =
						(AeronConfigValue) { .type = AERON_CONFIG_STRING,
											 .value.string_value = modifier_names[bit] };
			s->modifiers = (AeronConfigValue) { .type = AERON_CONFIG_SEQUENCE,
												.value.sequence = { s->modifier_values, mods } };
			s->fields[0] = (AeronConfigMapValue) { "key", &s->key };
			s->fields[1] = (AeronConfigMapValue) { "modifiers", &s->modifiers };
			sources[used++] =
				(AeronConfigValue) { .type = AERON_CONFIG_MAP, .value.map = { s->fields, mods ? 2 : 1 } };
		}
		actions[action - 1] = (AeronConfigValue) { .type = AERON_CONFIG_SEQUENCE,
												   .value.sequence = { sources + first, used - first } };
		entries[action - 1] =
			(AeronConfigMapValue) { TieInputActions_ToName((TieInputAction)action), &actions[action - 1] };
	}
	AeronConfigValue value = { .type = AERON_CONFIG_MAP,
							   .value.map = { entries, TIE_INPUT_ACTION_COUNT - 1 } };
	bool ok = AeronConfigFile_SetValue(document, "input.keyboard", &value, error) != 0;
	free(scratch);
	return ok;
}

bool TieKeyboardConfig_Migrate(AeronConfigFile* document, const TieKeyboardBindings* defaults, char* error,
							   size_t capacity) {
	const AeronConfigNode* map = AeronConfigFile_GetNode(document, "input.keyboard");
	if (map && AeronConfigNode_Type(map) != AERON_CONFIG_MAP)
		return ConfigError(error, capacity, "input.keyboard must be a mapping");
	TieKeyboardBindings profile = *defaults;
	for (size_t i = 0; i < AeronConfigNode_MapCount(map); ++i) {
		const char* name = AeronConfigNode_MapKeyAt(map, i);
		const char* key_name = AeronConfigNode_String(AeronConfigNode_MapValueAt(map, i), NULL);
		TieInputAction action = TieInputActions_FromName(name);
		AeronKey key;
		if (!action || !AeronKey_FromName(key_name, &key))
			return ConfigError(error, capacity, "invalid version 5 keyboard binding '%s'", name);
		if (key == AERON_KEY_ESCAPE) {
			Aeron_LogWarn("tie.config", "Removed Escape binding for %s; Escape is reserved for settings",
						  name);
			continue;
		}
		AeronKeyChord source = { .key = (uint16_t)key };
		TieKeyboardMapping_Remove(&profile, TieKeyboardMapping_Find(&profile, source));
		if (!AddSource(&profile, action, source, error, capacity))
			return false;
	}
	AeronConfigError detail = { 0 };
	bool ok = AeronConfigNode_MapCount(map)
				  ? TieKeyboardConfig_Write(document, &profile, &detail)
				  : AeronConfigFile_Remove(document, "input.keyboard", &detail) != 0;
	if (!ok || !AeronConfigFile_SetInt(document, "version", 6, &detail))
		return ConfigError(error, capacity, "%s", detail.message);
	return true;
}

bool TieKeyboardConfig_Resolve(const TieKeyboardBindings* defaults, const AeronConfigFile* user,
							   AeronConfigFile* merged, char* error, size_t capacity) {
	if (!AeronConfigFile_Has(user, "input.keyboard"))
		return true;
	TieKeyboardBindings effective;
	if (!TieKeyboardConfig_Read(user, &effective, error, capacity))
		return false;
	const AeronConfigNode* overrides = AeronConfigFile_GetNode(user, "input.keyboard");
	for (size_t i = 0; i < defaults->count; ++i) {
		const TieKeyboardBinding* binding = &defaults->bindings[i];
		/* Explicit action lists replace their defaults, including empty lists.
		 * Explicit sources also displace matching sources from inherited actions. */
		if (AeronConfigNode_MapGet(overrides, TieInputActions_ToName(binding->action)) ||
			TieKeyboardMapping_Find(&effective, binding->source) != SIZE_MAX)
			continue;
		if (!AddSource(&effective, binding->action, binding->source, error, capacity))
			return false;
	}
	AeronConfigError detail = { 0 };
	if (!TieKeyboardConfig_Write(merged, &effective, &detail))
		return ConfigError(error, capacity, "%s", detail.message);
	return true;
}
