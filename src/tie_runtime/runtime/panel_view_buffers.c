#include "tie_runtime/runtime/panel_view_buffers.h"

#include "tie/panel.h"

#include <stddef.h>
#include <stdlib.h>

static void* s_panel_view_buffers[PANEL_NUM_VIEWS];

void TiePanelViewBuffers_Set(uint16_t view_idx, void* buffer) {
	if (view_idx >= PANEL_NUM_VIEWS)
		return;
	s_panel_view_buffers[view_idx] = buffer;
}

void TiePanelViewBuffers_FreeAll(void) {
	uint16_t i;

	for (i = 0; i < PANEL_NUM_VIEWS; ++i) {
		free(s_panel_view_buffers[i]);
		s_panel_view_buffers[i] = NULL;
		panelviewptrs[i].handle = 0;
		panelviewptrs[i].image = NULL;
		panelviewptrs[i].mask = NULL;
		panelviewptrs[i].palette = NULL;
	}
	panelsloadedflag = 0;
}
