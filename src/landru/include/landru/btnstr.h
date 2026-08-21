#ifndef LANDRU_BTNSTR_H
#define LANDRU_BTNSTR_H

#include <stdbool.h>
#include <stdint.h>

#include <landru/input.h>
#include <landru/rect.h>

typedef struct {
	Input header;
	char* string;        /* 256-byte editable text buffer */
	uint8_t filter_type; /* 0=printable, 1=alpha, 2=numeric, 3=alnum+underscore+hyphen */
} StringButton;

void lbtnstr_Create_String_Button_Module(void);
void lbtnstr_Destroy_String_Button_Module(void);
StringButton* lbtnstr_Generate_String_Button(Input* parent, char* res_buffer, const char* initial_str);
StringButton* lbtnstr_Alloc_String_Button(Input* parent, Rect* frame, int16_t zinput, InputUserFunc callback,
										  const char* initial_str, uint8_t filter_type, int16_t id);
int16_t lbtnstr_Free_String_Button(StringButton* btn);
void lbtnstr_idraw_String_Button(Input* input, Rect* paint_rect, Rect* dirty_rect, int16_t refresh);
int16_t lbtnstr_iupdate_String_Button(Input* input, Rect* frame, Rect* clip, int16_t key, uint8_t mouse_l,
									  uint8_t mouse_r, int16_t x, int16_t y);
int16_t lbtnstr_Add_Key_To_String(char* str, int16_t key);
void lbtnstr_Set_String_Button_Name(StringButton* btn, const char* name);
void lbtnstr_Get_String_Button_Name(StringButton* btn, char* out);
void lbtnstr_Get_Clipped_String_Button_Name(StringButton* btn, char* out, int16_t max_len);

#endif
