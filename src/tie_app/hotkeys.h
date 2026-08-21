#ifndef TIE_APP_HOTKEYS_H
#define TIE_APP_HOTKEYS_H

#include <stdbool.h>

typedef struct AeronInputSnapshot AeronInputSnapshot;

typedef struct TieHotkeys {
	int last_fullscreen;
	bool paused;
} TieHotkeys;

typedef struct TieHotkeysFrame {
	bool menu_open;
	bool paused;
} TieHotkeysFrame;

void TieHotkeys_Init(TieHotkeys* hotkeys);
TieHotkeysFrame TieHotkeys_Process(TieHotkeys* hotkeys, const AeronInputSnapshot* input);

#endif
