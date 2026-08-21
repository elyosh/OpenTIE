#ifndef TIE_INSTALLATION_UI_H
#define TIE_INSTALLATION_UI_H

#include <stddef.h>
#include <stdint.h>

#include "aeron/scene/ui.h"

/* Draws the shared installation path field and Browse action. */
uint32_t TieInstallation_PathRow(AeronUiContext* ui, const char* label, char* path, size_t path_capacity,
								 uint32_t input_flags);

#endif
