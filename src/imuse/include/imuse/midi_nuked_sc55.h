#ifndef IMUSE_MIDI_NUKED_SC55_H
#define IMUSE_MIDI_NUKED_SC55_H

#include <stddef.h>

#include <imuse/midi_backend.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ImuseNukedSc55Romset ImuseNukedSc55Romset;

int imuse_nuked_sc55_backend_available(void);
ImuseNukedSc55Romset* imuse_nuked_sc55_romset_load(const char* directory, char* error, size_t error_capacity);
const char* imuse_nuked_sc55_romset_name(const ImuseNukedSc55Romset* romset);
void imuse_nuked_sc55_romset_release(ImuseNukedSc55Romset* romset);
ImuseMidiBackend* imuse_nuked_sc55_backend_create(const ImuseNukedSc55Romset* romset);

#ifdef __cplusplus
}
#endif

#endif
