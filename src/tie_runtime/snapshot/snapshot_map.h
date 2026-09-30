#ifndef TIE_RUNTIME_SNAPSHOT_MAP_H
#define TIE_RUNTIME_SNAPSHOT_MAP_H

struct Input;

/* Borrowed widget; clear it before the briefing input is destroyed. */
void TieMapSnapshot_SetInput(struct Input* input);
void TieMapSnapshot_Capture(void);

#endif
