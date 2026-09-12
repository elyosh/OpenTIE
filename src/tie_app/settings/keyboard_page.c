#include "tie_app/settings/keyboard_page.h"
#include <stdio.h>
#include <string.h>

void TieKeyboardSettings_Open(TieKeyboardSettings* settings, const TieAppConfigState* config) {
	memset(settings, 0, sizeof *settings);
	settings->original = settings->draft = config->requested.keyboard;
	TieBindingsEditor_Init(&settings->editor);
}

void TieKeyboardSettings_CancelCapture(TieKeyboardSettings* settings, AeronUiContext* ui) {
	AeronUi_CancelKeyboardCapture(ui);
	settings->editor.binding_modal_open = 0;
	settings->conflict_open = settings->restore_open = 0;
}

static void Changed(TieKeyboardSettings* settings) {
	TieKeyboardMapping_Sort(&settings->draft);
	settings->dirty =
		settings->restore_defaults || !TieKeyboardMapping_Equal(&settings->original, &settings->draft);
	settings->restore_defaults = false;
	settings->editor.binding_selected = SIZE_MAX;
	settings->error[0] = 0;
}

static bool Captured(TieKeyboardSettings* settings, AeronUiContext* ui, const char* label,
					 const char* display, AeronKeyChord* chord) {
	if (AeronUi_KeyboardCapture(ui, label, display, chord) != AERON_UI_KEYBOARD_CAPTURE_CAPTURED)
		return false;
	if (TieKeyboardMapping_Shortcut(*chord) != TIE_KEYBOARD_SHORTCUT_NONE) {
		snprintf(settings->error, sizeof settings->error,
				 "This key combination is reserved for the application.");
		return false;
	}
	if (!TieKeyboardMapping_SourceValid(*chord)) {
		snprintf(settings->error, sizeof settings->error, "This key combination is not supported.");
		return false;
	}
	settings->error[0] = 0;
	return true;
}

static void AddBinding(TieKeyboardSettings* settings, AeronKeyChord source) {
	size_t existing = TieKeyboardMapping_Find(&settings->draft, source);
	if (existing != SIZE_MAX) {
		TieInputAction action = settings->draft.bindings[existing].action;
		if (action == settings->editor.selected_action) {
			size_t position = 0;
			for (size_t i = 0; i < existing; ++i)
				position += settings->draft.bindings[i].action == action;
			settings->editor.binding_selected = position;
		} else {
			settings->pending = source;
			settings->conflicting_action = action;
			settings->conflict_open = 1;
		}
		return;
	}
	if (settings->draft.count == TIE_KEYBOARD_BINDING_CAP) {
		snprintf(settings->error, sizeof settings->error, "The keyboard has reached its binding capacity.");
		return;
	}
	settings->draft.bindings[settings->draft.count++] =
		(TieKeyboardBinding) { source, settings->editor.selected_action };
	Changed(settings);
}

static void DescribeAction(const TieKeyboardBindings* profile, TieInputAction action, char* text,
						   size_t capacity) {
	text[0] = 0;
	for (size_t i = 0; i < profile->count; ++i) {
		if (profile->bindings[i].action != action)
			continue;
		char label[128];
		TieKeyboardMapping_FormatSource(label, sizeof label, profile->bindings[i].source);
		size_t length = strlen(text);
		int written = snprintf(text + length, capacity - length, "%s%s", length ? "   " : "", label);
		if (written < 0 || (size_t)written >= capacity - length)
			break;
	}
	if (!text[0])
		snprintf(text, capacity, "Not Bound");
}

void TieKeyboardSettings_Draw(TieKeyboardSettings* settings, AeronUiContext* ui) {
	TieBindingsEditor_Category(&settings->editor, ui);
	AeronKeyChord source;
	if (Captured(settings, ui, "Find Binding...", "Press to identify", &source)) {
		size_t index = TieKeyboardMapping_Find(&settings->draft, source);
		if (index == SIZE_MAX)
			snprintf(settings->error, sizeof settings->error, "This key combination is not bound.");
		else
			TieBindingsEditor_Select(&settings->editor, settings->draft.bindings[index].action, false);
	}
	AeronUi_Spacer(ui, 8.0f);
	AeronUiListItem items[TIE_INPUT_ACTION_COUNT - 1];
	char details[TIE_INPUT_ACTION_COUNT - 1][256];
	size_t count = 0;
	for (int action = TIE_INPUT_ACTION_NONE + 1; action < TIE_INPUT_ACTION_COUNT; ++action) {
		if ((int)TieInputActions_Category((TieInputAction)action) != settings->editor.category)
			continue;
		DescribeAction(&settings->draft, (TieInputAction)action, details[count], sizeof details[count]);
		items[count] = (AeronUiListItem) { .id = (uint64_t)action,
										   .label = TieInputActions_DisplayName((TieInputAction)action),
										   .detail = details[count] };
		++count;
	}
	float trailing_height = 143.0f;
	if (settings->error[0])
		trailing_height += AeronUi_MeasureHelpHeight(ui, settings->error, 0.0f) + 60.0f;
	TieBindingsEditor_Actions(&settings->editor, ui, items, count, trailing_height);
	if (settings->error[0]) {
		AeronUi_Error(ui, settings->error);
		if (AeronUi_Button(ui, "Dismiss Error"))
			settings->error[0] = 0;
	}
	if (AeronUi_Button(ui, "Restore Defaults")) {
		AeronUi_CancelKeyboardCapture(ui);
		settings->restore_open = 1;
	}
}

static void Detail(TieKeyboardSettings* settings, AeronUiContext* ui) {
	if (!TieBindingsEditor_BeginDetail(&settings->editor, ui))
		return;
	AeronUiListItem items[TIE_KEYBOARD_BINDING_CAP];
	char labels[TIE_KEYBOARD_BINDING_CAP][128];
	size_t count = 0;
	for (size_t i = 0; i < settings->draft.count; ++i) {
		if (settings->draft.bindings[i].action != settings->editor.selected_action)
			continue;
		TieKeyboardMapping_FormatSource(labels[count], sizeof labels[count],
										settings->draft.bindings[i].source);
		items[count] = (AeronUiListItem) { .id = i, .label = labels[count] };
		++count;
	}
	TieBindingsEditor_List(&settings->editor, ui, items, count, "This action has no keyboard binding.");
	if (settings->editor.binding_selected < count && TieBindingsEditor_Remove(ui)) {
		TieKeyboardMapping_Remove(&settings->draft, (size_t)items[settings->editor.binding_selected].id);
		Changed(settings);
	}
	AeronUi_Separator(ui);
	AeronKeyChord source;
	if (Captured(settings, ui, "Add Binding...", "Press to add", &source))
		AddBinding(settings, source);
	if (settings->error[0])
		AeronUi_Error(ui, settings->error);
	TieBindingsEditor_EndDetail(&settings->editor, ui);
}

static void Restore(TieKeyboardSettings* settings, AeronUiContext* ui, const TieAppConfigState* config) {
	if (!AeronUi_BeginModal(ui, "RESTORE KEYBOARD DEFAULTS", &settings->restore_open, NULL))
		return;
	AeronUi_Help(ui, "Replace all keyboard bindings with the shipped defaults?");
	AeronUi_BeginColumns(ui, 2, NULL);
	if (AeronUi_Button(ui, "Restore")) {
		settings->draft = config->defaults.keyboard;
		settings->restore_defaults = settings->dirty = true;
		settings->restore_open = 0;
		settings->error[0] = 0;
		TieBindingsEditor_Init(&settings->editor);
	}
	AeronUi_NextColumn(ui);
	if (AeronUi_Button(ui, "Cancel"))
		settings->restore_open = 0;
	AeronUi_EndColumns(ui);
	AeronUi_EndModal(ui);
}

void TieKeyboardSettings_DrawModals(TieKeyboardSettings* settings, AeronUiContext* ui,
									const TieAppConfigState* config) {
	if (settings->conflict_open) {
		char label[128];
		TieKeyboardMapping_FormatSource(label, sizeof label, settings->pending);
		if (TieBindingsEditor_Conflict(ui, &settings->conflict_open, label, settings->conflicting_action,
									   settings->editor.selected_action)) {
			size_t existing = TieKeyboardMapping_Find(&settings->draft, settings->pending);
			if (existing != SIZE_MAX) {
				settings->draft.bindings[existing].action = settings->editor.selected_action;
				Changed(settings);
			}
		}
	} else if (settings->restore_open) {
		Restore(settings, ui, config);
	} else {
		Detail(settings, ui);
	}
}

bool TieKeyboardSettings_Commit(TieKeyboardSettings* settings, TieAppConfigState* config, char* error,
								size_t capacity) {
	if (!settings->dirty)
		return true;
	bool ok = settings->restore_defaults
				  ? TieAppConfig_RestoreKeyboard(config, error, capacity)
				  : TieAppConfig_SetKeyboard(config, &settings->draft, error, capacity);
	if (!ok)
		return false;
	TieKeyboardMapping_Install(&config->requested.keyboard);
	settings->draft = settings->original = config->requested.keyboard;
	settings->restore_defaults = settings->dirty = false;
	return true;
}
