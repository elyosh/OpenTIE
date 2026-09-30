#include "tie_runtime/runtime/mainmenu_task.h"
#include "tie/mainmenu.h"
#include "tie_runtime/diagnostics/diagnostics.h"
#include "tie_runtime/runtime/profile.h"
#include "tie_runtime/snapshot/snapshot_internal.h"

#include <landru/error.h>
#include <landru/surface.h>
#include <landru/task.h>
#include <landru/view.h>
#include <landru/viewadd.h>

typedef struct MainMenuTask {
	SceneHeadStruct* scene_head;
	ResFile* resource;
	bool started;
	bool tie98;
} MainMenuTask;

// ORIGINAL_FUNCTION: TIE95 0x70820
// ORIGINAL_FUNCTION: TIE98 0x44CEB0
static LandruTaskStepResult mainmenu_step(void* self) {
	MainMenuTask* task = self;
	if (task->started)
		return LANDRU_TASK_STEP_DONE;
	task->started = true;
	const MainMenuLayout* layout = task->tie98 ? &mainmenu_layout_tie98 : &mainmenu_layout_tie95;
	const char* missing_resource = "surface set";
	if (xsurface_Select_Surface_Set(task->tie98 ? LANDRU_SURFACE_SVGA : LANDRU_SURFACE_VGA)) {
		xview_Init_View(xview_Get_Current_View());
		missing_resource = mainmenu_OpenScene(task->scene_head, layout, &task->resource);
	}
	if (missing_resource) {
		TieDiagnostics_Log(TIE_LOG_ERROR, "[MAINMENU] missing frontend resource: %s\n", missing_resource);
		xerror_Set_Landru_Error(6);
		return LANDRU_TASK_STEP_DONE;
	}
	TieSnapshotBuilder_SetActiveFilm(task->tie98 ? "MM640" : "MAINMENU", layout->film);
	xviewadd_Push_Handle_View_Task();
	return LANDRU_TASK_STEP_CONTINUE;
}

static void mainmenu_end(void* self) {
	MainMenuTask* task = self;
	if (!task->started)
		return;
	mainmenu_CloseScene(task->resource);
	if (task->tie98)
		xsurface_Select_Surface_Set(LANDRU_SURFACE_VGA);
}

static const LandruTaskVtable mainmenu_vtable = { mainmenu_step, mainmenu_end, NULL, NULL };

void TieMainMenu_Begin(SceneHeadStruct* scene_head) {
	MainMenuTask* task = landru_task_push(&mainmenu_vtable);
	if (!task)
		return;
	task->scene_head = scene_head;
	task->tie98 = TieProfile_FrontendId() == TIE_FRONTEND_PROFILE_TIE98;
}
