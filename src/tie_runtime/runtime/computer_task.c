#include "tie_runtime/runtime/computer_task.h"
#include "tie/computer.h"
#include <landru/dialog.h>
#include <landru/task.h>

typedef struct ComputerTask {
	ComputerDialogState dialog;
	bool opened;
} ComputerTask;

/* Asynchronous adaptation of TIE95 0x82AD0 and TIE98 0x40BA40. */
static LandruTaskStepResult computer_step(void* self) {
	ComputerTask* task = self;
	if (!task->opened) {
		if (!computer_OpenDialog(&task->dialog))
			return LANDRU_TASK_STEP_DONE;
		xdialog_Push_Dialog_View_Task(task->dialog.the_dialog);
		task->opened = true;
		return LANDRU_TASK_STEP_CONTINUE;
	}
	computer_CloseDialog(&task->dialog);
	return LANDRU_TASK_STEP_DONE;
}

static const LandruTaskVtable computer_vtable = { .step = computer_step };

void TieComputer_Begin(void) {
	ComputerTask* task = landru_task_push(&computer_vtable);
	if (!task)
		return;
	computer_PrepareDialog(&task->dialog);
	task->opened = false;
}
