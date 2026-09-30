#include "tie_runtime/runtime/register_task.h"
#include "tie/register.h"
#include "tie_runtime/diagnostics/diagnostics.h"
#include "tie_runtime/runtime/profile.h"
#include <landru/canvas.h>
#include <landru/cursor.h>
#include <landru/dialog.h>
#include <landru/error.h>
#include <landru/inpattr.h>
#include <landru/io.h>
#include <landru/surface.h>
#include <landru/task.h>
#include <landru/view.h>
#include <landru/viewadd.h>
#include <stdlib.h>

typedef enum { REGISTER_BEGIN, REGISTER_PROTECT, REGISTER_VIEW, REGISTER_CLEANUP } RegisterPhase;
typedef struct RegisterTask {
	SceneHeadStruct* scene_head;
	ResFile* resource;
	Input* protect_dialog;
	bool tie98;
	bool view_pushed;
	RegisterPhase phase;
} RegisterTask;

typedef struct RegisterDeleteState {
	Input* owner;
	Input* dialog;
	int16_t key_buttons;
	int16_t result;
	bool resumed;
} RegisterDeleteState;

static bool copy_protection_enabled;
static RegisterDeleteState delete_state;
static LandruTaskStepResult register_step(void* self) {
	RegisterTask* task = self;
	switch (task->phase) {
		case REGISTER_BEGIN:
			if (!xsurface_Select_Surface_Set(task->tie98 ? LANDRU_SURFACE_SVGA : LANDRU_SURFACE_VGA)) {
				TieRegister_RunView(NULL, false, "surface set");
				return LANDRU_TASK_STEP_CONTINUE;
			}
			xview_Init_View(xview_Get_Current_View());
			register_Register(task->scene_head);
			return LANDRU_TASK_STEP_CONTINUE;
		case REGISTER_PROTECT:
			if (register_Do_Protect_Dialog())
				xerror_Set_Landru_Exit(0);
			xio_Set_Mouse_Position(160, 130);
			task->phase = REGISTER_VIEW;
			return LANDRU_TASK_STEP_CONTINUE;
		case REGISTER_VIEW:
			xview_Set_View_Update_Function(register_end_View);
			xviewadd_Clear_View();
			xview_Disable_All_View_Erase();
			xcanvas_Invalid_Screen_Diff();
			xio_Set_Key_Buttons();
			xviewadd_Push_Handle_View_Task();
			task->view_pushed = true;
			task->phase = REGISTER_CLEANUP;
			return LANDRU_TASK_STEP_CONTINUE;
		case REGISTER_CLEANUP:
			xio_Clear_Key_Buttons();
			xview_Enable_All_View_Erase();
			xview_Clear_View_Update_Function();
			if (!task->view_pushed) {
				xview_Free_All_From_View(xview_Get_Current_View());
				xview_Init_View(xview_Get_Current_View());
			}
			xfiledir_Free_Directory(&register_directory);
			free(register_fast_pilot_record);
			register_fast_pilot_record = NULL;
			if (xcursor_Is_Cursor_Visible())
				xcursor_Hide_Cursor();
			if (task->resource)
				xres_Close_Resource(task->resource);
			if (task->tie98)
				(void)xsurface_Select_Surface_Set(LANDRU_SURFACE_VGA);
			return LANDRU_TASK_STEP_DONE;
	}
	return LANDRU_TASK_STEP_DONE;
}
static const LandruTaskVtable register_vtable = { .step = register_step };
void TieRegister_RunView(ResFile* resource, bool protect, const char* missing_resource) {
	RegisterTask* task = landru_task_top();
	task->resource = resource;
	if (missing_resource) {
		TieDiagnostics_Log(TIE_LOG_ERROR, "[REGISTER] missing frontend resource: %s\n", missing_resource);
		xerror_Set_Landru_Error(6);
		task->phase = REGISTER_CLEANUP;
	} else if (protect) {
		register_Do_Protect_Dialog();
		task->phase = REGISTER_PROTECT;
	} else {
		task->phase = REGISTER_VIEW;
	}
}
void TieRegister_Begin(SceneHeadStruct* head) {
	RegisterTask* task = landru_task_push(&register_vtable);
	if (!task)
		return;
	task->scene_head = head;
	task->tie98 = TieProfile_FrontendId() == TIE_FRONTEND_PROFILE_TIE98;
}

void TieRegister_SetCopyProtection(bool enabled) { copy_protection_enabled = enabled; }

bool TieRegister_CopyProtectionEnabled(void) { return copy_protection_enabled; }

void TieRegister_RunProtect(Input* dialog) {
	RegisterTask* task = landru_task_top();
	task->protect_dialog = dialog;
	xdialog_Push_Dialog_View_Task(dialog);
}

bool TieRegister_ResumeProtect(Input** dialog) {
	RegisterTask* task = landru_task_top();
	if (!task->protect_dialog)
		return false;
	*dialog = task->protect_dialog;
	task->protect_dialog = NULL;
	return true;
}

static void after_delete_dialog(int16_t result, void* unused) {
	Input* owner = delete_state.owner;
	(void)unused;
	if (!owner || !owner->user) {
		/* Nothing to resume: finish the dialog tail directly. */
		xinput_Free_Inputs(delete_state.dialog);
		if (!delete_state.key_buttons)
			xio_Clear_Key_Buttons();
		delete_state.dialog = NULL;
		delete_state.owner = NULL;
		return;
	}
	delete_state.result = result;
	delete_state.resumed = true;
	xinpattr_Selected_Input(owner);
	owner->user(owner, 0);
	delete_state.resumed = false;
	delete_state.dialog = NULL;
	delete_state.owner = NULL;
}

void TieRegister_BeginDelete(Input* owner) {
	if (!delete_state.resumed)
		delete_state.owner = owner;
}

void TieRegister_RunDelete(Input* dialog, int16_t key_buttons) {
	delete_state.dialog = dialog;
	delete_state.key_buttons = key_buttons;
	xdialog_Schedule_Sub_Dialog(dialog, after_delete_dialog, NULL);
}

bool TieRegister_ResumeDelete(Input** dialog, int16_t* key_buttons, int16_t* result) {
	if (!delete_state.resumed)
		return false;
	*dialog = delete_state.dialog;
	*key_buttons = delete_state.key_buttons;
	*result = delete_state.result;
	delete_state.resumed = false;
	return true;
}

bool TieRegister_IsResumingDelete(void) { return delete_state.resumed; }
