/*
 * tie_settings — the remaster settings menu (AeronUi).
 *
 * A modal overlay above the game layers. Live settings are applied through
 * typed option owners; the flight engine is queued for the next flight, and
 * the remaining launch-only settings stay pending until restart.
 * The game's Esc options flow opens the menu; gamepad Start opens it outside flight. Esc closes it.
 * While open, the frame loop skips the host input pump and TieRuntime_Tick and pauses audio.
 */
#ifndef TIE_APP_SETTINGS_H
#define TIE_APP_SETTINGS_H

#include <stdbool.h>
#include <stddef.h>

#include "aeron/aeron.h"

#include "tie_app/config/app_config.h"
#include "tie_app/ui.h"

/* Borrows the application UI and config for the process lifetime. */
bool TieSettings_Init(TieUi* ui, TieAppConfigState* config, bool has_tie95, bool has_tie98, char* error,
					  size_t error_capacity);
void TieSettings_Shutdown(void);

/* Nonzero when init succeeded and the menu can be opened. */
bool TieSettings_Available(void);
/* Nonzero while the menu is open (game input + tick must be gated). */
bool TieSettings_Open(void);
/* Nonzero while AeronUi is capturing a controller control for rebinding. */
bool TieSettings_CapturesController(void);
void TieSettings_Show(void);
void TieSettings_Toggle(void);

/* Flushes option owners and commits an open controller draft. */
bool TieSettings_Flush(char* error, size_t error_capacity);

/* Builds and submits the menu UI for this frame. Call after
 * TieRemaster_Frame so the menu layer composites above the game.
 * No-op while closed. */
void TieSettings_Frame(const AeronInputSnapshot* input, float dt_seconds);

#endif /* TIE_APP_SETTINGS_H */
