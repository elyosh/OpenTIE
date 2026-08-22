#include "tie_app/config/app_config.h"

#include "aeron/log.h"

#include <float.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static TieAppConfigState* g_current_config;

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

static bool TieAppConfig_ReadInt(const AeronConfigFile* document, const char* path, int minimum, int maximum,
								 int* out, char* error, size_t capacity) {
	const AeronConfigNode* node =
		TieAppConfig_RequiredNode(document, path, AERON_CONFIG_INT, error, capacity);
	int64_t value;
	if (!node)
		return false;
	value = AeronConfigNode_Int(node, 0);
	if (value < minimum || value > maximum)
		return TieAppConfig_ConfigError(error, capacity, "setting '%s' is outside [%d, %d]", path, minimum,
										maximum);
	*out = (int)value;
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

static bool TieAppConfig_ValidateResourcePath(const char* setting, const char* path, char* error,
											  size_t capacity) {
	const char* component = path;
	bool drive_absolute;
	if (!path || !path[0])
		return TieAppConfig_ConfigError(error, capacity,
										"setting '%s' must be a non-empty RESOURCE-relative path", setting);
	drive_absolute =
		(((path[0] >= 'A' && path[0] <= 'Z') || (path[0] >= 'a' && path[0] <= 'z')) && path[1] == ':');
	if (path[0] == '/' || path[0] == '\\' || drive_absolute)
		return TieAppConfig_ConfigError(error, capacity, "setting '%s' must be relative to RESOURCE",
										setting);
	for (const char* cursor = path;; ++cursor) {
		if (*cursor != '/' && *cursor != '\\' && *cursor != '\0')
			continue;
		if ((cursor - component == 2 && component[0] == '.' && component[1] == '.') || cursor == component)
			return TieAppConfig_ConfigError(error, capacity,
											"setting '%s' contains an invalid path component", setting);
		if (*cursor == '\0')
			break;
		component = cursor + 1;
	}
	return true;
}

static bool TieAppConfig_CheckVersion(const AeronConfigFile* document, char* error, size_t capacity) {
	const AeronConfigNode* root = AeronConfigFile_Root(document);
	const AeronConfigNode* version;
	if (AeronConfigNode_Type(root) != AERON_CONFIG_MAP)
		return TieAppConfig_ConfigError(error, capacity, "configuration root must be a mapping");
	version = AeronConfigNode_MapGet(root, "version");
	if (!version || AeronConfigNode_Type(version) != AERON_CONFIG_INT || AeronConfigNode_Int(version, 0) != 4)
		return TieAppConfig_ConfigError(error, capacity, "configuration version must be integer 4");
	return true;
}

static bool TieAppConfig_KeyAllowed(const char* key, const char* const* allowed, size_t count) {
	size_t index;
	for (index = 0; index < count; ++index)
		if (strcmp(key, allowed[index]) == 0)
			return true;
	return false;
}

static bool TieAppConfig_ValidateMapKeys(const AeronConfigFile* document, const char* path,
										 const char* const* allowed, size_t allowed_count, bool warn,
										 char* error, size_t capacity) {
	const AeronConfigNode* map =
		path[0] ? AeronConfigFile_GetNode(document, path) : AeronConfigFile_Root(document);
	size_t index;
	if (!map)
		return true;
	if (AeronConfigNode_Type(map) != AERON_CONFIG_MAP)
		return TieAppConfig_ConfigError(error, capacity, "'%s' must be a mapping", path[0] ? path : "<root>");
	for (index = 0; index < AeronConfigNode_MapCount(map); ++index) {
		const char* key = AeronConfigNode_MapKeyAt(map, index);
		if (TieAppConfig_KeyAllowed(key, allowed, allowed_count))
			continue;
		if (!warn)
			return TieAppConfig_ConfigError(error, capacity, "unknown shipped setting '%s%s%s'", path,
											path[0] ? "." : "", key);
		Aeron_LogWarn("tie.config", "unknown user setting '%s%s%s'", path, path[0] ? "." : "", key);
	}
	return true;
}

#define VALIDATE_KEYS(doc, path, warn, ...)                                                                  \
	do {                                                                                                     \
		static const char* const keys[] = { __VA_ARGS__ };                                                   \
		if (!TieAppConfig_ValidateMapKeys((doc), (path), keys, sizeof keys / sizeof keys[0], (warn), error,  \
										  capacity))                                                         \
			return false;                                                                                    \
	} while (0)

static bool TieAppConfig_ValidateSchemaKeys(const AeronConfigFile* document, bool warn, char* error,
											size_t capacity) {
	VALIDATE_KEYS(document, "", warn, "version", "paths", "audio", "ui", "frontend", "flight", "input",
				  "video", "render", "pbr", "point_lights");
	VALIDATE_KEYS(document, "paths", warn, "installations");
	VALIDATE_KEYS(document, "paths.installations", warn, "tie95", "tie98");
	VALIDATE_KEYS(document, "audio", warn, "midi_backend", "music", "sb16_filter",
				  "prefer_tie95_frontend_voices",
				  "player_engine_sound_volume_percent", "fluidsynth", "sc55");
	VALIDATE_KEYS(document, "audio.fluidsynth", warn, "soundfont_file");
	VALIDATE_KEYS(document, "audio.sc55", warn, "rom_directory");
	VALIDATE_KEYS(document, "ui", warn, "font");
	VALIDATE_KEYS(document, "frontend", warn, "version", "aspect_correct_legacy_scenes");
	VALIDATE_KEYS(document, "flight", warn, "version", "original_renderer", "update_rate",
				  "player_engine_sound", "models");
	VALIDATE_KEYS(document, "flight.models", warn, "source", "smooth_angle_degrees", "opt_emissive_strength",
				  "opt_projectile_emissive_strength");
	VALIDATE_KEYS(document, "input", warn, "device", "gamepad", "joystick", "keyboard");
	VALIDATE_KEYS(document, "input.device", warn, "guid", "path", "ordinal");
	VALIDATE_KEYS(document, "input.gamepad", warn, "axes", "buttons");
	VALIDATE_KEYS(document, "input.joystick", warn, "axes", "buttons");
	VALIDATE_KEYS(document, "input.gamepad.axes", warn, "yaw", "pitch", "roll", "throttle");
	VALIDATE_KEYS(document, "input.joystick.axes", warn, "yaw", "pitch", "roll", "throttle");
	VALIDATE_KEYS(document, "input.gamepad.axes.yaw", warn, "source", "invert", "deadzone");
	VALIDATE_KEYS(document, "input.gamepad.axes.pitch", warn, "source", "invert", "deadzone");
	VALIDATE_KEYS(document, "input.gamepad.axes.roll", warn, "source", "invert", "deadzone");
	VALIDATE_KEYS(document, "input.gamepad.axes.throttle", warn, "source", "invert", "deadzone");
	VALIDATE_KEYS(document, "input.joystick.axes.yaw", warn, "source", "invert", "deadzone");
	VALIDATE_KEYS(document, "input.joystick.axes.pitch", warn, "source", "invert", "deadzone");
	VALIDATE_KEYS(document, "input.joystick.axes.roll", warn, "source", "invert", "deadzone");
	VALIDATE_KEYS(document, "input.joystick.axes.throttle", warn, "source", "invert", "deadzone");
	VALIDATE_KEYS(document, "video", warn, "fullscreen", "hdr", "sdr_content_gamma", "paper_white_nits");
	VALIDATE_KEYS(document, "render", warn, "anisotropy", "ssao", "temporal_upscaling", "shadows", "tonemap",
				  "motion_blur", "msaa_samples", "starfield_style");
	VALIDATE_KEYS(document, "render.motion_blur", warn, "quality", "shutter");
	VALIDATE_KEYS(document, "render.ssao", warn, "quality", "intensity", "power", "radius_view", "bias_view",
				  "direct", "debug_viz", "min_screen_frac", "max_screen_frac", "sample_jitter");
	VALIDATE_KEYS(document, "render.temporal_upscaling", warn, "mode", "sharpness");
	VALIDATE_KEYS(document, "render.shadows", warn, "mode", "atlas_size", "cascade_count", "fit_mode",
				  "max_distance", "split_lambda", "explicit_splits", "split_1", "split_2", "split_3",
				  "filter_quality", "filter_radius", "contact_hardening", "light_angular_radius_degrees",
				  "max_filter_radius", "pcss_min_filter_radius", "normal_bias_texels", "depth_bias_texels",
				  "transition_fraction", "distance_fade_fraction", "debug_cascades");
	VALIDATE_KEYS(document, "render.tonemap", warn, "operator", "agx_look", "agx_eotf_exponent",
				  "agx_punchy_power", "agx_punchy_saturation", "aces_pre_exposure");
	VALIDATE_KEYS(document, "pbr", warn, "light_intensity", "global_specular_multiplier", "light_wrap",
				  "geometric_specular_adaptation", "ambient_default");
	VALIDATE_KEYS(document, "pbr.ambient_default", warn, "pos_x", "neg_x", "pos_y", "neg_y", "pos_z", "neg_z",
				  "sun_color");
	VALIDATE_KEYS(document, "point_lights", warn, "enabled", "clustered", "cluster_depth_slices",
				  "cluster_debug", "scale", "range_scale", "min_distance", "spec_weight", "diffuse_wrap",
				  "contrib_cap", "training_headlight");
	VALIDATE_KEYS(document, "point_lights.training_headlight", warn, "enabled", "color", "intensity",
				  "range_m", "nose_offset_m");
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
		snprintf(path, sizeof path, "input.%s.axes.%s", domain, names[index]);
		node = TieAppConfig_RequiredNode(document, path, AERON_CONFIG_MAP, error, capacity);
		if (!node)
			return false;
		snprintf(path, sizeof path, "input.%s.axes.%s.source", domain, names[index]);
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
		snprintf(path, sizeof path, "input.%s.axes.%s.invert", domain, names[index]);
		if (!TieAppConfig_ReadBool(document, path, &binding->invert, error, capacity))
			return false;
		snprintf(path, sizeof path, "input.%s.axes.%s.deadzone", domain, names[index]);
		if (!TieAppConfig_ReadFloat(document, path, 0.0, 1.0, &binding->deadzone, error, capacity))
			return false;
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
	size_t index;
	if (AeronConfigNode_Type(node) == AERON_CONFIG_SEQUENCE) {
		for (index = 0; index < AeronConfigNode_SequenceCount(node); ++index)
			if (!TieAppConfig_ParseBindingValue(AeronConfigNode_SequenceGet(node, index), gamepad, action,
												bindings, count, maximum, error, capacity))
				return false;
		return true;
	}
	{
		AeronControllerDigitalSource source;
		return TieAppConfig_ParseDigitalSource(node, gamepad, &source, error, capacity) &&
			   TieAppConfig_AddBinding(bindings, count, maximum, action, &source, error, capacity);
	}
}

static bool TieAppConfig_ParseBindings(const AeronConfigFile* document, TieControllerOptions* controller,
									   TieKeyboardBindings* keyboard, char* error, size_t capacity) {
	const char* paths[] = { "input.gamepad.buttons", "input.joystick.buttons" };
	size_t domain;
	memset(keyboard, 0, sizeof *keyboard);
	for (domain = 0; domain < 2; ++domain) {
		TieControllerProfile* profile = domain == 0 ? &controller->gamepad : &controller->joystick;
		const AeronConfigNode* map =
			TieAppConfig_RequiredNode(document, paths[domain], AERON_CONFIG_MAP, error, capacity);
		size_t index;
		if (!map)
			return false;
		for (index = 0; index < AeronConfigNode_MapCount(map); ++index) {
			const char* name = AeronConfigNode_MapKeyAt(map, index);
			TieInputAction action = TieInputActions_FromName(name);
			if (action == TIE_INPUT_ACTION_NONE)
				return TieAppConfig_ConfigError(error, capacity, "unknown input action '%s'", name);
			if (!TieAppConfig_ParseBindingValue(AeronConfigNode_MapValueAt(map, index), domain == 0, action,
												profile->bindings, &profile->binding_count,
												TIE_CONTROLLER_BINDING_CAP, error, capacity)) {
				return false;
			}
		}
	}
	{
		const AeronConfigNode* map =
			TieAppConfig_RequiredNode(document, "input.keyboard", AERON_CONFIG_MAP, error, capacity);
		size_t index;
		if (!map)
			return false;
		for (index = 0; index < AeronConfigNode_MapCount(map); ++index) {
			const char* name = AeronConfigNode_MapKeyAt(map, index);
			const char* key = AeronConfigNode_String(AeronConfigNode_MapValueAt(map, index), NULL);
			TieInputAction action = TieInputActions_FromName(name);
			AeronKey aeron_key;
			if (action == TIE_INPUT_ACTION_NONE || !key)
				return TieAppConfig_ConfigError(error, capacity, "invalid keyboard binding '%s'", name);
			if (!AeronKey_FromName(key, &aeron_key))
				return TieAppConfig_ConfigError(error, capacity, "unknown keyboard key '%s'", key);
			if (keyboard->keyboard[aeron_key] != TIE_INPUT_ACTION_NONE)
				return TieAppConfig_ConfigError(error, capacity, "keyboard key '%s' is bound twice", key);
			keyboard->keyboard[aeron_key] = action;
		}
	}
	return TieControllerMapping_Profilevalidate(&controller->gamepad, AERON_CONTROLLER_KIND_GAMEPAD, error,
												capacity) &&
		   TieControllerMapping_Profilevalidate(&controller->joystick, AERON_CONTROLLER_KIND_JOYSTICK, error,
												capacity);
}

static bool TieAppConfig_ReadVec3(const AeronConfigFile* document, const char* path, float out[3],
								  char* error, size_t capacity) {
	const AeronConfigNode* node =
		TieAppConfig_RequiredNode(document, path, AERON_CONFIG_SEQUENCE, error, capacity);
	size_t index;
	if (!node)
		return false;
	if (AeronConfigNode_SequenceCount(node) != 3)
		return TieAppConfig_ConfigError(error, capacity, "'%s' must contain three numbers", path);
	for (index = 0; index < 3; ++index) {
		const AeronConfigNode* item = AeronConfigNode_SequenceGet(node, index);
		double value = AeronConfigNode_Float(item, NAN);
		if (!isfinite(value) || value < 0.0)
			return TieAppConfig_ConfigError(error, capacity, "'%s' values must be finite and non-negative",
											path);
		out[index] = (float)value;
	}
	return true;
}

static bool TieAppConfig_ParseVideo(const AeronConfigFile* document, TieAppVideoConfig* out, char* error,
									size_t capacity) {
	const AeronConfigNode* gamma;
	const AeronConfigNode* white;
	if (!TieAppConfig_ReadBool(document, "video.fullscreen", &out->fullscreen, error, capacity) ||
		!TieAppConfig_ReadBool(document, "video.hdr", &out->output.hdr, error, capacity))
		return false;
	gamma = AeronConfigFile_GetNode(document, "video.sdr_content_gamma");
	if (AeronConfigNode_Type(gamma) == AERON_CONFIG_STRING) {
		const char* name = AeronConfigNode_String(gamma, "");
		if (strcmp(name, "auto") == 0) {
			/* Compatibility with the shipped configuration: auto resolves to
			 * the same platform default used by Aeron and OpenXWA. */
#if defined(__APPLE__)
			out->output.sdr_content_gamma = TIE_SDR_CONTENT_GAMMA_SRGB;
#else
			out->output.sdr_content_gamma = TIE_SDR_CONTENT_GAMMA_2_2;
#endif
		} else if (strcmp(name, "srgb") == 0) {
			out->output.sdr_content_gamma = TIE_SDR_CONTENT_GAMMA_SRGB;
		} else if (strcmp(name, "2.2") == 0) {
			out->output.sdr_content_gamma = TIE_SDR_CONTENT_GAMMA_2_2;
		} else if (strcmp(name, "2.4") == 0) {
			out->output.sdr_content_gamma = TIE_SDR_CONTENT_GAMMA_2_4;
		} else
			return TieAppConfig_ConfigError(error, capacity, "invalid video.sdr_content_gamma");
	} else {
		double value = AeronConfigNode_Float(gamma, NAN);
		if (value > 2.19 && value < 2.21)
			out->output.sdr_content_gamma = TIE_SDR_CONTENT_GAMMA_2_2;
		else if (value > 2.39 && value < 2.41)
			out->output.sdr_content_gamma = TIE_SDR_CONTENT_GAMMA_2_4;
		else
			return TieAppConfig_ConfigError(error, capacity, "invalid video.sdr_content_gamma");
	}
#if defined(__APPLE__)
	/* Apple presents SDR content through its fixed piecewise curve. */
	out->output.sdr_content_gamma = TIE_SDR_CONTENT_GAMMA_SRGB;
#endif
	white = AeronConfigFile_GetNode(document, "video.paper_white_nits");
	if (AeronConfigNode_Type(white) == AERON_CONFIG_STRING &&
		strcmp(AeronConfigNode_String(white, ""), "auto") == 0) {
		out->output.paper_white_auto = true;
		out->output.paper_white_nits = 0.0f;
	} else {
		double value = AeronConfigNode_Float(white, NAN);
		if (!isfinite(value) || value <= 0.0)
			return TieAppConfig_ConfigError(error, capacity, "invalid video.paper_white_nits");
		out->output.paper_white_auto = false;
		out->output.paper_white_nits = (float)value;
	}
#if defined(__APPLE__)
	/* EDR reference white follows the system brightness on Apple. */
	out->output.paper_white_auto = true;
	out->output.paper_white_nits = 0.0f;
#endif
	return true;
}

static bool TieAppConfig_ParseRender(const AeronConfigFile* document,
									 const AeronSceneSsaoSettings* baseline_ssao,
									 const AeronSceneShadowSettings* baseline_shadows,
									 const AeronSceneTonemapSettings* baseline_tonemap,
									 TieFlightRenderConfig* out, char* error, size_t capacity) {
	AeronConfigError aeron_error = { 0 };
	const AeronConfigNode* render;
	const char* mode;
	const char* starfield_style;
	memset(out, 0, sizeof *out);
	out->ssao = *baseline_ssao;
	out->shadows = *baseline_shadows;
	out->tonemap = *baseline_tonemap;
	if (!TieAppConfig_ReadFloat(document, "render.anisotropy", 1, 16, &out->anisotropy, error, capacity) ||
		!TieAppConfig_ReadFloat(document, "render.temporal_upscaling.sharpness", 0, 1,
								&out->temporal_sharpness, error, capacity) ||
		!TieAppConfig_ReadInt(document, "render.motion_blur.quality", 0, 2, &out->motion_blur_quality, error,
							  capacity) ||
		!TieAppConfig_ReadFloat(document, "render.motion_blur.shutter", 0, 8, &out->motion_blur_shutter,
								error, capacity) ||
		!TieAppConfig_ReadInt(document, "render.msaa_samples", 1, 8, &out->msaa_samples, error, capacity))
		return false;
	render = TieAppConfig_RequiredNode(document, "render", AERON_CONFIG_MAP, error, capacity);
	if (!render)
		return false;
	if (!AeronSceneSettings_Overlay(render, &out->ssao, &out->shadows, &out->tonemap, &aeron_error))
		return TieAppConfig_ConfigError(error, capacity, "%s:%d:%d: %s", aeron_error.path, aeron_error.line,
										aeron_error.column, aeron_error.message);
	mode = AeronConfigFile_GetString(document, "render.temporal_upscaling.mode", NULL);
	if (!mode)
		return TieAppConfig_ConfigError(error, capacity, "missing temporal upscaling mode");
	if (strcmp(mode, "off") == 0)
		out->temporal_mode = TIE_FLIGHT_TEMPORAL_OFF;
	else if (strcmp(mode, "native_aa") == 0)
		out->temporal_mode = TIE_FLIGHT_TEMPORAL_NATIVE_AA;
	else if (strcmp(mode, "quality") == 0)
		out->temporal_mode = TIE_FLIGHT_TEMPORAL_QUALITY;
	else if (strcmp(mode, "balanced") == 0)
		out->temporal_mode = TIE_FLIGHT_TEMPORAL_BALANCED;
	else if (strcmp(mode, "performance") == 0)
		out->temporal_mode = TIE_FLIGHT_TEMPORAL_PERFORMANCE;
	else
		return TieAppConfig_ConfigError(error, capacity, "invalid temporal upscaling mode '%s'", mode);
	starfield_style = AeronConfigFile_GetString(document, "render.starfield_style", NULL);
	if (!starfield_style)
		return TieAppConfig_ConfigError(error, capacity, "missing render.starfield_style");
	if (strcmp(starfield_style, "tie95") == 0)
		out->starfield_style = TIE_FLIGHT_STARFIELD_STYLE_TIE95;
	else if (strcmp(starfield_style, "tie98") == 0)
		out->starfield_style = TIE_FLIGHT_STARFIELD_STYLE_TIE98;
	else
		return TieAppConfig_ConfigError(error, capacity, "invalid starfield style '%s'", starfield_style);
	return true;
}

static bool TieAppConfig_ParsePbr(const AeronConfigFile* document, TieFlightPbrConfig* out, char* error,
								  size_t capacity) {
	memset(out, 0, sizeof *out);
	if (!TieAppConfig_ReadFloat(document, "pbr.light_intensity", 0, INFINITY, &out->light_intensity, error,
								capacity) ||
		!TieAppConfig_ReadFloat(document, "pbr.global_specular_multiplier", 0, INFINITY,
								&out->global_specular_multiplier, error, capacity) ||
		!TieAppConfig_ReadFloat(document, "pbr.light_wrap", 0, 1, &out->light_wrap, error, capacity) ||
		!TieAppConfig_ReadBool(document, "pbr.geometric_specular_adaptation",
							   &out->geometric_specular_adaptation, error, capacity))
		return false;
#define V(name, field)                                                                                       \
	if (!TieAppConfig_ReadVec3(document, "pbr.ambient_default." name, out->ambient_default.field, error,     \
							   capacity))                                                                    \
	return false
	V("pos_x", pos_x);
	V("neg_x", neg_x);
	V("pos_y", pos_y);
	V("neg_y", neg_y);
	V("pos_z", pos_z);
	V("neg_z", neg_z);
	V("sun_color", sun_color);
#undef V
	return true;
}

static bool TieAppConfig_ParsePointLights(const AeronConfigFile* document, TieFlightPointLightParams* out,
										  char* error, size_t capacity) {
	memset(out, 0, sizeof *out);
	if (!TieAppConfig_ReadBool(document, "point_lights.enabled", &out->enabled, error, capacity) ||
		!TieAppConfig_ReadBool(document, "point_lights.clustered", &out->clustered, error, capacity) ||
		!TieAppConfig_ReadInt(document, "point_lights.cluster_depth_slices", 4, 64,
							  &out->cluster_depth_slices, error, capacity) ||
		!TieAppConfig_ReadBool(document, "point_lights.cluster_debug", &out->cluster_debug, error,
							   capacity) ||
		!TieAppConfig_ReadFloat(document, "point_lights.scale", 0, FLT_MAX, &out->scale, error, capacity) ||
		!TieAppConfig_ReadFloat(document, "point_lights.range_scale", 0, FLT_MAX, &out->range_scale, error,
								capacity) ||
		!TieAppConfig_ReadFloat(document, "point_lights.min_distance", 0, FLT_MAX, &out->min_distance, error,
								capacity) ||
		!TieAppConfig_ReadFloat(document, "point_lights.spec_weight", 0, FLT_MAX, &out->spec_weight, error,
								capacity) ||
		!TieAppConfig_ReadFloat(document, "point_lights.diffuse_wrap", 0, 1, &out->diffuse_wrap, error,
								capacity) ||
		!TieAppConfig_ReadFloat(document, "point_lights.contrib_cap", 0, FLT_MAX, &out->contrib_cap, error,
								capacity) ||
		!TieAppConfig_ReadBool(document, "point_lights.training_headlight.enabled",
							   &out->training_headlight_enabled, error, capacity) ||
		!TieAppConfig_ReadVec3(document, "point_lights.training_headlight.color",
							   out->training_headlight_color, error, capacity) ||
		!TieAppConfig_ReadFloat(document, "point_lights.training_headlight.intensity", 0, FLT_MAX,
								&out->training_headlight_intensity, error, capacity) ||
		!TieAppConfig_ReadFloat(document, "point_lights.training_headlight.range_m", 0, FLT_MAX,
								&out->training_headlight_range_m, error, capacity) ||
		!TieAppConfig_ReadFloat(document, "point_lights.training_headlight.nose_offset_m", 0, FLT_MAX,
								&out->training_headlight_nose_offset_m, error, capacity))
		return false;
	if (!(out->range_scale > 0.0f))
		return TieAppConfig_ConfigError(error, capacity,
										"setting 'point_lights.range_scale' must be greater than 0");
	if (!(out->min_distance > 0.0f))
		return TieAppConfig_ConfigError(error, capacity,
										"setting 'point_lights.min_distance' must be greater than 0");
	if (!(out->training_headlight_range_m > 0.0f))
		return TieAppConfig_ConfigError(
			error, capacity, "setting 'point_lights.training_headlight.range_m' must be greater than 0");
	return true;
}

static bool TieAppConfig_ParseComplete(const AeronConfigFile* document,
									   const AeronSceneSsaoSettings* baseline_ssao,
									   const AeronSceneShadowSettings* baseline_shadows,
									   const AeronSceneTonemapSettings* baseline_tonemap, TieAppConfig* out,
									   char* error, size_t capacity) {
	int ordinal;
	memset(out, 0, sizeof *out);
	const char* frontend_version;
	const char* flight_version;
	const char* model_source;
	const char* original_renderer;
	const char* update_rate;
	const char* midi_backend;
	const char* music_source;
	if (!TieAppConfig_CheckVersion(document, error, capacity) ||
		!TieAppConfig_ReadString(document, "paths.installations.tie95", out->tie95_data,
								 sizeof out->tie95_data, error, capacity) ||
		!TieAppConfig_ReadString(document, "paths.installations.tie98", out->tie98_data,
								 sizeof out->tie98_data, error, capacity) ||
		!TieAppConfig_ReadString(document, "audio.fluidsynth.soundfont_file", out->fluidsynth_soundfont_file,
								 sizeof out->fluidsynth_soundfont_file, error, capacity) ||
		!TieAppConfig_ReadString(document, "audio.sc55.rom_directory", out->sc55_rom_directory,
								 sizeof out->sc55_rom_directory, error, capacity) ||
		!TieAppConfig_ReadString(document, "ui.font", out->ui.font, sizeof out->ui.font, error, capacity) ||
		!TieAppConfig_ReadString(document, "input.device.guid", out->controller.selector.guid,
								 sizeof out->controller.selector.guid, error, capacity) ||
		!TieAppConfig_ReadString(document, "input.device.path", out->controller.selector.path,
								 sizeof out->controller.selector.path, error, capacity) ||
		!TieAppConfig_ReadInt(document, "input.device.ordinal", 0, AERON_CONTROLLER_MAX - 1, &ordinal, error,
							  capacity))
		return false;
	if (!TieAppConfig_ValidateResourcePath("ui.font", out->ui.font, error, capacity))
		return false;
	midi_backend = AeronConfigFile_GetString(document, "audio.midi_backend", NULL);
	if (!midi_backend)
		return TieAppConfig_ConfigError(error, capacity, "missing audio.midi_backend");
	if (strcmp(midi_backend, "fluidsynth") == 0)
		out->midi_backend = TIE_MIDI_BACKEND_FLUIDSYNTH;
	else if (strcmp(midi_backend, "fm4_opl3") == 0)
		out->midi_backend = TIE_MIDI_BACKEND_FM4_OPL3;
	else if (strcmp(midi_backend, "sc55") == 0)
		out->midi_backend = TIE_MIDI_BACKEND_SC55;
	else
		return TieAppConfig_ConfigError(error, capacity, "invalid audio.midi_backend '%s'", midi_backend);
	music_source = AeronConfigFile_GetString(document, "audio.music", NULL);
	if (!music_source)
		return TieAppConfig_ConfigError(error, capacity, "missing audio.music");
	if (strcmp(music_source, "imuse") == 0)
		out->music_source = TIE_MUSIC_IMUSE;
	else if (strcmp(music_source, "tie98") == 0)
		out->music_source = TIE_MUSIC_TIE98;
	else
		return TieAppConfig_ConfigError(error, capacity, "invalid audio.music '%s'", music_source);
	if (!TieAppConfig_ReadBool(document, "audio.sb16_filter", &out->sb16_filter_enabled, error, capacity))
		return false;
	if (!TieAppConfig_ReadBool(document, "audio.prefer_tie95_frontend_voices",
							   &out->prefer_tie95_frontend_voices, error, capacity))
		return false;
	if (!TieAppConfig_ReadInt(document, "audio.player_engine_sound_volume_percent", 0, 100,
							  &out->player_engine_sound_volume_percent, error, capacity))
		return false;
	if (!TieAppConfig_ReadBool(document, "frontend.aspect_correct_legacy_scenes",
							   &out->aspect_correct_legacy_scenes, error, capacity))
		return false;
	frontend_version = AeronConfigFile_GetString(document, "frontend.version", NULL);
	flight_version = AeronConfigFile_GetString(document, "flight.version", NULL);
	if (!frontend_version || !flight_version)
		return TieAppConfig_ConfigError(error, capacity, "missing frontend.version or flight.version");
	if (strcmp(frontend_version, "tie95") == 0)
		out->frontend_version = TIE_VERSION_SELECTION_TIE95;
	else if (strcmp(frontend_version, "tie98") == 0)
		out->frontend_version = TIE_VERSION_SELECTION_TIE98;
	else
		return TieAppConfig_ConfigError(error, capacity, "invalid frontend.version '%s'", frontend_version);
	if (strcmp(flight_version, "tie95") == 0)
		out->flight_version = TIE_VERSION_SELECTION_TIE95;
	else if (strcmp(flight_version, "tie98") == 0)
		out->flight_version = TIE_VERSION_SELECTION_TIE98;
	else
		return TieAppConfig_ConfigError(error, capacity, "invalid flight.version '%s'", flight_version);
	original_renderer = AeronConfigFile_GetString(document, "flight.original_renderer", NULL);
	if (!original_renderer)
		return TieAppConfig_ConfigError(error, capacity, "missing flight.original_renderer");
	if (strcmp(original_renderer, "software") == 0)
		out->requested_tie98_original_renderer = TIE98_ORIGINAL_RENDERER_SOFTWARE;
	else if (strcmp(original_renderer, "d3d") == 0)
		out->requested_tie98_original_renderer = TIE98_ORIGINAL_RENDERER_D3D;
	else
		return TieAppConfig_ConfigError(error, capacity, "invalid flight.original_renderer '%s'",
										original_renderer);
	update_rate = AeronConfigFile_GetString(document, "flight.update_rate", "unlocked");
	if (strcmp(update_rate, "native") == 0)
		out->requested_flight_update_rate = TIE_FLIGHT_UPDATE_RATE_NATIVE;
	else if (strcmp(update_rate, "tie95") == 0)
		out->requested_flight_update_rate = TIE_FLIGHT_UPDATE_RATE_TIE95;
	else if (strcmp(update_rate, "unlocked") == 0)
		out->requested_flight_update_rate = TIE_FLIGHT_UPDATE_RATE_UNLOCKED;
	else
		return TieAppConfig_ConfigError(error, capacity, "invalid flight.update_rate '%s'", update_rate);
	model_source = AeronConfigFile_GetString(document, "flight.models.source", NULL);
	if (!model_source)
		return TieAppConfig_ConfigError(error, capacity, "missing flight.models.source");
	if (strcmp(model_source, "original") == 0)
		out->requested_model_source = TIE_FLIGHT_MODEL_SOURCE_ORIGINAL;
	else if (strcmp(model_source, "remastered") == 0)
		out->requested_model_source = TIE_FLIGHT_MODEL_SOURCE_REMASTERED;
	else
		return TieAppConfig_ConfigError(error, capacity, "invalid flight.models.source '%s'", model_source);
	if (!TieAppConfig_ReadFloat(document, "flight.models.smooth_angle_degrees", 0, 180,
								&out->flight_model_smooth_angle_degrees, error, capacity))
		return false;
	if (!TieAppConfig_ReadFloat(document, "flight.models.opt_emissive_strength", 0, FLT_MAX,
								&out->flight_model_opt_emissive_strength, error, capacity) ||
		!TieAppConfig_ReadFloat(document, "flight.models.opt_projectile_emissive_strength", 0, FLT_MAX,
								&out->flight_model_opt_projectile_emissive_strength, error, capacity))
		return false;
	if (!TieAppConfig_ReadBool(document, "flight.player_engine_sound", &out->player_engine_sound_enabled,
							   error, capacity))
		return false;
	out->controller.selector.ordinal = ordinal;
	const bool parsed =
		TieAppConfig_ParseAxisMapping(document, "gamepad", true, &out->controller.gamepad, error, capacity) &&
		TieAppConfig_ParseAxisMapping(document, "joystick", false, &out->controller.joystick, error,
									  capacity) &&
		TieAppConfig_ParseBindings(document, &out->controller, &out->keyboard, error, capacity) &&
		TieAppConfig_ParseVideo(document, &out->video, error, capacity) &&
		TieAppConfig_ParseRender(document, baseline_ssao, baseline_shadows, baseline_tonemap, &out->render,
								 error, capacity) &&
		TieAppConfig_ParsePbr(document, &out->pbr, error, capacity) &&
		TieAppConfig_ParsePointLights(document, &out->point_lights, error, capacity);
	if (parsed) {
		out->video.output.ssao_quality = out->render.ssao.ssao_quality;
		out->video.output.shadows_enabled = out->render.shadows.enabled;
		out->video.output.shadow_atlas_size = out->render.shadows.atlas_size;
		out->video.output.fsr_mode = out->render.temporal_mode;
		out->video.output.fsr_sharpness = out->render.temporal_sharpness;
		out->video.output.motion_blur_quality = out->render.motion_blur_quality;
		out->video.output.motion_blur_shutter = out->render.motion_blur_shutter;
		out->video.output.msaa_samples = out->render.msaa_samples;
		out->video.output.starfield_style = out->render.starfield_style;
	}
	return parsed;
}

static bool TieAppConfig_LogAeronError(const AeronConfigError* source, char* error, size_t capacity) {
	return TieAppConfig_ConfigError(error, capacity, "%s:%d:%d: %s", source->path, source->line,
									source->column, source->message);
}

bool TieAppConfig_Load(AeronVfs* vfs, TieAppConfigState* state, char* error, size_t capacity) {
	AeronConfigFile* shipped = NULL;
	AeronConfigFile* user = NULL;
	AeronConfigFile* merged = NULL;
	AeronConfigFile* scene_defaults = NULL;
	AeronConfigError aeron_error = { 0 };
	AeronSceneSsaoSettings baseline_ssao;
	AeronSceneShadowSettings baseline_shadows;
	AeronSceneTonemapSettings baseline_tonemap;
	TieAppConfig defaults_value;
	TieAppConfig requested_value;
	bool success = false;

	if (!vfs || !state)
		return TieAppConfig_ConfigError(error, capacity, "invalid configuration load arguments");
	if (!AeronConfigFile_LoadYamlEx(vfs, AERON_VFS_ROOT_RESOURCE, "config.yaml", &shipped, &aeron_error)) {
		TieAppConfig_LogAeronError(&aeron_error, error, capacity);
		goto done;
	}
	if (!AeronConfigFile_LoadYamlEx(vfs, AERON_VFS_ROOT_RESOURCE, "aeron/scene3d_defaults.yaml",
									&scene_defaults, &aeron_error) ||
		!AeronSceneSettings_Load(AeronConfigFile_Root(scene_defaults), &baseline_ssao, &baseline_shadows,
								 &baseline_tonemap, &aeron_error)) {
		TieAppConfig_LogAeronError(&aeron_error, error, capacity);
		goto done;
	}
	if (!TieAppConfig_CheckVersion(shipped, error, capacity) ||
		!TieAppConfig_ValidateSchemaKeys(shipped, false, error, capacity) ||
		!TieAppConfig_ParseComplete(shipped, &baseline_ssao, &baseline_shadows, &baseline_tonemap,
									&defaults_value, error, capacity))
		goto done;
	if (AeronVfs_Exists(vfs, AERON_VFS_ROOT_USER, "config.yaml")) {
		if (!AeronConfigFile_LoadYamlEx(vfs, AERON_VFS_ROOT_USER, "config.yaml", &user, &aeron_error)) {
			TieAppConfig_LogAeronError(&aeron_error, error, capacity);
			goto done;
		}
		if (!TieAppConfig_CheckVersion(user, error, capacity) ||
			!TieAppConfig_ValidateSchemaKeys(user, true, error, capacity))
			goto done;
	} else {
		if (!AeronConfigFile_CreateMap(AERON_VFS_ROOT_USER, "config.yaml", &user, &aeron_error) ||
			!AeronConfigFile_SetInt(user, "version", 4, &aeron_error)) {
			TieAppConfig_LogAeronError(&aeron_error, error, capacity);
			goto done;
		}
	}
	if (!AeronConfigFile_Overlay(shipped, user, &merged, &aeron_error)) {
		TieAppConfig_LogAeronError(&aeron_error, error, capacity);
		goto done;
	}
	if (!TieAppConfig_ParseComplete(merged, &baseline_ssao, &baseline_shadows, &baseline_tonemap,
									&requested_value, error, capacity))
		goto done;
	TieAppConfig_Destroy(state);
	state->defaults = defaults_value;
	state->requested = requested_value;
	state->shipped_document = shipped;
	state->user_document = user;
	shipped = NULL;
	user = NULL;
	g_current_config = state;
	success = true;
done:
	AeronConfigFile_Destroy(scene_defaults);
	AeronConfigFile_Destroy(merged);
	AeronConfigFile_Destroy(user);
	AeronConfigFile_Destroy(shipped);
	return success;
}

void TieAppConfig_Destroy(TieAppConfigState* state) {
	if (!state)
		return;
	if (g_current_config == state)
		g_current_config = NULL;
	AeronConfigFile_Destroy(state->shipped_document);
	AeronConfigFile_Destroy(state->user_document);
	memset(state, 0, sizeof *state);
}

TieAppConfigState* TieAppConfig_Current(void) { return g_current_config; }

static bool TieAppConfig_ReplaceUserCandidate(TieAppConfigState* state, AeronConfigFile* candidate,
											  char* error, size_t capacity) {
	AeronConfigFile* merged = NULL;
	AeronConfigError aeron_error = { 0 };
	TieAppConfig requested;
	if (!AeronConfigFile_Overlay(state->shipped_document, candidate, &merged, &aeron_error))
		return TieAppConfig_LogAeronError(&aeron_error, error, capacity);
	if (!TieAppConfig_ParseComplete(merged, &state->defaults.render.ssao, &state->defaults.render.shadows,
									&state->defaults.render.tonemap, &requested, error, capacity)) {
		AeronConfigFile_Destroy(merged);
		return false;
	}
	AeronConfigFile_Destroy(merged);
	AeronConfigFile_Destroy(state->user_document);
	state->user_document = candidate;
	state->requested = requested;
	state->dirty = true;
	return true;
}

bool TieAppConfig_SetInstallation(TieAppConfigState* state, TieGameVersion version, const char* path,
								  char* error, size_t capacity) {
	AeronConfigFile* candidate = NULL;
	AeronConfigError aeron_error = { 0 };
	if (!state || !path || (version != TIE_GAME_VERSION_TIE95 && version != TIE_GAME_VERSION_TIE98) ||
		strlen(path) >= TIE_GAME_DATA_PATH_MAX)
		return TieAppConfig_ConfigError(error, capacity, "selected game-data path is invalid or too long");
	const char* key =
		version == TIE_GAME_VERSION_TIE98 ? "paths.installations.tie98" : "paths.installations.tie95";
	if (!AeronConfigFile_Clone(state->user_document, &candidate, &aeron_error) ||
		!AeronConfigFile_SetString(candidate, key, path, &aeron_error)) {
		AeronConfigFile_Destroy(candidate);
		return TieAppConfig_LogAeronError(&aeron_error, error, capacity);
	}
	if (!TieAppConfig_ReplaceUserCandidate(state, candidate, error, capacity)) {
		AeronConfigFile_Destroy(candidate);
		return false;
	}
	return true;
}

static const char* TieAppConfig_VersionSelectionName(TieVersionSelection selection) {
	switch (selection) {
		case TIE_VERSION_SELECTION_TIE95:
			return "tie95";
		case TIE_VERSION_SELECTION_TIE98:
			return "tie98";
	}
	return NULL;
}

static const char* TieAppConfig_MidiBackendName(TieMidiBackendKind backend) {
	switch (backend) {
		case TIE_MIDI_BACKEND_FLUIDSYNTH:
			return "fluidsynth";
		case TIE_MIDI_BACKEND_FM4_OPL3:
			return "fm4_opl3";
		case TIE_MIDI_BACKEND_SC55:
			return "sc55";
		case TIE_MIDI_BACKEND_NONE:
			return NULL;
	}
	return NULL;
}

void TieAppConfig_GetLiveFlightOptions(const TieAppConfig* config, TieAppLiveFlightOptions* out) {
	if (!config || !out)
		return;
	*out = (TieAppLiveFlightOptions) {
		.aspect_correct_legacy_scenes = config->aspect_correct_legacy_scenes,
		.tie98_original_renderer = config->requested_tie98_original_renderer,
		.update_rate = config->requested_flight_update_rate,
		.player_engine_sound_enabled = config->player_engine_sound_enabled,
		.player_engine_sound_volume_percent = config->player_engine_sound_volume_percent,
	};
}

void TieAppConfig_GetLaunchOptions(const TieAppConfig* config, TieAppLaunchOptions* out) {
	if (!config || !out)
		return;
	memset(out, 0, sizeof *out);
	snprintf(out->tie95_data, sizeof out->tie95_data, "%s", config->tie95_data);
	snprintf(out->tie98_data, sizeof out->tie98_data, "%s", config->tie98_data);
	snprintf(out->fluidsynth_soundfont_file, sizeof out->fluidsynth_soundfont_file, "%s",
			 config->fluidsynth_soundfont_file);
	snprintf(out->sc55_rom_directory, sizeof out->sc55_rom_directory, "%s", config->sc55_rom_directory);
	out->frontend_version = config->frontend_version;
	out->flight_version = config->flight_version;
	out->model_source = config->requested_model_source;
	out->midi_backend = config->midi_backend;
	out->sb16_filter_enabled = config->sb16_filter_enabled;
	out->music_source = config->music_source;
}

bool TieAppConfig_SetLiveFlightOptions(TieAppConfigState* state, const TieAppLiveFlightOptions* options,
									   char* error, size_t capacity) {
	AeronConfigFile* candidate = NULL;
	AeronConfigError aeron_error = { 0 };
	if (!state || !options ||
		(options->tie98_original_renderer != TIE98_ORIGINAL_RENDERER_SOFTWARE &&
		 options->tie98_original_renderer != TIE98_ORIGINAL_RENDERER_D3D) ||
		(options->update_rate != TIE_FLIGHT_UPDATE_RATE_NATIVE &&
		 options->update_rate != TIE_FLIGHT_UPDATE_RATE_TIE95 &&
		 options->update_rate != TIE_FLIGHT_UPDATE_RATE_UNLOCKED) ||
		(unsigned int)options->player_engine_sound_volume_percent > 100u)
		return TieAppConfig_ConfigError(error, capacity, "invalid live flight settings");
	if (!AeronConfigFile_Clone(state->user_document, &candidate, &aeron_error) ||
		!AeronConfigFile_SetBool(candidate, "frontend.aspect_correct_legacy_scenes",
								 options->aspect_correct_legacy_scenes, &aeron_error) ||
		!AeronConfigFile_SetString(
			candidate, "flight.original_renderer",
			options->tie98_original_renderer == TIE98_ORIGINAL_RENDERER_D3D ? "d3d" : "software",
			&aeron_error) ||
		!AeronConfigFile_SetString(candidate, "flight.update_rate",
								   options->update_rate == TIE_FLIGHT_UPDATE_RATE_UNLOCKED ? "unlocked"
								   : options->update_rate == TIE_FLIGHT_UPDATE_RATE_TIE95  ? "tie95"
																						   : "native",
								   &aeron_error) ||
		!AeronConfigFile_SetBool(candidate, "flight.player_engine_sound",
								 options->player_engine_sound_enabled, &aeron_error) ||
		!AeronConfigFile_SetInt(candidate, "audio.player_engine_sound_volume_percent",
								options->player_engine_sound_volume_percent, &aeron_error)) {
		AeronConfigFile_Destroy(candidate);
		return TieAppConfig_LogAeronError(&aeron_error, error, capacity);
	}
	if (!TieAppConfig_ReplaceUserCandidate(state, candidate, error, capacity)) {
		AeronConfigFile_Destroy(candidate);
		return false;
	}
	return true;
}

bool TieAppConfig_SetLaunchOptions(TieAppConfigState* state, const TieAppLaunchOptions* options, char* error,
								   size_t capacity) {
	AeronConfigFile* candidate = NULL;
	AeronConfigError aeron_error = { 0 };
	const char* frontend_name = options ? TieAppConfig_VersionSelectionName(options->frontend_version) : NULL;
	const char* flight_name = options ? TieAppConfig_VersionSelectionName(options->flight_version) : NULL;
	const char* backend_name = options ? TieAppConfig_MidiBackendName(options->midi_backend) : NULL;
	if (!state || !options || !frontend_name || !flight_name || !backend_name ||
		strlen(options->tie95_data) >= TIE_GAME_DATA_PATH_MAX ||
		strlen(options->tie98_data) >= TIE_GAME_DATA_PATH_MAX ||
		strlen(options->fluidsynth_soundfont_file) >= TIE_GAME_DATA_PATH_MAX ||
		strlen(options->sc55_rom_directory) >= TIE_GAME_DATA_PATH_MAX ||
		(options->model_source != TIE_FLIGHT_MODEL_SOURCE_ORIGINAL &&
		 options->model_source != TIE_FLIGHT_MODEL_SOURCE_REMASTERED) ||
		(options->music_source != TIE_MUSIC_IMUSE && options->music_source != TIE_MUSIC_TIE98))
		return TieAppConfig_ConfigError(error, capacity, "invalid launch settings");
	if (options->midi_backend == TIE_MIDI_BACKEND_SC55 && options->music_source == TIE_MUSIC_IMUSE &&
		options->sc55_rom_directory[0] == '\0')
		return TieAppConfig_ConfigError(error, capacity,
										"select an SC-55 ROM directory before closing settings");
	if (!AeronConfigFile_Clone(state->user_document, &candidate, &aeron_error) ||
		!AeronConfigFile_SetString(candidate, "paths.installations.tie95", options->tie95_data,
								   &aeron_error) ||
		!AeronConfigFile_SetString(candidate, "paths.installations.tie98", options->tie98_data,
								   &aeron_error) ||
		!AeronConfigFile_SetString(candidate, "audio.midi_backend", backend_name, &aeron_error) ||
		!AeronConfigFile_SetString(candidate, "audio.fluidsynth.soundfont_file",
								   options->fluidsynth_soundfont_file, &aeron_error) ||
		!AeronConfigFile_SetString(candidate, "audio.sc55.rom_directory", options->sc55_rom_directory,
								   &aeron_error) ||
		!AeronConfigFile_SetString(candidate, "frontend.version", frontend_name, &aeron_error) ||
		!AeronConfigFile_SetString(candidate, "flight.version", flight_name, &aeron_error) ||
		!AeronConfigFile_SetString(candidate, "flight.models.source",
								   options->model_source == TIE_FLIGHT_MODEL_SOURCE_REMASTERED ? "remastered"
																							   : "original",
								   &aeron_error) ||
		!AeronConfigFile_SetBool(candidate, "audio.sb16_filter", options->sb16_filter_enabled,
								 &aeron_error) ||
		!AeronConfigFile_SetString(candidate, "audio.music",
								   options->music_source == TIE_MUSIC_TIE98 ? "tie98" : "imuse",
								   &aeron_error)) {
		AeronConfigFile_Destroy(candidate);
		return TieAppConfig_LogAeronError(&aeron_error, error, capacity);
	}
	if (!TieAppConfig_ReplaceUserCandidate(state, candidate, error, capacity)) {
		AeronConfigFile_Destroy(candidate);
		return false;
	}
	return true;
}

static bool TieAppConfig_ResolveVersion(TieVersionSelection selection, bool has_tie95, bool has_tie98,
										TieGameVersion* out, const char* setting, char* error,
										size_t capacity) {
	if (selection != TIE_VERSION_SELECTION_TIE95 && selection != TIE_VERSION_SELECTION_TIE98)
		return TieAppConfig_ConfigError(error, capacity, "%s has an invalid version selection", setting);
	const TieGameVersion version =
		selection == TIE_VERSION_SELECTION_TIE98 ? TIE_GAME_VERSION_TIE98 : TIE_GAME_VERSION_TIE95;
	if ((version == TIE_GAME_VERSION_TIE95 && !has_tie95) ||
		(version == TIE_GAME_VERSION_TIE98 && !has_tie98))
		return TieAppConfig_ConfigError(error, capacity,
										"%s selects %s, but that installation is unavailable", setting,
										version == TIE_GAME_VERSION_TIE98 ? "tie98" : "tie95");
	*out = version;
	return true;
}

bool TieAppConfig_ResolveFlightProfile(const TieAppConfig* config, bool has_tie95, bool has_tie98,
									   TieFlightProfile* out, char* error, size_t capacity) {
	TieGameVersion flight_version;
	if (!config || !out || (!has_tie95 && !has_tie98))
		return TieAppConfig_ConfigError(error, capacity,
										"at least one complete TIE installation is required");
	if (!TieAppConfig_ResolveVersion(config->flight_version, has_tie95, has_tie98, &flight_version,
									 "flight.version", error, capacity))
		return false;
	*out = (TieFlightProfile) {
		.version = flight_version,
		.model_source = flight_version == TIE_GAME_VERSION_TIE95 ? TIE_FLIGHT_MODEL_SOURCE_ORIGINAL
																 : config->requested_model_source,
		.tie98_original_renderer = flight_version == TIE_GAME_VERSION_TIE95
									   ? TIE98_ORIGINAL_RENDERER_SOFTWARE
									   : config->requested_tie98_original_renderer,
		.update_rate = config->requested_flight_update_rate,
		.player_engine_sound_enabled =
			flight_version == TIE_GAME_VERSION_TIE98 && config->player_engine_sound_enabled,
		.player_engine_sound_volume_percent = config->player_engine_sound_volume_percent,
	};
	return true;
}

bool TieAppConfig_ResolveLaunch(const TieAppConfig* config, bool has_tie95, bool has_tie98,
								TieLaunchConfig* out, char* error, size_t capacity) {
	TieGameVersion frontend_version;
	if (!config || !out || (!has_tie95 && !has_tie98))
		return TieAppConfig_ConfigError(error, capacity,
										"at least one complete TIE installation is required");
	if (!TieAppConfig_ResolveVersion(config->frontend_version, has_tie95, has_tie98, &frontend_version,
									 "frontend.version", error, capacity) ||
		!TieAppConfig_ResolveFlightProfile(config, has_tie95, has_tie98, &out->flight_profile, error,
										   capacity))
		return false;
	out->frontend_version = frontend_version;
	return true;
}

static bool TieAppConfig_SetBoolOverride(AeronConfigFile* document, const char* path, bool value,
										 bool default_value, AeronConfigError* error) {
	return value == default_value ? AeronConfigFile_Remove(document, path, error)
								  : AeronConfigFile_SetBool(document, path, value, error);
}

static bool TieAppConfig_SetIntOverride(AeronConfigFile* document, const char* path, int value,
										int default_value, AeronConfigError* error) {
	return value == default_value ? AeronConfigFile_Remove(document, path, error)
								  : AeronConfigFile_SetInt(document, path, value, error);
}

static bool TieAppConfig_SetFloatOverride(AeronConfigFile* document, const char* path, float value,
										  float default_value, AeronConfigError* error) {
	return value == default_value ? AeronConfigFile_Remove(document, path, error)
								  : AeronConfigFile_SetFloat(document, path, value, error);
}

static bool TieAppConfig_SetStringOverride(AeronConfigFile* document, const char* path, const char* value,
										   const char* default_value, AeronConfigError* error) {
	return strcmp(value, default_value) == 0 ? AeronConfigFile_Remove(document, path, error)
											 : AeronConfigFile_SetString(document, path, value, error);
}

static bool TieAppConfig_VideoPaperWhiteEqual(const TieVideoOptions* left, const TieVideoOptions* right) {
	return left->paper_white_auto == right->paper_white_auto &&
		   (left->paper_white_auto || left->paper_white_nits == right->paper_white_nits);
}

static bool TieAppConfig_SetVideoOverrides(AeronConfigFile* document, const TieAppVideoConfig* defaults,
										   const TieAppVideoConfig* video, AeronConfigError* error) {
	static const char* const temporal_modes[] = { "off", "native_aa", "quality", "balanced", "performance" };
	static const char* const gamma_names[] = { "2.2", "2.4", "srgb" };
	static const char* const starfield_styles[] = { "tie95", "tie98" };
	const int temporal_mode =
		video->output.msaa_samples > 1 ? TIE_FLIGHT_TEMPORAL_OFF : video->output.fsr_mode;
	const int default_temporal_mode =
		defaults->output.msaa_samples > 1 ? TIE_FLIGHT_TEMPORAL_OFF : defaults->output.fsr_mode;
	if (temporal_mode < TIE_FLIGHT_TEMPORAL_OFF || temporal_mode > TIE_FLIGHT_TEMPORAL_PERFORMANCE ||
		default_temporal_mode < TIE_FLIGHT_TEMPORAL_OFF ||
		default_temporal_mode > TIE_FLIGHT_TEMPORAL_PERFORMANCE ||
		video->output.starfield_style < TIE_FLIGHT_STARFIELD_STYLE_TIE95 ||
		video->output.starfield_style > TIE_FLIGHT_STARFIELD_STYLE_TIE98 ||
		defaults->output.starfield_style < TIE_FLIGHT_STARFIELD_STYLE_TIE95 ||
		defaults->output.starfield_style > TIE_FLIGHT_STARFIELD_STYLE_TIE98)
		return false;
	if (!TieAppConfig_SetBoolOverride(document, "video.fullscreen", video->fullscreen, defaults->fullscreen,
									  error) ||
		!TieAppConfig_SetBoolOverride(document, "video.hdr", video->output.hdr, defaults->output.hdr, error))
		return false;
	if (video->output.sdr_content_gamma < TIE_SDR_CONTENT_GAMMA_2_2 ||
		video->output.sdr_content_gamma > TIE_SDR_CONTENT_GAMMA_SRGB ||
		defaults->output.sdr_content_gamma < TIE_SDR_CONTENT_GAMMA_2_2 ||
		defaults->output.sdr_content_gamma > TIE_SDR_CONTENT_GAMMA_SRGB)
		return false;
	if (!TieAppConfig_SetStringOverride(document, "video.sdr_content_gamma",
										gamma_names[video->output.sdr_content_gamma],
										gamma_names[defaults->output.sdr_content_gamma], error)) {
		return false;
	}
	if (TieAppConfig_VideoPaperWhiteEqual(&video->output, &defaults->output)) {
		if (!AeronConfigFile_Remove(document, "video.paper_white_nits", error))
			return false;
	} else if (video->output.paper_white_auto) {
		if (!AeronConfigFile_SetString(document, "video.paper_white_nits", "auto", error))
			return false;
	} else if (!AeronConfigFile_SetFloat(document, "video.paper_white_nits", video->output.paper_white_nits,
										 error)) {
		return false;
	}
	return TieAppConfig_SetIntOverride(document, "render.ssao.quality", video->output.ssao_quality,
									   defaults->output.ssao_quality, error) &&
		   TieAppConfig_SetStringOverride(document, "render.shadows.mode",
										  video->output.shadows_enabled ? "pcf" : "off",
										  defaults->output.shadows_enabled ? "pcf" : "off", error) &&
		   TieAppConfig_SetIntOverride(document, "render.shadows.atlas_size", video->output.shadow_atlas_size,
									   defaults->output.shadow_atlas_size, error) &&
		   TieAppConfig_SetStringOverride(document, "render.temporal_upscaling.mode",
										  temporal_modes[temporal_mode],
										  temporal_modes[default_temporal_mode], error) &&
		   TieAppConfig_SetFloatOverride(document, "render.temporal_upscaling.sharpness",
										 video->output.fsr_sharpness, defaults->output.fsr_sharpness,
										 error) &&
		   TieAppConfig_SetIntOverride(document, "render.motion_blur.quality",
									   video->output.motion_blur_quality,
									   defaults->output.motion_blur_quality, error) &&
		   TieAppConfig_SetFloatOverride(document, "render.motion_blur.shutter",
										 video->output.motion_blur_shutter,
										 defaults->output.motion_blur_shutter, error) &&
		   TieAppConfig_SetIntOverride(document, "render.msaa_samples", video->output.msaa_samples,
									   defaults->output.msaa_samples, error) &&
		   TieAppConfig_SetStringOverride(document, "render.starfield_style",
										  starfield_styles[video->output.starfield_style],
										  starfield_styles[defaults->output.starfield_style], error);
}

bool TieAppConfig_SetVideo(TieAppConfigState* state, const TieAppVideoConfig* video, char* error,
						   size_t capacity) {
	AeronConfigFile* candidate = NULL;
	AeronConfigError aeron_error = { 0 };
	if (!state || !video)
		return TieAppConfig_ConfigError(error, capacity, "application configuration is unavailable");
	if (!AeronConfigFile_Clone(state->user_document, &candidate, &aeron_error) ||
		!TieAppConfig_SetVideoOverrides(candidate, &state->defaults.video, video, &aeron_error)) {
		AeronConfigFile_Destroy(candidate);
		return TieAppConfig_LogAeronError(&aeron_error, error, capacity);
	}
	if (!TieAppConfig_ReplaceUserCandidate(state, candidate, error, capacity)) {
		AeronConfigFile_Destroy(candidate);
		return false;
	}
	return true;
}

bool TieAppConfig_RestoreVideo(TieAppConfigState* state, char* error, size_t capacity) {
	static const char* const paths[] = { "video.fullscreen",
										 "video.hdr",
										 "video.sdr_content_gamma",
										 "video.paper_white_nits",
										 "render.ssao.quality",
										 "render.shadows.mode",
										 "render.shadows.atlas_size",
										 "render.temporal_upscaling.mode",
										 "render.temporal_upscaling.sharpness",
										 "render.motion_blur.quality",
										 "render.motion_blur.shutter",
										 "render.msaa_samples",
										 "render.starfield_style" };
	AeronConfigFile* candidate = NULL;
	AeronConfigError aeron_error = { 0 };
	size_t index;
	if (!state)
		return TieAppConfig_ConfigError(error, capacity, "application configuration is unavailable");
	if (!AeronConfigFile_Clone(state->user_document, &candidate, &aeron_error))
		return TieAppConfig_LogAeronError(&aeron_error, error, capacity);
	for (index = 0; index < sizeof paths / sizeof paths[0]; ++index) {
		if (!AeronConfigFile_Remove(candidate, paths[index], &aeron_error)) {
			AeronConfigFile_Destroy(candidate);
			return TieAppConfig_LogAeronError(&aeron_error, error, capacity);
		}
	}
	if (!TieAppConfig_ReplaceUserCandidate(state, candidate, error, capacity)) {
		AeronConfigFile_Destroy(candidate);
		return false;
	}
	return true;
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

static bool TieAppConfig_SetControllerButtons(AeronConfigFile* document, const char* path,
											  const TieControllerProfile* profile, bool gamepad,
											  AeronConfigError* error) {
	TieAppConfigControllerYamlScratch scratch = { 0 };
	AeronConfigValue root = { .type = AERON_CONFIG_MAP };
	size_t source_count = 0;
	for (int action = TIE_INPUT_ACTION_NONE + 1; action < TIE_INPUT_ACTION_COUNT; ++action) {
		const size_t action_index = (size_t)(action - 1);
		const size_t first = source_count;
		for (size_t index = 0; index < profile->binding_count; ++index) {
			if ((int)profile->bindings[index].action != action)
				continue;
			TieAppConfig_ControllerSourceYaml(&profile->bindings[index].source, gamepad, &scratch,
											  source_count++);
		}
		scratch.action_values[action_index].type = AERON_CONFIG_SEQUENCE;
		scratch.action_values[action_index].value.sequence.values = &scratch.sources[first];
		scratch.action_values[action_index].value.sequence.count = source_count - first;
		scratch.action_map[action_index].key = TieInputActions_ToName((TieInputAction)action);
		scratch.action_map[action_index].value = &scratch.action_values[action_index];
	}
	root.value.map.entries = scratch.action_map;
	root.value.map.count = TIE_INPUT_ACTION_COUNT - 1;
	return AeronConfigFile_SetValue(document, path, &root, error) != 0;
}

static bool TieAppConfig_SetControllerAxes(AeronConfigFile* document, const char* domain,
										   const TieControllerProfile* profile, bool gamepad,
										   AeronConfigError* error) {
	static const char* const names[TIE_INPUT_AXIS_COUNT] = { "yaw", "pitch", "roll", "throttle" };
	for (int axis = 0; axis < TIE_INPUT_AXIS_COUNT; ++axis) {
		const TieInputAxisBinding* binding = &profile->mapping.axes[axis];
		char path[128];
		snprintf(path, sizeof path, "input.%s.axes.%s.source", domain, names[axis]);
		if (binding->source < 0) {
			if (!AeronConfigFile_SetString(document, path, "none", error))
				return false;
		} else if (gamepad) {
			if (!AeronConfigFile_SetString(document, path,
										   Aeron_GamepadAxisName((AeronGamepadAxis)binding->source), error))
				return false;
		} else if (!AeronConfigFile_SetInt(document, path, binding->source, error)) {
			return false;
		}
		snprintf(path, sizeof path, "input.%s.axes.%s.invert", domain, names[axis]);
		if (!AeronConfigFile_SetBool(document, path, binding->invert, error))
			return false;
		snprintf(path, sizeof path, "input.%s.axes.%s.deadzone", domain, names[axis]);
		if (!AeronConfigFile_SetFloat(document, path, binding->deadzone, error))
			return false;
	}
	return true;
}

static bool TieAppConfig_ControllerOptionsValid(const TieControllerOptions* controller, char* error,
												size_t capacity) {
	if (!controller || controller->selector.ordinal < 0 ||
		controller->selector.ordinal >= AERON_CONTROLLER_MAX ||
		!memchr(controller->selector.guid, '\0', sizeof controller->selector.guid) ||
		!memchr(controller->selector.path, '\0', sizeof controller->selector.path))
		return TieAppConfig_ConfigError(error, capacity, "controller selector is invalid");
	return TieControllerMapping_Profilevalidate(&controller->gamepad, AERON_CONTROLLER_KIND_GAMEPAD, error,
												capacity) &&
		   TieControllerMapping_Profilevalidate(&controller->joystick, AERON_CONTROLLER_KIND_JOYSTICK, error,
												capacity);
}

bool TieAppConfig_SetController(TieAppConfigState* state, const TieControllerOptions* controller, char* error,
								size_t capacity) {
	AeronConfigFile* candidate = NULL;
	AeronConfigError aeron_error = { 0 };
	if (!state)
		return TieAppConfig_ConfigError(error, capacity, "application configuration is unavailable");
	if (!TieAppConfig_ControllerOptionsValid(controller, error, capacity))
		return false;
	if (!AeronConfigFile_Clone(state->user_document, &candidate, &aeron_error) ||
		!AeronConfigFile_SetString(candidate, "input.device.guid", controller->selector.guid, &aeron_error) ||
		!AeronConfigFile_SetString(candidate, "input.device.path", controller->selector.path, &aeron_error) ||
		!AeronConfigFile_SetInt(candidate, "input.device.ordinal", controller->selector.ordinal,
								&aeron_error) ||
		!TieAppConfig_SetControllerAxes(candidate, "gamepad", &controller->gamepad, true, &aeron_error) ||
		!TieAppConfig_SetControllerAxes(candidate, "joystick", &controller->joystick, false, &aeron_error) ||
		!TieAppConfig_SetControllerButtons(candidate, "input.gamepad.buttons", &controller->gamepad, true,
										   &aeron_error) ||
		!TieAppConfig_SetControllerButtons(candidate, "input.joystick.buttons", &controller->joystick, false,
										   &aeron_error)) {
		AeronConfigFile_Destroy(candidate);
		return TieAppConfig_LogAeronError(&aeron_error, error, capacity);
	}
	if (!TieAppConfig_ReplaceUserCandidate(state, candidate, error, capacity)) {
		AeronConfigFile_Destroy(candidate);
		return false;
	}
	return true;
}

static bool TieAppConfig_RestoreUserPaths(TieAppConfigState* state, const char* const* paths,
										  size_t path_count, char* error, size_t capacity) {
	AeronConfigFile* candidate = NULL;
	AeronConfigError aeron_error = { 0 };
	size_t index;
	if (!state)
		return TieAppConfig_ConfigError(error, capacity, "application configuration is unavailable");
	if (!AeronConfigFile_Clone(state->user_document, &candidate, &aeron_error))
		return TieAppConfig_LogAeronError(&aeron_error, error, capacity);
	for (index = 0; index < path_count; ++index) {
		if (!AeronConfigFile_Remove(candidate, paths[index], &aeron_error)) {
			AeronConfigFile_Destroy(candidate);
			return TieAppConfig_LogAeronError(&aeron_error, error, capacity);
		}
	}
	if (!TieAppConfig_ReplaceUserCandidate(state, candidate, error, capacity)) {
		AeronConfigFile_Destroy(candidate);
		return false;
	}
	return true;
}

bool TieAppConfig_RestoreController(TieAppConfigState* state, char* error, size_t capacity) {
	static const char* const paths[] = { "input.device", "input.gamepad", "input.joystick" };
	if (!TieAppConfig_RestoreUserPaths(state, paths, sizeof paths / sizeof paths[0], error, capacity))
		return false;
	return true;
}

static const char* TieAppConfig_ShadowFitModeName(uint32_t mode) {
	switch (mode) {
		case AERON_SCENE_SHADOW_FIT_STABLE:
			return "stable";
		case AERON_SCENE_SHADOW_FIT_FRUSTUM:
			return "frustum";
		case AERON_SCENE_SHADOW_FIT_SCENE_DEPENDENT:
			return "scene_dependent";
	}
	return NULL;
}

static bool TieAppConfig_SetShadowValues(AeronConfigFile* document, const AeronSceneShadowSettings* shadows,
										 AeronConfigError* error) {
	const char* fit_mode = TieAppConfig_ShadowFitModeName(shadows->fit_mode);
	if (!fit_mode)
		return false;
	return AeronConfigFile_SetString(document, "render.shadows.mode", shadows->enabled ? "pcf" : "off",
									 error) &&
		   AeronConfigFile_SetInt(document, "render.shadows.atlas_size", shadows->atlas_size, error) &&
		   AeronConfigFile_SetInt(document, "render.shadows.cascade_count", shadows->cascade_count, error) &&
		   AeronConfigFile_SetString(document, "render.shadows.fit_mode", fit_mode, error) &&
		   AeronConfigFile_SetFloat(document, "render.shadows.max_distance", shadows->max_distance, error) &&
		   AeronConfigFile_SetFloat(document, "render.shadows.split_lambda", shadows->split_lambda, error) &&
		   AeronConfigFile_SetBool(document, "render.shadows.explicit_splits", shadows->explicit_splits,
								   error) &&
		   AeronConfigFile_SetFloat(document, "render.shadows.split_1", shadows->split_positions[0], error) &&
		   AeronConfigFile_SetFloat(document, "render.shadows.split_2", shadows->split_positions[1], error) &&
		   AeronConfigFile_SetFloat(document, "render.shadows.split_3", shadows->split_positions[2], error) &&
		   AeronConfigFile_SetInt(document, "render.shadows.filter_quality", shadows->filter_quality,
								  error) &&
		   AeronConfigFile_SetFloat(document, "render.shadows.filter_radius", shadows->filter_radius,
									error) &&
		   AeronConfigFile_SetBool(document, "render.shadows.contact_hardening", shadows->contact_hardening,
								   error) &&
		   AeronConfigFile_SetFloat(document, "render.shadows.light_angular_radius_degrees",
									shadows->light_angular_radius_degrees, error) &&
		   AeronConfigFile_SetFloat(document, "render.shadows.max_filter_radius", shadows->max_filter_radius,
									error) &&
		   AeronConfigFile_SetFloat(document, "render.shadows.pcss_min_filter_radius",
									shadows->pcss_min_filter_radius, error) &&
		   AeronConfigFile_SetFloat(document, "render.shadows.normal_bias_texels",
									shadows->normal_bias_texels, error) &&
		   AeronConfigFile_SetFloat(document, "render.shadows.depth_bias_texels", shadows->depth_bias_texels,
									error) &&
		   AeronConfigFile_SetFloat(document, "render.shadows.transition_fraction",
									shadows->transition_fraction, error) &&
		   AeronConfigFile_SetFloat(document, "render.shadows.distance_fade_fraction",
									shadows->distance_fade_fraction, error) &&
		   AeronConfigFile_SetBool(document, "render.shadows.debug_cascades", shadows->debug_cascades, error);
}

bool TieAppConfig_SetShadows(TieAppConfigState* state, const AeronSceneShadowSettings* shadows, char* error,
							 size_t capacity) {
	AeronConfigFile* candidate = NULL;
	AeronConfigError aeron_error = { 0 };
	if (!state || !shadows)
		return TieAppConfig_ConfigError(error, capacity, "application configuration is unavailable");
	if (!TieAppConfig_ShadowFitModeName(shadows->fit_mode))
		return TieAppConfig_ConfigError(error, capacity, "invalid directional-shadow fit mode");
	if (!AeronConfigFile_Clone(state->user_document, &candidate, &aeron_error) ||
		!TieAppConfig_SetShadowValues(candidate, shadows, &aeron_error)) {
		AeronConfigFile_Destroy(candidate);
		return TieAppConfig_LogAeronError(&aeron_error, error, capacity);
	}
	if (!TieAppConfig_ReplaceUserCandidate(state, candidate, error, capacity)) {
		AeronConfigFile_Destroy(candidate);
		return false;
	}
	return true;
}

bool TieAppConfig_RestoreShadows(TieAppConfigState* state, char* error, size_t capacity) {
	static const char* const paths[] = { "render.shadows" };
	return TieAppConfig_RestoreUserPaths(state, paths, 1, error, capacity);
}

static bool TieAppConfig_SetSsaoValues(AeronConfigFile* document, const AeronSceneSsaoSettings* ssao,
									   AeronConfigError* error) {
	return AeronConfigFile_SetInt(document, "render.ssao.quality", ssao->ssao_quality, error) &&
		   AeronConfigFile_SetFloat(document, "render.ssao.intensity", ssao->ssao_intensity, error) &&
		   AeronConfigFile_SetFloat(document, "render.ssao.power", ssao->ssao_power, error) &&
		   AeronConfigFile_SetFloat(document, "render.ssao.radius_view", ssao->ssao_radius_view, error) &&
		   AeronConfigFile_SetFloat(document, "render.ssao.bias_view", ssao->ssao_bias_view, error) &&
		   AeronConfigFile_SetFloat(document, "render.ssao.direct", ssao->ssao_direct, error) &&
		   AeronConfigFile_SetBool(document, "render.ssao.debug_viz", ssao->ssao_debug_viz, error) &&
		   AeronConfigFile_SetFloat(document, "render.ssao.min_screen_frac", ssao->ssao_min_screen_frac,
									error) &&
		   AeronConfigFile_SetFloat(document, "render.ssao.max_screen_frac", ssao->ssao_max_screen_frac,
									error) &&
		   AeronConfigFile_SetFloat(document, "render.ssao.sample_jitter", ssao->ssao_sample_jitter, error);
}

bool TieAppConfig_SetSsao(TieAppConfigState* state, const AeronSceneSsaoSettings* ssao, char* error,
						  size_t capacity) {
	AeronConfigFile* candidate = NULL;
	AeronConfigError aeron_error = { 0 };
	if (!state || !ssao)
		return TieAppConfig_ConfigError(error, capacity, "application configuration is unavailable");
	if (!AeronConfigFile_Clone(state->user_document, &candidate, &aeron_error) ||
		!TieAppConfig_SetSsaoValues(candidate, ssao, &aeron_error)) {
		AeronConfigFile_Destroy(candidate);
		return TieAppConfig_LogAeronError(&aeron_error, error, capacity);
	}
	if (!TieAppConfig_ReplaceUserCandidate(state, candidate, error, capacity)) {
		AeronConfigFile_Destroy(candidate);
		return false;
	}
	return true;
}

bool TieAppConfig_RestoreSsao(TieAppConfigState* state, char* error, size_t capacity) {
	static const char* const paths[] = { "render.ssao" };
	return TieAppConfig_RestoreUserPaths(state, paths, 1, error, capacity);
}

bool TieAppConfig_SetPbrGlobals(TieAppConfigState* state, const TieFlightPbrConfig* pbr, char* error,
								size_t capacity) {
	AeronConfigFile* candidate = NULL;
	AeronConfigError aeron_error = { 0 };
	if (!state || !pbr)
		return TieAppConfig_ConfigError(error, capacity, "application configuration is unavailable");
	if (!AeronConfigFile_Clone(state->user_document, &candidate, &aeron_error) ||
		!AeronConfigFile_SetFloat(candidate, "pbr.light_intensity", pbr->light_intensity, &aeron_error) ||
		!AeronConfigFile_SetFloat(candidate, "pbr.global_specular_multiplier",
								  pbr->global_specular_multiplier, &aeron_error) ||
		!AeronConfigFile_SetFloat(candidate, "pbr.light_wrap", pbr->light_wrap, &aeron_error) ||
		!AeronConfigFile_SetBool(candidate, "pbr.geometric_specular_adaptation",
								 pbr->geometric_specular_adaptation, &aeron_error)) {
		AeronConfigFile_Destroy(candidate);
		return TieAppConfig_LogAeronError(&aeron_error, error, capacity);
	}
	if (!TieAppConfig_ReplaceUserCandidate(state, candidate, error, capacity)) {
		AeronConfigFile_Destroy(candidate);
		return false;
	}
	return true;
}

bool TieAppConfig_RestorePbrGlobals(TieAppConfigState* state, char* error, size_t capacity) {
	static const char* const paths[] = { "pbr.light_intensity", "pbr.global_specular_multiplier",
										 "pbr.light_wrap", "pbr.geometric_specular_adaptation" };
	return TieAppConfig_RestoreUserPaths(state, paths, sizeof paths / sizeof paths[0], error, capacity);
}

bool TieAppConfig_Save(AeronVfs* vfs, TieAppConfigState* state, char* error, size_t capacity) {
	const AeronConfigNode* root;
	AeronConfigError aeron_error = { 0 };
	if (!state || !state->dirty)
		return true;
	root = AeronConfigFile_Root(state->user_document);
	if (AeronConfigNode_MapCount(root) == 1 && AeronConfigNode_MapGet(root, "version")) {
		if (AeronVfs_Exists(vfs, AERON_VFS_ROOT_USER, "config.yaml") &&
			!AeronVfs_Remove(vfs, AERON_VFS_ROOT_USER, "config.yaml"))
			return TieAppConfig_ConfigError(error, capacity, "could not remove empty user config.yaml");
	} else if (!AeronConfigFile_SaveYaml(vfs, state->user_document, &aeron_error)) {
		return TieAppConfig_LogAeronError(&aeron_error, error, capacity);
	}
	state->dirty = false;
	return true;
}
