#include <stddef.h>

#include <landru/inpattr.h>
#include <landru/inpcall.h>
#include <landru/rect.h>
#include <landru/view.h>

/* --- Visibility --- */

void linpattr_Show_Input(Input* input) { input->flags |= INPUT_VISIBLE; }
void linpattr_Hide_Input(Input* input) { input->flags &= ~INPUT_VISIBLE; }
int16_t linpattr_Is_Input_Visible(Input* input) { return input->flags & INPUT_VISIBLE; }

/* --- Active --- */

void linpattr_Activate_Input(Input* input) { input->flags |= INPUT_ACTIVE; }
void linpattr_Deactivate_Input(Input* input) { input->flags &= ~INPUT_ACTIVE; }
int16_t linpattr_Is_Input_Active(Input* input) { return input->flags & INPUT_ACTIVE; }

/* --- Start / Stop (visible + active combined) --- */

void linpattr_Start_Input(Input* input) { input->flags |= (INPUT_VISIBLE | INPUT_ACTIVE); }
void linpattr_Stop_Input(Input* input) { input->flags &= ~(INPUT_VISIBLE | INPUT_ACTIVE); }

/* --- Dirty --- */

void linpattr_Dirty_Input(Input* input) { input->flags |= INPUT_DIRTY; }
void linpattr_Non_Dirty_Input(Input* input) { input->flags &= ~INPUT_DIRTY; }
int16_t linpattr_Is_Input_Dirty(Input* input) { return input->flags & INPUT_DIRTY; }

/* --- Refresh --- */

void linpattr_Refresh_Input(Input* input) { input->flags |= INPUT_REFRESH; }
void linpattr_Non_Refresh_Input(Input* input) { input->flags &= ~INPUT_REFRESH; }
int16_t linpattr_Is_Input_Refresh(Input* input) { return input->flags & INPUT_REFRESH; }

/* --- Refreshable --- */

void linpattr_Refreshable_Input(Input* input) { input->flags |= INPUT_REFRESHABLE; }
void linpattr_Non_Refreshable_Input(Input* input) { input->flags &= ~INPUT_REFRESHABLE; }
int16_t linpattr_Is_Input_Refreshable(Input* input) { return input->flags & INPUT_REFRESHABLE; }

/* --- Discard data --- */

void linpattr_Discard_Input_Data(Input* input) { input->flags |= INPUT_DISCARD_DATA; }
void linpattr_Non_Discard_Input_Data(Input* input) { input->flags &= ~INPUT_DISCARD_DATA; }
int16_t linpattr_Is_Discard_Input_Data(Input* input) { return input->flags & INPUT_DISCARD_DATA; }

/* --- Selected (read-and-clear via Get_Input_Selected) --- */

void linpattr_Selected_Input(Input* input) { input->flags |= INPUT_SELECTED; }
void linpattr_Non_Selected_Input(Input* input) { input->flags &= ~INPUT_SELECTED; }
int16_t linpattr_Is_Input_Selected(Input* input) { return input->flags & INPUT_SELECTED; }

int16_t linpattr_Get_Input_Selected(Input* input) {
	if (linpattr_Is_Input_Selected(input)) {
		linpattr_Non_Selected_Input(input);
		return 1;
	}
	return 0;
}

/* --- User flags 1 & 2 (return boolean 0/1, not raw mask) --- */

void linpattr_Set_Input_Flag1(Input* input) { input->flags |= INPUT_FLAG1; }
void linpattr_Clear_Input_Flag1(Input* input) { input->flags &= ~INPUT_FLAG1; }
int16_t linpattr_Is_Input_Flag1(Input* input) { return (input->flags & INPUT_FLAG1) != 0; }

void linpattr_Set_Input_Flag2(Input* input) { input->flags |= INPUT_FLAG2; }
void linpattr_Clear_Input_Flag2(Input* input) { input->flags &= ~INPUT_FLAG2; }
int16_t linpattr_Is_Input_Flag2(Input* input) { return (input->flags & INPUT_FLAG2) != 0; }

/* --- Frame --- */

void linpattr_Set_Input_Frame(Input* input, Rect* src) { lrect_Copy_Rect(&input->frame, src); }
void linpattr_Get_Input_Frame(Input* input, Rect* dst) { lrect_Copy_Rect(dst, &input->frame); }

/* --- ZInput --- */

void linpattr_Set_Input_ZInput(Input* input, int16_t zinput) {
	input->zinput = zinput;
	zinput_build_gbl = 1;
}

int16_t linpattr_Get_Input_ZInput(Input* input, uint16_t* out) {
	*out = input->zinput;
	return input->zinput;
}

/* --- Alignment (encoded in window_flags low byte) --- */

void linpattr_Set_Input_Allign(Input* input, uint8_t halign, uint8_t valign) {
	uint16_t keep = input->alignment & ~(INPUT_HALIGN_1 | INPUT_HALIGN_2 | INPUT_VALIGN_1 | INPUT_VALIGN_2);
	input->alignment = keep;

	if (halign == 1)
		input->alignment |= INPUT_HALIGN_1;
	else if (halign == 2)
		input->alignment |= INPUT_HALIGN_2;

	if (valign == 1)
		input->alignment |= INPUT_VALIGN_1;
	else if (valign == 2)
		input->alignment |= INPUT_VALIGN_2;
}

void linpattr_Get_Input_Allign(Input* input, uint8_t* out_h, uint8_t* out_v) {
	uint16_t h_bits = input->alignment & (INPUT_HALIGN_1 | INPUT_HALIGN_2);
	if (h_bits == INPUT_HALIGN_1)
		*out_h = 1;
	else if (h_bits == INPUT_HALIGN_2)
		*out_h = 2;
	else
		*out_h = 0;

	uint16_t v_bits = input->alignment & (INPUT_VALIGN_1 | INPUT_VALIGN_2);
	if (v_bits == INPUT_VALIGN_1)
		*out_v = 1;
	else if (v_bits == INPUT_VALIGN_2)
		*out_v = 2;
	else
		*out_v = 0;
}

/* --- Function pointers --- */

void linpattr_Set_Input_Draw_Function(Input* input, InputDrawFunc fn) { input->draw = fn; }
void linpattr_Get_Input_Draw_Function(Input* input, InputDrawFunc* out) { *out = input->draw; }
void linpattr_Set_Input_Update_Function(Input* input, InputUpdateFunc fn) { input->update = fn; }
void linpattr_Get_Input_Update_Function(Input* input, InputUpdateFunc* out) { *out = input->update; }
void linpattr_Set_Input_User_Function(Input* input, InputUserFunc fn) { input->user = fn; }
void linpattr_Get_Input_User_Function(Input* input, InputUserFunc* out) { *out = input->user; }

/* --- Window flags --- */

void linpattr_Set_Input_Window(Input* input) { input->alignment |= INPUT_WINDOW; }
void linpattr_Clear_Input_Window(Input* input) { input->alignment &= ~INPUT_WINDOW; }
int16_t linpattr_Is_Input_Window(Input* input) { return (input->alignment & INPUT_WINDOW) != 0; }

void linpattr_Set_Input_Window_Active(Input* input) { input->alignment |= INPUT_WIN_ACTIVE; }
void linpattr_Clear_Input_Window_Active(Input* input) { input->alignment &= ~INPUT_WIN_ACTIVE; }
int16_t linpattr_Is_Input_Window_Active(Input* input) { return (input->alignment & INPUT_WIN_ACTIVE) != 0; }

/* --- Activate_Window: move input to front of parent view's child list --- */

void linpattr_Activate_Window(Input* input) {
	Input* parent = linpcall_Find_View_Parent_For_Input(input);
	if (!parent)
		return;

	/* Unlink input from its current position in the child list */
	if (input != parent->child) {
		Input* prev = parent->child;
		while (prev->next != input)
			prev = prev->next;
		prev->next = input->next;
		input->next = parent->child;
		parent->child = input;
	}

	/* Renumber zinput from 100 upward, clear window_active on all */
	int16_t z = 100;
	for (Input* cur = input; cur; cur = cur->next) {
		linpattr_Set_Input_ZInput(cur, z++);
		linpattr_Clear_Input_Window_Active(cur);
	}
	linpattr_Set_Input_Window_Active(input);
	linpattr_Refresh_Input(input);
}

/* --- Move --- */

void linpattr_Move_Input(Input* input, int16_t x, int16_t y) {
	lrect_Offset_Rect(&input->frame, x - input->frame.left, y - input->frame.top);
}

/* --- Z-order sorting --- */

void linpattr_Check_Input_ZInputs(void) {
	if (zinput_build_gbl) {
		zinput_build_gbl = 0;
		lview_Get_Current_View();
		Input* head = input_list_gbl;
		linpattr_Sort_Input_ZInput_List(&head);
	}
}

void linpattr_Sort_Input_ZInputs(void) {
	lview_Get_Current_View();
	Input* head = input_list_gbl;
	linpattr_Sort_Input_ZInput_List(&head);
}

Input* linpattr_Sort_Input_ZInput_List(Input** head) {
	/* Recurse into children first.
	   The binary does NOT write the sorted child head back to cur->child —
	   the local is discarded. This matches the original behavior. */
	for (Input* iter = *head; iter; iter = iter->next) {
		if (iter->child) {
			Input* child_head = iter->child;
			linpattr_Sort_Input_ZInput_List(&child_head);
		}
	}

	/* Insertion sort by descending zinput */
	Input* sorted = NULL;
	Input* unsorted = *head;
	while (unsorted) {
		Input* cur = unsorted;
		unsorted = unsorted->next;
		cur->next = NULL;

		Input* scan = sorted;
		Input* prev = NULL;
		while (scan && scan->zinput >= cur->zinput) {
			prev = scan;
			scan = scan->next;
		}

		if (prev) {
			cur->next = prev->next;
			prev->next = cur;
		} else {
			cur->next = sorted;
			sorted = cur;
		}
	}

	*head = sorted;
	return sorted;
}
