#include <stdlib.h>
#include <string.h>

#include <landru/btnpush.h>
#include <landru/dirty.h>
#include <landru/fourcc.h>
#include <landru/inpattr.h>
#include <landru/memptr.h>
#include <landru/rect.h>
#include <landru/style.h>

// GLOBAL: TIE 0xD3018
static bool push_button_module_gbl;

void lbtnpush_Create_Button_Module(void) {
	linput_Create_Input_Type(FOURCC_BUTN, (void*)lbtnpush_Generate_Button, (void*)lbtnpush_Free_Button);
	linput_Create_Input_Type(FOURCC_SBTN, (void*)lbtnpush_Generate_Button, (void*)lbtnpush_Free_Button);
	push_button_module_gbl = true;
}

void lbtnpush_Destroy_Button_Module(void) {
	if (push_button_module_gbl) {
		linput_Destroy_Input_Type(FOURCC_SBTN);
		linput_Destroy_Input_Type(FOURCC_BUTN);
		push_button_module_gbl = false;
	}
}

PushButton* lbtnpush_Generate_Button(Input* parent, char* res_buffer, const char* name) {
	InputResourceHeader hdr;
	InputResourceHeader_decode(&hdr, (const uint8_t*)res_buffer);

	PushButton* btn;
	if (hdr.res_type == FOURCC_BUTN)
		btn = lbtnpush_Alloc_Button(parent, &hdr.frame, hdr.zinput, 0, name, hdr.id);
	else
		btn = lbtnpush_Alloc_Small_Button(parent, &hdr.frame, hdr.zinput, 0, name, hdr.id);

	if (!btn)
		return NULL;

	btn->header.alignment = hdr.window_flags;
	btn->header.flags = hdr.state_flags | INPUT_REFRESH;
	btn->header.type = hdr.res_type;
	btn->header.var1 = hdr.exit_pending;
	btn->header.var2 = hdr.exit_code;

	return btn;
}

PushButton* lbtnpush_Alloc_Button(Input* parent, Rect* frame, int16_t zinput, InputUserFunc callback,
								  const char* name, int16_t id) {
	PushButton* btn =
		(PushButton*)linput_Alloc_Dialog_Input(parent, frame, zinput, sizeof(PushButton) - sizeof(Input));
	if (btn) {
		lbtnpush_Init_Button(btn, callback, name, id);
		btn->header.type = FOURCC_BUTN;
	}
	return btn;
}

PushButton* lbtnpush_Alloc_Small_Button(Input* parent, Rect* frame, int16_t zinput, InputUserFunc callback,
										const char* name, int16_t id) {
	PushButton* btn = lbtnpush_Alloc_Button(parent, frame, zinput, callback, name, id);
	if (btn)
		btn->header.type = FOURCC_SBTN;
	return btn;
}

int16_t lbtnpush_Free_Button(PushButton* btn) {
	if (btn->name) {
		free(btn->name);
		btn->name = NULL;
	}
	return 1;
}

void lbtnpush_Init_Button(PushButton* btn, InputUserFunc callback, const char* name, int16_t id) {
	linpattr_Set_Input_Draw_Function(&btn->header, lbtnpush_idraw_Button);
	linpattr_Set_Input_Update_Function(&btn->header, lbtnpush_iupdate_Button);
	linpattr_Set_Input_User_Function(&btn->header, callback);
	btn->header.mouseUsage = downMoveUpInput;
	btn->header.id = id;

	if (name)
		btn->name = lmemptr_Duplicate_String(name);
	else
		btn->name = NULL;

	btn->pressed = 0;
}

void lbtnpush_idraw_Button(Input* input, Rect* paint_rect, Rect* dirty_rect, int16_t do_draw) {
	PushButton* btn = (PushButton*)input;
	if (!do_draw)
		return;

	/* Paint button background — large vs small dispatch */
	if (btn->header.type == FOURCC_BUTN)
		lstyle_Style_Paint_Button(paint_rect, btn->pressed);
	else
		lstyle_Style_Paint_Border(paint_rect, btn->pressed);

	/* Draw centered label text */
	if (btn->name) {
		if (btn->header.type == FOURCC_BUTN)
			lstyle_Style_Button_Text(btn->name, paint_rect, btn->pressed);
		else
			lstyle_Style_Small_Button_Text(btn->name, paint_rect, btn->pressed);
	}

	if (linpattr_Is_Input_Dirty(&btn->header))
		ldirty_Dirty_Rect(dirty_rect);
}

int16_t lbtnpush_iupdate_Button(Input* input, Rect* paint_rect, Rect* clip, int16_t phase, uint8_t mouse_l,
								uint8_t mouse_r, int16_t x, int16_t y) {
	PushButton* btn = (PushButton*)input;
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
		}
	}

	linpattr_Refresh_Input(&btn->header);
	/* Binary XBTNPUSH_iupdate_Button always returns 1 (0xad3e5: mov eax,1).
	 * XINPCALL_Update_Mouse_Down reads this as "update handled" — non-zero
	 * arms Set_InputActive_Down, which grabs the input so mouse_state=3
	 * (release) dispatches back here. Returning 0 made the dispatcher take
	 * the "nobody handled it" path and Set_InputActive_Ignore, silently
	 * eating the release event. */
	return 1;
}

void lbtnpush_Set_Button_Name(PushButton* btn, const char* name) {
	free(btn->name);
	btn->name = lmemptr_Duplicate_String(name);
	linpattr_Refresh_Input(&btn->header);
}

void lbtnpush_Get_Button_Name(PushButton* btn, char* out) {
	if (btn->name)
		strcpy(out, btn->name);
	else
		*out = '\0';
}
