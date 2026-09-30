#include "tie_runtime/runtime/filmview_task.h"
#include "tie/filmview.h"

#include <landru/dialog.h>
#include <landru/error.h>
#include <landru/inpattr.h>
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
	int16_t result;
	bool active;
	bool scheduled;
	bool resumed;
} FilmViewFileContext;

typedef struct FilmViewDeleteContext {
	Input* root;
	Input* owner;
	int16_t result;
	bool resumed;
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
	file_context.scheduled = false;
}

static void after_file_dialog(int16_t result, void* unused) {
	void (*update)(int32_t) = NULL;
	(void)unused;
	close_file_dialog();
	/* Re-enter the view update callback; its Do_FV_File_Dialog call now
	 * returns the dialog result instead of opening the dialog again. */
	file_context.result = result;
	file_context.resumed = true;
	xview_Get_View_Update_Function(&update);
	if (update)
		update(0);
	file_context.resumed = false;
}

FileDialog* TieFilmView_OpenFileDialog(void) {
	memset(&file_context, 0, sizeof file_context);
	file_context.active = true;
	return &file_context.dialog;
}

void TieFilmView_RunFileDialog(Input* root, int16_t key_buttons) {
	file_context.root = root;
	file_context.had_key_buttons = key_buttons;
	file_context.scheduled = true;
	xdialog_Schedule_Sub_Dialog(root, after_file_dialog, NULL);
}

void TieFilmView_CloseFileDialog(int16_t key_buttons) {
	file_context.had_key_buttons = key_buttons;
	close_file_dialog();
}

bool TieFilmView_FileDialogPending(void) { return file_context.scheduled; }

bool TieFilmView_TakeFileResult(int16_t* result) {
	if (!file_context.resumed)
		return false;
	*result = file_context.result;
	file_context.resumed = false;
	return true;
}

static void close_delete_dialog(void) {
	if (!delete_context.root)
		return;
	xdialog_Remove_Dialog_From_System(delete_context.root);
	xinput_Free_Inputs(delete_context.root);
	delete_context.root = NULL;
}

static void after_delete_dialog(int16_t result, void* unused) {
	Input* owner = delete_context.owner;
	(void)unused;
	close_delete_dialog();
	if (!owner || !owner->user) {
		delete_context.owner = NULL;
		return;
	}
	/* Re-enter the requesting callback; its Do_Delete_Dialog call now
	 * returns the confirmation result instead of opening the dialog again. */
	delete_context.result = result;
	delete_context.resumed = true;
	xinpattr_Selected_Input(owner);
	owner->user(owner, 0);
	delete_context.resumed = false;
	delete_context.owner = NULL;
}

void TieFilmView_BeginDelete(Input* owner) {
	if (!delete_context.resumed)
		delete_context.owner = owner;
}

void TieFilmView_RunDelete(Input* dialog) {
	if (delete_context.root)
		return;
	if (!dialog) {
		delete_context.owner = NULL;
		xerror_Set_Landru_Error(6);
		return;
	}
	delete_context.root = dialog;
	xdialog_Schedule_Sub_Dialog(dialog, after_delete_dialog, NULL);
}

bool TieFilmView_TakeDeleteResult(int16_t* result) {
	if (!delete_context.resumed)
		return false;
	*result = delete_context.result;
	delete_context.resumed = false;
	return true;
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
		delete_context.owner = NULL;
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
