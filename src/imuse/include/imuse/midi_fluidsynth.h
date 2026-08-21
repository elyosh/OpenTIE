#ifndef LIBIMUSE_PUBLIC_MIDI_FLUIDSYNTH_H
#define LIBIMUSE_PUBLIC_MIDI_FLUIDSYNTH_H

/* FluidSynth General MIDI backend with SC-55-oriented event filtering. Its
 * sample rate comes from ImuseConfig; this header configures the SoundFont. */

#include <stddef.h>

#include <imuse/midi_backend.h>

/* SoundFont source: exactly one of {soundfontPath, soundfontData}
 * must be set per config.
 *
 * - soundfontPath: classic file-system load. The factory copies the
 *   path string internally, so the host can free its own copy
 *   immediately after the create call.
 *
 * - soundfontData / soundfontSize: in-memory .sf2 bytes loaded
 *   through FluidSynth's SFLoader callback API. The buffer is
 *   borrowed, not copied — SoundFonts can run to 100+ MB and a copy
 *   would double the RAM footprint. The host MUST keep the buffer
 *   alive until imuse_destroy returns.
 */
typedef struct ImuseFluidSynthConfig {
	const char* soundfontPath; /* file mode: path to .sf2 */
	const void* soundfontData; /* buffer mode: pointer to .sf2 bytes */
	size_t soundfontSize;      /* required when soundfontData != NULL */
	int polyphony;             /* 0 = default 256 */
} ImuseFluidSynthConfig;

/* Nonzero when this libimuse build is linked with FluidSynth support. */
int imuse_fluidsynth_backend_available(void);

/* Nonzero when FluidSynth recognizes the file as a SoundFont. */
int imuse_fluidsynth_soundfont_is_valid(const char* path);

/* Allocate a FluidSynth backend handle. The factory:
 *   - validates that exactly one SoundFont source is configured;
 *   - copies the path string when in file mode (host may free its
 *     copy immediately);
 *   - records the buffer pointer + size when in buffer mode (host
 *     MUST keep it alive until imuse_destroy returns).
 *
 * Returns NULL on validation or allocation failure. The actual
 * FluidSynth synth is brought up at imuse_create time via the
 * backend's open hook; the factory only allocates the descriptor.
 *
 * Ownership: the returned handle is transferred to imuse_create
 * as its `backend` argument — see <imuse/midi_backend.h>. */
ImuseMidiBackend* imuse_fluidsynth_backend_create(const ImuseFluidSynthConfig* cfg);

#endif /* LIBIMUSE_PUBLIC_MIDI_FLUIDSYNTH_H */
