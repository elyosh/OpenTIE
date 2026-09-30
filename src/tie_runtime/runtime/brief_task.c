#include "tie_runtime/runtime/brief_task.h"
#include "tie/brief.h"
#include "tie_runtime/runtime/profile.h"

#include "tie/player.h"
#include <landru/cursor.h>
#include <landru/dialog.h>
#include <landru/io.h>
#include <landru/surface.h>
#include <landru/task.h>
#include <landru/vesa.h>
#include <landru/view.h>
#include <landru/viewadd.h>

typedef enum BriefPhase {
	BRIEF_BEGIN,
	BRIEF_NOTICE,
	BRIEF_VIEW,
	BRIEF_CLEANUP,
} BriefPhase;

typedef struct BriefTask {
	SceneHeadStruct* scene_head;
	Input* notice;
	bool svga;
	int16_t return_x, return_y;
	BriefPhase phase;
} BriefTask;

static LandruTaskStepResult brief_step(void* self) {
	BriefTask* task = self;
	switch (task->phase) {
		case BRIEF_BEGIN:
			if (task->svga) {
				(void)xsurface_Select_Surface_Set(LANDRU_SURFACE_SVGA);
				xview_Init_View(xview_Get_Current_View());
				xvesa_Erase_Video(16);
			}
			brief_Brief(task->scene_head);
			return LANDRU_TASK_STEP_CONTINUE;
		case BRIEF_NOTICE:
			xdialog_Clear_Dialog_Exit();
			xinput_Free_Inputs(task->notice);
			xio_Set_Mouse_Position(task->return_x, task->return_y);
			task->notice = NULL;
			task->phase = BRIEF_VIEW;
			return LANDRU_TASK_STEP_CONTINUE;
		case BRIEF_VIEW:
			xview_Set_View_Update_Function(brief_end_View);
			xviewadd_Clear_View();
			xview_Disable_All_View_Erase();
			xviewadd_Push_Handle_View_Task();
			task->phase = BRIEF_CLEANUP;
			return LANDRU_TASK_STEP_CONTINUE;
		case BRIEF_CLEANUP:
			player_Free_Brief_Display();
			xview_Enable_All_View_Erase();
			xview_Clear_View_Update_Function();
			if (xcursor_Is_Cursor_Visible())
				xcursor_Hide_Cursor();
			if (task->svga) {
				xvesa_Erase_Video(16);
				xviewadd_Clear_View();
				(void)xsurface_Select_Surface_Set(LANDRU_SURFACE_VGA);
			}
			return LANDRU_TASK_STEP_DONE;
	}
	return LANDRU_TASK_STEP_DONE;
}

static const LandruTaskVtable brief_vtable = { brief_step, NULL, NULL, NULL };

void TieBrief_RunView(Input* notice, bool svga, int16_t return_x, int16_t return_y) {
	BriefTask* task = landru_task_top();
	task->notice = notice;
	task->svga = svga;
	task->return_x = return_x;
	task->return_y = return_y;
	if (notice) {
		xdialog_Push_Dialog_View_Task(notice);
		task->phase = BRIEF_NOTICE;
	} else {
		task->phase = BRIEF_VIEW;
	}
}

void TieBrief_Begin(SceneHeadStruct* scene_head) {
	BriefTask* task = landru_task_push(&brief_vtable);
	if (!task)
		return;
	task->scene_head = scene_head;
	task->notice = NULL;
	task->svga = TieProfile_UsesTie98Frontend();
	task->phase = BRIEF_BEGIN;
}
