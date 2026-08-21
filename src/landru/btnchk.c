#include <stdlib.h>
#include <string.h>

#include <landru/btnchk.h>
#include <landru/dirty.h>
#include <landru/font.h>
#include <landru/inpattr.h>
#include <landru/memptr.h>
#include <landru/paint.h>
#include <landru/rect.h>
#include <landru/remap.h>
#include <landru/style.h>

#include "binio.h"
#include <landru/fourcc.h>

// GLOBAL: TIE 0xD3028
static bool check_button_module_gbl;

void lbtnchk_Create_Check_Button_Module(void) {
	linput_Create_Input_Type(FOURCC_CHCK, (void*)lbtnchk_Generate_Check_Button,
							 (void*)lbtnchk_Free_Check_Button);
	check_button_module_gbl = true;
}

void lbtnchk_Destroy_Check_Button_Module(void) {
	if (check_button_module_gbl) {
		linput_Destroy_Input_Type(FOURCC_CHCK);
		check_button_module_gbl = false;
	}
}

CheckButton* lbtnchk_Generate_Check_Button(Input* parent, char* res_buffer, const char* name) {
	InputResourceHeader hdr;
	InputResourceHeader_decode(&hdr, (const uint8_t*)res_buffer);

	CheckButton* btn = lbtnchk_Alloc_Check_Button(parent, &hdr.frame, hdr.zinput, NULL, name, hdr.id);
	if (!btn)
		return NULL;

	btn->header.alignment = hdr.window_flags;
	btn->header.flags = hdr.state_flags;
	btn->header.var1 = hdr.exit_pending;
	btn->header.var2 = hdr.exit_code;
	btn->header.type = hdr.res_type;

	const uint8_t* ext = (const uint8_t*)res_buffer + INPUTRESOURCEHEADER_DISK_SIZE;
	if (br_i16le(ext + 0)) {
		btn->text_color = br_i16le(ext + 2);
		btn->state = br_i16le(ext + 4);
	}

	return btn;
}

CheckButton* lbtnchk_Alloc_Check_Button(Input* parent, Rect* frame, int16_t zinput, InputUserFunc callback,
										const char* name, int16_t id) {
	CheckButton* btn =
		(CheckButton*)linput_Alloc_Dialog_Input(parent, frame, zinput, sizeof(CheckButton) - sizeof(Input));
	if (btn) {
		lbtnchk_Init_Check_Button(btn, callback, name, id);
		btn->header.type = FOURCC_CHCK;
	}
	return btn;
}

int16_t lbtnchk_Free_Check_Button(CheckButton* btn) {
	if (btn->name) {
		free(btn->name);
		btn->name = NULL;
	}
	return 1;
}

void lbtnchk_Init_Check_Button(CheckButton* btn, InputUserFunc callback, const char* name, int16_t id) {
	linpattr_Set_Input_Draw_Function(&btn->header, lbtnchk_idraw_Check_Button);
	linpattr_Set_Input_Update_Function(&btn->header, lbtnchk_iupdate_Check_Button);
	linpattr_Set_Input_User_Function(&btn->header, callback);
	btn->header.mouseUsage = downMoveUpInput;
	btn->header.id = id;

	if (name) {
		btn->name = lmemptr_Duplicate_String(name);
	} else {
		btn->name = NULL;
	}

	btn->state = 0;
	btn->pressed = 0;
	btn->text_color = 26;
}

void lbtnchk_idraw_Check_Button(Input* input, Rect* paint_rect, Rect* dirty_rect, int16_t refresh) {
	CheckButton* btn = (CheckButton*)input;
	if (!refresh)
		return;

	/* Checkbox area: 12px wide, vertically centered in paint_rect */
	Rect dst;
	lrect_Copy_Rect(&dst, paint_rect);
	dst.right = dst.left + 12;

	int16_t box_h = dst.bottom - dst.top;
	if (box_h >= 12)
		lrect_Inset_Rect(&dst, 0, (box_h - 12) >> 1);

	/* Draw bevel border (inverts while pressed) */
	lstyle_Style_Paint_Border(&dst, btn->pressed);

	/* Fill check mark if state != pressed (preview toggle while clicking) */
	if (btn->state != btn->pressed) {
		lrect_Inset_Rect(&dst, 3, 3);
		int16_t color = lremap_Get_Remap(REMAP_GRAY_1);
		lpaint_Paint_Clipped_Rect(&dst, color);
	}

	/* Draw label text to the right of the checkbox */
	if (btn->name) {
		int16_t h = paint_rect->bottom - paint_rect->top;
		int16_t text_y = paint_rect->top + ((h - lfont_Get_FontID_Height(0)) >> 1);
		lfont_Print_Clipped_Text(btn->name, paint_rect->left + 16, text_y, 0, btn->text_color);
	}

	if (linpattr_Is_Input_Dirty(&btn->header))
		ldirty_Dirty_Rect(dirty_rect);
}

int16_t lbtnchk_iupdate_Check_Button(Input* input, Rect* paint_rect, Rect* clip, int16_t phase,
									 uint8_t mouse_l, uint8_t mouse_r, int16_t x, int16_t y) {
	CheckButton* btn = (CheckButton*)input;
	(void)clip;
	if (phase)
		return 0;

	uint8_t mouse_state = 0;
	if (mouse_l)
		mouse_state = mouse_l;
	if (mouse_r)
		mouse_state = mouse_r;

	if (mouse_state == 1) {
		btn->pressed = 1;
	} else if (mouse_state == 2) {
		btn->pressed = lrect_Point_In_Rect(paint_rect, paint_rect->left + x, paint_rect->top + y);
	} else if (mouse_state == 3) {
		if (btn->pressed) {
			linpattr_Selected_Input(&btn->header);
			btn->pressed = 0;
			btn->state ^= 1;
		}
	}

	linpattr_Refresh_Input(&btn->header);
	return 0;
}

void lbtnchk_Set_Check_Button_Name(CheckButton* btn, const char* name) {
	free(btn->name);
	btn->name = lmemptr_Duplicate_String(name);
	linpattr_Refresh_Input(&btn->header);
}

void lbtnchk_Get_Check_Button_Name(CheckButton* btn, char* out) {
	if (btn->name)
		strcpy(out, btn->name);
	else
		*out = '\0';
}

void lbtnchk_Set_Check_Button_State(CheckButton* btn, int16_t state) {
	btn->state = state;
	linpattr_Refresh_Input(&btn->header);
}

int16_t lbtnchk_Get_Check_Button_State(CheckButton* btn) { return btn->state; }
