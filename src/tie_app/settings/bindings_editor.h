#ifndef TIE_BINDINGS_EDITOR_H
#define TIE_BINDINGS_EDITOR_H

#include "aeron/scene/ui.h"
#include "tie_runtime/input/actions.h"

typedef struct TieBindingsEditor {
	int category;
	size_t action_selected;
	size_t binding_selected;
	TieInputAction selected_action;
	int binding_modal_open;
} TieBindingsEditor;

void TieBindingsEditor_Init(TieBindingsEditor* editor);
void TieBindingsEditor_Select(TieBindingsEditor* editor, TieInputAction action, bool open_modal);
void TieBindingsEditor_Category(TieBindingsEditor* editor, AeronUiContext* ui);
void TieBindingsEditor_Actions(TieBindingsEditor* editor, AeronUiContext* ui, const AeronUiListItem* items,
							   size_t count, float trailing_height);
bool TieBindingsEditor_BeginDetail(TieBindingsEditor* editor, AeronUiContext* ui);
void TieBindingsEditor_List(TieBindingsEditor* editor, AeronUiContext* ui, const AeronUiListItem* items,
							size_t count, const char* empty_text);
bool TieBindingsEditor_Remove(AeronUiContext* ui);
void TieBindingsEditor_EndDetail(TieBindingsEditor* editor, AeronUiContext* ui);
/* Returns true only when Replace is chosen. The dialog owns its open flag. */
bool TieBindingsEditor_Conflict(AeronUiContext* ui, int* open, const char* source, TieInputAction previous,
								TieInputAction replacement);
#endif
