#include "tie_runtime/runtime/title_task.h"
#include "tie/title.h"
#include "tie_runtime/runtime/profile.h"
#include "tie_runtime/snapshot/snapshot_internal.h"
#include "tie_runtime/snapshot/title.h"
#include "tie_runtime/storage/storage.h"

#include <landru/bitmap.h>
#include <landru/canvas.h>
#include <landru/error.h>
#include <landru/paragrp.h>
#include <landru/stream.h>
#include <landru/task.h>
#include <landru/timer.h>
#include <landru/view.h>
#include <landru/viewadd.h>

typedef struct TitleTask {
	SceneHeadStruct* scene_head;
	ResFile* resource;
	bool started;
} TitleTask;

static LandruTaskStepResult title_step(void* self) {
	TitleTask* task = self;
	if (task->started)
		return LANDRU_TASK_STEP_DONE;
	task->started = true;
	TieTitleSnapshot_Reset();
	title_Title(task->scene_head);
	return LANDRU_TASK_STEP_CONTINUE;
}

static void title_end(void* self) {
	TitleTask* task = self;
	Rect frame;
	if (!task->started)
		return;
	xview_Enable_Global_View_Erase();
	xview_Clear_View_Update_Function();
	xbitmap_Free_Bitmap(&title_background);
	xparagrp_Free_Paragraph(title_text);
	title_text = LANDRU_NULL_HANDLE;
	title_num_lines = 0;
	if (task->resource)
		xres_Close_Resource(task->resource);
	task->resource = NULL;
	xcanvas_Get_Drawing_Canvas_Bounds(&frame);
	xview_Set_View_Frame(0, &frame);
	xview_Set_View_Pos(0, 0, 0);
	TieTitleSnapshot_Reset();
}

static const LandruTaskVtable title_vtable = { title_step, title_end, NULL, NULL };

void TieTitle_RunView(ResFile* resource, const char* film_name, bool ready) {
	TitleTask* task = landru_task_top();
	task->resource = resource;
	if (!ready) {
		xerror_Set_Landru_Error(6);
		return;
	}
	const char* path = (TieStorage_IsDirectory(TIE_FILE_ROOT_FRONTEND_ASSET, "astream") ||
						TieStorage_IsDirectory(TIE_FILE_ROOT_FRONTEND_ASSET, "ASTREAM"))
						   ? "astream\\os1-v3.wrk"
						   : "stream\\os1-v3.wrk";
	xstream_Chain_Stream_File(0, path);
	TieSnapshotBuilder_SetActiveFilm("TITLE", film_name);
	TieSnapshotBuilder_SetRedrawModel(TIE_REDRAW_FULL_FRAME);
	xviewadd_Push_Handle_View_Task();
}

void TieTitle_Begin(SceneHeadStruct* scene_head) {
	TitleTask* task = landru_task_push(&title_vtable);
	if (task)
		task->scene_head = scene_head;
}
