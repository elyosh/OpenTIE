#ifndef TIE_FORMATS_PANEL_H
#define TIE_FORMATS_PANEL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "tie_formats/common.h"

typedef struct TiePanelSection {
	uint32_t type;
	char name[9];
	const uint8_t* data;
	size_t size;
} TiePanelSection;

typedef struct TiePanel {
	TiePanelSection* sections;
	uint32_t count;
} TiePanel;

bool TiePanel_Parse(const void* bytes, size_t size, TiePanel* out, TieFormatError* error);
const TiePanelSection* TiePanel_Find(const TiePanel* panel, uint32_t type);
void TiePanel_Free(TiePanel* panel);

#endif
