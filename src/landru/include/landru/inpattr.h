#ifndef LANDRU_INPATTR_H
#define LANDRU_INPATTR_H

#include <stdint.h>

#include <landru/input.h>
#include <landru/rect.h>

void linpattr_Show_Input(Input* input);
void linpattr_Hide_Input(Input* input);
int16_t linpattr_Is_Input_Visible(Input* input);

void linpattr_Activate_Input(Input* input);
void linpattr_Deactivate_Input(Input* input);
int16_t linpattr_Is_Input_Active(Input* input);

void linpattr_Start_Input(Input* input);
void linpattr_Stop_Input(Input* input);

void linpattr_Dirty_Input(Input* input);
void linpattr_Non_Dirty_Input(Input* input);
int16_t linpattr_Is_Input_Dirty(Input* input);

void linpattr_Refresh_Input(Input* input);
void linpattr_Non_Refresh_Input(Input* input);
int16_t linpattr_Is_Input_Refresh(Input* input);

void linpattr_Refreshable_Input(Input* input);
void linpattr_Non_Refreshable_Input(Input* input);
int16_t linpattr_Is_Input_Refreshable(Input* input);

void linpattr_Discard_Input_Data(Input* input);
void linpattr_Non_Discard_Input_Data(Input* input);
int16_t linpattr_Is_Discard_Input_Data(Input* input);

void linpattr_Selected_Input(Input* input);
void linpattr_Non_Selected_Input(Input* input);
int16_t linpattr_Is_Input_Selected(Input* input);
int16_t linpattr_Get_Input_Selected(Input* input);

void linpattr_Set_Input_Flag1(Input* input);
void linpattr_Clear_Input_Flag1(Input* input);
int16_t linpattr_Is_Input_Flag1(Input* input);

void linpattr_Set_Input_Flag2(Input* input);
void linpattr_Clear_Input_Flag2(Input* input);
int16_t linpattr_Is_Input_Flag2(Input* input);

void linpattr_Set_Input_Frame(Input* input, Rect* src);
void linpattr_Get_Input_Frame(Input* input, Rect* dst);

void linpattr_Set_Input_ZInput(Input* input, int16_t zinput);
int16_t linpattr_Get_Input_ZInput(Input* input, uint16_t* out);

void linpattr_Set_Input_Allign(Input* input, uint8_t halign, uint8_t valign);
void linpattr_Get_Input_Allign(Input* input, uint8_t* out_h, uint8_t* out_v);

void linpattr_Set_Input_Draw_Function(Input* input, InputDrawFunc fn);
void linpattr_Get_Input_Draw_Function(Input* input, InputDrawFunc* out);
void linpattr_Set_Input_Update_Function(Input* input, InputUpdateFunc fn);
void linpattr_Get_Input_Update_Function(Input* input, InputUpdateFunc* out);
void linpattr_Set_Input_User_Function(Input* input, InputUserFunc fn);
void linpattr_Get_Input_User_Function(Input* input, InputUserFunc* out);

void linpattr_Set_Input_Window(Input* input);
void linpattr_Clear_Input_Window(Input* input);
int16_t linpattr_Is_Input_Window(Input* input);

void linpattr_Set_Input_Window_Active(Input* input);
void linpattr_Clear_Input_Window_Active(Input* input);
int16_t linpattr_Is_Input_Window_Active(Input* input);

void linpattr_Activate_Window(Input* input);
void linpattr_Move_Input(Input* input, int16_t x, int16_t y);

void linpattr_Check_Input_ZInputs(void);
void linpattr_Sort_Input_ZInputs(void);
Input* linpattr_Sort_Input_ZInput_List(Input** head);

#endif
