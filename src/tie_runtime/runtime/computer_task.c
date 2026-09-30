#include "tie_runtime/runtime/computer_task.h"
#include "tie/computer.h"
#include "tie_runtime/runtime/profile.h"
#include "tie_runtime/runtime/runtime.h"
#include <landru/btnpush.h>
#include <landru/dialog.h>
#include <landru/inpattr.h>
#include <landru/task.h>

typedef struct ComputerConfirmState {
	Input* owner;
	Input* dialog;
	int16_t result;
	bool resumed;
} ComputerConfirmState;

static ComputerConfirmState confirm_state;
static Input* options_button;

static LandruTaskStepResult computer_step(void* self) {
	ComputerDialogState* task = self;
	computer_Do_Computer_Dialog();
	return task->finished ? LANDRU_TASK_STEP_DONE : LANDRU_TASK_STEP_CONTINUE;
}
static const LandruTaskVtable computer_vtable = { .step = computer_step };
void TieComputer_RunView(Input* dialog) {
	ComputerDialogState* task = landru_task_top();
	task->the_dialog = dialog;
	task->started = true;
	xdialog_Push_Dialog_View_Task(dialog);
}
void TieComputer_Fail(ResFile* open_resource, const char* missing_resource) {
	ComputerDialogState* task = landru_task_top();
	if (open_resource)
		xres_Close_Resource(open_resource);
	task->missing_resource = missing_resource;
	task->failed = true;
}
void TieComputer_Begin(void) {
	ComputerDialogState* task = landru_task_push(&computer_vtable);
	if (task)
		task->tie98 = TieProfile_FrontendId() == TIE_FRONTEND_PROFILE_TIE98;
}

static void after_confirm_dialog(int16_t result, void* unused) {
	Input* owner = confirm_state.owner;
	(void)unused;
	xinput_Free_Inputs(confirm_state.dialog);
	confirm_state.dialog = NULL;
	xdialog_Clear_Dialog_Exit();
	if (!owner || !owner->user) {
		confirm_state.owner = NULL;
		return;
	}
	/* Re-enter the requesting callback; its Check function now returns
	 * the confirmation result instead of opening the dialog again. */
	confirm_state.result = result;
	confirm_state.resumed = true;
	xinpattr_Selected_Input(owner);
	owner->user(owner, 0);
	confirm_state.resumed = false;
	confirm_state.owner = NULL;
}

void TieComputer_BeginConfirm(Input* owner) {
	if (!confirm_state.resumed)
		confirm_state.owner = owner;
}

void TieComputer_RunConfirm(Input* confirm_dialog) {
	confirm_state.dialog = confirm_dialog;
	xdialog_Schedule_Sub_Dialog(confirm_dialog, after_confirm_dialog, NULL);
}

bool TieComputer_TakeConfirmResult(int16_t* result) {
	if (!confirm_state.resumed)
		return false;
	*result = confirm_state.result;
	confirm_state.resumed = false;
	return true;
}

static void iuser_options_button(Input* input, int32_t time) {
	(void)time;
	if (xinpattr_Get_Input_Selected(input))
		TieRuntime_RequestSettingsMenu();
}

void TieComputer_AllocOptionsButton(Input* parent, Rect* frame, InputDrawFunc draw) {
	options_button = (Input*)xbtnpush_Alloc_Button(parent, frame, 0, iuser_options_button, NULL, 5);
	if (options_button)
		xinpattr_Set_Input_Draw_Function(options_button, draw);
}

void TieComputer_ShowOptionsButton(bool show) {
	if (!options_button)
		return;
	if (show)
		xinpattr_Show_Input(options_button);
	else
		xinpattr_Hide_Input(options_button);
}
