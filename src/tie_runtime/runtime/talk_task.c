#include "tie_runtime/runtime/talk_task.h"
#include "tie/player.h"
#include "tie/talk.h"
#include <landru/cursor.h>
#include <landru/res.h>
#include <landru/task.h>
#include <landru/view.h>
#include <landru/viewadd.h>

typedef struct TalkTask {
	SceneHeadStruct* scene_head;
	bool started;
	ResFile* resource;
} TalkTask;

static LandruTaskStepResult talk_step(void* self) {
	TalkTask* task = self;
	if (!task->started) {
		task->started = true;
		talk_Talk(task->scene_head);
		return LANDRU_TASK_STEP_CONTINUE;
	}
	talk_Free_Speech_Sound();
	xview_Enable_All_View_Erase();
	xview_Clear_View_Update_Function();

	if (xcursor_Is_Cursor_Visible())
		xcursor_Hide_Cursor();

	player_Free_Display_Map();
	xres_Close_Resource(task->resource);
	return LANDRU_TASK_STEP_DONE;
}

static const LandruTaskVtable talk_vtable = { .step = talk_step };

void TieTalk_RunView(ResFile* resource) {
	TalkTask* task = landru_task_top();
	task->resource = resource;
	xviewadd_Push_Handle_View_Task();
}

void TieTalk_Begin(SceneHeadStruct* scene_head) {
	TalkTask* task = landru_task_push(&talk_vtable);
	if (!task)
		return;
	task->scene_head = scene_head;
}
