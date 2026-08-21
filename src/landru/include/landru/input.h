#ifndef LANDRU_INPUT_H
#define LANDRU_INPUT_H

#include <landru/rect.h>
#include <stdint.h>

typedef struct InputType InputType;
typedef struct Input Input;

/* Callback signatures for Input function pointers */
typedef void (*InputDrawFunc)(Input* input, Rect* paint_rect, Rect* clip, int16_t refresh);
typedef int16_t (*InputUpdateFunc)(Input* input, Rect* frame, Rect* clip, int16_t phase, uint8_t mouse_l,
								   uint8_t mouse_r, int16_t dx, int16_t dy);
typedef void (*InputUserFunc)(Input* input, int32_t context);

typedef enum MouseUsage {
	noInput = 0,
	downInput = 1,
	downUpInput = 2,
	downMoveUpInput = 3,
	allInput = 4
} MouseUsage;

struct InputType {
	uint32_t type;
	InputType* next;
	void* func1;
	void* func2;
};

enum InputWindowFlags {
	INPUT_HALIGN_1 = 0x0001,
	INPUT_HALIGN_2 = 0x0002,
	INPUT_VALIGN_1 = 0x0010,
	INPUT_VALIGN_2 = 0x0020,
	INPUT_WINDOW = 0x0100,
	INPUT_WIN_ACTIVE = 0x0200,
};

enum InputStateFlags {
	INPUT_VISIBLE = 0x0001,
	INPUT_ACTIVE = 0x0002,
	INPUT_DISCARD_DATA = 0x0004,
	INPUT_DIRTY = 0x0008,
	INPUT_REFRESHABLE = 0x0010,
	INPUT_REFRESH = 0x0020,
	INPUT_SELECTED = 0x0100,
	INPUT_KEY_FOCUSED = 0x0200,
	INPUT_KEY_GROUP = 0x0400,
	INPUT_FLAG1 = 0x4000,
	INPUT_FLAG2 = 0x8000,
};

struct Input {
	uint32_t type;
	Input* next;
	Input* child;
	Rect frame;
	uint16_t alignment;
	uint16_t flags;
	int16_t zinput;
	int16_t id;
	MouseUsage mouseUsage;
	int16_t var1;
	int16_t var2;
	void* varptr;
	void* varhdl;
	InputDrawFunc draw;
	InputUpdateFunc update;
	InputUserFunc user;
};

/* 25-byte resource header from LFD input records.
 *
 * The runtime struct is naturally aligned (the trailing exit_pending /
 * exit_code int16s land at odd offsets +0x15 / +0x17 in the on-disk
 * image, which forces 1-byte padding before exit_pending and 2-byte
 * trailing padding under natural alignment, growing sizeof from 25 to
 * 28). On-disk layout is the canonical 25-byte little-endian record
 * produced by the original DOS binary; loaders go through
 * InputResourceHeader_decode so BE / strict-align hosts get the same
 * in-memory values. Read-only at runtime, hence no encoder. */
typedef struct {
	uint32_t res_type;
	Rect frame;
	int16_t zinput;
	uint16_t window_flags;
	uint16_t state_flags;
	int16_t id;
	uint8_t pad;
	int16_t exit_pending;
	int16_t exit_code;
} InputResourceHeader;

#define INPUTRESOURCEHEADER_DISK_SIZE 25u

void InputResourceHeader_decode(InputResourceHeader* dst, const uint8_t* src);

extern Input* input_list_gbl;
extern int16_t zinput_build_gbl;

void linput_Create_Input_Module(void);
void linput_Destroy_Input_Module(void);
bool linput_Create_Input_Type(uint32_t type, void* create_handler, void* free_handler);
void linput_Destroy_Input_Type(uint32_t type);
InputType* linput_Find_Input_Type(uint32_t type);
void linput_Set_Active_Input_List(Input* input);
Input* linput_Get_Active_Input_List(void);
void* linput_Res_Input_Handle(const char* name);
Input* linput_Generate_Input_Handle(void* handle, int16_t tree_index);
Input* linput_Generate_Callback_Input_Handle(void* handle, int16_t tree_index, void* callback);
Input* linput_Generate_Input_Branch(Input* parent, void* handle, int* offset, void* callback,
									int16_t generate);
Input* linput_Generate_Input(Input* parent, char* buffer);
void linput_Init_Input(Input* input, Rect* frame, int16_t z);
Input* linput_Alloc_Input(Input* parent, Rect* frame, int16_t z, int16_t extend);
Input* linput_Alloc_Dialog_Input(Input* parent, Rect* frame, int16_t z, int16_t extend);
void linput_Free_Input_Lists(void);
void linput_Free_Inputs(Input* input);
void linput_Free_Input(Input* input);
void linput_Free_Input_Data(Input* input);
void linput_idraw_Input(Input* input, Rect* frame, Rect* rect, int16_t refresh);
void linput_Add_Input_To_System(Input* input);
void linput_Add_Input_To_Parent(Input* parent, Input* child);
void linput_Remove_Input_From_System(Input* input);
void linput_Remove_Input_From_Parent(Input* parent, Input* input);
void linput_Update_System_Inputs(void);
void linput_User_System_Inputs(int32_t time);
void linput_Draw_System_Inputs(int16_t refresh);
void linput_Refresh_System_Inputs(void);
void linput_Set_System_Input_Frame(Rect* frame);
void linput_Get_System_Input_Frame(Rect* outFrame);

#endif
