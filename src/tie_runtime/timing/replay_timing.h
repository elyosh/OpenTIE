#ifndef TIE_RUNTIME_TIMING_REPLAY_TIMING_H
#define TIE_RUNTIME_TIMING_REPLAY_TIMING_H

#include <stdbool.h>
#include <stdint.h>

#include "tie/replay.h"

bool TieReplayTiming_CurrentRecordAvailable(void);
bool TieReplayTiming_DecodeCurrentInputFrame(ReplayInputFrame* destination);
uint32_t TieReplayTiming_InputFrameDeltaUs(const ReplayInputFrame* frame);

void TieReplayTiming_Reset(void);
int32_t TieReplayTiming_SelectEngineDeltaUs(int32_t host_delta_us);
bool TieReplayTiming_IsFrameDue(void);
void TieReplayTiming_ConsumeFrame(void);
uint64_t TieReplayTiming_NextWakeDelayUs(void);

#endif /* TIE_RUNTIME_TIMING_REPLAY_TIMING_H */
