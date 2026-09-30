#include "tie_runtime/timing/flight_cadence.h"

#include "tie/anim.h"
#include "tie/gate.h"
#include "tie/pai.h"
#include "tie/tie.h"
#include "tie_runtime/timing/ai_lead.h"

static uint16_t s_ai_timer_elapsed_ticks;

void TieFlightCadence_SetAiTimerTicks(TieFlightCadence ai_cadence) {
	s_ai_timer_elapsed_ticks = ai_cadence.due ? ai_cadence.elapsed_ticks : 0;
}

uint16_t TieFlightCadence_AiTimerTicks(void) { return s_ai_timer_elapsed_ticks; }

void TieFlightCadence_RunPlaneAi(TieFlightCadence cadence) {
	uint16_t real_frameticks;
	uint16_t real_framerate;

	if (!cadence.due || mission.train_craft_type != 0)
		return;
	if (!TieFlightTiming_IsHighRate()) {
		pai_updateplaneai();
		return;
	}

	real_frameticks = frameticks;
	real_framerate = framerate;
	frameticks = cadence.elapsed_ticks;
	framerate = (uint16_t)(236u / cadence.elapsed_ticks);
	if (!framerate)
		framerate = 1;
	pai_updateplaneai();
	TieAiLead_CommitBoundary();
	frameticks = real_frameticks;
	framerate = real_framerate;
}

void TieFlightCadence_RunAnimation(TieFlightCadence cadence) {
	uint16_t real_frameticks;
	uint16_t real_framerate;

	if (!cadence.due) {
		/* Player movement still advances on unlocked ticks. Keep the
		 * gate-plane check at that cadence while mesh animation remains on
		 * the recovered compatibility cadence. */
		if (TieFlightTiming_IsHighRate() && mission.train_craft_type)
			gate_updatecourseprogress();
		return;
	}
	if (!TieFlightTiming_IsHighRate()) {
		anim_updateanimation();
		return;
	}

	real_frameticks = frameticks;
	real_framerate = framerate;
	frameticks = cadence.elapsed_ticks;
	framerate = (uint16_t)(236u / cadence.elapsed_ticks);
	if (!framerate)
		framerate = 1;
	anim_updateanimation();
	frameticks = real_frameticks;
	framerate = real_framerate;
}
