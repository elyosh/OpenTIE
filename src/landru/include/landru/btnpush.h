#ifndef LANDRU_BTNPUSH_H
#define LANDRU_BTNPUSH_H

#include <stdbool.h>
#include <stdint.h>

#include <landru/input.h>
#include <landru/rect.h>

typedef struct {
	Input header;
	char* name;
	int16_t pressed;
} PushButton;

void lbtnpush_Create_Button_Module(void);
void lbtnpush_Destroy_Button_Module(void);
PushButton* lbtnpush_Generate_Button(Input* parent, char* res_buffer, const char* name);
PushButton* lbtnpush_Alloc_Button(Input* parent, Rect* frame, int16_t zinput, InputUserFunc callback,
								  const char* name, int16_t id);
PushButton* lbtnpush_Alloc_Small_Button(Input* parent, Rect* frame, int16_t zinput, InputUserFunc callback,
										const char* name, int16_t id);
int16_t lbtnpush_Free_Button(PushButton* btn);
void lbtnpush_Init_Button(PushButton* btn, InputUserFunc callback, const char* name, int16_t id);
void lbtnpush_idraw_Button(Input* input, Rect* paint_rect, Rect* dirty_rect, int16_t do_draw);
int16_t lbtnpush_iupdate_Button(Input* input, Rect* paint_rect, Rect* clip, int16_t phase, uint8_t mouse_l,
								uint8_t mouse_r, int16_t x, int16_t y);
void lbtnpush_Set_Button_Name(PushButton* btn, const char* name);
void lbtnpush_Get_Button_Name(PushButton* btn, char* out);

#endif
