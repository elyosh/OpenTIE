#include "tie_runtime/input/controller_mapping.h"

#include "aeron/log.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

typedef struct TieControllerMappingState {
	TieControllerOptions options;
	AeronControllerSnapshot selected;
	AeronControllerKind active_kind;
	bool held[TIE_CONTROLLER_BINDING_CAP];
	bool suspended;
	bool prime_held;
	uint32_t options_revision;
	uint32_t warned_instance;
	uint32_t warned_revision;
} TieControllerMappingState;

static TieControllerMappingState g_controller;

static bool TieControllerMapping_SameSource(const AeronControllerDigitalSource* left,
											const AeronControllerDigitalSource* right) {
	return left->kind == right->kind && left->index == right->index &&
		   (left->kind != AERON_CONTROLLER_DIGITAL_HAT || left->hat_direction == right->hat_direction);
}

static bool TieControllerMapping_ProfileEqual(const TieControllerProfile* left,
											  const TieControllerProfile* right) {
	if (left->binding_count != right->binding_count)
		return false;
	for (int axis = 0; axis < TIE_INPUT_AXIS_COUNT; ++axis) {
		const TieInputAxisBinding* a = &left->mapping.axes[axis];
		const TieInputAxisBinding* b = &right->mapping.axes[axis];
		if (a->source != b->source || a->invert != b->invert || a->deadzone != b->deadzone)
			return false;
	}
	for (size_t index = 0; index < left->binding_count; ++index) {
		const TieInputActionBinding* a = &left->bindings[index];
		const TieInputActionBinding* b = &right->bindings[index];
		if (a->action != b->action || !TieControllerMapping_SameSource(&a->source, &b->source) ||
			a->source.threshold != b->source.threshold)
			return false;
	}
	return true;
}

bool TieControllerMapping_Optionsequal(const TieControllerOptions* left, const TieControllerOptions* right) {
	if (!left || !right)
		return left == right;
	return left->selector.ordinal == right->selector.ordinal &&
		   strcmp(left->selector.guid, right->selector.guid) == 0 &&
		   strcmp(left->selector.path, right->selector.path) == 0 &&
		   TieControllerMapping_ProfileEqual(&left->gamepad, &right->gamepad) &&
		   TieControllerMapping_ProfileEqual(&left->joystick, &right->joystick);
}

bool TieControllerMapping_EffectiveAxisInvert(AeronControllerKind kind, TieInputAxis axis, bool invert) {
	/* SDL gamepad Y is positive downward, while positive TIE pitch raises the nose. */
	return kind == AERON_CONTROLLER_KIND_GAMEPAD && axis == TIE_INPUT_AXIS_PITCH ? !invert : invert;
}

static bool TieControllerMapping_ValidationError(char* error, size_t capacity, const char* message) {
	if (error && capacity)
		snprintf(error, capacity, "%s", message);
	return false;
}

bool TieControllerMapping_Profilevalidate(const TieControllerProfile* profile, AeronControllerKind kind,
										  char* error, size_t capacity) {
	const int axis_limit =
		kind == AERON_CONTROLLER_KIND_GAMEPAD ? AERON_GAMEPAD_AXIS_COUNT : AERON_CONTROLLER_AXIS_MAX;
	const int button_limit =
		kind == AERON_CONTROLLER_KIND_GAMEPAD ? AERON_GAMEPAD_BUTTON_COUNT : AERON_CONTROLLER_BUTTON_MAX;
	if (!profile || (kind != AERON_CONTROLLER_KIND_GAMEPAD && kind != AERON_CONTROLLER_KIND_JOYSTICK))
		return TieControllerMapping_ValidationError(error, capacity, "invalid controller profile");
	if (profile->binding_count > TIE_CONTROLLER_BINDING_CAP)
		return TieControllerMapping_ValidationError(error, capacity, "controller binding capacity exceeded");
	for (int axis = 0; axis < TIE_INPUT_AXIS_COUNT; ++axis) {
		const TieInputAxisBinding* binding = &profile->mapping.axes[axis];
		if (binding->source < -1 || binding->source >= axis_limit || !isfinite(binding->deadzone) ||
			binding->deadzone < 0.0f || binding->deadzone > 1.0f)
			return TieControllerMapping_ValidationError(error, capacity,
														"controller axis binding is invalid");
		if (binding->source < 0)
			continue;
		for (int prior = 0; prior < axis; ++prior)
			if (profile->mapping.axes[prior].source == binding->source)
				return TieControllerMapping_ValidationError(
					error, capacity, "controller axis source is assigned more than once");
	}
	for (size_t index = 0; index < profile->binding_count; ++index) {
		const TieInputActionBinding* binding = &profile->bindings[index];
		const AeronControllerDigitalSource* source = &binding->source;
		if (binding->action <= TIE_INPUT_ACTION_NONE || binding->action >= TIE_INPUT_ACTION_COUNT)
			return TieControllerMapping_ValidationError(error, capacity,
														"controller action binding is invalid");
		if (!isfinite(source->threshold) || source->threshold <= 0.0f || source->threshold > 1.0f)
			return TieControllerMapping_ValidationError(error, capacity,
														"controller digital threshold is invalid");
		switch (source->kind) {
			case AERON_CONTROLLER_DIGITAL_BUTTON:
				if (source->index >= button_limit)
					return TieControllerMapping_ValidationError(error, capacity,
																"controller button index is out of range");
				break;
			case AERON_CONTROLLER_DIGITAL_AXIS_POSITIVE:
			case AERON_CONTROLLER_DIGITAL_AXIS_NEGATIVE:
				if (source->index >= axis_limit)
					return TieControllerMapping_ValidationError(error, capacity,
																"controller digital axis is invalid");
				break;
			case AERON_CONTROLLER_DIGITAL_HAT:
				if (kind != AERON_CONTROLLER_KIND_JOYSTICK || source->index >= AERON_CONTROLLER_HAT_MAX ||
					(source->hat_direction != AERON_CONTROLLER_HAT_UP &&
					 source->hat_direction != AERON_CONTROLLER_HAT_RIGHT &&
					 source->hat_direction != AERON_CONTROLLER_HAT_DOWN &&
					 source->hat_direction != AERON_CONTROLLER_HAT_LEFT))
					return TieControllerMapping_ValidationError(error, capacity,
																"controller hat binding is invalid");
				break;
			default:
				return TieControllerMapping_ValidationError(error, capacity,
															"controller source kind is invalid");
		}
		for (size_t prior = 0; prior < index; ++prior)
			if (TieControllerMapping_SameSource(source, &profile->bindings[prior].source))
				return TieControllerMapping_ValidationError(error, capacity,
															"controller source is bound more than once");
	}
	return true;
}

static const TieControllerProfile* TieControllerMapping_ActiveProfile(void) {
	if (g_controller.active_kind == AERON_CONTROLLER_KIND_GAMEPAD)
		return &g_controller.options.gamepad;
	if (g_controller.active_kind == AERON_CONTROLLER_KIND_JOYSTICK)
		return &g_controller.options.joystick;
	return NULL;
}

static void TieControllerMapping_ReleaseActiveBindings(void) {
	const TieControllerProfile* profile = TieControllerMapping_ActiveProfile();
	if (profile) {
		for (size_t index = 0; index < profile->binding_count; ++index) {
			if (g_controller.held[index])
				TieInputActions_DispatchController(profile->bindings[index].action, false);
		}
	}
	memset(g_controller.held, 0, sizeof g_controller.held);
}

static void TieControllerMapping_ClearSelected(void) {
	memset(&g_controller.selected, 0, sizeof g_controller.selected);
	g_controller.active_kind = AERON_CONTROLLER_KIND_NONE;
}

void TieControllerMapping_SetOptions(const TieControllerOptions* options) {
	TieControllerOptions empty = { 0 };
	if (!options) {
		for (int axis = 0; axis < TIE_INPUT_AXIS_COUNT; ++axis) {
			empty.gamepad.mapping.axes[axis].source = -1;
			empty.joystick.mapping.axes[axis].source = -1;
		}
		options = &empty;
	}
	if (TieControllerMapping_Optionsequal(&g_controller.options, options))
		return;
	TieControllerMapping_ReleaseActiveBindings();
	TieControllerMapping_ClearSelected();
	g_controller.options = *options;
	g_controller.prime_held = true;
	if (++g_controller.options_revision == 0)
		++g_controller.options_revision;
}

void TieControllerMapping_Suspend(void) {
	if (g_controller.suspended)
		return;
	TieControllerMapping_ReleaseActiveBindings();
	TieControllerMapping_ClearSelected();
	g_controller.suspended = true;
}

void TieControllerMapping_Resume(void) {
	if (!g_controller.suspended)
		return;
	g_controller.suspended = false;
	g_controller.prime_held = true;
}

static int TieControllerMapping_SourceAvailable(const AeronControllerSnapshot* controller,
												const AeronControllerDigitalSource* source) {
	if (source->kind == AERON_CONTROLLER_DIGITAL_BUTTON)
		return controller->kind == AERON_CONTROLLER_KIND_GAMEPAD
				   ? source->index < AERON_GAMEPAD_BUTTON_COUNT &&
						 (controller->gamepad_available_buttons & (1u << source->index))
				   : source->index < controller->button_count;
	if (source->kind == AERON_CONTROLLER_DIGITAL_AXIS_POSITIVE ||
		source->kind == AERON_CONTROLLER_DIGITAL_AXIS_NEGATIVE)
		return controller->kind == AERON_CONTROLLER_KIND_GAMEPAD
				   ? source->index < AERON_GAMEPAD_AXIS_COUNT &&
						 (controller->gamepad_available_axes & (1u << source->index))
				   : source->index < controller->axis_count;
	if (source->kind == AERON_CONTROLLER_DIGITAL_HAT)
		return controller->kind == AERON_CONTROLLER_KIND_JOYSTICK && source->index < controller->hat_count;
	return 0;
}

static void TieControllerMapping_LogUnavailableSources(const AeronControllerSnapshot* controller,
													   const TieControllerProfile* profile) {
	int missing_axes = 0;
	int missing_digital = 0;
	if (g_controller.warned_instance == controller->instance_id &&
		g_controller.warned_revision == g_controller.options_revision)
		return;
	g_controller.warned_instance = controller->instance_id;
	g_controller.warned_revision = g_controller.options_revision;
	for (int axis = 0; axis < TIE_INPUT_AXIS_COUNT; ++axis) {
		const int source = profile->mapping.axes[axis].source;
		if (source < 0)
			continue;
		if (controller->kind == AERON_CONTROLLER_KIND_GAMEPAD) {
			if (source >= AERON_GAMEPAD_AXIS_COUNT || !(controller->gamepad_available_axes & (1u << source)))
				++missing_axes;
		} else if (source >= controller->axis_count) {
			++missing_axes;
		}
	}
	for (size_t index = 0; index < profile->binding_count; ++index)
		if (!TieControllerMapping_SourceAvailable(controller, &profile->bindings[index].source))
			++missing_digital;
	if (controller->controls_truncated || missing_axes || missing_digital)
		Aeron_LogWarn("tie.input", "controller '%s': %d unavailable axes, %d unavailable bindings%s",
					  controller->name, missing_axes, missing_digital,
					  controller->controls_truncated ? ", controls truncated" : "");
}

void TieControllerMapping_Update(const AeronInputSnapshot* input) {
	const AeronControllerSnapshot* controller;
	const TieControllerProfile* profile;
	bool changed;
	if (g_controller.suspended)
		return;
	controller =
		input && input->has_focus ? Aeron_SelectController(input, &g_controller.options.selector) : NULL;
	changed = controller && (!g_controller.selected.connected ||
							 g_controller.selected.instance_id != controller->instance_id ||
							 g_controller.selected.kind != controller->kind);
	if (!controller) {
		if (g_controller.selected.connected)
			TieControllerMapping_ReleaseActiveBindings();
		TieControllerMapping_ClearSelected();
		return;
	}
	if (changed) {
		TieControllerMapping_ReleaseActiveBindings();
		TieControllerMapping_ClearSelected();
		g_controller.active_kind = controller->kind;
		g_controller.prime_held = true;
	}
	profile = controller->kind == AERON_CONTROLLER_KIND_GAMEPAD ? &g_controller.options.gamepad
																: &g_controller.options.joystick;
	if (changed) {
		TieInputMapping mapping = profile->mapping;
		for (int axis = 0; axis < TIE_INPUT_AXIS_COUNT; ++axis) {
			mapping.axes[axis].invert = TieControllerMapping_EffectiveAxisInvert(
				controller->kind, (TieInputAxis)axis, mapping.axes[axis].invert);
		}
		TieInput_SetMapping(&mapping);
	}
	TieControllerMapping_LogUnavailableSources(controller, profile);
	for (size_t index = 0; index < profile->binding_count; ++index) {
		const bool down = Aeron_ControllerDigitalSourceDown(controller, &profile->bindings[index].source,
															g_controller.held[index]) != 0;
		if (!g_controller.prime_held && down != g_controller.held[index])
			TieInputActions_DispatchController(profile->bindings[index].action, down);
		g_controller.held[index] = down;
	}
	g_controller.prime_held = false;
	g_controller.selected = *controller;
}

int TieControllerMapping_Present(void) { return g_controller.selected.connected ? 1 : 0; }

static int16_t TieControllerMapping_NormalizeAxis(int16_t raw) {
	int value = ((int32_t)raw * 127) / 32767;
	if (value < -127)
		value = -127;
	if (value > 127)
		value = 127;
	return (int16_t)value;
}

void TieControllerMapping_Read(int16_t* axes, int axis_count, uint16_t* buttons) {
	const AeronControllerSnapshot* controller = &g_controller.selected;
	int count;
	if (axes && axis_count > 0)
		memset(axes, 0, (size_t)axis_count * sizeof *axes);
	if (buttons)
		*buttons = 0;
	if (!controller->connected)
		return;
	if (axes && controller->kind == AERON_CONTROLLER_KIND_GAMEPAD) {
		count = axis_count < AERON_GAMEPAD_AXIS_COUNT ? axis_count : AERON_GAMEPAD_AXIS_COUNT;
		for (int index = 0; index < count; ++index) {
			if (controller->gamepad_available_axes & (1u << index)) {
				int16_t value = TieControllerMapping_NormalizeAxis(controller->gamepad_axes[index]);
				if ((index == AERON_GAMEPAD_AXIS_LEFT_TRIGGER || index == AERON_GAMEPAD_AXIS_RIGHT_TRIGGER) &&
					value < 0)
					value = 0;
				axes[index] = value;
			}
		}
	} else if (axes && controller->kind == AERON_CONTROLLER_KIND_JOYSTICK) {
		count = axis_count < controller->axis_count ? axis_count : controller->axis_count;
		if (count > AERON_CONTROLLER_AXIS_MAX)
			count = AERON_CONTROLLER_AXIS_MAX;
		for (int index = 0; index < count; ++index)
			axes[index] = TieControllerMapping_NormalizeAxis(controller->raw_axes[index]);
	}
	if (buttons)
		*buttons = TieInputActions_VirtualButtons;
}
