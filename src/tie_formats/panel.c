#include "tie_formats/panel.h"
#include "tie_formats/internal.h"

#include <stdlib.h>
#include <string.h>

void TiePanel_Free(TiePanel* panel) {
	if (!panel)
		return;
	free(panel->sections);
	memset(panel, 0, sizeof *panel);
}

bool TiePanel_Parse(const void* data, size_t size, TiePanel* out, TieFormatError* error) {
	const uint8_t* bytes = data;
	if (out)
		memset(out, 0, sizeof *out);
	if (!bytes || !out || size < 16 || size > TIE_FORMAT_MAX_ENTRY_SIZE)
		return TieFormat_SetError(error, 20, "invalid cockpit LFD size");
	size_t offset = 0;
	uint32_t count = 0;
	while (offset < size) {
		if (size - offset < 16)
			return TieFormat_SetError(error, 21, "truncated cockpit LFD section header");
		const uint32_t section_size = TieFormat_ReadU32Le(bytes + offset + 12);
		if ((size_t)section_size > size - offset - 16)
			return TieFormat_SetError(error, 22, "cockpit LFD section %u is truncated", count);
		offset += 16 + (size_t)section_size;
		if (++count > 64)
			return TieFormat_SetError(error, 23, "cockpit LFD has too many sections");
	}
	TiePanelSection* sections = calloc(count, sizeof *sections);
	if (!sections)
		return TieFormat_SetError(error, 24, "cockpit LFD allocation failed");
	offset = 0;
	for (uint32_t index = 0; index < count; ++index) {
		TiePanelSection* section = &sections[index];
		section->type = TieFormat_ReadFourcc(bytes + offset);
		memcpy(section->name, bytes + offset + 4, 8);
		section->name[8] = '\0';
		section->size = TieFormat_ReadU32Le(bytes + offset + 12);
		section->data = bytes + offset + 16;
		offset += 16 + section->size;
	}
	out->sections = sections;
	out->count = count;
	return true;
}

const TiePanelSection* TiePanel_Find(const TiePanel* panel, uint32_t type) {
	if (!panel)
		return NULL;
	const TiePanelSection* found = NULL;
	for (uint32_t index = 0; index < panel->count; ++index) {
		if (panel->sections[index].type != type)
			continue;
		if (found)
			return NULL;
		found = &panel->sections[index];
	}
	return found;
}
