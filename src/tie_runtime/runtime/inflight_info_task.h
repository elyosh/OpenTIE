#ifndef TIE_RUNTIME_INFLIGHT_INFO_TASK_H
#define TIE_RUNTIME_INFLIGHT_INFO_TASK_H

#include "tie_runtime/runtime/flight_screen.h"
#include <stdbool.h>
#include <stdint.h>
typedef enum InflightPhase {
	INFLIGHT_PHASE_BEGIN,
	INFLIGHT_PHASE_DISPATCH,
	INFLIGHT_PHASE_AFTER_SUB,
	INFLIGHT_PHASE_FINISH,
} InflightPhase;
typedef struct InflightInfoTask {
	int32_t screen_id;
	uint16_t saved_master_vol;
	uint16_t old_target;
	int16_t retreat_flag;
	int16_t exit_flag;
	int32_t screen;
	TieFlightScreen previous_screen;
	InflightPhase phase;
	bool finished;
} InflightInfoTask;
int32_t TieInflightInfo_ReadReplay(void);
void TieInflightInfo_RecordRoom(int32_t screen);

#include <stdint.h>

void TieInflightInfo_Begin(int32_t screen);

#endif
