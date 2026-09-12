#ifndef TIE_KEYBOARD_SETTINGS_H
#define TIE_KEYBOARD_SETTINGS_H
#include "tie_app/config/app_config.h"
#include "tie_app/settings/bindings_editor.h"

typedef struct TieKeyboardSettings {
	TieKeyboardBindings original;
	TieKeyboardBindings draft;
	TieBindingsEditor editor;
	AeronKeyChord pending;
	TieInputAction conflicting_action;
	int conflict_open;
	int restore_open;
	bool dirty;
	bool restore_defaults;
	char error[512];
} TieKeyboardSettings;

void TieKeyboardSettings_Open(TieKeyboardSettings* settings, const TieAppConfigState* config);
void TieKeyboardSettings_Draw(TieKeyboardSettings* settings, AeronUiContext* ui);
void TieKeyboardSettings_DrawModals(TieKeyboardSettings* settings, AeronUiContext* ui,
									const TieAppConfigState* config);
void TieKeyboardSettings_CancelCapture(TieKeyboardSettings* settings, AeronUiContext* ui);
bool TieKeyboardSettings_Commit(TieKeyboardSettings* settings, TieAppConfigState* config, char* error,
								size_t capacity);
#endif
