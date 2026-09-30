#include "tie_runtime/runtime/register_task.h"
#include "tie/register.h"
#include <landru/dialog.h>
#include <landru/task.h>
#include <landru/viewadd.h>

typedef enum { REGISTER_BEGIN, REGISTER_PROTECT, REGISTER_VIEW, REGISTER_CLEANUP } RegisterPhase;
typedef struct RegisterTask {
	RegisterSceneState scene;
	RegisterPhase phase;
} RegisterTask;

/* Task adaptation of TIE95 0x7A4C0 and TIE98 0x46FBC0. */
static LandruTaskStepResult register_step(void* self) {
	RegisterTask* task = self;
	switch (task->phase) {
		case REGISTER_BEGIN: {
			RegisterOpenResult result = register_OpenScene(&task->scene);
			if (result == REGISTER_OPEN_FAILED)
				task->phase = REGISTER_CLEANUP;
			else if (result == REGISTER_OPEN_PROTECT) {
				xdialog_Push_Dialog_View_Task(register_OpenProtection());
				task->phase = REGISTER_PROTECT;
			} else
				task->phase = REGISTER_VIEW;
			return LANDRU_TASK_STEP_CONTINUE;
		}
		case REGISTER_PROTECT:
			register_CloseProtection();
			task->phase = REGISTER_VIEW;
			return LANDRU_TASK_STEP_CONTINUE;
		case REGISTER_VIEW:
			register_PrepareView();
			xviewadd_Push_Handle_View_Task();
			task->scene.view_pushed = true;
			task->phase = REGISTER_CLEANUP;
			return LANDRU_TASK_STEP_CONTINUE;
		case REGISTER_CLEANUP:
			register_CloseScene(&task->scene);
			return LANDRU_TASK_STEP_DONE;
	}
	return LANDRU_TASK_STEP_DONE;
}
static const LandruTaskVtable register_vtable = { .step = register_step };
void TieRegister_Begin(SceneHeadStruct* head) {
	RegisterTask* task = landru_task_push(&register_vtable);
	if (!task)
		return;
	register_PrepareScene(&task->scene, head);
	task->phase = REGISTER_BEGIN;
}
