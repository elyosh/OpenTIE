#include "tie/filmview.h"
#include "landru/viewadd.h"
#include "tie/edition.h"
#include "tie/shellext.h"
#include "tie/textext.h"
#include "tie_runtime/runtime/filmview_task.h"
#include "tie_runtime/runtime/profile.h"
#include "tie_runtime/storage/storage.h"

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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- Static data (initialized from binary .data segment) ---- */

/* ---- Static globals ---- */

// GLOBAL: TIE95 0xF5E18
// GLOBAL: TIE98 0x51F880
static char film_name_str[5][16]; /* button labels from TEXTEXT */
// GLOBAL: TIE95 0xF5E68
// GLOBAL: TIE98 0x51F8D0
static Film* filmview_film;
// GLOBAL: TIE95 0xF5E6C
// GLOBAL: TIE98 0x51F878
static Input* delete_input;
// GLOBAL: TIE95 0xF5E70
// GLOBAL: TIE98 0x51F8D8
static Input* load_input;
// GLOBAL: TIE95 0xF5E74
// GLOBAL: TIE98 0x51F8E0
static char filmview_name[14]; /* currently selected filename (no ext) */
// GLOBAL: TIE95 0xF5E82
// GLOBAL: TIE98 0x51F8D4
static int16_t num_pages;
// GLOBAL: TIE95 0xF5E84
// GLOBAL: TIE98 0x51F8DC
static int16_t cur_page;

/* replayclipname is a global shared with the flight engine */

/* ---- Forward declarations ---- */

static int16_t filmview_Do_FV_File_Dialog(void);
static int16_t filmview_Build_FV_File_Dialog(Input** file, FileDialog* the_dialog, const char* string);
static void filmview_iuser_FilmView_Button(Input* input, int32_t time);
static void filmview_idraw_FilmView_Button(Input* input, Rect* r, Rect* clip_r, int16_t refresh);
static void filmview_idraw_FilmView_Page(Input* input, Rect* r, Rect* clip_r, int16_t refresh);
static void filmview_idraw_FV_File(Input* input, Rect* r, Rect* clip_r, int16_t refresh);
static int16_t filmview_iupdate_FV_File(Input* input, Rect* r, Rect* clip_r, int16_t key, uint8_t left,
										uint8_t right, int16_t x, int16_t y);
static void filmview_iuser_FV_File(Input* input, int32_t time);
static void filmview_Select_Active_FV_File(Input* input, Rect* r, int16_t x, int16_t y);
static void filmview_Set_Active_FV_File(FileDialog* the_dialog, int16_t file, int16_t hit);

static int16_t filmview_Do_Delete_Dialog(void);
static Input* filmview_Build_Delete_Dialog(void);
static int16_t filmview_iupdate_Delete_Input(Input* input, Rect* r, Rect* clip_r, int16_t key, uint8_t left,
											 uint8_t right, int16_t x, int16_t y);
static void filmview_iuser_Delete_Input(Input* input, int32_t time);
static void filmview_idraw_Delete_Input(Input* input, Rect* r, Rect* clip_r, int16_t refresh);

/* ================================================================
 * View update callback
 * ================================================================ */

// FUNCTION: TIE95 0x7177C
// FUNCTION: TIE98 0x41D940
static void filmview_end_View(int32_t time) {
	(void)time;

	/* Penultimate frame: choose the clip to play. */
	if (filmview_film->cur_cel == filmview_film->cels - 1) {
		if (filmview_Do_FV_File_Dialog() && filmview_name[0])
			strcpy(replayclipname, filmview_name);
		else
#ifdef TIE_MODERN
			/* The file dialog runs as a sub-dialog and re-enters this
			 * callback with its result. */
			if (!TieFilmView_FileDialogPending())
#endif
				xerror_Set_Landru_Exit(SCENE_MAIN_MENU);
	}

	/* Last frame: play the clip selected by the Film Room dialog. */
	if (filmview_film->cur_cel == filmview_film->cels)
		xerror_Set_Landru_Exit(SCENE_FILM_REPLAY);
}

/* ================================================================
 * File dialog
 * ================================================================ */

// FUNCTION: TIE95 0x717F8
// FUNCTION: TIE98 0x41D9C0
static int16_t filmview_Do_FV_File_Dialog(void) {
#ifdef TIE_MODERN
	FileDialog* the_dialog;
#else
	FileDialog dialog;
	FileDialog* the_dialog = &dialog;
#endif
	Input* file = NULL;
	int16_t key_buttons;
	int16_t result;

#ifdef TIE_MODERN
	if (TieFilmView_TakeFileResult(&result))
		return result == 1;
	the_dialog = TieFilmView_OpenFileDialog();
#endif
	key_buttons = xio_Is_Key_Buttons();
	if (!key_buttons)
		xio_Set_Key_Buttons();

	the_dialog->name_offset = 0;
	the_dialog->active_name = 0;
	the_dialog->active_hits = 0;
	filmview_name[0] = 0;
	the_dialog->read = 1;
	xfiledir_Init_Directory(&the_dialog->the_head, ".CLP", 0);
	xfiledir_Read_Directory(&the_dialog->the_head);
	num_pages = (the_dialog->the_head.count + 15) / 16;
	if (!num_pages)
		num_pages = 1;

	result = filmview_Build_FV_File_Dialog(&file, the_dialog, "Load Mission Film");
	the_dialog->dialog = file;
	if (result && the_dialog->the_head.count) {
		filmview_Set_Active_FV_File(the_dialog, 0, 1);
		the_dialog->active_hits = 0;
	}

	if (result) {
#ifdef TIE_MODERN
		TieFilmView_RunFileDialog(file, key_buttons);
		return 0;
#else
		result = xdialog_Handle_Dialog_View(file);
		xfiledir_Free_Directory(&the_dialog->the_head);
		xinput_Free_Inputs(file);
#endif
	}

#ifdef TIE_MODERN
	TieFilmView_CloseFileDialog(key_buttons);
#else
	if (!key_buttons)
		xio_Clear_Key_Buttons();
#endif
	return result == 1;
}

/* ================================================================
 * Build file dialog UI
 * ================================================================ */

// FUNCTION: TIE95 0x71960
// FUNCTION: TIE98 0x41DB30
static int16_t filmview_Build_FV_File_Dialog(Input** file, FileDialog* the_dialog, const char* string) {
	Rect r;
	Input *the_input, *child_input;

	/* Parent dialog */
	xrect_Set_Rect(&r, TIE_FRONTEND_EDITION(0, 70), 14, TIE_FRONTEND_EDITION(180, 250), 148);
	the_input = xinput_Alloc_Dialog_Input(NULL, &r, 0, 0);
	if (!the_input)
		return 0;
	xinpattr_Set_Input_Draw_Function(the_input, filmview_idraw_FV_File);
	xinpattr_Set_Input_Allign(the_input, TIE_FRONTEND_EDITION(1, 0), 0);
	the_input->varptr = (void*)string;
	the_input->id = 0;

	/* File list area */
	xrect_Set_Rect(&r, 4, 12, 176, 77);
	child_input = xinput_Alloc_Dialog_Input(the_input, &r, 0, 0);
	if (!child_input) {
		xinput_Free_Inputs(the_input);
		return 0;
	}
	xinpattr_Set_Input_Draw_Function(child_input, filmview_idraw_FV_File);
	xinpattr_Set_Input_Update_Function(child_input, filmview_iupdate_FV_File);
	xinpattr_Set_Input_User_Function(child_input, filmview_iuser_FV_File);
	child_input->mouseUsage = downInput;
	child_input->varptr = (void*)the_dialog;
	child_input->id = 1;

	/* Left page button */
	xrect_Set_Rect(&r, 4, 36, 18, 52);
	child_input = (Input*)xbtnpush_Alloc_Button(the_input, &r, 0, filmview_iuser_FilmView_Button, NULL, 0);
	if (!child_input) {
		xinput_Free_Inputs(the_input);
		return 0;
	}
	xinpattr_Set_Input_Draw_Function(child_input, filmview_idraw_FilmView_Button);
	xinpattr_Set_Input_Allign(child_input, 0, 2);
	child_input->varptr = (void*)the_dialog;

	/* Right page button */
	xrect_Set_Rect(&r, 4, 36, 18, 52);
	child_input = (Input*)xbtnpush_Alloc_Button(the_input, &r, 0, filmview_iuser_FilmView_Button, NULL, 1);
	if (!child_input) {
		xinput_Free_Inputs(the_input);
		return 0;
	}
	xinpattr_Set_Input_Draw_Function(child_input, filmview_idraw_FilmView_Button);
	xinpattr_Set_Input_Allign(child_input, 2, 2);
	child_input->varptr = (void*)the_dialog;

	/* Page counter display */
	xrect_Set_Rect(&r, 0, 37, 140, 51);
	child_input = xinput_Alloc_Input(the_input, &r, 0, 0);
	if (!child_input) {
		xinput_Free_Inputs(the_input);
		return 0;
	}
	xinpattr_Set_Input_Draw_Function(child_input, filmview_idraw_FilmView_Page);
	xinpattr_Set_Input_Allign(child_input, 1, 2);
	child_input->id = 1;

	/* Load button (only if files exist) */
	if (the_dialog->the_head.count) {
		xrect_Set_Rect(&r, 4, 4, 88, 18);
		child_input =
			(Input*)xbtnpush_Alloc_Button(the_input, &r, 0, filmview_iuser_FV_File, film_name_str[2], 2);
		if (!child_input) {
			xinput_Free_Inputs(the_input);
			return 0;
		}
		xinpattr_Set_Input_Draw_Function(child_input, filmview_idraw_FV_File);
		xinpattr_Set_Input_Allign(child_input, 0, 2);
		load_input = child_input;
	} else {
		load_input = NULL;
	}

	/* Exit button */
	xrect_Set_Rect(&r, 4, 4, 88, 18);
	child_input =
		(Input*)xbtnpush_Alloc_Button(the_input, &r, 0, filmview_iuser_FV_File, film_name_str[3], 3);
	if (!child_input) {
		xinput_Free_Inputs(the_input);
		return 0;
	}
	xinpattr_Set_Input_Draw_Function(child_input, filmview_idraw_FV_File);
	xinpattr_Set_Input_Allign(child_input, 2, 2);

	/* Delete button (only if files exist) */
	if (the_dialog->the_head.count) {
		xrect_Set_Rect(&r, 4, 20, 88, 34);
		child_input =
			(Input*)xbtnpush_Alloc_Button(the_input, &r, 0, filmview_iuser_FV_File, film_name_str[0], 4);
		if (!child_input) {
			xinput_Free_Inputs(the_input);
			return 0;
		}
		xinpattr_Set_Input_Draw_Function(child_input, filmview_idraw_FV_File);
		xinpattr_Set_Input_Allign(child_input, 0, 2);
		child_input->varptr = (void*)the_dialog;
		delete_input = child_input;
	} else {
		delete_input = NULL;
	}

	/* Replay button (only if replayclipname exists) */
	if (replayclipname[0]) {
		xrect_Set_Rect(&r, 4, 20, 88, 34);
		child_input =
			(Input*)xbtnpush_Alloc_Button(the_input, &r, 0, filmview_iuser_FV_File, film_name_str[4], 5);
		if (!child_input) {
			xinput_Free_Inputs(the_input);
			return 0;
		}
		xinpattr_Set_Input_Draw_Function(child_input, filmview_idraw_FV_File);
		xinpattr_Set_Input_Allign(child_input, 2, 2);
	}

	*file = the_input;
	return 1;
}

/* ================================================================
 * Page button callbacks
 * ================================================================ */

// FUNCTION: TIE95 0x71C78
// FUNCTION: TIE98 0x41DE70
static void filmview_iuser_FilmView_Button(Input* input, int32_t time) {
	FileDialog* the_dialog = (FileDialog*)input->varptr;
	(void)time;

	if (!xinpattr_Get_Input_Selected(input))
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
			xview_Refresh_View();
		}
	}

	if (num_pages > 1) {
		the_dialog->name_offset = 16 * cur_page;
		filmview_Set_Active_FV_File(the_dialog, the_dialog->name_offset, 0);
		xinpattr_Refresh_Input(the_dialog->dialog);
	}
}

// FUNCTION: TIE95 0x71D20
// FUNCTION: TIE98 0x41DF10
static void filmview_idraw_FilmView_Button(Input* input, Rect* r, Rect* clip_r, int16_t refresh) {
	PushButton* btn;
	uint8_t icon;
	if (!refresh)
		return;

	btn = (PushButton*)input;
	xstyle_Style_Paint_Border(r, btn->pressed);

	if (input->id == 0)
		icon = iconLeftArrow;
	else
		icon = iconRightArrow;
	xstyle_Style_Draw_Centered_Icon(icon, r, clip_r, btn->pressed);

	if (xinpattr_Is_Input_Dirty(input))
		xdirty_Dirty_Rect(clip_r);
}

// FUNCTION: TIE95 0x71D78
// FUNCTION: TIE98 0x41DF70
static void filmview_idraw_FilmView_Page(Input* input, Rect* r, Rect* clip_r, int16_t refresh) {
	char string[32];
	(void)clip_r;
	if (!refresh)
		return;

	xstyle_Style_Paint_TextField(r);

	snprintf(string, sizeof(string), "Page %d/%d", cur_page + 1, num_pages);
	xfont_Print_Centered_Text(string, r, 1, 15);

	if (xinpattr_Is_Input_Dirty(input))
		xdirty_Dirty_Rect(clip_r);
}

/* ================================================================
 * File list draw
 * ================================================================ */

// FUNCTION: TIE95 0x71DF4
// FUNCTION: TIE98 0x41DFF0
static void filmview_idraw_FV_File(Input* input, Rect* r, Rect* clip_r, int16_t refresh) {
	Rect dr;
	if (!refresh)
		return;

	xrect_Copy_Rect(&dr, r);

	switch (input->id) {
		case 0:
			/* Title bar */
			xstyle_Style_Paint_Base(r, 0);
			xstyle_Style_Trim_Base(&dr);
			xfont_Print_Clipped_Text((const char*)input->varptr, dr.left + 35, dr.top + 1, 0, 21);
			break;

		case 1: {
			FileDialog* the_dialog = (FileDialog*)input->varptr;
			const DirEntry* entries;
			int16_t off_y, off_x, half, count;

			/* File list: two columns of eight entries. */
			xstyle_Style_Paint_TextField(r);
			xstyle_Style_Trim_TextField(&dr);
			entries = xmemhdl_Lock_Handle(the_dialog->the_head.entries);
			if (!entries)
				break;
			off_y = dr.top;
			off_x = dr.left;
			half = ((dr.right - dr.left) >> 1) - 1;

			xpaint_Vert_Clipped_Line(dr.left + half, dr.top, dr.bottom - dr.top, 2);

			for (count = the_dialog->name_offset;
				 count < the_dialog->name_offset + 16 && count < the_dialog->the_head.count; count++) {
				int16_t color, old_font, width;
				char size_str[16];
				if (count == the_dialog->active_name) {
					Rect dr2;
					xrect_Set_Rect(&dr2, off_x, off_y, half + off_x, off_y + 7);
					xpaint_Paint_Clipped_Rect(&dr2, 2);
					color = 16;
				} else {
					color = 15;
				}

				xfont_Print_Clipped_Text(entries[count].name, off_x + 4, off_y + 1, 1, color);

				snprintf(size_str, sizeof(size_str), "%uK", (unsigned)entries[count].size_kb);

				old_font = xfont_Get_Font();
				xfont_Set_Font(1);
				width = xfont_Get_String_Width(size_str) + 8;
				xfont_Set_Font(old_font);

				xfont_Print_Clipped_Text(size_str, half + off_x - width, off_y + 1, 1, color);

				/* After 8th file in left column, switch to right column */
				if (count == the_dialog->name_offset + 7) {
					off_y = dr.top;
					off_x = half + dr.left + 1;
				} else {
					off_y += 8;
				}
			}
			xmemhdl_Unlock_Handle(the_dialog->the_head.entries);
			break;
		}

		case 2:
		case 3:
		case 4:
		case 5: {
			/* Styled button with text label */
			PushButton* btn = (PushButton*)input;
			xstyle_Style_Paint_Border(r, btn->pressed);
			if (btn->name) {
				xstyle_Style_Button_Text((const char*)xmemhdl_Lock_Handle(btn->name), r, btn->pressed);
				xmemhdl_Unlock_Handle(btn->name);
			}
			break;
		}

		default:
			break;
	}

	if (xinpattr_Is_Input_Dirty(input))
		xdirty_Dirty_Rect(clip_r);
}

/* ================================================================
 * File list input callbacks
 * ================================================================ */

// FUNCTION: TIE95 0x720B0
// FUNCTION: TIE98 0x41E2B0
static int16_t filmview_iupdate_FV_File(Input* input, Rect* r, Rect* clip_r, int16_t key, uint8_t left,
										uint8_t right, int16_t x, int16_t y) {
	(void)clip_r;
	(void)left;
	(void)right;

	if (key) {
		if (input->id == 1 && key == 13) {
			xdialog_Set_Dialog_Exit(1);
			return 1;
		}
		return 0;
	}

	if (input->id == 1)
		filmview_Select_Active_FV_File(input, r, x, y);
	return 1;
}

// FUNCTION: TIE95 0x72110
// FUNCTION: TIE98 0x41E310
static void filmview_iuser_FV_File(Input* input, int32_t time) {
	(void)time;

	switch (input->id) {
		case 2: /* Load */
			if (xinpattr_Get_Input_Selected(input))
				xdialog_Set_Dialog_Exit(1);
			break;

		case 3: /* Exit */
			if (xinpattr_Get_Input_Selected(input))
				xdialog_Set_Dialog_Exit(2);
			break;

		case 4: /* Delete */
#ifdef TIE_MODERN
			TieFilmView_BeginDelete(input);
#endif
			if (xinpattr_Get_Input_Selected(input) && filmview_name[0] && filmview_Do_Delete_Dialog()) {
				FileDialog* the_dialog = (FileDialog*)input->varptr;
				DirEntry* entries = xmemhdl_Lock_Handle(the_dialog->the_head.entries);

				if (the_dialog->active_name < the_dialog->the_head.count) {
					char file_name[16];
					int16_t j;

					/* Remove the file */
					strcpy(file_name, filmview_name);
					strcat(file_name, ".clp");
					TieStorage_Remove(TIE_FILE_ROOT_USER, file_name);

					/* Shift remaining entries down */
					for (j = the_dialog->active_name; j < the_dialog->the_head.count - 1; j++)
						entries[j] = entries[j + 1];

					if (--the_dialog->the_head.count <= the_dialog->active_name && the_dialog->active_name)
						the_dialog->active_name--;

					filmview_name[0] = 0;
					the_dialog->active_hits = 0;

					if (the_dialog->the_head.count) {
						filmview_Set_Active_FV_File(the_dialog, 0, 1);
						the_dialog->active_hits = 0;
					} else {
						if (load_input)
							xinpattr_Hide_Input(load_input);
						if (delete_input)
							xinpattr_Hide_Input(delete_input);
					}
					xview_Refresh_View();
				}
				xmemhdl_Unlock_Handle(the_dialog->the_head.entries);
			}
			break;

		case 5: /* Replay last */
			if (xinpattr_Get_Input_Selected(input)) {
				xdialog_Set_Dialog_Exit(1);
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

// FUNCTION: TIE95 0x7231C
// FUNCTION: TIE98 0x41E580
static void filmview_Select_Active_FV_File(Input* input, Rect* r, int16_t x, int16_t y) {
	FileDialog* the_dialog = (FileDialog*)input->varptr;
	int16_t file = the_dialog->name_offset + (y / 8);

	/* Right column adds 8 */
	if (x > (r->right - r->left) / 2)
		file += 8;

	filmview_Set_Active_FV_File(the_dialog, file, 1);
}

// FUNCTION: TIE95 0x7236C
// FUNCTION: TIE98 0x41E5D0
static void filmview_Set_Active_FV_File(FileDialog* the_dialog, int16_t file, int16_t hit) {
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
		const DirEntry* entries = xmemhdl_Lock_Handle(the_dialog->the_head.entries);
		if (entries && file >= 0 && file < the_dialog->the_head.count) {
			if (the_dialog->active_hits > 1)
				xdialog_Set_Dialog_Exit(1);
			strcpy(filmview_name, entries[file].name);
		}
		xmemhdl_Unlock_Handle(the_dialog->the_head.entries);
	}

	if (change || scroll)
		xinpattr_Refresh_Input(the_dialog->dialog);
}

// FUNCTION: TIE95 0x724A8
static int16_t filmview_Clip_Active_FV_File(FileDialog* the_dialog) {
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

// FUNCTION: TIE95 0x724D8
// FUNCTION: TIE98 0x41E720
static int16_t filmview_Do_Delete_Dialog(void) {
	Input* the_input;
	int16_t result;

#ifdef TIE_MODERN
	if (TieFilmView_TakeDeleteResult(&result))
		return result != 2;
#endif
	the_input = filmview_Build_Delete_Dialog();
#ifdef TIE_MODERN
	TieFilmView_RunDelete(the_input);
	return 0;
#else
	result = xdialog_Handle_Dialog_View(the_input);
	xdialog_Clear_Dialog_Exit();
	xinput_Free_Inputs(the_input);
	return result != 2;
#endif
}

// FUNCTION: TIE95 0x72504
// FUNCTION: TIE98 0x41E750
static Input* filmview_Build_Delete_Dialog(void) {
	Rect r;
	Input *parent_input, *input;

	xrect_Set_Rect(&r, 0, 0, 180, 46);
	parent_input = xinput_Alloc_Dialog_Input(NULL, &r, 0, 0);
	if (!parent_input)
		return NULL;
	xinpattr_Set_Input_Update_Function(parent_input, filmview_iupdate_Delete_Input);
	xinpattr_Set_Input_Draw_Function(parent_input, filmview_idraw_Delete_Input);
	xinpattr_Set_Input_Allign(parent_input, 1, 1);
	xinpattr_Show_Input(parent_input);
	parent_input->id = 0;

	/* Delete button */
	xrect_Set_Rect(&r, 4, 4, 76, 20);
	input =
		(Input*)xbtnpush_Alloc_Button(parent_input, &r, 0, filmview_iuser_Delete_Input, film_name_str[0], 1);
	if (!input) {
		xinput_Free_Inputs(parent_input);
		return NULL;
	}
	xinpattr_Set_Input_Allign(input, 0, 2);

	/* Cancel button */
	xrect_Set_Rect(&r, 4, 4, 76, 20);
	input =
		(Input*)xbtnpush_Alloc_Button(parent_input, &r, 0, filmview_iuser_Delete_Input, film_name_str[1], 2);
	if (!input) {
		xinput_Free_Inputs(parent_input);
		return NULL;
	}
	xinpattr_Set_Input_Allign(input, 2, 2);

	return parent_input;
}

// FUNCTION: TIE95 0x725E4
// FUNCTION: TIE98 0x41E840
static int16_t filmview_iupdate_Delete_Input(Input* input, Rect* r, Rect* clip_r, int16_t key, uint8_t left,
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

// FUNCTION: TIE95 0x72620
// FUNCTION: TIE98 0x41E880
static void filmview_iuser_Delete_Input(Input* input, int32_t time) {
	int16_t id;
	(void)time;
	if (!xinpattr_Get_Input_Selected(input))
		return;

	id = input->id;
	if (id >= 1 && id <= 2)
		xdialog_Set_Dialog_Exit(id);
}

// FUNCTION: TIE95 0x72648
// FUNCTION: TIE98 0x41E8B0
static void filmview_idraw_Delete_Input(Input* input, Rect* r, Rect* clip_r, int16_t refresh) {
	char string[32];
	if (!refresh)
		return;

	strcpy(string, film_name_str[0]); /* "Delete" */
	strcat(string, " ");
	strcat(string, filmview_name);
	strcat(string, "?");

	if (!input->id) {
		Rect dr;
		xrect_Copy_Rect(&dr, r);
		xpaint_Frame_Clipped_Rect(&dr, 16);
		xrect_Inset_Rect(&dr, 1, 1);
		xstyle_Style_Paint_Border(&dr, 0);
		dr.bottom = dr.top + 14;
		xfont_Enable_FontID_Shadow(0);
		xfont_Print_Centered_Text(string, &dr, 0, 15);
		xfont_Disable_FontID_Shadow(0);
	}

	if (xinpattr_Is_Input_Dirty(input))
		xdirty_Dirty_Rect(clip_r);
}

/* ================================================================
 * Entry point
 * ================================================================ */

// FUNCTION: TIE95 0x71690
// FUNCTION: TIE98 0x41D810
int16_t filmview_FilmView(SceneHeadStruct* scene_head) {
	ResFile* resource;
	Rect r;
	int16_t i;

	cur_page = 0;
	num_pages = 1;
	if (scene_head->last_scene == SCENE_MAIN_MENU)
		replayclipname[0] = 0;
	resource = shellext_Open_Empire_Resource("filmview.lfd");
#ifdef TIE_MODERN
	if (!resource) {
		TieFilmView_RunView(resource, false);
		return 0;
	}
#endif
	xrect_Set_Rect(&r, 0, 0, 320, 200);
	for (i = 0; i < 5; i++) {
		const char* text = textext_Get_Text((TIEText)(txtFilmDelete + i));
		strcpy(film_name_str[i], text);
	}
	filmview_film = xfilm_Res_Film("filmview", &r, 0, 0, 0);
#ifdef TIE_MODERN
	if (!filmview_film) {
		TieFilmView_RunView(resource, false);
		return 0;
	}
#endif
	xfilm_Set_Film_Def_Palette(filmview_film, scene_head->def_palette);
	xview_Set_View_Update_Function(filmview_end_View);
#ifdef TIE_MODERN
	TieFilmView_RunView(resource, true);
	return 0;
#else
	shellext_Handle_TIE_View();
	xview_Clear_View_Update_Function();
	xres_Close_Resource(resource);
	return xerror_Get_Landru_Exit();
#endif
}
