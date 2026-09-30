#include "tie_runtime/runtime/computer_task.h"
#include "tie/computer.h"
#include "tie_runtime/runtime/profile.h"
#include "tie_runtime/runtime/runtime.h"
#include <landru/btnpush.h>
#include <landru/dialog.h>
#include <landru/inpattr.h>
#include <landru/task.h>

typedef struct ComputerConfirmState {
	Input* owner;
	Input* dialog;
	int16_t result;
	bool resumed;
} ComputerConfirmState;

const char TieComputer_SvgaStr[13][14] = {
	"computer.lfd", "arm",    "sleeve", "newtscrn", "compface", "medlbak", "buttons",
	"compicon",     "tattoo", "arm",    "newtscrn", "computr",  "medlbak",
};

const char TieComputer_SvgaMedalStr[19][14] = {
	"awardshr.lfd", "trnships", "medls-a",  "strsnbrs", "sunrisea", "aniplnts", "star-1",
	"star-2",       "coins",    "starsbak", "mdl-bak1", "mdl-bak2", "ani1-sta", "trnships",
	"raptrhed",     "trnshps",  "medlbak",  "brnzpal",  "slvrpal",
};

const char TieComputer_SvgaMedalStr2[7][14] = {
	"awards1h.lfd", "mislboat", "a-medals", "amed-obj", "medpal", "coins", "tattoo",
};

const char TieComputer_SvgaMedalStr3[7][14] = {
	"awards2h.lfd", "mislboat", "b-medals", "bmed-obj", "medpal", "coins", "tattoo",
};

const Rect TieComputer_SvgaModeRect[4] = {
	{ 350, 318, 453, 380 },
	{ 350, 388, 453, 445 },
	{ 350, 451, 453, 511 },
	{ 350, 517, 453, 577 },
};

const Rect TieComputer_SvgaPrefRect[18] = {
	{ 26, 242, 60, 478 },   { 67, 190, 93, 280 },   { 67, 286, 93, 386 },   { 67, 392, 93, 530 },
	{ 98, 190, 124, 280 },  { 98, 286, 124, 386 },  { 98, 392, 124, 530 },  { 129, 190, 155, 280 },
	{ 129, 286, 155, 386 }, { 129, 392, 155, 530 }, { 160, 190, 186, 264 }, { 160, 270, 186, 354 },
	{ 160, 360, 186, 434 }, { 160, 440, 186, 530 }, { 191, 190, 217, 344 }, { 191, 350, 217, 530 },
	{ 222, 190, 248, 434 }, { 253, 190, 279, 530 },
};

const Rect TieComputer_SvgaBackupRect[8] = {
	{ 26, 242, 64, 478 },   { 67, 190, 96, 424 },   { 67, 430, 96, 530 },   { 105, 190, 134, 424 },
	{ 105, 430, 134, 530 }, { 144, 190, 177, 350 }, { 144, 370, 177, 530 }, { 187, 190, 285, 530 },
};

static ComputerConfirmState confirm_state;
static Input* options_button;

static LandruTaskStepResult computer_step(void* self) {
	ComputerDialogState* task = self;
	computer_Do_Computer_Dialog();
	return task->finished ? LANDRU_TASK_STEP_DONE : LANDRU_TASK_STEP_CONTINUE;
}
static const LandruTaskVtable computer_vtable = { .step = computer_step };
void TieComputer_RunView(Input* dialog) {
	ComputerDialogState* task = landru_task_top();
	task->the_dialog = dialog;
	task->started = true;
	xdialog_Push_Dialog_View_Task(dialog);
}
void TieComputer_Fail(ResFile* open_resource, const char* missing_resource) {
	ComputerDialogState* task = landru_task_top();
	if (open_resource)
		xres_Close_Resource(open_resource);
	task->missing_resource = missing_resource;
	task->failed = true;
}
void TieComputer_Begin(void) {
	ComputerDialogState* task = landru_task_push(&computer_vtable);
	if (task)
		task->tie98 = TieProfile_UsesTie98Frontend();
}

static void after_confirm_dialog(int16_t result, void* unused) {
	Input* owner = confirm_state.owner;
	(void)unused;
	xinput_Free_Inputs(confirm_state.dialog);
	confirm_state.dialog = NULL;
	xdialog_Clear_Dialog_Exit();
	if (!owner || !owner->user) {
		confirm_state.owner = NULL;
		return;
	}
	/* Re-enter the requesting callback; its Check function now returns
	 * the confirmation result instead of opening the dialog again. */
	confirm_state.result = result;
	confirm_state.resumed = true;
	xinpattr_Selected_Input(owner);
	owner->user(owner, 0);
	confirm_state.resumed = false;
	confirm_state.owner = NULL;
}

void TieComputer_BeginConfirm(Input* owner) {
	if (!confirm_state.resumed)
		confirm_state.owner = owner;
}

void TieComputer_RunConfirm(Input* confirm_dialog) {
	confirm_state.dialog = confirm_dialog;
	xdialog_Schedule_Sub_Dialog(confirm_dialog, after_confirm_dialog, NULL);
}

bool TieComputer_TakeConfirmResult(int16_t* result) {
	if (!confirm_state.resumed)
		return false;
	*result = confirm_state.result;
	confirm_state.resumed = false;
	return true;
}

static void iuser_options_button(Input* input, int32_t time) {
	(void)time;
	if (xinpattr_Get_Input_Selected(input))
		TieRuntime_RequestSettingsMenu();
}

void TieComputer_AllocOptionsButton(Input* parent, Rect* frame, InputDrawFunc draw) {
	options_button = (Input*)xbtnpush_Alloc_Button(parent, frame, 0, iuser_options_button, NULL, 5);
	if (options_button)
		xinpattr_Set_Input_Draw_Function(options_button, draw);
}

void TieComputer_ShowOptionsButton(bool show) {
	if (!options_button)
		return;
	if (show)
		xinpattr_Show_Input(options_button);
	else
		xinpattr_Hide_Input(options_button);
}
