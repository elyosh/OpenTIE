#ifndef TIE_RUNTIME_TIMING_AI_LEAD_H
#define TIE_RUNTIME_TIMING_AI_LEAD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void TieAiLead_Reset(void);
void TieAiLead_Advance(uint16_t elapsed_ticks);
void TieAiLead_CommitBoundary(void);
bool TieAiLead_GetDisplacement(uint16_t target_obj_idx, int32_t* dx, int32_t* dy, int32_t* dz);

size_t TieAiLead_CheckpointSize(void);
void TieAiLead_SaveCheckpoint(void* destination);
bool TieAiLead_RestoreCheckpoint(const void* source, size_t size);

#endif /* TIE_RUNTIME_TIMING_AI_LEAD_H */
