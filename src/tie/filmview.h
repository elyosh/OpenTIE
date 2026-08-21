#ifndef __FILMVIEW_H__
#define __FILMVIEW_H__

#include "landru/filedir.h"
#include "landru/input.h"
#include "tie/shellext.h"
#include <stdint.h>

/* FileDialog — file selection dialog state, wraps a Directory with
 * dialog-specific UI fields. From watdbg FileDialogStruct. */
typedef struct {
	Directory the_head;  /* base directory info */
	Input* dialog;       /* root dialog input */
	Input* scroll;       /* scroll area child */
	Input* string;       /* string display child */
	int16_t name_offset; /* first visible file index */
	int16_t active_name; /* currently selected file index */
	int16_t active_hits; /* click count on active file */
	int16_t read;        /* needs-read flag */
} FileDialog;

/* Push the Film Room scene as a tie_core task. */
void filmview_Push_FilmView_Task(SceneHeadStruct* scene_head);

#endif
