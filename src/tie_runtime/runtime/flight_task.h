#ifndef TIE_RUNTIME_RUNTIME_FLIGHT_TASK_H
#define TIE_RUNTIME_RUNTIME_FLIGHT_TASK_H

#include "tie_runtime/runtime/flight_screen.h"
#include <landru/task.h>
#include <stdbool.h>
#include <stdint.h>
enum { TIE_SIM_LOADSCREEN_MIN_US = 2000000 };
typedef enum TieSimPhase {
	TIE_SIM_PHASE_INIT,
	TIE_SIM_PHASE_LOADSCREEN_HOLD,
	TIE_SIM_PHASE_AFTER_HYPER,
	TIE_SIM_PHASE_AFTER_REPLAY_VIEWER,
	TIE_SIM_PHASE_AFTER_MISSION,
	TIE_SIM_PHASE_PROMPT_RENDER,
	TIE_SIM_PHASE_PROMPT_POLL,
	TIE_SIM_PHASE_PROMPT_AFTER_VIEWER,
	TIE_SIM_PHASE_AFTER_REPLAY_PROMPT,
	TIE_SIM_PHASE_AFTER_INFOROOM,
	TIE_SIM_PHASE_TEARDOWN,
} TieSimPhase;
typedef struct TieSimulatorTask {
	int replay_mode;
	TieSimPhase phase;
	uint8_t saved_drawbackdrop;
	uint8_t saved_drawdebris;
	uint16_t saved_master_vol;
	uint64_t loadscreen_shown_us;
	bool post_mission_ui;
	TieFlightScreen previous_screen;
	LandruTaskStepResult next_step;
} TieSimulatorTask;
void TieFlightRuntime_ResetTiming(void);
bool TieFlightRuntime_PrepareSimulator(int replay_mode);
void TieFlightRuntime_BeginHyperspace(void);

/* Begin a live mission (zero) or replay-only simulator session (nonzero). */
void TieFlightTask_Begin(int replay_mode);
void TieFlightTask_BeginMission(void);
void TieFlightRuntime_ReleaseRecoveredResources(void);

#endif
