#include "tie_runtime/runtime/debrief_task.h"
#include "tie/debrief.h"
#include "tie_runtime/runtime/profile.h"
#include <landru/cursor.h>
#include <landru/res.h>
#include <landru/surface.h>
#include <landru/task.h>
#include <landru/view.h>
#include <landru/viewadd.h>

typedef struct DebriefTask {
	SceneHeadStruct* scene_head;
	bool started;
	ResFile* resource;
	bool svga;
} DebriefTask;

static LandruTaskStepResult debrief_step(void* self) {
	DebriefTask* task = self;
	if (!task->started) {
		task->started = true;
		if (TieProfile_FrontendId() == TIE_FRONTEND_PROFILE_TIE98) {
			(void)xsurface_Select_Surface_Set(LANDRU_SURFACE_SVGA);
			xview_Init_View(xview_Get_Current_View());
		}
		debrief_Debrief(task->scene_head);
		return LANDRU_TASK_STEP_CONTINUE;
	}
	xview_Enable_All_View_Erase();
	xview_Clear_View_Update_Function();

	if (xcursor_Is_Cursor_Visible())
		xcursor_Hide_Cursor();

	xres_Close_Resource(task->resource);
	if (task->svga)
		(void)xsurface_Select_Surface_Set(LANDRU_SURFACE_VGA);
	return LANDRU_TASK_STEP_DONE;
}

static const LandruTaskVtable debrief_vtable = { .step = debrief_step };

void TieDebrief_RunView(ResFile* resource, bool svga) {
	DebriefTask* task = landru_task_top();
	task->resource = resource;
	task->svga = svga;
	xviewadd_Push_Handle_View_Task();
}

void TieDebrief_Begin(SceneHeadStruct* scene_head) {
	DebriefTask* task = landru_task_push(&debrief_vtable);
	if (!task)
		return;
	task->scene_head = scene_head;
}
