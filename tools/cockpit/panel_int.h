/*
 * panel_int — cockpit .INT descriptor decoder.
 *
 * Layout (1589 bytes total for all 12 flyable craft):
 *   +0x000  28 × PanelViewDef (36 bytes each)   = 1008 bytes
 *   +0x3F0  95 × HudInstrument (6 bytes each)   =  570 bytes
 *   +0x626  11 × byte parts[]                   =   11 bytes
 *
 * Codec parity: matches PanelViewDef_decode / HudInstrument_decode in
 * src/tie/panel.c. parts[0..7] holds the .PNL filename basename (NUL-
 * padded to 8 chars), parts[8] is a mode flag, parts[9]+parts[10] is the
 * total shape count to read from that .PNL.
 *
 * PanelViewDef.flags is the dispatch byte the runtime walks in
 * panel_dosetnewpilotview:
 *   0x00            slot unused
 *   0x01            primary view (own LFD at <cockpitdir>/<name>.LFD)
 *   0x80 | N        inherit-from-view-N (no own LFD)
 *   0x90/0x91       inherited / special inherit (threat-mode tagging)
 *   0xC0 | N        mirror-of-view-N (flip-x of view N's bitmap)
 *
 * The extractor only needs to load LFDs for views with flags==1 (other
 * views inherit pixels from their source view).
 */
#ifndef TIE_TOOLS_COCKPIT_PANEL_INT_H
#define TIE_TOOLS_COCKPIT_PANEL_INT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PANEL_INT_NUM_VIEWS 28
#define PANEL_INT_NUM_INSTRUMENTS 95
#define PANEL_INT_NUM_PARTS 11
#define PANEL_INT_FILE_SIZE 1589

typedef struct {
	uint8_t flags;
	char name[10]; /* 9 chars from disk + NUL */
	uint16_t pos_x;
	uint16_t pos_y;
	uint16_t width;
	uint16_t depth;
	int16_t yoffset;
	char title[17]; /* 16 chars from disk + NUL */
} TieCockpitPanelIntView;

typedef struct {
	uint16_t x;
	uint16_t y;
	uint8_t param1;
	uint8_t param2;
} TieCockpitPanelIntInstruction;

typedef struct {
	TieCockpitPanelIntView views[PANEL_INT_NUM_VIEWS];
	TieCockpitPanelIntInstruction instruments[PANEL_INT_NUM_INSTRUMENTS];
	uint8_t parts[PANEL_INT_NUM_PARTS];
	char parts_basename[9];     /* parts[0..7] + NUL */
	uint16_t parts_shape_count; /* parts[9] + parts[10] (sum, per panel.c:2228) */
} TieCockpitPanelInt;

/* Read and decode a .INT file. Returns false on I/O / size error. */
bool TieCockpitPanelInt_Open(TieCockpitPanelInt* out, const char* path, char* err, size_t errsz);

#ifdef __cplusplus
}
#endif

#endif
