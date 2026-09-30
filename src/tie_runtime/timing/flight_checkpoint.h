#ifndef TIE_FLIGHT_CHECKPOINT_H
#define TIE_FLIGHT_CHECKPOINT_H

#include <stddef.h>

#include "tie_runtime/storage/storage.h"

size_t TieFlightCheckpoint_Size(void);
int TieFlightCheckpoint_Write(TieFile* file);
int TieFlightCheckpoint_Read(TieFile* file);
/* Recorder checkpoints hold absolute addresses into fixed-image globals
 * (object craft pointers, player aliases). Rebuild them after a
 * replayio_copyfromsave restore. */
void TieFlightCheckpoint_RebindPointers(void);

#endif
