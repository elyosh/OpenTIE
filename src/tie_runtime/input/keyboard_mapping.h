#ifndef TIE_KEYBOARD_MAPPING_H
#define TIE_KEYBOARD_MAPPING_H

#include "tie_runtime/input/actions.h"
#include <stddef.h>

#define TIE_KEYBOARD_BINDING_CAP 256

typedef struct TieKeyboardBinding {
	AeronKeyChord source;
	TieInputAction action;
} TieKeyboardBinding;

typedef struct TieKeyboardBindings {
	TieKeyboardBinding bindings[TIE_KEYBOARD_BINDING_CAP];
	size_t count;
} TieKeyboardBindings;

typedef enum TieKeyboardPlatform {
	TIE_KEYBOARD_PLATFORM_MACOS,
	TIE_KEYBOARD_PLATFORM_WINDOWS,
	TIE_KEYBOARD_PLATFORM_OTHER,
} TieKeyboardPlatform;

typedef enum TieKeyboardShortcut {
	TIE_KEYBOARD_SHORTCUT_NONE,
	TIE_KEYBOARD_SHORTCUT_SETTINGS,
	TIE_KEYBOARD_SHORTCUT_DEBUG,
	TIE_KEYBOARD_SHORTCUT_FULLSCREEN,
	TIE_KEYBOARD_SHORTCUT_PAUSE,
	TIE_KEYBOARD_SHORTCUT_MOUSE,
} TieKeyboardShortcut;

void TieKeyboardMapping_SetPolicy(TieKeyboardPlatform platform, bool debug_available);
TieKeyboardShortcut TieKeyboardMapping_Shortcut(AeronKeyChord source);
bool TieKeyboardMapping_SourceValid(AeronKeyChord source);
void TieKeyboardMapping_FormatSource(char* text, size_t capacity, AeronKeyChord source);
size_t TieKeyboardMapping_Find(const TieKeyboardBindings* profile, AeronKeyChord source);
bool TieKeyboardMapping_Equal(const TieKeyboardBindings* a, const TieKeyboardBindings* b);
void TieKeyboardMapping_Sort(TieKeyboardBindings* profile);
void TieKeyboardMapping_Remove(TieKeyboardBindings* profile, size_t index);
void TieKeyboardMapping_Install(const TieKeyboardBindings* profile);
void TieKeyboardMapping_Suspend(void);
void TieKeyboardMapping_Enable(bool enabled, const AeronInputSnapshot* input);
void TieKeyboardMapping_BeginFrame(const AeronInputSnapshot* input);
void TieKeyboardMapping_Event(const AeronKeyEvent* event, bool suppressed);
bool TieKeyboardMapping_HasButtons(void);
uint16_t TieKeyboardMapping_ReadButtons(void);

#endif
