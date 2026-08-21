#include "tie_app/hotkeys.h"

#include "aeron/aeron.h"
#include "tie_app/settings/settings.h"
#include "tie_app/settings/video_options.h"
#include "tie_runtime/input/input.h"
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

static void TieHotkeys_ProcessDebugUi(const AeronInputSnapshot* input) {
	if (!input || !input->key_pressed[AERON_KEY_GRAVE] || !Aeron_DebugUiAvailable())
		return;
	Aeron_DebugUiToggle();
	TieInput_SuppressKey(AERON_KEY_GRAVE);
}

/* Platform-conventional fullscreen chord: Cmd+Ctrl+F on macOS,
 * Alt+Enter on Windows, and Alt+Enter or F11 elsewhere. */
static int TieHotkeys_FullscreenTrigger(const AeronInputSnapshot* input) {
	if (!input)
		return -1;

#if defined(__APPLE__)
	const bool gui = input->key_down[AERON_KEY_LGUI] || input->key_down[AERON_KEY_RGUI];
	const bool ctrl = input->key_down[AERON_KEY_LCTRL] || input->key_down[AERON_KEY_RCTRL];
	const int f_key = AERON_KEY_A + ('f' - 'a');
	return gui && ctrl && input->key_pressed[f_key] ? f_key : -1;
#else
	const bool alt = input->key_down[AERON_KEY_LALT] || input->key_down[AERON_KEY_RALT];
	if (alt && input->key_pressed[AERON_KEY_RETURN])
		return AERON_KEY_RETURN;
	if (alt && input->key_pressed[AERON_KEY_KP_ENTER])
		return AERON_KEY_KP_ENTER;
#if !defined(_WIN32)
	if (input->key_pressed[AERON_KEY_F11])
		return AERON_KEY_F11;
#endif
	return -1;
#endif
}

static void TieHotkeys_ProcessFullscreen(const AeronInputSnapshot* input) {
	const int trigger = TieHotkeys_FullscreenTrigger(input);
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
	if (input && TieSettings_Available()) {
		const bool start_pressed = TieHotkeys_ControllerStartPressed(input);
		const TieSnapshot* snapshot = TieSnapshot_Current();
		const bool start_controls_settings =
			was_open || !snapshot || snapshot->scene_kind != TIE_SCENE_FLIGHT;
		if (start_pressed && start_controls_settings && !TieSettings_CapturesController())
			TieSettings_Toggle();
	}
	return was_open || TieSettings_Open();
}

static void TieHotkeys_ProcessPause(TieHotkeys* hotkeys, const AeronInputSnapshot* input) {
	if (!input)
		return;
	const bool gui = input->key_down[AERON_KEY_LGUI] || input->key_down[AERON_KEY_RGUI];
	const AeronKey pause_key = (AeronKey)(AERON_KEY_A + ('p' - 'a'));
	if (!gui || !input->key_pressed[pause_key])
		return;
	hotkeys->paused = !hotkeys->paused;
	TieInput_SuppressKey(pause_key);
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
	TieHotkeys_ProcessDebugUi(input);
	TieHotkeys_ProcessFullscreen(input);
	frame.menu_open = TieHotkeys_ProcessSettings(input);
	TieHotkeys_ProcessPause(hotkeys, input);
	frame.paused = hotkeys->paused;
	return frame;
}
