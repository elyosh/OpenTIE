#ifndef TIE_REPLAYIO_H
#define TIE_REPLAYIO_H

/* Replay spooling, mission checkpoints, and viewer entry. */

#include "tie/panel.h"
#include "tie/tie.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int16_t replayio_copytosave(const char* fname);
int16_t replayio_copyfromsave(const char* fname);
int replayio_openreplayinputfile(void);
int16_t replayio_spoolreplayinput(void);
int16_t replayio_savereplaybuffer(void);
int replayio_restorereplaybuffer(void);
void replayio_setreturnview(void);

/* --------------------------------------------------------------------------
 * Dual-purpose checkpoint tables. Each entry is (ptr, size) describing a
 * dynamic-state region to serialize. Terminated by the first nullptr in
 * `savearrayptrs`. 67 entries are populated at init by the engine's boot
 * code (shipext/create); 68th is the nullptr terminator.
 *
 * NOTE: the binary reads these two different ways:
 *   - replayio_copytosave / copyfromsave use u32 stride (full iteration).
 *   - replay_savereplay / loadreplay use u16 stride, so they effec-
 *     tively consume only the first entry (objects[]) before terminating.
 *     That's reproduced explicitly in replay.c (single hardcoded copy)
 *     rather than kept as a trick on this table. */
extern void* savearrayptrs[68];
extern uint32_t savearraysizes[68];

/* Replay-viewer CAMERA / FILM panel sections (demo `_replayviewptr`). */
extern PanelViewPtrs replayviewptr;

void replayio_replayscreen(void);

#ifdef __cplusplus
}
#endif

#endif
