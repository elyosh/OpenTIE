#ifndef TIE_COMPUTER_H
#define TIE_COMPUTER_H

#include <landru/input.h>
#include <landru/surface.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Computer mode tabs */
typedef enum {
	COMP_MODE_MEDALS = 0,
	COMP_MODE_RECORD = 1,
	COMP_MODE_BACKUP = 2,
	COMP_MODE_OPTIONS = 3,
} ComputerMode;

typedef struct ComputerDialogState {
	Input* the_dialog;
	bool tie98;
	LandruSurfaceSet saved_surface_set;
	Rect saved_view_frame;
	Rect saved_view_clip;
} ComputerDialogState;

void computer_PrepareDialog(ComputerDialogState* state);
bool computer_OpenDialog(ComputerDialogState* state);
void computer_CloseDialog(ComputerDialogState* state);

#ifdef __cplusplus
}
#endif

#endif
