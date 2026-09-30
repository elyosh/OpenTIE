#ifndef TIE_COMPUTER_TASK_H
#define TIE_COMPUTER_TASK_H

#include <landru/input.h>
#include <landru/res.h>
#include <landru/surface.h>
#include <stdbool.h>
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

#endif
