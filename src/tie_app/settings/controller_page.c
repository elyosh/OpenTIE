#include "tie_app/settings/controller_page.h"

#include "tie_runtime/input/controller_mapping.h"

#include <stdio.h>
#include <string.h>

enum {
	CONTROLLER_PAGE_AXES = 0,
	CONTROLLER_PAGE_ACTIONS,
};

static const char* const k_axis_names[TIE_INPUT_AXIS_COUNT] = { "Yaw", "Pitch", "Roll", "Throttle Rate" };

static TieControllerProfile* TieControllerPage_ActiveProfile(TieControllerSettings* settings,
															 const AeronControllerSnapshot* controller) {
	return controller->kind == AERON_CONTROLLER_KIND_GAMEPAD ? &settings->draft.gamepad
															 : &settings->draft.joystick;
}

static const TieControllerProfile*
TieControllerPage_ActiveProfileConst(const TieControllerSettings* settings,
									 const AeronControllerSnapshot* controller) {
	return controller->kind == AERON_CONTROLLER_KIND_GAMEPAD ? &settings->draft.gamepad
															 : &settings->draft.joystick;
}

static bool TieControllerSettings_SelectorIsAutomatic(const AeronControllerSelector* selector) {
	return !selector->guid[0] && !selector->path[0] && selector->ordinal == 0;
}

static int TieControllerSettings_ControllerOrdinal(const AeronInputSnapshot* input, int slot) {
	int ordinal = 0;
	const AeronControllerSnapshot* target = &input->controllers[slot];
	for (int index = 0; index < slot; ++index) {
		const AeronControllerSnapshot* controller = &input->controllers[index];
		if (controller->connected && strcmp(controller->guid, target->guid) == 0 &&
			strcmp(controller->path, target->path) == 0)
			++ordinal;
	}
	return ordinal;
}

static const AeronControllerSnapshot*
TieControllerPage_SelectedController(const TieControllerSettings* settings, const AeronInputSnapshot* input) {
	return input ? Aeron_SelectController(input, &settings->draft.selector) : NULL;
}

static void TieControllerSettings_ApplyDraft(TieControllerSettings* settings) {
	settings->dirty = !TieControllerMapping_Optionsequal(&settings->draft, &settings->original);
	TieControllerMapping_SetOptions(&settings->draft);
	settings->error[0] = '\0';
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
	if (!controller)
		return false;
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
	return source->kind == AERON_CONTROLLER_DIGITAL_HAT &&
		   controller->kind == AERON_CONTROLLER_KIND_JOYSTICK && source->index < controller->hat_count;
}

static void TieControllerSettings_FormatAxisSource(char* buffer, size_t capacity, int source,
												   const AeronControllerSnapshot* controller) {
	if (source < 0) {
		snprintf(buffer, capacity, "Not Bound");
		return;
	}
	const char* name = controller->kind == AERON_CONTROLLER_KIND_GAMEPAD
						   ? TieControllerPage_GamepadAxisDisplay(source)
						   : NULL;
	const bool available =
		controller->kind == AERON_CONTROLLER_KIND_GAMEPAD
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
	const char* name = NULL;
	if (source->kind == AERON_CONTROLLER_DIGITAL_BUTTON) {
		name = controller->kind == AERON_CONTROLLER_KIND_GAMEPAD
				   ? TieControllerPage_GamepadButtonDisplay(source->index)
				   : NULL;
		if (name)
			snprintf(buffer, capacity, "%s", name);
		else
			snprintf(buffer, capacity, "Button %u", (unsigned)source->index);
	} else if (source->kind == AERON_CONTROLLER_DIGITAL_AXIS_POSITIVE ||
			   source->kind == AERON_CONTROLLER_DIGITAL_AXIS_NEGATIVE) {
		name = controller->kind == AERON_CONTROLLER_KIND_GAMEPAD
				   ? TieControllerPage_GamepadAxisDisplay(source->index)
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
}

void TieControllerSettings_CancelCapture(TieControllerSettings* settings, AeronUiContext* ui) {
	if (ui)
		AeronUi_CancelControllerCapture(ui);
	if (settings) {
		settings->axis_conflict_open = 0;
		settings->binding_conflict_open = 0;
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
	const bool defaults = TieControllerMapping_Optionsequal(&settings->draft, &config->defaults.controller);
	const bool result = defaults
							? TieAppConfig_RestoreController(config, error, error_capacity)
							: TieAppConfig_SetController(config, &settings->draft, error, error_capacity);
	if (!result)
		return false;
	settings->draft = config->requested.controller;
	settings->original = settings->draft;
	settings->dirty = false;
	return true;
}

static void TieControllerSettings_DeviceSelector(TieControllerSettings* settings, AeronUiContext* ui,
												 const AeronInputSnapshot* input) {
	char labels[AERON_CONTROLLER_MAX + 2][192];
	const char* options[AERON_CONTROLLER_MAX + 2];
	AeronControllerSelector choices[AERON_CONTROLLER_MAX + 2];
	const AeronControllerSnapshot* configured = Aeron_SelectController(input, &settings->draft.selector);
	const AeronControllerSelector automatic = { 0 };
	const AeronControllerSnapshot* automatic_controller = Aeron_SelectController(input, &automatic);
	int count = 1;
	int selected = TieControllerSettings_SelectorIsAutomatic(&settings->draft.selector) ? 0 : -1;
	if (automatic_controller)
		snprintf(labels[0], sizeof labels[0], "Automatic - %s", automatic_controller->name);
	else
		snprintf(labels[0], sizeof labels[0], "Automatic - No controller");
	options[0] = labels[0];
	memset(&choices[0], 0, sizeof choices[0]);
	for (int slot = 0; slot < AERON_CONTROLLER_MAX; ++slot) {
		const AeronControllerSnapshot* controller = &input->controllers[slot];
		if (!controller->connected)
			continue;
		int duplicate_count = 0;
		int name_ordinal = 0;
		for (int other = 0; other < AERON_CONTROLLER_MAX; ++other) {
			const AeronControllerSnapshot* candidate = &input->controllers[other];
			if (!candidate->connected || strcmp(candidate->name, controller->name))
				continue;
			if (other < slot)
				++name_ordinal;
			++duplicate_count;
		}
		if (duplicate_count > 1)
			snprintf(labels[count], sizeof labels[count], "%s #%d", controller->name, name_ordinal + 1);
		else
			snprintf(labels[count], sizeof labels[count], "%s", controller->name);
		options[count] = labels[count];
		snprintf(choices[count].guid, sizeof choices[count].guid, "%s", controller->guid);
		snprintf(choices[count].path, sizeof choices[count].path, "%s", controller->path);
		choices[count].ordinal = TieControllerSettings_ControllerOrdinal(input, slot);
		if (configured == controller)
			selected = count;
		++count;
	}
	if (selected < 0) {
		snprintf(labels[count], sizeof labels[count], "Configured Device (Unavailable)");
		options[count] = labels[count];
		choices[count] = settings->draft.selector;
		selected = count++;
	}
	if (AeronUi_Selector(ui, "Device", &selected, options, count)) {
		settings->draft.selector = choices[selected];
		TieControllerSettings_ApplyDraft(settings);
	}
}

static int TieControllerSettings_ProfileMissingCount(const TieControllerProfile* profile,
													 const AeronControllerSnapshot* controller) {
	int missing = 0;
	for (int axis = 0; axis < TIE_INPUT_AXIS_COUNT; ++axis) {
		const int source = profile->mapping.axes[axis].source;
		if (source < 0)
			continue;
		if (controller->kind == AERON_CONTROLLER_KIND_GAMEPAD) {
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
		AeronUi_Error(ui, TieControllerSettings_SelectorIsAutomatic(&settings->draft.selector)
							  ? "No controller is connected."
							  : "The configured controller is unavailable.");
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
	if (!controller || source < 0)
		return 0.0f;
	int value;
	if (controller->kind == AERON_CONTROLLER_KIND_GAMEPAD) {
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

static void TieControllerSettings_AssignCapturedAxis(TieControllerSettings* settings,
													 const AeronControllerSnapshot* controller,
													 TieInputAxis axis, int source) {
	TieControllerProfile* profile = TieControllerPage_ActiveProfile(settings, controller);
	for (int other = 0; other < TIE_INPUT_AXIS_COUNT; ++other) {
		if (other == (int)axis || profile->mapping.axes[other].source != source)
			continue;
		settings->pending_axis = axis;
		settings->pending_axis_source = source;
		settings->conflicting_axis = other;
		settings->axis_conflict_open = 1;
		return;
	}
	profile->mapping.axes[axis].source = (int8_t)source;
	TieControllerSettings_ApplyDraft(settings);
}

static void TieControllerSettings_AxisEditor(TieControllerSettings* settings, AeronUiContext* ui,
											 const AeronControllerSnapshot* controller, TieInputAxis axis) {
	TieControllerProfile* profile = TieControllerPage_ActiveProfile(settings, controller);
	const AeronControllerKind kind = controller->kind;
	TieInputAxisBinding* binding = &profile->mapping.axes[axis];
	char source[128];
	TieControllerSettings_FormatAxisSource(source, sizeof source, binding->source, controller);
	AeronUi_PushId(ui, axis);
	const AeronUiControllerCaptureDesc desc = { controller->instance_id,
												AERON_UI_CONTROLLER_CAPTURE_ANALOG_AXIS };
	AeronUiControllerInput captured;
	const AeronUiControllerCaptureResult capture =
		AeronUi_ControllerCapture(ui, "Source", source, &desc, &captured);
	if (capture == AERON_UI_CONTROLLER_CAPTURE_CAPTURED && captured.controller_kind == kind)
		TieControllerSettings_AssignCapturedAxis(settings, controller, axis, captured.value.axis);
	float live = TieControllerSettings_ControllerAxisValue(controller, binding->source);
	if (TieControllerMapping_EffectiveAxisInvert(kind, axis, binding->invert))
		live = -live;
	AeronUi_ControllerAxisMeter(ui, "Input", live, binding->deadzone);
	int invert = binding->invert;
	if (AeronUi_Toggle(ui, "Invert", &invert)) {
		binding->invert = invert != 0;
		TieControllerSettings_ApplyDraft(settings);
	}
	int deadzone_percent = (int)(binding->deadzone * 100.0f + 0.5f);
	if (AeronUi_SliderInt(ui, "Deadzone", &deadzone_percent, 0, 100, 5, "%d%%")) {
		binding->deadzone = (float)deadzone_percent / 100.0f;
		TieControllerSettings_ApplyDraft(settings);
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
	char text[192];
	snprintf(text, sizeof text, "%s already uses this source. Replace it with %s?",
			 k_axis_names[settings->conflicting_axis], k_axis_names[settings->pending_axis]);
	AeronUi_Error(ui, text);
	AeronUi_BeginColumns(ui, 2, NULL);
	if (AeronUi_Button(ui, "Replace")) {
		TieControllerProfile* profile = TieControllerPage_ActiveProfile(settings, controller);
		profile->mapping.axes[settings->conflicting_axis].source = -1;
		profile->mapping.axes[settings->pending_axis].source = (int8_t)settings->pending_axis_source;
		settings->axis_conflict_open = 0;
		TieControllerSettings_ApplyDraft(settings);
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
	const AeronControllerKind kind = controller->kind;
	const AeronUiControllerCaptureDesc desc = { controller->instance_id,
												AERON_UI_CONTROLLER_CAPTURE_DIGITAL };
	AeronUiControllerInput captured;
	const AeronUiControllerCaptureResult result =
		AeronUi_ControllerCapture(ui, "Find Binding...", "Press to identify", &desc, &captured);
	if (result != AERON_UI_CONTROLLER_CAPTURE_CAPTURED || captured.controller_kind != kind)
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
	const AeronControllerKind kind = controller->kind;
	const bool has_capacity = profile->binding_count < TIE_CONTROLLER_BINDING_CAP;
	const AeronUiControllerCaptureDesc desc = { has_capacity ? controller->instance_id : 0,
												AERON_UI_CONTROLLER_CAPTURE_DIGITAL };
	AeronUiControllerInput captured;
	const AeronUiControllerCaptureResult capture =
		AeronUi_ControllerCapture(ui, "Add Binding...", "Press to add", &desc, &captured);
	if (capture == AERON_UI_CONTROLLER_CAPTURE_CAPTURED && captured.controller_kind == kind)
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
	if (!AeronUi_BeginModal(ui, "RESTORE CONTROLLER DEFAULTS", &settings->restore_modal_open, NULL))
		return;
	AeronUi_Help(ui, "This replaces the device selection and controller mappings with the shipped defaults.");
	AeronUi_BeginColumns(ui, 2, NULL);
	if (AeronUi_Button(ui, "Restore")) {
		settings->draft = config->defaults.controller;
		settings->restore_modal_open = 0;
		settings->binding_modal_open = 0;
		TieControllerSettings_ApplyDraft(settings);
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
	TieControllerSettings_ControllerWarning(settings, ui, controller);
	if (controller && AeronUi_SegmentedSelector(ui, "Controller Page", &settings->page, pages, 2)) {
		AeronUi_CancelControllerCapture(ui);
		settings->binding_modal_open = 0;
	}
	float trailing_height = 143.0f;
	if (settings->error[0])
		trailing_height += AeronUi_MeasureHelpHeight(ui, settings->error, 0.0f) + 60.0f;
	if (controller && settings->page == CONTROLLER_PAGE_AXES) {
		float scroll_height = AeronUi_AvailableHeight(ui) - trailing_height;
		if (scroll_height < 180.0f)
			scroll_height = 180.0f;
		if (AeronUi_BeginScroll(ui, "Flight Axes", scroll_height)) {
			TieControllerSettings_AxisPage(settings, ui, controller);
			AeronUi_EndScroll(ui);
		}
	} else if (controller) {
		TieControllerSettings_ActionsPage(settings, ui, controller, trailing_height);
	}
	if (settings->error[0]) {
		AeronUi_Error(ui, settings->error);
		if (AeronUi_Button(ui, "Dismiss Error"))
			settings->error[0] = '\0';
	}
	if (AeronUi_Button(ui, "Restore Controller Defaults"))
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
