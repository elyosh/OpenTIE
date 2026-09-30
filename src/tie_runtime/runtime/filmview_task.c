#include "tie_runtime/runtime/filmview_task.h"
#include "tie/filmview.h"

#include <landru/dialog.h>
#include <landru/error.h>
#include <landru/inpcall.h>
#include <landru/io.h>
#include <landru/task.h>
#include <landru/view.h>
#include <landru/viewadd.h>
#include <string.h>

typedef struct FilmViewFileContext {
	FileDialog dialog;
	Input* root;
	int16_t had_key_buttons;
	bool active;
} FilmViewFileContext;

typedef struct FilmViewDeleteContext {
	Input* root;
	Input* file_input;
} FilmViewDeleteContext;

static FilmViewFileContext file_context;
static FilmViewDeleteContext delete_context;

static void close_file_dialog(void) {
	if (!file_context.active)
		return;
	xfiledir_Free_Directory(&file_context.dialog.the_head);
	if (file_context.root) {
		xdialog_Remove_Dialog_From_System(file_context.root);
		xinput_Free_Inputs(file_context.root);
	}
	if (!file_context.had_key_buttons)
		xio_Clear_Key_Buttons();
	file_context.root = NULL;
	file_context.active = false;
}

static void after_file_dialog(int16_t result, void* unused) {
	(void)unused;
	close_file_dialog();
	filmview_ApplySelectedFile(result);
}

void TieFilmView_RequestFiles(void) {
	if (file_context.active)
		return;
	memset(&file_context, 0, sizeof file_context);
	file_context.active = true;
	file_context.had_key_buttons = xio_Is_Key_Buttons();
	if (!file_context.had_key_buttons)
		xio_Set_Key_Buttons();
	if (!filmview_PrepareFileDialog(&file_context.dialog, &file_context.root)) {
		close_file_dialog();
		xerror_Set_Landru_Exit(SCENE_MAIN_MENU);
		return;
	}
	xdialog_Schedule_Sub_Dialog(file_context.root, after_file_dialog, NULL);
}

static void close_delete_dialog(void) {
	if (!delete_context.root)
		return;
	xdialog_Remove_Dialog_From_System(delete_context.root);
	xinput_Free_Inputs(delete_context.root);
	delete_context.root = NULL;
}

static void after_delete_dialog(int16_t result, void* unused) {
	(void)unused;
	close_delete_dialog();
	if (result != 2)
		filmview_CompleteDelete(delete_context.file_input);
	delete_context.file_input = NULL;
}

void TieFilmView_RequestDelete(Input* input) {
	if (delete_context.root)
		return;
	delete_context.root = filmview_BuildDeleteDialog();
	if (!delete_context.root) {
		xerror_Set_Landru_Error(6);
		return;
	}
	delete_context.file_input = input;
	xdialog_Schedule_Sub_Dialog(delete_context.root, after_delete_dialog, NULL);
}

typedef struct FilmViewTask {
	SceneHeadStruct* scene_head;
	ResFile* resource;
	bool started;
} FilmViewTask;

static LandruTaskStepResult filmview_step(void* self) {
	FilmViewTask* task = self;
	if (task->started)
		return LANDRU_TASK_STEP_DONE;
	task->started = true;
	filmview_FilmView(task->scene_head);
	return LANDRU_TASK_STEP_CONTINUE;
}

static void filmview_end(void* self) {
	FilmViewTask* task = self;
	if (!task->started)
		return;
	/* These dialogs belong to this scene; children have already been popped. */
	if (file_context.active || delete_context.root) {
		xdialog_Schedule_Sub_Dialog(NULL, NULL, NULL);
		xinpcall_Clear_Active_Input();
		close_delete_dialog();
		close_file_dialog();
		delete_context.file_input = NULL;
	}
	xview_Clear_View_Update_Function();
	if (task->resource)
		xres_Close_Resource(task->resource);
}

static const LandruTaskVtable filmview_vtable = { filmview_step, filmview_end, NULL, NULL };

void TieFilmView_RunView(ResFile* resource, bool ready) {
	FilmViewTask* task = landru_task_top();
	task->resource = resource;
	if (ready)
		xviewadd_Push_Handle_View_Task();
	else
		xerror_Set_Landru_Error(6);
}

void TieFilmView_Begin(SceneHeadStruct* scene_head) {
	FilmViewTask* task = landru_task_push(&filmview_vtable);
	if (task)
		task->scene_head = scene_head;
}
