#ifndef TIE_APP_UI_H
#define TIE_APP_UI_H

#include <stdbool.h>
#include <stddef.h>

#include "aeron/aeron.h"
#include "aeron/scene/font_atlas.h"
#include "aeron/scene/ui.h"

#include "tie_app/config/app_config.h"

typedef struct TieUi {
	AeronUiContext* context;
	AeronFontAtlas font;
} TieUi;

bool TieUi_Init(TieUi* ui, const TieAppUiConfig* config, char* error, size_t error_capacity);
void TieUi_Shutdown(TieUi* ui);
AeronUiContext* TieUi_Context(TieUi* ui);

#endif
