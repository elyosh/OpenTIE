#include "tie_runtime/runtime/train_task.h"
#include "tie/bpflight.h"
#include "tie/train.h"
#include "tie_runtime/runtime/profile.h"
#include <landru/cursor.h>
#include <landru/inpcall.h>
#include <landru/res.h>
#include <landru/surface.h>
#include <landru/task.h>
#include <landru/vesa.h>
#include <landru/view.h>
#include <landru/viewadd.h>

typedef struct TrainTask {
	SceneHeadStruct* scene_head;
	bool started;
	ResFile* resource;
	bool svga;
} TrainTask;

static LandruTaskStepResult train_step(void* self) {
	TrainTask* task = self;
	if (!task->started) {
		task->started = true;
		if (TieProfile_UsesTie98Frontend()) {
			(void)xsurface_Select_Surface_Set(LANDRU_SURFACE_SVGA);
			xview_Init_View(xview_Get_Current_View());
			xvesa_Erase_Video(16);
		}
		train_Train(task->scene_head);
		return LANDRU_TASK_STEP_CONTINUE;
	}
	xinpcall_Clear_Active_Input();
	xview_Clear_View_Update_Function();
	bpflight_Close_Flight_Engine();
	xview_Enable_All_View_Erase();

	if (xcursor_Is_Cursor_Visible())
		xcursor_Hide_Cursor();

	xres_Close_Resource(task->resource);
	if (task->svga) {
		xvesa_Erase_Video(16);
		xviewadd_Clear_View();
		(void)xsurface_Select_Surface_Set(LANDRU_SURFACE_VGA);
	}
	return LANDRU_TASK_STEP_DONE;
}

static const LandruTaskVtable train_vtable = { .step = train_step };

void TieTrain_RunView(ResFile* resource, bool svga) {
	TrainTask* task = landru_task_top();
	task->resource = resource;
	task->svga = svga;
	xviewadd_Push_Handle_View_Task();
}

void TieTrain_Begin(SceneHeadStruct* scene_head) {
	TrainTask* task = landru_task_push(&train_vtable);
	if (!task)
		return;
	task->scene_head = scene_head;
}
