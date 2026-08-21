#include "tie_app/setup/installation_ui.h"

uint32_t TieInstallation_PathRow(AeronUiContext* ui, const char* label, char* path, size_t path_capacity,
								 uint32_t input_flags) {
	return AeronUi_InputTextWithAction(ui, label, path, path_capacity, input_flags, "Browse...");
}
