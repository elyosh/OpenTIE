#ifndef TIE_RUNTIME_TIMING_REPLAY_RECORDING_H
#define TIE_RUNTIME_TIMING_REPLAY_RECORDING_H

#include <stdbool.h>
#include <stdint.h>

void TieReplayRecording_ResetDuration(void);
void TieReplayRecording_StoreRecord(bool advances_time);
bool TieReplayRecording_KeyStartsInfoPayload(uint16_t key);
bool TieReplayRecording_PrepareSlots(uint16_t required_slots, bool timed_frame_starts_payload);

#endif
