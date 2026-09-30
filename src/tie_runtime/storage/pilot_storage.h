#ifndef TIE_RUNTIME_PILOT_STORAGE_H
#define TIE_RUNTIME_PILOT_STORAGE_H

#include "tie/shipext.h"

void PilotRecord_decode(PilotRecord* dst, const uint8_t* src);
void PilotRecord_encode(uint8_t* dst, const PilotRecord* src);

/* Shared storage policy: pilots remain selectable after switching frontends. */
#define TIE_PILOT_NAME_MAX 16
#define TIE_PILOT_NAME_CAPACITY (TIE_PILOT_NAME_MAX + 1)
#define TIE_PILOT_FILENAME_CAPACITY (TIE_PILOT_NAME_CAPACITY + sizeof(".tfr") - 1)

#endif
