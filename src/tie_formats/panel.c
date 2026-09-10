#include "tie_formats/panel.h"
#include <string.h>

void TiePanel_Free(TiePanel* panel) {
	if (!panel)
		return;
	AeronLfd lfd = { panel->sections, panel->count };
	AeronLfd_Free(&lfd);
	memset(panel, 0, sizeof *panel);
}

bool TiePanel_Parse(const void* bytes, size_t size, TiePanel* out, TieFormatError* error) {
	AeronLfd lfd = { 0 };
	if (out)
		memset(out, 0, sizeof *out);
	if (!AeronLfd_Parse(bytes, size, out ? &lfd : NULL, error))
		return false;
	out->sections = lfd.entries;
	out->count = lfd.count;
	return true;
}

const TiePanelSection* TiePanel_Find(const TiePanel* panel, uint32_t type) {
	if (!panel)
		return NULL;
	const AeronLfd lfd = { panel->sections, panel->count };
	return AeronLfd_Find(&lfd, type);
}
