#ifndef TIE_COMPUTER_TASK_H
#define TIE_COMPUTER_TASK_H

#include <landru/input.h>
#include <landru/rect.h>
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

/* PORT: TIE98 (640x480) COMPUTER layout for the runtime-selected frontend.
 * Copies of the TIE98 computer_str, computer_medal_str*, computer_mode_rect,
 * pref_rect (first 18 entries) and backup_rect data; the TIE95 data is the
 * recovered data in tie/computer.c. */
extern const char TieComputer_SvgaStr[13][14];
extern const char TieComputer_SvgaMedalStr[19][14];
extern const char TieComputer_SvgaMedalStr2[7][14];
extern const char TieComputer_SvgaMedalStr3[7][14];
extern const Rect TieComputer_SvgaModeRect[4];
extern const Rect TieComputer_SvgaPrefRect[18];
extern const Rect TieComputer_SvgaBackupRect[8];

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
