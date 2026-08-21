#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "landru/viewadd.h"
#include "tie/filmview.h"
#include "tie/shellext.h"
#include "tie/textext.h"
#include "tie_runtime/audio/config.h"
#include "tie_runtime/diagnostics/diagnostics.h"
#include "tie_runtime/display/classic_display.h"
#include "tie_runtime/display/classic_framebuffer.h"
#include "tie_runtime/flight_assets/model_types.h"
#include "tie_runtime/input/input.h"
#include "tie_runtime/runtime/exports.h"
#include "tie_runtime/runtime/profile.h"
#include "tie_runtime/storage/storage.h"
#include <landru/task.h>

#include "landru/btnpush.h"
#include "landru/dialog.h"
#include "landru/dirty.h"
#include "landru/error.h"
#include "landru/filedir.h"
#include "landru/film.h"
#include "landru/font.h"
#include "landru/inpattr.h"
#include "landru/input.h"
#include "landru/io.h"
#include "landru/paint.h"
#include "landru/rect.h"
#include "landru/res.h"
#include "landru/style.h"
#include "landru/view.h"
#include "tie/tie.h"

/* ---- Static data (initialized from binary .data segment) ---- */

static const char film_str[3][18] = { "filmview.lfd", "filmview", "filmload" };

/* ---- Static globals ---- */

static char film_name_str[5][16]; /* button labels from TEXTEXT */
static Film* filmview_film;
static Input* delete_input;
static Input* load_input;
static char filmview_name[14]; /* currently selected filename (no ext) */
static int16_t num_pages;
static int16_t cur_page;

/* replayclipname is a global shared with the flight engine */

/* ---- Forward declarations ---- */

static int16_t Build_FV_File_Dialog(Input** file, FileDialog* the_dialog, const char* string);
static void iuser_FilmView_Button(Input* input, int32_t time);
static void idraw_FilmView_Button(Input* input, Rect* r, Rect* clip_r, int16_t refresh);
static void idraw_FilmView_Page(Input* input, Rect* r, Rect* clip_r, int16_t refresh);
static void idraw_FV_File(Input* input, Rect* r, Rect* clip_r, int16_t refresh);
static int16_t iupdate_FV_File(Input* input, Rect* r, Rect* clip_r, int16_t key, uint8_t left, uint8_t right,
							   int16_t x, int16_t y);
static void iuser_FV_File(Input* input, int32_t time);
static void Select_Active_FV_File(Input* input, Rect* r, int16_t x, int16_t y);
static void Set_Active_FV_File(FileDialog* the_dialog, int16_t file, int16_t hit);
static Input* Build_Delete_Dialog(void);
static int16_t iupdate_Delete_Input(Input* input, Rect* r, Rect* clip_r, int16_t key, uint8_t left,
									uint8_t right, int16_t x, int16_t y);
static void iuser_Delete_Input(Input* input, int32_t time);
static void idraw_Delete_Input(Input* input, Rect* r, Rect* clip_r, int16_t refresh);

/* ================================================================
 * View update callback
 * ================================================================ */

/* Deferred sub-dialog contexts. Both sites are single-shot per
 * scene-life, so module-static slots are sufficient. */
typedef struct FvFileCtx {
	Input* sub_dlg;
	FileDialog file_dlg;
	int16_t was_key_buttons;
} FvFileCtx;

typedef struct FvDeleteCtx {
	Input* sub_dlg;
	Input* file_input; /* the file-list input that triggered the delete */
} FvDeleteCtx;

static FvFileCtx s_fv_file_ctx;
static FvDeleteCtx s_fv_delete_ctx;

static void after_fv_file_dialog(int16_t result, void* ctx);
static void after_fv_delete_dialog(int16_t result, void* ctx);
static void schedule_fv_file_dialog(void);

static void end_View(int32_t time) {
	(void)time;

	/* Penultimate frame: open the file dialog as a deferred
	 * sub-dialog. ViewAddTask drains the request after this
	 * callback returns and pushes the dialog task. */
	if (filmview_film->cur_cel == filmview_film->cels - 1) {
		schedule_fv_file_dialog();
	}

	/* Last frame: play the clip selected by the Film Room dialog. */
	if (filmview_film->cur_cel == filmview_film->cels)
		lerror_Set_Landru_Exit(SCENE_FILM_REPLAY);
}

/* ================================================================
 * File dialog
 * ================================================================ */

static void schedule_fv_file_dialog(void) {
	s_fv_file_ctx.was_key_buttons = lio_Is_Key_Buttons();
	if (!s_fv_file_ctx.was_key_buttons)
		lio_Set_Key_Buttons();

	memset(&s_fv_file_ctx.file_dlg, 0, sizeof(s_fv_file_ctx.file_dlg));
	s_fv_file_ctx.file_dlg.read = 1;
	filmview_name[0] = 0;

	lfiledir_Init_Directory(&s_fv_file_ctx.file_dlg.the_head, ".CLP", 0);
	lfiledir_Read_Directory(&s_fv_file_ctx.file_dlg.the_head);

	num_pages = (s_fv_file_ctx.file_dlg.the_head.count + 15) / 16;
	if (!num_pages)
		num_pages = 1;

	Input* the_input = NULL;
	int16_t built = Build_FV_File_Dialog(&the_input, &s_fv_file_ctx.file_dlg, "Load Mission Film");
	s_fv_file_ctx.file_dlg.dialog = the_input;

	if (s_fv_file_ctx.file_dlg.the_head.count) {
		Set_Active_FV_File(&s_fv_file_ctx.file_dlg, 0, 1);
		s_fv_file_ctx.file_dlg.active_hits = 0;
	}

	if (built) {
		s_fv_file_ctx.sub_dlg = the_input;
		ldialog_Schedule_Sub_Dialog(the_input, after_fv_file_dialog, &s_fv_file_ctx);
	} else {
		/* Build failed (no entries / alloc failure). Skip the
		 * dialog and fall back to MAIN_MENU. */
		lfiledir_Free_Directory(&s_fv_file_ctx.file_dlg.the_head);
		if (!s_fv_file_ctx.was_key_buttons)
			lio_Clear_Key_Buttons();
		lerror_Set_Landru_Exit(SCENE_MAIN_MENU);
	}
}

static void after_fv_file_dialog(int16_t result, void* ctx) {
	FvFileCtx* c = (FvFileCtx*)ctx;
	lfiledir_Free_Directory(&c->file_dlg.the_head);
	linput_Free_Inputs(c->sub_dlg);
	c->sub_dlg = NULL;

	if (!c->was_key_buttons)
		lio_Clear_Key_Buttons();

	/* result == 1 → user picked a clip; otherwise drop back to
	 * the main menu. */
	if (result == 1 && filmview_name[0])
		strcpy(replayclipname, filmview_name);
	else
		lerror_Set_Landru_Exit(SCENE_MAIN_MENU);
}

/* ================================================================
 * Build file dialog UI
 * ================================================================ */

static int16_t Build_FV_File_Dialog(Input** file, FileDialog* the_dialog, const char* string) {
	Rect r;
	Input *the_input, *child_input;

	/* Parent dialog */
	if (TieProfile_FrontendId() == TIE_FRONTEND_PROFILE_TIE98)
		lrect_Set_Rect(&r, 70, 14, 250, 148);
	else
		lrect_Set_Rect(&r, 0, 14, 180, 148);
	the_input = linput_Alloc_Dialog_Input(NULL, &r, 0, 0);
	linpattr_Set_Input_Draw_Function(the_input, idraw_FV_File);
	linpattr_Set_Input_Allign(the_input, TieProfile_FrontendId() == TIE_FRONTEND_PROFILE_TIE98 ? 0 : 1, 0);
	the_input->varptr = (void*)string;
	the_input->id = 0;

	/* File list area */
	lrect_Set_Rect(&r, 4, 12, 176, 77);
	child_input = linput_Alloc_Dialog_Input(the_input, &r, 0, 0);
	linpattr_Set_Input_Draw_Function(child_input, idraw_FV_File);
	linpattr_Set_Input_Update_Function(child_input, iupdate_FV_File);
	linpattr_Set_Input_User_Function(child_input, iuser_FV_File);
	child_input->mouseUsage = downInput;
	child_input->varptr = (void*)the_dialog;
	child_input->id = 1;

	/* Left page button */
	lrect_Set_Rect(&r, 4, 36, 18, 52);
	child_input = (Input*)lbtnpush_Alloc_Button(the_input, &r, 0, iuser_FilmView_Button, NULL, 0);
	linpattr_Set_Input_Draw_Function(child_input, idraw_FilmView_Button);
	linpattr_Set_Input_Allign(child_input, 0, 2);
	child_input->varptr = (void*)the_dialog;

	/* Right page button */
	lrect_Set_Rect(&r, 4, 36, 18, 52);
	child_input = (Input*)lbtnpush_Alloc_Button(the_input, &r, 0, iuser_FilmView_Button, NULL, 1);
	linpattr_Set_Input_Draw_Function(child_input, idraw_FilmView_Button);
	linpattr_Set_Input_Allign(child_input, 2, 2);
	child_input->varptr = (void*)the_dialog;

	/* Page counter display */
	lrect_Set_Rect(&r, 0, 37, 140, 51);
	child_input = linput_Alloc_Input(the_input, &r, 0, 0);
	linpattr_Set_Input_Draw_Function(child_input, idraw_FilmView_Page);
	linpattr_Set_Input_Allign(child_input, 1, 2);
	child_input->id = 1;

	/* Load button (only if files exist) */
	if (the_dialog->the_head.count) {
		lrect_Set_Rect(&r, 4, 4, 88, 18);
		child_input = (Input*)lbtnpush_Alloc_Button(the_input, &r, 0, iuser_FV_File, film_name_str[2], 2);
		linpattr_Set_Input_Draw_Function(child_input, idraw_FV_File);
		linpattr_Set_Input_Allign(child_input, 0, 2);
		load_input = child_input;
	} else {
		load_input = NULL;
	}

	/* Exit button */
	lrect_Set_Rect(&r, 4, 4, 88, 18);
	child_input = (Input*)lbtnpush_Alloc_Button(the_input, &r, 0, iuser_FV_File, film_name_str[3], 3);
	linpattr_Set_Input_Draw_Function(child_input, idraw_FV_File);
	linpattr_Set_Input_Allign(child_input, 2, 2);

	/* Delete button (only if files exist) */
	if (the_dialog->the_head.count) {
		lrect_Set_Rect(&r, 4, 20, 88, 34);
		child_input = (Input*)lbtnpush_Alloc_Button(the_input, &r, 0, iuser_FV_File, film_name_str[0], 4);
		linpattr_Set_Input_Draw_Function(child_input, idraw_FV_File);
		linpattr_Set_Input_Allign(child_input, 0, 2);
		child_input->varptr = (void*)the_dialog;
		delete_input = child_input;
	} else {
		delete_input = NULL;
	}

	/* Replay button (only if replayclipname exists) */
	if (replayclipname[0]) {
		lrect_Set_Rect(&r, 4, 20, 88, 34);
		child_input = (Input*)lbtnpush_Alloc_Button(the_input, &r, 0, iuser_FV_File, film_name_str[4], 5);
		linpattr_Set_Input_Draw_Function(child_input, idraw_FV_File);
		linpattr_Set_Input_Allign(child_input, 2, 2);
	}

	*file = the_input;
	return 1;
}

/* ================================================================
 * Page button callbacks
 * ================================================================ */

static void iuser_FilmView_Button(Input* input, int32_t time) {
	(void)time;
	FileDialog* the_dialog = (FileDialog*)input->varptr;

	if (!linpattr_Get_Input_Selected(input))
		return;

	if (input->id) {
		/* Right button: next page */
		if (cur_page >= num_pages - 1)
			cur_page = 0;
		else
			cur_page++;
	} else {
		/* Left button: previous page */
		if (cur_page <= 0) {
			cur_page = num_pages - 1;
		} else {
			cur_page--;
			lview_Refresh_View();
		}
	}

	if (num_pages > 1) {
		the_dialog->name_offset = 16 * cur_page;
		Set_Active_FV_File(the_dialog, the_dialog->name_offset, 0);
		linpattr_Refresh_Input(the_dialog->dialog);
	}
}

static void idraw_FilmView_Button(Input* input, Rect* r, Rect* clip_r, int16_t refresh) {
	if (!refresh)
		return;

	PushButton* btn = (PushButton*)input;
	lstyle_Style_Paint_Border(r, btn->pressed);

	uint8_t icon = input->id ? iconRightArrow : iconLeftArrow;
	lstyle_Style_Draw_Centered_Icon(icon, r, clip_r, btn->pressed);

	if (linpattr_Is_Input_Dirty(input))
		ldirty_Dirty_Rect(clip_r);
}

static void idraw_FilmView_Page(Input* input, Rect* r, Rect* clip_r, int16_t refresh) {
	(void)clip_r;
	if (!refresh)
		return;

	lstyle_Style_Paint_TextField(r);

	char string[32];
	snprintf(string, sizeof(string), "Page %d/%d", cur_page + 1, num_pages);
	lfont_Print_Centered_Text(string, r, 15, 1);

	if (linpattr_Is_Input_Dirty(input))
		ldirty_Dirty_Rect(clip_r);
}

/* ================================================================
 * File list draw
 * ================================================================ */

static void idraw_FV_File(Input* input, Rect* r, Rect* clip_r, int16_t refresh) {
	if (!refresh)
		return;

	Rect dr;
	lrect_Copy_Rect(&dr, r);

	switch (input->id) {
		case 0:
			/* Title bar */
			lstyle_Style_Paint_Base(r, 0);
			lstyle_Style_Trim_Base(&dr);
			lfont_Print_Clipped_Text((const char*)input->varptr, dr.left + 35, dr.top + 1, 0, 21);
			break;

		case 1: {
			/* File list (two columns, 8 per column) */
			lstyle_Style_Paint_TextField(r);
			lstyle_Style_Trim_TextField(&dr);

			FileDialog* the_dialog = (FileDialog*)input->varptr;
			DirEntry* entries = the_dialog->the_head.entries;

			int16_t off_y = dr.top;
			int16_t off_x = dr.left;
			int16_t half = ((dr.right - dr.left) >> 1) - 1;
			int16_t count;

			lpaint_Vert_Clipped_Line(dr.left + half, dr.top, dr.bottom - dr.top, 2);

			for (count = the_dialog->name_offset;
				 count < the_dialog->name_offset + 16 && count < the_dialog->the_head.count; count++) {
				int16_t color;
				if (count == the_dialog->active_name) {
					Rect dr2;
					lrect_Set_Rect(&dr2, off_x, off_y, half + off_x, off_y + 7);
					lpaint_Paint_Clipped_Rect(&dr2, 2);
					color = 16;
				} else {
					color = 15;
				}

				lfont_Print_Clipped_Text(entries[count].name, off_x + 4, off_y + 1, 1, color);

				char size_str[16];
				snprintf(size_str, sizeof(size_str), "%uK", (unsigned)entries[count].size_kb);

				int16_t old_font = lfont_Get_Font();
				lfont_Set_Font(1);
				int16_t width = lfont_Get_String_Width(size_str) + 8;
				lfont_Set_Font(old_font);

				lfont_Print_Clipped_Text(size_str, half + off_x - width, off_y + 1, 1, color);

				/* After 8th file in left column, switch to right column */
				if (count == the_dialog->name_offset + 7) {
					off_y = dr.top;
					off_x = half + dr.left + 1;
				} else {
					off_y += 8;
				}
			}
			break;
		}

		case 2:
		case 3:
		case 4:
		case 5: {
			/* Styled button with text label */
			PushButton* btn = (PushButton*)input;
			lstyle_Style_Paint_Border(r, btn->pressed);
			if (btn->name) {
				lstyle_Style_Button_Text(btn->name, r, btn->pressed);
			}
			break;
		}

		default:
			break;
	}

	if (linpattr_Is_Input_Dirty(input))
		ldirty_Dirty_Rect(clip_r);
}

/* ================================================================
 * File list input callbacks
 * ================================================================ */

static int16_t iupdate_FV_File(Input* input, Rect* r, Rect* clip_r, int16_t key, uint8_t left, uint8_t right,
							   int16_t x, int16_t y) {
	(void)clip_r;
	(void)left;
	(void)right;

	if (key) {
		if (input->id == 1 && key == 13) {
			ldialog_Set_Dialog_Exit(1);
			return 1;
		}
		return 0;
	}

	if (input->id == 1)
		Select_Active_FV_File(input, r, x, y);
	return 1;
}

static void iuser_FV_File(Input* input, int32_t time) {
	(void)time;

	switch (input->id) {
		case 2: /* Load */
			if (linpattr_Get_Input_Selected(input))
				ldialog_Set_Dialog_Exit(1);
			break;

		case 3: /* Exit */
			if (linpattr_Get_Input_Selected(input))
				ldialog_Set_Dialog_Exit(2);
			break;

		case 4: { /* Delete */
			if (!linpattr_Get_Input_Selected(input) || !filmview_name[0])
				break;

			/* Open delete-confirmation as a deferred sub-dialog. The
			 * file-list input pointer is captured into the context so
			 * the handler can locate the active file and remove it. */
			s_fv_delete_ctx.sub_dlg = Build_Delete_Dialog();
			s_fv_delete_ctx.file_input = input;
			ldialog_Schedule_Sub_Dialog(s_fv_delete_ctx.sub_dlg, after_fv_delete_dialog, &s_fv_delete_ctx);
			break;
		}

		case 5: /* Replay last */
			if (linpattr_Get_Input_Selected(input)) {
				ldialog_Set_Dialog_Exit(1);
				strcpy(filmview_name, replayclipname);
			}
			break;

		default:
			break;
	}
}

/* ================================================================
 * File selection helpers
 * ================================================================ */

static void Select_Active_FV_File(Input* input, Rect* r, int16_t x, int16_t y) {
	FileDialog* the_dialog = (FileDialog*)input->varptr;
	int16_t file = the_dialog->name_offset + (y / 8);

	/* Right column adds 8 */
	if (x > (r->right - r->left) / 2)
		file += 8;

	Set_Active_FV_File(the_dialog, file, 1);
}

static void Set_Active_FV_File(FileDialog* the_dialog, int16_t file, int16_t hit) {
	int16_t change = 0;
	int16_t scroll = 0;

	if (file >= the_dialog->the_head.count)
		file = the_dialog->the_head.count - 1;
	if (file < 0)
		file = 0;

	if (the_dialog->active_name == file) {
		if (hit) {
			the_dialog->active_hits++;
			change = 1;
		}
	} else {
		the_dialog->active_name = file;
		the_dialog->active_hits = hit;

		if (the_dialog->active_name < the_dialog->name_offset)
			scroll = 1;
		if (the_dialog->active_name >= the_dialog->name_offset + 16)
			scroll++;
		change = 1;
	}

	if (scroll) {
		int16_t offset = the_dialog->active_name - (the_dialog->active_name % 16);
		if (the_dialog->name_offset != offset) {
			the_dialog->name_offset = offset;
			cur_page = (offset + 15) / 16;
			scroll++;
		}
	}

	if (change) {
		DirEntry* entries = the_dialog->the_head.entries;
		if (file < the_dialog->the_head.count) {
			if (the_dialog->active_hits > 1)
				ldialog_Set_Dialog_Exit(1);
			strcpy(filmview_name, entries[file].name);
		}
	}

	if (change || scroll)
		linpattr_Refresh_Input(the_dialog->dialog);
}

static int16_t Clip_Active_FV_File(FileDialog* the_dialog) {
	int16_t value = the_dialog->active_name;
	if (value < the_dialog->name_offset)
		value = the_dialog->name_offset;
	if (value >= the_dialog->name_offset + 16)
		return the_dialog->name_offset + 15;
	return value;
}

/* ================================================================
 * Delete confirmation dialog
 * ================================================================ */

static void after_fv_delete_dialog(int16_t result, void* ctx) {
	FvDeleteCtx* c = (FvDeleteCtx*)ctx;
	linput_Free_Inputs(c->sub_dlg);
	c->sub_dlg = NULL;

	if (result == 2) {
		/* Cancelled */
		return;
	}

	Input* input = c->file_input;
	FileDialog* the_dialog = (FileDialog*)input->varptr;
	DirEntry* entries = the_dialog->the_head.entries;
	int16_t idx = the_dialog->active_name;

	if (idx >= the_dialog->the_head.count)
		return;

	/* Remove the file */
	char file_name[16];
	strcpy(file_name, filmview_name);
	strcat(file_name, ".clp");
	TieStorage_Remove(TIE_FILE_ROOT_USER, file_name);

	/* Shift remaining entries down */
	int16_t j;
	for (j = idx; j < the_dialog->the_head.count - 1; j++)
		entries[j] = entries[j + 1];

	if (the_dialog->active_name >= --the_dialog->the_head.count && the_dialog->active_name)
		the_dialog->active_name--;

	filmview_name[0] = 0;
	the_dialog->active_hits = 0;

	if (the_dialog->the_head.count) {
		Set_Active_FV_File(the_dialog, 0, 1);
		the_dialog->active_hits = 0;
	} else {
		if (load_input)
			linpattr_Hide_Input(load_input);
		if (delete_input)
			linpattr_Hide_Input(delete_input);
	}
	lview_Refresh_View();
}

static Input* Build_Delete_Dialog(void) {
	Rect r;
	Input *parent_input, *input;

	lrect_Set_Rect(&r, 0, 0, 180, 46);
	parent_input = linput_Alloc_Dialog_Input(NULL, &r, 0, 0);
	linpattr_Set_Input_Update_Function(parent_input, iupdate_Delete_Input);
	linpattr_Set_Input_Draw_Function(parent_input, idraw_Delete_Input);
	linpattr_Set_Input_Allign(parent_input, 1, 1);
	linpattr_Show_Input(parent_input);
	parent_input->id = 0;

	/* Delete button */
	lrect_Set_Rect(&r, 4, 4, 76, 20);
	input = (Input*)lbtnpush_Alloc_Button(parent_input, &r, 0, iuser_Delete_Input, film_name_str[0], 1);
	linpattr_Set_Input_Allign(input, 0, 2);

	/* Cancel button */
	lrect_Set_Rect(&r, 4, 4, 76, 20);
	input = (Input*)lbtnpush_Alloc_Button(parent_input, &r, 0, iuser_Delete_Input, film_name_str[1], 2);
	linpattr_Set_Input_Allign(input, 2, 2);

	return parent_input;
}

static int16_t iupdate_Delete_Input(Input* input, Rect* r, Rect* clip_r, int16_t key, uint8_t left,
									uint8_t right, int16_t x, int16_t y) {
	(void)input;
	(void)r;
	(void)clip_r;
	(void)left;
	(void)right;
	(void)x;
	(void)y;

	(void)key;
	return 0;
}

static void iuser_Delete_Input(Input* input, int32_t time) {
	(void)time;
	if (!linpattr_Get_Input_Selected(input))
		return;

	int16_t id = input->id;
	if (id >= 1 && id <= 2)
		ldialog_Set_Dialog_Exit(id);
}

static void idraw_Delete_Input(Input* input, Rect* r, Rect* clip_r, int16_t refresh) {
	if (!refresh)
		return;

	char string[32];
	strcpy(string, film_name_str[0]); /* "Delete" */
	strcat(string, " ");
	strcat(string, filmview_name);
	strcat(string, "?");

	if (!input->id) {
		Rect dr;
		lrect_Copy_Rect(&dr, r);
		lpaint_Frame_Clipped_Rect(&dr, 16);
		lrect_Inset_Rect(&dr, 1, 1);
		lstyle_Style_Paint_Border(&dr, 0);
		dr.bottom = dr.top + 14;
		lfont_Enable_FontID_Shadow(0);
		lfont_Print_Centered_Text(string, &dr, 15, 0);
		lfont_Disable_FontID_Shadow(0);
	}

	if (linpattr_Is_Input_Dirty(input))
		ldirty_Dirty_Rect(clip_r);
}

/* ================================================================
 * Entry point
 * ================================================================ */

typedef enum {
	FILMVIEW_PHASE_BEGIN = 0,
	FILMVIEW_PHASE_CLEANUP = 1,
} FilmViewPhase;

typedef struct FilmViewTask {
	SceneHeadStruct* scene_head;
	ResFile* res_file;
	FilmViewPhase phase;
} FilmViewTask;

static LandruTaskStepResult filmview_task_step(void* self) {
	FilmViewTask* t = (FilmViewTask*)self;

	if (t->phase == FILMVIEW_PHASE_BEGIN) {
		Rect r;

		cur_page = 0;
		num_pages = 1;

		/* If last scene was 110, clear the replay clip name */
		if (t->scene_head->last_scene == SCENE_MAIN_MENU)
			replayclipname[0] = 0;

		/* Load resources */
		t->res_file = shellext_Open_Empire_Resource(film_str[0]);

		lrect_Set_Rect(&r, 0, 0, 320, 200);

		/* Fill button label strings from TEXTEXT */
		int16_t i;
		for (i = 0; i < 5; i++) {
			const char* text = textext_Get_Text((TIEText)(txtFilmDelete + i));
			strcpy(film_name_str[i], text);
		}

		/* Load the film room film */
		filmview_film = lfilm_Res_Film(film_str[1], &r, 0, 0, 0);
		lfilm_Set_Film_Def_Palette(filmview_film, t->scene_head->def_palette);

		/* Push the modal view task */
		lview_Set_View_Update_Function(end_View);
		lviewadd_Push_Handle_View_Task();

		t->phase = FILMVIEW_PHASE_CLEANUP;
		return LANDRU_TASK_STEP_CONTINUE;
	}

	/* CLEANUP */
	lview_Clear_View_Update_Function();
	lres_Close_Resource(t->res_file);
	return LANDRU_TASK_STEP_DONE;
}

static const LandruTaskVtable filmview_task_vt = {
	.step = filmview_task_step,
};

void filmview_Push_FilmView_Task(SceneHeadStruct* scene_head) {
	FilmViewTask* t = (FilmViewTask*)landru_task_push(&filmview_task_vt);
	if (!t)
		return;
	t->scene_head = scene_head;
	t->res_file = NULL;
	t->phase = FILMVIEW_PHASE_BEGIN;
}
