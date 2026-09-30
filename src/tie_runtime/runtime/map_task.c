#include "tie_runtime/runtime/map_task.h"
#include "tie/map.h"
#include "tie/player.h"
#include "tie/talk.h"
#include <landru/cursor.h>
#include <landru/task.h>
#include <landru/view.h>
#include <landru/viewadd.h>

typedef struct MapTask {
	SceneHeadStruct* scene_head;
	bool started;
} MapTask;

static LandruTaskStepResult map_step(void* self) {
	MapTask* task = self;
	if (!task->started) {
		task->started = true;
		map_Map(task->scene_head);
		return LANDRU_TASK_STEP_CONTINUE;
	}
	talk_Free_Speech_Sound();
	player_Free_Brief_Display();
	xview_Enable_All_View_Erase();
	xview_Clear_View_Update_Function();

	if (xcursor_Is_Cursor_Visible())
		xcursor_Hide_Cursor();
	return LANDRU_TASK_STEP_DONE;
}

static const LandruTaskVtable map_vtable = { .step = map_step };

void TieMap_RunView(void) { xviewadd_Push_Handle_View_Task(); }

void TieMap_Begin(SceneHeadStruct* scene_head) {
	MapTask* task = landru_task_push(&map_vtable);
	if (!task)
		return;
	task->scene_head = scene_head;
}
