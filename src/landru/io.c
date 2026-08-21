#include <landru/canvas.h>
#include <landru/cursor.h>
#include <landru/error.h>
#include <landru/io.h>
#include <landru/joy.h>
#include <landru/mouse.h>
#include <landru/rect.h>
#include <landru/timer.h>
#include <landru/view.h>

#include "host_internal.h"

/* Globals */
Rect mouse_limits_gbl;
int16_t joy_port_gbl;
// GLOBAL: TIE 0xD2BE2
int16_t mouse_x_gbl;
// GLOBAL: TIE 0xD2BE4
int16_t mouse_y_gbl;
// GLOBAL: TIE 0xD2BE6
int16_t mouse_cursor_x_gbl;
// GLOBAL: TIE 0xD2BE8
int16_t mouse_cursor_y_gbl;
int16_t fast_mouse_x_gbl;
int16_t fast_mouse_y_gbl;
// GLOBAL: TIE 0xD2BEE
int16_t last_mouse_x_gbl;
// GLOBAL: TIE 0xD2BF0
int16_t last_mouse_y_gbl;
int16_t joy_move_gbl;
int16_t joy_xf_gbl;
int16_t joy_yf_gbl;
// GLOBAL: TIE 0xD2BF8
int16_t down_button_gbl;
int16_t fast_button_gbl;
bool fast_mouse_release_gbl;
bool fast_joy_release_gbl;
// GLOBAL: TIE 0xD2C00
int16_t last_button_gbl;
// GLOBAL: TIE 0xD2C02
int16_t release_button_gbl;
// GLOBAL: TIE 0xD2C04
int16_t key_gbl;
// GLOBAL: TIE 0xD2C06
bool ask_key_gbl;

// GLOBAL: TIE 0xD2C08
int16_t special_key_gbl;
// GLOBAL: TIE 0xD2C0A
bool key_btn_gbl;
int32_t cursor_time_gbl;
/* Min PIT ticks between cursor screen updates (8 ticks @ ~250 Hz = 32ms,
 * matches the binary's data-segment value at 0xe1874). */
int16_t cursor_rate_gbl = 8;
uint8_t system_speed_gbl = 4; /* Modern systems are always "fast". Original binary calibrated this from PIT
								 timer at startup; thresholds checked are 2 and 3. */
// GLOBAL: TIE 0xD2C14
bool io_module_gbl;
// GLOBAL: TIE 0xFB924
bool mouse_exists_gbl;
// GLOBAL: TIE 0xFB928
int16_t joy_exists_gbl;

/* Front-end screenshot hook. Retail SHELLEXT_Open_Landru installs a callback
 * (Save_PCX_Screenshot wrapper) that XIO_Poll_Input invokes when Alt+O
 * (key code 0x1800) is pressed in any Landru scene. */
static void (*screenshot_hook_gbl)(void);

void lio_Set_Screenshot_Hook(void (*hook)(void)) { screenshot_hook_gbl = hook; }

/* --- Module lifecycle --- */

void lio_Create_IO_Module(void) {
	mouse_exists_gbl = lmouse_MS_Initialize_Mouse();
	if (mouse_exists_gbl) {
		bool fast_release = fast_mouse_release_gbl && fast_joy_release_gbl;
		if (lio_Is_Mouse_Input() || lio_Is_Joystick_Input()) {
			if ((last_button_gbl & fast_button_gbl) && fast_release) {
				/* Keep cursor position unchanged */
			} else if (fast_button_gbl) {
				mouse_cursor_x_gbl = fast_mouse_x_gbl;
				mouse_cursor_y_gbl = fast_mouse_y_gbl;
			}
			fast_button_gbl = 0;
			fast_mouse_release_gbl = !mouse_exists_gbl;
			fast_joy_release_gbl = !joy_exists_gbl;
		} else {
			mouse_cursor_x_gbl = 0;
			mouse_cursor_y_gbl = 0;
		}
	} else {
		mouse_cursor_x_gbl = 128;
		mouse_cursor_y_gbl = 128;
	}

	fast_mouse_release_gbl = !mouse_exists_gbl;
	joy_exists_gbl = ljoy_Joystick_Init();
	joy_port_gbl = (joy_exists_gbl == 2);
	down_button_gbl = 0;
	fast_button_gbl = 0;
	last_button_gbl = 0;
	release_button_gbl = 0;
	ask_key_gbl = false;
	special_key_gbl = 0;
	key_btn_gbl = false;
	fast_joy_release_gbl = !joy_exists_gbl;
	io_module_gbl = true;
	key_gbl = 0;
}

void lio_Destroy_IO_Module(void) {
	if (io_module_gbl) {
		joy_exists_gbl = 0;
		io_module_gbl = false;
		mouse_exists_gbl = false;
	}
}

/* --- Input queries --- */

bool lio_Is_Mouse_Input(void) { return mouse_exists_gbl; }
bool lio_Is_Joystick_Input(void) { return joy_exists_gbl != 0; }

/* --- Calc_Mouse_Pos: latch fast-polled state into stable output --- */

void lio_Calc_Mouse_Pos(int16_t* outX, int16_t* outY, int16_t* outBtn) {
	bool fast_release = fast_mouse_release_gbl && fast_joy_release_gbl;

	if (mouse_exists_gbl || lio_Is_Joystick_Input()) {
		if ((last_button_gbl & fast_button_gbl) && fast_release) {
			*outX = mouse_cursor_x_gbl;
			*outY = mouse_cursor_y_gbl;
			*outBtn = 0;
		} else {
			if (fast_button_gbl) {
				*outX = fast_mouse_x_gbl;
				*outY = fast_mouse_y_gbl;
			} else {
				*outX = mouse_cursor_x_gbl;
				*outY = mouse_cursor_y_gbl;
			}
			*outBtn = fast_button_gbl;
		}
		fast_button_gbl = 0;
		fast_mouse_release_gbl = !mouse_exists_gbl;
		fast_joy_release_gbl = !joy_exists_gbl;
	} else {
		*outX = 0;
		*outY = 0;
		*outBtn = 0;
	}
}

/* --- Flush_Input --- */

void lio_Flush_Input(void) {
	int16_t x = mouse_cursor_x_gbl;
	int16_t y = mouse_cursor_y_gbl;
	int16_t btn = 0;

	if (lio_Is_Mouse_Input())
		lio_Poll_Fast_Mouse_Input(&x, &y, &btn);

	if (cursor_time_gbl + cursor_rate_gbl <= ltimer_Current_Time()) {
		if (lio_Is_Joystick_Input())
			lio_Poll_Fast_Joystick_Input(&x, &y, &btn);
		if (lcursor_Is_Cursor())
			lcursor_Cursor_To_Screen(x, y);
		cursor_time_gbl = ltimer_Current_Time();
	}

	lio_Calc_Mouse_Pos(&mouse_x_gbl, &mouse_y_gbl, &down_button_gbl);
	release_button_gbl = 0;
	down_button_gbl = 0;
}

/* --- Poll_Input: main per-frame input poll --- */

int lio_Poll_Input(void) {
	/* The application updates its input snapshot before entering Landru.
	 * Polling here only consumes that latched state. */
	last_button_gbl = down_button_gbl;
	release_button_gbl = down_button_gbl;
	last_mouse_x_gbl = mouse_x_gbl;
	last_mouse_y_gbl = mouse_y_gbl;

	lio_Calc_Mouse_Pos(&mouse_x_gbl, &mouse_y_gbl, &down_button_gbl);

	/* Pause step-counter */
	if (view_gbl->stepCount) {
		view_gbl->stepCount--;
		if (view_gbl->stepCount == 0)
			view_gbl->step = 1;
	}

	/* Read special key flags (BIOS data area) */
	special_key_gbl = lio_Special_Keys();

	/* Keyboard input — only when no mouse buttons active */
	if (!down_button_gbl && !release_button_gbl) {
		if (landru_host_key_pending()) {
			int16_t key_val = landru_host_key_read();
			if (!key_val)
				key_val = landru_host_key_read() << 8;
			key_gbl = key_val;

			/* Strip high byte for ASCII keys */
			if ((uint8_t)key_val)
				key_gbl = (uint8_t)key_val;

			/* Drain duplicate buffered keys */
			if (key_gbl) {
				while (landru_host_key_pending()) {
					int16_t peek = landru_host_key_pending();
					if ((uint8_t)peek && (uint8_t)peek != key_gbl)
						break;
					if (peek == key_gbl) {
						int drain = landru_host_key_read();
						if (!drain)
							landru_host_key_read();
					} else {
						break;
					}
				}
			}

			ask_key_gbl = false;

			/* Handle special keys */
			if (key_gbl == 0x0D && key_btn_gbl) {
				/* Enter with key_btn: map to right-click */
				key_gbl = 0;
				down_button_gbl |= 2;
			} else if (key_gbl == 27) {
				/* Escape */
				lerror_Do_Landru_Escape();
			} else if ((uint16_t)key_gbl == 0x1800) {
				/* Alt+O: front-end screenshot */
				if (screenshot_hook_gbl)
					screenshot_hook_gbl();
			} else if ((uint16_t)key_gbl == 0x5200) {
				/* Insert: toggle pause */
				view_gbl->step ^= 1;
				view_gbl->stepCount = 0;
			} else if ((uint16_t)key_gbl == 0x5300) {
				/* Delete: single-step */
				if (view_gbl->step) {
					view_gbl->stepCount = 1;
					view_gbl->step = 0;
				}
			}
		} else if (ask_key_gbl) {
			key_gbl = 0;
			ask_key_gbl = false;
		}
	}

	release_button_gbl &= ~down_button_gbl;
	return 0;
}

/* --- Fast polling (hardware-specific) --- */

void lio_Poll_Fast_Input(void) {
	int16_t cur_x = mouse_cursor_x_gbl;
	int16_t cur_y = mouse_cursor_y_gbl;
	int16_t btn = 0;
	int16_t current_joysticks = ljoy_Joystick_Init();
	if (current_joysticks != joy_exists_gbl) {
		joy_exists_gbl = current_joysticks;
		joy_port_gbl = (joy_exists_gbl == 2);
		fast_joy_release_gbl = true;
	}

	if (mouse_exists_gbl)
		lio_Poll_Fast_Mouse_Input(&cur_x, &cur_y, &btn);

	if (cursor_time_gbl + cursor_rate_gbl <= ltimer_Current_Time()) {
		if (joy_exists_gbl)
			lio_Poll_Fast_Joystick_Input(&cur_x, &cur_y, &btn);
		if (lcursor_Is_Cursor())
			lcursor_Cursor_To_Screen(cur_x, cur_y);
		cursor_time_gbl = ltimer_Current_Time();
	}
}

void lio_Poll_Fast_Mouse_Input(int16_t* outX, int16_t* outY, int16_t* outBtn) {
	int16_t buttons = 0, dx = 0, dy = 0;
	lmouse_MS_Get_Mouse_Pos(&buttons, &dx, &dy);
	lmouse_MS_Mouse_Movement(&dx, &dy);
	mouse_cursor_x_gbl += dx;
	mouse_cursor_y_gbl += dy;
	lrect_Clip_Point_To_Rect(&mouse_limits_gbl, &mouse_cursor_x_gbl, &mouse_cursor_y_gbl);

	if (buttons) {
		fast_mouse_x_gbl = mouse_cursor_x_gbl;
		fast_mouse_y_gbl = mouse_cursor_y_gbl;
	}
	fast_button_gbl |= buttons;
	fast_mouse_release_gbl |= (buttons == 0);

	*outX = mouse_cursor_x_gbl;
	*outY = mouse_cursor_y_gbl;
	*outBtn |= buttons;
}

void lio_Poll_Fast_Joystick_Input(int16_t* outX, int16_t* outY, int16_t* outBtn) {
	int16_t raw_x = 0, raw_y = 0;
	int16_t joy_buttons = ljoy_Joystick_Read(&raw_x, &raw_y, joy_port_gbl);

	if (joy_exists_gbl) {
		/* Coarse dead zone: clear low 3 bits, preserve sign. */
		raw_x &= (int16_t)~0x07;
		raw_y &= (int16_t)~0x07;

		int moved = 0;
		if (raw_x || raw_y) {
			int16_t elapsed = (ltimer_Current_Time() - cursor_time_gbl) / cursor_rate_gbl;
			int16_t speed = joy_move_gbl * elapsed;
			moved = 1;

			/* X axis: 8.8 fixed-point accumulation */
			int16_t xacc = raw_x * speed + joy_xf_gbl;
			joy_xf_gbl = xacc;
			if (xacc >= 0) {
				raw_x = xacc >> 8;
				joy_xf_gbl &= 0xFF;
			} else {
				raw_x = -((-joy_xf_gbl) >> 8);
				joy_xf_gbl = -(uint8_t)(-(int8_t)xacc);
			}

			/* Y axis */
			int16_t yacc = raw_y * (speed >> 1) + joy_yf_gbl;
			joy_yf_gbl = yacc;
			if (yacc >= 0) {
				raw_y = yacc >> 8;
				joy_yf_gbl &= 0xFF;
			} else {
				raw_y = -((-joy_yf_gbl) >> 8);
				joy_yf_gbl = -(uint8_t)(-(int8_t)yacc);
			}

			if (joy_move_gbl < 32)
				joy_move_gbl++;
		} else {
			joy_xf_gbl = 0;
			joy_yf_gbl = 0;
			joy_move_gbl = 0;
		}

		int16_t new_x = mouse_cursor_x_gbl + raw_x;
		int16_t new_y = mouse_cursor_y_gbl + raw_y;

		/* Clamp to canvas bounds */
		Rect canvas;
		lcanvas_Get_Drawing_Canvas_Bounds(&canvas);
		if (new_x < canvas.left)
			new_x = canvas.left;
		if (new_x >= canvas.right)
			new_x = canvas.right - 1;
		if (new_y < canvas.top)
			new_y = canvas.top;
		if (new_y >= canvas.bottom)
			new_y = canvas.bottom - 1;

		if (joy_buttons) {
			fast_mouse_x_gbl = new_x;
			fast_mouse_y_gbl = new_y;
		}
		if (moved) {
			mouse_cursor_x_gbl = new_x;
			mouse_cursor_y_gbl = new_y;
		}
		*outX = new_x;
		*outY = new_y;
	}

	fast_button_gbl |= joy_buttons;
	fast_joy_release_gbl |= (joy_buttons == 0);
	*outBtn |= joy_buttons;
}

/* --- Mouse position/limits --- */

void lio_Set_Mouse_Position(int16_t x, int16_t y) {
	mouse_cursor_x_gbl = x;
	mouse_cursor_y_gbl = y;
	lmouse_MS_Set_Mouse_Pos(x, y);
}

void lio_Set_Mouse_Limits(Rect* rect) {
	mouse_limits_gbl = *rect;
	mouse_limits_gbl.right--;
	mouse_limits_gbl.bottom--;
}

void lio_Get_Mouse_Limits(Rect* out) {
	*out = mouse_limits_gbl;
	out->right++;
	out->bottom++;
}

/* --- Button state --- */

int16_t lio_Any_Button(void) { return down_button_gbl; }
int16_t lio_Left_Button(void) { return down_button_gbl & 1; }
int16_t lio_Right_Button(void) { return down_button_gbl & 2; }
int16_t lio_Any_Button_Release(void) { return release_button_gbl; }
int16_t lio_Left_Button_Release(void) { return release_button_gbl & 1; }
int16_t lio_Right_Button_Release(void) { return release_button_gbl & 2; }

int16_t lio_Button_Changed(void) { return last_button_gbl != down_button_gbl; }

/* --- Mouse position --- */

int16_t lio_Mouse_X(void) { return mouse_x_gbl; }
int16_t lio_Mouse_Y(void) { return mouse_y_gbl; }

int16_t lio_Mouse_Moved(void) {
	if (last_mouse_x_gbl == mouse_x_gbl && last_mouse_y_gbl == mouse_y_gbl)
		return 0;
	return 1;
}

int16_t lio_Input_Changed(void) {
	if (last_button_gbl != down_button_gbl)
		return 1;
	if (lio_Mouse_Moved())
		return 1;
	if (lio_Get_Free_Key())
		return 1;
	return 0;
}

/* --- Keyboard --- */

int16_t lio_Get_Key(void) {
	ask_key_gbl = true;
	return key_gbl;
}

int16_t lio_Get_Free_Key(void) { return key_gbl; }

int16_t lio_Wait_For_Key(void) {
	if (ask_key_gbl || !key_gbl) {
		ask_key_gbl = true;
		int16_t val = landru_host_key_read();
		if (!val)
			val = landru_host_key_read() << 8;
		key_gbl = val;
	} else {
		ask_key_gbl = true;
	}
	return key_gbl;
}

void lio_Clear_Key(void) {
	key_gbl = 0;
	ask_key_gbl = false;
}

void lio_Set_Key_Buttons(void) { key_btn_gbl = true; }
void lio_Clear_Key_Buttons(void) { key_btn_gbl = false; }
bool lio_Is_Key_Buttons(void) { return key_btn_gbl; }

/* --- Special key state (BIOS keyboard flags) --- */

bool lio_Is_Insert_Key_Down(void) { return special_key_gbl & 0x80; }
bool lio_Is_Caps_Lock_Down(void) { return special_key_gbl & 0x40; }
bool lio_Is_Num_Lock_Down(void) { return special_key_gbl & 0x20; }
bool lio_Is_Scroll_Lock_Down(void) { return special_key_gbl & 0x10; }
bool lio_Is_Alt_Key_Down(void) { return special_key_gbl & 0x08; }
bool lio_Is_Ctrl_Key_Down(void) { return special_key_gbl & 0x04; }
bool lio_Is_Shift_Key_Down(void) { return special_key_gbl & 0x03; }
bool lio_Is_Left_Shift_Key_Down(void) { return special_key_gbl & 0x02; }
bool lio_Is_Right_Shift_Key_Down(void) { return special_key_gbl & 0x01; }

/* --- Blink / system speed --- */

bool lio_Blink(void) { return (ltimer_Current_Time() >> 6) & 1; }
bool lio_Fast_Blink(void) { return (ltimer_Current_Time() >> 4) & 1; }

void lio_Set_System_Speed(uint8_t speed) { system_speed_gbl = speed; }

bool lio_Is_System_Fast_Enough(uint8_t level) { return system_speed_gbl >= level; }

bool lio_Is_System_Slower_Than(uint8_t speed) { return system_speed_gbl < speed; }

/* --- DOS-specific key functions --- */

int lio_Special_Keys(void) { return landru_host_modifier_keys(); }

int lio_Wait_Key(void) {
	int val = landru_host_key_read();
	if (!val)
		return landru_host_key_read() << 8;
	return val;
}

int lio_Test_Key(void) { return landru_host_key_pending(); }
