#ifndef TIE_FLIGHT_CHECKPOINT_H
#define TIE_FLIGHT_CHECKPOINT_H

#include <stddef.h>

#include "tie_runtime/storage/storage.h"

size_t TieFlightCheckpoint_Size(void);
int TieFlightCheckpoint_Write(TieFile* file);
int TieFlightCheckpoint_Read(TieFile* file);

#endif
