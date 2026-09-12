#include "tie_runtime/input/keyboard_mapping.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct KeyboardPress {
	TieInputAction action;
	bool down;
	bool ignored;
} KeyboardPress;

static struct {
	uint16_t actions[AERON_KEY_COUNT][16];
	KeyboardPress pressed[AERON_KEY_COUNT];
	TieKeyboardPlatform platform;
	bool debug_available;
	bool enabled;
	bool has_buttons;
	uint16_t pending;
	uint16_t observed;
} g_keyboard;

void TieKeyboardMapping_SetPolicy(TieKeyboardPlatform platform, bool debug_available) {
	g_keyboard.platform = platform;
	g_keyboard.debug_available = debug_available;
}

TieKeyboardShortcut TieKeyboardMapping_Shortcut(AeronKeyChord source) {
	const int key = source.key;
	const uint8_t mod = source.modifiers;
	if (key == AERON_KEY_ESCAPE)
		return TIE_KEYBOARD_SHORTCUT_SETTINGS;
	if (key == AERON_KEY_GRAVE && g_keyboard.debug_available)
		return TIE_KEYBOARD_SHORTCUT_DEBUG;
	if (g_keyboard.platform == TIE_KEYBOARD_PLATFORM_MACOS) {
		if (key == AERON_KEY_A + ('f' - 'a') &&
			(mod & (AERON_KEY_MOD_GUI | AERON_KEY_MOD_CTRL)) == (AERON_KEY_MOD_GUI | AERON_KEY_MOD_CTRL))
			return TIE_KEYBOARD_SHORTCUT_FULLSCREEN;
	} else if (((key == AERON_KEY_RETURN || key == AERON_KEY_KP_ENTER) && (mod & AERON_KEY_MOD_ALT)) ||
			   (g_keyboard.platform == TIE_KEYBOARD_PLATFORM_OTHER && key == AERON_KEY_F11)) {
		return TIE_KEYBOARD_SHORTCUT_FULLSCREEN;
	}
	if (key == AERON_KEY_A + ('p' - 'a') && (mod & AERON_KEY_MOD_GUI))
		return TIE_KEYBOARD_SHORTCUT_PAUSE;
	if (key == AERON_KEY_A + ('m' - 'a') &&
		(mod & (AERON_KEY_MOD_CTRL | AERON_KEY_MOD_ALT)) == (AERON_KEY_MOD_CTRL | AERON_KEY_MOD_ALT))
		return TIE_KEYBOARD_SHORTCUT_MOUSE;
	return TIE_KEYBOARD_SHORTCUT_NONE;
}

bool TieKeyboardMapping_SourceValid(AeronKeyChord source) {
	return source.key > 0 && source.key < AERON_KEY_COUNT && source.modifiers < 16 &&
		   AeronKey_Name((AeronKey)source.key)[0] &&
		   (!AeronKey_Modifier((AeronKey)source.key) || source.modifiers == 0) &&
		   TieKeyboardMapping_Shortcut(source) == TIE_KEYBOARD_SHORTCUT_NONE;
}

void TieKeyboardMapping_FormatSource(char* text, size_t capacity, AeronKeyChord source) {
	snprintf(text, capacity, "%s%s%s%s%s", (source.modifiers & AERON_KEY_MOD_CTRL) ? "Ctrl+" : "",
			 (source.modifiers & AERON_KEY_MOD_ALT) ? "Alt+" : "",
			 (source.modifiers & AERON_KEY_MOD_SHIFT) ? "Shift+" : "",
			 (source.modifiers & AERON_KEY_MOD_GUI)
				 ? (g_keyboard.platform == TIE_KEYBOARD_PLATFORM_MACOS ? "Command+" : "Super+")
				 : "",
			 AeronKey_Name((AeronKey)source.key));
}

size_t TieKeyboardMapping_Find(const TieKeyboardBindings* profile, AeronKeyChord source) {
	for (size_t i = 0; i < profile->count; ++i)
		if (profile->bindings[i].source.key == source.key &&
			profile->bindings[i].source.modifiers == source.modifiers)
			return i;
	return SIZE_MAX;
}

static int BindingCompare(const void* left, const void* right) {
	const TieKeyboardBinding* a = left;
	const TieKeyboardBinding* b = right;
	if (a->action != b->action)
		return (int)a->action - (int)b->action;
	if (a->source.key != b->source.key)
		return (int)a->source.key - (int)b->source.key;
	return (int)a->source.modifiers - (int)b->source.modifiers;
}

void TieKeyboardMapping_Sort(TieKeyboardBindings* profile) {
	qsort(profile->bindings, profile->count, sizeof profile->bindings[0], BindingCompare);
}

bool TieKeyboardMapping_Equal(const TieKeyboardBindings* a, const TieKeyboardBindings* b) {
	if (a->count != b->count)
		return false;
	for (size_t i = 0; i < a->count; ++i)
		if (BindingCompare(&a->bindings[i], &b->bindings[i]))
			return false;
	return true;
}

void TieKeyboardMapping_Remove(TieKeyboardBindings* profile, size_t index) {
	if (index >= profile->count)
		return;
	memmove(&profile->bindings[index], &profile->bindings[index + 1],
			(profile->count - index - 1) * sizeof profile->bindings[0]);
	--profile->count;
}

void TieKeyboardMapping_Suspend(void) {
	for (int key = 0; key < AERON_KEY_COUNT; ++key) {
		KeyboardPress* press = &g_keyboard.pressed[key];
		if (press->down && press->action != TIE_INPUT_ACTION_NONE)
			TieInputActions_DispatchKeyboard(press->action, false, false);
	}
	memset(g_keyboard.pressed, 0, sizeof g_keyboard.pressed);
	g_keyboard.pending = g_keyboard.observed = 0;
	g_keyboard.enabled = false;
}

static void Compile(uint16_t table[AERON_KEY_COUNT][16], const TieKeyboardBindings* profile) {
	memset(table, 0, sizeof g_keyboard.actions);
	for (size_t i = 0; i < profile->count; ++i) {
		const TieKeyboardBinding* b = &profile->bindings[i];
		table[b->source.key][b->source.modifiers] = (uint16_t)b->action;
	}
}

void TieKeyboardMapping_Install(const TieKeyboardBindings* profile) {
	TieKeyboardMapping_Suspend();
	Compile(g_keyboard.actions, profile);
	g_keyboard.has_buttons = false;
	for (size_t i = 0; i < profile->count; ++i)
		g_keyboard.has_buttons |= TieInputActions_ButtonBit(profile->bindings[i].action) != 0;
}

void TieKeyboardMapping_Enable(bool enabled, const AeronInputSnapshot* input) {
	if (enabled == g_keyboard.enabled)
		return;
	TieKeyboardMapping_Suspend();
	g_keyboard.enabled = enabled;
	if (enabled && input)
		for (int key = 0; key < AERON_KEY_COUNT; ++key)
			g_keyboard.pressed[key].ignored = input->key_down[key] != 0;
}

void TieKeyboardMapping_BeginFrame(const AeronInputSnapshot* input) {
	g_keyboard.pending &= (uint16_t)~g_keyboard.observed;
	g_keyboard.observed = 0;
	if (input->key_events_overflow && g_keyboard.enabled) {
		TieKeyboardMapping_Suspend();
		TieKeyboardMapping_Enable(true, input);
	}
}

void TieKeyboardMapping_Event(const AeronKeyEvent* event, bool suppressed) {
	if (!g_keyboard.enabled || event->chord.key >= AERON_KEY_COUNT)
		return;
	KeyboardPress* press = &g_keyboard.pressed[event->chord.key];
	if (!event->down) {
		if (press->down && press->action != TIE_INPUT_ACTION_NONE)
			TieInputActions_DispatchKeyboard(press->action, false, false);
		*press = (KeyboardPress) { 0 };
		return;
	}
	if (press->ignored)
		return;
	if (suppressed) {
		if (press->down && press->action != TIE_INPUT_ACTION_NONE)
			TieInputActions_DispatchKeyboard(press->action, false, false);
		*press = (KeyboardPress) { .ignored = true };
		return;
	}
	if (!press->down) {
		if (event->repeat)
			return; /* Resuming never turns typematic into a fresh press. */
		AeronKeyChord chord = event->chord;
		chord.modifiers &= (uint8_t)~AeronKey_Modifier((AeronKey)chord.key);
		press->down = true;
		if (TieKeyboardMapping_Shortcut(event->chord) != TIE_KEYBOARD_SHORTCUT_NONE)
			return;
		press->action = (TieInputAction)g_keyboard.actions[chord.key][chord.modifiers];
		if (press->action != TIE_INPUT_ACTION_NONE)
			g_keyboard.pending |= TieInputActions_ButtonBit(press->action);
	} else if (!event->repeat) {
		return;
	}
	if (press->action != TIE_INPUT_ACTION_NONE)
		TieInputActions_DispatchKeyboard(press->action, true, event->repeat != 0);
}

bool TieKeyboardMapping_HasButtons(void) { return g_keyboard.enabled && g_keyboard.has_buttons; }

uint16_t TieKeyboardMapping_ReadButtons(void) {
	g_keyboard.observed |= g_keyboard.pending;
	return TieInputActions_VirtualButtons | g_keyboard.pending;
}
