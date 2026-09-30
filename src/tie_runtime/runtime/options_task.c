#include "tie_runtime/runtime/options_task.h"
#include "tie/flight_surface_tie98.h"
#include "tie/frontend_display_tie98.h"
#include "tie/option.h"
#include "tie/user.h"
#include "tie_runtime/diagnostics/diagnostics.h"
#include "tie_runtime/display/classic_display.h"
#include "tie_runtime/runtime/inflight_state.h"

#include <landru/task.h>

typedef struct OptionsTask {
	OptionRoomState room;
	bool render;
} OptionsTask;

// ORIGINAL_FUNCTION: TIE95 0x34C40
static LandruTaskStepResult options_step(void* self) {
	OptionsTask* task = self;
	if (task->render) {
		const bool dx5 = TieClassicDisplay_UsesDx5();
		if (dx5)
			FlightSurface_Lock();
		option_RenderRows(&task->room);
		if (dx5) {
			FlightSurface_Unlock();
			FrontendDisplay_BlitOffscreenToRenderSurface();
			FrontendDisplay_PresentFrame();
		}
		task->render = false;
		return LANDRU_TASK_STEP_CONTINUE;
	}
	int result = option_PollOnce(&task->room);
	if (result == 1) {
		char error[128];
		TieInflightOptions_StoreLegacy(task->room.values);
		if (!TieInflightOptions_Flush(error, sizeof error))
			TieDiagnostics_Log(TIE_LOG_ERROR, "%s\n", error);
		option_ApplyValues(task->room.values);
		TieInflightOptions_ApplyAudio();
		user_submodal_result = task->room.exit_code;
		return LANDRU_TASK_STEP_DONE;
	}
	if (result == 2)
		task->render = true;
	return LANDRU_TASK_STEP_CONTINUE;
}

static const LandruTaskVtable options_vtable = { options_step, NULL, NULL, NULL };

void TieOptions_Begin(void) {
	OptionRoomState room;
	const bool dx5 = TieClassicDisplay_UsesDx5();
	if (dx5)
		FlightSurface_Lock();
	option_OpenRoom(&room);
	if (dx5)
		FlightSurface_Unlock();
	OptionsTask* task = landru_task_push(&options_vtable);
	if (!task)
		return;
	task->room = room;
	task->render = true;
}
