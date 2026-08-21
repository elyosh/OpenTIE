#ifndef TIE_MIDI_BACKEND_H
#define TIE_MIDI_BACKEND_H

#include "tie_runtime/audio/config.h"
#include "tie_runtime/input/input.h"
#include "tie_runtime/storage/storage.h"

struct ImuseMidiBackend;

struct ImuseMidiBackend* TieMidiBackend_Create(const TieMidiBackendConfig* config);

#endif
