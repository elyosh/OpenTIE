#ifndef TIE_RUNTIME_FILMVIEW_TASK_H
#define TIE_RUNTIME_FILMVIEW_TASK_H

#include "tie/filmview.h"
#include "tie/shellext.h"
#include <landru/input.h>
#include <stdbool.h>
#include <stdint.h>

void TieFilmView_Begin(SceneHeadStruct* scene_head);

/* Film Room file and delete dialogs. In the modern build these cannot block
 * inside the view update or input callback: Run* schedules the dialog as a
 * sub-dialog, and the calling callback is re-entered afterwards, where
 * Take*Result returns the stored dialog result. */
FileDialog* TieFilmView_OpenFileDialog(void);
void TieFilmView_RunFileDialog(Input* root, int16_t key_buttons);
void TieFilmView_CloseFileDialog(int16_t key_buttons);
bool TieFilmView_FileDialogPending(void);
bool TieFilmView_TakeFileResult(int16_t* result);
void TieFilmView_BeginDelete(Input* owner);
void TieFilmView_RunDelete(Input* dialog);
bool TieFilmView_TakeDeleteResult(int16_t* result);

void TieFilmView_RunView(ResFile* resource, bool ready);

#endif
