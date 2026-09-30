#include "tie_runtime/runtime/title_task.h"
#include "tie/title.h"
#include "tie_runtime/runtime/profile.h"
#include "tie_runtime/snapshot/snapshot_internal.h"
#include "tie_runtime/snapshot/title.h"
#include "tie_runtime/storage/storage.h"

#include <landru/error.h>
#include <landru/stream.h>
#include <landru/task.h>
#include <landru/viewadd.h>

typedef struct TitleTask {
	SceneHeadStruct* scene_head;
	TitleSceneResources resources;
	bool started;
} TitleTask;

// ORIGINAL_FUNCTION: TIE95 0x668A0
// ORIGINAL_FUNCTION: TIE98 0x48FF80
static LandruTaskStepResult title_step(void* self) {
	TitleTask* task = self;
	if (task->started)
		return LANDRU_TASK_STEP_DONE;
	task->started = true;
	TieTitleSnapshot_Reset();
	const int16_t font_slot = TieProfile_FrontendId() == TIE_FRONTEND_PROFILE_TIE98 ? 4 : 2;
	if (!title_OpenScene(task->scene_head, &task->resources, font_slot)) {
		xerror_Set_Landru_Error(6);
		return LANDRU_TASK_STEP_DONE;
	}
	const char* path = (TieStorage_IsDirectory(TIE_FILE_ROOT_FRONTEND_ASSET, "astream") ||
						TieStorage_IsDirectory(TIE_FILE_ROOT_FRONTEND_ASSET, "ASTREAM"))
						   ? "astream\\os1-v3.wrk"
						   : "stream\\os1-v3.wrk";
	xstream_Chain_Stream_File(0, path);
	TieSnapshotBuilder_SetActiveFilm("TITLE", task->resources.film_name);
	TieSnapshotBuilder_SetRedrawModel(TIE_REDRAW_FULL_FRAME);
	xviewadd_Push_Handle_View_Task();
	return LANDRU_TASK_STEP_CONTINUE;
}

static void title_end(void* self) {
	TitleTask* task = self;
	if (!task->started)
		return;
	title_CloseScene(&task->resources);
	TieTitleSnapshot_Reset();
}

static const LandruTaskVtable title_vtable = { title_step, title_end, NULL, NULL };

void TieTitle_Begin(SceneHeadStruct* scene_head) {
	TitleTask* task = landru_task_push(&title_vtable);
	if (task)
		task->scene_head = scene_head;
}
