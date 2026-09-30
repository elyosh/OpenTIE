#include "tie_runtime/runtime/brief_task.h"
#include "tie/brief.h"
#include "tie_runtime/runtime/profile.h"

#include <landru/dialog.h>
#include <landru/task.h>
#include <landru/viewadd.h>

typedef enum BriefPhase {
	BRIEF_BEGIN,
	BRIEF_NOTICE,
	BRIEF_VIEW,
	BRIEF_CLEANUP,
} BriefPhase;

typedef struct BriefTask {
	SceneHeadStruct* scene_head;
	Input* notice;
	bool svga;
	BriefPhase phase;
} BriefTask;

static LandruTaskStepResult brief_step(void* self) {
	BriefTask* task = self;
	switch (task->phase) {
		case BRIEF_BEGIN:
			if (brief_OpenScene(task->scene_head, task->svga, &task->notice)) {
				xdialog_Push_Dialog_View_Task(task->notice);
				task->phase = BRIEF_NOTICE;
			} else {
				task->phase = BRIEF_VIEW;
			}
			return LANDRU_TASK_STEP_CONTINUE;
		case BRIEF_NOTICE:
			brief_CloseNotice(task->notice);
			task->notice = NULL;
			task->phase = BRIEF_VIEW;
			return LANDRU_TASK_STEP_CONTINUE;
		case BRIEF_VIEW:
			brief_PrepareView();
			xviewadd_Push_Handle_View_Task();
			task->phase = BRIEF_CLEANUP;
			return LANDRU_TASK_STEP_CONTINUE;
		case BRIEF_CLEANUP:
			brief_CloseScene(task->svga);
			return LANDRU_TASK_STEP_DONE;
	}
	return LANDRU_TASK_STEP_DONE;
}

static const LandruTaskVtable brief_vtable = { brief_step, NULL, NULL, NULL };

void TieBrief_Begin(SceneHeadStruct* scene_head) {
	BriefTask* task = landru_task_push(&brief_vtable);
	if (!task)
		return;
	task->scene_head = scene_head;
	task->notice = NULL;
	task->svga = TieProfile_FrontendId() == TIE_FRONTEND_PROFILE_TIE98;
	task->phase = BRIEF_BEGIN;
}
