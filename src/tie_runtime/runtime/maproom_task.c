#include "tie_runtime/runtime/maproom_task.h"
#include "tie/flight_surface_tie98.h"
#include "tie/frontend_display_tie98.h"
#include "tie/maproom.h"
#include "tie/tie.h"
#include "tie/user.h"
#include "tie/xtimer.h"
#include "tie_runtime/display/classic_display.h"
#include <landru/task.h>

typedef struct MaproomTask {
	MaproomState room;
	bool render;
} MaproomTask;

// ORIGINAL_FUNCTION: TIE95 0x2F1BC
// ORIGINAL_FUNCTION: TIE98 0x451470
// Loop portion; the room's setup and drawing are separate game operations.
static LandruTaskStepResult maproom_step(void* self) {
	MaproomTask* task = self;
	if (task->render) {
		tickcounter += (uint16_t)xtimer_time_elapsed();
		if (tickcounter < MAP_FRAME_TICKS)
			return LANDRU_TASK_STEP_YIELD;
		frameticks = tickcounter;
		const bool tie98 = TieClassicDisplay_UsesDx5();
		if (tie98)
			FlightSurface_Lock();
		maproom_DrawRoom(&task->room);
		if (tie98) {
			FlightSurface_Unlock();
			FrontendDisplay_BlitOffscreenToRenderSurface();
			FrontendDisplay_PresentFrame();
		}
		task->render = false;
		return LANDRU_TASK_STEP_CONTINUE;
	}
	int result = maproom_PollRoom(&task->room);
	if (result == 1) {
		maproom_CloseRoom();
		user_submodal_result = task->room.page_delta;
		return LANDRU_TASK_STEP_DONE;
	}
	if (result == 2)
		task->render = true;
	return LANDRU_TASK_STEP_CONTINUE;
}
static const LandruTaskVtable maproom_vtable = { .step = maproom_step };
void TieMaproom_Begin(void) {
	MaproomState room;
	maproom_OpenRoom(&room);
	MaproomTask* task = landru_task_push(&maproom_vtable);
	if (!task)
		return;
	task->room = room;
	task->render = true;
}
