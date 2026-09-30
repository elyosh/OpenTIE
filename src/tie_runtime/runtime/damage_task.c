#include "tie_runtime/runtime/damage_task.h"
#include "tie/damage.h"
#include "tie/flight_surface_tie98.h"
#include "tie/frontend_display_tie98.h"
#include "tie/user.h"
#include "tie_runtime/display/classic_display.h"
#include <landru/task.h>

typedef enum {
	DAMAGE_PHASE_RENDER = 0,
	DAMAGE_PHASE_POLL,
} DamagePhase;

typedef struct DamageTask {
	DamageRoomState room;
	DamagePhase phase;
} DamageTask;

// ORIGINAL_FUNCTION: TIE95 0x1A600
// ORIGINAL_FUNCTION: TIE98 0x414CC0
// (task-split recovery)
static LandruTaskStepResult damage_task_step(void* self) {
	DamageTask* t = (DamageTask*)self;

	if (t->phase == DAMAGE_PHASE_RENDER) {
		const bool tie98_display = TieClassicDisplay_UsesDx5();
		if (tie98_display)
			FlightSurface_Lock();
		damage_render_page(&t->room.sel_sys);
		if (tie98_display) {
			FlightSurface_Unlock();
			FrontendDisplay_BlitOffscreenToRenderSurface();
			FrontendDisplay_PresentFrame();
		}
		t->phase = DAMAGE_PHASE_POLL;
		return LANDRU_TASK_STEP_CONTINUE;
	}

	int r = damage_poll_once(&t->room);
	if (r == 1) {
		user_submodal_result = (int32_t)t->room.ret_dir;
		return LANDRU_TASK_STEP_DONE;
	}
	if (r == 2)
		t->phase = DAMAGE_PHASE_RENDER;
	return LANDRU_TASK_STEP_CONTINUE;
}

static const LandruTaskVtable damage_task_vt = {
	.step = damage_task_step,
};

void TieDamage_Begin(void) {
	DamageRoomState room;
	DamageTask* t;

	damage_OpenRoom(&room);
	t = (DamageTask*)landru_task_push(&damage_task_vt);
	if (!t)
		return;
	t->room = room;
	t->phase = DAMAGE_PHASE_RENDER;
}
