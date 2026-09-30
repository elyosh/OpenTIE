#include "tie_runtime/runtime/film_task.h"
#include "tie/play1.h"
#include "tie_runtime/snapshot/snapshot_internal.h"
#include <landru/task.h>
#include <landru/viewadd.h>

typedef struct FilmTask {
	Play1SceneState scene;
	bool opened;
} FilmTask;
static LandruTaskStepResult film_step(void* self) {
	FilmTask* task = self;
	if (task->opened)
		return LANDRU_TASK_STEP_DONE;
	if (!play1_OpenScene(&task->scene))
		return LANDRU_TASK_STEP_DONE;
	xviewadd_Push_Handle_View_Task();
	task->opened = true;
	return LANDRU_TASK_STEP_CONTINUE;
}
static void film_end(void* self) {
	FilmTask* task = self;
	if (task->opened)
		play1_CloseScene(&task->scene);
}
static const LandruTaskVtable film_vtable = { .step = film_step, .end = film_end };
void TieFilm_Begin(SceneHeadStruct* head) {
	TieSnapshotBuilder_SetSceneKind(TIE_SCENE_CUTSCENE);
	TieSnapshotBuilder_SetRedrawModel(TIE_REDRAW_FULL_FRAME);
	FilmTask* task = landru_task_push(&film_vtable);
	if (!task)
		return;
	task->scene.the_head = head;
	task->scene.file = NULL;
	task->scene.file2 = NULL;
	task->scene.rate_changed = false;
	task->scene.is_streaming_active = false;
	task->scene.surface_set = LANDRU_SURFACE_VGA;
	task->opened = false;
}
