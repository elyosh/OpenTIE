#include "tie_runtime/runtime/combat_task.h"
#include "tie/bpflight.h"
#include "tie/combat.h"
#include "tie_runtime/runtime/profile.h"
#include <landru/cursor.h>
#include <landru/inpcall.h>
#include <landru/res.h>
#include <landru/surface.h>
#include <landru/task.h>
#include <landru/vesa.h>
#include <landru/view.h>
#include <landru/viewadd.h>
#include <stdlib.h>

typedef struct CombatTask {
	SceneHeadStruct* scene_head;
	bool started;
	ResFile* resource;
	ResFile* train_resource;
	bool svga;
} CombatTask;

static LandruTaskStepResult combat_step(void* self) {
	CombatTask* task = self;
	if (!task->started) {
		task->started = true;
		if (TieProfile_FrontendId() == TIE_FRONTEND_PROFILE_TIE98) {
			(void)xsurface_Select_Surface_Set(LANDRU_SURFACE_SVGA);
			xview_Init_View(xview_Get_Current_View());
			xvesa_Erase_Video(16);
		}
		combat_Combat(task->scene_head);
		return LANDRU_TASK_STEP_CONTINUE;
	}
	xinpcall_Clear_Active_Input();
	xview_Clear_View_Update_Function();
	bpflight_Close_Flight_Engine();
	xview_Enable_All_View_Erase();

	if (xcursor_Is_Cursor_Visible())
		xcursor_Hide_Cursor();

	xres_Close_Resource(task->resource);
	if (task->train_resource)
		xres_Close_Resource(task->train_resource);
	if (combat_score_data) {
		free(combat_score_data);
		combat_score_data = NULL;
	}
	if (task->svga) {
		xvesa_Erase_Video(16);
		xviewadd_Clear_View();
		(void)xsurface_Select_Surface_Set(LANDRU_SURFACE_VGA);
	}
	return LANDRU_TASK_STEP_DONE;
}

static const LandruTaskVtable combat_vtable = { .step = combat_step };

void TieCombat_RunView(ResFile* resource, ResFile* train_resource, bool svga) {
	CombatTask* task = landru_task_top();
	task->resource = resource;
	task->train_resource = train_resource;
	task->svga = svga;
	xviewadd_Push_Handle_View_Task();
}

void TieCombat_Begin(SceneHeadStruct* scene_head) {
	CombatTask* task = landru_task_push(&combat_vtable);
	if (!task)
		return;
	task->scene_head = scene_head;
}
