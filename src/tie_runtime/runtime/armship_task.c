#include "tie_runtime/runtime/armship_task.h"
#include "tie/armship.h"

#include <landru/cursor.h>
#include <landru/error.h>
#include <landru/inpcall.h>
#include <landru/surface.h>
#include <landru/task.h>
#include <landru/view.h>
#include <landru/viewadd.h>

typedef struct ArmShipTask {
	SceneHeadStruct* scene_head;
	ResFile* launch_resource;
	LandruSurfaceSet previous_surface;
	bool started;
} ArmShipTask;

static LandruTaskStepResult armship_step(void* self) {
	ArmShipTask* task = self;
	if (task->started)
		return LANDRU_TASK_STEP_DONE;
	task->previous_surface = xsurface_Get_Surface_Set();
	if (!xsurface_Select_Surface_Set(LANDRU_SURFACE_VGA)) {
		xerror_Set_Landru_Error(6);
		return LANDRU_TASK_STEP_DONE;
	}
	task->started = true;
	xview_Init_View(xview_Get_Current_View());
	armship_ArmShip(task->scene_head);
	return LANDRU_TASK_STEP_CONTINUE;
}

static void armship_end(void* self) {
	ArmShipTask* task = self;
	if (!task->started)
		return;
	xinpcall_Clear_Active_Input();
	xview_Clear_View_Update_Function();
	xview_Enable_All_View_Erase();
	if (xcursor_Is_Cursor_Visible())
		xcursor_Hide_Cursor();
	if (task->launch_resource)
		xres_Close_Resource(task->launch_resource);
	if (armship_file)
		xres_Close_Resource(armship_file);
	armship_file = NULL;
	if (task->previous_surface != LANDRU_SURFACE_VGA) {
		xsurface_Select_Surface_Set(task->previous_surface);
		xview_Init_View(xview_Get_Current_View());
	}
}

static const LandruTaskVtable armship_vtable = { armship_step, armship_end, NULL, NULL };

void TieArmShip_RunView(ResFile* launch_resource, bool ready) {
	ArmShipTask* task = landru_task_top();
	task->launch_resource = launch_resource;
	if (ready)
		xviewadd_Push_Handle_View_Task();
	else
		xerror_Set_Landru_Error(6);
}

void TieArmShip_Begin(SceneHeadStruct* scene_head) {
	ArmShipTask* task = landru_task_push(&armship_vtable);
	if (!task)
		return;
	task->scene_head = scene_head;
}
