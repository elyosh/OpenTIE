#include <stddef.h>
#include <stdlib.h>

#include <landru/canvas.h>
#include <landru/dirty.h>
#include <landru/inpattr.h>
#include <landru/inpcall.h>
#include <landru/input.h>
#include <landru/memptr.h>
#include <landru/rect.h>
#include <landru/style.h>

#include "binio.h"
#include <landru/file.h>
#include <landru/fourcc.h>
#include <landru/res.h>

/* Static buffers for resource deserialization */
static int32_t input_res_buffer[64];
static uint8_t input_str_buffer[256];

/* Module globals */
// GLOBAL: TIE 0xFC10C
InputType* input_type_gbl = NULL;
// GLOBAL: TIE 0xFC104
Input* input_list_gbl = NULL;
// GLOBAL: TIE 0xFC108
Input* active_input_list_gbl = NULL;
// GLOBAL: TIE 0xD3008
bool input_module_gbl = false;
// GLOBAL: TIE 0xFC0FC
Rect input_frame_gbl;
// GLOBAL: TIE 0xFC110
bool refresh_inputs_gbl = false;
// GLOBAL: TIE 0xFC114
int16_t zinput_build_gbl = 0;

/* --- Module lifecycle --- */

void linput_Create_Input_Module(void) {
	input_type_gbl = NULL;
	input_list_gbl = NULL;
	active_input_list_gbl = NULL;
	input_module_gbl = true;
	lcanvas_Get_Drawing_Canvas_Bounds(&input_frame_gbl);
	if (linput_Create_Input_Type(FOURCC_INPT, NULL, NULL)) {
		if (!linput_Create_Input_Type(FOURCC_BACK, NULL, NULL)) {
			linput_Destroy_Input_Type(FOURCC_INPT);
			input_module_gbl = false;
		}
	}
}

void linput_Destroy_Input_Module(void) {
	if (input_module_gbl) {
		linput_Destroy_Input_Type(FOURCC_INPT);
		linput_Destroy_Input_Type(FOURCC_BACK);
		input_module_gbl = false;
	}
}

/* --- Input type registry --- */

bool linput_Create_Input_Type(uint32_t type, void* create_handler, void* free_handler) {
	InputType* t = lmemptr_Alloc_Clear_System_Pointer(sizeof(InputType));
	if (!t)
		return false;
	t->type = type;
	t->func1 = create_handler;
	t->func2 = free_handler;
	t->next = input_type_gbl;
	input_type_gbl = t;
	return true;
}

void linput_Destroy_Input_Type(uint32_t type) {
	InputType* cur = input_type_gbl;
	InputType* prev = NULL;
	while (cur) {
		if (cur->type == type)
			break;
		prev = cur;
		cur = cur->next;
	}
	if (cur) {
		if (prev)
			prev->next = cur->next;
		else
			input_type_gbl = input_type_gbl->next;
		lmemptr_Free_System_Pointer(cur);
	}
}

InputType* linput_Find_Input_Type(uint32_t type) {
	for (InputType* t = input_type_gbl; t; t = t->next) {
		if (t->type == type)
			return t;
	}
	return NULL;
}

/* --- Active input list --- */

void linput_Set_Active_Input_List(Input* input) { active_input_list_gbl = input; }

Input* linput_Get_Active_Input_List(void) {
	if (active_input_list_gbl)
		return active_input_list_gbl;
	return input_list_gbl;
}

/* --- Resource loading --- */

void* linput_Res_Input_Handle(const char* name) { return lres_Load_Resource_Data(FOURCC_INPT, name); }

Input* linput_Generate_Input_Handle(void* handle, int16_t tree_index) {
	return linput_Generate_Callback_Input_Handle(handle, tree_index, NULL);
}

typedef Input* (*InputCallbackFunc)(Input* parent, void* res_buffer, uint8_t* str_buffer);

Input* linput_Generate_Callback_Input_Handle(void* handle, int16_t tree_index, void* callback) {
	InputCallbackFunc cb = (InputCallbackFunc)callback;
	uint8_t* buf = (uint8_t*)handle;
	int offset = 0;
	Input* last_input = NULL;

	lfile_Read_Word_From_Buffer(buf, &offset); /* skip version */
	int16_t tree_count = lfile_Read_Word_From_Buffer(buf, &offset);

	for (int16_t t = 0; t < tree_count; t++) {
		if (t == tree_index) {
			Input* cur_input = NULL;
			int16_t rec_count = lfile_Read_Word_From_Buffer(buf, &offset);

			for (int16_t r = 0; r < rec_count; r++) {
				uint8_t name_data[12];
				lfile_Read_Data_From_Buffer(buf, name_data, &offset, 8);
				name_data[9] = 0;

				int16_t res_size = lfile_Read_Word_From_Buffer(buf, &offset);
				lfile_Read_Data_From_Buffer(buf, input_res_buffer, &offset, res_size);

				int16_t str_size = lfile_Read_Word_From_Buffer(buf, &offset);
				if (str_size)
					lfile_Read_Data_From_Buffer(buf, input_str_buffer, &offset, str_size);
				else
					input_str_buffer[0] = 0;

				cur_input = NULL;
				if (cb)
					cur_input = cb(NULL, input_res_buffer, input_str_buffer);

				if (!cur_input) {
					uint32_t res_type = *(uint32_t*)input_res_buffer;
					InputType* type = linput_Find_Input_Type(res_type);
					if (type && type->func1) {
						typedef Input* (*CreateFunc)(Input*, void*, uint8_t*);
						cur_input = ((CreateFunc)type->func1)(NULL, input_res_buffer, input_str_buffer);
					} else {
						cur_input = linput_Generate_Input(NULL, (char*)input_res_buffer);
					}
				}

				if (cur_input) {
					if (cb)
						cb(cur_input, input_res_buffer, input_str_buffer);
					linput_Generate_Input_Branch(cur_input, handle, &offset, callback, 1);
				}
			}
			last_input = cur_input;
		} else {
			/* Skip this tree */
			int16_t rec_count = lfile_Read_Word_From_Buffer(buf, &offset);
			for (int16_t r = 0; r < rec_count; r++) {
				uint8_t name_data[12];
				lfile_Read_Data_From_Buffer(buf, name_data, &offset, 8);
				name_data[9] = 0;

				int16_t res_size = lfile_Read_Word_From_Buffer(buf, &offset);
				lfile_Read_Data_From_Buffer(buf, input_res_buffer, &offset, res_size);

				int16_t str_size = lfile_Read_Word_From_Buffer(buf, &offset);
				if (str_size)
					lfile_Read_Data_From_Buffer(buf, input_str_buffer, &offset, str_size);
				else
					input_str_buffer[0] = 0;

				linput_Generate_Input_Branch(NULL, handle, &offset, callback, 0);
			}
		}
	}
	return last_input;
}

Input* linput_Generate_Input_Branch(Input* parent, void* handle, int* offset, void* callback,
									int16_t generate) {
	InputCallbackFunc cb = (InputCallbackFunc)callback;
	uint8_t* buf = (uint8_t*)handle;
	Input* cur_input = NULL;

	int16_t rec_count = lfile_Read_Word_From_Buffer(buf, offset);

	for (int16_t i = 0; i < rec_count; i++) {
		uint8_t name_data[12];
		lfile_Read_Data_From_Buffer(buf, name_data, offset, 8);
		name_data[9] = 0;

		int16_t res_size = lfile_Read_Word_From_Buffer(buf, offset);
		lfile_Read_Data_From_Buffer(buf, input_res_buffer, offset, res_size);

		int16_t str_size = lfile_Read_Word_From_Buffer(buf, offset);
		if (str_size)
			lfile_Read_Data_From_Buffer(buf, input_str_buffer, offset, str_size);
		else
			input_str_buffer[0] = 0;

		if (generate) {
			cur_input = NULL;
			if (cb)
				cur_input = cb(NULL, input_res_buffer, input_str_buffer);

			if (!cur_input) {
				uint32_t res_type = *(uint32_t*)input_res_buffer;
				InputType* type = linput_Find_Input_Type(res_type);
				if (type && type->func1) {
					typedef Input* (*CreateFunc)(Input*, void*, uint8_t*);
					cur_input = ((CreateFunc)type->func1)(parent, input_res_buffer, input_str_buffer);
				} else {
					cur_input = linput_Generate_Input(parent, (char*)input_res_buffer);
				}
			}

			if (cur_input) {
				if (cb)
					cb(cur_input, input_res_buffer, input_str_buffer);
				linput_Generate_Input_Branch(cur_input, handle, offset, callback, 1);
			}
		} else {
			linput_Generate_Input_Branch(NULL, handle, offset, callback, 0);
		}
	}
	return cur_input;
}

/* On-disk codec for the 25-byte LFD input-resource header. Layout:
 *   +0x00 res_type (u32)
 *   +0x04 frame.top, .left, .bottom, .right (4 x i16)
 *   +0x0C zinput (i16)
 *   +0x0E window_flags (u16)
 *   +0x10 state_flags (u16)
 *   +0x12 id (i16)
 *   +0x14 pad (u8)
 *   +0x15 exit_pending (i16, ODD offset)
 *   +0x17 exit_code    (i16, ODD offset)
 * Read-only at runtime, hence no encoder. */
void InputResourceHeader_decode(InputResourceHeader* dst, const uint8_t* src) {
	dst->res_type = br_u32le(src + 0x00);
	dst->frame.top = br_i16le(src + 0x04);
	dst->frame.left = br_i16le(src + 0x06);
	dst->frame.bottom = br_i16le(src + 0x08);
	dst->frame.right = br_i16le(src + 0x0A);
	dst->zinput = br_i16le(src + 0x0C);
	dst->window_flags = br_u16le(src + 0x0E);
	dst->state_flags = br_u16le(src + 0x10);
	dst->id = br_i16le(src + 0x12);
	dst->pad = src[0x14];
	dst->exit_pending = br_i16le(src + 0x15);
	dst->exit_code = br_i16le(src + 0x17);
}

Input* linput_Generate_Input(Input* parent, char* buffer) {
	InputResourceHeader hdr;
	InputResourceHeader_decode(&hdr, (const uint8_t*)buffer);
	Input* input = linput_Alloc_Dialog_Input(parent, &hdr.frame, hdr.zinput, 0);
	if (!input)
		return NULL;

	input->type = hdr.res_type;
	input->alignment = hdr.window_flags;
	input->flags = hdr.state_flags | INPUT_REFRESH;
	/* The id at offset 18 (= 0x12) and mouseUsage at offset 20 (= 0x14)
	 * are read directly from the on-disk image; the binary uses these
	 * raw byte reads as a side channel. The id matches hdr.id; the
	 * mouseUsage byte is the 'pad' field in the header struct. */
	input->id = hdr.id;
	input->mouseUsage = (MouseUsage)hdr.pad;
	input->var1 = hdr.exit_pending;
	input->var2 = hdr.exit_code;
	linpattr_Set_Input_Draw_Function(input, linput_idraw_Input);
	return input;
}

/* --- Init / Alloc --- */

void linput_Init_Input(Input* input, Rect* frame, int16_t z) {
	input->child = NULL;
	input->type = FOURCC_INPT;
	input->mouseUsage = downUpInput;
	input->next = NULL;
	input->frame = *frame;
	input->flags = INPUT_VISIBLE | INPUT_ACTIVE | INPUT_DIRTY | INPUT_REFRESH | INPUT_KEY_FOCUSED;
	input->alignment = 0;
	input->zinput = z;
	input->id = 0;
}

Input* linput_Alloc_Input(Input* parent, Rect* frame, int16_t z, int16_t extend) {
	Input* input = lmemptr_Alloc_Clear_System_Pointer(sizeof(Input) + extend);
	if (!input)
		return NULL;
	linput_Init_Input(input, frame, z);
	if (parent)
		linput_Add_Input_To_Parent(parent, input);
	else
		linput_Add_Input_To_System(input);
	return input;
}

Input* linput_Alloc_Dialog_Input(Input* parent, Rect* frame, int16_t z, int16_t extend) {
	Input* input = lmemptr_Alloc_Clear_System_Pointer(sizeof(Input) + extend);
	if (!input)
		return NULL;
	linput_Init_Input(input, frame, z);
	if (parent)
		linput_Add_Input_To_Parent(parent, input);
	return input;
}

/* --- Free --- */

void linput_Free_Input_Lists(void) {
	Input* cur = input_list_gbl;
	while (cur) {
		Input* next = cur->next;
		if (cur->child) {
			linput_Free_Inputs(cur->child);
			cur->child = NULL;
		}
		linput_Free_Input(cur);
		cur = next;
	}
	input_list_gbl = NULL;
}

void linput_Free_Inputs(Input* input) {
	while (input) {
		Input* next = input->next;
		if (input->child) {
			linput_Free_Inputs(input->child);
			input->child = NULL;
		}
		linput_Free_Input(input);
		input = next;
	}
}

void linput_Free_Input(Input* input) {
	/* Find the InputType for this input's res_type */
	InputType* type = NULL;
	for (InputType* t = input_type_gbl; t; t = t->next) {
		if (t->type == input->type) {
			type = t;
			break;
		}
	}

	/* If no type, no free_handler, or free_handler returns non-zero: do default cleanup */
	typedef int16_t (*FreeFunc)(Input*);
	if (!type || !type->func2 || ((FreeFunc)type->func2)(input)) {
		linput_Free_Input_Data(input);
		lmemptr_Free_System_Pointer(input);
	}
}

void linput_Free_Input_Data(Input* input) {
	if (linpattr_Is_Discard_Input_Data(input)) {
		if (input->varptr) {
			lmemptr_Free_System_Pointer(input->varptr);
		}
		if (input->varhdl) {
			free(input->varhdl);
		}
	}
	input->varhdl = NULL;
	input->varptr = NULL;
}

/* --- Draw --- */

void linput_idraw_Input(Input* input, Rect* frame, Rect* rect, int16_t refresh) {
	if (!refresh)
		return;

	if (input->type == FOURCC_BACK)
		lstyle_Style_Paint_TextField(frame);
	else if (input->type == FOURCC_INPT)
		lstyle_Style_Paint_Border(frame, 0);

	if (linpattr_Is_Input_Dirty(input))
		ldirty_Dirty_Rect(rect);
}

/* --- Add/Remove (sorted by descending zinput) --- */

void linput_Add_Input_To_System(Input* input) {
	Input* cur = input_list_gbl;
	Input* prev = NULL;
	while (cur) {
		if (input->zinput >= cur->zinput)
			break;
		prev = cur;
		cur = cur->next;
	}
	input->next = cur;
	if (prev)
		prev->next = input;
	else
		input_list_gbl = input;
}

void linput_Add_Input_To_Parent(Input* parent, Input* child) {
	Input* cur = parent->child;
	Input* prev = NULL;
	while (cur) {
		if (child->zinput >= cur->zinput)
			break;
		prev = cur;
		cur = cur->next;
	}
	child->next = cur;
	if (prev)
		prev->next = child;
	else
		parent->child = child;
}

void linput_Remove_Input_From_System(Input* input) {
	Input* cur = input_list_gbl;
	Input* prev = NULL;
	while (cur) {
		if (cur == input)
			break;
		prev = cur;
		cur = cur->next;
	}
	if (cur == input) {
		if (prev)
			prev->next = input->next;
		else
			input_list_gbl = input->next;
		input->next = NULL;
	}
}

void linput_Remove_Input_From_Parent(Input* parent, Input* input) {
	Input* cur = parent->child;
	Input* prev = NULL;
	while (cur) {
		if (cur == input)
			break;
		prev = cur;
		cur = cur->next;
	}
	if (cur == input) {
		if (prev)
			prev->next = input->next;
		else
			parent->child = input->next;
		input->next = NULL;
	}
}

/* --- System dispatchers --- */

void linput_Update_System_Inputs(void) { linpcall_Update_Inputs(input_list_gbl); }

void linput_User_System_Inputs(int32_t time) { linpcall_User_Inputs(input_list_gbl, time); }

void linput_Draw_System_Inputs(int16_t refresh) {
	int16_t flags = refresh | refresh_inputs_gbl;
	refresh_inputs_gbl = false;
	linpcall_Draw_Inputs(input_list_gbl, NULL, NULL, flags);
}

void linput_Refresh_System_Inputs(void) { refresh_inputs_gbl = true; }

void linput_Set_System_Input_Frame(Rect* frame) { lrect_Copy_Rect(&input_frame_gbl, frame); }

void linput_Get_System_Input_Frame(Rect* outFrame) { lrect_Copy_Rect(outFrame, &input_frame_gbl); }
