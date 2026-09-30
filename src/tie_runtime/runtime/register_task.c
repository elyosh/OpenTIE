#include "tie_runtime/runtime/register_task.h"
#include "tie/register.h"
#include "tie_runtime/diagnostics/diagnostics.h"
#include "tie_runtime/runtime/profile.h"
#include <landru/canvas.h>
#include <landru/cursor.h>
#include <landru/dialog.h>
#include <landru/error.h>
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
	bool tie98;
	bool view_pushed;
	RegisterPhase phase;
} RegisterTask;
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
			register_CloseProtection();
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
		xdialog_Push_Dialog_View_Task(register_OpenProtection());
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
