#include "tie_runtime/runtime/computer_task.h"
#include "tie/computer.h"
#include "tie_runtime/runtime/profile.h"
#include <landru/dialog.h>
#include <landru/task.h>

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
