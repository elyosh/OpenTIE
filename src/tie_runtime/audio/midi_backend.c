#include "tie_runtime/audio/midi_backend.h"

#include <imuse/midi_fluidsynth.h>
#include <imuse/midi_fm4_opl3.h>
#include <imuse/midi_nuked_sc55.h>

bool TieMidiBackend_Available(TieMidiBackendKind kind) {
	switch (kind) {
		case TIE_MIDI_BACKEND_NONE:
			return true;
		case TIE_MIDI_BACKEND_FLUIDSYNTH:
			return imuse_fluidsynth_backend_available() != 0;
		case TIE_MIDI_BACKEND_FM4_OPL3:
			return imuse_fm4_opl3_backend_available() != 0;
		case TIE_MIDI_BACKEND_SC55:
			return imuse_nuked_sc55_backend_available() != 0;
	}
	return false;
}

struct ImuseMidiBackend* TieMidiBackend_Create(const TieMidiBackendConfig* config) {
	if (!config)
		return NULL;

	switch (config->kind) {
		case TIE_MIDI_BACKEND_NONE:
			return NULL;
		case TIE_MIDI_BACKEND_FLUIDSYNTH: {
			ImuseFluidSynthConfig fluidsynth = {
				.soundfontPath = config->soundfont_path,
				.polyphony = 0,
			};
			return imuse_fluidsynth_backend_create(&fluidsynth);
		}
		case TIE_MIDI_BACKEND_FM4_OPL3:
			return imuse_fm4_opl3_backend_create();
		case TIE_MIDI_BACKEND_SC55:
			return imuse_nuked_sc55_backend_create(config->sc55_romset);
	}
	return NULL;
}
