#ifndef TIE_RUNTIME_RUNTIME_PANEL_VIEW_BUFFERS_H
#define TIE_RUNTIME_RUNTIME_PANEL_VIEW_BUFFERS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Host ownership of the cockpit-view LFD blobs preloaded by
 * panel_tryEMSforpanels. The original allocated each view through
 * XMEMHDL_Alloc_Handle and kept the handle in panelviewptrs[].handle; the
 * port stores a malloc'd pointer here so flight teardown can release it. */
void TiePanelViewBuffers_Set(uint16_t view_idx, void* buffer);

/* Release every preloaded panel-view buffer, clear the panelviewptrs[]
 * entries that referenced them, and reset panelsloadedflag. Called from the
 * fediskio flight-handle teardown path. */
void TiePanelViewBuffers_FreeAll(void);

#ifdef __cplusplus
}
#endif

#endif
