#ifndef LANDRU_BTNTEXT_H
#define LANDRU_BTNTEXT_H

#include <stdbool.h>
#include <stdint.h>

#include <landru/input.h>
#include <landru/rect.h>

typedef struct {
	Input header;
	char* name;
	int16_t text_color;
} TextButton;

void lbtntext_Create_Text_Button_Module(void);
void lbtntext_Destroy_Text_Button_Module(void);
TextButton* lbtntext_Generate_Text_Button(Input* parent, char* res_buffer, const char* name);
TextButton* lbtntext_Alloc_Text_Button(Input* parent, Rect* frame, int16_t zinput, InputUserFunc callback,
									   const char* name, int16_t id);
TextButton* lbtntext_Alloc_Frame_Button(Input* parent, Rect* frame, int16_t zinput, InputUserFunc callback,
										const char* name, int16_t id);
int16_t lbtntext_Free_Text_Button(TextButton* btn);
void lbtntext_Init_Text_Button(TextButton* btn, InputUserFunc callback, const char* name, int16_t id);
void lbtntext_idraw_Text_Button(TextButton* btn, Rect* paint_rect, Rect* dirty_rect, int16_t refresh);
void lbtntext_Set_Text_Button_Name(TextButton* btn, const char* name);
void lbtntext_Get_Text_Button_Name(TextButton* btn, char* out);
void lbtntext_Set_Text_Button_Color(TextButton* btn, int16_t color);
int16_t lbtntext_Get_Text_Button_Color(TextButton* btn);

#endif
