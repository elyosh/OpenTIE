#include "tie_runtime/runtime/tourdesk_task.h"
#include "tie/tourdesk.h"
#include "tie_runtime/runtime/profile.h"
#include <landru/cursor.h>
#include <landru/surface.h>
#include <landru/task.h>
#include <landru/vesa.h>
#include <landru/view.h>
#include <landru/viewadd.h>

typedef struct TourDeskTask {
	SceneHeadStruct* scene_head;
	bool started;
	bool svga;
} TourDeskTask;

static LandruTaskStepResult tourdesk_step(void* self) {
	TourDeskTask* task = self;
	if (!task->started) {
		task->started = true;
		if (TieProfile_UsesTie98Frontend()) {
			(void)xsurface_Select_Surface_Set(LANDRU_SURFACE_SVGA);
			xview_Init_View(xview_Get_Current_View());
		}
		tourdesk_TourDesk(task->scene_head);
		return LANDRU_TASK_STEP_CONTINUE;
	}
	xview_Enable_All_View_Erase();
	xview_Clear_View_Update_Function();

	if (xcursor_Is_Cursor_Visible())
		xcursor_Hide_Cursor();

	if (task->svga) {
		xvesa_Erase_Video(16);
		(void)xsurface_Select_Surface_Set(LANDRU_SURFACE_VGA);
	}
	return LANDRU_TASK_STEP_DONE;
}

static const LandruTaskVtable tourdesk_vtable = { .step = tourdesk_step };

void TieTourDesk_RunView(bool svga) {
	TourDeskTask* task = landru_task_top();
	task->svga = svga;
	xviewadd_Push_Handle_View_Task();
}

void TieTourDesk_Begin(SceneHeadStruct* scene_head) {
	TourDeskTask* task = landru_task_push(&tourdesk_vtable);
	if (!task)
		return;
	task->scene_head = scene_head;
}
