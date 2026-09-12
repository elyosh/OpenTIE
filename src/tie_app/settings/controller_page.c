#include "tie_app/settings/controller_page.h"

#include "tie_runtime/input/controller_mapping.h"

#include <stdio.h>
#include <string.h>

enum {
	CONTROLLER_PAGE_AXES = 0,
	CONTROLLER_PAGE_ACTIONS,
};

static const char* const k_axis_names[TIE_INPUT_AXIS_COUNT] = { "Yaw", "Pitch", "Roll", "Throttle" };

static TieControllerProfile* TieControllerPage_ActiveProfile(TieControllerSettings* settings,
															 const AeronControllerSnapshot* controller) {
	int i = TieControllerMapping_FindModel(&settings->draft, controller->guid);
	return i >= 0 ? &settings->draft.models[i].profile : &settings->unconfigured;
}
static const TieControllerProfile*
TieControllerPage_ActiveProfileConst(const TieControllerSettings* settings,
									 const AeronControllerSnapshot* controller) {
	int i = TieControllerMapping_FindModel(&settings->draft, controller->guid);
	return i >= 0 ? &settings->draft.models[i].profile : &settings->unconfigured;
}
static const AeronControllerSnapshot* TieControllerPage_SelectedController(TieControllerSettings* settings,
																		   const AeronInputSnapshot* input) {
	if (!input)
		return NULL;
	for (int i = 0; i < AERON_CONTROLLER_MAX; ++i)
		if (input->controllers[i].connected &&
			input->controllers[i].instance_id == settings->selected_instance)
			return &input->controllers[i];
	return NULL;
}
static void TieControllerSettings_ApplyDraft(TieControllerSettings* settings) {
	if (settings->selected_guid[0] &&
		TieControllerMapping_FindModel(&settings->draft, settings->selected_guid) < 0) {
		const AeronControllerSnapshot* device =
			TieControllerPage_SelectedController(settings, Aeron_InputSnapshot());
		if (!device ||
			!TieControllerMapping_AddModel(&settings->draft, device, settings->error, sizeof settings->error))
			return;
		settings->draft.models[settings->draft.count - 1].profile = settings->unconfigured;
	}
	settings->dirty = !TieControllerMapping_Optionsequal(&settings->draft, &settings->original);
	if (!TieControllerMapping_OptionsValid(&settings->draft, settings->error, sizeof settings->error))
		return;
	TieControllerMapping_SetOptions(&settings->draft);
	settings->error[0] = 0;
}

static bool TieControllerSettings_DigitalSourceEqual(const AeronControllerDigitalSource* left,
													 const AeronControllerDigitalSource* right) {
	return left->kind == right->kind && left->index == right->index &&
		   (left->kind != AERON_CONTROLLER_DIGITAL_HAT || left->hat_direction == right->hat_direction);
}

static const char* TieControllerPage_GamepadAxisDisplay(int source) {
	static const char* const names[AERON_GAMEPAD_AXIS_COUNT] = { "Left X",  "Left Y",       "Right X",
																 "Right Y", "Left Trigger", "Right Trigger" };
	return source >= 0 && source < AERON_GAMEPAD_AXIS_COUNT ? names[source] : NULL;
}

static const char* TieControllerPage_GamepadButtonDisplay(int source) {
	static const char* const names[AERON_GAMEPAD_BUTTON_COUNT] = {
		"South",          "East",          "West",        "North",         "Back",           "Guide",
		"Start",          "Left Stick",    "Right Stick", "Left Shoulder", "Right Shoulder", "D-pad Up",
		"D-pad Down",     "D-pad Left",    "D-pad Right", "Misc 1",        "Right Paddle 1", "Left Paddle 1",
		"Right Paddle 2", "Left Paddle 2", "Touchpad",    "Misc 2",        "Misc 3",         "Misc 4",
		"Misc 5",         "Misc 6"
	};
	return source >= 0 && source < AERON_GAMEPAD_BUTTON_COUNT ? names[source] : NULL;
}

static bool TieControllerSettings_SourceAvailable(const AeronControllerSnapshot* controller,
												  const AeronControllerDigitalSource* source) {
	const AeronControllerKind kind = controller ? controller->kind : AERON_CONTROLLER_KIND_NONE;
	if (!controller->connected)
		return false;
	if (source->kind == AERON_CONTROLLER_DIGITAL_BUTTON)
		return kind == AERON_CONTROLLER_KIND_GAMEPAD
				   ? source->index < AERON_GAMEPAD_BUTTON_COUNT &&
						 (controller->gamepad_available_buttons & (1u << source->index))
				   : source->index < controller->button_count;
	if (source->kind == AERON_CONTROLLER_DIGITAL_AXIS_POSITIVE ||
		source->kind == AERON_CONTROLLER_DIGITAL_AXIS_NEGATIVE)
		return kind == AERON_CONTROLLER_KIND_GAMEPAD
				   ? source->index < AERON_GAMEPAD_AXIS_COUNT &&
						 (controller->gamepad_available_axes & (1u << source->index))
				   : source->index < controller->axis_count;
	return source->kind == AERON_CONTROLLER_DIGITAL_HAT && kind == AERON_CONTROLLER_KIND_JOYSTICK &&
		   source->index < controller->hat_count;
}

static void TieControllerSettings_FormatAxisSource(char* buffer, size_t capacity, int source,
												   const AeronControllerSnapshot* controller) {
	const AeronControllerKind kind = controller ? controller->kind : AERON_CONTROLLER_KIND_NONE;
	if (source < 0) {
		snprintf(buffer, capacity, "Not Bound");
		return;
	}
	const char* name =
		kind == AERON_CONTROLLER_KIND_GAMEPAD ? TieControllerPage_GamepadAxisDisplay(source) : NULL;
	const bool available =
		kind == AERON_CONTROLLER_KIND_GAMEPAD
			? source < AERON_GAMEPAD_AXIS_COUNT && (controller->gamepad_available_axes & (1u << source))
			: source < controller->axis_count;
	if (name)
		snprintf(buffer, capacity, "%s%s", name, available ? "" : " (Unavailable)");
	else
		snprintf(buffer, capacity, "Axis %d%s", source, available ? "" : " (Unavailable)");
}

static const char* TieControllerPage_HatDirectionName(uint8_t direction) {
	switch (direction) {
		case AERON_CONTROLLER_HAT_UP:
			return "Up";
		case AERON_CONTROLLER_HAT_RIGHT:
			return "Right";
		case AERON_CONTROLLER_HAT_DOWN:
			return "Down";
		default:
			return "Left";
	}
}

static void TieControllerSettings_FormatDigitalSource(char* buffer, size_t capacity,
													  const AeronControllerDigitalSource* source,
													  const AeronControllerSnapshot* controller) {
	const AeronControllerKind kind = controller ? controller->kind : AERON_CONTROLLER_KIND_NONE;
	const char* name = NULL;
	if (source->kind == AERON_CONTROLLER_DIGITAL_BUTTON) {
		name = kind == AERON_CONTROLLER_KIND_GAMEPAD ? TieControllerPage_GamepadButtonDisplay(source->index)
													 : NULL;
		if (name)
			snprintf(buffer, capacity, "%s", name);
		else
			snprintf(buffer, capacity, "Button %u", (unsigned)source->index);
	} else if (source->kind == AERON_CONTROLLER_DIGITAL_AXIS_POSITIVE ||
			   source->kind == AERON_CONTROLLER_DIGITAL_AXIS_NEGATIVE) {
		name = kind == AERON_CONTROLLER_KIND_GAMEPAD ? TieControllerPage_GamepadAxisDisplay(source->index)
													 : NULL;
		if (name)
			snprintf(buffer, capacity, "%s %c", name,
					 source->kind == AERON_CONTROLLER_DIGITAL_AXIS_POSITIVE ? '+' : '-');
		else
			snprintf(buffer, capacity, "Axis %u %c", (unsigned)source->index,
					 source->kind == AERON_CONTROLLER_DIGITAL_AXIS_POSITIVE ? '+' : '-');
	} else {
		snprintf(buffer, capacity, "Hat %u %s", (unsigned)source->index,
				 TieControllerPage_HatDirectionName(source->hat_direction));
	}
	if (!TieControllerSettings_SourceAvailable(controller, source)) {
		const size_t used = strlen(buffer);
		if (used < capacity)
			snprintf(buffer + used, capacity - used, " (Unavailable)");
	}
}

static void TieControllerSettings_AppendText(char* buffer, size_t capacity, const char* text) {
	const size_t used = strlen(buffer);
	if (used < capacity)
		snprintf(buffer + used, capacity - used, "%s%s", used ? ", " : "", text);
}

static void TieControllerSettings_DescribeActionBindings(char* buffer, size_t capacity,
														 const TieControllerProfile* profile,
														 TieInputAction action,
														 const AeronControllerSnapshot* controller) {
	buffer[0] = '\0';
	for (size_t index = 0; index < profile->binding_count; ++index) {
		if (profile->bindings[index].action != action)
			continue;
		char source[96];
		TieControllerSettings_FormatDigitalSource(source, sizeof source, &profile->bindings[index].source,
												  controller);
		TieControllerSettings_AppendText(buffer, capacity, source);
	}
	if (!buffer[0])
		snprintf(buffer, capacity, "Not Bound");
}

void TieControllerSettings_Open(TieControllerSettings* settings, const TieAppConfigState* config) {
	if (!settings || !config)
		return;
	memset(settings, 0, sizeof *settings);
	settings->original = config->requested.controller;
	settings->draft = settings->original;
	settings->action_selected = SIZE_MAX;
	settings->binding_selected = SIZE_MAX;
	settings->selected_action = TIE_INPUT_ACTION_NONE;
	settings->pending_axis = TIE_INPUT_AXIS_YAW;
	TieControllerMapping_ClearProfile(&settings->unconfigured, AERON_CONTROLLER_KIND_JOYSTICK);
}

void TieControllerSettings_CancelCapture(TieControllerSettings* settings, AeronUiContext* ui) {
	if (ui)
		AeronUi_CancelControllerCapture(ui);
	if (settings) {
		settings->axis_conflict_open = 0;
		settings->binding_conflict_open = 0;
		settings->binding_modal_open = 0;
	}
}

static void TieControllerSettings_ResetDeviceEditState(TieControllerSettings* settings, AeronUiContext* ui) {
	AeronUi_CancelControllerCapture(ui);
	settings->action_selected = SIZE_MAX;
	settings->binding_selected = SIZE_MAX;
	settings->selected_action = TIE_INPUT_ACTION_NONE;
	settings->binding_modal_open = 0;
	settings->axis_conflict_open = 0;
	settings->binding_conflict_open = 0;
}

bool TieControllerSettings_Commit(TieControllerSettings* settings, TieAppConfigState* config, char* error,
								  size_t error_capacity) {
	if (!settings || !config) {
		if (error && error_capacity)
			snprintf(error, error_capacity, "controller settings are unavailable");
		return false;
	}
	if (!settings->dirty)
		return true;
	const bool result = TieAppConfig_SetController(config, &settings->draft, error, error_capacity);
	if (!result)
		return false;
	settings->draft = config->requested.controller;
	settings->original = settings->draft;
	settings->dirty = false;
	return true;
}

static void TieControllerSettings_DeviceSelector(TieControllerSettings* settings, AeronUiContext* ui,
												 const AeronInputSnapshot* input) {
	enum { CAP = AERON_CONTROLLER_MAX };
	char labels[CAP][192];
	const char* options[CAP];
	const char* guids[CAP];
	uint32_t ids[CAP];
	int count = 0, selected = -1;
	for (int i = 0; i < AERON_CONTROLLER_MAX; ++i) {
		const AeronControllerSnapshot* d = &input->controllers[i];
		if (!d->connected)
			continue;
		bool duplicate_name = false;
		for (int other = 0; other < AERON_CONTROLLER_MAX; ++other) {
			const AeronControllerSnapshot* candidate = &input->controllers[other];
			if (other != i && candidate->connected && !strcmp(candidate->name, d->name))
				duplicate_name = true;
		}
		if (duplicate_name)
			snprintf(labels[count], sizeof labels[count], "%s #%d", d->name, i + 1);
		else
			snprintf(labels[count], sizeof labels[count], "%s", d->name);
		options[count] = labels[count];
		guids[count] = d->guid;
		ids[count] = d->instance_id;
		if (settings->selected_instance == d->instance_id)
			selected = count;
		++count;
	}
	if (!count) {
		TieControllerSettings_ResetDeviceEditState(settings, ui);
		settings->selected_instance = 0;
		settings->selected_guid[0] = 0;
		AeronUi_Help(ui, "Connect a controller to configure its controls.");
		return;
	}
	bool changed = selected < 0;
	if (selected < 0)
		selected = 0;
	AeronUi_Header(ui, "Device");
	changed |= AeronUi_Selector(ui, "##controller_device", &selected, options, count) != 0;
	if (changed || settings->selected_instance != ids[selected]) {
		TieControllerSettings_ResetDeviceEditState(settings, ui);
		snprintf(settings->selected_guid, sizeof settings->selected_guid, "%s", guids[selected]);
		settings->selected_instance = ids[selected];
		const AeronControllerSnapshot* selected_device =
			TieControllerPage_SelectedController(settings, input);
		TieControllerMapping_ClearProfile(&settings->unconfigured, selected_device
																	   ? selected_device->kind
																	   : AERON_CONTROLLER_KIND_JOYSTICK);
	}
}

void TieControllerSettings_Discover(TieControllerSettings* settings, AeronUiContext* ui,
									const AeronInputSnapshot* input, const TieControllerProfile* defaults) {
	size_t count = settings->draft.count;
	bool ok = TieControllerMapping_InitializeGamepads(&settings->draft, defaults, input, settings->error,
													  sizeof settings->error);
	if (settings->draft.count != count) {
		TieControllerSettings_ResetDeviceEditState(settings, ui);
		settings->dirty = true;
		TieControllerMapping_SetOptions(&settings->draft);
	}
	if (ok && settings->capacity_warned)
		settings->error[0] = 0;
	settings->capacity_warned = !ok;
}

static int TieControllerSettings_ProfileMissingCount(const TieControllerProfile* profile,
													 const AeronControllerSnapshot* controller) {
	const AeronControllerKind kind = controller ? controller->kind : AERON_CONTROLLER_KIND_NONE;
	int missing = 0;
	for (int axis = 0; axis < TIE_INPUT_AXIS_COUNT; ++axis) {
		const int source = profile->mapping.axes[axis].source;
		if (source < 0)
			continue;
		if (kind == AERON_CONTROLLER_KIND_GAMEPAD) {
			if (source >= AERON_GAMEPAD_AXIS_COUNT || !(controller->gamepad_available_axes & (1u << source)))
				++missing;
		} else if (source >= controller->axis_count) {
			++missing;
		}
	}
	for (size_t index = 0; index < profile->binding_count; ++index)
		if (!TieControllerSettings_SourceAvailable(controller, &profile->bindings[index].source))
			++missing;
	return missing;
}

static void TieControllerSettings_ControllerWarning(const TieControllerSettings* settings, AeronUiContext* ui,
													const AeronControllerSnapshot* controller) {
	if (!controller) {
		AeronUi_Help(ui, "Select a connected device to assign controls.");
		return;
	}

	const int missing = TieControllerSettings_ProfileMissingCount(
		TieControllerPage_ActiveProfileConst(settings, controller), controller);
	if (missing && controller->controls_truncated) {
		char text[256];
		snprintf(text, sizeof text,
				 "%d configured controls are unavailable; this controller also exposes more controls than "
				 "the game supports.",
				 missing);
		AeronUi_Error(ui, text);
	} else if (missing) {
		char text[128];
		snprintf(text, sizeof text, "%d configured controls are unavailable.", missing);
		AeronUi_Error(ui, text);
	} else if (controller->controls_truncated)
		AeronUi_Error(ui, "This controller exposes more controls than the game supports.");
}

static float TieControllerSettings_ControllerAxisValue(const AeronControllerSnapshot* controller,
													   int source) {
	const AeronControllerKind kind = controller ? controller->kind : AERON_CONTROLLER_KIND_NONE;
	if (!controller || source < 0)
		return 0.0f;
	int value;
	if (kind == AERON_CONTROLLER_KIND_GAMEPAD) {
		if (source >= AERON_GAMEPAD_AXIS_COUNT || !(controller->gamepad_available_axes & (1u << source)))
			return 0.0f;
		value = controller->gamepad_axes[source];
	} else {
		if (source >= controller->axis_count || source >= AERON_CONTROLLER_AXIS_MAX)
			return 0.0f;
		value = controller->raw_axes[source];
	}
	return value < 0 ? (float)value / 32768.0f : (float)value / 32767.0f;
}

static void TieControllerSettings_InstallAxis(TieControllerSettings* settings,
											  const AeronControllerSnapshot* controller, TieInputAxis axis,
											  int source) {
	TieControllerOptions candidate = settings->draft;
	int model = TieControllerMapping_FindModel(&candidate, controller->guid);
	if (model < 0) {
		if (!TieControllerMapping_AddModel(&candidate, controller, settings->error, sizeof settings->error))
			return;
		model = (int)candidate.count - 1;
		candidate.models[model].profile = settings->unconfigured;
	}
	TieControllerProfile* p = &candidate.models[model].profile;
	for (size_t i = 0; i < candidate.count; ++i)
		candidate.models[i].profile.mapping.axes[axis].source = -1;
	for (int i = 0; i < TIE_INPUT_AXIS_COUNT; ++i)
		if (p->mapping.axes[i].source == source)
			p->mapping.axes[i].source = -1;
	p->mapping.axes[axis].source = (int8_t)source;
	if (!TieControllerMapping_OptionsValid(&candidate, settings->error, sizeof settings->error))
		return;
	settings->draft = candidate;
	TieControllerSettings_ApplyDraft(settings);
}
static void TieControllerSettings_AssignCapturedAxis(TieControllerSettings* settings,
													 const AeronControllerSnapshot* controller,
													 TieInputAxis axis, int source) {
	settings->conflict_text[0] = 0;
	const TieControllerProfile* p = TieControllerPage_ActiveProfileConst(settings, controller);
	for (size_t i = 0; i < settings->draft.count; ++i) {
		const TieControllerModel* m = &settings->draft.models[i];
		if (strcmp(m->guid, controller->guid) && m->profile.mapping.axes[axis].source >= 0) {
			char text[192];
			snprintf(text, sizeof text, "%s on %s", k_axis_names[axis], m->name);
			TieControllerSettings_AppendText(settings->conflict_text, sizeof settings->conflict_text, text);
		}
	}
	for (int i = 0; i < TIE_INPUT_AXIS_COUNT; ++i)
		if (i != (int)axis && p->mapping.axes[i].source == source)
			TieControllerSettings_AppendText(settings->conflict_text, sizeof settings->conflict_text,
											 k_axis_names[i]);
	if (settings->conflict_text[0]) {
		settings->pending_axis = axis;
		settings->pending_axis_source = source;
		settings->axis_conflict_open = 1;
	} else
		TieControllerSettings_InstallAxis(settings, controller, axis, source);
}

static void TieControllerSettings_AxisEditor(TieControllerSettings* settings, AeronUiContext* ui,
											 const AeronControllerSnapshot* controller, TieInputAxis axis) {
	const AeronControllerKind kind = controller ? controller->kind : AERON_CONTROLLER_KIND_NONE;
	TieControllerProfile* profile = TieControllerPage_ActiveProfile(settings, controller);

	TieInputAxisBinding* binding = &profile->mapping.axes[axis];
	char source[128];
	TieControllerSettings_FormatAxisSource(source, sizeof source, binding->source, controller);
	AeronUi_PushId(ui, axis);
	const AeronUiControllerCaptureDesc desc = { controller->connected ? controller->instance_id : 0,
												AERON_UI_CONTROLLER_CAPTURE_ANALOG_AXIS };
	AeronUiControllerInput captured;
	const AeronUiControllerCaptureResult capture =
		AeronUi_ControllerCapture(ui, "Source", source, &desc, &captured);
	if (capture == AERON_UI_CONTROLLER_CAPTURE_CAPTURED && captured.controller_kind == kind &&
		captured.instance_id == controller->instance_id)
		TieControllerSettings_AssignCapturedAxis(settings, controller, axis, captured.value.axis);
	profile = TieControllerPage_ActiveProfile(settings, controller);
	binding = &profile->mapping.axes[axis];
	float live = TieControllerSettings_ControllerAxisValue(controller, binding->source);
	if (TieControllerMapping_EffectiveAxisInvert(kind, axis, binding->invert))
		live = -live;
	if (axis == TIE_INPUT_AXIS_THROTTLE) {
		if (Aeron_ControllerAxisAvailable(controller, binding->source)) {
			const int16_t raw = Aeron_ControllerAxisValue(controller, binding->source);
			const uint16_t position =
				TieControllerMapping_ThrottlePosition(raw, kind, binding->source, binding->invert);
			AeronUi_PercentageMeter(ui, "Position", (float)position / UINT16_MAX);
		} else
			AeronUi_Help(ui, "Assign an available throttle axis to see its position.");
		AeronUi_Help(ui, "Move the throttle to take control.");
	} else {
		AeronUi_ControllerAxisMeter(ui, "Input", live,
									TieInput_AxisDeadzonePercent(binding->deadzone) / 100.0f);
	}
	int invert = binding->invert;
	if (AeronUi_Toggle(ui, "Invert", &invert)) {
		binding->invert = invert != 0;
		TieControllerSettings_ApplyDraft(settings);
	}
	if (axis != TIE_INPUT_AXIS_THROTTLE) {
		float deadzone_percent = TieInput_AxisDeadzonePercent(binding->deadzone);
		const float minimum = TieInput_AxisDeadzonePercent(0.0f);
		if (AeronUi_SliderFloat(ui, "Deadzone", &deadzone_percent, minimum, 100.0f, 1.0f, "%.1f%%")) {
			binding->deadzone = TieInput_AxisDeadzoneFromPercent(deadzone_percent);
			TieControllerSettings_ApplyDraft(settings);
		}
	}
	if (AeronUi_ButtonEnabled(ui, "Clear Binding", binding->source >= 0)) {
		binding->source = -1;
		TieControllerSettings_ApplyDraft(settings);
	}
	AeronUi_PopId(ui);
}

static void TieControllerSettings_AxisPage(TieControllerSettings* settings, AeronUiContext* ui,
										   const AeronControllerSnapshot* controller) {
	if (AeronUi_SegmentedSelector(ui, "Flight Axis", &settings->axis, k_axis_names, TIE_INPUT_AXIS_COUNT))
		AeronUi_CancelControllerCapture(ui);
	TieControllerSettings_AxisEditor(settings, ui, controller, (TieInputAxis)settings->axis);
}

static void TieControllerSettings_AxisConflictModal(TieControllerSettings* settings, AeronUiContext* ui,
													const AeronControllerSnapshot* controller) {
	if (!AeronUi_BeginModal(ui, "AXIS ALREADY ASSIGNED", &settings->axis_conflict_open, NULL))
		return;
	char text[640];
	snprintf(text, sizeof text, "Replace these assignments with %s: %s?",
			 k_axis_names[settings->pending_axis], settings->conflict_text);
	AeronUi_Error(ui, text);
	AeronUi_BeginColumns(ui, 2, NULL);
	if (AeronUi_Button(ui, "Replace")) {
		TieControllerSettings_InstallAxis(settings, controller, settings->pending_axis,
										  settings->pending_axis_source);
		settings->axis_conflict_open = 0;
	}
	AeronUi_NextColumn(ui);
	if (AeronUi_Button(ui, "Cancel"))
		settings->axis_conflict_open = 0;
	AeronUi_EndColumns(ui);
	AeronUi_EndModal(ui);
}

static size_t TieControllerSettings_FindSourceBinding(const TieControllerProfile* profile,
													  const AeronControllerDigitalSource* source) {
	for (size_t index = 0; index < profile->binding_count; ++index)
		if (TieControllerSettings_DigitalSourceEqual(&profile->bindings[index].source, source))
			return index;
	return SIZE_MAX;
}

static void TieControllerSettings_RemoveBinding(TieControllerProfile* profile, size_t index) {
	if (index >= profile->binding_count)
		return;
	if (index + 1 < profile->binding_count)
		memmove(&profile->bindings[index], &profile->bindings[index + 1],
				(profile->binding_count - index - 1) * sizeof profile->bindings[0]);
	--profile->binding_count;
}

static size_t TieControllerSettings_ActionListPosition(TieInputActionCategory category,
													   TieInputAction action) {
	size_t position = 0;
	for (int candidate = TIE_INPUT_ACTION_NONE + 1; candidate < TIE_INPUT_ACTION_COUNT; ++candidate) {
		if (TieInputActions_Category((TieInputAction)candidate) != category)
			continue;
		if (candidate == (int)action)
			return position;
		++position;
	}
	return SIZE_MAX;
}

static void TieControllerSettings_SelectAction(TieControllerSettings* settings, TieInputAction action,
											   bool open_modal) {
	settings->selected_action = action;
	settings->category = TieInputActions_Category(action);
	settings->action_selected =
		TieControllerSettings_ActionListPosition((TieInputActionCategory)settings->category, action);
	settings->binding_selected = SIZE_MAX;
	if (open_modal)
		settings->binding_modal_open = 1;
}

static void TieControllerSettings_AddCapturedBinding(TieControllerSettings* settings,
													 const AeronControllerSnapshot* controller,
													 const AeronControllerDigitalSource* source) {
	TieControllerProfile* profile = TieControllerPage_ActiveProfile(settings, controller);
	const size_t existing = TieControllerSettings_FindSourceBinding(profile, source);
	if (existing != SIZE_MAX) {
		if (profile->bindings[existing].action == settings->selected_action) {
			size_t position = 0;
			for (size_t index = 0; index < existing; ++index)
				if (profile->bindings[index].action == settings->selected_action)
					++position;
			settings->binding_selected = position;
			return;
		}
		settings->pending_digital = *source;
		settings->conflicting_action = profile->bindings[existing].action;
		settings->binding_conflict_open = 1;
		return;
	}
	if (profile->binding_count >= TIE_CONTROLLER_BINDING_CAP) {
		snprintf(settings->error, sizeof settings->error,
				 "This controller has reached its binding capacity.");
		return;
	}
	profile->bindings[profile->binding_count++] =
		(TieInputActionBinding) { .source = *source, .action = settings->selected_action };
	settings->binding_selected = SIZE_MAX;
	TieControllerSettings_ApplyDraft(settings);
}

static void TieControllerSettings_FindBindingCapture(TieControllerSettings* settings, AeronUiContext* ui,
													 const AeronControllerSnapshot* controller) {
	const AeronControllerKind kind = controller ? controller->kind : AERON_CONTROLLER_KIND_NONE;

	const AeronUiControllerCaptureDesc desc = { controller->connected ? controller->instance_id : 0,
												AERON_UI_CONTROLLER_CAPTURE_DIGITAL };
	AeronUiControllerInput captured;
	const AeronUiControllerCaptureResult result =
		AeronUi_ControllerCapture(ui, "Find Binding...", "Press to identify", &desc, &captured);
	if (result != AERON_UI_CONTROLLER_CAPTURE_CAPTURED || captured.controller_kind != kind ||
		captured.instance_id != controller->instance_id)
		return;
	const TieControllerProfile* profile = TieControllerPage_ActiveProfileConst(settings, controller);
	const size_t binding = TieControllerSettings_FindSourceBinding(profile, &captured.value.digital);
	if (binding == SIZE_MAX) {
		snprintf(settings->error, sizeof settings->error, "This control is not bound.");
		return;
	}
	TieControllerSettings_SelectAction(settings, profile->bindings[binding].action, false);
	settings->error[0] = '\0';
}

static void TieControllerSettings_ActionsPage(TieControllerSettings* settings, AeronUiContext* ui,
											  const AeronControllerSnapshot* controller,
											  float trailing_height_ref) {
	static const char* const categories[TIE_INPUT_ACTION_CATEGORY_COUNT] = { "Weapons", "Targets", "Throttle",
																			 "View",    "Info",    "System",
																			 "Comms" };
	if (AeronUi_SegmentedSelector(ui, "Action Category", &settings->category, categories,
								  TIE_INPUT_ACTION_CATEGORY_COUNT)) {
		settings->action_selected = SIZE_MAX;
		settings->selected_action = TIE_INPUT_ACTION_NONE;
	}
	TieControllerSettings_FindBindingCapture(settings, ui, controller);
	AeronUi_Spacer(ui, 8.0f);
	AeronUiListItem items[TIE_INPUT_ACTION_COUNT - 1];
	char details[TIE_INPUT_ACTION_COUNT - 1][256];
	size_t count = 0;
	const TieControllerProfile* profile = TieControllerPage_ActiveProfileConst(settings, controller);
	for (int action = TIE_INPUT_ACTION_NONE + 1; action < TIE_INPUT_ACTION_COUNT; ++action) {
		if ((int)TieInputActions_Category((TieInputAction)action) != settings->category)
			continue;
		TieControllerSettings_DescribeActionBindings(details[count], sizeof details[count], profile,
													 (TieInputAction)action, controller);
		items[count] = (AeronUiListItem) { .id = (uint64_t)action,
										   .label = TieInputActions_DisplayName((TieInputAction)action),
										   .detail = details[count] };
		++count;
	}
	float list_height = AeronUi_AvailableHeight(ui) - trailing_height_ref;
	if (list_height < 180.0f)
		list_height = 180.0f;
	const uint32_t result =
		AeronUi_ListBox(ui, "Actions", items, count, &settings->action_selected, list_height);
	if ((result & AERON_UI_LIST_ACTIVATED) && settings->action_selected < count)
		TieControllerSettings_SelectAction(settings, (TieInputAction)items[settings->action_selected].id,
										   true);
}

static size_t TieControllerSettings_BuildActionBindingItems(const TieControllerSettings* settings,
															const AeronControllerSnapshot* controller,
															AeronUiListItem* items, char labels[][128],
															size_t* profile_indices) {
	const TieControllerProfile* profile = TieControllerPage_ActiveProfileConst(settings, controller);
	size_t count = 0;
	for (size_t index = 0; index < profile->binding_count; ++index) {
		if (profile->bindings[index].action != settings->selected_action)
			continue;
		TieControllerSettings_FormatDigitalSource(labels[count], 128, &profile->bindings[index].source,
												  controller);
		items[count] = (AeronUiListItem) { .id = index, .label = labels[count], .detail = NULL };
		profile_indices[count] = index;
		++count;
	}
	return count;
}

static void TieControllerSettings_BindingDetailModal(TieControllerSettings* settings, AeronUiContext* ui,
													 const AeronControllerSnapshot* controller) {
	const AeronControllerKind kind = controller ? controller->kind : AERON_CONTROLLER_KIND_NONE;
	if (!settings->binding_modal_open || settings->selected_action == TIE_INPUT_ACTION_NONE)
		return;
	char title[128];
	snprintf(title, sizeof title, "%s", TieInputActions_DisplayName(settings->selected_action));
	if (!AeronUi_BeginModal(ui, title, &settings->binding_modal_open,
							&(AeronUiWindowDesc) { .width_ref = 720.0f, .centered = 1 }))
		return;
	TieControllerProfile* profile = TieControllerPage_ActiveProfile(settings, controller);
	AeronUiListItem items[TIE_CONTROLLER_BINDING_CAP];
	char labels[TIE_CONTROLLER_BINDING_CAP][128];
	size_t profile_indices[TIE_CONTROLLER_BINDING_CAP];
	const size_t count =
		TieControllerSettings_BuildActionBindingItems(settings, controller, items, labels, profile_indices);
	AeronUi_Header(ui, "Current Bindings");
	if (count) {
		AeronUi_ListBox(ui, "Bindings", items, count, &settings->binding_selected, 180.0f);
	} else {
		settings->binding_selected = SIZE_MAX;
		AeronUi_Help(ui, "This action has no controller binding.");
	}
	if (settings->binding_selected < count) {
		const size_t profile_index = profile_indices[settings->binding_selected];
		AeronControllerDigitalSource* source = &profile->bindings[profile_index].source;
		if (source->kind == AERON_CONTROLLER_DIGITAL_AXIS_POSITIVE ||
			source->kind == AERON_CONTROLLER_DIGITAL_AXIS_NEGATIVE) {
			int threshold = (int)(source->threshold * 100.0f + 0.5f);
			if (AeronUi_SliderInt(ui, "Axis Threshold", &threshold, 5, 100, 5, "%d%%")) {
				source->threshold = (float)threshold / 100.0f;
				TieControllerSettings_ApplyDraft(settings);
			}
		}
		if (AeronUi_Button(ui, "Remove Binding")) {
			TieControllerSettings_RemoveBinding(profile, profile_index);
			settings->binding_selected = SIZE_MAX;
			TieControllerSettings_ApplyDraft(settings);
		}
	}
	AeronUi_Separator(ui);

	const bool has_capacity = profile->binding_count < TIE_CONTROLLER_BINDING_CAP;
	const AeronUiControllerCaptureDesc desc = { has_capacity && controller->connected
													? controller->instance_id
													: 0,
												AERON_UI_CONTROLLER_CAPTURE_DIGITAL };
	AeronUiControllerInput captured;
	const AeronUiControllerCaptureResult capture =
		AeronUi_ControllerCapture(ui, "Add Binding...", "Press to add", &desc, &captured);
	if (capture == AERON_UI_CONTROLLER_CAPTURE_CAPTURED && captured.controller_kind == kind &&
		captured.instance_id == controller->instance_id)
		TieControllerSettings_AddCapturedBinding(settings, controller, &captured.value.digital);
	if (!has_capacity)
		AeronUi_Error(ui, "This controller has reached its binding capacity.");
	if (AeronUi_Button(ui, "Done"))
		settings->binding_modal_open = 0;
	AeronUi_EndModal(ui);
}

static void TieControllerSettings_BindingConflictModal(TieControllerSettings* settings, AeronUiContext* ui,
													   const AeronControllerSnapshot* controller) {
	if (!AeronUi_BeginModal(ui, "CONTROL ALREADY BOUND", &settings->binding_conflict_open, NULL))
		return;
	char text[256];
	snprintf(text, sizeof text, "This control is assigned to %s. Replace it with %s?",
			 TieInputActions_DisplayName(settings->conflicting_action),
			 TieInputActions_DisplayName(settings->selected_action));
	AeronUi_Error(ui, text);
	AeronUi_BeginColumns(ui, 2, NULL);
	if (AeronUi_Button(ui, "Replace")) {
		TieControllerProfile* profile = TieControllerPage_ActiveProfile(settings, controller);
		const size_t existing = TieControllerSettings_FindSourceBinding(profile, &settings->pending_digital);
		if (existing != SIZE_MAX)
			TieControllerSettings_RemoveBinding(profile, existing);
		if (profile->binding_count < TIE_CONTROLLER_BINDING_CAP)
			profile->bindings[profile->binding_count++] =
				(TieInputActionBinding) { .source = settings->pending_digital,
										  .action = settings->selected_action };
		settings->binding_selected = SIZE_MAX;
		settings->binding_conflict_open = 0;
		TieControllerSettings_ApplyDraft(settings);
	}
	AeronUi_NextColumn(ui);
	if (AeronUi_Button(ui, "Cancel"))
		settings->binding_conflict_open = 0;
	AeronUi_EndColumns(ui);
	AeronUi_EndModal(ui);
}

static void TieControllerSettings_RestoreModal(TieControllerSettings* settings, AeronUiContext* ui,
											   const TieAppConfigState* config) {
	(void)config;
	if (!AeronUi_BeginModal(ui, "CLEAR ALL CONTROLLER BINDINGS", &settings->restore_modal_open, NULL))
		return;
	AeronUi_Help(ui, "Clear all saved controller bindings, including disconnected models?");
	AeronUi_BeginColumns(ui, 2, NULL);
	if (AeronUi_Button(ui, "Clear All")) {
		TieControllerOptions candidate = settings->draft;
		const AeronInputSnapshot* input = Aeron_InputSnapshot();
		bool ok = true;
		for (int i = 0; input && i < AERON_CONTROLLER_MAX; ++i) {
			const AeronControllerSnapshot* d = &input->controllers[i];
			if (d->connected && d->kind == AERON_CONTROLLER_KIND_GAMEPAD &&
				!TieControllerMapping_AddModel(&candidate, d, settings->error, sizeof settings->error))
				ok = false;
		}
		if (ok) {
			for (int i = 0; input && i < AERON_CONTROLLER_MAX; ++i) {
				const AeronControllerSnapshot* d = &input->controllers[i];
				int model = d->connected ? TieControllerMapping_FindModel(&candidate, d->guid) : -1;
				if (model >= 0)
					candidate.models[model].kind = d->kind;
			}
			for (size_t i = 0; i < candidate.count; ++i)
				TieControllerMapping_ClearProfile(&candidate.models[i].profile, candidate.models[i].kind);
			settings->draft = candidate;
			const AeronControllerSnapshot* selected_device =
				TieControllerPage_SelectedController(settings, input);
			TieControllerMapping_ClearProfile(&settings->unconfigured, selected_device
																		   ? selected_device->kind
																		   : AERON_CONTROLLER_KIND_JOYSTICK);
			settings->dirty = !TieControllerMapping_Optionsequal(&settings->draft, &settings->original);
			TieControllerMapping_SetOptions(&settings->draft);
			settings->error[0] = 0;
		}
		settings->restore_modal_open = 0;
		settings->binding_modal_open = 0;
	}
	AeronUi_NextColumn(ui);
	if (AeronUi_Button(ui, "Cancel"))
		settings->restore_modal_open = 0;
	AeronUi_EndColumns(ui);
	AeronUi_EndModal(ui);
}

void TieControllerSettings_Draw(TieControllerSettings* settings, AeronUiContext* ui,
								const AeronInputSnapshot* input) {
	static const char* const pages[] = { "Axes", "Bindings" };
	if (!settings || !ui || !input)
		return;
	TieControllerSettings_DeviceSelector(settings, ui, input);
	const AeronControllerSnapshot* controller = TieControllerPage_SelectedController(settings, input);
	const uint32_t active_instance = controller ? controller->instance_id : 0;
	if (active_instance != settings->active_instance) {
		TieControllerSettings_ResetDeviceEditState(settings, ui);
		settings->active_instance = active_instance;
	}
	if (controller) {
		int matches = 0;
		for (int i = 0; i < AERON_CONTROLLER_MAX; ++i)
			if (input->controllers[i].connected && !strcmp(input->controllers[i].guid, controller->guid))
				++matches;
		if (matches > 1) {
			AeronUi_Help(ui, "Bindings shared by all controllers of this model.");
			uint32_t preferred = TieControllerMapping_AnalogInstance(controller->guid);
			int model = TieControllerMapping_FindModel(&settings->draft, controller->guid);
			const AeronControllerSnapshot* analog =
				model >= 0 ? TieControllerMapping_Resolve(&settings->draft.models[model], input, preferred)
						   : NULL;
			for (int i = 0; analog && i < AERON_CONTROLLER_MAX; ++i)
				if (analog == &input->controllers[i]) {
					char text[160];
					snprintf(text, sizeof text, "Analog input: %s #%d", analog->name, i + 1);
					AeronUi_Help(ui, text);
				}
		}
		if (AeronUi_Button(ui, "Clear Bindings")) {
			int model = TieControllerMapping_FindModel(&settings->draft, controller->guid);
			if (model >= 0)
				settings->draft.models[model].kind = controller->kind;
			TieControllerMapping_ClearProfile(TieControllerPage_ActiveProfile(settings, controller),
											  controller->kind);
			TieControllerSettings_ApplyDraft(settings);
			TieControllerSettings_ResetDeviceEditState(settings, ui);
		}
	}
	int model = controller ? TieControllerMapping_FindModel(&settings->draft, controller->guid) : -1;
	bool compatible = !controller || model < 0 || settings->draft.models[model].kind == controller->kind;
	if (!compatible) {
		TieControllerSettings_ResetDeviceEditState(settings, ui);
		AeronUi_Error(ui,
					  "Saved bindings use a different device type. Clear Bindings to configure this device.");
	} else
		TieControllerSettings_ControllerWarning(settings, ui, controller);
	if (controller && compatible &&
		AeronUi_SegmentedSelector(ui, "Controller Page", &settings->page, pages, 2)) {
		AeronUi_CancelControllerCapture(ui);
		settings->binding_modal_open = 0;
	}
	float trailing_height = 143.0f;
	if (settings->error[0])
		trailing_height += AeronUi_MeasureHelpHeight(ui, settings->error, 0.0f) + 60.0f;
	if (controller && compatible && settings->page == CONTROLLER_PAGE_AXES) {
		float scroll_height = AeronUi_AvailableHeight(ui) - trailing_height;
		if (scroll_height < 180.0f)
			scroll_height = 180.0f;
		if (AeronUi_BeginScroll(ui, "Flight Axes", scroll_height)) {
			TieControllerSettings_AxisPage(settings, ui, controller);
			AeronUi_EndScroll(ui);
		}
	} else if (controller && compatible) {
		TieControllerSettings_ActionsPage(settings, ui, controller, trailing_height);
	}
	if (settings->error[0]) {
		AeronUi_Error(ui, settings->error);
		if (AeronUi_Button(ui, "Dismiss Error"))
			settings->error[0] = '\0';
	}
	if (AeronUi_Button(ui, "Clear All Controller Bindings"))
		settings->restore_modal_open = 1;
}

void TieControllerSettings_DrawModals(TieControllerSettings* settings, AeronUiContext* ui,
									  const AeronInputSnapshot* input, const TieAppConfigState* config) {
	if (!settings || !ui || !input || !config)
		return;
	const AeronControllerSnapshot* controller = TieControllerPage_SelectedController(settings, input);
	if (!controller) {
		AeronUi_CancelControllerCapture(ui);
		settings->axis_conflict_open = 0;
		settings->binding_conflict_open = 0;
		settings->binding_modal_open = 0;
	}
	if (settings->axis_conflict_open) {
		TieControllerSettings_AxisConflictModal(settings, ui, controller);
		return;
	}
	if (settings->binding_conflict_open) {
		TieControllerSettings_BindingConflictModal(settings, ui, controller);
		return;
	}
	if (settings->restore_modal_open) {
		TieControllerSettings_RestoreModal(settings, ui, config);
		return;
	}
	if (controller)
		TieControllerSettings_BindingDetailModal(settings, ui, controller);
}
