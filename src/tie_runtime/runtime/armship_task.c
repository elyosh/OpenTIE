#include "tie_runtime/runtime/armship_task.h"
#include "tie/armship.h"

#include <landru/error.h>
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

// ORIGINAL_FUNCTION: TIE95 0x6EB48
// ORIGINAL_FUNCTION: TIE98 0x402680
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
	if (!armship_OpenScene(task->scene_head, &task->launch_resource)) {
		xerror_Set_Landru_Error(6);
		return LANDRU_TASK_STEP_DONE;
	}
	xviewadd_Push_Handle_View_Task();
	return LANDRU_TASK_STEP_CONTINUE;
}

static void armship_end(void* self) {
	ArmShipTask* task = self;
	if (!task->started)
		return;
	armship_CloseScene(task->launch_resource);
	if (task->previous_surface != LANDRU_SURFACE_VGA) {
		xsurface_Select_Surface_Set(task->previous_surface);
		xview_Init_View(xview_Get_Current_View());
	}
}

static const LandruTaskVtable armship_vtable = { armship_step, armship_end, NULL, NULL };

void TieArmShip_Begin(SceneHeadStruct* scene_head) {
	ArmShipTask* task = landru_task_push(&armship_vtable);
	if (!task)
		return;
	task->scene_head = scene_head;
}
