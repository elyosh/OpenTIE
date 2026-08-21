#ifndef TIE_CONTROLLER_SETTINGS_H
#define TIE_CONTROLLER_SETTINGS_H

#include <stdbool.h>
#include <stddef.h>

#include "aeron/scene/ui.h"
#include "tie_app/config/app_config.h"

#define TIE_CONTROLLER_SETTINGS_ERROR_CAPACITY 512

typedef struct TieControllerSettings {
	TieControllerOptions original;
	TieControllerOptions draft;
	int page;
	int axis;
	int category;
	size_t action_selected;
	size_t binding_selected;
	TieInputAction selected_action;
	TieInputAxis pending_axis;
	int pending_axis_source;
	int conflicting_axis;
	AeronControllerDigitalSource pending_digital;
	TieInputAction conflicting_action;
	uint32_t active_instance;
	int binding_modal_open;
	int axis_conflict_open;
	int binding_conflict_open;
	int restore_modal_open;
	bool dirty;
	char error[TIE_CONTROLLER_SETTINGS_ERROR_CAPACITY];
} TieControllerSettings;

void TieControllerSettings_Open(TieControllerSettings* settings, const TieAppConfigState* config);
void TieControllerSettings_Draw(TieControllerSettings* settings, AeronUiContext* ui,
								const AeronInputSnapshot* input);
void TieControllerSettings_DrawModals(TieControllerSettings* settings, AeronUiContext* ui,
									  const AeronInputSnapshot* input, const TieAppConfigState* config);
bool TieControllerSettings_Commit(TieControllerSettings* settings, TieAppConfigState* config, char* error,
								  size_t error_capacity);
void TieControllerSettings_CancelCapture(TieControllerSettings* settings, AeronUiContext* ui);

#endif
