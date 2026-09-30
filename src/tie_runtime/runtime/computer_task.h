#ifndef TIE_COMPUTER_TASK_H
#define TIE_COMPUTER_TASK_H

#include <landru/input.h>
#include <landru/res.h>
#include <landru/surface.h>
#include <stdbool.h>
#include <stdint.h>
typedef struct ComputerDialogState {
	Input* the_dialog;
	bool tie98;
	LandruSurfaceSet saved_surface_set;
	Rect saved_view_frame;
	Rect saved_view_clip;
	bool started;
	bool failed;
	bool finished;
	const char* missing_resource;
} ComputerDialogState;
void TieComputer_RunView(Input* dialog);
void TieComputer_Fail(ResFile* open_resource, const char* missing_resource);

void TieComputer_Begin(void);

/* Exit/backup/restore confirmations: BeginConfirm records the input whose
 * user callback asks for confirmation, RunConfirm schedules the confirmation
 * dialog, and the callback is re-entered with the result afterwards. */
void TieComputer_BeginConfirm(Input* owner);
void TieComputer_RunConfirm(Input* confirm_dialog);
bool TieComputer_TakeConfirmResult(int16_t* result);

/* Modern "OpenTIE Options" button on the COMPUTER options page. */
void TieComputer_AllocOptionsButton(Input* parent, Rect* frame, InputDrawFunc draw);
void TieComputer_ShowOptionsButton(bool show);

#endif
