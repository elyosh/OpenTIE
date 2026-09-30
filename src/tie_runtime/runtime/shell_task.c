#include "tie_runtime/runtime/shell_task.h"
#include "tie/frontend_display_tie98.h"
#include "tie/gamesnd.h"
#include "tie/shell.h"
#include "tie/shipext.h"
#include "tie/wavestream_tie98.h"
#include "tie_runtime/diagnostics/diagnostics.h"
#include "tie_runtime/runtime/profile.h"
#include "tie_runtime/snapshot/snapshot_internal.h"
#include <landru/error.h>
#include <landru/stream.h>
#include <landru/surface.h>
#include <landru/task.h>
#include <landru/view.h>

bool TieShell_PrepareScene(int16_t scene) {
	TieSnapshotBuilder_SetSceneKind(TIE_SCENE_FRONTEND);
	TieSnapshotBuilder_SetActiveFilm(NULL, NULL);
	TieSnapshotBuilder_SetRedrawModel(TIE_REDRAW_INCREMENTAL);
	if (TieProfile_FrontendId() == TIE_FRONTEND_PROFILE_TIE98 &&
		!xsurface_Select_Surface_Set(LANDRU_SURFACE_VGA)) {
		TieDiagnostics_Log(TIE_LOG_ERROR, "[SHELL] could not select VGA Landru surface for scene %d\n",
						   scene);
		xerror_Set_Landru_Error(7);
		return false;
	}
	if (TieProfile_FrontendId() == TIE_FRONTEND_PROFILE_TIE98)
		xview_Init_View(xview_Get_Current_View());

	if (TieProfile_FrontendId() == TIE_FRONTEND_PROFILE_TIE98 &&
		(scene == SCENE_FLIGHT_COMBAT || scene == SCENE_FLIGHT_BATTLE || scene == SCENE_TRAIN_A ||
		 scene == SCENE_TRAIN_MAP || scene == SCENE_COMBAT_A || scene == SCENE_COMBAT_MAP_A ||
		 scene == SCENE_COMBAT_MAP_B || scene == SCENE_FILM_VIEWER || scene == SCENE_BRIEF_MAP ||
		 scene == SCENE_FILM_REPLAY)) {
		FrontendDisplay_ClearPresentationSurfaces();
		FrontendDisplay_PresentFrame();
	}

	return true;
}
static LandruTaskStepResult shell_task_step(void* self) {
	ShellTask* task = self;
	shell_Shell(task->cur_scene, task->script);
	return task->finished ? LANDRU_TASK_STEP_DONE : LANDRU_TASK_STEP_CONTINUE;
}
static void shell_task_end(void* self) {
	(void)self;
	FrontendWaveStream_Shutdown();
	shipext_Validate_Tour_Battle();
	shipext_Delete_Temp_Pilot();
	xstream_Exit_Stream_Engine(0);
	shellext_Close_Landru(0);
	gamesnd_Close_Pre_iMuse();
	if (TieProfile_UsesDx5())
		g_frontendDisplayWndProcMode = -1;
}
static const LandruTaskVtable shell_task_vt = {
	.step = shell_task_step,
	.end = shell_task_end,
};
void TieShell_Begin(int16_t scene, int16_t script) {
	ShellTask* task = landru_task_push(&shell_task_vt);
	if (!task)
		return;
	task->cur_scene = scene;
	task->script = script;
	shell_Shell(scene, script);
}
