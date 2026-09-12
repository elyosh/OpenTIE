#include "tie_app/hotkeys.h"

#include "aeron/aeron.h"
#include "tie_app/settings/settings.h"
#include "tie_app/settings/video_options.h"
#include "tie_runtime/input/input.h"
#include "tie_runtime/input/keyboard_mapping.h"
#include "tie_runtime/snapshot/snapshot.h"

static void TieHotkeys_ReconcileFullscreen(TieHotkeys* hotkeys) {
	const int fullscreen = Aeron_Fullscreen();
	if (fullscreen == hotkeys->last_fullscreen)
		return;

	TieAppVideoConfig options;
	char error[256];
	TieVideoOptions_Get(&options);
	options.fullscreen = fullscreen != 0;
	if (!TieVideoOptions_Set(&options, error, sizeof error))
		Aeron_LogWarn("tie", "could not reconcile fullscreen mode: %s", error);
	hotkeys->last_fullscreen = fullscreen;
}

static int TieHotkeys_Trigger(const AeronInputSnapshot* input, TieKeyboardShortcut shortcut) {
	if (!input || !input->has_focus || input->key_events_overflow)
		return -1;
	for (uint16_t i = 0; i < input->key_event_count; ++i) {
		const AeronKeyEvent* event = &input->key_events[i];
		if (event->down && !event->repeat && TieKeyboardMapping_Shortcut(event->chord) == shortcut)
			return event->chord.key;
	}
	return -1;
}

static void TieHotkeys_ProcessDebugUi(const AeronInputSnapshot* input) {
	const int trigger = TieHotkeys_Trigger(input, TIE_KEYBOARD_SHORTCUT_DEBUG);
	if (trigger < 0)
		return;
	Aeron_DebugUiToggle();
	TieInput_SuppressKey(trigger);
}

static void TieHotkeys_ProcessFullscreen(const AeronInputSnapshot* input) {
	const int trigger = TieHotkeys_Trigger(input, TIE_KEYBOARD_SHORTCUT_FULLSCREEN);
	if (trigger < 0)
		return;

	TieAppVideoConfig options;
	char error[256];
	TieVideoOptions_Get(&options);
	options.fullscreen = !Aeron_Fullscreen();
	if (!TieVideoOptions_Set(&options, error, sizeof error))
		Aeron_LogWarn("tie", "could not toggle fullscreen mode: %s", error);
	TieInput_SuppressKey(trigger);
}

static bool TieHotkeys_ControllerStartPressed(const AeronInputSnapshot* input) {
	if (!input)
		return false;
	for (int pad = 0; pad < AERON_CONTROLLER_MAX; ++pad) {
		if (input->controllers[pad].connected &&
			(input->controllers[pad].gamepad_pressed_buttons & (1u << AERON_GAMEPAD_BUTTON_START)))
			return true;
	}
	return false;
}

static bool TieHotkeys_ProcessSettings(const AeronInputSnapshot* input) {
	const bool was_open = TieSettings_Open();
	if (input && input->has_focus && TieSettings_Available()) {
		const TieSnapshot* snapshot = TieSnapshot_Current();
		const int escape = TieHotkeys_Trigger(input, TIE_KEYBOARD_SHORTCUT_SETTINGS);
		/* Outside flight, the frontend owns Escape through the raw key queue. */
		if (escape >= 0 && !was_open && snapshot && snapshot->scene_kind == TIE_SCENE_FLIGHT) {
			TieInput_SuppressKey(escape);
			TieSettings_Show();
			return true;
		}
		const bool start_pressed = TieHotkeys_ControllerStartPressed(input);
		const bool start_controls_settings =
			was_open || !snapshot || snapshot->scene_kind != TIE_SCENE_FLIGHT;
		if (start_pressed && start_controls_settings && !TieSettings_CapturesController() &&
			!TieSettings_CapturesKeyboard())
			TieSettings_Toggle();
	}
	return was_open || TieSettings_Open();
}

static void TieHotkeys_ProcessPause(TieHotkeys* hotkeys, const AeronInputSnapshot* input) {
	const int trigger = TieHotkeys_Trigger(input, TIE_KEYBOARD_SHORTCUT_PAUSE);
	if (trigger < 0)
		return;
	hotkeys->paused = !hotkeys->paused;
	TieInput_ResetThrottle();
	TieInput_SuppressKey(trigger);
}

void TieHotkeys_Init(TieHotkeys* hotkeys) {
	if (!hotkeys)
		return;
	hotkeys->last_fullscreen = Aeron_Fullscreen();
	hotkeys->paused = false;
}

TieHotkeysFrame TieHotkeys_Process(TieHotkeys* hotkeys, const AeronInputSnapshot* input) {
	TieHotkeysFrame frame = { 0 };
	if (!hotkeys)
		return frame;

	TieHotkeys_ReconcileFullscreen(hotkeys);
	if (!TieSettings_CapturesKeyboard()) {
		TieHotkeys_ProcessDebugUi(input);
		TieHotkeys_ProcessFullscreen(input);
	}
	const bool was_open = TieSettings_Open();
	frame.menu_open = TieHotkeys_ProcessSettings(input);
	frame.settings_opened = !was_open && TieSettings_Open();
	if (!TieSettings_CapturesKeyboard())
		TieHotkeys_ProcessPause(hotkeys, input);
	frame.paused = hotkeys->paused;
	return frame;
}
