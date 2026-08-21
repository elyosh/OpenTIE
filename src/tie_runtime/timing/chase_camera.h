#ifndef TIE_RUNTIME_TIMING_CHASE_CAMERA_H
#define TIE_RUNTIME_TIMING_CHASE_CAMERA_H

#include <stdbool.h>
#include <stddef.h>

void TieChaseCamera_Reset(void);
void TieChaseCamera_Update(void);

size_t TieChaseCamera_CheckpointSize(void);
void TieChaseCamera_SaveCheckpoint(void* destination);
bool TieChaseCamera_RestoreCheckpoint(const void* source, size_t size);

#endif /* TIE_RUNTIME_TIMING_CHASE_CAMERA_H */
