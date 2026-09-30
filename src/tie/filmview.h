#ifndef TIE_FILMVIEW_H
#define TIE_FILMVIEW_H

#include "landru/filedir.h"
#include "landru/input.h"
#include "tie/shellext.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

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

int16_t filmview_FilmView(SceneHeadStruct* scene_head);

#ifdef __cplusplus
}
#endif

#endif
