#include "tie_runtime/runtime/tielogo_task.h"
#include "tie/tielogo.h"
#include "tie_runtime/snapshot/logo.h"
#include "tie_runtime/snapshot/snapshot_internal.h"

#include <landru/error.h>
#include <landru/task.h>
#include <landru/viewadd.h>

typedef struct TielogoTask {
	SceneHeadStruct* scene_head;
	ResFile* resource;
	bool started;
} TielogoTask;

// ORIGINAL_FUNCTION: TIE95 0x72780
// ORIGINAL_FUNCTION: TIE98 0x48F830
static LandruTaskStepResult tielogo_step(void* self) {
	TielogoTask* task = self;
	if (task->started)
		return LANDRU_TASK_STEP_DONE;
	task->started = true;
	/* Film loading invokes callbacks that can capture the first stamps. */
	TieLogoSnapshot_Reset();
	if (!tielogo_OpenScene(task->scene_head, &task->resource)) {
		xerror_Set_Landru_Error(6);
		return LANDRU_TASK_STEP_DONE;
	}
	xactor_Set_Actor_Render_Capture_Hidden(fighter_actor, true);
	xactor_Set_Actor_Render_Capture_Hidden(fighter2_actor, true);
	TieSnapshotBuilder_SetActiveFilm("TIELOGO", "logo");
	TieSnapshotBuilder_SetRedrawModel(TIE_REDRAW_FULL_FRAME);
	TieSnapshotBuilder_SetSceneKind(TIE_SCENE_CUTSCENE);
	xviewadd_Push_Handle_View_Task();
	return LANDRU_TASK_STEP_CONTINUE;
}

static void tielogo_end(void* self) {
	TielogoTask* task = self;
	if (!task->started)
		return;
	tielogo_CloseScene(task->resource);
	TieLogoSnapshot_Reset();
}

static const LandruTaskVtable tielogo_vtable = { tielogo_step, tielogo_end, NULL, NULL };

void TieLogo_Begin(SceneHeadStruct* scene_head) {
	TielogoTask* task = landru_task_push(&tielogo_vtable);
	if (!task)
		return;
	task->scene_head = scene_head;
}
