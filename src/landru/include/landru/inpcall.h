#ifndef LANDRU_INPCALL_H
#define LANDRU_INPCALL_H

#include <stdbool.h>
#include <stdint.h>

#include <landru/input.h>
#include <landru/rect.h>

typedef enum {
	INPUT_ACTIVE_IDLE = 0,
	INPUT_ACTIVE_LEFT_DOWN = 1,
	INPUT_ACTIVE_RIGHT_DOWN = 2,
	INPUT_ACTIVE_LEFT_UP = 3,
	INPUT_ACTIVE_RIGHT_UP = 4,
} InputActiveState;

void linpcall_Draw_Inputs(Input* list, Rect* parent_frame, Rect* parent_clip, int16_t refresh);
void linpcall_Draw_Input_Layer(Input* list, Rect* parent_frame, Rect* parent_clip, int16_t refresh);
void linpcall_Get_Input_Parent_Frame(Rect* parent_frame, Rect* parent_clip, Rect* out_frame, Rect* out_clip);
int16_t linpcall_Clip_Input_To_Frame(Input* input, Rect* frame, Rect* clip);
void linpcall_Update_Inputs(Input* root);
int16_t linpcall_Update_Key_Inputs(Input* root, Rect* parent_frame, Rect* parent_clip, int16_t key,
								   int16_t mouse_x, int16_t mouse_y);
int16_t linpcall_Update_Mouse_Down(Input* root, Rect* parent_frame, uint8_t mouse_l, Rect* parent_clip,
								   uint8_t mouse_r, int16_t mouse_x, int16_t mouse_y);
int16_t linpcall_Update_Mouse_Move(Input* root, Rect* parent_frame, Rect* parent_clip, int16_t mouse_x,
								   int16_t mouse_y);
void linpcall_Update_Mouse_UpMove(Input* input, Rect* frame, Rect* clip, uint8_t mouse_l, uint8_t mouse_r);
void linpcall_User_Inputs(Input* root, int32_t context);
int16_t linpcall_Fetch_Input_Buttons(uint8_t* out_left, uint8_t* out_right);
void linpcall_Set_InputActive_Down(Input* input, Rect* frame, Rect* clip, uint8_t mouse_l, uint8_t mouse_r);
int16_t linpcall_Replace_InputActive(Input* input);
void linpcall_Set_InputActive_Frame(Rect* frame, Rect* clip);
void linpcall_Set_InputActive_Ignore(uint8_t mouse_l, uint8_t mouse_r);
void linpcall_Set_InputActive_Up(uint8_t mouse_l, uint8_t mouse_r);
void linpcall_Clear_Active_Input(void);
int16_t linpcall_Find_View_Input_Frame(Input* target, Rect* out_frame, Rect* out_clip);
int16_t linpcall_Find_Input_Frame(Input* root, Input* target, Rect* frame, Rect* clip);
Input* linpcall_Check_Parent_Children_For_Input(Input* parent, Input* target);
Input* linpcall_Find_View_Parent_For_Input(Input* target);
Input* linpcall_Find_Parent_For_Input(Input* root, Input* target);
Input* linpcall_Find_View_Input_With_ID(int16_t id);
Input* linpcall_Find_Input_With_ID(Input* root, int16_t id);
void linpcall_Select_View_Shared_Key_Input(Input* target);
void linpcall_Select_Shared_Key_Input(Input* root, Input* target);

#endif
