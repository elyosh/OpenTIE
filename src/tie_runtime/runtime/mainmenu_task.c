#include "tie_runtime/runtime/mainmenu_task.h"
#include "tie/mainmenu.h"
#include "tie_runtime/diagnostics/diagnostics.h"
#include "tie_runtime/runtime/profile.h"
#include "tie_runtime/snapshot/snapshot_internal.h"

#include <landru/cursor.h>
#include <landru/error.h>
#include <landru/surface.h>
#include <landru/task.h>
#include <landru/view.h>
#include <landru/viewadd.h>

const MainMenuLayout mainmenu_layout_tie95 = {
	"mainmenu.lfd",
	"mainmenu",
	"main-1",
	{ "m-hang-d", "m-door-1", "m-door-2", "m-door-3", "m-door-4", "m-door-5", "m-door-6", "m-door-7" },
	320,
	200,
	190,
	100,
	{ { 14, 28, 170, 64 },
	  { 0 },
	  { 82, 64, 128, 90 },
	  { 220, 64, 260, 90 },
	  { 272, 68, 304, 94 },
	  { 0, 110, 34, 146 },
	  { 44, 96, 82, 126 },
	  { 130, 86, 168, 106 } },
	{ SCENE_BRIEF, 0, SCENE_TOUR_DESK, SCENE_BLUEPRINT, SCENE_FILM_VIEWER, SCENE_EXIT, SCENE_TRAIN_TRANSITION,
	  SCENE_COMBAT_TRANSITION },
	{ 0, txtMainCustom, 0, txtMainTrain, txtMainCombat, txtMainRegister, txtMainTech, txtMainFilm },
	0,
	4,
};

const MainMenuLayout mainmenu_layout_tie98 = {
	"mm640.lfd",
	"main_00",
	NULL,
	{ "hr_mhngr", "hr_md3_1", "hr_md2_1", "hr_md1_2", "hr_md1_3", "hr_md1_1", "hr_md2_2", "hr_md2_3" },
	640,
	480,
	387,
	387,
	{ { 19, 66, 340, 142 },
	  { 0 },
	  { 172, 163, 248, 210 },
	  { 104, 237, 154, 299 },
	  { 266, 212, 331, 260 },
	  { 0, 268, 55, 357 },
	  { 433, 159, 512, 215 },
	  { 549, 168, 604, 226 } },
	{ SCENE_BRIEF, 0, SCENE_TOUR_DESK, SCENE_TRAIN_TRANSITION, SCENE_COMBAT_TRANSITION, SCENE_EXIT,
	  SCENE_BLUEPRINT, SCENE_FILM_VIEWER },
	{ 0, txtMainCustom, 0, txtMainTech, txtMainFilm, txtMainRegister, txtMainTrain, txtMainCombat },
	2,
	7,
};

typedef struct MainMenuTask {
	SceneHeadStruct* scene_head;
	ResFile* resource;
	bool started;
	bool tie98;
} MainMenuTask;

static LandruTaskStepResult mainmenu_step(void* self) {
	MainMenuTask* task = self;
	if (task->started)
		return LANDRU_TASK_STEP_DONE;
	task->started = true;
	if (!xsurface_Select_Surface_Set(task->tie98 ? LANDRU_SURFACE_SVGA : LANDRU_SURFACE_VGA)) {
		TieDiagnostics_Log(TIE_LOG_ERROR, "[MAINMENU] missing frontend resource: surface set\n");
		xerror_Set_Landru_Error(6);
		return LANDRU_TASK_STEP_DONE;
	}
	xview_Init_View(xview_Get_Current_View());
	mainmenu_Main_Menu(task->scene_head);
	return LANDRU_TASK_STEP_CONTINUE;
}

static void mainmenu_end(void* self) {
	MainMenuTask* task = self;
	if (!task->started)
		return;
	xview_Enable_All_View_Erase();
	xview_Clear_View_Update_Function();
	if (xcursor_Is_Cursor_Visible())
		xcursor_Hide_Cursor();
	if (task->resource)
		xres_Close_Resource(task->resource);
	if (task->tie98)
		xsurface_Select_Surface_Set(LANDRU_SURFACE_VGA);
}

static const LandruTaskVtable mainmenu_vtable = { mainmenu_step, mainmenu_end, NULL, NULL };

void TieMainMenu_RunView(ResFile* resource, const char* missing_resource) {
	MainMenuTask* task = landru_task_top();
	task->resource = resource;
	if (missing_resource) {
		TieDiagnostics_Log(TIE_LOG_ERROR, "[MAINMENU] missing frontend resource: %s\n", missing_resource);
		xerror_Set_Landru_Error(6);
		return;
	}
	const MainMenuLayout* layout = task->tie98 ? &mainmenu_layout_tie98 : &mainmenu_layout_tie95;
	TieSnapshotBuilder_SetActiveFilm(task->tie98 ? "MM640" : "MAINMENU", layout->film);
	xviewadd_Push_Handle_View_Task();
}

void TieMainMenu_Begin(SceneHeadStruct* scene_head) {
	MainMenuTask* task = landru_task_push(&mainmenu_vtable);
	if (!task)
		return;
	task->scene_head = scene_head;
	task->tie98 = TieProfile_FrontendId() == TIE_FRONTEND_PROFILE_TIE98;
}
