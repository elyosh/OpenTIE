#include "tie_app/settings/bindings_editor.h"
#include <stdio.h>

void TieBindingsEditor_Init(TieBindingsEditor* editor) {
	*editor = (TieBindingsEditor) { .action_selected = SIZE_MAX, .binding_selected = SIZE_MAX };
}

void TieBindingsEditor_Select(TieBindingsEditor* editor, TieInputAction action, bool open_modal) {
	editor->selected_action = action;
	editor->category = TieInputActions_Category(action);
	editor->action_selected = 0;
	for (int candidate = TIE_INPUT_ACTION_NONE + 1; candidate < (int)action; ++candidate)
		if ((int)TieInputActions_Category((TieInputAction)candidate) == editor->category)
			++editor->action_selected;
	editor->binding_selected = SIZE_MAX;
	if (open_modal)
		editor->binding_modal_open = 1;
}

void TieBindingsEditor_Category(TieBindingsEditor* editor, AeronUiContext* ui) {
	static const char* const categories[] = { "Weapons", "Targets", "Throttle", "View",
											  "Info",    "System",  "Comms" };
	if (AeronUi_SegmentedSelector(ui, "Action Category", &editor->category, categories,
								  TIE_INPUT_ACTION_CATEGORY_COUNT)) {
		editor->action_selected = SIZE_MAX;
		editor->selected_action = TIE_INPUT_ACTION_NONE;
	}
}

void TieBindingsEditor_Actions(TieBindingsEditor* editor, AeronUiContext* ui, const AeronUiListItem* items,
							   size_t count, float trailing_height) {
	float height = AeronUi_AvailableHeight(ui) - trailing_height;
	if (height < 180.0f)
		height = 180.0f;
	uint32_t result = AeronUi_ListBox(ui, "Actions", items, count, &editor->action_selected, height);
	if ((result & AERON_UI_LIST_ACTIVATED) && editor->action_selected < count)
		TieBindingsEditor_Select(editor, (TieInputAction)items[editor->action_selected].id, true);
}

bool TieBindingsEditor_BeginDetail(TieBindingsEditor* editor, AeronUiContext* ui) {
	if (!editor->binding_modal_open || editor->selected_action == TIE_INPUT_ACTION_NONE)
		return false;
	return AeronUi_BeginModal(ui, TieInputActions_DisplayName(editor->selected_action),
							  &editor->binding_modal_open,
							  &(AeronUiWindowDesc) { .width_ref = 720.0f, .centered = 1 }) != 0;
}

void TieBindingsEditor_List(TieBindingsEditor* editor, AeronUiContext* ui, const AeronUiListItem* items,
							size_t count, const char* empty_text) {
	AeronUi_Header(ui, "Current Bindings");
	if (count)
		AeronUi_ListBox(ui, "Bindings", items, count, &editor->binding_selected, 180.0f);
	else {
		editor->binding_selected = SIZE_MAX;
		AeronUi_Help(ui, empty_text);
	}
}

bool TieBindingsEditor_Remove(AeronUiContext* ui) { return AeronUi_Button(ui, "Remove Binding") != 0; }

void TieBindingsEditor_EndDetail(TieBindingsEditor* editor, AeronUiContext* ui) {
	if (AeronUi_Button(ui, "Done"))
		editor->binding_modal_open = 0;
	AeronUi_EndModal(ui);
}

bool TieBindingsEditor_Conflict(AeronUiContext* ui, int* open, const char* source, TieInputAction previous,
								TieInputAction replacement) {
	if (!AeronUi_BeginModal(ui, "CONTROL ALREADY BOUND", open, NULL))
		return false;
	char text[512];
	snprintf(text, sizeof text, "%s is assigned to %s. Replace it with %s?", source,
			 TieInputActions_DisplayName(previous), TieInputActions_DisplayName(replacement));
	AeronUi_Error(ui, text);
	AeronUi_BeginColumns(ui, 2, NULL);
	bool replace = AeronUi_Button(ui, "Replace") != 0;
	if (replace)
		*open = 0;
	AeronUi_NextColumn(ui);
	if (AeronUi_Button(ui, "Cancel"))
		*open = 0;
	AeronUi_EndColumns(ui);
	AeronUi_EndModal(ui);
	return replace;
}
