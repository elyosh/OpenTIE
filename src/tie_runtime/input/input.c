#include "tie_runtime/input/input.h"
#include "tie_runtime/display/classic_framebuffer.h"

#include "tie_runtime/display/classic_display.h"
#include "tie_runtime/input/actions.h"
#include "tie_runtime/input/controller_mapping.h"
#include "tie_runtime/snapshot/snapshot.h"

#include "aeron/aeron.h"

#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

enum { TIE_FLIGHT_MOUSE_REFERENCE_INTERVAL_US = 16000 };

/* ================================================================
 * Keyboard — DOS scancode queue fed from AeronInputSnapshot
 * ================================================================ */

#define KEY_QUEUE_SIZE 64
static int16_t key_queue[KEY_QUEUE_SIZE];
static int key_queue_head;
static int key_queue_tail;

static TieInputMapping s_input_mapping = {
	.axes = {
		[TIE_INPUT_AXIS_YAW] = {2, false, 0.0f},
		[TIE_INPUT_AXIS_PITCH] = {3, false, 0.0f},
		[TIE_INPUT_AXIS_ROLL] = {0, false, 0.0f},
		[TIE_INPUT_AXIS_THROTTLE_RATE] = {-1, false, 0.0f},
	},
};

void TieInput_SetMapping(const TieInputMapping* mapping) {
	static const TieInputMapping default_mapping = {
		.axes = {
			[TIE_INPUT_AXIS_YAW] = {2, false, 0.0f},
			[TIE_INPUT_AXIS_PITCH] = {3, false, 0.0f},
			[TIE_INPUT_AXIS_ROLL] = {0, false, 0.0f},
			[TIE_INPUT_AXIS_THROTTLE_RATE] = {-1, false, 0.0f},
		},
	};
	s_input_mapping = mapping ? *mapping : default_mapping;
}

const TieInputMapping* TieInput_Mapping(void) { return &s_input_mapping; }

int16_t TieInput_MapAxis(const int16_t* raw, int count, TieInputAxisBinding binding) {
	if (!raw || binding.source < 0 || binding.source >= count)
		return 0;
	int value = raw[binding.source];
	if (binding.invert)
		value = -value;
	const int cutoff = (int)(binding.deadzone * 127.0f + 0.5f);
	return value >= -cutoff && value <= cutoff ? 0 : (int16_t)value;
}

static void TieInput_EnqueueByte(int16_t key) {
	int next = (key_queue_tail + 1) % KEY_QUEUE_SIZE;
	if (next != key_queue_head) {
		key_queue[key_queue_tail] = key;
		key_queue_tail = next;
	}
}

static int TieInput_FreeQueueSlots(void) {
	return (key_queue_head - key_queue_tail - 1 + KEY_QUEUE_SIZE) % KEY_QUEUE_SIZE;
}

static void TieInput_EnqueueDosKey(int16_t key) {
	const uint16_t packed = (uint16_t)key;
	if (packed & 0xFF00u) {
		/* DOS getch() returns extended keys as two reads: zero, then scan.
		 * Keep the pair atomic so queue pressure cannot leave an orphan prefix. */
		if (TieInput_FreeQueueSlots() < 2)
			return;
		TieInput_EnqueueByte(0);
		TieInput_EnqueueByte((int16_t)(packed >> 8));
		return;
	}
	TieInput_EnqueueByte(key);
}

void TieInput_EnqueueKey(int16_t key) { TieInput_EnqueueByte(key); }

static int16_t TieInput_DequeueKey(void) {
	if (key_queue_head == key_queue_tail)
		return 0;
	int16_t key = key_queue[key_queue_head];
	key_queue_head = (key_queue_head + 1) % KEY_QUEUE_SIZE;
	return key;
}

/* Translate an Aeron key (SDL scancode value) to a packed DOS getch() value.
 * TieInput_EnqueueDosKey expands extended keys into the two reads expected by TIE. */
static int16_t TieInput_TranslateAltAeronKey(int sc) {
	switch (sc) {
		case AERON_KEY_A + ('e' - 'a'):
			return 0x1200;
		case AERON_KEY_A + ('t' - 'a'):
			return 0x1400;
		case AERON_KEY_A + ('v' - 'a'):
			return 0x2F00;
		default:
			return 0;
	}
}

static int16_t TieInput_TranslateAeronKey(int sc, int shift) {
	if (sc >= AERON_KEY_F1 && sc <= AERON_KEY_F1 + 9) {
		const int scan = (shift ? 0x54 : 0x3B) + (sc - AERON_KEY_F1);
		return (int16_t)(scan << 8);
	}
	switch (sc) {
		case AERON_KEY_UP:
			return 0x4800;
		case AERON_KEY_DOWN:
			return 0x5000;
		case AERON_KEY_LEFT:
			return 0x4B00;
		case AERON_KEY_RIGHT:
			return 0x4D00;
		case AERON_KEY_HOME:
			return 0x4700;
		case AERON_KEY_END:
			return 0x4F00;
		case AERON_KEY_PAGEUP:
			return 0x4900;
		case AERON_KEY_PAGEDOWN:
			return 0x5100;
		case AERON_KEY_INSERT:
			return 0x5200;
		case AERON_KEY_DELETE:
			return 0x5300;
		case AERON_KEY_ESCAPE:
			return 27;
		case AERON_KEY_RETURN:
			return 13;
		case AERON_KEY_BACKSPACE:
			return 8;
		case AERON_KEY_TAB:
			return 9;
		default:
			break;
	}
	return 0;
}

int TieInput_KeyPending(void) { return key_queue_head != key_queue_tail; }

int TieInput_ReadKey(void) { return TieInput_DequeueKey(); }

int TieInput_ModifierKeys(void) {
	const AeronInputSnapshot* in = Aeron_InputSnapshot();
	int result = 0;
	if (!in)
		return 0;
	if (in->key_down[AERON_KEY_LSHIFT] || in->key_down[AERON_KEY_RSHIFT])
		result |= 0x03;
	if (in->key_down[AERON_KEY_LCTRL] || in->key_down[AERON_KEY_RCTRL])
		result |= 0x04;
	if (in->key_down[AERON_KEY_LALT] || in->key_down[AERON_KEY_RALT])
		result |= 0x08;
	return result;
}

/* ================================================================
 * Mouse — canonical cursor in classic-framebuffer coordinates.
 *
 * Frontend and pointer-driven flight screens map the absolute OS pointer
 * into the classic surface, then adapt it to Landru's delta interface.
 * Captured flight consumes relative motion with sub-pixel scaling.
 * ================================================================ */

static float cursor_fb_x = 160.0f, cursor_fb_y = 100.0f;
static int fb_w = 320, fb_h = 200;
static int16_t mouse_dx_acc, mouse_dy_acc;
static int cursor_visible_for_engine = 1;
static int pillarbox_cursor_active;
static int frames_since_mouse_motion;
/* Placement of the classic FB layer inside the Aeron logical space
 * (TieInput_SetClassicLayout) — drives the pillarbox clamp
 * bounds in FB units. */
static int layout_log_w = 1920, layout_log_h = 1080;
static int layout_classic_x = 240, layout_classic_y;
static int layout_classic_w = 1440, layout_classic_h = 1080;
static float relative_motion_x, relative_motion_y;
static float relative_drain_fraction_x, relative_drain_fraction_y;
static uint64_t relative_motion_interval_us;
static int capture_active;
static int absolute_cursor_active;
static int skip_absolute_frame;
static int engine_cursor_valid;
static int16_t engine_cursor_x, engine_cursor_y;
static int manual_release;
static int eat_mouse_buttons;
static int system_cursor_visible = -1;
/* DOS-order press edges retained until the engine samples them. */
static int16_t pending_mouse_presses;
static int16_t observed_mouse_presses;

static float TieInput_Clamp(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

static bool TieInput_WarpHostCursorToFramebufferCell(int16_t x, int16_t y) {
	if (capture_active || fb_w <= 0 || fb_h <= 0 || layout_classic_w <= 0 || layout_classic_h <= 0)
		return false;

	/* Target the cell center so the next inverse mapping resolves to the
	 * same integer framebuffer coordinate despite presentation scaling. */
	const int logical_x = layout_classic_x + (int)(((int64_t)x * 2 + 1) * layout_classic_w / (2 * fb_w));
	const int logical_y = layout_classic_y + (int)(((int64_t)y * 2 + 1) * layout_classic_h / (2 * fb_h));
	if (!Aeron_WarpMouseLogical(logical_x, logical_y))
		return false;

	/* Match the track to the position that the absolute mapper will observe
	 * next frame, including the sub-cell offset introduced by scaling. */
	cursor_fb_x = (logical_x - (float)layout_classic_x) * (float)fb_w / (float)layout_classic_w;
	cursor_fb_y = (logical_y - (float)layout_classic_y) * (float)fb_h / (float)layout_classic_h;
	return true;
}

void TieInput_GetMousePosition(int16_t* buttons, int16_t* x, int16_t* y) {
	const AeronInputSnapshot* in = Aeron_InputSnapshot();
	int16_t btn = 0;

	if (in && !eat_mouse_buttons) {
		/* input bridge order: bit 0 = left, bit 1 = right, bit 2 = middle. */
		if (in->mouse.buttons & AERON_MOUSE_BUTTON_LEFT)
			btn |= 1;
		if (in->mouse.buttons & AERON_MOUSE_BUTTON_RIGHT)
			btn |= 2;
		if (in->mouse.buttons & AERON_MOUSE_BUTTON_MIDDLE)
			btn |= 4;
		/* A complete click can arrive between host samples and leave the
		 * level state released. Expose its press until an engine read sees it. */
		btn |= pending_mouse_presses;
	}

	if (buttons) {
		*buttons = btn;
		observed_mouse_presses |= pending_mouse_presses;
	}
	if (x)
		*x = (int16_t)TieInput_Clamp(cursor_fb_x, 0.0f, (float)(fb_w - 1));
	if (y)
		*y = (int16_t)TieInput_Clamp(cursor_fb_y, 0.0f, (float)(fb_h - 1));
}

void TieInput_GetMouseMovement(int16_t* dx, int16_t* dy) {
	if (capture_active && relative_motion_interval_us) {
		/* Preserve the existing TIE95 four-PIT-tick mouse tuning while
		 * consuming motion gathered across an arbitrary host interval. */
		const float scale =
			(float)TIE_FLIGHT_MOUSE_REFERENCE_INTERVAL_US / (float)relative_motion_interval_us;
		const float scaled_x = relative_motion_x * scale + relative_drain_fraction_x;
		const float scaled_y = relative_motion_y * scale + relative_drain_fraction_y;
		const int32_t value_x = (int32_t)scaled_x;
		const int32_t value_y = (int32_t)scaled_y;
		relative_drain_fraction_x = scaled_x - (float)value_x;
		relative_drain_fraction_y = scaled_y - (float)value_y;
		if (dx)
			*dx = (int16_t)(value_x < INT16_MIN ? INT16_MIN : value_x > INT16_MAX ? INT16_MAX : value_x);
		if (dy)
			*dy = (int16_t)(value_y < INT16_MIN ? INT16_MIN : value_y > INT16_MAX ? INT16_MAX : value_y);
	} else {
		if (dx)
			*dx = mouse_dx_acc;
		if (dy)
			*dy = mouse_dy_acc;
	}
	mouse_dx_acc = 0;
	mouse_dy_acc = 0;
	relative_motion_x = 0.0f;
	relative_motion_y = 0.0f;
	relative_motion_interval_us = 0;
}

void TieInput_SetMousePosition(int16_t x, int16_t y) {
	/* Surface switches update Landru's active dimensions before repositioning
	 * the cursor, so synchronize the host extent before interpreting x/y. */
	const TieFramebuffer* framebuffer = TieClassicFramebuffer_Current();
	if (framebuffer && framebuffer->width > 0 && framebuffer->height > 0)
		TieInput_SetFramebufferSize(framebuffer->width, framebuffer->height);

	const int16_t clamped_x = (int16_t)TieInput_Clamp((float)x, 0.0f, (float)(fb_w - 1));
	const int16_t clamped_y = (int16_t)TieInput_Clamp((float)y, 0.0f, (float)(fb_h - 1));
	cursor_fb_x = (float)clamped_x;
	cursor_fb_y = (float)clamped_y;
	engine_cursor_x = clamped_x;
	engine_cursor_y = clamped_y;
	engine_cursor_valid = 1;
	frames_since_mouse_motion = 0;
	mouse_dx_acc = 0;
	mouse_dy_acc = 0;

	(void)TieInput_WarpHostCursorToFramebufferCell(clamped_x, clamped_y);
}

void TieInput_ShowCursor(bool show) {
	/* Engine-cursor visibility is also an input-capture boundary in flight. */
	cursor_visible_for_engine = show ? 1 : 0;
}

static bool TieInput_MapAbsoluteCursor(const AeronInputSnapshot* in, float* out_x, float* out_y) {
	if (!in || !out_x || !out_y || !in->has_focus || !in->mouse.inside_content || layout_classic_w <= 0 ||
		layout_classic_h <= 0)
		return false;
	*out_x = (in->mouse.x - (float)layout_classic_x) * (float)fb_w / (float)layout_classic_w;
	*out_y = (in->mouse.y - (float)layout_classic_y) * (float)fb_h / (float)layout_classic_h;
	return true;
}

static bool TieInput_FlightScreenUsesRelativeInput(TieFlightScreen screen) {
	switch (screen) {
		case TIE_FLIGHT_SCREEN_NORMAL:
		case TIE_FLIGHT_SCREEN_MAP:
		case TIE_FLIGHT_SCREEN_REPLAY_VIEWER:
			return true;
		default:
			return false;
	}
}

void TieInput_UpdateCapture(const TieSnapshot* snapshot, bool settings_open) {
	const AeronInputSnapshot* in = Aeron_InputSnapshot();
	eat_mouse_buttons = 0;
	if (!in)
		return;

	const bool relative_input_screen =
		snapshot && snapshot->scene_kind == TIE_SCENE_FLIGHT &&
		TieInput_FlightScreenUsesRelativeInput((TieFlightScreen)snapshot->flight_screen);
	if (snapshot) {
		engine_cursor_x = snapshot->cursor.x;
		engine_cursor_y = snapshot->cursor.y;
		engine_cursor_valid = 1;
	}
	const bool ctrl = in->key_down[AERON_KEY_LCTRL] || in->key_down[AERON_KEY_RCTRL];
	const bool alt = in->key_down[AERON_KEY_LALT] || in->key_down[AERON_KEY_RALT];
	const int release_key = AERON_KEY_A + ('m' - 'a');
	if (relative_input_screen && ctrl && alt && in->key_pressed[release_key]) {
		manual_release = !manual_release;
		TieInput_SuppressKey(release_key);
	}
	if (!relative_input_screen)
		manual_release = 0;
	if (relative_input_screen && manual_release && in->has_focus && in->mouse.inside_content &&
		in->mouse.pressed_buttons) {
		manual_release = 0;
		eat_mouse_buttons = 1;
	}

	const int desired = relative_input_screen && !cursor_visible_for_engine && in->has_focus &&
						!settings_open && !manual_release;
	absolute_cursor_active =
		!desired && (!relative_input_screen || cursor_visible_for_engine) && !settings_open;
	if (desired == capture_active)
		return;

	const int was_captured = capture_active;
	if (!Aeron_SetRelativeMouseMode(desired))
		return;

	/* Aeron hides the host cursor after every relative-mode transition. */
	system_cursor_visible = 0;
	capture_active = desired;
	relative_motion_x = 0.0f;
	relative_motion_y = 0.0f;
	relative_drain_fraction_x = 0.0f;
	relative_drain_fraction_y = 0.0f;
	relative_motion_interval_us = 0;
	mouse_dx_acc = 0;
	mouse_dy_acc = 0;
	/* The snapshot that requests release was sampled while SDL was still
	 * captured, so its absolute coordinates are not current until the next
	 * host frame. */
	skip_absolute_frame = was_captured && !capture_active;
}

void TieInput_SyncSystemCursor(bool settings_open) {
	const int desired = settings_open || manual_release;
	if (desired == system_cursor_visible)
		return;
	if (Aeron_SetHostCursorVisible(desired))
		system_cursor_visible = desired;
}

void TieInput_SetClassicLayout(const AeronRectI* frame, const AeronRectI* classic) {
	if (!frame || !classic || frame->width <= 0 || frame->height <= 0 || classic->width <= 0 ||
		classic->height <= 0)
		return;
	layout_log_w = frame->width;
	layout_log_h = frame->height;
	layout_classic_x = classic->x;
	layout_classic_y = classic->y;
	layout_classic_w = classic->width;
	layout_classic_h = classic->height;
}

/* Per-frame cursor update from the application, after TieRuntime_Tick. Mirrors the
 * sdl3 application's platform_update_cursor:
 *   pillarbox_active — HD overlay visible (alpha > 0); lets the track
 *     extend into the modern viewport. Going active→inactive while in
 *     the pillarbox snaps back to the FB edge so engine and track
 *     agree.
 *   engine_x/y — snap.cursor pose; re-anchors the track when idle
 *     (absorbs the initial seed mismatch and engine-internal warps).
 *     Skipped during active motion (the engine pose is rate-limited
 *     to ~31 Hz and would cap the HD cursor) and while in the
 *     pillarbox (the engine pegs at mouse_limits there). */
void TieInput_UpdateCursor(bool pillarbox_active, int16_t engine_x, int16_t engine_y) {
	pillarbox_cursor_active = pillarbox_active ? 1 : 0;
	engine_cursor_x = engine_x;
	engine_cursor_y = engine_y;
	engine_cursor_valid = 1;
	if (fb_w <= 0 || fb_h <= 0)
		return;
	float fb_w_max = (float)(fb_w - 1);
	float fb_h_max = (float)(fb_h - 1);
	if (!pillarbox_active) {
		cursor_fb_x = TieInput_Clamp(cursor_fb_x, 0.0f, fb_w_max);
		cursor_fb_y = TieInput_Clamp(cursor_fb_y, 0.0f, fb_h_max);
	}
	if (frames_since_mouse_motion < 3)
		++frames_since_mouse_motion;
	if (frames_since_mouse_motion < 3)
		return;
	bool reanchored = false;
	if (cursor_fb_x >= 0.0f && cursor_fb_x <= fb_w_max && (int16_t)cursor_fb_x != engine_x) {
		cursor_fb_x = (float)engine_x;
		reanchored = true;
	}
	if (cursor_fb_y >= 0.0f && cursor_fb_y <= fb_h_max && (int16_t)cursor_fb_y != engine_y) {
		cursor_fb_y = (float)engine_y;
		reanchored = true;
	}

	/* The absolute host pointer is the next frame's mouse target. Keep it in
	 * sync when Landru moved the cursor through a controller or an internal
	 * path, otherwise that stale target immediately undoes the engine move. */
	if (reanchored && absolute_cursor_active && cursor_fb_x >= 0.0f && cursor_fb_x <= fb_w_max &&
		cursor_fb_y >= 0.0f && cursor_fb_y <= fb_h_max) {
		mouse_dx_acc = 0;
		mouse_dy_acc = 0;
		(void)TieInput_WarpHostCursorToFramebufferCell(engine_x, engine_y);
	}
}

void TieInput_CursorFramebufferPosition(float* x, float* y) {
	if (x)
		*x = cursor_fb_x;
	if (y)
		*y = cursor_fb_y;
}

/* ================================================================
 * Joystick — Aeron gamepad snapshot, canonical axis order
 * ================================================================ */

int TieInput_JoystickPresent(void) { return TieControllerMapping_Present(); }

void TieInput_JoystickShutdown(void) {}

void TieInput_ReadJoystick(int port, int16_t* axes, int n_axes, uint16_t* out_buttons) {
	if (port == 0)
		TieControllerMapping_Read(axes, n_axes, out_buttons);
	else {
		if (axes && n_axes > 0)
			memset(axes, 0, (size_t)n_axes * sizeof *axes);
		if (out_buttons)
			*out_buttons = 0;
	}
}

/* ================================================================
 * Frame pump + lifecycle
 * ================================================================ */

void TieInput_SetFramebufferSize(int w, int h) {
	if (w <= 0 || h <= 0 || (w == fb_w && h == fb_h))
		return;
	if (fb_w > 0 && fb_h > 0) {
		cursor_fb_x = cursor_fb_x * (float)w / (float)fb_w;
		cursor_fb_y = cursor_fb_y * (float)h / (float)fb_h;
	}
	fb_w = w;
	fb_h = h;
	relative_drain_fraction_x = 0.0f;
	relative_drain_fraction_y = 0.0f;
}

/* Application-consumed keys skipped by the next pump (Tab view-mode cycle). */
static uint8_t suppressed_keys[AERON_KEY_COUNT];

void TieInput_SuppressKey(int aeron_key) {
	if (aeron_key >= 0 && aeron_key < AERON_KEY_COUNT)
		suppressed_keys[aeron_key] = 1;
}

void TieInput_BeginFrame(int32_t delta_us) {
	const AeronInputSnapshot* in = Aeron_InputSnapshot();
	int sc;

	if (!in) {
		memset(suppressed_keys, 0, sizeof suppressed_keys);
		pending_mouse_presses = 0;
		observed_mouse_presses = 0;
		return;
	}

	/* Retire presses sampled during an earlier host frame, then retain new
	 * edges until Landru or the flight input path reads the button state. */
	pending_mouse_presses &= (int16_t)~observed_mouse_presses;
	observed_mouse_presses = 0;
	if (eat_mouse_buttons) {
		/* The press that restored flight capture must not become a shot. */
		pending_mouse_presses = 0;
	} else if (in->has_focus) {
		if (in->mouse.pressed_buttons & AERON_MOUSE_BUTTON_LEFT)
			pending_mouse_presses |= 1;
		if (in->mouse.pressed_buttons & AERON_MOUSE_BUTTON_RIGHT)
			pending_mouse_presses |= 2;
		if (in->mouse.pressed_buttons & AERON_MOUSE_BUTTON_MIDDLE)
			pending_mouse_presses |= 4;
	} else {
		pending_mouse_presses = 0;
	}
	if (capture_active) {
		if (!in->has_focus || delta_us <= 0 || delta_us > 250000) {
			relative_motion_x = 0.0f;
			relative_motion_y = 0.0f;
			relative_drain_fraction_x = 0.0f;
			relative_drain_fraction_y = 0.0f;
			relative_motion_interval_us = 0;
		} else {
			relative_motion_interval_us += (uint32_t)delta_us;
		}
	}

	TieControllerMapping_Update(in);

	/* Keyboard: key_typed counts include OS typematic repeats, matching the
	 * per-SDL-event enqueue of the sdl3 application (DOS BIOS repeat behavior).
	 * A key bound in the action layer REPLACES the default DOS key — the
	 * dispatch consumes it. Releases also dispatch so held BUTTON_BIT
	 * actions clear. */
	const int shift = in->key_down[AERON_KEY_LSHIFT] || in->key_down[AERON_KEY_RSHIFT];
	int suppress_alt_text = 0;
	for (sc = 0; sc < AERON_KEY_COUNT; ++sc) {
		if (in->key_released[sc])
			(void)TieInputActions_DispatchKeyboard(sc, false);
		int n = suppressed_keys[sc] ? 0 : in->key_typed[sc];
		int alt_n = suppressed_keys[sc] ? 0 : in->key_alt_typed[sc];
		if (alt_n > n)
			alt_n = n;
		if (alt_n)
			suppress_alt_text = 1;
		if (n) {
			const bool action_consumed = TieInputActions_DispatchKeyboard(sc, true);
			if (action_consumed)
				continue;
			int16_t key = TieInput_TranslateAltAeronKey(sc);
			for (int repeat = 0; key && repeat < alt_n; ++repeat)
				TieInput_EnqueueDosKey(key);
			n -= alt_n;
			key = TieInput_TranslateAeronKey(sc, shift);
			if (key) {
				while (n--)
					TieInput_EnqueueDosKey(key);
			}
		}
	}
	if (in->has_focus && !suppress_alt_text) {
		for (uint32_t index = 0; index < in->text_length; ++index) {
			const uint32_t codepoint = in->text[index];
			if (codepoint >= 0x20u && codepoint <= 0x7Eu)
				TieInput_EnqueueByte((int16_t)codepoint);
		}
	}
	memset(suppressed_keys, 0, sizeof suppressed_keys);

	/* Mouse: captured flight consumes relative motion. Released frontend
	 * input follows the absolute OS pointer and converts that position into
	 * the delta-based interface expected by Landru. */
	float rel_fx = in->mouse.relative_x;
	float rel_fy = in->mouse.relative_y;
	if (capture_active && (rel_fx != 0.0f || rel_fy != 0.0f)) {
		frames_since_mouse_motion = 0;

		float old_x = cursor_fb_x;
		float old_y = cursor_fb_y;
		float new_x = old_x + rel_fx;
		float new_y = old_y + rel_fy;

		float min_x = 0.0f, max_x = (float)(fb_w - 1);
		float min_y = 0.0f, max_y = (float)(fb_h - 1);
		if (pillarbox_cursor_active && layout_classic_w > 0 && layout_classic_h > 0) {
			/* Clamp range = the full logical viewport projected into FB
			 * units through the classic layer's placement:
			 *   logical_x = classic_x + fb_x * classic_w / fb_w
			 * so the inverse is
			 *   fb_x = (logical_x - classic_x) * fb_w / classic_w.
			 * The classic layer fills the logical height, so Y keeps
			 * the FB range. */
			float fb_per_log_x = (float)fb_w / (float)layout_classic_w;
			float fb_per_log_y = (float)fb_h / (float)layout_classic_h;
			min_x = -(float)layout_classic_x * fb_per_log_x;
			max_x = (float)(layout_log_w - layout_classic_x) * fb_per_log_x - 1.0f;
			min_y = -(float)layout_classic_y * fb_per_log_y;
			max_y = (float)(layout_log_h - layout_classic_y) * fb_per_log_y - 1.0f;
		}
		new_x = TieInput_Clamp(new_x, min_x, max_x);
		new_y = TieInput_Clamp(new_y, min_y, max_y);
		cursor_fb_x = new_x;
		cursor_fb_y = new_y;

		/* Flight path (cursor hidden): pre-scale by TIE_FLIGHT_DELTA_SUBPX
		 * before the integer floor so mouse_dx_acc carries 1/4-pixel
		 * resolution (see the sdl3 application for the slow-pan pop
		 * derivation). */
#define TIE_FLIGHT_DELTA_SUBPX 4
		float engine_dx_f = rel_fx * (float)TIE_FLIGHT_DELTA_SUBPX;
		float engine_dy_f = rel_fy * (float)TIE_FLIGHT_DELTA_SUBPX;
#undef TIE_FLIGHT_DELTA_SUBPX

		relative_motion_x += engine_dx_f;
		relative_motion_y += engine_dy_f;
	} else if (absolute_cursor_active) {
		if (skip_absolute_frame) {
			skip_absolute_frame = 0;
			mouse_dx_acc = 0;
			mouse_dy_acc = 0;
		} else {
			float new_x, new_y;
			if (TieInput_MapAbsoluteCursor(in, &new_x, &new_y)) {
				float min_x = 0.0f, max_x = (float)(fb_w - 1);
				float min_y = 0.0f, max_y = (float)(fb_h - 1);
				if (pillarbox_cursor_active && layout_classic_w > 0 && layout_classic_h > 0) {
					const float fb_per_log_x = (float)fb_w / (float)layout_classic_w;
					const float fb_per_log_y = (float)fb_h / (float)layout_classic_h;
					min_x = -(float)layout_classic_x * fb_per_log_x;
					max_x = (float)(layout_log_w - layout_classic_x) * fb_per_log_x - 1.0f;
					min_y = -(float)layout_classic_y * fb_per_log_y;
					max_y = (float)(layout_log_h - layout_classic_y) * fb_per_log_y - 1.0f;
				}
				new_x = TieInput_Clamp(new_x, min_x, max_x);
				new_y = TieInput_Clamp(new_y, min_y, max_y);
				if (new_x != cursor_fb_x || new_y != cursor_fb_y)
					frames_since_mouse_motion = 0;
				cursor_fb_x = new_x;
				cursor_fb_y = new_y;

				if (engine_cursor_valid) {
					const int16_t target_x = (int16_t)TieInput_Clamp(new_x, 0.0f, (float)(fb_w - 1));
					const int16_t target_y = (int16_t)TieInput_Clamp(new_y, 0.0f, (float)(fb_h - 1));
					/* Replace, rather than accumulate: this is an absolute target and
					 * the latest snapshot is Landru's authoritative current pose. */
					mouse_dx_acc = (int16_t)(target_x - engine_cursor_x);
					mouse_dy_acc = (int16_t)(target_y - engine_cursor_y);
				}
			} else {
				mouse_dx_acc = 0;
				mouse_dy_acc = 0;
			}
		}
	} else {
		mouse_dx_acc = 0;
		mouse_dy_acc = 0;
	}
}
