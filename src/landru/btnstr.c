#include <ctype.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include <landru/btnstr.h>
#include <landru/dirty.h>
#include <landru/font.h>
#include <landru/inpattr.h>
#include <landru/inpcall.h>
#include <landru/io.h>
#include <landru/paint.h>
#include <landru/rect.h>
#include <landru/remap.h>
#include <landru/style.h>

#include "binio.h"
#include <landru/fourcc.h>

#define STRING_BUF_SIZE 256

// GLOBAL: TIE 0xD3020
static bool string_button_module_gbl;

void lbtnstr_Create_String_Button_Module(void) {
	linput_Create_Input_Type(FOURCC_STRG, (void*)lbtnstr_Generate_String_Button,
							 (void*)lbtnstr_Free_String_Button);
	string_button_module_gbl = true;
}

void lbtnstr_Destroy_String_Button_Module(void) {
	if (string_button_module_gbl) {
		linput_Destroy_Input_Type(FOURCC_STRG);
		string_button_module_gbl = false;
	}
}

StringButton* lbtnstr_Generate_String_Button(Input* parent, char* res_buffer, const char* initial_str) {
	InputResourceHeader hdr;
	InputResourceHeader_decode(&hdr, (const uint8_t*)res_buffer);

	StringButton* btn =
		lbtnstr_Alloc_String_Button(parent, &hdr.frame, hdr.zinput, NULL, initial_str, 3, hdr.id);
	if (!btn)
		return NULL;

	btn->header.alignment = hdr.window_flags;
	/* Set REFRESH + KEY_GROUP, clear KEY_FOCUSED */
	btn->header.flags = (hdr.state_flags | INPUT_REFRESH | INPUT_KEY_GROUP) & ~INPUT_KEY_FOCUSED;
	btn->header.var1 = hdr.exit_pending;
	btn->header.var2 = hdr.exit_code;

	const uint8_t* ext = (const uint8_t*)res_buffer + INPUTRESOURCEHEADER_DISK_SIZE;
	if (br_i16le(ext + 0))
		btn->filter_type = ((const uint8_t*)res_buffer)[27];

	return btn;
}

StringButton* lbtnstr_Alloc_String_Button(Input* parent, Rect* frame, int16_t zinput, InputUserFunc callback,
										  const char* initial_str, uint8_t filter_type, int16_t id) {
	StringButton* btn =
		(StringButton*)linput_Alloc_Dialog_Input(parent, frame, zinput, sizeof(StringButton) - sizeof(Input));
	if (!btn)
		return NULL;

	linpattr_Set_Input_Draw_Function(&btn->header, lbtnstr_idraw_String_Button);
	linpattr_Set_Input_Update_Function(&btn->header, lbtnstr_iupdate_String_Button);
	linpattr_Set_Input_User_Function(&btn->header, callback);
	/* Set KEY_GROUP, clear KEY_FOCUSED */
	btn->header.flags = (btn->header.flags & ~(INPUT_KEY_FOCUSED | INPUT_KEY_GROUP)) | INPUT_KEY_GROUP;
	btn->header.id = id;
	btn->header.type = FOURCC_STRG;

	btn->string = (char*)malloc(STRING_BUF_SIZE);
	if (btn->string && initial_str) {
		int16_t i = 0;
		while (initial_str[i] && i < STRING_BUF_SIZE - 1) {
			btn->string[i] = initial_str[i];
			i++;
		}
		btn->string[i] = '\0';
	} else if (btn->string) {
		btn->string[0] = '\0';
	}

	btn->filter_type = filter_type;

	return btn;
}

int16_t lbtnstr_Free_String_Button(StringButton* btn) {
	if (btn->string) {
		free(btn->string);
		btn->string = NULL;
	}
	return 1;
}

void lbtnstr_idraw_String_Button(Input* input, Rect* paint_rect, Rect* dirty_rect, int16_t refresh) {
	StringButton* btn = (StringButton*)input;
	int16_t should_dirty = refresh;

	if (refresh) {
		lstyle_Style_Paint_TextField(paint_rect);
		if (btn->string) {
			int16_t color = lstyle_Get_Style_Down_Color();
			lfont_Print_Clipped_Text(btn->string, paint_rect->left + 3, paint_rect->top + 3, 0, color);
		}
	}

	/* Blinking cursor when active and key-focused */
	if (linpattr_Is_Input_Active(&btn->header) && (btn->header.flags & INPUT_KEY_FOCUSED)) {
		int16_t str_w = 0;
		if (btn->string)
			str_w = lfont_Get_String_Width(btn->string);

		int16_t cursor_color;
		if (lio_Blink())
			cursor_color = lstyle_Get_Style_Down_Color();
		else
			cursor_color = lremap_Get_Remap(REMAP_GRAY_0);

		lpaint_Horiz_Clipped_Line(paint_rect->left + str_w + 4, paint_rect->top + 11, 6, cursor_color);
		should_dirty = 1;
	}

	if (linpattr_Is_Input_Dirty(&btn->header)) {
		if (should_dirty)
			ldirty_Dirty_Rect(dirty_rect);
	}
}

int16_t lbtnstr_iupdate_String_Button(Input* input, Rect* frame, Rect* clip, int16_t key, uint8_t mouse_l,
									  uint8_t mouse_r, int16_t x, int16_t y) {
	StringButton* btn = (StringButton*)input;
	(void)frame;
	(void)clip;
	(void)x;
	(void)y;

	if (!btn->string)
		return 0;

	int16_t updated = 0;

	if (key) {
		/* Backspace (8) or Delete (0x5300) → truncate */
		if (key == 8 || key == 0x5300) {
			int len = (int16_t)strlen(btn->string);
			if (len > 0)
				btn->string[len - 1] = '\0';
			updated = 1;
		} else {
			/* Filter by type and append */
			switch (btn->filter_type) {
				case 0: /* Printable ASCII */
					if (key >= 32 && key < 127)
						updated = lbtnstr_Add_Key_To_String(btn->string, key);
					break;
				case 1: /* Alpha */
					if (isalpha((unsigned char)key))
						updated = lbtnstr_Add_Key_To_String(btn->string, key);
					break;
				case 2: /* Numeric (with optional leading minus) */
					if (key == '-' && strlen(btn->string) == 0)
						updated = lbtnstr_Add_Key_To_String(btn->string, key);
					else if (isdigit((unsigned char)key))
						updated = lbtnstr_Add_Key_To_String(btn->string, key);
					break;
				case 3: /* Alphanumeric + underscore + hyphen */
					if (key > 32 && key < 127 &&
						(key == '_' || key == '-' || isdigit((unsigned char)key) ||
						 isalpha((unsigned char)key)))
						updated = lbtnstr_Add_Key_To_String(btn->string, key);
					break;
			}
		}
	} else if (mouse_l == 1 || mouse_r == 1) {
		updated = 1;
		linpcall_Select_View_Shared_Key_Input(&btn->header);
	}

	if (updated) {
		linpattr_Refresh_Input(&btn->header);
		linpattr_Selected_Input(&btn->header);
	}
	return updated;
}

int16_t lbtnstr_Add_Key_To_String(char* str, int16_t key) {
	int len = (int16_t)strlen(str);
	if (len >= STRING_BUF_SIZE - 1)
		return 0;
	str[len] = (char)key;
	str[len + 1] = '\0';
	return 1;
}

void lbtnstr_Set_String_Button_Name(StringButton* btn, const char* name) {
	if (btn->string) {
		if (name)
			strcpy(btn->string, name);
		else
			btn->string[0] = '\0';
		linpattr_Refresh_Input(&btn->header);
	}
}

void lbtnstr_Get_String_Button_Name(StringButton* btn, char* out) {
	lbtnstr_Get_Clipped_String_Button_Name(btn, out, STRING_BUF_SIZE - 1);
}

void lbtnstr_Get_Clipped_String_Button_Name(StringButton* btn, char* out, int16_t max_len) {
	if (btn->string) {
		int16_t i = 0;
		while (i < max_len - 1 && btn->string[i]) {
			out[i] = btn->string[i];
			i++;
		}
		out[i] = '\0';
	} else {
		*out = '\0';
	}
}
