#ifndef LIBIMUSE_INTERNAL_MIDI_BACKEND_H
#define LIBIMUSE_INTERNAL_MIDI_BACKEND_H

/*
 * libimuse — MIDI backend vtable + engine-side dispatch.
 *
 * The MIDI synth is a per-session pluggable component. Built-in backends
 * currently cover FluidSynth, Nuked SC-55, and the TIE FM4 OPL3 driver. The
 * engine's slots / mix code only references the typed dispatch helpers below;
 * backends register a
 * vtable matching this struct and the dispatcher fans calls through
 * im->midi.backend.
 *
 * Adding a new backend means writing one .c file with a vtable
 * instance + factory; no changes to slots.c, mix.c, or this header.
 *
 * Threading: open / close / per-event ops run on the game thread;
 * render_float runs on the audio thread. Backend implementations
 * are responsible for whatever synchronization that requires.
 */

#include <imuse/commands.h> /* ImuseLogFunc */
#include <imuse/handle.h>
#include <imuse/midi_backend.h>

struct ImuseMidiBackend {
	/* ===== Lifecycle ===== */

	/* Bring up the synth with the supplied output sample rate and
	 * host log hooks. Called by ImCommands_Init before the first
	 * event is emitted. The backend should remember logFunc/logUser
	 * if it needs to log after open returns (typically only error
	 * paths and trace events). Returns 0 on success, negative on
	 * failure (which aborts imuse_create). */
	int (*open)(ImuseMidiBackend* self, int sampleRate, ImuseLogFunc logFunc, void* logUser);

	/* Tear down the synth. Called by ImCommands_Terminate. After
	 * close returns, no event ops will be invoked until release.
	 * Returns 0 on success. */
	int (*close)(ImuseMidiBackend* self);

	/* Free the backend struct + any state. Called by
	 * imuse_midi_backend_release (host-driven). The handle must
	 * not be used after this returns. */
	void (*release)(ImuseMidiBackend* self);

	/* ===== Per-event ops (game thread) =====
	 *
	 * Synthesized backends enqueue semantic events here and apply them
	 * from render_float. Calls before open() / after close() are ignored. */
	void (*program_change)(ImuseMidiBackend* self, int channel, int program);
	void (*note_on)(ImuseMidiBackend* self, int channel, int note, int velocity);
	void (*note_off)(ImuseMidiBackend* self, int channel, int note);
	void (*control_change)(ImuseMidiBackend* self, int channel, unsigned int ctrl, int value);
	void (*pitch_bend)(ImuseMidiBackend* self, int channel, int wire14bit);

	/* ===== Audio render (audio thread) =====
	 *
	 * Render `frames` stereo float frames into `buf` (interleaved
	 * L,R,L,R,...). Overwrites buf. NULL-safe (writes silence). */
	void (*render_float)(ImuseMidiBackend* self, float* buf, int frames);
};

/* ===== Engine-side dispatch =====
 *
 * NULL-safe wrappers that fan out to im->midi.backend. The slots /
 * mix modules only call these — they don't see the vtable. */
void ImMidi_ProgramChange(imuse_t* im, int channel, int program);
void ImMidi_NoteOn(imuse_t* im, int channel, int note, int velocity);
void ImMidi_NoteOff(imuse_t* im, int channel, int note);
void ImMidi_ControlChange(imuse_t* im, int channel, unsigned int ctrl, int value);
void ImMidi_PitchBend(imuse_t* im, int channel, int wire14bit);
void ImMidi_RenderFloat(imuse_t* im, float* buf, int frames);

/* Release a backend handle via its release() vtable slot. Internal
 * helper used by imuse_create rollback and imuse_destroy; not
 * exposed to the host (lifetime is library-owned). NULL-safe. */
void ImMidi_BackendRelease(ImuseMidiBackend* backend);

#endif /* LIBIMUSE_INTERNAL_MIDI_BACKEND_H */
