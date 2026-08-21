#ifndef TIE_RUNTIME_SNAPSHOT_H
#define TIE_RUNTIME_SNAPSHOT_H

#include "tie_runtime/snapshot/snapshot_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ===== Public accessors =====
 *
 * Both return NULL until enough ticks have emitted (current after
 * tick 1, previous after tick 2). Pointers are stable for the tie_core
 * session — applications may cache them at first non-NULL read. The
 * *contents* change every tick. Read between TieRuntime_Tick calls only. */
const TieSnapshot* TieSnapshot_Current(void);
const TieSnapshot* TieSnapshot_Previous(void);

/* Pull-API for the live cursor's 8bpp bitmap. Pairs with the
 * TieCursorState in the snapshot — the snapshot carries position +
 * dimensions and this returns the raw pixel data.
 *
 * Returns NULL when no cursor module is initialised. out_w/out_h
 * mirror snapshot.cursor.w/h. The pointer is owned by tie_core and
 * stable for the session; the bitmap content itself mutates on every
 * lcursor_Set_Cursor call (pointer ↔ wait), so callers re-check
 * cursor.kind to decide whether to re-upload. */
const uint8_t* TieCursorSnapshot_Bitmap(int16_t* out_w, int16_t* out_h);

/* Borrow the festring color-remap table (engine `color_remap_table[]`
 * at tie.c:435). Hosts that render festring text snapshots
 * (`compose_text_emit_festring_*`) need this to translate the LOGICAL
 * color values produced by festring_settextcolor / panel_buildobjectname's
 * 0xFE-escape bytes into POST-remap palette indices the engine writes
 * to the framebuffer. Mirrors `festring.c::remap_color`: for
 * `color >= 0x40` use `table[color]`, else passthrough.
 *
 * Returned pointer is owned by tie_core and stable for the session. The
 * table is engine state initialized statically and never mutated at
 * runtime, so callers can read it any time after tie_core initialization. */
const uint8_t* TieTextSnapshot_ColorRemapTable(void);

/* STRINGS.DAT cell-indexed accessor. tie_core's fediskio_loadstringdata
 * loads the file once at boot, relocates the 32-bit offset header into
 * resolved pointers, and exposes the named globals (messagetable,
 * componentnames, etc.). HD overlays that need raw cell-index access
 * (e.g. cockpit AI-order labels addressing messagetable[N] by
 * STR_CELL_MESSAGETABLE_0+N) should use these instead of opening
 * strings.dat a second time. Returns NULL when cell is out of range
 * or tie_core hasn't loaded strings.dat yet; pointer is owned by tie_core
 * and stable until the next mission boot. */
const char* TieTextSnapshot_StringCell(int cell);
int TieTextSnapshot_StringCount(void);

/* AI-order-code → messagetable[] index lookup table, 69 entries. The
 * engine uses this in user_resetview to map a craft's `order` byte
 * to a display-message cell (panel.c:1713). HD overlays that build
 * threat-view labels read it via this accessor. */
const uint8_t* TieTextSnapshot_ConvertMessageTable(void);

typedef enum TieSnapshotChannel {
	TIE_SNAPSHOT_CHANNEL_FLIGHTS,
	TIE_SNAPSHOT_CHANNEL_STATICS,
	TIE_SNAPSHOT_CHANNEL_FLIGHT_COMPONENTS,
	TIE_SNAPSHOT_CHANNEL_BILLBOARDS,
	TIE_SNAPSHOT_CHANNEL_HYPERSTARS,
	TIE_SNAPSHOT_CHANNEL_STARS,
	TIE_SNAPSHOT_CHANNEL_REQUIRED_MODELS,
	TIE_SNAPSHOT_CHANNEL_REQUIRED_SPRITES,
	TIE_SNAPSHOT_CHANNEL_ACTORS_2D,
	TIE_SNAPSHOT_CHANNEL_FILMS_2D,
	TIE_SNAPSHOT_CHANNEL_DRAWS_2D,
	TIE_SNAPSHOT_CHANNEL_UI_TEXTS,
	TIE_SNAPSHOT_CHANNEL_PAINT_CMDS,
	TIE_SNAPSHOT_CHANNEL_TITLE_CRAWL,
	TIE_SNAPSHOT_CHANNEL_EVENTS,
	TIE_SNAPSHOT_CHANNEL_COUNT,
} TieSnapshotChannel;

typedef struct TieSnapshotOverflowStats {
	uint64_t tick;
	uint32_t dropped[TIE_SNAPSHOT_CHANNEL_COUNT];
	uint32_t capacity[TIE_SNAPSHOT_CHANNEL_COUNT];
} TieSnapshotOverflowStats;

/* Per-channel overflow counts for the most recently emitted slot and its
 * fixed capacities. Intended for runtime inspectors. */
void TieSnapshot_OverflowStats(TieSnapshotOverflowStats* out);

#ifdef __cplusplus
}
#endif

#endif /* TIE_RUNTIME_SNAPSHOT_H */
