#ifndef LANDRU_IO_H
#define LANDRU_IO_H

#include <stdbool.h>
#include <stdint.h>

#include <landru/rect.h>

extern Rect mouse_limits_gbl;
extern int16_t joy_port_gbl;
extern int16_t mouse_x_gbl;
extern int16_t mouse_y_gbl;
extern int16_t mouse_cursor_x_gbl;
extern int16_t mouse_cursor_y_gbl;
extern int16_t fast_mouse_x_gbl;
extern int16_t fast_mouse_y_gbl;
extern int16_t last_mouse_x_gbl;
extern int16_t last_mouse_y_gbl;
extern int16_t joy_move_gbl;
extern int16_t joy_xf_gbl;
extern int16_t joy_yf_gbl;
extern int16_t down_button_gbl;
extern int16_t fast_button_gbl;
extern bool fast_mouse_release_gbl;
extern bool fast_joy_release_gbl;
extern int16_t last_button_gbl;
extern int16_t release_button_gbl;
extern int16_t key_gbl;
extern bool ask_key_gbl;
extern int16_t special_key_gbl;
extern bool key_btn_gbl;
extern int32_t cursor_time_gbl;
extern int16_t cursor_rate_gbl;
extern uint8_t system_speed_gbl;
extern bool io_module_gbl;
extern bool mouse_exists_gbl;
extern int16_t joy_exists_gbl;

void lio_Create_IO_Module(void);
void lio_Destroy_IO_Module(void);
bool lio_Is_Mouse_Input(void);
bool lio_Is_Joystick_Input(void);

void lio_Flush_Input(void);
int lio_Poll_Input(void);
void lio_Poll_Fast_Input(void);
void lio_Poll_Fast_Mouse_Input(int16_t* outX, int16_t* outY, int16_t* outBtn);
void lio_Poll_Fast_Joystick_Input(int16_t* outX, int16_t* outY, int16_t* outBtn);
void lio_Calc_Mouse_Pos(int16_t* outX, int16_t* outY, int16_t* outBtn);
void lio_Set_Mouse_Position(int16_t x, int16_t y);
void lio_Set_Mouse_Limits(Rect* rect);
void lio_Get_Mouse_Limits(Rect* out);
int16_t lio_Any_Button(void);
int16_t lio_Left_Button(void);
int16_t lio_Right_Button(void);
int16_t lio_Any_Button_Release(void);
int16_t lio_Left_Button_Release(void);
int16_t lio_Right_Button_Release(void);
int16_t lio_Button_Changed(void);
int16_t lio_Mouse_X(void);
int16_t lio_Mouse_Y(void);
int16_t lio_Mouse_Moved(void);
int16_t lio_Input_Changed(void);
int16_t lio_Get_Key(void);
int16_t lio_Get_Free_Key(void);
int16_t lio_Wait_For_Key(void);
void lio_Clear_Key(void);
void lio_Set_Key_Buttons(void);
void lio_Clear_Key_Buttons(void);
bool lio_Is_Key_Buttons(void);
bool lio_Is_Insert_Key_Down(void);
bool lio_Is_Caps_Lock_Down(void);
bool lio_Is_Num_Lock_Down(void);
bool lio_Is_Scroll_Lock_Down(void);
bool lio_Is_Alt_Key_Down(void);
bool lio_Is_Ctrl_Key_Down(void);
bool lio_Is_Shift_Key_Down(void);
bool lio_Is_Left_Shift_Key_Down(void);
bool lio_Is_Right_Shift_Key_Down(void);
bool lio_Blink(void);
bool lio_Fast_Blink(void);
void lio_Set_System_Speed(uint8_t speed);
bool lio_Is_System_Fast_Enough(uint8_t level);
bool lio_Is_System_Slower_Than(uint8_t speed);
int lio_Special_Keys(void);
int lio_Wait_Key(void);
int lio_Test_Key(void);
void lio_Set_Screenshot_Hook(void (*hook)(void));

#endif
